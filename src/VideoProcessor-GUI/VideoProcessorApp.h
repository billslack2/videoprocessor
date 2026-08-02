/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstddef>


class CVideoProcessorApp:
	public CWinAppEx
{
public:
	virtual BOOL InitInstance();

	// This is intentionally configuration-only.  It must not become a public
	// command-line switch because it only has meaning for the alpha renderer.
	size_t GetAlphaQueueSizeOverride() const { return m_alphaQueueSizeOverride; }
	void SetAlphaQueueSizeOverride(size_t value) { m_alphaQueueSizeOverride = value; }

	// DirectShow's startup threshold and maintained reserve are separate
	// policies.  Zero means automatic; explicit values were schema-validated
	// as whole-frame values in the inclusive range [0, 16].
	size_t GetQueueStartupPrerollFrames() const { return m_queueStartupPrerollFrames; }
	void SetQueueStartupPrerollFrames(size_t value) { m_queueStartupPrerollFrames = value; }
	size_t GetQueueSteadyReserveFrames() const { return m_queueSteadyReserveFrames; }
	bool HasQueueSteadyReserveFrames() const { return m_hasQueueSteadyReserveFrames; }
	void SetQueueSteadyReserveFrames(size_t value)
	{
		m_queueSteadyReserveFrames = value;
		m_hasQueueSteadyReserveFrames = true;
	}
	bool HasPresentationLeadFrames() const { return m_hasPresentationLeadFrames; }
	size_t GetPresentationLeadFrames() const { return m_presentationLeadFrames; }
	void SetPresentationLeadFrames(size_t value)
	{
		m_presentationLeadFrames = value;
		m_hasPresentationLeadFrames = true;
	}

private:
	size_t m_alphaQueueSizeOverride = 0;
	size_t m_queueStartupPrerollFrames = 0;
	size_t m_queueSteadyReserveFrames = 0;
	bool m_hasQueueSteadyReserveFrames = false;
	size_t m_presentationLeadFrames = 0;
	bool m_hasPresentationLeadFrames = false;

	DECLARE_MESSAGE_MAP()
};

extern CVideoProcessorApp videoProcessorApp;
