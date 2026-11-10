#include "pch.h"
#include "CppUnitTest.h"
#include <libplacebo/config.h>
#include <string>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace { void GpuDependencyModuleAnchor() {} }
namespace Tests {
TEST_CLASS(RendererDependencyTests) { public:
    TEST_METHOD(GpuTestsLoadTheSelectedRendererDependency) {
#ifdef VP_LIBPLACEBO_BETA
        static_assert(PL_API_VER == 388, "Beta tests need the beta headers");
        const auto module=L"libplacebo-388.dll";
        const auto version="7.388";
#else
        static_assert(PL_API_VER == 360, "Standard tests need the standard headers");
        const auto module=L"libplacebo-360.dll";
        const auto version="7.360";
#endif
        const auto actual=std::string(pl_version());
        Logger::WriteMessage(actual.c_str());
        Assert::IsTrue(actual.find(version)!=std::string::npos,
            L"GPU regressions must execute against the selected shipped dependency");
        const auto loaded=GetModuleHandleW(module);
        Assert::IsTrue(loaded!=nullptr);
        wchar_t dependencyPath[32768]{};
        Assert::IsTrue(GetModuleFileNameW(loaded,dependencyPath,32768)>0);
        Logger::WriteMessage(dependencyPath);
        HMODULE testModule=nullptr;
        Assert::IsTrue(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GpuDependencyModuleAnchor),&testModule)!=0);
        wchar_t testPath[32768]{};
        Assert::IsTrue(GetModuleFileNameW(testModule,testPath,32768)>0);
        const auto parent=[](const wchar_t* path) {
            const auto text=std::wstring(path);return text.substr(0,text.find_last_of(L"\\/"));
        };
        Assert::IsTrue(_wcsicmp(parent(dependencyPath).c_str(),parent(testPath).c_str())==0,
            L"GPU tests must load their isolated bundled dependency");
    }
};
}
