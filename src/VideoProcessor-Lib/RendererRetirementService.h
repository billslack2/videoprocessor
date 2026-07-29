#pragma once

#include <Windows.h>
#include <objbase.h>
#include <IRenderer.h>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>


// Performs the final potentially message-dependent renderer/apartment release
// away from the UI thread after graph teardown is terminal.
class RendererRetirementService
{
public:
	RendererRetirementService()
	{
		m_worker = std::thread([this]()
			{
				const HRESULT initializeResult =
					CoInitializeEx(nullptr, COINIT_MULTITHREADED);
				for (;;)
				{
					Item item;
					{
						std::unique_lock<std::mutex> lock(m_mutex);
						m_workAvailable.wait(lock, [this]()
							{
								return m_closing || !m_items.empty();
							});
						if (m_items.empty())
						{
							if (m_closing)
								break;
							continue;
						}
						item = std::move(m_items.front());
						m_items.pop_front();
						m_active = true;
					}

					bool succeeded = true;
					try
					{
						item.renderer->Retire();
					}
					catch (...)
					{
						succeeded = false;
					}
					item.renderer.reset();
					{
						std::lock_guard<std::mutex> lock(m_mutex);
						m_active = false;
					}
					PostMessage(item.completionWindow,
						item.completionMessage,
						static_cast<WPARAM>(item.token),
						succeeded ? 0 : 1);
				}
				if (SUCCEEDED(initializeResult))
					CoUninitialize();
			});
	}

	~RendererRetirementService()
	{
		RequestClose();
		Join();
	}

	RendererRetirementService(const RendererRetirementService&) = delete;
	RendererRetirementService& operator=(
		const RendererRetirementService&) = delete;

	bool Retire(std::shared_ptr<IVideoRenderer> renderer,
		uint64_t token, HWND completionWindow, UINT completionMessage)
	{
		if (!renderer)
			return false;
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_closing)
			return false;
		m_items.push_back(
			{ std::move(renderer), token, completionWindow,
				completionMessage });
		m_workAvailable.notify_one();
		return true;
	}

	void RequestClose() noexcept
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_closing = true;
		}
		m_workAvailable.notify_all();
	}

	void Join() noexcept
	{
		if (m_worker.joinable())
			m_worker.join();
	}

	HANDLE NativeThreadHandle() noexcept
	{
		return m_worker.joinable() ? m_worker.native_handle() : nullptr;
	}

	bool IsIdle() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return !m_active && m_items.empty();
	}

private:
	struct Item
	{
		std::shared_ptr<IVideoRenderer> renderer;
		uint64_t token = 0;
		HWND completionWindow = nullptr;
		UINT completionMessage = 0;
	};

	mutable std::mutex m_mutex;
	std::condition_variable m_workAvailable;
	std::deque<Item> m_items;
	std::thread m_worker;
	bool m_active = false;
	bool m_closing = false;
};
