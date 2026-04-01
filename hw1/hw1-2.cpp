#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>

using namespace std;
using namespace std::chrono;

// Problem size configuration
const int N_MATRIX = 8192; // Matrix size N*N
const int N_ARRAY = 100000000; // Array size
const int REPEAT_TIMES = 10; // Number of repetitions to reduce experimental error

// ================= Problem I: Matrix-Vector Dot Product =================
// Naive algorithm: column-wise access (poor spatial locality)
void matrix_vector_naive(const vector<vector<float>>& b, const vector<float>& a, vector<float>& sum) {
    for (int i = 0; i < N_MATRIX; i++) {
        sum[i] = 0.0;
        for (int j = 0; j < N_MATRIX; j++) {
            sum[i] += b[j][i] * a[j];
        }
    }
}

// Cache-optimized algorithm: row-wise access (good spatial locality)
void matrix_vector_optimized(const vector<vector<float>>& b, const vector<float>& a, vector<float>& sum) {
    for (int i = 0; i < N_MATRIX; i++) {
        sum[i] = 0.0;
    }
    for (int j = 0; j < N_MATRIX; j++) {
        for (int i = 0; i < N_MATRIX; i++) {
            sum[i] += b[j][i] * a[j];
        }
    }
}

// Function to compute matrix-vector product and return sum of results for comparison
float matrix_vector_sum(const vector<vector<float>>& b, const vector<float>& a, 
                        void (*algorithm)(const vector<vector<float>>&, const vector<float>&, vector<float>&)) {
    vector<float> sum(N_MATRIX, 0.0f);
    algorithm(b, a, sum);
    
    float total = 0.0f;
    for (int i = 0; i < N_MATRIX; i++) {
        total += sum[i];
    }
    return total;
}

// ================= Problem II: Sum of n Numbers =================
// Naive algorithm: sequential accumulation
float array_sum_naive(const vector<float>& arr) {
    float sum = 0.0;
    for (int i = 0; i < N_ARRAY; i++) {
        sum += arr[i];
    }
    return sum;
}

// Superscalar optimization: two-way accumulation (exploits instruction-level parallelism)
float array_sum_optimized(const vector<float>& arr) {
    float sum1 = 0.0, sum2 = 0.0;
    for (int i = 0; i < N_ARRAY; i += 2) {
        sum1 += arr[i];
        sum2 += arr[i + 1];
    }
    return sum1 + sum2;
}

// Further optimization: unroll the loop by 8 (exploits more parallelism)
float array_sum_unroll8(const vector<float>& arr) {
    float sum0 = 0.0, sum1 = 0.0, sum2 = 0.0, sum3 = 0.0;
    float sum4 = 0.0, sum5 = 0.0, sum6 = 0.0, sum7 = 0.0;
    
    int i = 0;
    // Process 8 elements at a time
    for (; i <= N_ARRAY - 8; i += 8) {
        sum0 += arr[i];
        sum1 += arr[i+1];
        sum2 += arr[i+2];
        sum3 += arr[i+3];
        sum4 += arr[i+4];
        sum5 += arr[i+5];
        sum6 += arr[i+6];
        sum7 += arr[i+7];
    }
    
    // Handle remaining elements (if N_ARRAY is not a multiple of 8)
    for (; i < N_ARRAY; i++) {
        sum0 += arr[i];
    }
    
    return sum0 + sum1 + sum2 + sum3 + sum4 + sum5 + sum6 + sum7;
}

