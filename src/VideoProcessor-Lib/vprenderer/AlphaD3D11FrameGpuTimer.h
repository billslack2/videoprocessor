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
#include <cstdint>
#include <cmath>

// Asynchronous D3D11 GPU timing for tightly bounded stages of one frame.
//
// Every slot owns exactly one timestamp-disjoint interval and bounded timestamp
// pairs for its GPU-command stages. Their durations are summed, so CPU gaps
// between stages are excluded. Results are polled at least two subsequently
// issued renders later with D3D11_ASYNC_GETDATA_DONOTFLUSH. Nothing here
// flushes, waits, retries in the same frame or measures Present/VSync, DWM
// composition, or scanout.
// If the bounded ring fills, the new render is left untimed rather than reusing
// an unresolved query.
class AlphaD3D11FrameGpuTimer
{
public:
	static constexpr size_t CAPACITY = 16;
	static constexpr size_t MAXIMUM_STAGES = 6;
	static constexpr uint64_t MINIMUM_RESOLVE_LAG_FRAMES = 2;
	static constexpr double MAXIMUM_VALID_RENDER_MS = 1000.0;

	enum class Stage
	{
		SourceUpload,
		OverlayUpload,
		CoreRender,
	};

	struct Sample
	{
		uint64_t generation = 0;
		uint64_t sourceSequence = 0;
		uint64_t submissionSerial = 0;
		uint64_t lagFrames = 0;
		double milliseconds = 0.0;
		double sourceUploadMilliseconds = 0.0;
		double overlayUploadMilliseconds = 0.0;
		double coreRenderMilliseconds = 0.0;
		size_t stages = 0;
	};

	struct Diagnostics
	{
		bool supported = false;
		uint64_t issued = 0;
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
			for (size_t stage = 0; stage < MAXIMUM_STAGES; ++stage)
			{
				if (FAILED(device->CreateQuery(&timestamp, &slot.start[stage])) ||
					FAILED(device->CreateQuery(&timestamp, &slot.end[stage])))
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
		m_stageActive = false;
		m_stageCollectionFailed = false;
		m_outcomePending = false;
		m_read = 0;
		m_write = 0;
		m_count = 0;
		m_pending.store(0, std::memory_order_relaxed);
		for (Slot& slot : m_slots)
			slot = {};
	}

