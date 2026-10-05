// No PCH, shared project headers, dynamic initialization, or LTCG here.
#include <immintrin.h>
namespace CpuFeatures
{
    __declspec(noinline) void ExecuteAvx2Probe(const int* input, int* output) noexcept
    {
        const __m256i values = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(input));
        const __m256i reverse = _mm256_setr_epi32(7, 6, 5, 4, 3, 2, 1, 0);
        const __m256i shuffled = _mm256_permutevar8x32_epi32(values, reverse);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(output), _mm256_add_epi32(shuffled, values));
        _mm256_zeroupper();
    }
}
