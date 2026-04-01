#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>

using namespace std;
using namespace std::chrono;

const int REPEAT_TIMES = 10; // 减少系统误差的重复次数

// ================= 实验一：矩阵与向量内积 =================

// 平凡算法：逐列访问 (空间局部性差)
void matrix_vector_naive(const vector<vector<float>>& b, const vector<float>& a, vector<float>& sum) {
    int n = b.size();
    for (int i = 0; i < n; i++) {
        sum[i] = 0.0;
        for (int j = 0; j < n; j++) {
            sum[i] += b[j][i] * a[j];
        }
    }
}

// 缓存优化算法：逐行访问 (空间局部性好)
void matrix_vector_optimized(const vector<vector<float>>& b, const vector<float>& a, vector<float>& sum) {
    int n = b.size();
    for (int i = 0; i < n; i++) {
        sum[i] = 0.0;
    }
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            sum[i] += b[j][i] * a[j];
        }
    }
}

// ================= 实验二：n 个数的和 =================

// 平凡算法：单路顺序累加
float array_sum_naive(const vector<float>& arr) {
    float sum = 0.0;
    int n = arr.size();
    for (int i = 0; i < n; i++) {
        sum += arr[i];
    }
    return sum;
}

// 超标量优化：2路循环展开
float array_sum_optimized(const vector<float>& arr) {
    float sum1 = 0.0, sum2 = 0.0;
    int n = arr.size();
    for (int i = 0; i < n - 1; i += 2) {
        sum1 += arr[i];
        sum2 += arr[i + 1];
    }
    // 处理奇数长度尾部
    if (n % 2 != 0) sum1 += arr[n - 1];
    return sum1 + sum2;
}

// 进一步优化：8路循环展开
float array_sum_unroll8(const vector<float>& arr) {
    float sum0 = 0.0, sum1 = 0.0, sum2 = 0.0, sum3 = 0.0;
    float sum4 = 0.0, sum5 = 0.0, sum6 = 0.0, sum7 = 0.0;
    
    int n = arr.size();
    int i = 0;
    for (; i <= n - 8; i += 8) {
        sum0 += arr[i];
        sum1 += arr[i+1];
        sum2 += arr[i+2];
        sum3 += arr[i+3];
        sum4 += arr[i+4];
        sum5 += arr[i+5];
        sum6 += arr[i+6];
        sum7 += arr[i+7];
    }
    
    // 处理剩余尾部元素
    for (; i < n; i++) {
        sum0 += arr[i];
    }
    
    return sum0 + sum1 + sum2 + sum3 + sum4 + sum5 + sum6 + sum7;
}

// ================= 主函数与测试逻辑 =================
int main() {
    // 设置要测试的矩阵维度 (N * N)
    vector<int> matrix_sizes = {256, 1024, 2048, 4096};
    
    cout << "================ Experiment 1: Matrix-Vector Inner Product (Multi-Scale Test) ================\n";
    cout << left << setw(15) << "Matrix Size(N)" 
         << setw(20) << "Naive(ms)" 
         << setw(20) << "Optimized(ms)" 
         << "Speedup\n";
    cout << string(65, '-') << "\n";

    for (int n : matrix_sizes) {
        vector<vector<float>> matrix(n, vector<float>(n, 1.0f));
        vector<float> vec(n, 2.0f);
        vector<float> res_matrix(n, 0.0f);

        // 测试平凡算法
        auto start = high_resolution_clock::now();
        for(int r = 0; r < REPEAT_TIMES; r++) matrix_vector_naive(matrix, vec, res_matrix);
        auto end = high_resolution_clock::now();
        double naive_time = duration<double, std::milli>(end - start).count() / REPEAT_TIMES;

        // 测试优化算法
        start = high_resolution_clock::now();
        for(int r = 0; r < REPEAT_TIMES; r++) matrix_vector_optimized(matrix, vec, res_matrix);
        end = high_resolution_clock::now();
        double opt_time = duration<double, std::milli>(end - start).count() / REPEAT_TIMES;

        cout << left << setw(15) << n 
             << setw(20) << naive_time 
             << setw(20) << opt_time 
             << fixed << setprecision(2) << naive_time / opt_time << "x\n";
    }

    // 设置要测试的数组规模
    vector<int> array_sizes = {100000, 1000000, 10000000, 100000000};
    
    cout << "\n================ Experiment 2: Sum of n Numbers (Multi-Scale Test) ================\n";
    cout << left << setw(15) << "Array Size" 
         << setw(15) << "Naive(ms)" 
         << setw(15) << "Unroll-2(ms)" 
         << setw(15) << "Unroll-8(ms)" << "\n";
    cout << string(60, '-') << "\n";

    for (int n : array_sizes) {
        vector<float> arr(n, 1.0f);

        volatile float sum_res = 0;

        // 单路
        auto start = high_resolution_clock::now();
        for(int r = 0; r < REPEAT_TIMES; r++) sum_res = array_sum_naive(arr);
        auto end = high_resolution_clock::now();
        double time_naive = duration<double, std::milli>(end - start).count() / REPEAT_TIMES;

        // 2路展开
        start = high_resolution_clock::now();
        for(int r = 0; r < REPEAT_TIMES; r++) sum_res = array_sum_optimized(arr);
        end = high_resolution_clock::now();
        double time_unroll2 = duration<double, std::milli>(end - start).count() / REPEAT_TIMES;

        // 8路展开
        start = high_resolution_clock::now();
        for(int r = 0; r < REPEAT_TIMES; r++) sum_res = array_sum_unroll8(arr);
        end = high_resolution_clock::now();
        double time_unroll8 = duration<double, std::milli>(end - start).count() / REPEAT_TIMES;

        cout << left << setw(15) << n 
             << setw(15) << time_naive 
             << setw(15) << time_unroll2 
             << setw(15) << time_unroll8 << "\n";
    }

    return 0;
}