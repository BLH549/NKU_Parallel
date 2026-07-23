#include "PCFG.h"
#include "md5.h"

#include <mpi.h>
#include <omp.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace std;
using namespace chrono;

namespace
{
const int TAG_WORK = 1;
const int TAG_STOP = 2;
const int TAG_DONE = 3;
const int TAG_PROFILE = 4;
const int TAG_TRAIN_MODEL_SIZE = 10;
const int TAG_TRAIN_MODEL_DATA = 11;

const int REPORT_INTERVAL = 100000;
const int DEFAULT_HASH_BATCH_THRESHOLD = 1000000;
const int MIN_ADAPTIVE_HASH_BATCH_THRESHOLD = 250000;
const int MAX_ADAPTIVE_HASH_BATCH_THRESHOLD = 2000000;
const int HASH_BATCH_PER_WORKER = 250000;
const int GENERATE_LIMIT = 10000000;
const int HASH_OPENMP_THRESHOLD = 8192;
const int TRAIN_PROGRESS_INTERVAL = 10000;
const int TRAIN_LINE_LIMIT = 3000000;

double elapsed_seconds(system_clock::time_point begin, system_clock::time_point end)
{
    auto duration = duration_cast<microseconds>(end - begin);
    return double(duration.count()) * microseconds::period::num / microseconds::period::den;
}

struct MasterProfile
{
    double queue_init_time = 0;
    double pt_pop_time = 0;
    double dispatch_send_time = 0;
    double wait_time = 0;
    int dispatch_batches = 0;
    int dispatched_pt_tasks = 0;
    int dispatched_guesses = 0;
};

struct WorkerProfile
{
    double wait_for_task_time = 0;
    double recv_task_time = 0;
    double clear_time = 0;
    double generate_time = 0;
    double hash_time = 0;
    double send_done_time = 0;
    double work_total_time = 0;
    double unaccounted_work_time = 0;
    int batches = 0;
    int pt_tasks = 0;
    int processed_guesses = 0;
};

struct PTSliceTask
{
    PT pt;
    int value_begin = 0;
    int value_end = 0;
};

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

void merge_segment_values(segment &dst, const segment &src)
{
    for (const auto &value_id : src.values)
    {
        const string &value = value_id.first;
        int src_id = value_id.second;
        int freq = src.freqs.at(src_id);

        auto dst_iter = dst.values.find(value);
        if (dst_iter == dst.values.end())
        {
            int dst_id = static_cast<int>(dst.values.size());
            dst.values.emplace(value, dst_id);
            dst.freqs[dst_id] = freq;
        }
        else
        {
            dst.freqs[dst_iter->second] += freq;
        }
    }
}

void merge_segments(vector<segment> &dst_segments,
                    unordered_map<int, int> &dst_freq,
                    const vector<segment> &src_segments,
                    const unordered_map<int, int> &src_freq,
                    int (model::*find_func)(const segment &),
                    model &dst_model,
                    int (model::*next_id_func)())
{
    for (int src_id = 0; src_id < static_cast<int>(src_segments.size()); src_id += 1)
    {
        const segment &src_seg = src_segments[src_id];
        int dst_id = (dst_model.*find_func)(src_seg);

        if (dst_id == -1)
        {
            dst_id = (dst_model.*next_id_func)();
            dst_segments.emplace_back(src_seg.type, src_seg.length);
            dst_freq[dst_id] = 0;
        }

        dst_freq[dst_id] += src_freq.at(src_id);
        merge_segment_values(dst_segments[dst_id], src_seg);
    }
}

void merge_model(model &dst, const model &src)
{
    dst.total_preterm += src.total_preterm;

    for (int src_id = 0; src_id < static_cast<int>(src.preterminals.size()); src_id += 1)
    {
        const PT &src_pt = src.preterminals[src_id];
        int dst_id = dst.FindPT(src_pt);

        if (dst_id == -1)
        {
            dst_id = dst.GetNextPretermID();
            dst.preterminals.emplace_back(src_pt);
            dst.preterm_freq[dst_id] = 0;
        }

        dst.preterm_freq[dst_id] += src.preterm_freq.at(src_id);
    }

    merge_segments(dst.letters, dst.letters_freq, src.letters, src.letters_freq,
                   &model::FindLetter, dst, &model::GetNextLettersID);
    merge_segments(dst.digits, dst.digits_freq, src.digits, src.digits_freq,
                   &model::FindDigit, dst, &model::GetNextDigitsID);
    merge_segments(dst.symbols, dst.symbols_freq, src.symbols, src.symbols_freq,
                   &model::FindSymbol, dst, &model::GetNextSymbolsID);
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
            cerr << "Serialized merged model is too large for MPI_INT broadcast." << endl;
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
        m = deserialize_model(buffer);
        m.order();
    }
}

