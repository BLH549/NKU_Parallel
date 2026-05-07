一共100000次循环，每次循环，对8个密码进行MD5哈希计算，总共800000次MD5调用。

# serial_O0
=== Running Performance Test (Serial MD5Hash) ===
ITERATIONS: 100000
Total MD5 calls: 800000

=== Results ===
Total time: 0.753 seconds
Time per MD5 call: 0.941 microseconds
Checksum (verification): 6fc6123a05000


# serial_O1
=== Running Performance Test (Serial MD5Hash) ===
ITERATIONS: 100000
Total MD5 calls: 800000

=== Results ===
Total time: 0.236 seconds
Time per MD5 call: 0.295 microseconds
Checksum (verification): 6fc6123a05000

# serial_O2
=== Running Performance Test (Serial MD5Hash) ===
ITERATIONS: 100000
Total MD5 calls: 800000

=== Results ===
Total time: 0.242 seconds
Time per MD5 call: 0.302 microseconds
Checksum (verification): 6fc6123a05000

# simd_O0
=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 1.149 seconds
Time per MD5 call: 1.436 microseconds
Checksum (verification): 6fc6123a05000

# simd_O1
=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 0.161 seconds
Time per MD5 call: 0.201 microseconds
Checksum (verification): 6fc6123a05000

# simd_O2
=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 0.153 seconds
Time per MD5 call: 0.191 microseconds
Checksum (verification): 6fc6123a05000

# Serial_O1_启动自动向量化
=== Results ===
Total time: 0.241 seconds
Time per MD5 call: 0.300 microseconds
Checksum (verification): 6fc6123a05000

# Serial_O2_启动了自动向量化
=== Running Performance Test (Serial MD5Hash) ===
ITERATIONS: 100000
Total MD5 calls: 800000

=== Results ===
Total time: 0.231 seconds
Time per MD5 call: 0.289 microseconds
Checksum (verification): 6fc6123a05000

==========================================================
# 下面为之前的测试记录，供参考，建议重点参考上面的最终结果部分
==========================================================
# O0
=== Running Performance Test (Serial MD5Hash) ===
ITERATIONS: 100000
Total MD5 calls: 800000

=== Results ===
Total time: 0.751 seconds
Time per MD5 call: 0.939 microseconds
Checksum (verification): 6fc6123a05000

## macro
=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 1.375 seconds
Time per MD5 call: 1.719 microseconds
Checksum (verification): 6fc6123a05000

### 修改测试文件
=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 1.151 seconds
Time per MD5 call: 1.438 microseconds
Checksum (verification): 6fc6123a05000

## inline
=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 1.692 seconds
Time per MD5 call: 2.115 microseconds
Checksum (verification): 6fc6123a05000



# O1
=== Running Performance Test (Serial MD5Hash) ===
ITERATIONS: 100000
Total MD5 calls: 800000

=== Results ===
Total time: 0.238 seconds
Time per MD5 call: 0.297 microseconds
Checksum (verification): 6fc6123a05000

=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 0.198 seconds
Time per MD5 call: 0.247 microseconds
Checksum (verification): 6fc6123a05000

## 优化之后
=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 0.16                                                                                                                                  4 seconds
Time per MD5 call: 0.205 microseconds
Checksum (verification): 6fc6123a05000

# O2
=== Running Performance Test (Serial MD5Hash) ===
ITERATIONS: 100000
Total MD5 calls: 800000

=== Results ===
Total time: 0.242 seconds
Time per MD5 call: 0.302 microseconds
Checksum (verification): 6fc6123a05000

=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 0.189 seconds
Time per MD5 call: 0.236 microseconds
Checksum (verification): 6fc6123a05000


# 最终优化结果

## 串行O0
=== Results ===
Total time: 0.751 seconds
Time per MD5 call: 0.939 microseconds
Checksum (verification): 6fc6123a05000

## SIMD O0
=== Results ===
Total time: 1.084 seconds
Time per MD5 call: 1.355 microseconds
Checksum (verification): 6fc6123a05000

## 串行O1
Total time: 0.238 seconds
Time per MD5 call: 0.297 microseconds
Checksum (verification): 6fc6123a05000

## SIMD O1
=== Results ===
Total time: 0.133 seconds
Time per MD5 call: 0.167 microseconds
Checksum (verification): 6fc6123a05000

## 串行 O2
Total time: 0.242 seconds
Time per MD5 call: 0.302 microseconds
Checksum (verification): 6fc6123a05000

## SIMD O2
=== Results ===
Total time: 0.127 seconds
Time per MD5 call: 0.159 microseconds
Checksum (verification): 6fc6123a05000

