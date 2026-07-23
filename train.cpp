#include "PCFG.h"
#include <fstream>
#include <cctype>
#include <algorithm>
#include <vector>

// 这个文件里面的各函数你都不需要完全理解，甚至根本不需要看
// 从学术价值上讲，加速模型的训练过程是一个没什么价值的问题，因为我们一般假定统计学模型的训练成本较低
// 但是，假如你是一个投稿时顶着ddl做实验的倒霉研究生/实习生，提高训练速度就可以大幅节省你的时间了
// 所以如果你愿意，也可以尝试用多线程加速训练过程

/**
 * 怎么加速PCFG训练过程？据助教所知，没有公开文献提出过有效的加速方法（因为这么做基本无学术价值）
 *
 * 但是统计学模型好就好在其数据是可加的。例如，假如我把数据集拆分成4个部分，并行训练4个不同的模型。
 * 然后我可以直接将四个模型的统计数据进行简单加和，就得到了和串行训练相同的模型了。
 *
 * 说起来容易，做起来不一定容易，你可能会碰到一系列具体的工程问题。如果你决定加速训练过程，祝你好运！
 *
 */

namespace
{
const int TRAIN_PROGRESS_INTERVAL = 10000;
const int TRAIN_LINE_LIMIT = 3000000;

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
            int dst_id = dst.values.size();
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
    for (int src_id = 0; src_id < src_segments.size(); src_id += 1)
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

    for (int src_id = 0; src_id < src.preterminals.size(); src_id += 1)
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
}

// 训练的wrapper，实际上就是读取训练集
void model::train(string path)
{
    string pw;
    ifstream train_set(path);
    int lines = 0;
    vector<string> passwords;
    passwords.reserve(TRAIN_LINE_LIMIT + TRAIN_PROGRESS_INTERVAL);

    cout<<"Training..."<<endl;
    cout<<"Training phase 1: reading and parsing passwords..."<<endl;
    while (train_set >> pw)
    {
        lines += 1;
        if (lines % TRAIN_PROGRESS_INTERVAL == 0)
        {
            cout <<"Lines processed: "<< lines << endl;
            // 在这里更改读取的训练集口令上限
            if (lines > TRAIN_LINE_LIMIT)
            {
                break;
            }
        }
        passwords.emplace_back(pw);
    }

    int thread_count = omp_get_max_threads();
    if (thread_count < 1)
    {
        thread_count = 1;
    }
    if (passwords.size() < thread_count)
    {
        thread_count = passwords.empty() ? 1 : passwords.size();
    }

    cout << "Training phase 1.5: parallel parsing with "
         << thread_count << " local models..." << endl;

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

    cout << "Training phase 1.6: merging local models..." << endl;
    for (model &partial : partial_models)
    {
        merge_model(*this, partial);
    }
}

/// @brief 在模型中找到一个PT的统计数据
/// @param pt 需要查找的PT
/// @return 目标PT在模型中的对应下标
int model::FindPT(const PT &pt)
{
    for (int id = 0; id < preterminals.size(); id += 1)
    {
        if (preterminals[id].content.size() != pt.content.size())
        {
            continue;
        }
        else
        {
            bool equal_flag = true;
            for (int idx = 0; idx < preterminals[id].content.size(); idx += 1)
            {
                if (preterminals[id].content[idx].type != pt.content[idx].type || preterminals[id].content[idx].length != pt.content[idx].length)
                {
                    equal_flag = false;
                    break;
                }
            }
            if (equal_flag == true)
            {
                return id;
            }
        }
    }
    return -1;
}

/// @brief 在模型中找到一个letter segment的统计数据
/// @param seg 要找的letter segment
/// @return 目标letter segment的对应下标
int model::FindLetter(const segment &seg)
{
    for (int id = 0; id < letters.size(); id += 1)
    {
        if (letters[id].length == seg.length)
        {
            return id;
        }
    }
    return -1;
}

/// @brief 在模型中找到一个digit segment的统计数据
/// @param seg 要找的digit segment
/// @return 目标digit segment的对应下标
int model::FindDigit(const segment &seg)
{
    for (int id = 0; id < digits.size(); id += 1)
    {
        if (digits[id].length == seg.length)
        {
            return id;
        }
    }
    return -1;
}

int model::FindSymbol(const segment &seg)
{
    for (int id = 0; id < symbols.size(); id += 1)
    {
        if (symbols[id].length == seg.length)
        {
            return id;
        }
    }
    return -1;
}

void PT::insert(segment seg)
{
    content.emplace_back(seg);
}

void segment::insert(string value)
{
    auto iter = values.find(value);
    if (iter == values.end())
    {
        int id = values.size();
        values.emplace(value, id);
        freqs[id] = 1;
    }
    else
    {
        freqs[iter->second] += 1;
    }
}


