
# perf stat ./serial_0
=== Running Performance Test (Serial MD5Hash) ===
ITERATIONS: 100000
Total MD5 calls: 800000

=== Results ===
Total time: 0.753 seconds
Time per MD5 call: 0.942 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './serial_0':

            755.81 msec task-clock:u              #    0.998 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
               107      page-faults:u             #    0.142 K/sec                  
     1,949,637,172      cycles:u                  #    2.580 GHz                    
     2,871,262,207      instructions:u            #    1.47  insn per cycle         
   <not supported>      branches:u                                                  
         1,309,281      branch-misses:u                                             

       0.757075860 seconds time elapsed

       0.755427000 seconds user
       0.000000000 seconds sys

# perf stat ./serial_1

=== Running Performance Test (Serial MD5Hash) ===
ITERATIONS: 100000
Total MD5 calls: 800000

=== Results ===
Total time: 0.236 seconds
Time per MD5 call: 0.295 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './serial_1':

            238.43 msec task-clock:u              #    0.997 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
               108      page-faults:u             #    0.453 K/sec                  
       614,751,256      cycles:u                  #    2.578 GHz                    
     1,354,571,309      instructions:u            #    2.20  insn per cycle         
   <not supported>      branches:u                                                  
           603,815      branch-misses:u                                             

       0.239260974 seconds time elapsed

       0.238982000 seconds user
       0.000000000 seconds sys
# perf stat ./serial_2

=== Running Performance Test (Serial MD5Hash) ===
ITERATIONS: 100000
Total MD5 calls: 800000

=== Results ===
Total time: 0.242 seconds
Time per MD5 call: 0.302 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './serial_2':

            243.62 msec task-clock:u              #    0.997 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
               107      page-faults:u             #    0.439 K/sec                  
       629,250,736      cycles:u                  #    2.583 GHz                    
     1,311,972,081      instructions:u            #    2.08  insn per cycle         
   <not supported>      branches:u                                                  
         2,248,446      branch-misses:u                                             

       0.244373695 seconds time elapsed

       0.244078000 seconds user
       0.000000000 seconds sys




# perf stat ./simd_0
=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 1.154 seconds
Time per MD5 call: 1.443 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './simd_0':

          1,156.18 msec task-clock:u              #    0.999 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
               112      page-faults:u             #    0.097 K/sec                  
     2,972,991,616      cycles:u                  #    2.571 GHz                    
     2,716,101,750      instructions:u            #    0.91  insn per cycle         
   <not supported>      branches:u                                                  
         1,894,453      branch-misses:u                                             

       1.157293838 seconds time elapsed

       1.155285000 seconds user
       0.000000000 seconds sys


# perf stat ./simd_1

=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 0.162 seconds
Time per MD5 call: 0.202 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './simd_1':

            163.64 msec task-clock:u              #    0.996 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
               107      page-faults:u             #    0.654 K/sec                  
       420,948,649      cycles:u                  #    2.572 GHz                    
       836,675,242      instructions:u            #    1.99  insn per cycle         
   <not supported>      branches:u                                                  
           922,104      branch-misses:u                                             

       0.164316156 seconds time elapsed

       0.164071000 seconds user
       0.000000000 seconds sys


# perf stat ./simd_2
=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 0.152 seconds
Time per MD5 call: 0.190 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './simd_2':

            154.13 msec task-clock:u              #    0.995 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
               107      page-faults:u             #    0.694 K/sec                  
       395,662,437      cycles:u                  #    2.567 GHz                    
       778,469,396      instructions:u            #    1.97  insn per cycle         
   <not supported>      branches:u                                                  
           483,026      branch-misses:u                                             

       0.154900696 seconds time elapsed

       0.154435000 seconds user
       0.000000000 seconds sys


# IPC与缓存行为
## 命令
perf stat -e cycles,instructions,cache-references,cache-misses,\
L1-dcache-load-misses,L1-dcache-loads,\
LLC-load-misses,LLC-loads \
./对应文件

