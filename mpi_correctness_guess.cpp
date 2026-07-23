#include "PCFG.h"
#include "md5.h"

#include <mpi.h>
#include <omp.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;
using namespace chrono;

namespace
{
const int TAG_WORK = 1;
const int TAG_STOP = 2;
const int TAG_DONE = 3;

const int REPORT_INTERVAL = 100000;
const int DEFAULT_HASH_BATCH_THRESHOLD = 1000000;
const int MIN_ADAPTIVE_HASH_BATCH_THRESHOLD = 250000;
const int MAX_ADAPTIVE_HASH_BATCH_THRESHOLD = 2000000;
const int HASH_BATCH_PER_WORKER = 250000;
const int GENERATE_LIMIT = 10000000;
const int HASH_OPENMP_THRESHOLD = 8192;
const int TEST_PASSWORD_LIMIT = 1000000;
const int DIGEST_BYTES = 16;

int get_hash_batch_threshold(int world_size)
{
    const char *env_value = getenv("HASH_BATCH_SIZE");
    if (env_value != nullptr && env_value[0] != '\0')
    {
        char *end = nullptr;
        long parsed = strtol(env_value, &end, 10);
        if (end != env_value && *end == '\0' && parsed > 0 && parsed <= INT_MAX)
        {
            return static_cast<int>(parsed);
        }

        cerr << "Ignoring invalid HASH_BATCH_SIZE=" << env_value
             << "; using adaptive default." << endl;
    }

    if (world_size <= 1)
    {
        return DEFAULT_HASH_BATCH_THRESHOLD;
    }

    int worker_count = world_size - 1;
    int adaptive_threshold = HASH_BATCH_PER_WORKER * worker_count;
    return max(MIN_ADAPTIVE_HASH_BATCH_THRESHOLD,
               min(MAX_ADAPTIVE_HASH_BATCH_THRESHOLD, adaptive_threshold));
}

struct HashStats
{
    int processed;
    int cracked;
};

struct PTSliceTask
{
    PT pt;
    int value_begin = 0;
    int value_end = 0;
};

void append_int32(vector<char> &buffer, int value)
{
    const char *raw = reinterpret_cast<const char *>(&value);
    buffer.insert(buffer.end(), raw, raw + sizeof(value));
}

void append_int64(vector<char> &buffer, int64_t value)
{
    const char *raw = reinterpret_cast<const char *>(&value);
    buffer.insert(buffer.end(), raw, raw + sizeof(value));
}

int read_int32(const vector<char> &buffer, size_t &offset)
{
    int value = 0;
    memcpy(&value, buffer.data() + offset, sizeof(value));
    offset += sizeof(value);
    return value;
}

int64_t read_int64(const vector<char> &buffer, size_t &offset)
{
    int64_t value = 0;
    memcpy(&value, buffer.data() + offset, sizeof(value));
    offset += sizeof(value);
    return value;
}

void append_string(vector<char> &buffer, const string &value)
{
    append_int64(buffer, static_cast<int64_t>(value.size()));
    buffer.insert(buffer.end(), value.begin(), value.end());
}

string read_string(const vector<char> &buffer, size_t &offset)
{
    int64_t length = read_int64(buffer, offset);
    string value(buffer.data() + offset, static_cast<size_t>(length));
    offset += static_cast<size_t>(length);
    return value;
}

string digest_from_state(const bit32 state[4])
{
    return string(reinterpret_cast<const char *>(state), DIGEST_BYTES);
}

void append_pt(vector<char> &buffer, const PT &pt)
{
    append_int32(buffer, pt.pivot);
    append_int32(buffer, static_cast<int>(pt.content.size()));
    for (const segment &seg : pt.content)
    {
        append_int32(buffer, seg.type);
        append_int32(buffer, seg.length);
    }

    append_int32(buffer, static_cast<int>(pt.curr_indices.size()));
    for (int value : pt.curr_indices)
    {
        append_int32(buffer, value);
    }

    append_int32(buffer, static_cast<int>(pt.max_indices.size()));
    for (int value : pt.max_indices)
    {
        append_int32(buffer, value);
    }
}

PT read_pt(const vector<char> &buffer, size_t &offset)
{
    PT pt;
    pt.pivot = read_int32(buffer, offset);

    int content_count = read_int32(buffer, offset);
    for (int i = 0; i < content_count; i += 1)
    {
        int type = read_int32(buffer, offset);
        int length = read_int32(buffer, offset);
        pt.content.emplace_back(type, length);
    }

    int curr_count = read_int32(buffer, offset);
    for (int i = 0; i < curr_count; i += 1)
    {
        pt.curr_indices.emplace_back(read_int32(buffer, offset));
    }

    int max_count = read_int32(buffer, offset);
    for (int i = 0; i < max_count; i += 1)
    {
        pt.max_indices.emplace_back(read_int32(buffer, offset));
    }
    return pt;
}

void append_segments(vector<char> &buffer,
                     const vector<segment> &segments,
                     const unordered_map<int, int> &segment_freq)
{
    append_int32(buffer, static_cast<int>(segments.size()));
    for (int seg_id = 0; seg_id < static_cast<int>(segments.size()); seg_id += 1)
    {
        const segment &seg = segments[seg_id];
        append_int32(buffer, seg.type);
        append_int32(buffer, seg.length);
        append_int32(buffer, segment_freq.at(seg_id));
        append_int32(buffer, static_cast<int>(seg.values.size()));
        for (const auto &value_id : seg.values)
        {
            append_string(buffer, value_id.first);
            append_int32(buffer, seg.freqs.at(value_id.second));
        }
    }
}

void read_segments(const vector<char> &buffer,
                   size_t &offset,
                   vector<segment> &segments,
                   unordered_map<int, int> &segment_freq)
{
    int segment_count = read_int32(buffer, offset);
    for (int seg_id = 0; seg_id < segment_count; seg_id += 1)
    {
        int type = read_int32(buffer, offset);
        int length = read_int32(buffer, offset);
        int freq = read_int32(buffer, offset);
        int value_count = read_int32(buffer, offset);

        segment seg(type, length);
        for (int i = 0; i < value_count; i += 1)
        {
            string value = read_string(buffer, offset);
            int value_freq = read_int32(buffer, offset);
            int value_id = static_cast<int>(seg.values.size());
            seg.values.emplace(value, value_id);
            seg.freqs[value_id] = value_freq;
        }

        segments.emplace_back(std::move(seg));
        segment_freq[seg_id] = freq;
    }
}

vector<char> serialize_model(const model &m)
{
    vector<char> buffer;
    append_int32(buffer, m.total_preterm);

    append_int32(buffer, static_cast<int>(m.preterminals.size()));
    for (int pt_id = 0; pt_id < static_cast<int>(m.preterminals.size()); pt_id += 1)
    {
        append_pt(buffer, m.preterminals[pt_id]);
        append_int32(buffer, m.preterm_freq.at(pt_id));
    }

    append_segments(buffer, m.letters, m.letters_freq);
    append_segments(buffer, m.digits, m.digits_freq);
    append_segments(buffer, m.symbols, m.symbols_freq);
    return buffer;
}

model deserialize_model(const vector<char> &buffer)
{
    model m;
    size_t offset = 0;
    m.total_preterm = read_int32(buffer, offset);

    int pt_count = read_int32(buffer, offset);
    for (int pt_id = 0; pt_id < pt_count; pt_id += 1)
    {
        PT pt = read_pt(buffer, offset);
        int freq = read_int32(buffer, offset);
        int id = m.GetNextPretermID();
        m.preterminals.emplace_back(std::move(pt));
        m.preterm_freq[id] = freq;
    }

    read_segments(buffer, offset, m.letters, m.letters_freq);
    m.letters_id = static_cast<int>(m.letters.size()) - 1;
    read_segments(buffer, offset, m.digits, m.digits_freq);
    m.digits_id = static_cast<int>(m.digits.size()) - 1;
    read_segments(buffer, offset, m.symbols, m.symbols_freq);
    m.symbols_id = static_cast<int>(m.symbols.size()) - 1;
    return m;
}

void broadcast_trained_model(model &m, int rank)
{
    vector<char> buffer;
    int byte_count = 0;

    if (rank == 0)
    {
        buffer = serialize_model(m);
        if (buffer.size() > static_cast<size_t>(INT_MAX))
        {
            cerr << "Serialized correctness model is too large for MPI_INT broadcast." << endl;
            MPI_Abort(MPI_COMM_WORLD, 12);
        }
        byte_count = static_cast<int>(buffer.size());
    }

    MPI_Bcast(&byte_count, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (rank != 0)
    {
        buffer.resize(byte_count);
    }
    if (byte_count > 0)
    {
        MPI_Bcast(buffer.data(), byte_count, MPI_CHAR, 0, MPI_COMM_WORLD);
    }

    if (rank != 0)
    {
        m = deserialize_model(buffer);
        m.order();
    }
}

void append_pt_slice_task(vector<char> &buffer, const PTSliceTask &task)
{
    append_pt(buffer, task.pt);
    append_int32(buffer, task.value_begin);
    append_int32(buffer, task.value_end);
}

PTSliceTask read_pt_slice_task(const vector<char> &buffer, size_t &offset)
{
    PTSliceTask task;
    task.pt = read_pt(buffer, offset);
    task.value_begin = read_int32(buffer, offset);
    task.value_end = read_int32(buffer, offset);
    return task;
}

vector<char> pack_pt_slice_batch(const vector<PTSliceTask> &tasks)
{
    vector<char> buffer;
    for (const PTSliceTask &task : tasks)
    {
        append_pt_slice_task(buffer, task);
    }
    return buffer;
}

vector<PTSliceTask> unpack_pt_slice_batch(int count, const vector<char> &buffer)
{
    vector<PTSliceTask> tasks;
    tasks.reserve(count);

    size_t offset = 0;
    for (int i = 0; i < count; i += 1)
    {
        tasks.emplace_back(read_pt_slice_task(buffer, offset));
    }
    return tasks;
}

vector<char> pack_digest_set(const unordered_set<string> &digests)
{
    vector<char> buffer;
    buffer.reserve(digests.size() * DIGEST_BYTES);
    for (const string &digest : digests)
    {
        if (digest.size() != DIGEST_BYTES)
        {
            cerr << "Unexpected digest size while packing target hashes." << endl;
            MPI_Abort(MPI_COMM_WORLD, 6);
        }
        buffer.insert(buffer.end(), digest.begin(), digest.end());
    }
    return buffer;
}

unordered_set<string> unpack_digest_set(const vector<char> &buffer)
{
    if (buffer.size() % DIGEST_BYTES != 0)
    {
        cerr << "Unexpected target hash buffer size." << endl;
        MPI_Abort(MPI_COMM_WORLD, 7);
    }

    unordered_set<string> digests;
    digests.reserve(buffer.size() / DIGEST_BYTES);
    for (size_t offset = 0; offset < buffer.size(); offset += DIGEST_BYTES)
    {
        digests.emplace(buffer.data() + offset, DIGEST_BYTES);
    }
    return digests;
}

HashStats hash_and_match_span(const string *passwords, int count,
                              const unordered_set<string> &target_hashes)
{
    if (count == 0)
    {
        return {0, 0};
    }

    int thread_count = omp_get_max_threads();
    if (thread_count <= 1 || count < HASH_OPENMP_THRESHOLD)
    {
        bit32 (*states)[4] = new bit32[count][4];
        MD5Hash_SIMD(passwords, count, states);
        int cracked = 0;
        for (int i = 0; i < count; i += 1)
        {
            if (target_hashes.find(digest_from_state(states[i])) != target_hashes.end())
            {
                cracked += 1;
            }
        }
        delete[] states;
        return {count, cracked};
    }

    int cracked = 0;
#pragma omp parallel num_threads(thread_count)
    {
        int tid = omp_get_thread_num();
        int begin = count * tid / thread_count;
        int end = count * (tid + 1) / thread_count;
        int local_count = end - begin;
        int local_cracked = 0;

        if (local_count > 0)
        {
            bit32 (*states)[4] = new bit32[local_count][4];
            MD5Hash_SIMD(passwords + begin, local_count, states);
            for (int i = 0; i < local_count; i += 1)
            {
                if (target_hashes.find(digest_from_state(states[i])) != target_hashes.end())
                {
                    local_cracked += 1;
                }
            }
            delete[] states;
        }

#pragma omp atomic
        cracked += local_cracked;
    }

    return {count, cracked};
}

HashStats hash_password_range(const vector<string> &passwords, size_t begin, size_t end,
                              const unordered_set<string> &target_hashes)
{
    int count = static_cast<int>(end - begin);
    if (count == 0)
    {
        return {0, 0};
    }

    return hash_and_match_span(passwords.data() + begin, count, target_hashes);
}

void send_stop_to_workers(int world_size)
{
    int count = 0;
    for (int rank = 1; rank < world_size; rank += 1)
    {
        MPI_Send(&count, 1, MPI_INT, rank, TAG_STOP, MPI_COMM_WORLD);
    }
}

void send_password_batch(const vector<string> &passwords, size_t begin, size_t end, int dest)
{
    int count = static_cast<int>(end - begin);
    MPI_Send(&count, 1, MPI_INT, dest, TAG_WORK, MPI_COMM_WORLD);
    if (count == 0)
    {
        return;
    }

    vector<int> lengths(count);
    size_t total_chars = 0;
    for (int i = 0; i < count; i += 1)
    {
        const string &pw = passwords[begin + i];
        if (pw.size() > static_cast<size_t>(INT_MAX))
        {
            cerr << "Password length is too large for MPI_INT transfer." << endl;
            MPI_Abort(MPI_COMM_WORLD, 2);
        }
        lengths[i] = static_cast<int>(pw.size());
        total_chars += pw.size();
    }
    if (total_chars > static_cast<size_t>(INT_MAX))
    {
        cerr << "Password batch is too large for one MPI transfer." << endl;
        MPI_Abort(MPI_COMM_WORLD, 2);
    }

    vector<char> buffer;
    buffer.reserve(total_chars);
    for (int i = 0; i < count; i += 1)
    {
        const string &pw = passwords[begin + i];
        buffer.insert(buffer.end(), pw.begin(), pw.end());
    }

    MPI_Send(lengths.data(), count, MPI_INT, dest, TAG_WORK, MPI_COMM_WORLD);
    if (!buffer.empty())
    {
        MPI_Send(buffer.data(), static_cast<int>(buffer.size()), MPI_CHAR, dest, TAG_WORK, MPI_COMM_WORLD);
    }
}

vector<string> receive_password_batch(int count)
{
    vector<int> lengths(count);
    MPI_Recv(lengths.data(), count, MPI_INT, 0, TAG_WORK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    size_t total_chars = 0;
    for (int len : lengths)
    {
        if (len < 0)
        {
            cerr << "Received a negative password length." << endl;
            MPI_Abort(MPI_COMM_WORLD, 3);
        }
        total_chars += static_cast<size_t>(len);
    }

    vector<char> buffer(total_chars);
    if (!buffer.empty())
    {
        MPI_Recv(buffer.data(), static_cast<int>(buffer.size()), MPI_CHAR, 0, TAG_WORK,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    vector<string> passwords;
    passwords.reserve(count);
    size_t offset = 0;
    for (int len : lengths)
    {
        if (len == 0)
        {
            passwords.emplace_back();
        }
        else
        {
            passwords.emplace_back(buffer.data() + offset, static_cast<size_t>(len));
        }
        offset += static_cast<size_t>(len);
    }
    return passwords;
}

void send_pt_slice_batch(const vector<PTSliceTask> &tasks, int dest)
{
    int count = static_cast<int>(tasks.size());
    MPI_Send(&count, 1, MPI_INT, dest, TAG_WORK, MPI_COMM_WORLD);
    if (count == 0)
    {
        return;
    }

    vector<char> buffer = pack_pt_slice_batch(tasks);
    if (buffer.size() > static_cast<size_t>(INT_MAX))
    {
        cerr << "PT slice correctness batch is too large for one MPI transfer." << endl;
        MPI_Abort(MPI_COMM_WORLD, 13);
    }

    int byte_count = static_cast<int>(buffer.size());
    MPI_Send(&byte_count, 1, MPI_INT, dest, TAG_WORK, MPI_COMM_WORLD);
    if (byte_count > 0)
    {
        MPI_Send(buffer.data(), byte_count, MPI_CHAR, dest, TAG_WORK, MPI_COMM_WORLD);
    }
}

vector<PTSliceTask> receive_pt_slice_batch(int count)
{
    int byte_count = 0;
    MPI_Recv(&byte_count, 1, MPI_INT, 0, TAG_WORK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    if (byte_count < 0)
    {
        cerr << "Received a negative PT slice correctness buffer size." << endl;
        MPI_Abort(MPI_COMM_WORLD, 14);
    }

    vector<char> buffer(byte_count);
    if (byte_count > 0)
    {
        MPI_Recv(buffer.data(), byte_count, MPI_CHAR, 0, TAG_WORK,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
    return unpack_pt_slice_batch(count, buffer);
}

int estimate_pt_guess_count(const PT &pt)
{
    if (pt.max_indices.empty())
    {
        return 0;
    }
    return pt.max_indices.back();
}

PT pop_next_pt_task(PriorityQueue &q)
{
    PT task = q.priority.front();

    std::pop_heap(q.priority.begin(), q.priority.end(),
                  [](const PT &a, const PT &b) { return a.prob < b.prob; });
    q.priority.pop_back();

    vector<PT> new_pts = task.NewPTs();
    for (PT pt : new_pts)
    {
        q.CalProb(pt);
        q.priority.push_back(pt);
        std::push_heap(q.priority.begin(), q.priority.end(),
                       [](const PT &a, const PT &b) { return a.prob < b.prob; });
    }

    return task;
}

segment *find_model_segment(model &m, const segment &seg)
{
    if (seg.type == 1)
    {
        return &m.letters[m.FindLetter(seg)];
    }
    if (seg.type == 2)
    {
        return &m.digits[m.FindDigit(seg)];
    }
    if (seg.type == 3)
    {
        return &m.symbols[m.FindSymbol(seg)];
    }
    return nullptr;
}

void generate_pt_slice(PriorityQueue &q, const PTSliceTask &task)
{
    PT pt = task.pt;
    if (pt.content.empty() || pt.max_indices.empty())
    {
        return;
    }

    int max_value_count = pt.max_indices.back();
    int value_begin = max(0, min(task.value_begin, max_value_count));
    int value_end = max(value_begin, min(task.value_end, max_value_count));
    int guess_count = value_end - value_begin;
    if (guess_count <= 0)
    {
        return;
    }

    string prefix;
    if (pt.content.size() > 1)
    {
        int seg_idx = 0;
        for (int idx : pt.curr_indices)
        {
            if (seg_idx >= static_cast<int>(pt.content.size()) - 1)
            {
                break;
            }

            segment *seg_values = find_model_segment(q.m, pt.content[seg_idx]);
            if (seg_values == nullptr)
            {
                cerr << "Invalid segment type in correctness PT slice prefix." << endl;
                MPI_Abort(MPI_COMM_WORLD, 15);
            }
            prefix += seg_values->ordered_values[idx];
            seg_idx += 1;
        }
    }

    segment *last_values = find_model_segment(q.m, pt.content.back());
    if (last_values == nullptr)
    {
        cerr << "Invalid segment type in correctness PT slice suffix." << endl;
        MPI_Abort(MPI_COMM_WORLD, 16);
    }

    size_t old_size = q.guesses.size();
    q.guesses.resize(old_size + guess_count);

    if (pt.content.size() == 1)
    {
        if (guess_count >= 200)
        {
#pragma omp parallel for schedule(static)
            for (int i = 0; i < guess_count; i += 1)
            {
                q.guesses[old_size + i] = last_values->ordered_values[value_begin + i];
            }
        }
        else
        {
            for (int i = 0; i < guess_count; i += 1)
            {
                q.guesses[old_size + i] = last_values->ordered_values[value_begin + i];
            }
        }
    }
    else
    {
        if (guess_count >= 200)
        {
#pragma omp parallel for schedule(static)
            for (int i = 0; i < guess_count; i += 1)
            {
                q.guesses[old_size + i] = prefix + last_values->ordered_values[value_begin + i];
            }
        }
        else
        {
            for (int i = 0; i < guess_count; i += 1)
            {
                q.guesses[old_size + i] = prefix + last_values->ordered_values[value_begin + i];
            }
        }
    }

    q.total_guesses += guess_count;
}

HashStats hash_and_match_pt_slices(const vector<PTSliceTask> &tasks,
                                   const model &trained_model,
                                   const unordered_set<string> &target_hashes)
{
    if (tasks.empty())
    {
        return {0, 0};
    }

    PriorityQueue local_q;
    local_q.m = trained_model;
    for (const PTSliceTask &task : tasks)
    {
        generate_pt_slice(local_q, task);
    }

    return hash_and_match_span(local_q.guesses.data(),
                               static_cast<int>(local_q.guesses.size()),
                               target_hashes);
}

HashStats hash_batch_mpi(const vector<string> &passwords, int world_size,
                         const unordered_set<string> &target_hashes)
{
    if (passwords.empty())
    {
        return {0, 0};
    }

    if (world_size == 1)
    {
        return hash_password_range(passwords, 0, passwords.size(), target_hashes);
    }

    size_t total = passwords.size();
    int worker_count = world_size - 1;
    for (int worker = 1; worker < world_size; worker += 1)
    {
        int worker_index = worker - 1;
        size_t begin = total * worker_index / worker_count;
        size_t end = total * (worker_index + 1) / worker_count;
        send_password_batch(passwords, begin, end, worker);
    }

    HashStats total_stats = {0, 0};
    for (int worker = 1; worker < world_size; worker += 1)
    {
        int stats_buffer[2] = {0, 0};
        MPI_Recv(stats_buffer, 2, MPI_INT, worker, TAG_DONE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        total_stats.processed += stats_buffer[0];
        total_stats.cracked += stats_buffer[1];
    }
    return total_stats;
}

unordered_set<string> load_target_hashes()
{
    unordered_set<string> target_hashes;
    ifstream test_data("/guessdata/Rockyou-singleLined-full.txt");
    string pw;
    int test_count = 0;
    while (test_data >> pw)
    {
        bit32 state[4];
        MD5Hash(pw, state);
        target_hashes.insert(digest_from_state(state));
        test_count += 1;
        if (test_count >= TEST_PASSWORD_LIMIT)
        {
            break;
        }
    }
    return target_hashes;
}

unordered_set<string> broadcast_target_hashes(int rank)
{
    unordered_set<string> target_hashes;
    vector<char> buffer;
    int byte_count = 0;

    if (rank == 0)
    {
        target_hashes = load_target_hashes();
        buffer = pack_digest_set(target_hashes);
        if (buffer.size() > static_cast<size_t>(INT_MAX))
        {
            cerr << "Target hash set is too large for MPI_INT transfer." << endl;
            MPI_Abort(MPI_COMM_WORLD, 8);
        }
        byte_count = static_cast<int>(buffer.size());
    }

    MPI_Bcast(&byte_count, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (rank != 0)
    {
        buffer.resize(byte_count);
    }
    if (byte_count > 0)
    {
        MPI_Bcast(buffer.data(), byte_count, MPI_CHAR, 0, MPI_COMM_WORLD);
    }

    if (rank != 0)
    {
        target_hashes = unpack_digest_set(buffer);
    }
    return target_hashes;
}

void worker_loop(const unordered_set<string> &target_hashes, const model &trained_model)
{
    HashStats stats = {0, 0};

    while (true)
    {
        int stats_buffer[2] = {stats.processed, stats.cracked};
        MPI_Send(stats_buffer, 2, MPI_INT, 0, TAG_DONE, MPI_COMM_WORLD);
        stats = {0, 0};

        int count = 0;
        MPI_Status status;
        MPI_Recv(&count, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

        if (status.MPI_TAG == TAG_STOP)
        {
            break;
        }
        if (status.MPI_TAG != TAG_WORK || count < 0)
        {
            cerr << "Worker received an invalid MPI task." << endl;
            MPI_Abort(MPI_COMM_WORLD, 4);
        }

        if (count > 0)
        {
            vector<PTSliceTask> tasks = receive_pt_slice_batch(count);
            stats = hash_and_match_pt_slices(tasks, trained_model, target_hashes);
        }
    }
}

HashStats hash_pt_slices_locally(const vector<PTSliceTask> &tasks,
                                 const model &trained_model,
                                 const unordered_set<string> &target_hashes)
{
    return hash_and_match_pt_slices(tasks, trained_model, target_hashes);
}

int master_main(int world_size, model trained_model, double time_train,
                const unordered_set<string> &target_hashes)
{
    double time_hash = 0;
    double time_guess = 0;
    int cracked = 0;
    int hash_batch_threshold = get_hash_batch_threshold(world_size);

    cout << "Hash batch threshold:" << hash_batch_threshold << endl;

    PriorityQueue q;
    q.m = std::move(trained_model);
    q.init();
    cout << "here" << endl;

    int curr_num = 0;
    int history = 0;
    auto start = system_clock::now();
    int worker_task_target = hash_batch_threshold;
    if (world_size > 1)
    {
        worker_task_target = max(1, hash_batch_threshold / (world_size - 1));
    }
    cout << "Worker-driven PT task target:" << worker_task_target << endl;

    bool has_current_slice_pt = false;
    PT current_slice_pt;
    int current_slice_begin = 0;
    int current_slice_end = 0;

    auto load_next_slice_pt = [&]() {
        if (q.priority.empty())
        {
            return false;
        }

        current_slice_pt = pop_next_pt_task(q);
        current_slice_begin = 0;
        current_slice_end = estimate_pt_guess_count(current_slice_pt);
        has_current_slice_pt = current_slice_end > 0;
        return has_current_slice_pt;
    };

    auto make_next_slice_batch = [&](vector<PTSliceTask> &tasks, int &guess_count) {
        tasks.clear();
        guess_count = 0;

        while (history + guess_count < GENERATE_LIMIT)
        {
            if (!has_current_slice_pt && !load_next_slice_pt())
            {
                break;
            }

            int remaining_limit = GENERATE_LIMIT - history - guess_count;
            int remaining_target = worker_task_target - guess_count;
            if (remaining_target <= 0)
            {
                break;
            }

            int slice_end = min(current_slice_end,
                                current_slice_begin + min(remaining_target, remaining_limit));
            if (slice_end <= current_slice_begin)
            {
                has_current_slice_pt = false;
                continue;
            }

            PTSliceTask task;
            task.pt = current_slice_pt;
            task.value_begin = current_slice_begin;
            task.value_end = slice_end;
            tasks.emplace_back(std::move(task));
            guess_count += slice_end - current_slice_begin;
            current_slice_begin = slice_end;

            if (current_slice_begin >= current_slice_end)
            {
                has_current_slice_pt = false;
            }

            if (guess_count >= worker_task_target)
            {
                break;
            }
        }
    };

    auto print_progress = [&]() {
        while (history - curr_num >= REPORT_INTERVAL)
        {
            curr_num += REPORT_INTERVAL;
            cout << "Guesses generated: " << curr_num << endl;
        }
    };

    if (world_size == 1)
    {
        while ((has_current_slice_pt || !q.priority.empty()) && history < GENERATE_LIMIT)
        {
            vector<PTSliceTask> tasks;
            int guess_count = 0;
            make_next_slice_batch(tasks, guess_count);
            if (tasks.empty())
            {
                break;
            }

            auto start_hash = system_clock::now();
            HashStats stats = hash_pt_slices_locally(tasks, q.m, target_hashes);
            auto end_hash = system_clock::now();
            auto duration = duration_cast<microseconds>(end_hash - start_hash);
            time_hash += double(duration.count()) * microseconds::period::num / microseconds::period::den;

            if (stats.processed != guess_count)
            {
                cerr << "Local correctness PT slice processed count mismatch." << endl;
                MPI_Abort(MPI_COMM_WORLD, 17);
            }
            cracked += stats.cracked;
            history += guess_count;
            print_progress();
        }
    }
    else
    {
        int active_workers = world_size - 1;
        int completed_guesses = 0;
        bool no_more_tasks = false;

        auto receive_worker_done = [&](int source, HashStats &stats, MPI_Status &status) {
            auto start_hash_wait = system_clock::now();
            int stats_buffer[2] = {0, 0};
            MPI_Recv(stats_buffer, 2, MPI_INT, source, TAG_DONE, MPI_COMM_WORLD, &status);
            auto end_hash_wait = system_clock::now();
            auto duration = duration_cast<microseconds>(end_hash_wait - start_hash_wait);
            time_hash += double(duration.count()) * microseconds::period::num / microseconds::period::den;

            stats.processed = stats_buffer[0];
            stats.cracked = stats_buffer[1];
            if (stats.processed < 0 || stats.cracked < 0)
            {
                cerr << "Master received invalid correctness stats." << endl;
                MPI_Abort(MPI_COMM_WORLD, 18);
            }
            completed_guesses += stats.processed;
            cracked += stats.cracked;
        };

        auto send_next_or_stop = [&](int worker) {
            if (!no_more_tasks)
            {
                vector<PTSliceTask> tasks;
                int guess_count = 0;
                make_next_slice_batch(tasks, guess_count);

                if (!tasks.empty())
                {
                    auto start_hash_dispatch = system_clock::now();
                    send_pt_slice_batch(tasks, worker);
                    auto end_hash_dispatch = system_clock::now();
                    auto duration = duration_cast<microseconds>(end_hash_dispatch - start_hash_dispatch);
                    time_hash += double(duration.count()) * microseconds::period::num / microseconds::period::den;

                    history += guess_count;
                    print_progress();
                }
                else
                {
                    no_more_tasks = true;
                }
            }

            if (no_more_tasks)
            {
                int count = 0;
                MPI_Send(&count, 1, MPI_INT, worker, TAG_STOP, MPI_COMM_WORLD);
                active_workers -= 1;
            }
        };

        for (int worker = 1; worker < world_size; worker += 1)
        {
            HashStats stats = {0, 0};
            MPI_Status status;
            receive_worker_done(worker, stats, status);
            send_next_or_stop(worker);
        }

        while (active_workers > 0)
        {
            HashStats stats = {0, 0};
            MPI_Status status;
            receive_worker_done(MPI_ANY_SOURCE, stats, status);
            send_next_or_stop(status.MPI_SOURCE);
        }

        if (completed_guesses != history)
        {
            cerr << "MPI correctness PT slice processed count mismatch. dispatched="
                 << history << " completed=" << completed_guesses << endl;
            MPI_Abort(MPI_COMM_WORLD, 19);
        }
    }

    auto end = system_clock::now();
    auto duration = duration_cast<microseconds>(end - start);
    time_guess = double(duration.count()) * microseconds::period::num / microseconds::period::den;
    cout << "Guess time:" << time_guess - time_hash << "seconds" << endl;
    cout << "Hash time:" << time_hash << "seconds" << endl;
    cout << "Train time:" << time_train << "seconds" << endl;
    cout << "Cracked:" << cracked << endl;
    return 0;
}
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int rank = 0;
    int world_size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    unordered_set<string> target_hashes = broadcast_target_hashes(rank);

    model trained_model;
    auto start_train = system_clock::now();
    if (rank == 0)
    {
        cout << "Training..." << endl;
        trained_model.train("/guessdata/Rockyou-singleLined-full.txt");
        trained_model.order();
    }
    broadcast_trained_model(trained_model, rank);
    MPI_Barrier(MPI_COMM_WORLD);
    auto end_train = system_clock::now();
    auto duration_train = duration_cast<microseconds>(end_train - start_train);
    double time_train = double(duration_train.count()) * microseconds::period::num / microseconds::period::den;

    int exit_code = 0;
    if (rank == 0)
    {
        exit_code = master_main(world_size, std::move(trained_model), time_train, target_hashes);
    }
    else
    {
        worker_loop(target_hashes, trained_model);
    }

    MPI_Finalize();
    return exit_code;
}