void segment::order()
{
    ordered_values.clear();
    ordered_freqs.clear();
    total_freq = 0;

    ordered_values.reserve(values.size());
    ordered_freqs.reserve(values.size());

    for (const pair<string, int> &value : values)
    {
        ordered_values.emplace_back(value.first);
    }
    // cout << "value size:" << ordered_values.size() << endl;
    std::sort(ordered_values.begin(), ordered_values.end(),
              [this](const std::string &a, const std::string &b)
              {
                  return freqs.at(values[a]) > freqs.at(values[b]);
              });

    // 将排序后的频率存入 ordered_freqs 并计算 total_freq
    for (const std::string &val : ordered_values)
    {
        ordered_freqs.emplace_back(freqs.at(values[val]));
        total_freq += freqs.at(values[val]);
    }
}

void model::parse(string pw)
{
    PT pt;
    string curr_part = "";
    int curr_type = 0; // 0: 未设置, 1: 字母, 2: 数字, 3: 特殊字符
    // 请学会使用这种方式写for循环：for (auto it : iterable)
    // 相信我，以后你会用上的。You're welcome :)
    for (char ch : pw)
    {
        if (isalpha(ch))
        {
            if (curr_type != 1)
            {
                if (curr_type == 2)
                {
                    segment seg(curr_type, curr_part.length());
                    int id = FindDigit(seg);
                    if (id == -1)
                    {
                        id = GetNextDigitsID();
                        digits.emplace_back(seg);
                        digits[id].insert(curr_part);
                        digits_freq[id] = 1;
                    }
                    else
                    {
                        digits_freq[id] += 1;
                        digits[id].insert(curr_part);
                    }
                    curr_part.clear();
                    pt.insert(seg);
                }
                else if (curr_type == 3)
                {
                    segment seg(curr_type, curr_part.length());
                    int id = FindSymbol(seg);
                    if (id == -1)
                    {
                        id = GetNextSymbolsID();
                        symbols.emplace_back(seg);
                        symbols_freq[id] = 1;
                        symbols[id].insert(curr_part);
                    }
                    else
                    {
                        symbols_freq[id] += 1;
                        symbols[id].insert(curr_part);
                    }
                    curr_part.clear();
                    pt.insert(seg);
                }
            }
            curr_type = 1;
            curr_part += ch;
        }
        else if (isdigit(ch))
        {
            if (curr_type != 2)
            {
                if (curr_type == 1)
                {
                    segment seg(curr_type, curr_part.length());
                    int id = FindLetter(seg);
                    if (id == -1)
                    {
                        id = GetNextLettersID();
                        letters.emplace_back(seg);
                        letters_freq[id] = 1;
                        letters[id].insert(curr_part);
                    }
                    else
                    {
                        letters_freq[id] += 1;
                        letters[id].insert(curr_part);
                    }
                    curr_part.clear();
                    pt.insert(seg);
                }
                else if (curr_type == 3)
                {
                    segment seg(curr_type, curr_part.length());
                    int id = FindSymbol(seg);
                    if (id == -1)
                    {
                        id = GetNextSymbolsID();
                        symbols.emplace_back(seg);
                        symbols_freq[id] = 1;
                        symbols[id].insert(curr_part);
                    }
                    else
                    {
                        symbols_freq[id] += 1;
                        symbols[id].insert(curr_part);
                    }
                    curr_part.clear();
                    pt.insert(seg);
                }
            }
            curr_type = 2;
            curr_part += ch;
        }
        else
        {
            if (curr_type != 3)
            {
                if (curr_type == 1)
                {
                    segment seg(curr_type, curr_part.length());
                    int id = FindLetter(seg);
                    if (id == -1)
                    {
                        id = GetNextLettersID();
                        letters.emplace_back(seg);
                        letters_freq[id] = 1;
                        letters[id].insert(curr_part);
                    }
                    else
                    {
                        letters_freq[id] += 1;
                        letters[id].insert(curr_part);
                    }
                    curr_part.clear();
                    pt.insert(seg);
                }
                else if (curr_type == 2)
                {
                    segment seg(curr_type, curr_part.length());
                    int id = FindDigit(seg);
                    if (id == -1)
                    {
                        id = GetNextDigitsID();
                        digits.emplace_back(seg);
                        digits_freq[id] = 1;
                        digits[id].insert(curr_part);
                    }
                    else
                    {
                        digits_freq[id] += 1;
                        digits[id].insert(curr_part);
                    }
                    curr_part.clear();
                    pt.insert(seg);
                }
            }
            curr_type = 3;
            curr_part += ch;
        }
    }
    if (!curr_part.empty())
    {
        if (curr_type == 1)
        {
            segment seg(curr_type, curr_part.length());
            int id = FindLetter(seg);
            if (id == -1)
            {
                id = GetNextLettersID();
                letters.emplace_back(seg);
                letters_freq[id] = 1;
                letters[id].insert(curr_part);
            }
            else
            {
                letters_freq[id] += 1;
                letters[id].insert(curr_part);
            }
            curr_part.clear();
            pt.insert(seg);
        }
        else if (curr_type == 2)
        {
            segment seg(curr_type, curr_part.length());
            int id = FindDigit(seg);
            if (id == -1)
            {
                id = GetNextDigitsID();
                digits.emplace_back(seg);
                digits_freq[id] = 1;
                digits[id].insert(curr_part);
            }
            else
            {
                digits_freq[id] += 1;
                digits[id].insert(curr_part);
            }
            curr_part.clear();
            pt.insert(seg);
        }
        else
        {
            segment seg(curr_type, curr_part.length());
            int id = FindSymbol(seg);
            if (id == -1)
            {
                id = GetNextSymbolsID();
                symbols.emplace_back(seg);
                symbols_freq[id] = 1;
                symbols[id].insert(curr_part);
            }
            else
            {
                symbols_freq[id] += 1;
                symbols[id].insert(curr_part);
            }
            curr_part.clear();
            pt.insert(seg);
        }
    }
    // pt.PrintPT();
    // cout<<endl;
    // cout << FindPT(pt) << endl;
    total_preterm += 1;
    int pt_id = FindPT(pt);
    if (pt_id == -1)
    {
        for (int i = 0; i < pt.content.size(); i += 1)
        {
            pt.curr_indices.emplace_back(0);
        }
        int id = GetNextPretermID();
        // cout << id << endl;
        preterminals.emplace_back(pt);
        preterm_freq[id] = 1;
    }
    else
    {
        // cout << id << endl;
        preterm_freq[pt_id] += 1;
    }
}

