# perf stat -d ./main_serial_1 (O1)
Guess time:0.641295seconds
Hash time:3.01984seconds
Train time:30.515seconds


 Performance counter stats for './main_serial_1':

         38,719.28 msec task-clock:u              #    1.000 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
            64,399      page-faults:u             #    0.002 M/sec                  
    98,295,823,281      cycles:u                  #    2.539 GHz                    
    60,430,587,473      instructions:u            #    0.61  insn per cycle         
   <not supported>      branches:u                                                  
       630,034,763      branch-misses:u                                             
    21,734,761,547      L1-dcache-loads:u         #  561.342 M/sec                  
       931,365,210      L1-dcache-load-misses:u   #    4.29% of all L1-dcache accesses
     1,703,728,726      LLC-loads:u               #   44.002 M/sec                  
       255,756,601      LLC-load-misses:u         #   15.01% of all LL-cache accesses

      38.729226436 seconds time elapsed

      38.139313000 seconds user
       0.486695000 seconds sys



# main_serial_O1 的perf缓存性能分析 
```
perf stat -e cycles,instructions,cache-references,cache-misses,\
> L1-dcache-load-misses,L1-dcache-loads,\
> LLC-load-misses,LLC-loads \
> ./main_serial_1
```

Guess time:0.598392seconds
Hash time:2.92278seconds
Train time:30.9425seconds

 Performance counter stats for './main_serial_1':

    99,837,378,439      cycles:u                                                    
    60,430,800,211      instructions:u            #    0.61  insn per cycle         
    21,748,903,460      cache-references:u                                          
       964,068,545      cache-misses:u            #    4.433 % of all cache refs    
       964,068,545      L1-dcache-load-misses:u   #    4.43% of all L1-dcache accesses
    21,748,903,460      L1-dcache-loads:u                                           
       168,559,928      LLC-load-misses:u         #    9.82% of all LL-cache accesses
     1,716,349,851      LLC-loads:u                                                 

      39.269518563 seconds time elapsed

      38.710100000 seconds user
       0.467550000 seconds sys




# 最终优化版本的perf stat分析(O1)

Guess time:0.584681seconds
Hash time:1.12089seconds
Train time:30.2739seconds

 Performance counter stats for './main_simd_1':

         36,395.91 msec task-clock:u              #    1.000 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
            66,397      page-faults:u             #    0.002 M/sec                  
    91,828,521,470      cycles:u                  #    2.523 GHz                    
    47,797,149,758      instructions:u            #    0.52  insn per cycle         
   <not supported>      branches:u                                                  
       638,283,474      branch-misses:u                                             
    18,641,278,142      L1-dcache-loads:u         #  512.181 M/sec                  
       941,422,979      L1-dcache-load-misses:u   #    5.05% of all L1-dcache accesses
     1,675,389,073      LLC-loads:u               #   46.032 M/sec                  
       281,458,861      LLC-load-misses:u         #   16.80% of all LL-cache accesses

      36.405383142 seconds time elapsed

      35.909629000 seconds user
       0.367022000 seconds sys


# 最终优化simd版本的perf缓存性能分析(O1)
Guess time:0.590894seconds
Hash time:1.10927seconds
Train time:27.9577seconds

 Performance counter stats for './main_simd_1':

    85,498,677,284      cycles:u                                                    
    47,797,149,819      instructions:u            #    0.56  insn per cycle         
    18,697,210,173      cache-references:u                                          
       909,867,083      cache-misses:u            #    4.866 % of all cache refs    
       909,867,083      L1-dcache-load-misses:u   #    4.87% of all L1-dcache accesses
    18,697,210,173      L1-dcache-loads:u                                           
       160,767,474      LLC-load-misses:u         #    9.52% of all LL-cache accesses
     1,688,492,728      LLC-loads:u                                                 

      33.689217408 seconds time elapsed

      33.125956000 seconds user
       0.474757000 seconds sys








================================================================
# 下面为之前的测试结果，供参考，不是最终优化版本
===============================================================
## perf stat -d ./main_simd_1 (O1) 不是最终优化版本
Guess time:0.590418seconds
Hash time:2.48219seconds
Train time:30.3875seconds

 Performance counter stats for './main_simd_1':

         38,167.83 msec task-clock:u              #    1.000 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
           262,181      page-faults:u             #    0.007 M/sec                  
    95,898,431,190      cycles:u                  #    2.513 GHz                    
    52,497,387,402      instructions:u            #    0.55  insn per cycle         
   <not supported>      branches:u                                                  
       636,784,173      branch-misses:u                                             
    20,173,000,384      L1-dcache-loads:u         #  528.534 M/sec                  
     1,024,219,887      L1-dcache-load-misses:u   #    5.08% of all L1-dcache accesses
     1,821,326,650      LLC-loads:u               #   47.719 M/sec                  
       227,129,312      LLC-load-misses:u         #   12.47% of all LL-cache accesses

      38.177097139 seconds time elapsed

      37.198325000 seconds user
       0.881688000 seconds sys


## main_simd_O1 的perf缓存性能分析 不是最终版
``` 
perf stat -e cycles,instructions,cache-references,cache-misses,\
> L1-dcache-load-misses,L1-dcache-loads,\
> LLC-load-misses,LLC-loads \
> ./main_simd_1
```
Guess time:0.583571seconds
Hash time:2.5324seconds
Train time:25.1119seconds

 Performance counter stats for './main_simd_1':

    79,060,458,478      cycles:u                                                    
    52,497,387,391      instructions:u            #    0.66  insn per cycle         
    20,012,767,098      cache-references:u                                          
       965,684,266      cache-misses:u            #    4.825 % of all cache refs    
       965,684,266      L1-dcache-load-misses:u   #    4.83% of all L1-dcache accesses
    20,012,767,098      L1-dcache-loads:u                                           
       258,736,993      LLC-load-misses:u         #   14.49% of all LL-cache accesses
     1,785,124,915      LLC-loads:u                                                 

      31.580713537 seconds time elapsed

      30.637613000 seconds user
       0.857639000 seconds sys


## 只有md5的perf分析
```
perf stat -e cycles,instructions,cache-references,cache-misses,\
> L1-dcache-load-misses,L1-dcache-loads,\
> LLC-load-misses,LLC-loads \
> ./test_md5_simd_1
```

=== Results ===
Total time: 0.134 seconds
Time per MD5 call: 0.167 microseconds
Checksum (verification): 6fc6123a05000

 Performance counter stats for './test_md5_simd_1':

       348,695,907      cycles:u                                                    
       632,473,208      instructions:u            #    1.81  insn per cycle         
       168,532,602      cache-references:u                                          
            12,153      cache-misses:u            #    0.007 % of all cache refs    
            12,153      L1-dcache-load-misses:u   #    0.01% of all L1-dcache accesses
       168,532,602      L1-dcache-loads:u                                           
             7,135      LLC-load-misses:u         #   57.65% of all LL-cache accesses
            12,376      LLC-loads:u                                                 

       0.136459729 seconds time elapsed

       0.136368000 seconds user
       0.000000000 seconds sys
