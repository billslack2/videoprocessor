#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

#include <RendererResetRequest.h>


// Bridges a backend hot path to the renderer reset coordinator. A request is
// published at most once until Complete() confirms that the backend reset
// succeeded. The sink may be installed before or after the request is latched.
// Submit() is invoked while the latch is locked so Complete() cannot overtake
// publication. A sink must not synchronously re-enter this latch.
class RendererResetRequestLatch
{
public:
	void SetSink(std::shared_ptr<IRendererResetRequestSink> sink) noexcept
	{
		try
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_sink = std::move(sink);
			if (!m_sink)
				m_publishedSink = nullptr;
			if (m_pending.load(std::memory_order_acquire) &&
				m_sink &&
				m_publishedSink != m_sink.get())
			{
				m_publishedSink = m_sink.get();
				m_sink->Submit(m_request);
			}
		}
		catch (...)
		{
			return;
		}
	}

	bool Request(RendererResetRequest request) noexcept
	{
		try
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_pending.load(std::memory_order_acquire))
				return false;

			m_request = request;
			m_pending.store(true, std::memory_order_release);
			if (m_sink)
			{
				m_publishedSink = m_sink.get();
				m_sink->Submit(request);
			}
		}
		catch (...)
		{
			return false;
		}

		return true;
	}

	bool Pending() const noexcept
	{
		return m_pending.load(std::memory_order_acquire);
	}

	void Complete() noexcept
	{
		try
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_request = {};
			m_publishedSink = nullptr;
			m_pending.store(false, std::memory_order_release);
		}
		catch (...)
		{
		}
	}

private:
	mutable std::mutex m_mutex;
	std::shared_ptr<IRendererResetRequestSink> m_sink;
	RendererResetRequest m_request;
	std::atomic_bool m_pending{false};
	IRendererResetRequestSink* m_publishedSink = nullptr;
};
