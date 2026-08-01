/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <algorithm>
#include <cstdlib>
#include <cstdint>


// VP-0070 needs a conservative, cue-local view of active-picture authority.
// This deliberately does not feed back into VP-0066's global active-picture
// publication. A full-raster/dark-frame observation is ambiguous for subtitle
// work: retain the last demonstrated top/bottom crop rather than letting one
// global publication reset a cue already being verified by PanelSubtitleDetector.
struct PanelSubtitleActivePictureAuthorityObservation
{
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;
	int rasterWidth = 0;
	int rasterHeight = 0;
	bool sourceStable = false;
	bool fullRasterTrusted = false;
	uint64_t pipelineGeneration = 0;
	uint64_t activePictureResetGeneration = 0;
	uint64_t modeGeneration = 0;
};


struct PanelSubtitleActivePictureAuthorityResult
{
	bool available = false;
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;
	int rasterWidth = 0;
	int rasterHeight = 0;
	uint64_t generation = 0;
	uint32_t pendingContradictions = 0;
};


class PanelSubtitleActivePictureAuthority
{
public:
	// Three consecutive, globally stable, materially different crops are needed
	// before VP-0070 changes an already-authoritative cue geometry.
	static constexpr uint32_t CONTRADICTORY_CONFIRMATIONS = 3;
	// A global full-raster conclusion may only release a subtitle crop when the
	// evidence layer explicitly classified it as trustworthy. Hold that evidence
	// longer than a crop switch to avoid treating dark fades as 16:9 changes.
	static constexpr uint32_t FULL_RASTER_CONFIRMATIONS = 12;

	PanelSubtitleActivePictureAuthorityResult Observe(
		const PanelSubtitleActivePictureAuthorityObservation& observation)
	{
		if (ContextChanged(observation))
			StartContext(observation);

		if (IsStrongTrustedFullRaster(observation))
		{
			m_hasPendingContradiction = false;
			m_pendingContradictions = 0;
			if (m_hasAuthority && ++m_pendingFullRasterObservations >=
				FULL_RASTER_CONFIRMATIONS)
			{
				m_hasAuthority = false;
				m_pendingFullRasterObservations = 0;
				AdvanceGeneration();
			}
			return CurrentResult();
		}

		if (!IsStrongTopOrBottomCrop(observation))
		{
			m_pendingContradictions = 0;
			m_hasPendingContradiction = false;
			m_pendingFullRasterObservations = 0;
			return CurrentResult();
		}
		m_pendingFullRasterObservations = 0;

		if (!m_hasAuthority)
		{
			Adopt(observation);
			return CurrentResult();
		}

		if (SameGeometry(m_authority, observation))
		{
			m_pendingContradictions = 0;
			return CurrentResult();
		}

		if (m_hasPendingContradiction &&
			SameGeometry(m_pendingContradiction, observation))
			++m_pendingContradictions;
		else
		{
			m_pendingContradiction = observation;
			m_hasPendingContradiction = true;
			m_pendingContradictions = 1;
		}

		if (m_pendingContradictions >= CONTRADICTORY_CONFIRMATIONS)
			Adopt(m_pendingContradiction);
		return CurrentResult();
	}

	void Reset()
	{
		m_hasContext = false;
		m_hasAuthority = false;
		m_hasPendingContradiction = false;
		m_pendingContradictions = 0;
		m_pendingFullRasterObservations = 0;
		AdvanceGeneration();
	}

private:
	bool ContextChanged(const PanelSubtitleActivePictureAuthorityObservation& observation) const
	{
		return !m_hasContext ||
			m_pipelineGeneration != observation.pipelineGeneration ||
			m_activePictureResetGeneration != observation.activePictureResetGeneration ||
			m_modeGeneration != observation.modeGeneration ||
			m_rasterWidth != observation.rasterWidth ||
			m_rasterHeight != observation.rasterHeight;
	}

	void StartContext(const PanelSubtitleActivePictureAuthorityObservation& observation)
	{
		m_hasContext = true;
		m_pipelineGeneration = observation.pipelineGeneration;
		m_activePictureResetGeneration = observation.activePictureResetGeneration;
		m_modeGeneration = observation.modeGeneration;
		m_rasterWidth = observation.rasterWidth;
		m_rasterHeight = observation.rasterHeight;
		m_hasAuthority = false;
		m_hasPendingContradiction = false;
		m_pendingContradictions = 0;
		m_pendingFullRasterObservations = 0;
		AdvanceGeneration();
	}

	void Adopt(const PanelSubtitleActivePictureAuthorityObservation& observation)
	{
		m_authority = observation;
		m_hasAuthority = true;
		m_hasPendingContradiction = false;
		m_pendingContradictions = 0;
		AdvanceGeneration();
	}

	void AdvanceGeneration()
	{
		++m_generation;
		if (m_generation == 0)
			++m_generation;
	}

	static bool IsStrongTopOrBottomCrop(
		const PanelSubtitleActivePictureAuthorityObservation& value)
	{
		return value.sourceStable && value.rasterWidth > 0 &&
			value.rasterHeight > 0 && value.left >= 0 && value.top >= 0 &&
			value.right > value.left && value.bottom > value.top &&
			value.right <= value.rasterWidth && value.bottom <= value.rasterHeight &&
			(value.top > 0 || value.bottom < value.rasterHeight);
	}

	static bool IsStrongTrustedFullRaster(
		const PanelSubtitleActivePictureAuthorityObservation& value)
	{
		return value.sourceStable && value.fullRasterTrusted &&
			value.rasterWidth > 0 && value.rasterHeight > 0 &&
			value.left == 0 && value.top == 0 &&
			value.right == value.rasterWidth &&
			value.bottom == value.rasterHeight;
	}

	static bool SameGeometry(const PanelSubtitleActivePictureAuthorityObservation& left,
		const PanelSubtitleActivePictureAuthorityObservation& right)
	{
		if (left.rasterWidth != right.rasterWidth ||
			left.rasterHeight != right.rasterHeight)
			return false;
		const int tolerance = std::max(2, left.rasterHeight / 270);
		return std::abs(left.left - right.left) <= tolerance &&
			std::abs(left.top - right.top) <= tolerance &&
			std::abs(left.right - right.right) <= tolerance &&
			std::abs(left.bottom - right.bottom) <= tolerance;
	}

	PanelSubtitleActivePictureAuthorityResult CurrentResult() const
	{
		PanelSubtitleActivePictureAuthorityResult result;
		result.available = m_hasAuthority;
		result.generation = m_generation;
		result.pendingContradictions = m_pendingContradictions;
		if (m_hasAuthority)
		{
			result.left = m_authority.left;
			result.top = m_authority.top;
			result.right = m_authority.right;
			result.bottom = m_authority.bottom;
			result.rasterWidth = m_authority.rasterWidth;
			result.rasterHeight = m_authority.rasterHeight;
		}
		return result;
	}

	bool m_hasContext = false;
	uint64_t m_pipelineGeneration = 0;
	uint64_t m_activePictureResetGeneration = 0;
	uint64_t m_modeGeneration = 0;
	int m_rasterWidth = 0;
	int m_rasterHeight = 0;
	bool m_hasAuthority = false;
	PanelSubtitleActivePictureAuthorityObservation m_authority;
	bool m_hasPendingContradiction = false;
	PanelSubtitleActivePictureAuthorityObservation m_pendingContradiction;
	uint32_t m_pendingContradictions = 0;
	uint32_t m_pendingFullRasterObservations = 0;
	uint64_t m_generation = 0;
};
