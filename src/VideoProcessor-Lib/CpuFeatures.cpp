#include <pch.h>
#include "CpuFeatures.h"
#include <intrin.h>

namespace CpuFeatures
{
    namespace
    {
        bool Detect() noexcept
        {
            // Diagnostic opt-out is read once per process. It can never enable
            // unsupported instructions; launch a fresh process for each mode.
            wchar_t disabled[2] = {};
            if (GetEnvironmentVariableW(L"VP_DISABLE_AVX2", disabled, 2) == 1 && disabled[0] == L'1')
                return false;
            int regs[4] = {};
            __cpuid(regs, 0);
            Snapshot cpu{};
            cpu.maxLeaf = static_cast<uint32_t>(regs[0]);
            if (cpu.maxLeaf < 7)
                return false;
            __cpuidex(regs, 1, 0);
            cpu.leaf1Ecx = static_cast<uint32_t>(regs[2]);
            if ((cpu.leaf1Ecx & RequiredLeaf1) != RequiredLeaf1)
                return false;
            // XGETBV itself is guarded by XSAVE and OSXSAVE above.
            cpu.xcr0 = _xgetbv(0);
            __cpuidex(regs, 7, 0);
            cpu.leaf7Ebx = static_cast<uint32_t>(regs[1]);
            return CanUseAvx2Kernels(cpu);
        }
    }
    bool SupportsAvx2Kernels() noexcept
    {
        static const bool supported = Detect();
        return supported;
    }
}
