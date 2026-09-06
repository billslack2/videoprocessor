/*
 * Copyright(C) 2025 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <atlbase.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cmath>

// Asynchronous, frame-scoped D3D11 GPU timing for the VP Renderer.
//
// A slot's timestamps and identity are issued together on the immediate
// context. Results are polled at least two subsequently issued frames later
// with D3D11_ASYNC_GETDATA_DONOTFLUSH. Nothing in this class flushes, waits, or
// retries a query in the same frame, so diagnostics never deliberately stall
// the present path. If the bounded ring fills before a result becomes ready,
// timing is skipped for the new frame so an unresolved query is never reused.
class AlphaD3D11FrameGpuTimer
{
public:
	static constexpr size_t CAPACITY = 16;
	static constexpr size_t MAX_SEGMENTS = 8;
	static constexpr uint64_t MINIMUM_RESOLVE_LAG_FRAMES = 2;
	static constexpr double MAXIMUM_VALID_FRAME_MS = 1000.0;

	struct Sample
	{
		uint64_t generation = 0;
		uint64_t sourceSequence = 0;
		uint64_t submissionSerial = 0;
		uint64_t lagFrames = 0;
		size_t segmentCount = 0;
		double milliseconds = 0.0;
	};

	struct Diagnostics
	{
		bool supported = false;
		uint64_t resolved = 0;
		uint64_t notReady = 0;
		uint64_t disjoint = 0;
		uint64_t queryFailures = 0;
		uint64_t rejectedFrames = 0;
		uint64_t ringOverruns = 0;
		uint64_t pending = 0;
	};

	bool Initialize(ID3D11Device* device)
	{
		ReleaseResources();
		if (!device)
			return false;

		D3D11_QUERY_DESC timestamp{};
		timestamp.Query = D3D11_QUERY_TIMESTAMP;
		D3D11_QUERY_DESC disjoint{};
		disjoint.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		for (Slot& slot : m_slots)
		{
			if (FAILED(device->CreateQuery(&disjoint, &slot.disjoint)))
			{
				ReleaseResources();
				return false;
			}
			for (size_t segment = 0; segment < MAX_SEGMENTS; ++segment)
			{
				if (FAILED(device->CreateQuery(
					&timestamp, &slot.segmentStart[segment])) ||
					FAILED(device->CreateQuery(
						&timestamp, &slot.segmentEnd[segment])))
				{
					ReleaseResources();
					return false;
				}
			}
		}
		m_supported.store(true, std::memory_order_release);
		return true;
	}

	void ReleaseResources()
	{
		m_supported.store(false, std::memory_order_release);
		m_active = false;
		m_segmentActive = false;
		m_measurementValid = false;
		m_segmentCount = 0;
		m_outcomePending = false;
		m_read = 0;
		m_write = 0;
		m_count = 0;
		m_pending.store(0, std::memory_order_relaxed);
		for (Slot& slot : m_slots)
			slot = {};
	}

	bool BeginFrame(ID3D11DeviceContext* context, uint64_t generation,
		uint64_t sourceSequence)
	{
		if (!m_supported.load(std::memory_order_acquire) || !context || m_active)
			return false;
		if (m_outcomePending)
		{
			Slot& previous = m_slots[m_outcomeIndex];
			previous.accepted = false;
			previous.outcomeKnown = true;
			m_outcomePending = false;
		}
		if (m_count == CAPACITY)
		{
			m_ringOverruns.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		++m_frameOrdinal;
		Slot& slot = m_slots[m_write];
		slot.pending = false;
		slot.outcomeKnown = false;
		slot.accepted = false;
		slot.generation = generation;
		slot.sourceSequence = sourceSequence;
		slot.submissionSerial = 0;
		slot.ordinal = m_frameOrdinal;
		slot.segmentCount = 0;
		slot.measurementValid = true;
		context->Begin(slot.disjoint);
		m_active = true;
		m_segmentActive = false;
		m_measurementValid = true;
		m_segmentCount = 0;
		m_activeIndex = m_write;
		return true;
	}

	// One frame owns one disjoint query, while each actual GPU operation gets a
	// timestamp pair. The resolved load is the envelope from the first segment
	// start through the final segment end, so overlapping copy/shader work is
	// never double-counted.
	bool BeginSegment(ID3D11DeviceContext* context)
	{
		if (!m_active || m_segmentActive || !context ||
			m_segmentCount >= MAX_SEGMENTS)
		{
			if (m_active)
				m_measurementValid = false;
			return false;
		}
		Slot& slot = m_slots[m_activeIndex];
		context->End(slot.segmentStart[m_segmentCount]);
		m_segmentActive = true;
		return true;
	}

	void EndSegment(ID3D11DeviceContext* context)
	{
		if (!m_active || !m_segmentActive || !context)
			return;
		Slot& slot = m_slots[m_activeIndex];
		context->End(slot.segmentEnd[m_segmentCount]);
		++m_segmentCount;
		m_segmentActive = false;
	}

	// End after the last measured GPU operation, before Present or a CPU wait.
	void EndFrame(ID3D11DeviceContext* context)
	{
		if (!m_active || !context)
			return;
		if (m_segmentActive)
			EndSegment(context);
		Slot& slot = m_slots[m_activeIndex];
		context->End(slot.disjoint);
		slot.segmentCount = m_segmentCount;
		slot.measurementValid = m_measurementValid && m_segmentCount > 0;
		slot.pending = true;
		m_outcomeIndex = m_activeIndex;
		m_outcomePending = true;
		m_write = (m_write + 1) % CAPACITY;
		++m_count;
		m_pending.store(m_count, std::memory_order_relaxed);
		m_active = false;
	}

	// Only a rendered and successfully submitted frame may become a sample.
	void SetFrameOutcome(bool accepted, uint64_t submissionSerial = 0)
	{
		if (!m_outcomePending)
			return;
		Slot& slot = m_slots[m_outcomeIndex];
		slot.accepted = accepted;
		slot.submissionSerial = accepted ? submissionSerial : 0;
		slot.outcomeKnown = true;
		m_outcomePending = false;
	}

	enum class ResolveResult
	{
		Empty,
		NotReady,
		Discarded,
		Resolved,
	};

	ResolveResult TryResolveOldest(ID3D11DeviceContext* context, Sample& sample)
	{
		sample = {};
		if (!m_supported.load(std::memory_order_acquire) || !context || m_count == 0)
			return ResolveResult::Empty;
		Slot& slot = m_slots[m_read];
		if (!slot.pending || !slot.outcomeKnown ||
			m_frameOrdinal < slot.ordinal + MINIMUM_RESOLVE_LAG_FRAMES)
		{
			return ResolveResult::NotReady;
		}

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing{};
		const UINT flags = D3D11_ASYNC_GETDATA_DONOTFLUSH;
		HRESULT result = context->GetData(slot.disjoint, &timing,
			sizeof(timing), flags);
		if (result == S_FALSE)
		{
			m_notReady.fetch_add(1, std::memory_order_relaxed);
			return ResolveResult::NotReady;
		}
		if (result != S_OK)
			return DiscardOldest(m_queryFailures);
		if (!slot.accepted || !slot.measurementValid ||
			slot.segmentCount == 0 || slot.segmentCount > MAX_SEGMENTS)
			return DiscardOldest(m_rejectedFrames);
		if (timing.Disjoint || timing.Frequency == 0)
			return DiscardOldest(m_disjoint);

		UINT64 firstStart = 0;
		UINT64 lastEnd = 0;
		for (size_t segment = 0; segment < slot.segmentCount; ++segment)
		{
			UINT64 start = 0;
			UINT64 end = 0;
			result = context->GetData(slot.segmentStart[segment], &start,
				sizeof(start), flags);
			if (result == S_FALSE)
			{
				m_notReady.fetch_add(1, std::memory_order_relaxed);
				return ResolveResult::NotReady;
			}
			if (result != S_OK)
				return DiscardOldest(m_queryFailures);
			result = context->GetData(slot.segmentEnd[segment], &end,
				sizeof(end), flags);
			if (result == S_FALSE)
			{
				m_notReady.fetch_add(1, std::memory_order_relaxed);
				return ResolveResult::NotReady;
			}
			if (result != S_OK || end <= start)
			{
				return DiscardOldest(m_queryFailures);
			}
			if (segment == 0)
				firstStart = start;
			lastEnd = end;
		}
		if (lastEnd <= firstStart)
			return DiscardOldest(m_queryFailures);

		// The refresh-budget metric is the end-to-end GPU timeline from the
		// first source upload through final rendering. Do not sum phase durations:
		// a driver may overlap copy and shader engines, and summing would count
		// that overlap twice.
		const double milliseconds = static_cast<double>(lastEnd - firstStart) *
			1000.0 /
			static_cast<double>(timing.Frequency);
		if (!std::isfinite(milliseconds) || milliseconds <= 0.0 ||
			milliseconds > MAXIMUM_VALID_FRAME_MS)
		{
			return DiscardOldest(m_queryFailures);
		}

		sample.generation = slot.generation;
		sample.sourceSequence = slot.sourceSequence;
		sample.submissionSerial = slot.submissionSerial;
		sample.lagFrames = m_frameOrdinal - slot.ordinal;
		sample.segmentCount = slot.segmentCount;
		sample.milliseconds = milliseconds;
		m_resolved.fetch_add(1, std::memory_order_relaxed);
		PopOldest();
		return ResolveResult::Resolved;
	}

	Diagnostics Snapshot() const
	{
		Diagnostics result;
		result.supported = m_supported.load(std::memory_order_acquire);
		result.resolved = m_resolved.load(std::memory_order_relaxed);
		result.notReady = m_notReady.load(std::memory_order_relaxed);
		result.disjoint = m_disjoint.load(std::memory_order_relaxed);
		result.queryFailures = m_queryFailures.load(std::memory_order_relaxed);
		result.rejectedFrames = m_rejectedFrames.load(std::memory_order_relaxed);
		result.ringOverruns = m_ringOverruns.load(std::memory_order_relaxed);
		result.pending = m_pending.load(std::memory_order_relaxed);
		return result;
	}

private:
	struct Slot
	{
		CComPtr<ID3D11Query> disjoint;
		std::array<CComPtr<ID3D11Query>, MAX_SEGMENTS> segmentStart{};
		std::array<CComPtr<ID3D11Query>, MAX_SEGMENTS> segmentEnd{};
		uint64_t generation = 0;
		uint64_t sourceSequence = 0;
		uint64_t submissionSerial = 0;
		uint64_t ordinal = 0;
		size_t segmentCount = 0;
		bool pending = false;
		bool outcomeKnown = false;
		bool accepted = false;
		bool measurementValid = false;
	};

	ResolveResult DiscardOldest(std::atomic<uint64_t>& counter)
	{
		counter.fetch_add(1, std::memory_order_relaxed);
		PopOldest();
		return ResolveResult::Discarded;
	}

	void PopOldest()
	{
		m_slots[m_read].pending = false;
		m_read = (m_read + 1) % CAPACITY;
		--m_count;
		m_pending.store(m_count, std::memory_order_relaxed);
	}

	std::array<Slot, CAPACITY> m_slots{};
	size_t m_read = 0;
	size_t m_write = 0;
	size_t m_count = 0;
	size_t m_activeIndex = 0;
	size_t m_outcomeIndex = 0;
	uint64_t m_frameOrdinal = 0;
	bool m_active = false;
	bool m_segmentActive = false;
	bool m_measurementValid = false;
	size_t m_segmentCount = 0;
	bool m_outcomePending = false;

	std::atomic_bool m_supported{ false };
	std::atomic<uint64_t> m_resolved{ 0 };
	std::atomic<uint64_t> m_notReady{ 0 };
	std::atomic<uint64_t> m_disjoint{ 0 };
	std::atomic<uint64_t> m_queryFailures{ 0 };
	std::atomic<uint64_t> m_rejectedFrames{ 0 };
	std::atomic<uint64_t> m_ringOverruns{ 0 };
	std::atomic<uint64_t> m_pending{ 0 };
};