model train_local_shard(const string &path, int rank, int world_size)
{
    string pw;
    ifstream train_set(path);
    int lines = 0;
    vector<string> passwords;

    while (train_set >> pw)
    {
        lines += 1;
        if (lines % TRAIN_PROGRESS_INTERVAL == 0 && lines > TRAIN_LINE_LIMIT)
        {
            break;
        }
        if ((lines - 1) % world_size == rank)
        {
            passwords.emplace_back(pw);
        }
    }

    int thread_count = omp_get_max_threads();
    if (thread_count < 1)
    {
        thread_count = 1;
    }
    if (passwords.size() < static_cast<size_t>(thread_count))
    {
        thread_count = passwords.empty() ? 1 : static_cast<int>(passwords.size());
    }

    vector<model> partial_models(thread_count);

#pragma omp parallel num_threads(thread_count)
    {
        int tid = omp_get_thread_num();
        size_t begin = passwords.size() * tid / thread_count;
        size_t end = passwords.size() * (tid + 1) / thread_count;

        for (size_t i = begin; i < end; i += 1)
        {
            partial_models[tid].parse(passwords[i]);
        }
    }

    model local_model;
    for (const model &partial : partial_models)
    {
        merge_model(local_model, partial);
    }
    return local_model;
}

model train_model_mpi(const string &path, int rank, int world_size)
{
    if (rank == 0)
    {
        cout << "Training..." << endl;
        cout << "MPI training phase 1: each rank parses a disjoint line shard..." << endl;
    }

    model local_model = train_local_shard(path, rank, world_size);

    if (rank == 0)
    {
        cout << "MPI training phase 2: merging rank-local models..." << endl;
        model merged_model;
        merge_model(merged_model, local_model);

        for (int src = 1; src < world_size; src += 1)
        {
            int byte_count = 0;
            MPI_Recv(&byte_count, 1, MPI_INT, src, TAG_TRAIN_MODEL_SIZE,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            vector<char> buffer(byte_count);
            if (byte_count > 0)
            {
                MPI_Recv(buffer.data(), byte_count, MPI_CHAR, src, TAG_TRAIN_MODEL_DATA,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
            model remote_model = deserialize_model(buffer);
            merge_model(merged_model, remote_model);
        }

        merged_model.order();
        return merged_model;
    }

    vector<char> buffer = serialize_model(local_model);
    if (buffer.size() > static_cast<size_t>(INT_MAX))
    {
        cerr << "Serialized local training model is too large for MPI_INT transfer." << endl;
        MPI_Abort(MPI_COMM_WORLD, 7);
    }
    int byte_count = static_cast<int>(buffer.size());
    MPI_Send(&byte_count, 1, MPI_INT, 0, TAG_TRAIN_MODEL_SIZE, MPI_COMM_WORLD);
    if (byte_count > 0)
    {
        MPI_Send(buffer.data(), byte_count, MPI_CHAR, 0, TAG_TRAIN_MODEL_DATA, MPI_COMM_WORLD);
    }
    return model();
}

bool run_md5_self_test()
{
    cout << "Testing MD5Hash correctness..." << endl;
    string test_pws[8] = {"123456", "password", "12345678", "qwerty",
                          "123456789", "12345", "1234", "111111"};
    string test_hashes[8] = {
        "e10adc3949ba59abbe56e057f20f883e",
        "5f4dcc3b5aa765d61d8327deb882cf99",
        "25d55ad283aa400af464c76d713c07ad",
        "d8578edf8458ce06fbc5bb76a58c5ca4",
        "25f9e794323b453885f5181f1b624d0b",
        "827ccb0eea8a706c4c34a16891f84e7b",
        "81dc9bdb52d04dc20036dbd8313ed055",
        "96e79218965eb72c92a549dd5a330112"};

    for (int i = 0; i < 8; i += 1)
    {
        bit32 state[4];
        MD5Hash(test_pws[i], state);
        stringstream ss;
        for (int j = 0; j < 4; j += 1)
        {
            ss << setw(8) << setfill('0') << hex << state[j];
        }
        if (ss.str() != test_hashes[i])
        {
            cout << "MD5Hash test failed for " << test_pws[i] << "!" << endl;
            cout << "Expected: " << test_hashes[i] << "\nGot:      " << ss.str() << endl;
            return false;
        }
    }

    cout << "MD5Hash test passed!" << endl; //请不要修改这一行
    return true;
}

void hash_password_span(const string *passwords, int count)
{
    if (count == 0)
    {
        return;
    }

    int thread_count = omp_get_max_threads();
    if (thread_count <= 1 || count < HASH_OPENMP_THRESHOLD)
    {
        bit32 (*states)[4] = new bit32[count][4];
        MD5Hash_SIMD(passwords, count, states);
        delete[] states;
        return;
    }

#pragma omp parallel num_threads(thread_count)
    {
        int tid = omp_get_thread_num();
        int begin = count * tid / thread_count;
        int end = count * (tid + 1) / thread_count;
        int local_count = end - begin;

        if (local_count > 0)
        {
            bit32 (*states)[4] = new bit32[local_count][4];
            MD5Hash_SIMD(passwords + begin, local_count, states);
            delete[] states;
        }
    }
}

void hash_passwords(const vector<string> &passwords)
{
    int count = static_cast<int>(passwords.size());
    if (count == 0)
    {
        return;
    }

    hash_password_span(passwords.data(), count);
}

int hash_password_range(const vector<string> &passwords, size_t begin, size_t end)
{
    int count = static_cast<int>(end - begin);
    if (count == 0)
    {
        return 0;
    }

    hash_password_span(passwords.data() + begin, count);
    return count;
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

int hash_batch_mpi(const vector<string> &passwords, int world_size)
{
    if (passwords.empty())
    {
        return 0;
    }

    if (world_size == 1)
    {
        hash_passwords(passwords);
        return static_cast<int>(passwords.size());
    }

    size_t total = passwords.size();
    for (int worker = 1; worker < world_size; worker += 1)
    {
        size_t begin = total * worker / world_size;
        size_t end = total * (worker + 1) / world_size;
        send_password_batch(passwords, begin, end, worker);
    }

    size_t master_begin = 0;
    size_t master_end = total / world_size;
    int processed_total = hash_password_range(passwords, master_begin, master_end);

    for (int worker = 1; worker < world_size; worker += 1)
    {
        int processed = 0;
        MPI_Recv(&processed, 1, MPI_INT, worker, TAG_DONE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        processed_total += processed;
    }
    return processed_total;
}

int dispatch_hash_batch_to_workers(const vector<string> &passwords, int world_size)
{
    if (passwords.empty())
    {
        return 0;
    }

    int worker_count = world_size - 1;
    size_t total = passwords.size();
    for (int worker = 1; worker < world_size; worker += 1)
    {
        int worker_index = worker - 1;
        size_t begin = total * worker_index / worker_count;
        size_t end = total * (worker_index + 1) / worker_count;
        send_password_batch(passwords, begin, end, worker);
    }

    return static_cast<int>(total);
}

int finish_hash_batch_from_workers(int world_size)
{
    int processed_total = 0;
    for (int worker = 1; worker < world_size; worker += 1)
    {
        int processed = 0;
        MPI_Recv(&processed, 1, MPI_INT, worker, TAG_DONE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        processed_total += processed;
    }
    return processed_total;
}

void send_worker_profile(const WorkerProfile &profile)
{
    double times[8] = {
        profile.wait_for_task_time,
        profile.recv_task_time,
        profile.clear_time,
        profile.generate_time,
        profile.hash_time,
        profile.send_done_time,
        profile.work_total_time,
        profile.unaccounted_work_time};
    int counts[3] = {
        profile.batches,
        profile.pt_tasks,
        profile.processed_guesses};

    MPI_Send(times, 8, MPI_DOUBLE, 0, TAG_PROFILE, MPI_COMM_WORLD);
    MPI_Send(counts, 3, MPI_INT, 0, TAG_PROFILE, MPI_COMM_WORLD);
}

void collect_worker_profiles(int world_size)
{
    if (world_size <= 1)
    {
        return;
    }

    WorkerProfile total;
    for (int worker = 1; worker < world_size; worker += 1)
    {
        double times[8] = {};
        int counts[3] = {};
        MPI_Recv(times, 8, MPI_DOUBLE, worker, TAG_PROFILE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(counts, 3, MPI_INT, worker, TAG_PROFILE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        cout << "Profile worker " << worker
             << " wait=" << times[0]
             << " recv=" << times[1]
             << " clear=" << times[2]
             << " generate=" << times[3]
             << " hash=" << times[4]
             << " send_done=" << times[5]
             << " work_total=" << times[6]
             << " unaccounted=" << times[7]
             << " batches=" << counts[0]
             << " pt_tasks=" << counts[1]
             << " processed=" << counts[2]
             << endl;

        total.wait_for_task_time += times[0];
        total.recv_task_time += times[1];
        total.clear_time += times[2];
        total.generate_time += times[3];
        total.hash_time += times[4];
        total.send_done_time += times[5];
        total.work_total_time += times[6];
        total.unaccounted_work_time += times[7];
        total.batches += counts[0];
        total.pt_tasks += counts[1];
        total.processed_guesses += counts[2];
    }

    int worker_count = world_size - 1;
    cout << "Profile worker avg wait=" << total.wait_for_task_time / worker_count
         << " recv=" << total.recv_task_time / worker_count
         << " clear=" << total.clear_time / worker_count
         << " generate=" << total.generate_time / worker_count
         << " hash=" << total.hash_time / worker_count
         << " send_done=" << total.send_done_time / worker_count
         << " work_total=" << total.work_total_time / worker_count
         << " unaccounted=" << total.unaccounted_work_time / worker_count
         << " batches=" << total.batches
         << " pt_tasks=" << total.pt_tasks
         << " processed=" << total.processed_guesses
         << endl;
}

int estimate_pt_guess_count(const PT &pt)
{
    if (pt.max_indices.empty())
    {
        return 0;
    }
    return pt.max_indices.back();
}

int estimate_pt_batch_guess_count(const vector<PT> &tasks)
{
    int total = 0;
    for (const PT &pt : tasks)
    {
        total += estimate_pt_guess_count(pt);
    }
    return total;
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

vector<char> pack_pt_batch(const vector<PT> &tasks)
{
    vector<char> buffer;
    for (const PT &pt : tasks)
    {
        append_pt(buffer, pt);
    }
    return buffer;
}

vector<PT> unpack_pt_batch(int count, const vector<char> &buffer)
{
    vector<PT> tasks;
    tasks.reserve(count);

    size_t offset = 0;
    for (int i = 0; i < count; i += 1)
    {
        tasks.emplace_back(read_pt(buffer, offset));
    }
    return tasks;
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

void send_pt_batch(const vector<PT> &tasks, int dest)
{
    int count = static_cast<int>(tasks.size());
    MPI_Send(&count, 1, MPI_INT, dest, TAG_WORK, MPI_COMM_WORLD);
    if (count == 0)
    {
        return;
    }

    vector<char> buffer = pack_pt_batch(tasks);
    if (buffer.size() > static_cast<size_t>(INT_MAX))
    {
        cerr << "PT task batch is too large for one MPI transfer." << endl;
        MPI_Abort(MPI_COMM_WORLD, 9);
    }

    int byte_count = static_cast<int>(buffer.size());
    MPI_Send(&byte_count, 1, MPI_INT, dest, TAG_WORK, MPI_COMM_WORLD);
    if (byte_count > 0)
    {
        MPI_Send(buffer.data(), byte_count, MPI_CHAR, dest, TAG_WORK, MPI_COMM_WORLD);
    }
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
        cerr << "PT slice task batch is too large for one MPI transfer." << endl;
        MPI_Abort(MPI_COMM_WORLD, 14);
    }

    int byte_count = static_cast<int>(buffer.size());
    MPI_Send(&byte_count, 1, MPI_INT, dest, TAG_WORK, MPI_COMM_WORLD);
    if (byte_count > 0)
    {
        MPI_Send(buffer.data(), byte_count, MPI_CHAR, dest, TAG_WORK, MPI_COMM_WORLD);
    }
}

vector<PT> receive_pt_batch(int count)
{
    int byte_count = 0;
    MPI_Recv(&byte_count, 1, MPI_INT, 0, TAG_WORK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    if (byte_count < 0)
    {
        cerr << "Received a negative PT task buffer size." << endl;
        MPI_Abort(MPI_COMM_WORLD, 10);
    }

    vector<char> buffer(byte_count);
    if (byte_count > 0)
    {
        MPI_Recv(buffer.data(), byte_count, MPI_CHAR, 0, TAG_WORK,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
    return unpack_pt_batch(count, buffer);
}

vector<PTSliceTask> receive_pt_slice_batch(int count)
{
    int byte_count = 0;
    MPI_Recv(&byte_count, 1, MPI_INT, 0, TAG_WORK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    if (byte_count < 0)
    {
        cerr << "Received a negative PT slice task buffer size." << endl;
        MPI_Abort(MPI_COMM_WORLD, 15);
    }

    vector<char> buffer(byte_count);
    if (byte_count > 0)
    {
        MPI_Recv(buffer.data(), byte_count, MPI_CHAR, 0, TAG_WORK,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
    return unpack_pt_slice_batch(count, buffer);
}

int dispatch_pt_batch_to_workers(const vector<PT> &tasks, int world_size)
{
    if (tasks.empty())
    {
        return 0;
    }

    int worker_count = world_size - 1;
    vector<vector<PT>> worker_tasks(worker_count);
    vector<int> worker_loads(worker_count, 0);

    for (const PT &pt : tasks)
    {
        int best_worker = 0;
        for (int i = 1; i < worker_count; i += 1)
        {
            if (worker_loads[i] < worker_loads[best_worker])
            {
                best_worker = i;
            }
        }
        worker_tasks[best_worker].emplace_back(pt);
        worker_loads[best_worker] += estimate_pt_guess_count(pt);
    }

    for (int worker = 1; worker < world_size; worker += 1)
    {
        send_pt_batch(worker_tasks[worker - 1], worker);
    }

    return estimate_pt_batch_guess_count(tasks);
}

int hash_pt_tasks_locally(const vector<PT> &tasks, const model &trained_model)
{
    if (tasks.empty())
    {
        return 0;
    }

    PriorityQueue local_q;
    local_q.m = trained_model;
    for (const PT &pt : tasks)
    {
        local_q.Generate(pt);
    }

    int processed = static_cast<int>(local_q.guesses.size());
    hash_passwords(local_q.guesses);
    return processed;
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
                cerr << "Invalid segment type in PT slice prefix." << endl;
                MPI_Abort(MPI_COMM_WORLD, 16);
            }
            prefix += seg_values->ordered_values[idx];
            seg_idx += 1;
        }
    }

    segment *last_values = find_model_segment(q.m, pt.content.back());
    if (last_values == nullptr)
    {
        cerr << "Invalid segment type in PT slice suffix." << endl;
        MPI_Abort(MPI_COMM_WORLD, 17);
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

int hash_pt_slices_locally(const vector<PTSliceTask> &tasks, const model &trained_model)
{
    if (tasks.empty())
    {
        return 0;
    }

    PriorityQueue local_q;
    local_q.m = trained_model;
    for (const PTSliceTask &task : tasks)
    {
        generate_pt_slice(local_q, task);
    }

    int processed = static_cast<int>(local_q.guesses.size());
    hash_passwords(local_q.guesses);
    return processed;
}

void worker_loop(const model &trained_model)
{
    PriorityQueue local_q;
    local_q.m = trained_model;
    WorkerProfile profile;
    int processed = 0;

    while (true)
    {
        auto start_send_done = system_clock::now();
        MPI_Send(&processed, 1, MPI_INT, 0, TAG_DONE, MPI_COMM_WORLD);
        auto end_send_done = system_clock::now();
        profile.send_done_time += elapsed_seconds(start_send_done, end_send_done);
        processed = 0;

        int count = 0;
        MPI_Status status;
        auto start_wait = system_clock::now();
        MPI_Recv(&count, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        auto end_wait = system_clock::now();
        profile.wait_for_task_time += elapsed_seconds(start_wait, end_wait);

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
            auto start_work_total = system_clock::now();
            double recv_elapsed = 0;
            double clear_elapsed = 0;
            double generate_elapsed = 0;
            double hash_elapsed = 0;

            {
                auto start_recv = system_clock::now();
                vector<PTSliceTask> tasks = receive_pt_slice_batch(count);
                auto end_recv = system_clock::now();
                recv_elapsed = elapsed_seconds(start_recv, end_recv);
                profile.recv_task_time += recv_elapsed;
                profile.batches += 1;
                profile.pt_tasks += static_cast<int>(tasks.size());

                auto start_clear = system_clock::now();
                local_q.guesses.clear();
                local_q.total_guesses = 0;
                auto end_clear = system_clock::now();
                clear_elapsed = elapsed_seconds(start_clear, end_clear);
                profile.clear_time += clear_elapsed;

                auto start_generate = system_clock::now();
                for (const PTSliceTask &task : tasks)
                {
                    generate_pt_slice(local_q, task);
                }
                auto end_generate = system_clock::now();
                generate_elapsed = elapsed_seconds(start_generate, end_generate);
                profile.generate_time += generate_elapsed;

                processed = static_cast<int>(local_q.guesses.size());
                auto start_hash = system_clock::now();
                hash_passwords(local_q.guesses);
                auto end_hash = system_clock::now();
                hash_elapsed = elapsed_seconds(start_hash, end_hash);
                profile.hash_time += hash_elapsed;
                profile.processed_guesses += processed;
            }

            auto end_work_total = system_clock::now();
            double work_elapsed = elapsed_seconds(start_work_total, end_work_total);
            profile.work_total_time += work_elapsed;
            profile.unaccounted_work_time +=
                work_elapsed - recv_elapsed - clear_elapsed - generate_elapsed - hash_elapsed;
        }
    }

    send_worker_profile(profile);
}

int master_main(int world_size, model trained_model, double time_train,
                double train_model_time, double model_broadcast_time)
{
    double time_hash = 0;
    double time_guess = 0;
    MasterProfile profile;
    int hash_batch_threshold = get_hash_batch_threshold(world_size);

    cout << "Hash batch threshold:" << hash_batch_threshold << endl;
    cout << "Profile train_model=" << train_model_time
         << " model_broadcast=" << model_broadcast_time
         << endl;

    PriorityQueue q;
    q.m = std::move(trained_model);

    auto start_queue_init = system_clock::now();
    q.init();
    auto end_queue_init = system_clock::now();
    profile.queue_init_time = elapsed_seconds(start_queue_init, end_queue_init);
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

        auto start_pt_pop = system_clock::now();
        current_slice_pt = pop_next_pt_task(q);
        auto end_pt_pop = system_clock::now();
        profile.pt_pop_time += elapsed_seconds(start_pt_pop, end_pt_pop);

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
            int processed = hash_pt_slices_locally(tasks, q.m);
            auto end_hash = system_clock::now();
            time_hash += elapsed_seconds(start_hash, end_hash);
            if (processed != guess_count)
            {
                cerr << "Local PT hash processed count mismatch." << endl;
                MPI_Abort(MPI_COMM_WORLD, 11);
            }

            profile.dispatch_batches += 1;
            profile.dispatched_pt_tasks += static_cast<int>(tasks.size());
            profile.dispatched_guesses += guess_count;
            history += guess_count;
            print_progress();
        }
    }
    else
    {
        int active_workers = world_size - 1;
        int completed_guesses = 0;
        bool no_more_tasks = false;

        auto receive_worker_done = [&](int source, int &processed, MPI_Status &status) {
            auto start_hash_wait = system_clock::now();
            MPI_Recv(&processed, 1, MPI_INT, source, TAG_DONE, MPI_COMM_WORLD, &status);
            auto end_hash_wait = system_clock::now();
            double wait_elapsed = elapsed_seconds(start_hash_wait, end_hash_wait);
            time_hash += wait_elapsed;
            profile.wait_time += wait_elapsed;

            if (processed < 0)
            {
                cerr << "Master received a negative processed count." << endl;
                MPI_Abort(MPI_COMM_WORLD, 12);
            }
            completed_guesses += processed;
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
                    double dispatch_elapsed = elapsed_seconds(start_hash_dispatch, end_hash_dispatch);
                    time_hash += dispatch_elapsed;
                    profile.dispatch_send_time += dispatch_elapsed;

                    profile.dispatch_batches += 1;
                    profile.dispatched_pt_tasks += static_cast<int>(tasks.size());
                    profile.dispatched_guesses += guess_count;
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
            int processed = 0;
            MPI_Status status;
            receive_worker_done(worker, processed, status);
            send_next_or_stop(worker);
        }

        while (active_workers > 0)
        {
            int processed = 0;
            MPI_Status status;
            receive_worker_done(MPI_ANY_SOURCE, processed, status);
            send_next_or_stop(status.MPI_SOURCE);
        }

        if (completed_guesses != profile.dispatched_guesses)
        {
            cerr << "Worker-driven PT hash processed count mismatch. dispatched="
                 << profile.dispatched_guesses << " completed=" << completed_guesses << endl;
            MPI_Abort(MPI_COMM_WORLD, 13);
        }
    }

    auto end = system_clock::now();
    auto duration = duration_cast<microseconds>(end - start);
    time_guess = double(duration.count()) * microseconds::period::num / microseconds::period::den;
    cout << "Guess time:" << time_guess - time_hash << "seconds" << endl; //请不要修改这一行
    cout << "Hash time:" << time_hash << "seconds" << endl;               //请不要修改这一行
    cout << "Train time:" << time_train << "seconds" << endl;             //请不要修改这一行
    collect_worker_profiles(world_size);
    cout << "Profile master queue_init=" << profile.queue_init_time
         << " pt_pop=" << profile.pt_pop_time
         << " dispatch_send=" << profile.dispatch_send_time
         << " wait=" << profile.wait_time
         << " batches=" << profile.dispatch_batches
         << " pt_tasks=" << profile.dispatched_pt_tasks
         << " dispatched_guesses=" << profile.dispatched_guesses
         << endl;
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

    int self_test_ok = 1;
    if (rank == 0)
    {
        self_test_ok = run_md5_self_test() ? 1 : 0;
    }
    MPI_Bcast(&self_test_ok, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!self_test_ok)
    {
        MPI_Finalize();
        return 1;
    }

    auto start_train = system_clock::now();
    model trained_model = train_model_mpi("/guessdata/Rockyou-singleLined-full.txt", rank, world_size);
    auto end_train_model = system_clock::now();
    broadcast_trained_model(trained_model, rank);
    MPI_Barrier(MPI_COMM_WORLD);
    auto end_train = system_clock::now();
    double train_model_time = elapsed_seconds(start_train, end_train_model);
    double model_broadcast_time = elapsed_seconds(end_train_model, end_train);
    double time_train = elapsed_seconds(start_train, end_train);

    int exit_code = 0;
    if (rank == 0)
    {
        exit_code = master_main(world_size, std::move(trained_model), time_train,
                                train_model_time, model_broadcast_time);
    }
    else
    {
        worker_loop(trained_model);
    }

    MPI_Finalize();
    return exit_code;
}
