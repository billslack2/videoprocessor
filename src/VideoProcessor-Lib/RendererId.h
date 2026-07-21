/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


enum class RendererBackend
{
	DIRECTSHOW,
	LIBPLACEBO
};


/*
 * Identifies either an externally registered DirectShow renderer or an
 * in-process renderer implemented by VideoProcessor.
 */
struct RendererId
{
	CString name;
	RendererBackend backend = RendererBackend::DIRECTSHOW;
	GUID guid = GUID_NULL;

	bool operator< (const RendererId& other) const;

	static RendererId Libplacebo();
};
