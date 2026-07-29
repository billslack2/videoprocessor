#pragma once

#include <Windows.h>
#include <objbase.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace DirectShowExecutorDetail
{
	template<typename Result>
	struct FutureWaiter
	{
		static Result Get(std::future<Result>& result)
		{
			return result.get();
		}
	};

	template<>
	struct FutureWaiter<void>
	{
		static void Get(std::future<void>& result)
		{
			result.get();
		}
	};
}


// Owns the apartment in which a DirectShow graph is created, controlled, and
// released. Commands are serialized and the thread pumps window messages for
// renderer filters which create apartment-owned helper windows.
class DirectShowGraphExecutor
{
public:
	using CoalescingKey = std::uintptr_t;

	DirectShowGraphExecutor()
	{
		m_workEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		if (!m_workEvent)
			throw std::runtime_error(
				"Failed to create DirectShow graph executor event");

		std::promise<HRESULT> ready;
		std::future<HRESULT> readyResult = ready.get_future();
		m_thread = std::thread([this, ready = std::move(ready)]() mutable
			{
				const HRESULT initializeResult =
					CoInitializeEx(nullptr, COINIT_MULTITHREADED);
				m_ownerThreadId.store(
					GetCurrentThreadId(), std::memory_order_release);

				// Ensure this thread owns a message queue before graph filters
				// have an opportunity to create or post to helper windows.
				MSG message;
				PeekMessage(&message, nullptr, 0, 0, PM_NOREMOVE);
				ready.set_value(initializeResult);
				if (FAILED(initializeResult))
					return;

				for (;;)
				{
					const DWORD waitResult = MsgWaitForMultipleObjectsEx(
						1, &m_workEvent, INFINITE, QS_ALLINPUT,
						MWMO_INPUTAVAILABLE);
					if (waitResult == WAIT_OBJECT_0)
					{
						for (;;)
						{
							Command command;
							{
								std::lock_guard<std::mutex> lock(m_mutex);
								if (m_commands.empty())
								{
									ResetEvent(m_workEvent);
									break;
								}
								command = std::move(m_commands.front());
								m_commands.pop_front();
							}
							try
							{
								command.function();
							}
							catch (...)
							{
								// Async commands must never terminate the
								// permanent graph owner. Lifecycle commands
								// publish their own typed failure completion.
								OutputDebugString(
									TEXT("DirectShow graph async command failed\r\n"));
							}
						}
						std::lock_guard<std::mutex> lock(m_mutex);
						if (m_stopping && m_commands.empty())
							break;
					}
					else if (waitResult == WAIT_OBJECT_0 + 1)
					{
						while (PeekMessage(
							&message, nullptr, 0, 0, PM_REMOVE))
						{
							TranslateMessage(&message);
							DispatchMessage(&message);
						}
					}
					else
					{
						break;
					}
				}

				m_ownerThreadId.store(0, std::memory_order_release);
				CoUninitialize();
			});

		const HRESULT initializeResult = readyResult.get();
		if (FAILED(initializeResult))
		{
			m_thread.join();
			CloseHandle(m_workEvent);
			m_workEvent = nullptr;
			throw std::runtime_error(
				"Failed to initialize DirectShow graph owner apartment");
		}
	}

	~DirectShowGraphExecutor()
	{
		Shutdown();
	}

	DirectShowGraphExecutor(const DirectShowGraphExecutor&) = delete;
	DirectShowGraphExecutor& operator=(const DirectShowGraphExecutor&) = delete;

	bool IsOwnerThread() const noexcept
	{
		return m_ownerThreadId.load(std::memory_order_acquire) ==
			GetCurrentThreadId();
	}

	DWORD OwnerThreadId() const noexcept
	{
		return m_ownerThreadId.load(std::memory_order_acquire);
	}

	template<typename Function>
	auto Invoke(Function&& function) -> decltype(function())
	{
		using Result = decltype(function());
		if (IsOwnerThread())
			return function();

		auto task = std::make_shared<std::packaged_task<Result()>>(
			std::forward<Function>(function));
		std::future<Result> result = task->get_future();
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_stopping)
				throw std::runtime_error(
					"DirectShow graph executor is closed");
			m_commands.push_back({ 0, [task]()
				{
					(*task)();
				} });
			SetEvent(m_workEvent);
		}

		return DirectShowExecutorDetail::FutureWaiter<Result>::Get(result);
	}

	// Queue work without making the calling thread wait for graph-owner code.
	// This is the required entry point for UI paths which may cause a renderer
	// filter to synchronously call or send a message to its owner HWND.
	bool Post(std::function<void()> function)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_stopping)
			return false;
		m_commands.push_back({ 0, std::move(function) });
		SetEvent(m_workEvent);
		return true;
	}

	// Replace queued-but-not-started work with the same non-zero key. Active
	// work is never cancelled. This keeps resize/event storms bounded while
	// retaining FIFO ordering relative to graph reset and shutdown commands.
	bool PostCoalesced(
		CoalescingKey key,
		std::function<void()> function)
	{
		if (key == 0)
			return Post(std::move(function));

		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_stopping)
			return false;
		for (auto& command : m_commands)
		{
			if (command.coalescingKey == key)
			{
				command.function = std::move(function);
				return true;
			}
		}
		m_commands.push_back({ key, std::move(function) });
		SetEvent(m_workEvent);
		return true;
	}

	void Shutdown() noexcept
	{
		if (!m_thread.joinable())
			return;

		if (IsOwnerThread())
			std::terminate();

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stopping = true;
			SetEvent(m_workEvent);
		}
		m_thread.join();
		CloseHandle(m_workEvent);
		m_workEvent = nullptr;
	}

	// Forced lifetime boundary: discard work which has not started, execute one
	// final owner-apartment cleanup, and keep the caller's window apartment
	// dispatching synchronous renderer messages until cleanup is terminal.
	void CancelPendingAndShutdown(
		std::function<void()> finalCommand) noexcept
	{
		if (!m_thread.joinable())
			return;
		if (IsOwnerThread())
			std::terminate();

		HANDLE threadHandle = m_thread.native_handle();
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_commands.clear();
			m_commands.push_back({ 0, std::move(finalCommand) });
			m_stopping = true;
			SetEvent(m_workEvent);
		}

		for (;;)
		{
			const DWORD waitResult = MsgWaitForMultipleObjectsEx(
				1, &threadHandle, INFINITE, QS_SENDMESSAGE,
				MWMO_INPUTAVAILABLE);
			if (waitResult == WAIT_OBJECT_0)
				break;
			if (waitResult != WAIT_OBJECT_0 + 1)
				break;

			MSG message;
			PeekMessage(&message, nullptr, WM_NULL, WM_NULL, PM_NOREMOVE);
		}

		m_thread.join();
		CloseHandle(m_workEvent);
		m_workEvent = nullptr;
	}

private:
	struct Command
	{
		CoalescingKey coalescingKey;
		std::function<void()> function;
	};

	HANDLE m_workEvent = nullptr;
	std::thread m_thread;
	std::atomic<DWORD> m_ownerThreadId{0};
	std::mutex m_mutex;
	std::deque<Command> m_commands;
	bool m_stopping = false;
};
