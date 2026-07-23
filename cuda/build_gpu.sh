#!/bin/sh
set -eu

CXX=${CXX:-g++}
NVCC=${NVCC:-nvcc}
CXXFLAGS=${CXXFLAGS:-"-O2 -fopenmp"}
NVCCFLAGS=${NVCCFLAGS:-"-O2"}
CUDA_LIB_DIR=${CUDA_LIB_DIR:-"${CONDA_PREFIX:-}/lib"}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/build/cuda"}

if ! command -v "$NVCC" >/dev/null 2>&1; then
    echo "nvcc not found. Build the CPU fallback with:" >&2
    echo "  $CXX main.cpp train.cpp guessing.cpp md5.cpp cuda_guess_stub.cpp -o main -O2 -fopenmp" >&2
    exit 1
fi

if [ -z "$CUDA_LIB_DIR" ] || [ ! -f "$CUDA_LIB_DIR/libcudart.so" ]; then
    echo "libcudart.so not found. Run this script inside the conda CUDA environment, for example:" >&2
    echo "  conda run -n dl ./build_gpu.sh" >&2
    exit 1
fi

build_dir=${TMPDIR:-/tmp}/guess_cuda_build_$$
mkdir -p "$build_dir"
mkdir -p "$BUILD_DIR"
trap 'rm -rf "$build_dir"' EXIT INT TERM

"$NVCC" $NVCCFLAGS -c "$SCRIPT_DIR/cuda_guess.cu" -o "$build_dir/cuda_guess.o"

"$CXX" "$ROOT_DIR/main.cpp" "$ROOT_DIR/train.cpp" "$ROOT_DIR/guessing.cpp" "$ROOT_DIR/md5.cpp" "$build_dir/cuda_guess.o" \
    -o "$BUILD_DIR/main" $CXXFLAGS -L"$CUDA_LIB_DIR" -lcudart -Wl,-rpath,"$CUDA_LIB_DIR"

"$CXX" "$ROOT_DIR/correctness_guess.cpp" "$ROOT_DIR/train.cpp" "$ROOT_DIR/guessing.cpp" "$ROOT_DIR/md5.cpp" "$build_dir/cuda_guess.o" \
    -o "$BUILD_DIR/correctness_guess" $CXXFLAGS -L"$CUDA_LIB_DIR" -lcudart -Wl,-rpath,"$CUDA_LIB_DIR"

"$CXX" "$SCRIPT_DIR/cuda_fused_test.cpp" "$ROOT_DIR/md5.cpp" "$build_dir/cuda_guess.o" \
    -o "$BUILD_DIR/cuda_fused_test" -O2 -L"$CUDA_LIB_DIR" -lcudart -Wl,-rpath,"$CUDA_LIB_DIR"
