# PCFG Password Guessing Parallel Programming Assignment

南开大学《并行程序设计》课程作业。项目以 PCFG 口令猜测程序为基础，分别完成了 SIMD、OpenMP/Pthread、MPI 和 CUDA 相关实验，并在期末报告中汇总了各阶段的并行化思路与结果。

## 完成内容

本项目围绕 PCFG 口令猜测程序的 Train、Guess 和 Hash 三个阶段进行并行优化。主要工作包括：

- 在 Hash 阶段使用 ARM NEON 实现 4 路 SIMD MD5，对多条候选口令同时计算哈希。
- 在 Guess 阶段使用 OpenMP 按任务规模并行展开候选口令，并通过预分配数组直接写入、优先队列堆化等方式减少额外开销。
- 在 Train 阶段利用统计计数的可加性，将训练集拆分为局部模型后再合并，降低训练耗时。
- 在 MPI 版本中实现主进程生成任务、工作进程计算哈希的分工，并进一步尝试训练分片、生成和哈希流水线。
- 针对 MPI 字符串分发开销较大的问题，期末新增了 PT 模板和值范围分发方式，让工作进程在本地生成候选并计算哈希，减少反复传输完整字符串的通信成本。
- 在 CUDA 部分尝试将候选生成以及生成/MD5 融合计算迁移到 GPU，并保留 CPU 回退实现。该部分主要作为课程中的 GPU 实验探索。

## 内容

- 根目录：CPU、OpenMP 和 MPI 版本的主要源码与提交脚本。
- `cuda/`：CUDA 候选生成及融合生成/MD5 的实验代码，包含 CPU 回退实现和最小测试程序。
- `report/Final/main.pdf`：期末实验报告。
- `documents/口令猜测选题介绍.pdf`：课程选题说明。

## 构建

CPU/OpenMP 版本可在项目根目录运行：

```sh
mkdir -p build
g++ -O2 -fopenmp main.cpp train.cpp guessing.cpp md5.cpp \
  cuda/cuda_guess_stub.cpp -o build/main
```

MPI 版本可使用：

```sh
mkdir -p build
mpicxx -O2 -fopenmp mpi_main.cpp train.cpp guessing.cpp md5.cpp \
  -o build/mpi_main
```

如已配置 CUDA Toolkit 和运行时库，可在项目根目录执行：

```sh
./cuda/build_gpu.sh
```

CUDA 融合生成/MD5 路径保留为独立实验和测试代码，尚未接入主程序的完整运行流程。实验所需训练数据和口令数据未包含在仓库中。
