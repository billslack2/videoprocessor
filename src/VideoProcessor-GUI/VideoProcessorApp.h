/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstddef>
#include <string>

#include <ActivePictureLookaheadMode.h>


class CVideoProcessorApp:
	public CWinAppEx
{
public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();
	BOOL PreTranslateMessage(MSG* message) override;
	bool RestoreDisplayTopology(const char* reason);

	// DirectShow startup priming remains automatic. The public queue controls
	// are the timestamp lead and maintained VP queue target, both expressed as
	// whole frames in the inclusive range [0, 16].
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
	size_t GetActivePictureLookaheadFrames() const
	{
		return m_activePictureLookaheadFrames;
	}
	void SetActivePictureLookaheadFrames(size_t value)
	{
		m_activePictureLookaheadFrames = value > 8 ? 8 : value;
	}
	ActivePictureLookaheadMode GetActivePictureLookaheadMode() const
	{
		return m_activePictureLookaheadMode;
	}
	void SetActivePictureLookaheadMode(ActivePictureLookaheadMode value)
	{
		m_activePictureLookaheadMode = value;
	}

private:
	size_t m_queueStartupPrerollFrames = 0;
	size_t m_queueSteadyReserveFrames = 4;
	bool m_hasQueueSteadyReserveFrames = true;
	size_t m_presentationLeadFrames = 1;
	bool m_hasPresentationLeadFrames = true;
	size_t m_activePictureLookaheadFrames = 0;
	ActivePictureLookaheadMode m_activePictureLookaheadMode =
		ActivePictureLookaheadMode::OFF;
	std::string m_displayRecoveryStatePath;
	bool m_targetOnlyDisplaySessionActive = false;
	int m_startupExitCode = 0;

	DECLARE_MESSAGE_MAP()
};

extern CVideoProcessorApp videoProcessorApp;
