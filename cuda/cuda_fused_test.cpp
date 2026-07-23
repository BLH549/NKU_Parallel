#include "cuda_guess.h"
#include "md5.h"

#include <iomanip>
#include <iostream>

int main()
{
    const std::string prefix = "pre";
    const std::vector<std::string> values = {"fix", "123", "", std::string(53, 'x')};
    std::vector<std::array<unsigned int, 4>> gpu;
    std::vector<size_t> long_indices;
    if (!IsCUDAGuessAvailable() ||
        !GenerateAndHashGuessesCUDA(prefix, values, 0, values.size(), gpu, long_indices)) {
        std::cerr << "fused CUDA path unavailable or failed\n";
        return 1;
    }
    if (gpu.size() != 3 || long_indices.size() != 1 || long_indices[0] != 3) return 2;
    for (size_t i = 0; i < gpu.size(); ++i) {
        bit32 expected[4]; MD5Hash(prefix + values[i], expected);
        for (int word = 0; word < 4; ++word)
            if (gpu[i][word] != expected[word]) {
                std::cerr << "digest mismatch for candidate " << prefix + values[i]
                          << ", word " << word << "\n";
                std::cerr << "expected:";
                for (int j = 0; j < 4; ++j)
                    std::cerr << " " << std::hex << std::setw(8) << std::setfill('0') << expected[j];
                std::cerr << "\ngot:     ";
                for (int j = 0; j < 4; ++j)
                    std::cerr << " " << std::hex << std::setw(8) << std::setfill('0') << gpu[i][j];
                std::cerr << std::dec << "\n";
                return 3;
            }
    }
    std::cout << "CUDA fused MD5 test passed\n";
}
