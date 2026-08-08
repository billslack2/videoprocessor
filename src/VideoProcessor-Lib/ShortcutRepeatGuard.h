/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.
 */

#pragma once

#include <cstdint>


class ShortcutDebounceState
{
public:
	bool ProcessPhysicalKey(uint16_t key, bool keyDown, bool keyUp, bool repeat,
		bool guardedShortcut)
	{
		if (keyUp && key == m_keyDown)
			m_keyDown = 0;
		if (!keyDown || !guardedShortcut)
			return false;
		if (repeat && key == m_keyDown)
			return true;
		m_keyDown = key;
		return false;
	}

	void Queue(uint32_t command, uint64_t now)
	{
		m_pendingCommand = command;
		m_lastInputTick = now;
	}

	void Touch(uint64_t now)
	{
		if (m_pendingCommand != 0)
			m_lastInputTick = now;
	}

	bool HasPending() const { return m_pendingCommand != 0; }
	uint32_t PendingCommand() const { return m_pendingCommand; }

	uint32_t DelayRemaining(uint64_t now, uint32_t delayMs) const
	{
		if (m_pendingCommand == 0)
			return 0;
		if (m_keyDown != 0)
			return delayMs;
		const uint64_t elapsed = now >= m_lastInputTick ?
			now - m_lastInputTick : 0;
		return elapsed >= delayMs ? 0 :
			static_cast<uint32_t>(delayMs - elapsed);
	}

	bool TryTake(uint64_t now, uint32_t delayMs, uint32_t& command)
	{
		if (DelayRemaining(now, delayMs) != 0 || m_pendingCommand == 0)
			return false;
		command = m_pendingCommand;
		m_pendingCommand = 0;
		return true;
	}

private:
	uint16_t m_keyDown = 0;
	uint32_t m_pendingCommand = 0;
	uint64_t m_lastInputTick = 0;
};
