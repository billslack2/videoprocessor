#include "pch.h"
#include "CppUnitTest.h"

#include <magewell/MagewellCaptureDeviceDiscoverer.h>
#include "MagewellSdkTestAccess.h"

#include <cstring>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Tests call the same private discovery method as Start(), with a fake API
// table. No vendor DLL or capture card is needed for these ownership checks.
class MagewellDiscoveryTestAccess
{
public:
	static void Discover(MagewellCaptureDeviceDiscoverer& discoverer,
		const MagewellSdkInstancePtr& sdk)
	{
		discoverer.m_started = true;
		discoverer.Discover(sdk);
	}
};

namespace
{
	int g_openCount = 0;
	int g_closeCount = 0;
	int g_familyId = MW_FAMILY_ID_PRO_CAPTURE;
	bool g_openSucceeds = true;

	MW_RESULT FakeRefreshDevice() { return MW_SUCCEEDED; }
	int FakeGetChannelCount() { return 1; }
	MW_RESULT FakeGetChannelInfoByIndex(int index, MWCAP_CHANNEL_INFO* info)
	{
		if (index != 0 || !info) return MW_FAILED;
		*info = {};
		info->wFamilyID = static_cast<WORD>(g_familyId);
		strcpy_s(info->szProductName, "Fake Pro Capture");
		return MW_SUCCEEDED;
	}
	MW_RESULT FakeGetDevicePath(int index, WCHAR* path)
	{
		if (index != 0 || !path) return MW_FAILED;
		wcscpy_s(path, 128, L"fake-capture-channel");
		return MW_SUCCEEDED;
	}
	HCHANNEL FakeOpenChannelByPath(const WCHAR*)
	{
		++g_openCount;
		return g_openSucceeds ? reinterpret_cast<HCHANNEL>(1) : nullptr;
	}
	void FakeCloseChannel(HCHANNEL) { ++g_closeCount; }
	MW_RESULT FakeGetVideoInputSourceArray(HCHANNEL, DWORD* sources, DWORD* count)
	{
		if (!count) return MW_FAILED;
		if (sources) sources[0] = INPUT_SOURCE(MWCAP_VIDEO_INPUT_TYPE_HDMI, 0);
		*count = 1;
		return MW_SUCCEEDED;
	}
	MW_RESULT FakeGetVideoInputSource(HCHANNEL, DWORD* source)
	{
		if (!source) return MW_FAILED;
		*source = INPUT_SOURCE(MWCAP_VIDEO_INPUT_TYPE_HDMI, 0);
		return MW_SUCCEEDED;
	}

	MagewellSdkApi FakeApi()
	{
		MagewellSdkApi api;
		api.MWRefreshDevice = &FakeRefreshDevice;
		api.MWGetChannelCount = &FakeGetChannelCount;
		api.MWGetChannelInfoByIndex = &FakeGetChannelInfoByIndex;
		api.MWGetDevicePath = &FakeGetDevicePath;
		api.MWOpenChannelByPath = &FakeOpenChannelByPath;
		api.MWCloseChannel = &FakeCloseChannel;
		api.MWGetVideoInputSourceArray = &FakeGetVideoInputSourceArray;
		api.MWGetVideoInputSource = &FakeGetVideoInputSource;
		return api;
	}

	class Callback : public ICaptureDeviceDiscovererCallback
	{
	public:
		int found = 0;
		int lost = 0;
		bool lostSameDevice = false;
		ACaptureDeviceComPtr retained;

		void OnCaptureDeviceFound(ACaptureDeviceComPtr& device) override
		{
			++found;
			retained = device;
		}
		void OnCaptureDeviceLost(ACaptureDeviceComPtr& device) override
		{
			++lost;
			lostSameDevice = (retained == device);
		}
	};

	class DetachingCallback : public ICaptureDeviceDiscovererCallback
	{
	public:
		int found = 0;
		int lost = 0;
		ACaptureDeviceComPtr foundToken;
		ACaptureDeviceComPtr lostToken;

		void OnCaptureDeviceFound(ACaptureDeviceComPtr& device) override
		{
			++found;
			foundToken.Attach(device.Detach());
		}
		void OnCaptureDeviceLost(ACaptureDeviceComPtr& device) override
		{
			++lost;
			lostToken.Attach(device.Detach());
		}
	};
}

