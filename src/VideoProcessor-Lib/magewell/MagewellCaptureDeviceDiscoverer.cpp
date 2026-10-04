/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include "MagewellCaptureDeviceDiscoverer.h"
#include "MagewellCaptureDevice.h"

#include "MagewellSdkApi.h"

#include <DebugLog.h>

#include <stdexcept>
#include <string>


namespace
{
	// The Magewell SDK reports names as ANSI. Product names are plain ASCII,
	// so a direct widening avoids any dependency on the active code page.
	CString WidenProductName(const CHAR* value, size_t capacity)
	{
		CString widened;
		for (size_t index = 0; index < capacity && value[index] != '\0'; ++index)
			widened.AppendChar(static_cast<TCHAR>(
				static_cast<unsigned char>(value[index])));
		return widened;
	}

	// Mirrors the DeckLink convention: a single device is named plainly and
	// multiple devices carry a one-based suffix so each entry stays unique.
	// This must produce exactly the same string as the configuration editor's
	// discovery helper, because the configured capture_device name is matched
	// against it.
	CString MagewellDisplayName(
		const MWCAP_CHANNEL_INFO& info, int index, int channelCount)
	{
		CString name = WidenProductName(
			info.szProductName, sizeof(info.szProductName));
		if (name.IsEmpty())
			name = TEXT("Magewell Capture");
		if (channelCount > 1)
		{
			CString suffix;
			suffix.Format(TEXT(" (%d)"), index + 1);
			name += suffix;
		}
		return name;
	}
}


MagewellCaptureDeviceDiscoverer::MagewellCaptureDeviceDiscoverer(
	ICaptureDeviceDiscovererCallback& callback):
	ACaptureDeviceDiscoverer(callback),
	m_refCount(0)
{
}


MagewellCaptureDeviceDiscoverer::~MagewellCaptureDeviceDiscoverer()
{
	m_captureDevices.clear();
}


void MagewellCaptureDeviceDiscoverer::Start()
{
	if (m_started)
		throw std::runtime_error("Magewell discoverer already started");

	m_started = true;

	// A machine with no Magewell runtime is a normal condition, not an error.
	// The application must keep running and use whichever other capture
	// backends are present.
	Discover(MagewellSdkInstance::Acquire());
}

void MagewellCaptureDeviceDiscoverer::Discover(const MagewellSdkInstancePtr& sdk)
{
	m_sdkInstance = sdk;
	if (!m_sdkInstance)
	{
		DebugLog::Log(
			"Magewell discovery unavailable: %S", MagewellSdkInstance::UnavailableReason().c_str());
		return;
	}

	if (m_sdkInstance->Api().MWRefreshDevice() != MW_SUCCEEDED)
	{
		DebugLog::Log("Magewell discovery found no devices: refresh failed");
		return;
	}

	const int channelCount = m_sdkInstance->Api().MWGetChannelCount();
	DebugLog::Log("Magewell discovery found %d channel(s)", channelCount);

	for (int index = 0; index < channelCount; ++index)
	{
		MWCAP_CHANNEL_INFO info = {};
		if (m_sdkInstance->Api().MWGetChannelInfoByIndex(index, &info) != MW_SUCCEEDED)
		{
			DebugLog::Log("Magewell channel %d: info unavailable, skipped", index);
			continue;
		}

		// This backend uses Pro Capture APIs; other families have different capture contracts.
		if (info.wFamilyID != MW_FAMILY_ID_PRO_CAPTURE)
		{
			DebugLog::Log("Magewell channel %d: family %u not supported by Pro Capture backend", index, info.wFamilyID);
			continue;
		}

		WCHAR path[128] = {};
		if (m_sdkInstance->Api().MWGetDevicePath(index, path) != MW_SUCCEEDED)
		{
			DebugLog::Log("Magewell channel %d: device path unavailable, skipped", index);
			continue;
		}

		const CString displayName =
			MagewellDisplayName(info, index, channelCount);

		try
		{
			ACaptureDeviceComPtr captureDevice;
			captureDevice = new MagewellCaptureDevice(
				m_sdkInstance, path, info, displayName);

			if (!captureDevice->CanCapture())
			{
				DebugLog::Log("Magewell channel %d is unsupported or unavailable, skipped", index);
				continue;
			}

			m_captureDevices.push_back(captureDevice);
			m_callback.OnCaptureDeviceFound(captureDevice);

			DebugLog::Log("Magewell channel %d reported as '%S'",
				index, displayName.GetString());
		}
		catch (const std::exception& exception)
		{
			// One unusable channel must not prevent the rest being reported.
			DebugLog::Log("Magewell channel %d could not be opened: %s",
				index, exception.what());
		}
	}
}


void MagewellCaptureDeviceDiscoverer::Stop()
{
	if (!m_started)
		throw std::runtime_error("Magewell discoverer not running");

	m_started = false;

	for (auto& captureDevice : m_captureDevices)
	{
		ACaptureDeviceComPtr notification = captureDevice;
		m_callback.OnCaptureDeviceLost(notification);
	}

	m_captureDevices.clear();
	m_sdkInstance.reset();
}


HRESULT MagewellCaptureDeviceDiscoverer::QueryInterface(REFIID iid, LPVOID* ppv)
{
	if (!ppv)
		return E_INVALIDARG;

	*ppv = nullptr;

	if (iid == IID_IUnknown)
	{
		*ppv = this;
		AddRef();
		return S_OK;
	}

	return E_NOINTERFACE;
}


ULONG MagewellCaptureDeviceDiscoverer::AddRef(void)
{
	return ++m_refCount;
}


ULONG MagewellCaptureDeviceDiscoverer::Release(void)
{
	const ULONG newRefValue = --m_refCount;
	if (newRefValue == 0)
		delete this;

	return newRefValue;
}
