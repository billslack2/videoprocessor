#pragma once
#include <cstdint>

namespace CpuFeatures
{
    // Pure decoding permits tests for CPUs and OS configurations unavailable locally.
    struct Snapshot
    {
        uint32_t maxLeaf;
        uint32_t leaf1Ecx;
        uint32_t leaf7Ebx;
        uint64_t xcr0;
    };
    constexpr uint32_t RequiredLeaf1 = (1u << 26) | (1u << 27) | (1u << 28) | (1u << 12);
    constexpr uint32_t RequiredLeaf7 = (1u << 5) | (1u << 3) | (1u << 8);
    // /arch:AVX2 can also generate FMA and BMI instructions. Require that whole
    // target, plus OS-enabled XMM/YMM state, before entering an AVX2 object.
    constexpr bool CanUseAvx2Kernels(const Snapshot& cpu) noexcept
    {
        return cpu.maxLeaf >= 7 &&
            (cpu.leaf1Ecx & RequiredLeaf1) == RequiredLeaf1 &&
            (cpu.leaf7Ebx & RequiredLeaf7) == RequiredLeaf7 &&
            (cpu.xcr0 & 6) == 6;
    }
    bool SupportsAvx2Kernels() noexcept;
}
