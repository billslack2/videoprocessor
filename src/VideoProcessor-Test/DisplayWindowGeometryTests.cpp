#include "pch.h"
#include "CppUnitTest.h"
#include <DisplayWindowGeometry.h>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{
    TEST_CLASS(DisplayWindowGeometryTests)
    {
    public:
        TEST_METHOD(DisabledScopePreservesThreadContext)
        {
            const auto before=GetThreadDpiAwarenessContext();
            { DisplayWindowGeometry::PhysicalCoordinates scope(false);
              Assert::IsTrue(AreDpiAwarenessContextsEqual(before,GetThreadDpiAwarenessContext())!=FALSE); }
            Assert::IsTrue(AreDpiAwarenessContextsEqual(before,GetThreadDpiAwarenessContext())!=FALSE);
        }
        TEST_METHOD(NestedPhysicalScopesRestoreOriginalThreadContext)
        {
            const auto before=GetThreadDpiAwarenessContext();
            { DisplayWindowGeometry::PhysicalCoordinates outer;
              const auto physical=GetThreadDpiAwarenessContext();
              Assert::IsTrue(GetAwarenessFromDpiAwarenessContext(physical)==DPI_AWARENESS_PER_MONITOR_AWARE);
              { DisplayWindowGeometry::PhysicalCoordinates inner; }
              Assert::IsTrue(AreDpiAwarenessContextsEqual(physical,GetThreadDpiAwarenessContext())!=FALSE); }
            Assert::IsTrue(AreDpiAwarenessContextsEqual(before,GetThreadDpiAwarenessContext())!=FALSE);
        }
        TEST_METHOD(ClientRectReadsPhysicalPopupWithoutLeakingContext)
        {
            struct Window { HWND value=nullptr; ~Window() { if(value) DestroyWindow(value); } } window;
            { DisplayWindowGeometry::PhysicalCoordinates scope;
              window.value=CreateWindowExW(0,L"STATIC",L"Geometry regression",WS_POPUP,0,0,960,540,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr); }
            Assert::IsTrue(window.value!=nullptr);
            const auto before=GetThreadDpiAwarenessContext();
            RECT client{};
            Assert::IsTrue(DisplayWindowGeometry::ClientRect(window.value,&client)!=FALSE);
            Assert::AreEqual(LONG(960),client.right-client.left);
            Assert::AreEqual(LONG(540),client.bottom-client.top);
            Assert::IsTrue(AreDpiAwarenessContextsEqual(before,GetThreadDpiAwarenessContext())!=FALSE);
        }
    };
}
