#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/PassiveDisplayObserver.h>
#include <cstdlib>
#include <TlHelp32.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    class ScopedObserverEnvironmentOverride
    {
    public:
        ScopedObserverEnvironmentOverride(const wchar_t* value = L"0")
        {
            const int error = _wdupenv_s(
                &originalValue_, &originalLength_, L"VP_DISPLAY_OBSERVER");
            if (error != 0)
                return;
            captured_ = true;
            hadOriginalValue_ = originalValue_ != nullptr;
            setError_ = _wputenv_s(L"VP_DISPLAY_OBSERVER", value);
        }

        ~ScopedObserverEnvironmentOverride()
        {
            if (captured_)
            {
                _wputenv_s(L"VP_DISPLAY_OBSERVER",
                    hadOriginalValue_ ? originalValue_ : L"");
            }
            free(originalValue_);
        }

        bool Ready() const { return captured_ && setError_ == 0; }

    private:
        wchar_t* originalValue_ = nullptr;
        size_t originalLength_ = 0;
        int setError_ = 0;
        bool captured_ = false;
        bool hadOriginalValue_ = false;
    };
    DWORD OwnObserverChild() {
        HANDLE list=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if(list==INVALID_HANDLE_VALUE) return 0;
        PROCESSENTRY32W entry{}; entry.dwSize=sizeof(entry); DWORD found=0;
        if(Process32FirstW(list,&entry)) do {
            if(entry.th32ParentProcessID==GetCurrentProcessId() &&
                _wcsicmp(entry.szExeFile,L"VideoProcessorDisplayObserver.exe")==0) { found=entry.th32ProcessID; break; }
        } while(Process32NextW(list,&entry));
        CloseHandle(list); return found;
    }
    bool CollectorTestsEnabled() {
        wchar_t value[8]{};
        return GetEnvironmentVariableW(L"VP_TEST_PASSIVE_OBSERVER",value,_countof(value)) && wcscmp(value,L"1")==0;
    }
}

namespace Tests
{
    TEST_CLASS(PassiveDisplayObserverTests)
    {
    public:
        // Explicit opt-in integration test. The runner stages the helper beside
        // this test DLL; it targets only this test process and draws no window.
        TEST_METHOD(OptInCollectorStartsResetsAndRecoversFromItsOwnChildFailure)
        {
            if(!CollectorTestsEnabled()) { Logger::WriteMessage("Collector lifecycle not exercised: set VP_TEST_PASSIVE_OBSERVER=1 and stage the helper beside the test DLL."); return; }
            ScopedObserverEnvironmentOverride enabled(L"1");
            Assert::IsTrue(enabled.Ready()); Assert::AreEqual(DWORD(0),OwnObserverChild());
            {
                PassiveDisplayObserver observer; observer.Start();
                RendererPassiveDisplayStats state;
                const auto started=GetTickCount64();
                do { Sleep(20); state=observer.Snapshot(GetTickCount64()); }
                while((!state.running || state.errorCode) && GetTickCount64()-started<7000);
                Assert::IsTrue(state.running && state.errorCode==0,L"Collector did not report healthy startup.");
                Assert::IsFalse(state.fresh); // A healthy trace alone proves no output.
                LARGE_INTEGER qpc{}; QueryPerformanceCounter(&qpc);
                observer.Reset(1,qpc.QuadPart);
                const auto resetAt=GetTickCount64();
                do { Sleep(20); state=observer.Snapshot(GetTickCount64()); }
                while(state.appliedResetSerial<1 && GetTickCount64()-resetAt<2000);
                Assert::AreEqual(uint64_t(1),state.appliedResetSerial);
                const DWORD childId=OwnObserverChild(); Assert::IsTrue(childId!=0);
                HANDLE child=OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,childId);
                Assert::IsTrue(child!=nullptr);
                const bool terminated=TerminateProcess(child,1234)!=FALSE;
                CloseHandle(child); Assert::IsTrue(terminated);
                const auto failedAt=GetTickCount64();
                do { Sleep(20); state=observer.Snapshot(GetTickCount64()); }
                while(state.running && GetTickCount64()-failedAt<7000);
                Assert::IsTrue(state.supported); Assert::IsFalse(state.running); Assert::IsFalse(state.fresh);
                QueryPerformanceCounter(&qpc); observer.Reset(2,qpc.QuadPart);
                const auto secondResetAt=GetTickCount64();
                do { Sleep(20); state=observer.Snapshot(GetTickCount64()); }
                while(state.appliedResetSerial<2 && GetTickCount64()-secondResetAt<2000);
                Assert::AreEqual(uint64_t(2),state.appliedResetSerial);
                Assert::AreEqual(uint64_t(0),state.unexpectedDrops);
            }
            Assert::AreEqual(DWORD(0),OwnObserverChild());
        }
        TEST_METHOD(DisabledObserverIsInertAndDoesNotPublishCollectorState)
        {
            ScopedObserverEnvironmentOverride disabled;
            Assert::IsTrue(disabled.Ready());

            RendererPassiveDisplayStats snapshot;
            {
                PassiveDisplayObserver observer;
                observer.Start();
                observer.Start(); // Repeated disabled starts stay inert.

                PassiveDisplayStatistics::OutputIntent intent{};
                observer.Submit(intent);
                observer.Reset(1, 1);
                snapshot = observer.Snapshot(GetTickCount64());
            }

            Assert::IsFalse(snapshot.supported);
            Assert::IsFalse(snapshot.running);
            Assert::IsFalse(snapshot.fresh);
            Assert::AreEqual(static_cast<uint32_t>(0), snapshot.errorCode);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.matchedOutcomes);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.appliedResetSerial);
        }
    };
}
