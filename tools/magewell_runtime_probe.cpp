#include <magewell/MagewellSdkInstance.h>

#include <iostream>
#include <string>

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

	const auto sdk = MagewellSdkInstance::Acquire();
	if (!sdk)
	{
		std::wcout << L"MAGEWELL_UNAVAILABLE: "
			<< MagewellSdkInstance::UnavailableReason() << L'\n';
		return mode == "--expect-no-devices" ? 3 : 0;
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
	if (mode == "--expect-no-devices" && count != 0)
		return 4;
	return 0;
}
