#include <pch.h>
#include "CpuFeatures.h"
#include <intrin.h>

namespace CpuFeatures
{
    // Defined in a separate AVX2 object with no LTCG or startup initializers.
    __declspec(noinline) void ExecuteAvx2Probe(const int* input, int* output) noexcept;
    namespace
    {
        Diagnostics Detect() noexcept
        {
            Diagnostics result{};
            wchar_t disabled[2] = {};
            result.disabledByEnvironment =
                GetEnvironmentVariableW(L"VP_DISABLE_AVX2", disabled, 2) == 1 && disabled[0] == L'1';
            int regs[4] = {};
            __cpuid(regs, 0);
            result.cpu.maxLeaf = static_cast<uint32_t>(regs[0]);
            if (result.cpu.maxLeaf >= 1)
            {
                __cpuidex(regs, 1, 0);
                result.cpu.leaf1Ecx = static_cast<uint32_t>(regs[2]);
                // XGETBV requires both CPU XSAVE and OSXSAVE. No AVX2
                // instruction is executed while detecting capabilities.
                if (CanReadXcr0(result.cpu.leaf1Ecx))
                {
                    result.cpu.xcr0 = _xgetbv(0);
                    result.xgetbvRead = true;
                }
            }
            if (result.cpu.maxLeaf >= 7)
            {
                __cpuidex(regs, 7, 0);
                result.cpu.leaf7Ebx = static_cast<uint32_t>(regs[1]);
            }
            return result;
        }
    }
    const Diagnostics& GetDiagnostics() noexcept
    {
        static const Diagnostics diagnostics = Detect();
        return diagnostics;
    }
    bool SupportsAvx2Kernels() noexcept
    {
        // Keep the hot dispatch path a cached bool, with no logging or probing.
        static const bool supported = !GetDiagnostics().disabledByEnvironment &&
            CanUseAvx2Kernels(GetDiagnostics().cpu);
        return supported;
    }
    ProbeResult RunAvx2Probe() noexcept
    {
        const int input[8] = { 11, 23, 37, 41, 59, 61, 73, 89 };
        int output[8] = {};
        __try
        {
            ExecuteAvx2Probe(input, output);
        }
        __except (GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION ?
            EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            return ProbeResult::Unsupported;
        }
        for (int i = 0; i < 8; ++i)
            if (output[i] != input[7 - i] + input[i])
                return ProbeResult::IncorrectResult;
        return ProbeResult::Pass;
    }
}
