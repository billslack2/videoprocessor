#pragma once

#include <cstdint>

// Owns the CPU-side scheduling boundary between UI/presentation intent and a
// renderer-owned shader compiler. It deliberately has no graphics objects:
// callers can enqueue while a backend is compiling without acquiring the
// renderer or GPU lock. The compiler owns at most one active request and the
// newest pending replacement; an older completion cannot become active after
// newer intent arrives.
namespace ShaderPreparationCoordinator
{
	struct Generation
	{
		uint64_t renderer = 0;
		uint64_t configuration = 0;
		uint64_t shader = 0;
		uint64_t sourceFormat = 0;
		uint64_t outputContract = 0;
		uint64_t targetGeometry = 0;

		bool operator==(const Generation& other) const
		{
			return renderer == other.renderer &&
				configuration == other.configuration && shader == other.shader &&
				sourceFormat == other.sourceFormat &&
				outputContract == other.outputContract &&
				targetGeometry == other.targetGeometry;
		}
	};

	struct Request
	{
		Generation generation;
		uint64_t serial = 0;
	};

	enum class EnqueueResult
	{
		Start,
		Coalesced,
		ReplacedPending,
		RejectedRetired
	};

	enum class CompletionResult
	{
		Activate,
		DiscardSuperseded,
		DiscardRetired,
		DiscardUnknown
	};

	class Queue
	{
	public:
		EnqueueResult Enqueue(const Generation& generation, Request& start)
		{
			start = {};
			if (m_retired)
				return EnqueueResult::RejectedRetired;

			const Request request{ generation, m_nextSerial++ };
			if (!m_hasActive)
			{
				m_active = request;
				m_hasActive = true;
				start = request;
				return EnqueueResult::Start;
			}
			if (m_active.generation == generation)
			{
				start = m_active;
				return EnqueueResult::Coalesced;
			}
			if (m_hasPending && m_pending.generation == generation)
			{
				start = m_pending;
				return EnqueueResult::Coalesced;
			}

			m_pending = request;
			m_hasPending = true;
			start = request;
			return EnqueueResult::ReplacedPending;
		}

		CompletionResult Complete(const Request& completed, Request& next)
		{
			next = {};
			if (m_retired)
				return CompletionResult::DiscardRetired;
			if (!m_hasActive || completed.serial != m_active.serial ||
				!(completed.generation == m_active.generation))
				return CompletionResult::DiscardUnknown;

			m_hasActive = false;
			if (m_hasPending)
			{
				m_active = m_pending;
				m_hasActive = true;
				m_hasPending = false;
				next = m_active;
				return CompletionResult::DiscardSuperseded;
			}
			return CompletionResult::Activate;
		}

		void Retire()
		{
			m_retired = true;
			m_hasPending = false;
		}

		bool HasActive() const { return m_hasActive; }
		bool HasPending() const { return m_hasPending; }
		Request LatestRequest() const
		{
			return m_hasPending ? m_pending :
				(m_hasActive ? m_active : Request{});
		}

	private:
		uint64_t m_nextSerial = 1;
		Request m_active;
		Request m_pending;
		bool m_hasActive = false;
		bool m_hasPending = false;
		bool m_retired = false;
	};
}
