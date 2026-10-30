/*
 * Copyright(C) 2026 Andrew Nevard
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

// Host and configuration boundary for source plugins (plugins/sources/*.dll),
// shaped like MagewellBackend. Without a plugin every function finds nothing
// and VP behaves exactly as before.
#include <ACaptureDeviceDiscoverer.h>
#include <string>
#include <vector>

namespace SourcePluginBackend
{
	// Discoverer for the dialog. Start() loads every valid plugin once and
	// reports it as a capture device; Stop() reports each one lost.
	CComPtr<ACaptureDeviceDiscoverer> CreateDiscoverer(ICaptureDeviceDiscovererCallback& callback);

	// True if the device is a source plugin's input (by type, not by name).
	bool IsSourcePluginDevice(ACaptureDevice* device);

	// For the configuration editor: plugin names and their inputs. Reads each
	// plugin's function table only; never creates an instance.
	void AppendDeviceNames(std::vector<std::wstring>& names);
	bool IsDeviceName(const std::wstring& deviceName);
	std::vector<std::wstring> ConnectionNames(const std::wstring& deviceName);
}
