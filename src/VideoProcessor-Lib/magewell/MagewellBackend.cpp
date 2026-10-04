#include "pch.h"
#include "MagewellBackend.h"
#include "MagewellCaptureDeviceDiscoverer.h"
#include "MagewellSdkInstance.h"

namespace
{
    // Reuse the capture discoverer for the editor, so names, usable-card filters,
    // connector IDs and SDK lifetime cannot diverge between the two front ends.
    class DiscoveryQuery final : public ICaptureDeviceDiscovererCallback
    {
    public:
        std::vector<ACaptureDeviceComPtr> devices;
        void OnCaptureDeviceFound(ACaptureDeviceComPtr& device) override
        {
            devices.push_back(device);
        }
        void OnCaptureDeviceLost(ACaptureDeviceComPtr&) override {}
    };

    std::vector<ACaptureDeviceComPtr> QueryDevices()
    {
        DiscoveryQuery query;
        CComPtr<ACaptureDeviceDiscoverer> discovery = MagewellBackend::CreateDiscoverer(query);
        try
        {
            discovery->Start();
            discovery->Stop();
        }
        catch (const std::exception& error)
        {
            DebugLog::Log("Magewell configuration discovery unavailable: %s", error.what());
            return {};
        }
        return std::move(query.devices);
    }
}

namespace MagewellBackend
{
    CComPtr<ACaptureDeviceDiscoverer> CreateDiscoverer(ICaptureDeviceDiscovererCallback& callback)
    {
        return CComPtr<ACaptureDeviceDiscoverer>(new MagewellCaptureDeviceDiscoverer(callback));
    }

    CString UnavailableMessage(const CString& configuredDeviceName)
    {
        CString lower(configuredDeviceName);
        lower.MakeLower();
        if (lower.Find(TEXT("magewell")) < 0 && lower.Find(TEXT("pro capture")) < 0)
            return CString();
        const auto reason = MagewellSdkInstance::UnavailableReason();
        if (reason.empty())
            return CString();
        CString message;
        message.Format(TEXT("Magewell unavailable. Install or repair the official Magewell runtime and device driver. %s"), reason.c_str());
        return message;
    }
    void AppendDeviceNames(std::vector<std::wstring>& names)
    {
        for (const auto& device : QueryDevices())
            names.emplace_back(device->GetName().GetString());
    }

    std::vector<std::wstring> ConnectionNames(const std::wstring& deviceName)
    {
        std::vector<std::wstring> names;
        for (const auto& device : QueryDevices())
        {
            if (_wcsicmp(device->GetName().GetString(), deviceName.c_str()) != 0)
                continue;
            for (const auto& input : device->SupportedCaptureInputs())
                names.emplace_back(input.name.GetString());
            break;
        }
        return names;
    }
}