## serial_O1 的perf缓存性能分析 
=== Running Performance Test (Serial MD5Hash) ===
ITERATIONS: 100000
Total MD5 calls: 800000

=== Results ===
Total time: 0.237 seconds
Time per MD5 call: 0.297 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './serial_1':

       617,460,669      cycles:u                                                    
     1,354,378,447      instructions:u            #    2.19  insn per cycle         
       316,669,648      cache-references:u                                          
            12,299      cache-misses:u            #    0.004 % of all cache refs    
            12,299      L1-dcache-load-misses:u   #    0.00% of all L1-dcache accesses
       316,669,648      L1-dcache-loads:u                                           
             6,764      LLC-load-misses:u         #   54.31% of all LL-cache accesses
            12,454      LLC-loads:u                                                 

       0.240487081 seconds time elapsed

       0.240181000 seconds user
       0.000000000 seconds sys

## serial_O2 的perf缓存性能分析
=== Running Performance Test (Serial MD5Hash) ===
ITERATIONS: 100000
Total MD5 calls: 800000

=== Results ===
Total time: 0.242 seconds
Time per MD5 call: 0.303 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './serial_2':

       627,163,890      cycles:u                                                    
     1,311,972,158      instructions:u            #    2.09  insn per cycle         
       297,390,643      cache-references:u                                          
            12,445      cache-misses:u            #    0.004 % of all cache refs    
            12,445      L1-dcache-load-misses:u   #    0.00% of all L1-dcache accesses
       297,390,643      L1-dcache-loads:u                                           
             6,128      LLC-load-misses:u         #   49.86% of all LL-cache accesses
            12,291      LLC-loads:u                                                 

       0.245301912 seconds time elapsed

       0.244689000 seconds user
       0.000000000 seconds sys

## simd_O1 的perf缓存性能分析
=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 0.161 seconds
Time per MD5 call: 0.202 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './simd_1':

       420,389,210      cycles:u                                                    
       836,675,242      instructions:u            #    1.99  insn per cycle         
       246,871,708      cache-references:u                                          
            12,292      cache-misses:u            #    0.005 % of all cache refs    
            12,292      L1-dcache-load-misses:u   #    0.00% of all L1-dcache accesses
       246,871,708      L1-dcache-loads:u                                           
             7,279      LLC-load-misses:u         #   59.00% of all LL-cache accesses
            12,337      LLC-loads:u                                                 

       0.164335617 seconds time elapsed

       0.164081000 seconds user
       0.000000000 seconds sys

## simd_O2 的perf缓存性能分析

=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 0.153 seconds
Time per MD5 call: 0.191 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './simd_2':

       396,834,215      cycles:u                                                    
       778,469,432      instructions:u            #    1.96  insn per cycle         
       240,007,562      cache-references:u                                          
            12,465      cache-misses:u            #    0.005 % of all cache refs    
            12,465      L1-dcache-load-misses:u   #    0.01% of all L1-dcache accesses
       240,007,562      L1-dcache-loads:u                                           
             6,723      LLC-load-misses:u         #   53.51% of all LL-cache accesses
            12,563      LLC-loads:u                                                 

       0.155733768 seconds time elapsed

       0.155404000 seconds user
       0.000000000 seconds sys

===================================================
下面记录为之前的性能测试结果(部分为代码未优化成功版本)，仅供参考，建议重点参考上面的性能测试结果
====================================================


# perf stat ./simd_0
=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 1.689 seconds
Time per MD5 call: 2.111 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './simd_0':

          1,690.61 msec task-clock:u              #    0.999 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
               108      page-faults:u             #    0.064 K/sec                  
     4,377,578,406      cycles:u                  #    2.589 GHz                    
     4,186,009,202      instructions:u            #    0.96  insn per cycle         
   <not supported>      branches:u                                                  
         3,024,920      branch-misses:u                                             

       1.691499447 seconds time elapsed

       1.689817000 seconds user
       0.000000000 seconds sys

