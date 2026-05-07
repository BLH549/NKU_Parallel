#include "md5.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>

using namespace std;
using namespace chrono;

int main()
{
    // 测试密码集合（与main中的测试一致）
    vector<string> test_passwords = {
        "123456", "password", "12345678", "qwerty", "123456789", "12345", "1234", "111111"
    };

    // 预期的MD5哈希值
    vector<string> expected_hashes = {
        "e10adc3949ba59abbe56e057f20f883e",
        "5f4dcc3b5aa765d61d8327deb882cf99",
        "25d55ad283aa400af464c76d713c07ad",
        "d8578edf8458ce06fbc5bb76a58c5ca4",
        "25f9e794323b453885f5181f1b624d0b",
        "827ccb0eea8a706c4c34a16891f84e7b",
        "81dc9bdb52d04dc20036dbd8313ed055",
        "96e79218965eb72c92a549dd5a330112"
    };

    // 验证测试密码的MD5正确性
    cout << "=== Verifying MD5 Correctness ===" << endl;
    {
        bit32 states[8][4];
        MD5Hash_SIMD(test_passwords.data(), test_passwords.size(), states);
        
        for (size_t i = 0; i < test_passwords.size(); i++) {
            stringstream ss;
            for (int j = 0; j < 4; j++) {
                ss << setw(8) << setfill('0') << hex << states[i][j];
            }
            
            string computed = ss.str();
            if (computed != expected_hashes[i]) {
                cerr << "ERROR: MD5 mismatch for '" << test_passwords[i] << "'" << endl;
                cerr << "Expected: " << expected_hashes[i] << endl;
                cerr << "Got:      " << computed << endl;
                return 1;
            }
            cout << "✓ " << test_passwords[i] << " -> " << computed << endl;
        }
    }

    cout << "\n=== Running Performance Test (SIMD MD5Hash) ===" << endl;

    // 性能测试：批量处理，每批4个密码
    // 为了公平比较，总的密码处理数应该与串行版本相同
    const int ITERATIONS = 100000;
    const int BATCH_SIZE = 4;
    const int TEST_COUNT = test_passwords.size();

    cout << "ITERATIONS: " << ITERATIONS << endl;
    cout << "BATCH_SIZE: " << BATCH_SIZE << endl;
    cout << "Total MD5 batches: " << (ITERATIONS * TEST_COUNT / BATCH_SIZE) << endl;
    cout << "Total passwords processed: " << (ITERATIONS * TEST_COUNT) << endl;

    auto start_time = system_clock::now();

    bit32 states[8][4]; 
    uint64_t checksum = 0;

    for (int iter = 0; iter < ITERATIONS; iter++) {
        MD5Hash_SIMD(test_passwords.data(), TEST_COUNT, states);
        
        for (int i = 0; i < TEST_COUNT; i++) {
            checksum += states[i][0] ^ states[i][1] ^ states[i][2] ^ states[i][3];
        }
    }


    auto end_time = system_clock::now();
    auto duration = duration_cast<microseconds>(end_time - start_time);
    double time_seconds = double(duration.count()) * microseconds::period::num / microseconds::period::den;

    cout << "\n=== Results ===" << endl;
    cout << "Total time: " << fixed << setprecision(3) << time_seconds << " seconds" << endl;
    cout << "Time per MD5 call: " << (time_seconds / (ITERATIONS * TEST_COUNT)) * 1e6 << " microseconds" << endl;
    cout << "Checksum (verification): " << hex << checksum << endl;

    return 0;
}