// ================= Helper Timing Function =================
int main() {
    cout << fixed << setprecision(15); // Set high precision to show floating-point differences
    cout << "Initializing test data..." << endl;
    
    // Initialize matrix and vector with non-integer values to show floating-point rounding differences
    vector<vector<float>> matrix(N_MATRIX, vector<float>(N_MATRIX));
    vector<float> vec(N_MATRIX);
    
    // Use values that cause floating-point rounding errors
    for (int i = 0; i < N_MATRIX; i++) {
        vec[i] = 0.1f;  // 0.1 cannot be represented exactly in binary
        for (int j = 0; j < N_MATRIX; j++) {
            matrix[i][j] = 0.2f;  // 0.2 also has rounding errors
        }
    }
    
    // Initialize large array with non-integer values to show floating-point rounding differences
    vector<float> arr(N_ARRAY);
    for (int i = 0; i < N_ARRAY; i++) {
        arr[i] = 0.1f; // Using 0.1 instead of 1.0 to create floating-point rounding errors
    }

    cout << "\n================ Experiment I: Matrix-Vector Dot Product ================" << endl;
    cout << "Comparing results from different access patterns:\n" << endl;
    
    // Compute and display results for naive algorithm
    vector<float> res_matrix_naive(N_MATRIX, 0.0f);
    auto start = high_resolution_clock::now();
    for(int r = 0; r < REPEAT_TIMES; r++) {
        matrix_vector_naive(matrix, vec, res_matrix_naive);
    }
    auto end = high_resolution_clock::now();
    duration<double, std::milli> naive_mat_time = end - start;
    
    // Calculate sum of results for naive algorithm
    float naive_mat_sum = 0.0f;
    for (int i = 0; i < N_MATRIX; i++) {
        naive_mat_sum += res_matrix_naive[i];
    }
    
    cout << "Naive algorithm (column-wise access):" << endl;
    cout << "  Time: " << naive_mat_time.count() / REPEAT_TIMES << " ms/iteration" << endl;
    cout << "  Sum of all results: " << naive_mat_sum << endl;
    
    // Compute and display results for optimized algorithm
    vector<float> res_matrix_opt(N_MATRIX, 0.0f);
    start = high_resolution_clock::now();
    for(int r = 0; r < REPEAT_TIMES; r++) {
        matrix_vector_optimized(matrix, vec, res_matrix_opt);
    }
    end = high_resolution_clock::now();
    duration<double, std::milli> opt_mat_time = end - start;
    
    // Calculate sum of results for optimized algorithm
    float opt_mat_sum = 0.0f;
    for (int i = 0; i < N_MATRIX; i++) {
        opt_mat_sum += res_matrix_opt[i];
    }
    
    cout << "\nCache-optimized algorithm (row-wise access):" << endl;
    cout << "  Time: " << opt_mat_time.count() / REPEAT_TIMES << " ms/iteration" << endl;
    cout << "  Sum of all results: " << opt_mat_sum << endl;
    
    // Compare results
    cout << "\n=== Matrix-Vector Product Comparison ===" << endl;
    cout << "Naive sum:     " << naive_mat_sum << endl;
    cout << "Optimized sum: " << opt_mat_sum << endl;
    
    if (naive_mat_sum != opt_mat_sum) {
        cout << "\n✓ Different access patterns produce different results!" << endl;
        cout << "  This demonstrates floating-point rounding errors" << endl;
        cout << "  due to different operation orders in matrix multiplication." << endl;
        cout << "\n  Difference: |Naive - Optimized| = " << abs(naive_mat_sum - opt_mat_sum) << endl;
    } else {
        cout << "\n  Both algorithms produced identical results in this case." << endl;
    }
    
    // Performance comparison
    cout << "\n=== Matrix-Vector Performance Comparison ===" << endl;
    cout << "Speedup: " << (naive_mat_time.count() / opt_mat_time.count()) << "x faster" << endl;
    cout << "Naive time: " << naive_mat_time.count() / REPEAT_TIMES << " ms, "
         << "Optimized time: " << opt_mat_time.count() / REPEAT_TIMES << " ms" << endl;

    cout << "\n================ Experiment II: Sum of n Numbers ================" << endl;
    cout << "Comparing results from different summation algorithms:\n" << endl;
    
    // Test naive algorithm
    volatile float sum_res1 = 0; // Prevent compiler over-optimization
    start = high_resolution_clock::now();
    for(int r = 0; r < REPEAT_TIMES; r++) sum_res1 = array_sum_naive(arr);
    end = high_resolution_clock::now();
    duration<double, std::milli> naive_arr_time = end - start;
    cout << "Naive algorithm (sequential accumulation):" << endl;
    cout << "  Time: " << naive_arr_time.count() / REPEAT_TIMES << " ms/iteration" << endl;
    cout << "  Sum: " << sum_res1 << endl;

    // Test optimized algorithm
    volatile float sum_res2 = 0;
    start = high_resolution_clock::now();
    for(int r = 0; r < REPEAT_TIMES; r++) sum_res2 = array_sum_optimized(arr);
    end = high_resolution_clock::now();
    duration<double, std::milli> opt_arr_time = end - start;
    cout << "\nSuperscalar optimization (two-way unrolled):" << endl;
    cout << "  Time: " << opt_arr_time.count() / REPEAT_TIMES << " ms/iteration" << endl;
    cout << "  Sum: " << sum_res2 << endl;

    // Test unrolled algorithm
    volatile float sum_res3 = 0;
    start = high_resolution_clock::now();
    for(int r = 0; r < REPEAT_TIMES; r++) sum_res3 = array_sum_unroll8(arr);
    end = high_resolution_clock::now();
    duration<double, std::milli> unroll_arr_time = end - start;
    cout << "\nUnrolled by 8 (exploiting more parallelism):" << endl;
    cout << "  Time: " << unroll_arr_time.count() / REPEAT_TIMES << " ms/iteration" << endl;
    cout << "  Sum: " << sum_res3 << endl;
    
    // Compare the results
    cout << "\n=== Array Summation Comparison ===" << endl;
    cout << "Naive sum:     " << sum_res1 << endl;
    cout << "2-way sum:     " << sum_res2 << endl;
    cout << "8-way sum:     " << sum_res3 << endl;
    
    if (sum_res1 != sum_res2 || sum_res1 != sum_res3) {
        cout << "\n✓ Different summation orders produce different results!" << endl;
        cout << "  This demonstrates floating-point rounding errors" << endl;
        cout << "  due to different operation orders." << endl;
        
        // Calculate differences
        cout << "\nDifferences:" << endl;
        cout << "  |Naive - 2-way| = " << abs(sum_res1 - sum_res2) << endl;
        cout << "  |Naive - 8-way| = " << abs(sum_res1 - sum_res3) << endl;
        cout << "  |2-way - 8-way| = " << abs(sum_res2 - sum_res3) << endl;
    } else {
        cout << "\nAll sums are identical in this case." << endl;
    }
    
    // Performance comparison for array summation
    cout << "\n=== Array Summation Performance Comparison ===" << endl;
    cout << "2-way vs Naive: " << (naive_arr_time.count() / opt_arr_time.count()) << "x faster" << endl;
    cout << "8-way vs Naive: " << (naive_arr_time.count() / unroll_arr_time.count()) << "x faster" << endl;
    cout << "Naive time: " << naive_arr_time.count() / REPEAT_TIMES << " ms, "
         << "2-way time: " << opt_arr_time.count() / REPEAT_TIMES << " ms, "
         << "8-way time: " << unroll_arr_time.count() / REPEAT_TIMES << " ms" << endl;

    return 0;
}