# perf stat ./simd_1
=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 0.198 seconds
Time per MD5 call: 0.248 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './simd_1':

            199.82 msec task-clock:u              #    0.997 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
               108      page-faults:u             #    0.540 K/sec                  
       515,228,472      cycles:u                  #    2.578 GHz                    
     1,064,975,362      instructions:u            #    2.07  insn per cycle         
   <not supported>      branches:u                                                  
         1,169,127      branch-misses:u                                             

       0.200514570 seconds time elapsed

       0.200349000 seconds user
       0.000000000 seconds sys

# perf stat ./simd_2
=== Running Performance Test (SIMD MD5Hash) ===
ITERATIONS: 100000
BATCH_SIZE: 4
Total MD5 batches: 200000
Total passwords processed: 800000

=== Results ===
Total time: 0.188 seconds
Time per MD5 call: 0.235 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './simd_2':

            189.73 msec task-clock:u              #    0.997 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
               107      page-faults:u             #    0.564 K/sec                  
       487,628,484      cycles:u                  #    2.570 GHz                    
       999,369,252      instructions:u            #    2.05  insn per cycle         
   <not supported>      branches:u                                                  
           723,509      branch-misses:u                                             

       0.190358778 seconds time elapsed

       0.190198000 seconds user
       0.000000000 seconds sys

# perf stat ./simd_1_optimized
=== Results ===
Total time: 0.163 seconds
Time per MD5 call: 0.204 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './simd_1_y':

            165.24 msec task-clock:u              #    0.995 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
               108      page-faults:u             #    0.654 K/sec                  
       425,389,734      cycles:u                  #    2.574 GHz                    
       862,875,487      instructions:u            #    2.03  insn per cycle         
   <not supported>      branches:u                                                  
           777,274      branch-misses:u                                             

       0.166043553 seconds time elapsed

       0.165727000 seconds user
       0.000000000 seconds sys

# perf stat ./simd_2_optimized
=== Results ===
Total time: 0.156 seconds
Time per MD5 call: 0.195 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './simd_2_y':

            158.07 msec task-clock:u              #    0.995 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
               108      page-faults:u             #    0.683 K/sec                  
       406,343,026      cycles:u                  #    2.571 GHz                    
       801,870,069      instructions:u            #    1.97  insn per cycle         
   <not supported>      branches:u                                                  
           817,628      branch-misses:u                                             

       0.158822467 seconds time elapsed

       0.158544000 seconds user
       0.000000000 seconds sys

# perf stat ./simd_0_macro

Total time: 1.147 seconds
Time per MD5 call: 1.434 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './simd_0_macro':

          1,149.46 msec task-clock:u              #    0.999 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
               113      page-faults:u             #    0.098 K/sec                  
     2,973,817,561      cycles:u                  #    2.587 GHz                    
     2,716,101,418      instructions:u            #    0.91  insn per cycle         
   <not supported>      branches:u                                                  
         2,014,138      branch-misses:u                                             

       1.150304185 seconds time elapsed

       1.149120000 seconds user
       0.000000000 seconds sys

# perf stat -e cache-references,cache-misses,L1-dcache-load-misses ./simd_1_y
 Performance counter stats for './simd_1_y':

       240,914,939      cache-references:u                                          
            12,340      cache-misses:u            #    0.005 % of all cache refs    
            12,340      L1-dcache-load-misses:u                                     

       0.166397742 seconds time elapsed

       0.166193000 seconds user
       0.000000000 seconds sys

# perf stat -e cache-references,cache-misses,L1-dcache-load-misses ./simd_2_y
 Performance counter stats for './simd_2_y':

       234,211,398      cache-references:u                                          
            11,968      cache-misses:u            #    0.005 % of all cache refs    
            11,968      L1-dcache-load-misses:u                                     

       0.158916505 seconds time elapsed

       0.139122000 seconds user
       0.000000000 seconds sys