namespace Tests
{
	TEST_CLASS(MagewellDiscoveryTests)
	{
	public:
		TEST_METHOD(AbsentSdkReportsNoDevicesAndStopsCleanly)
		{
			Callback callback;
			MagewellCaptureDeviceDiscoverer discoverer(callback);
			MagewellDiscoveryTestAccess::Discover(discoverer, nullptr);
			Assert::AreEqual(0, callback.found);
			Assert::AreEqual(0, callback.lost);
			discoverer.Stop();
			Assert::AreEqual(0, callback.lost);
		}

		TEST_METHOD(FoundDeviceSurvivesDiscovererStopWhileClientRetainsIt)
		{
			g_openCount = 0;
			g_closeCount = 0;
			g_familyId = MW_FAMILY_ID_PRO_CAPTURE;
			g_openSucceeds = true;
			Callback callback;
			std::weak_ptr<MagewellSdkInstance> weakSdk;
			{
				MagewellCaptureDeviceDiscoverer discoverer(callback);
				auto sdk = MagewellSdkTestAccess::Create(FakeApi());
				weakSdk = sdk;
				MagewellDiscoveryTestAccess::Discover(discoverer, sdk);
				sdk.reset();
				Assert::AreEqual(1, callback.found);
				Assert::IsTrue(callback.retained != nullptr);
				Assert::AreEqual(1, g_openCount);
				Assert::AreEqual(1, g_closeCount);
				discoverer.Stop();
				Assert::AreEqual(1, callback.lost);
				Assert::IsTrue(callback.lostSameDevice);
				Assert::IsFalse(weakSdk.expired());
				Assert::IsTrue(callback.retained->CanCapture());
				Assert::IsFalse(callback.retained->GetName().IsEmpty());
			}
			Assert::IsFalse(weakSdk.expired());
			callback.retained.Release();
			Assert::IsTrue(weakSdk.expired());
		}

		TEST_METHOD(UnsupportedFamilyAndUnopenableCardStayHidden)
		{
			g_openCount = 0;
			g_closeCount = 0;
			g_familyId = MW_FAMILY_ID_PRO_CAPTURE + 1;
			g_openSucceeds = true;
			Callback callback;
			MagewellCaptureDeviceDiscoverer discoverer(callback);
			auto sdk = MagewellSdkTestAccess::Create(FakeApi());
			MagewellDiscoveryTestAccess::Discover(discoverer, sdk);
			Assert::AreEqual(0, callback.found);
			Assert::AreEqual(0, g_openCount);
			discoverer.Stop();

			g_familyId = MW_FAMILY_ID_PRO_CAPTURE;
			g_openSucceeds = false;
			MagewellCaptureDeviceDiscoverer second(callback);
			MagewellDiscoveryTestAccess::Discover(second, sdk);
			Assert::AreEqual(0, callback.found);
			Assert::AreEqual(1, g_openCount);
			Assert::AreEqual(0, g_closeCount);
			second.Stop();
		}

		TEST_METHOD(GuiStyleDetachedFoundAndLostTokensOwnDeviceIndependently)
		{
			g_familyId = MW_FAMILY_ID_PRO_CAPTURE;
			g_openSucceeds = true;
			DetachingCallback callback;
			std::weak_ptr<MagewellSdkInstance> weakSdk;
			{
				MagewellCaptureDeviceDiscoverer discoverer(callback);
				auto sdk = MagewellSdkTestAccess::Create(FakeApi());
				weakSdk = sdk;
				MagewellDiscoveryTestAccess::Discover(discoverer, sdk);
				sdk.reset();
				Assert::AreEqual(1, callback.found);
				Assert::IsTrue(callback.foundToken != nullptr);
				discoverer.Stop();
				Assert::AreEqual(1, callback.lost);
				Assert::IsTrue(callback.lostToken != nullptr);
				Assert::IsTrue(callback.foundToken == callback.lostToken);
				Assert::IsFalse(weakSdk.expired());
			}
			callback.foundToken.Release();
			Assert::IsFalse(weakSdk.expired());
			Assert::IsFalse(callback.lostToken->GetName().IsEmpty());
			callback.lostToken.Release();
			Assert::IsTrue(weakSdk.expired());
		}
	};
}
