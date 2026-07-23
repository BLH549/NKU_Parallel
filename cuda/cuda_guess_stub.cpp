#include "cuda_guess.h"

bool IsCUDAGuessAvailable()
{
    return false;
}

bool GenerateGuessesCUDA(const std::string &,
                         const std::vector<std::string> &,
                         size_t,
                         size_t,
                         std::vector<std::string> &,
                         size_t)
{
    return false;
}

bool GenerateAndHashGuessesCUDA(const std::string &, const std::vector<std::string> &,
                                size_t, size_t,
                                std::vector<std::array<unsigned int, 4>> &,
                                std::vector<size_t> &)
{
    return false;
}

CudaFusedStats GetCudaFusedStats()
{
    return {};
}

void ResetCudaFusedStats()
{
}
