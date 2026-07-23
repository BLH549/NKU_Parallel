#include "cuda_guess.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>
#include <array>
#include <mutex>

namespace
{
const int CUDA_BLOCK_SIZE = 256;
bool check_cuda(cudaError_t status, const char *where);

// The fused path is called repeatedly for large PTs.  Retaining capacity avoids
// paying five cudaMalloc/cudaFree pairs for every PT.  Calls are serialized by
// the mutex because one device workspace is intentionally shared.
struct FusedWorkspace {
    char *prefix = nullptr, *values = nullptr;
    int *offsets = nullptr, *lengths = nullptr;
    unsigned int *digests = nullptr;
    size_t prefix_cap = 0, values_cap = 0, offsets_cap = 0, lengths_cap = 0, digests_cap = 0;
    std::mutex mutex;
    ~FusedWorkspace() { cudaFree(prefix); cudaFree(values); cudaFree(offsets); cudaFree(lengths); cudaFree(digests); }
    bool grow(void **ptr, size_t &cap, size_t need, const char *where) {
        if (need <= cap) return true;
        void *next = nullptr; size_t next_cap = std::max(need, cap ? cap * 2 : size_t(4096));
        if (!check_cuda(cudaMalloc(&next, next_cap), where)) return false;
        cudaFree(*ptr); *ptr = next; cap = next_cap; return true;
    }
};

FusedWorkspace &fused_workspace() { static FusedWorkspace workspace; return workspace; }
CudaFusedStats &fused_stats() { static CudaFusedStats stats; return stats; }

__device__ __forceinline__ unsigned int rotl(unsigned int x, int n)
{
    return (x << n) | (x >> (32 - n));
}

__device__ __forceinline__ unsigned int md5_f(int round, unsigned int b, unsigned int c, unsigned int d)
{
    if (round < 16) return (b & c) | (~b & d);
    if (round < 32) return (d & b) | (~d & c);
    if (round < 48) return b ^ c ^ d;
    return c ^ (b | ~d);
}

__device__ __forceinline__ int md5_g(int round)
{
    if (round < 16) return round;
    if (round < 32) return (5 * round + 1) & 15;
    if (round < 48) return (3 * round + 5) & 15;
    return (7 * round) & 15;
}

__device__ __forceinline__ unsigned int bswap32(unsigned int x)
{
    return ((x & 0x000000ffU) << 24) | ((x & 0x0000ff00U) << 8) |
           ((x & 0x00ff0000U) >> 8) | ((x & 0xff000000U) >> 24);
}

__constant__ unsigned int MD5_K[64] = {
0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
__constant__ unsigned char MD5_S[64] = {7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};

__global__ void generate_hash_kernel(const char *prefix, int prefix_len,
                                     const char *values, const int *offsets,
                                     const int *lengths, int count, unsigned int *out)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;
    int len = prefix_len + lengths[idx];
    if (len > 55) return;
    unsigned char bytes[64] = {};
    for (int i = 0; i < prefix_len; ++i) bytes[i] = static_cast<unsigned char>(prefix[i]);
    int offset = offsets[idx];
    for (int i = 0; i < lengths[idx]; ++i) bytes[prefix_len + i] = static_cast<unsigned char>(values[offset + i]);
    bytes[len] = 0x80;
    unsigned long long bits = static_cast<unsigned long long>(len) * 8ULL;
    for (int i = 0; i < 8; ++i) bytes[56 + i] = (bits >> (8 * i)) & 0xff;
    unsigned int x[16];
    for (int i = 0; i < 16; ++i) x[i] = unsigned(bytes[4*i]) | (unsigned(bytes[4*i+1]) << 8) | (unsigned(bytes[4*i+2]) << 16) | (unsigned(bytes[4*i+3]) << 24);
    unsigned int a=0x67452301,b=0xefcdab89,c=0x98badcfe,d=0x10325476;
    unsigned int aa=a,bb=b,cc=c,dd=d;
    for (int r = 0; r < 64; ++r)
    {
        unsigned int f = md5_f(r, b, c, d);
        int g = md5_g(r);
        unsigned int t = d;
        d = c;
        c = b;
        b += rotl(a + f + MD5_K[r] + x[g], MD5_S[r]);
        a = t;
    }
    out[4*idx]=bswap32(aa+a); out[4*idx+1]=bswap32(bb+b); out[4*idx+2]=bswap32(cc+c); out[4*idx+3]=bswap32(dd+d);
}

__global__ void generate_kernel(const char *prefix,
                                int prefix_len,
                                const char *values,
                                const int *offsets,
                                const int *lengths,
                                int count,
                                char *out,
                                int stride)
{
    int idx = blockDim.x * blockIdx.x + threadIdx.x;
    if (idx >= count)
    {
        return;
    }

    char *dst = out + static_cast<size_t>(idx) * stride;
    for (int i = 0; i < prefix_len; i += 1)
    {
        dst[i] = prefix[i];
    }

    int value_offset = offsets[idx];
    int value_len = lengths[idx];
    for (int i = 0; i < value_len; i += 1)
    {
        dst[prefix_len + i] = values[value_offset + i];
    }
    dst[prefix_len + value_len] = '\0';
}

bool check_cuda(cudaError_t status, const char *where)
{
    if (status == cudaSuccess)
    {
        return true;
    }

    std::cerr << "CUDA guess generation failed at " << where << ": "
              << cudaGetErrorString(status) << std::endl;
    return false;
}
}

