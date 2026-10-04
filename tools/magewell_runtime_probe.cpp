#include <afxwin.h>
#include <magewell/MagewellSdkInstance.h>
#include <magewell/MagewellBackend.h>

#include <iostream>
#include <string>

namespace
{
	class ProbeDiscovererCallback : public ICaptureDeviceDiscovererCallback
	{
	public:
		int found = 0;
		void OnCaptureDeviceFound(ACaptureDeviceComPtr&) override { ++found; }
		void OnCaptureDeviceLost(ACaptureDeviceComPtr&) override {}
	};

	int ProbeNoDevices()
	{
		ProbeDiscovererCallback callback;
		CComPtr<ACaptureDeviceDiscoverer> discoverer =
			MagewellBackend::CreateDiscoverer(callback);
		try
		{
			discoverer->Start();
		}
		catch (const std::exception& exception)
		{
			std::cerr << "Magewell discovery failed: " << exception.what() << '\n';
			try { discoverer->Stop(); }
			catch (...) {}
			return 5;
		}

		const std::wstring reason = MagewellSdkInstance::UnavailableReason();
		if (callback.found != 0)
		{
			discoverer->Stop();
			std::cout << "MAGEWELL_AVAILABLE: found=" << callback.found << '\n';
			return 4;
		}
		if (reason.find(L"No usable Magewell Pro Capture device") == std::wstring::npos)
		{
			discoverer->Stop();
			std::wcout << L"MAGEWELL_UNAVAILABLE: " << reason << L'\n';
			return 3;
		}
		if (::GetModuleHandleW(L"LibMWCapture.dll") != nullptr)
		{
			discoverer->Stop();
			std::cerr << "Magewell runtime remained loaded after empty discovery\n";
			return 6;
		}

		discoverer->Stop();
		std::wcout << L"MAGEWELL_NO_DEVICES: " << reason
			<< L"; runtime module unloaded before Stop\n";
		return 0;
	}
}

// Small console process using the production loader. It is intentionally not
// included in the VP release package. Run --expect-unavailable on a clean VM,
// or --expect-no-devices on a machine with the runtime but no capture card.
int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		std::cerr << "usage: MagewellRuntimeProbe.exe --status|--expect-unavailable|--expect-no-devices\n";
		return 64;
	}
	const std::string mode(argv[1]);
	if (mode != "--status" && mode != "--expect-unavailable" &&
		mode != "--expect-no-devices")
	{
		std::cerr << "Unknown mode\n";
		return 64;
	}
	if (mode == "--expect-no-devices")
		return ProbeNoDevices();

	const auto sdk = MagewellSdkInstance::Acquire();
	if (!sdk)
	{
		std::wcout << L"MAGEWELL_UNAVAILABLE: "
			<< MagewellSdkInstance::UnavailableReason() << L'\n';
		return 0;
	}
	if (mode == "--expect-unavailable")
	{
		std::cout << "MAGEWELL_AVAILABLE: runtime loaded unexpectedly\n";
		return 2;
	}
	if (sdk->Api().MWRefreshDevice() != MW_SUCCEEDED)
	{
		std::cout << "MAGEWELL_REFRESH_FAILED\n";
		return 5;
	}
	const int count = sdk->Api().MWGetChannelCount();
	std::cout << "MAGEWELL_AVAILABLE: channels=" << count << '\n';
	return 0;
}
