/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.
 */

#pragma once

#include <cstdint>


class ShortcutRepeatGuard
{
public:
	bool Process(uint16_t key, bool keyDown, bool keyUp, bool repeat,
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

private:
	uint16_t m_keyDown = 0;
};