bool IsCUDAGuessAvailable()
{
    int device_count = 0;
    return cudaGetDeviceCount(&device_count) == cudaSuccess && device_count > 0;
}

bool GenerateGuessesCUDA(const std::string &prefix,
                         const std::vector<std::string> &values,
                         size_t begin,
                         size_t count,
                         std::vector<std::string> &out,
                         size_t out_offset)
{
    if (count == 0)
    {
        return true;
    }
    if (begin > values.size() || count > values.size() - begin)
    {
        return false;
    }
    if (out_offset > out.size() || count > out.size() - out_offset)
    {
        return false;
    }
    if (count > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }
    if (prefix.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    int prefix_len = static_cast<int>(prefix.size());
    int max_len = prefix_len;
    size_t value_bytes = 0;
    std::vector<int> offsets(count);
    std::vector<int> lengths(count);

    for (size_t i = 0; i < count; i += 1)
    {
        const std::string &value = values[begin + i];
        if (value.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            return false;
        }
        offsets[i] = static_cast<int>(value_bytes);
        lengths[i] = static_cast<int>(value.size());
        value_bytes += value.size();
        max_len = std::max(max_len, prefix_len + lengths[i]);
    }

    if (value_bytes > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    std::vector<char> packed_values(value_bytes == 0 ? 1 : value_bytes);
    size_t cursor = 0;
    for (size_t i = 0; i < count; i += 1)
    {
        const std::string &value = values[begin + i];
        if (!value.empty())
        {
            std::memcpy(packed_values.data() + cursor, value.data(), value.size());
            cursor += value.size();
        }
    }

    if (max_len == std::numeric_limits<int>::max())
    {
        return false;
    }

    int stride = max_len + 1;
    if (count > std::numeric_limits<size_t>::max() / static_cast<size_t>(stride))
    {
        return false;
    }
    size_t out_bytes = count * static_cast<size_t>(stride);
    std::vector<char> host_out(out_bytes);

    char *d_prefix = nullptr;
    char *d_values = nullptr;
    int *d_offsets = nullptr;
    int *d_lengths = nullptr;
    char *d_out = nullptr;

    bool ok = true;
    ok = ok && check_cuda(cudaMalloc(reinterpret_cast<void **>(&d_prefix),
                                      std::max<size_t>(1, prefix.size())),
                           "cudaMalloc prefix");
    ok = ok && check_cuda(cudaMalloc(reinterpret_cast<void **>(&d_values), packed_values.size()),
                           "cudaMalloc values");
    ok = ok && check_cuda(cudaMalloc(reinterpret_cast<void **>(&d_offsets), count * sizeof(int)),
                           "cudaMalloc offsets");
    ok = ok && check_cuda(cudaMalloc(reinterpret_cast<void **>(&d_lengths), count * sizeof(int)),
                           "cudaMalloc lengths");
    ok = ok && check_cuda(cudaMalloc(reinterpret_cast<void **>(&d_out), out_bytes),
                           "cudaMalloc output");

    if (ok && !prefix.empty())
    {
        ok = check_cuda(cudaMemcpy(d_prefix, prefix.data(), prefix.size(), cudaMemcpyHostToDevice),
                        "cudaMemcpy prefix");
    }
    if (ok)
    {
        ok = check_cuda(cudaMemcpy(d_values, packed_values.data(), packed_values.size(), cudaMemcpyHostToDevice),
                        "cudaMemcpy values");
    }
    if (ok)
    {
        ok = check_cuda(cudaMemcpy(d_offsets, offsets.data(), count * sizeof(int), cudaMemcpyHostToDevice),
                        "cudaMemcpy offsets");
    }
    if (ok)
    {
        ok = check_cuda(cudaMemcpy(d_lengths, lengths.data(), count * sizeof(int), cudaMemcpyHostToDevice),
                        "cudaMemcpy lengths");
    }

    if (ok)
    {
        int blocks = (static_cast<int>(count) + CUDA_BLOCK_SIZE - 1) / CUDA_BLOCK_SIZE;
        generate_kernel<<<blocks, CUDA_BLOCK_SIZE>>>(d_prefix,
                                                     prefix_len,
                                                     d_values,
                                                     d_offsets,
                                                     d_lengths,
                                                     static_cast<int>(count),
                                                     d_out,
                                                     stride);
        ok = check_cuda(cudaGetLastError(), "generate_kernel launch");
    }
    if (ok)
    {
        ok = check_cuda(cudaMemcpy(host_out.data(), d_out, out_bytes, cudaMemcpyDeviceToHost),
                        "cudaMemcpy output");
    }

    cudaFree(d_prefix);
    cudaFree(d_values);
    cudaFree(d_offsets);
    cudaFree(d_lengths);
    cudaFree(d_out);

    if (!ok)
    {
        return false;
    }

    for (size_t i = 0; i < count; i += 1)
    {
        out[out_offset + i].assign(host_out.data() + i * static_cast<size_t>(stride));
    }
    return true;
}

bool GenerateAndHashGuessesCUDA(const std::string &prefix,
                                const std::vector<std::string> &values,
                                size_t begin, size_t count,
                                std::vector<std::array<unsigned int, 4>> &digests,
                                std::vector<size_t> &long_indices)
{
    using clock = std::chrono::steady_clock;
    auto pack_start = clock::now();
    if (begin > values.size() || count > values.size() - begin ||
        count > static_cast<size_t>(std::numeric_limits<int>::max())) return false;
    std::vector<int> offsets(count), lengths(count);
    size_t bytes = 0;
    long_indices.clear();
    for (size_t i=0;i<count;++i) {
        const std::string &v=values[begin+i];
        if (prefix.size()+v.size()>55) long_indices.push_back(begin+i);
        if (v.size()>static_cast<size_t>(std::numeric_limits<int>::max()) || bytes>static_cast<size_t>(std::numeric_limits<int>::max())-v.size()) return false;
        offsets[i]=static_cast<int>(bytes); lengths[i]=static_cast<int>(v.size()); bytes+=v.size();
    }
    std::vector<char> packed(bytes ? bytes : 1);
    size_t cursor=0; for(size_t i=0;i<count;++i) { const std::string &v=values[begin+i]; std::memcpy(packed.data()+cursor,v.data(),v.size()); cursor+=v.size(); }
    auto pack_end = clock::now();
    FusedWorkspace &workspace = fused_workspace();
    std::lock_guard<std::mutex> lock(workspace.mutex);
    bool ok=workspace.grow(reinterpret_cast<void **>(&workspace.prefix), workspace.prefix_cap, std::max<size_t>(1,prefix.size()), "fused grow prefix") &&
            workspace.grow(reinterpret_cast<void **>(&workspace.values), workspace.values_cap, packed.size(), "fused grow values") &&
            workspace.grow(reinterpret_cast<void **>(&workspace.offsets), workspace.offsets_cap, count*sizeof(int), "fused grow offsets");
    if(ok) ok=workspace.grow(reinterpret_cast<void **>(&workspace.lengths), workspace.lengths_cap, count*sizeof(int), "fused grow lengths") &&
              workspace.grow(reinterpret_cast<void **>(&workspace.digests), workspace.digests_cap, count*4*sizeof(unsigned int), "fused grow digests");

    cudaEvent_t h2d_start = nullptr, h2d_stop = nullptr;
    cudaEvent_t kernel_start = nullptr, kernel_stop = nullptr;
    cudaEvent_t d2h_start = nullptr, d2h_stop = nullptr;
    float h2d_ms = 0.0f, kernel_ms = 0.0f, d2h_ms = 0.0f;
    if(ok) ok=check_cuda(cudaEventCreate(&h2d_start),"fused event h2d start") &&
              check_cuda(cudaEventCreate(&h2d_stop),"fused event h2d stop") &&
              check_cuda(cudaEventCreate(&kernel_start),"fused event kernel start") &&
              check_cuda(cudaEventCreate(&kernel_stop),"fused event kernel stop") &&
              check_cuda(cudaEventCreate(&d2h_start),"fused event d2h start") &&
              check_cuda(cudaEventCreate(&d2h_stop),"fused event d2h stop");
    if(ok) ok=check_cuda(cudaEventRecord(h2d_start),"fused record h2d start");
    if(ok && !prefix.empty()) ok=check_cuda(cudaMemcpy(workspace.prefix,prefix.data(),prefix.size(),cudaMemcpyHostToDevice),"fused copy prefix");
    if(ok) ok=check_cuda(cudaMemcpy(workspace.values,packed.data(),packed.size(),cudaMemcpyHostToDevice),"fused copy values");
    if(ok) ok=check_cuda(cudaMemcpy(workspace.offsets,offsets.data(),count*sizeof(int),cudaMemcpyHostToDevice),"fused copy offsets");
    if(ok) ok=check_cuda(cudaMemcpy(workspace.lengths,lengths.data(),count*sizeof(int),cudaMemcpyHostToDevice),"fused copy lengths");
    if(ok) ok=check_cuda(cudaEventRecord(h2d_stop),"fused record h2d stop") &&
              check_cuda(cudaEventSynchronize(h2d_stop),"fused sync h2d stop") &&
              check_cuda(cudaEventElapsedTime(&h2d_ms,h2d_start,h2d_stop),"fused elapsed h2d");
    std::vector<unsigned int> raw(count*4);
    if(ok) {
        ok=check_cuda(cudaEventRecord(kernel_start),"fused record kernel start");
        if(ok) {
            generate_hash_kernel<<<(static_cast<int>(count)+255)/256,256>>>(workspace.prefix,static_cast<int>(prefix.size()),workspace.values,workspace.offsets,workspace.lengths,static_cast<int>(count),workspace.digests);
            ok=check_cuda(cudaGetLastError(),"fused kernel");
        }
        if(ok) ok=check_cuda(cudaEventRecord(kernel_stop),"fused record kernel stop") &&
                  check_cuda(cudaEventSynchronize(kernel_stop),"fused sync kernel stop") &&
                  check_cuda(cudaEventElapsedTime(&kernel_ms,kernel_start,kernel_stop),"fused elapsed kernel");
    }
    if(ok) ok=check_cuda(cudaEventRecord(d2h_start),"fused record d2h start");
    if(ok) ok=check_cuda(cudaMemcpy(raw.data(),workspace.digests,raw.size()*sizeof(unsigned int),cudaMemcpyDeviceToHost),"fused copy digest");
    if(ok) ok=check_cuda(cudaEventRecord(d2h_stop),"fused record d2h stop") &&
              check_cuda(cudaEventSynchronize(d2h_stop),"fused sync d2h stop") &&
              check_cuda(cudaEventElapsedTime(&d2h_ms,d2h_start,d2h_stop),"fused elapsed d2h");
    cudaEventDestroy(h2d_start); cudaEventDestroy(h2d_stop);
    cudaEventDestroy(kernel_start); cudaEventDestroy(kernel_stop);
    cudaEventDestroy(d2h_start); cudaEventDestroy(d2h_stop);
    if(!ok) return false;
    digests.clear(); digests.reserve(count-long_indices.size());
    for(size_t i=0;i<count;++i) if(prefix.size()+values[begin+i].size()<=55) digests.push_back({raw[4*i],raw[4*i+1],raw[4*i+2],raw[4*i+3]});
    CudaFusedStats &stats = fused_stats();
    stats.calls += 1;
    stats.candidates += count;
    stats.long_candidates += long_indices.size();
    stats.pack_seconds += std::chrono::duration<double>(pack_end - pack_start).count();
    stats.h2d_seconds += h2d_ms / 1000.0;
    stats.kernel_seconds += kernel_ms / 1000.0;
    stats.d2h_seconds += d2h_ms / 1000.0;
    return true;
}

CudaFusedStats GetCudaFusedStats()
{
    return fused_stats();
}

void ResetCudaFusedStats()
{
    fused_stats() = {};
}
