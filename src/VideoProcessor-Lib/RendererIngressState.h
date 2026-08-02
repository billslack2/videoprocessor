#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>


// Shared admission/lifetime barrier for callbacks entering a renderer. Closing
// admission prevents new leases; ResetWithIngressDrain invokes WaitForDrain
// only after the graph has stopped and released a downstream Receive call.
class RendererIngressState :
	public std::enable_shared_from_this<RendererIngressState>
{
public:
	enum class CaptureSequencePublication
	{
		RequiresRendererAcknowledgement,
		RetainCurrentRendererState
	};

	struct CaptureSequenceSnapshot
	{
		uint64_t published = 0;
		uint64_t required = 0;
		uint64_t acknowledged = 0;
		bool admissionOpen = false;
	};

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
		if (!m_admitting ||
			(m_requiredCaptureSequence != 0 &&
				m_acknowledgedCaptureSequence !=
					m_requiredCaptureSequence))
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

	bool AcknowledgeCaptureSequence(uint64_t captureSequence) noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_acknowledgedCaptureSequence = captureSequence;
		return m_acknowledgedCaptureSequence == m_requiredCaptureSequence;
	}

	uint64_t PublishCaptureSequence(
		CaptureSequencePublication publication =
			CaptureSequencePublication::RequiresRendererAcknowledgement) noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const uint64_t sequence = ++m_publishedCaptureSequence;
		m_requiredCaptureSequence = sequence;
		// An invalid capture-state notification is advisory during the bounded
		// retain-last-valid grace period.  Publish it for ordering/staleness,
		// but atomically retain the renderer's current state and frame admission.
		if (publication == CaptureSequencePublication::RetainCurrentRendererState)
			m_acknowledgedCaptureSequence = sequence;
		return sequence;
	}

	uint64_t LatestCaptureSequence() const noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_publishedCaptureSequence;
	}

	CaptureSequenceSnapshot CaptureSequences() const noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		CaptureSequenceSnapshot snapshot;
		snapshot.published = m_publishedCaptureSequence;
		snapshot.required = m_requiredCaptureSequence;
		snapshot.acknowledged = m_acknowledgedCaptureSequence;
		snapshot.admissionOpen = m_admitting;
		return snapshot;
	}

	void WaitForDrain()
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_drained.wait(lock, [this]() { return m_activeLeases == 0; });
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
	uint64_t m_acknowledgedCaptureSequence = 0;
	uint64_t m_requiredCaptureSequence = 0;
	uint64_t m_publishedCaptureSequence = 0;
	size_t m_activeLeases = 0;
};
