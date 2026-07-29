#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>


// Shared admission/lifetime barrier for callbacks entering a renderer. Closing
// admission prevents new leases; ResetWithIngressDrain invokes WaitForDrain
// only after the renderer has begun an operation that releases downstream
// Receive calls (BeginFlush for in-place reset, Stop for teardown).
class RendererIngressState :
	public std::enable_shared_from_this<RendererIngressState>
{
public:
	class Lease
	{
	public:
		Lease() = default;
		Lease(const Lease&) = delete;
		Lease& operator=(const Lease&) = delete;

		Lease(Lease&& other) noexcept:
			m_state(std::move(other.m_state))
		{
		}

		Lease& operator=(Lease&& other) noexcept
		{
			if (this != &other)
			{
				Release();
				m_state = std::move(other.m_state);
			}
			return *this;
		}

		~Lease()
		{
			Release();
		}

		explicit operator bool() const noexcept
		{
			return static_cast<bool>(m_state);
		}

		void Release() noexcept
		{
			const std::shared_ptr<RendererIngressState> state =
				std::move(m_state);
			if (!state)
				return;
			std::lock_guard<std::mutex> lock(state->m_mutex);
			if (--state->m_activeLeases == 0)
				state->m_drained.notify_all();
		}

	private:
		friend class RendererIngressState;
		explicit Lease(std::shared_ptr<RendererIngressState> state):
			m_state(std::move(state))
		{
		}

		std::shared_ptr<RendererIngressState> m_state;
	};

	Lease TryAcquire()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_admitting)
			return {};
		++m_activeLeases;
		return Lease(shared_from_this());
	}

	void CloseAdmission() noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_admitting = false;
	}

	void OpenAdmission() noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_admitting = true;
	}

	void WaitForDrain()
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_drained.wait(lock, [this]() { return m_activeLeases == 0; });
	}

	bool WaitForDrainFor(std::chrono::milliseconds timeout)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		return m_drained.wait_for(
			lock, timeout, [this]() { return m_activeLeases == 0; });
	}

	bool IsAdmitting() const noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_admitting;
	}

	size_t ActiveLeases() const noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_activeLeases;
	}

private:
	mutable std::mutex m_mutex;
	std::condition_variable m_drained;
	bool m_admitting = false;
	size_t m_activeLeases = 0;
};