	bool BeginRender(ID3D11DeviceContext* context, uint64_t generation,
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

		++m_renderOrdinal;
		Slot& slot = m_slots[m_write];
		slot.pending = false;
		slot.outcomeKnown = false;
		slot.accepted = false;
		slot.generation = generation;
		slot.sourceSequence = sourceSequence;
		slot.submissionSerial = 0;
		slot.ordinal = m_renderOrdinal;
		slot.stageCount = 0;
		slot.stageCollectionValid = true;
		context->Begin(slot.disjoint);
		m_active = true;
		m_stageCollectionFailed = false;
		m_activeIndex = m_write;
		m_issued.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	bool BeginStage(ID3D11DeviceContext* context, Stage stage)
	{
		if (!m_active || !context)
			return false;
		Slot& slot = m_slots[m_activeIndex];
		if (m_stageActive || slot.stageCount >= MAXIMUM_STAGES)
		{
			m_stageCollectionFailed = true;
			return false;
		}
		slot.stageKinds[slot.stageCount] = stage;
		context->End(slot.start[slot.stageCount]);
		m_stageActive = true;
		return true;
	}

	void EndStage(ID3D11DeviceContext* context)
	{
		if (!m_active || !context || !m_stageActive)
			return;
		Slot& slot = m_slots[m_activeIndex];
		context->End(slot.end[slot.stageCount]);
		++slot.stageCount;
		m_stageActive = false;
	}

	// Close the frame-owned disjoint interval after its final bounded stage. The
	// outcome is associated only after submission is attempted.
	void EndRender(ID3D11DeviceContext* context)
	{
		if (!m_active || !context)
			return;
		Slot& slot = m_slots[m_activeIndex];
		EndStage(context);
		context->End(slot.disjoint);
		slot.stageCollectionValid =
			!m_stageCollectionFailed && slot.stageCount > 0;
		slot.pending = true;
		m_outcomeIndex = m_activeIndex;
		m_outcomePending = true;
		m_write = (m_write + 1) % CAPACITY;
		++m_count;
		m_pending.store(m_count, std::memory_order_relaxed);
		m_active = false;
	}

	// Only a successfully rendered and submitted source frame may become a
	// published sample. The unique submission serial distinguishes repeats.
	void SetFrameOutcome(bool accepted, uint64_t submissionSerial = 0)
	{
		if (!m_outcomePending)
			return;
		Slot& slot = m_slots[m_outcomeIndex];
		slot.accepted = accepted && slot.stageCollectionValid;
		slot.submissionSerial = slot.accepted ? submissionSerial : 0;
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
			m_renderOrdinal < slot.ordinal + MINIMUM_RESOLVE_LAG_FRAMES)
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
		if (!slot.accepted)
			return DiscardOldest(m_rejectedFrames);
		if (timing.Disjoint || timing.Frequency == 0)
			return DiscardOldest(m_disjoint);

		double milliseconds = 0.0;
		double sourceUploadMilliseconds = 0.0;
		double overlayUploadMilliseconds = 0.0;
		double coreRenderMilliseconds = 0.0;
		for (size_t stage = 0; stage < slot.stageCount; ++stage)
		{
			UINT64 start = 0;
			UINT64 end = 0;
			result = context->GetData(slot.start[stage], &start,
				sizeof(start), flags);
			if (result == S_FALSE)
			{
				m_notReady.fetch_add(1, std::memory_order_relaxed);
				return ResolveResult::NotReady;
			}
			if (result != S_OK)
				return DiscardOldest(m_queryFailures);
			result = context->GetData(slot.end[stage], &end,
				sizeof(end), flags);
			if (result == S_FALSE)
			{
				m_notReady.fetch_add(1, std::memory_order_relaxed);
				return ResolveResult::NotReady;
			}
			if (result != S_OK || end <= start)
				return DiscardOldest(m_queryFailures);
			const double stageMilliseconds =
				static_cast<double>(end - start) * 1000.0 /
				static_cast<double>(timing.Frequency);
			milliseconds += stageMilliseconds;
			switch (slot.stageKinds[stage])
			{
			case Stage::SourceUpload:
				sourceUploadMilliseconds += stageMilliseconds;
				break;
			case Stage::OverlayUpload:
				overlayUploadMilliseconds += stageMilliseconds;
				break;
			case Stage::CoreRender:
				coreRenderMilliseconds += stageMilliseconds;
				break;
			}
		}
		if (!std::isfinite(milliseconds) || milliseconds <= 0.0 ||
			milliseconds > MAXIMUM_VALID_RENDER_MS)
		{
			return DiscardOldest(m_queryFailures);
		}

		sample.generation = slot.generation;
		sample.sourceSequence = slot.sourceSequence;
		sample.submissionSerial = slot.submissionSerial;
		sample.lagFrames = m_renderOrdinal - slot.ordinal;
		sample.milliseconds = milliseconds;
		sample.sourceUploadMilliseconds = sourceUploadMilliseconds;
		sample.overlayUploadMilliseconds = overlayUploadMilliseconds;
		sample.coreRenderMilliseconds = coreRenderMilliseconds;
		sample.stages = slot.stageCount;
		m_resolved.fetch_add(1, std::memory_order_relaxed);
		PopOldest();
		return ResolveResult::Resolved;
	}

	Diagnostics Snapshot() const
	{
		Diagnostics result;
		result.supported = m_supported.load(std::memory_order_acquire);
		result.issued = m_issued.load(std::memory_order_relaxed);
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
		std::array<CComPtr<ID3D11Query>, MAXIMUM_STAGES> start;
		std::array<CComPtr<ID3D11Query>, MAXIMUM_STAGES> end;
		std::array<Stage, MAXIMUM_STAGES> stageKinds{};
		size_t stageCount = 0;
		uint64_t generation = 0;
		uint64_t sourceSequence = 0;
		uint64_t submissionSerial = 0;
		uint64_t ordinal = 0;
		bool pending = false;
		bool outcomeKnown = false;
		bool accepted = false;
		bool stageCollectionValid = false;
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
	uint64_t m_renderOrdinal = 0;
	bool m_active = false;
	bool m_stageActive = false;
	bool m_stageCollectionFailed = false;
	bool m_outcomePending = false;

	std::atomic_bool m_supported{ false };
	std::atomic<uint64_t> m_issued{ 0 };
	std::atomic<uint64_t> m_resolved{ 0 };
	std::atomic<uint64_t> m_notReady{ 0 };
	std::atomic<uint64_t> m_disjoint{ 0 };
	std::atomic<uint64_t> m_queryFailures{ 0 };
	std::atomic<uint64_t> m_rejectedFrames{ 0 };
	std::atomic<uint64_t> m_ringOverruns{ 0 };
	std::atomic<uint64_t> m_pending{ 0 };
};
