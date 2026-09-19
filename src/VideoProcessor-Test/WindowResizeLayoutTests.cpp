#include "pch.h"
#include "CppUnitTest.h"
#include <WindowResizeLayout.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	namespace
	{
		// All test windows are children of a never-shown container. Real USER32
		// WM_SIZE/WM_MOVE/maximize/restore messages run the production adapter
		// without displaying windows or changing the user's desktop placement.
		// These lifecycle invariants also apply during external actions, which
		// must not acquire ownership of the application window's placement.
		class ResizeHost
		{
		public:
			HWND container = nullptr, window = nullptr, child = nullptr;
			unsigned notifications = 0;
			bool notificationSawCompletedLayout = true;
			ResizeHost()
			{
				WNDCLASSW wc{};
				wc.hInstance = GetModuleHandleW(nullptr);
				wc.lpszClassName = L"VPWindowResizeLayoutTest";
				wc.lpfnWndProc = Procedure;
				RegisterClassW(&wc);
				container = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW,
					0, 0, 2000, 1400, nullptr, nullptr, wc.hInstance, nullptr);
				window = CreateWindowExW(0, wc.lpszClassName, L"",
					WS_CHILD | WS_OVERLAPPEDWINDOW, 100, 100, 1200, 800,
					container, nullptr, wc.hInstance, this);
				Assert::IsTrue(container != nullptr && window != nullptr);
				child = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 1, 1,
					window, nullptr, wc.hInstance, nullptr);
				Assert::IsTrue(child != nullptr);
			}
			~ResizeHost() { if (container) DestroyWindow(container); }
			static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
			{
				auto* self = reinterpret_cast<ResizeHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
				if (message == WM_NCCREATE)
				{
					self = static_cast<ResizeHost*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
					SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
				}
				if (self && message == WM_SIZE)
				{
					WindowResizeLayout::HandleSize(LOWORD(lp), HIWORD(lp), [self](int width, int height)
						{
							if (self->child) SetWindowPos(self->child, nullptr, 0, 0, width, height,
								SWP_NOACTIVATE | SWP_NOZORDER);
						}, [self, hwnd]()
						{
							++self->notifications;
							if (!self->child) return;
							RECT outer{}, inner{};
							GetClientRect(hwnd, &outer);
							GetWindowRect(self->child, &inner);
							self->notificationSawCompletedLayout = self->notificationSawCompletedLayout &&
								outer.right == inner.right - inner.left && outer.bottom == inner.bottom - inner.top;
						});
				}
				return DefWindowProcW(hwnd, message, wp, lp);
			}
			RECT Bounds() const { RECT value{}; GetWindowRect(window, &value); return value; }
			void Place(int x, int y, int width, int height, UINT flags = 0)
			{
				Assert::IsTrue(SetWindowPos(window, nullptr, x, y, width, height,
					flags | SWP_NOACTIVATE | SWP_NOZORDER) != FALSE);
			}
		};
	}
	TEST_CLASS(WindowResizeLayoutTests)
	{
	public:
		TEST_METHOD(ResizePreservesRequestedWindowBounds)
		{
			ResizeHost host;
			host.Place(100, 100, 1400, 900);
			const auto bounds = host.Bounds();
			Assert::AreEqual(LONG{1400}, bounds.right - bounds.left);
			Assert::AreEqual(LONG{900}, bounds.bottom - bounds.top);
			Assert::IsTrue(host.notificationSawCompletedLayout);
		}
		TEST_METHOD(LayoutDoesNotUndoMaximize)
		{
			ResizeHost host;
			ShowWindow(host.window, SW_SHOWMAXIMIZED);
			Assert::IsTrue(IsZoomed(host.window) != FALSE);
		}
		TEST_METHOD(LayoutDoesNotUndoRestore)
		{
			ResizeHost host;
			ShowWindow(host.window, SW_SHOWMAXIMIZED);
			ShowWindow(host.window, SW_SHOWNORMAL);
			Assert::IsFalse(IsZoomed(host.window) != FALSE);
		}
		TEST_METHOD(ShellStylePlacementWithoutSizingHintsIsNotReverted)
		{
			ResizeHost host;
			// No SC_SIZE or WM_ENTERSIZEMOVE: the shell may position directly.
			host.Place(0, 0, 1000, 1350);
			const auto bounds = host.Bounds();
			Assert::AreEqual(LONG{1000}, bounds.right - bounds.left);
			Assert::AreEqual(LONG{1350}, bounds.bottom - bounds.top);
		}
		TEST_METHOD(MoveOnlyThenResizeKeepsCurrentPosition)
		{
			ResizeHost host;
			host.Place(500, 300, 0, 0, SWP_NOSIZE);
			const auto moved = host.Bounds();
			host.Place(0, 0, 1400, 900, SWP_NOMOVE);
			const auto resized = host.Bounds();
			Assert::AreEqual(moved.left, resized.left);
			Assert::AreEqual(moved.top, resized.top);
		}
		TEST_METHOD(LayoutCompletesBeforeRendererNotification)
		{
			ResizeHost host;
			const auto before = host.notifications;
			host.Place(200, 200, 1400, 900);
			Assert::IsTrue(host.notifications > before);
			Assert::IsTrue(host.notificationSawCompletedLayout);
		}
		TEST_METHOD(LayoutDoesNotUndoMinimize)
		{
			ResizeHost host;
			ShowWindow(host.window, SW_MINIMIZE);
			Assert::IsTrue(IsIconic(host.window) != FALSE);
		}
	};
}