void segment::PrintSeg()
{
    if (type == 1)
    {
        cout << "L" << length;
    }
    if (type == 2)
    {
        cout << "D" << length;
    }
    if (type == 3)
    {
        cout << "S" << length;
    }
}

void segment::PrintValues()
{
    // order();
    for (string iter : ordered_values)
    {
        cout << iter << " freq:" << freqs[values[iter]] << endl;
    }
}

void PT::PrintPT()
{
    for (auto iter : content)
    {
        iter.PrintSeg();
    }
}

void model::print()
{
    cout << "preterminals:" << endl;
    for (int i = 0; i < preterminals.size(); i += 1)
    {
        preterminals[i].PrintPT();
        // cout << preterminals[i].curr_indices.size() << endl;
        cout << " freq:" << preterm_freq[i];
        cout << endl;
    }
    // order();
    for (auto iter : ordered_pts)
    {
        iter.PrintPT();
        cout << " freq:" << preterm_freq[FindPT(iter)];
        cout << endl;
    }
    cout << "segments:" << endl;
    for (int i = 0; i < letters.size(); i += 1)
    {
        letters[i].PrintSeg();
        // letters[i].PrintValues();
        cout << " freq:" << letters_freq[i];
        cout << endl;
    }
    for (int i = 0; i < digits.size(); i += 1)
    {
        digits[i].PrintSeg();
        // digits[i].PrintValues();
        cout << " freq:" << digits_freq[i];
        cout << endl;
    }
    for (int i = 0; i < symbols.size(); i += 1)
    {
        symbols[i].PrintSeg();
        // symbols[i].PrintValues();
        cout << " freq:" << symbols_freq[i];
        cout << endl;
    }
}

bool compareByPretermProb(const PT& a, const PT& b) {
    return a.preterm_prob > b.preterm_prob;  // 降序排序
}

void model::order()
{
    cout << "Training phase 2: Ordering segment values and PTs..." << endl;
    ordered_pts.clear();
    ordered_pts.reserve(preterminals.size());

    for (int id = 0; id < preterminals.size(); id += 1)
    {
        PT pt = preterminals[id];
        pt.preterm_prob = float(preterm_freq[id]) / total_preterm;
        ordered_pts.emplace_back(pt);
    }
    bool swapped;
    cout << "total pts" << ordered_pts.size() << endl;
    std::sort(ordered_pts.begin(), ordered_pts.end(), compareByPretermProb);
    cout << "Ordering letters" << endl;
    // cout << "total letters" << endl;
    for (int i = 0; i < letters.size(); i += 1)
    {
        // cout << i << endl;
        letters[i].order();
    }
    cout << "Ordering digits" << endl;
    // cout << "total letters" << endl;
    for (int i = 0; i < digits.size(); i += 1)
    {
        digits[i].order();
    }
    cout << "ordering symbols" << endl;
    // cout << "total letters" << endl;
    for (int i = 0; i < symbols.size(); i += 1)
    {
        symbols[i].order();
    }
}
