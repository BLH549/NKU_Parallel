#ifndef CUDA_GUESS_H
#define CUDA_GUESS_H

#include <string>
#include <array>
#include <vector>

struct CudaFusedStats
{
    size_t calls = 0;
    size_t candidates = 0;
    size_t long_candidates = 0;
    double pack_seconds = 0.0;
    double h2d_seconds = 0.0;
    double kernel_seconds = 0.0;
    double d2h_seconds = 0.0;
};

// Returns true when CUDA generation completed successfully. CPU builds link the
// stub implementation, which returns false and lets callers use their fallback.
bool IsCUDAGuessAvailable();

bool GenerateGuessesCUDA(const std::string &prefix,
                         const std::vector<std::string> &values,
                         size_t begin,
                         size_t count,
                         std::vector<std::string> &out,
                         size_t out_offset);

// Fused short-password path.  Each candidate is logically prefix + values[i],
// but only its MD5 digest is copied back.  Candidates longer than 55 bytes are
// reported in long_indices and must be handled by the CPU fallback.
bool GenerateAndHashGuessesCUDA(const std::string &prefix,
                                const std::vector<std::string> &values,
                                size_t begin,
                                size_t count,
                                std::vector<std::array<unsigned int, 4>> &digests,
                                std::vector<size_t> &long_indices);

CudaFusedStats GetCudaFusedStats();
void ResetCudaFusedStats();

#endif
