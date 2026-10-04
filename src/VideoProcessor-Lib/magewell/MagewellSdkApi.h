#pragma once

#include <Windows.h>
#include <LibMWCapture/MWCapture.h>

#include <string>

using MagewellSdkExportResolver = FARPROC (*)(
	void* context, HMODULE module, LPCSTR exportName);

// The only Magewell entry points used by the VP backend. Keep this list in
// sync with the SDK calls under magewell/ so every required export is checked
// before discovery or capture can begin.
#define MAGEWELL_SDK_API_FUNCTIONS(X) \
	X(MWCaptureInitInstance) \
	X(MWCaptureExitInstance) \
	X(MWRefreshDevice) \
	X(MWGetChannelCount) \
	X(MWGetChannelInfoByIndex) \
	X(MWGetDevicePath) \
	X(MWOpenChannelByPath) \
	X(MWCloseChannel) \
	X(MWGetVideoInputSourceArray) \
	X(MWGetVideoInputSource) \
	X(MWSetVideoInputSource) \
	X(MWGetHDMIInfoFrameValidFlag) \
	X(MWGetHDMIInfoFramePacket) \
	X(MWGetVideoSignalStatus) \
	X(MWGetInputSpecificStatus) \
	X(MWGetFamilyInfo) \
	X(MWStartVideoCapture) \
	X(MWRegisterNotify) \
	X(MWGetVideoCaptureSupportColorFormat) \
	X(MWGetNotifyStatus) \
	X(MWStopVideoCapture) \
	X(MWGetVideoBufferInfo) \
	X(MWGetVideoFrameInfo) \
	X(MWCaptureVideoFrameToVirtualAddressEx) \
	X(MWGetVideoCaptureStatus) \
	X(MWGetDeviceTime) \
	X(MWUnregisterNotify)

struct MagewellSdkApi
{
#define MAGEWELL_DECLARE_SDK_FUNCTION(name) decltype(&::name) name = nullptr;
	MAGEWELL_SDK_API_FUNCTIONS(MAGEWELL_DECLARE_SDK_FUNCTION)
#undef MAGEWELL_DECLARE_SDK_FUNCTION

	bool IsComplete(std::wstring* missingExport = nullptr) const;

private:
	static bool BindExports(
		HMODULE module,
		MagewellSdkApi& api,
		std::wstring& missingExport,
		MagewellSdkExportResolver resolver = nullptr,
		void* resolverContext = nullptr);

	friend class MagewellSdkInstance;
	friend class MagewellSdkTestAccess;
};
