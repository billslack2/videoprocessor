/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <atomic>
#include <vector>

#include <ACaptureDeviceDiscoverer.h>
#include <magewell/MagewellSdkInstance.h>


/**
 * Discovers Magewell capture devices.
 *
 * Unlike DeckLink there is no arrival or removal notification, because these
 * are PCIe cards rather than hot-pluggable devices. Enumeration therefore
 * happens once at Start() and every device found is reported immediately.
 */
class MagewellCaptureDeviceDiscoverer:
	public ACaptureDeviceDiscoverer
{
public:

	MagewellCaptureDeviceDiscoverer(ICaptureDeviceDiscovererCallback& callback);
	virtual ~MagewellCaptureDeviceDiscoverer();

	// ACaptureDeviceDiscoverer
	void Start() override;
	void Stop() override;

	// IUnknown
	HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override;
	ULONG AddRef() override;
	ULONG Release() override;

private:
	friend class MagewellDiscoveryTestAccess;
	void Discover(const MagewellSdkInstancePtr& sdk);


	MagewellSdkInstancePtr m_sdkInstance;
	std::vector<ACaptureDeviceComPtr> m_captureDevices;
	bool m_started = false;

	std::atomic<ULONG> m_refCount;
};
