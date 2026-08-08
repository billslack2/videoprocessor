#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <commctrl.h>
#include <uxtheme.h>

#include <ConfigFile.h>
#include <MainConfigSchema.h>
#include <RendererProfileConfig.h>
#include <ConfigurationDiscovery.h>

#include "Resource.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace
{
constexpr wchar_t kWindowClass[] = L"VideoProcessorConfigEditorWindow";
constexpr wchar_t kCardClass[] = L"VideoProcessorConfigCard";
constexpr wchar_t kContentClass[] = L"VideoProcessorConfigContent";
constexpr wchar_t kEditorProperty[] = L"VPConfigEditor";
constexpr wchar_t kMutexName[] = L"Local\\VideoProcessorConfigEditor-1";
constexpr UINT kTrayMessage = WM_APP + 17;
constexpr UINT_PTR kTrayIconId = 1;
constexpr UINT kTrayOpenCommand = 201;
constexpr UINT kTrayExitCommand = 202;
constexpr int kControlFirst = 100;
constexpr int kNavigationStartup = 130;
constexpr int kNavigationQueue = 131;
constexpr int kNavigationRenderer = 132;
constexpr int kNavigationViewports = 133;
constexpr int kNavigationLldv = 134;
constexpr int kNavigationShortcuts = 135;
constexpr int kViewportSelector = 140;
constexpr int kViewportAddButton = 141;
constexpr int kViewportRemoveButton = 142;
constexpr int kViewportMoveUpButton = 143;
constexpr int kViewportNameButton = 144;
constexpr int kViewportMoveDownButton = 146;
constexpr int kViewportRule = 150;
constexpr int kViewportFieldFirst = 151;
constexpr int kProfileSelector = 160;
constexpr int kProfileAddButton = 161;
constexpr int kProfileRemoveButton = 162;
constexpr int kProfileRenameButton = 163;
constexpr int kProfileMoveUpButton = 164;
constexpr int kProfileMoveDownButton = 165;
constexpr int kProfileRule = 166;
constexpr int kProfileKey = 168;
constexpr int kViewportKey = 169;
constexpr int kProfileRuleToggle = 170;
constexpr int kViewportRuleToggle = 171;
constexpr int kProfileName = 172;
constexpr int kViewportName = 173;
constexpr int kLldvFieldFirst = 220;
constexpr int kShortcutFieldFirst = 230;
constexpr int kQueueRecoveryFieldFirst = 250;
constexpr int kStartupFieldFirst = 270;
constexpr UINT_PTR kViewportDragTimer = 801;
constexpr int kSaveButton = 120;
constexpr int kReloadButton = 121;
constexpr int kValidateButton = 122;

struct ShortcutFieldDefinition
{
    const wchar_t* label;
    const char* key;
    const wchar_t* defaultValue;
};

constexpr ShortcutFieldDefinition kShortcutFields[] = {
    { L"Auto-set", "auto_set", L"Ctrl+Shift+a" },
    { L"Exit fullscreen", "fullscreen_exit", L"Esc" },
    { L"Toggle fullscreen", "fullscreen_toggle", L"Alt+Enter" },
    { L"Toggle statistics", "toggle_stats_overlay", L"Ctrl+i" },
    { L"Set PQ", "pq_set", L"Ctrl+Shift+p" },
    { L"Restart renderer", "renderer_restart", L"Shift+r" },
    { L"Reset renderer", "renderer_reset", L"r" },
    { L"Capture input 1", "capture_1", L"Ctrl+1" },
    { L"Capture input 2", "capture_2", L"Ctrl+2" },
    { L"Capture input 3", "capture_3", L"Ctrl+3" },
    { L"Capture input 4", "capture_4", L"Ctrl+4" },
    { L"Disable video conversion", "video_conversion_off", L"v" },
    { L"Use P010 conversion", "video_conversion_p010", L"Shift+v" },
    { L"Open configuration", "config_editor", L"Ctrl+Shift+s" }
};

std::wstring ToWide(const std::string& value)
{
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_ACP, 0, value.c_str(), -1, nullptr, 0);
    std::wstring result(static_cast<size_t>(size > 0 ? size : 0), L'\0');
    if (size > 1) MultiByteToWideChar(CP_ACP, 0, value.c_str(), -1, &result[0], size);
    if (!result.empty()) result.pop_back();
    return result;
}

std::string ToNarrow(const std::wstring& value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size > 0 ? size : 0), '\0');
    if (size > 1) WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, &result[0], size, nullptr, nullptr);
    if (!result.empty()) result.pop_back();
    return result;
}

std::wstring ExecutableDirectory()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    std::wstring result(path);
    const size_t slash = result.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : result.substr(0, slash);
}

bool PathExists(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring AbsolutePath(const std::wstring& path)
{
    const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (required == 0) return path;
    std::wstring absolute(static_cast<size_t>(required), L'\0');
    const DWORD written = GetFullPathNameW(path.c_str(), required, &absolute[0], nullptr);
    if (written == 0 || written >= required) return path;
    absolute.resize(written);
    return absolute;
}

// An installed editor always loads the configuration beside itself.  A local
// development build lives three directories below the source checkout, where
// the checked-in sample is the only meaningful direct-launch default.  This
// fallback is intentionally gated on the project file so an installed binary
// can never walk up into an unrelated parent directory.
std::wstring DirectLaunchConfigPath()
{
    const std::wstring besideExecutable = ExecutableDirectory() + L"\\VideoProcessor.cfg";
    if (PathExists(besideExecutable)) return besideExecutable;
    const std::wstring projectRoot = ExecutableDirectory() + L"\\..\\..\\..\\..";
    if (PathExists(projectRoot + L"\\src\\VideoProcessor-Config\\VideoProcessor-Config.vcxproj") &&
        PathExists(projectRoot + L"\\VideoProcessor.cfg"))
        return AbsolutePath(projectRoot + L"\\VideoProcessor.cfg");
    return besideExecutable;
}

std::wstring Timestamp()
{
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    wchar_t value[32] = {};
    swprintf_s(value, L"%04u%02u%02u-%02u%02u%02u", time.wYear, time.wMonth,
        time.wDay, time.wHour, time.wMinute, time.wSecond);
    return value;
}

struct ConfigDocument
{
    std::wstring path;
    std::vector<std::string> lines;
    std::string lineEnding = "\r\n";

    bool Load(const std::wstring& input, std::wstring& error)
    {
        path = input;
        std::ifstream inputFile(ToNarrow(path), std::ios::binary);
        if (!inputFile)
        {
            error = L"Cannot open " + path;
            return false;
        }
        std::string text((std::istreambuf_iterator<char>(inputFile)), {});
        lineEnding = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        lines.clear();
        size_t start = 0;
        while (start < text.size())
        {
            const size_t end = text.find_first_of("\r\n", start);
            lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
            if (text[end] == '\r' && start < text.size() && text[start] == '\n') ++start;
        }
        return true;
    }

    static std::string StripComment(const std::string& value)
    {
        for (size_t index = 0; index < value.size(); ++index)
            if ((value[index] == '#' || value[index] == ';') &&
                (index == 0 || isspace(static_cast<unsigned char>(value[index - 1]))))
                return value.substr(0, index);
        return value;
    }

    bool Find(const std::string& wantedSection, const std::string& wantedKey,
        size_t& lineIndex, size_t& valueStart, size_t& valueEnd) const
    {
        std::string section;
        for (size_t index = 0; index < lines.size(); ++index)
        {
            const std::string& raw = lines[index];
            const std::string text = ConfigFile::Trim(StripComment(raw));
            if (text.size() >= 2 && text.front() == '[' && text.back() == ']')
            {
                section = ConfigFile::NormalizeName(text.substr(1, text.size() - 2));
                continue;
            }
            if (section != wantedSection) continue;
            const size_t colon = raw.find(':');
            const size_t equal = raw.find('=');
            const size_t separator = colon == std::string::npos ? equal :
                (equal == std::string::npos ? colon : std::min(colon, equal));
            if (separator == std::string::npos) continue;
            if (ConfigFile::NormalizeName(raw.substr(0, separator)) != wantedKey) continue;
            const size_t comment = StripComment(raw).size();
            valueStart = separator + 1;
            while (valueStart < comment && isspace(static_cast<unsigned char>(raw[valueStart]))) ++valueStart;
            valueEnd = comment;
            while (valueEnd > valueStart && isspace(static_cast<unsigned char>(raw[valueEnd - 1]))) --valueEnd;
            lineIndex = index;
            return true;
        }
        return false;
    }

    std::string Get(const char* section, const char* key) const
    {
        size_t line = 0, start = 0, end = 0;
        return Find(section, key, line, start, end) ? lines[line].substr(start, end - start) : std::string();
    }

    bool SetExisting(const char* section, const char* key, const std::string& value)
    {
        size_t line = 0, start = 0, end = 0;
        if (!Find(section, key, line, start, end)) return false;
        lines[line].replace(start, end - start, value);
        return true;
    }

    // Structured controls may add one of their own known keys to an existing
    // section. Everything else (comments, rules, shaders, and unknown keys)
    // remains untouched.
    bool SetKnown(const std::string& wantedSection, const char* key, const std::string& value)
    {
        size_t line = 0, start = 0, end = 0;
        if (Find(wantedSection, key, line, start, end))
        {
            lines[line].replace(start, end - start, value);
            return true;
        }
        const std::string normalized = ConfigFile::NormalizeName(wantedSection);
        for (size_t index = 0; index < lines.size(); ++index)
        {
            const std::string text = ConfigFile::Trim(StripComment(lines[index]));
            if (text.size() < 2 || text.front() != '[' || text.back() != ']') continue;
            if (ConfigFile::NormalizeName(text.substr(1, text.size() - 2)) != normalized) continue;
            size_t insertAt = index + 1;
            while (insertAt < lines.size())
            {
                const std::string next = ConfigFile::Trim(StripComment(lines[insertAt]));
                if (next.size() >= 2 && next.front() == '[' && next.back() == ']') break;
                ++insertAt;
            }
            lines.insert(lines.begin() + insertAt, std::string(key) + ": " + value);
            return true;
        }
        return false;
    }

    bool RemoveKnown(const std::string& wantedSection, const char* key)
    {
        size_t line = 0, start = 0, end = 0;
        if (!Find(wantedSection, key, line, start, end)) return false;
        lines.erase(lines.begin() + line);
        return true;
    }

    bool AddSection(const std::string& section)
    {
        const std::string normalized = ConfigFile::NormalizeName(section);
        for (const std::string& existing : SectionNamesWithPrefix(section))
            if (existing == normalized) return false;
        if (!lines.empty() && !lines.back().empty()) lines.push_back({});
        lines.push_back("[" + section + "]");
        return true;
    }

    bool RemoveSection(const std::string& wantedSection)
    {
        const std::string normalized = ConfigFile::NormalizeName(wantedSection);
        for (size_t index = 0; index < lines.size(); ++index)
        {
            const std::string text = ConfigFile::Trim(StripComment(lines[index]));
            if (text.size() < 2 || text.front() != '[' || text.back() != ']') continue;
            if (ConfigFile::NormalizeName(text.substr(1, text.size() - 2)) != normalized) continue;
            size_t end = index + 1;
            while (end < lines.size())
            {
                const std::string next = ConfigFile::Trim(StripComment(lines[end]));
                if (next.size() >= 2 && next.front() == '[' && next.back() == ']') break;
                ++end;
            }
            if (end < lines.size() && end > index + 1 && lines[end - 1].empty()) --end;
            lines.erase(lines.begin() + index, lines.begin() + end);
            return true;
        }
        return false;
    }

    bool FindSectionHeader(const std::string& wantedSection, size_t& lineIndex,
        size_t& nameStart, size_t& nameEnd) const
    {
        const std::string normalized = ConfigFile::NormalizeName(wantedSection);
        for (size_t index = 0; index < lines.size(); ++index)
        {
            const std::string text = ConfigFile::Trim(StripComment(lines[index]));
            if (text.size() < 2 || text.front() != '[' || text.back() != ']') continue;
            if (ConfigFile::NormalizeName(text.substr(1, text.size() - 2)) != normalized) continue;
            const size_t open = lines[index].find('[');
            const size_t close = lines[index].find(']', open);
            if (open == std::string::npos || close == std::string::npos) return false;
            lineIndex = index; nameStart = open + 1; nameEnd = close;
            return true;
        }
        return false;
    }

    bool RenameSection(const std::string& oldSection, const std::string& newSection)
    {
        const std::string oldName = ConfigFile::NormalizeName(oldSection);
        const std::string newName = ConfigFile::NormalizeName(newSection);
        if (oldName.empty() || newName.empty()) return false;
        if (oldName == newName) return true;
        for (const std::string& existing : SectionNamesWithPrefix(newName))
            if (existing == newName) return false;
        size_t line = 0, start = 0, end = 0;
        if (!FindSectionHeader(oldName, line, start, end)) return false;
        lines[line].replace(start, end - start, newName);
        return true;
    }

    bool SwapSectionHeaders(const std::string& first, const std::string& second)
    {
        if (ConfigFile::NormalizeName(first) == ConfigFile::NormalizeName(second)) return true;
        size_t firstLine = 0, firstStart = 0, firstEnd = 0;
        size_t secondLine = 0, secondStart = 0, secondEnd = 0;
        if (!FindSectionHeader(first, firstLine, firstStart, firstEnd) ||
            !FindSectionHeader(second, secondLine, secondStart, secondEnd)) return false;
        lines[firstLine].replace(firstStart, firstEnd - firstStart, second);
        lines[secondLine].replace(secondStart, secondEnd - secondStart, first);
        return true;
    }

    bool MoveSectionBefore(const std::string& wantedSection, const std::string& beforeSection)
    {
        if (ConfigFile::NormalizeName(wantedSection) == ConfigFile::NormalizeName(beforeSection)) return true;
        size_t start = 0, nameStart = 0, nameEnd = 0;
        if (!FindSectionHeader(wantedSection, start, nameStart, nameEnd)) return false;
        size_t beforeLine = 0;
        if (!FindSectionHeader(beforeSection, beforeLine, nameStart, nameEnd)) return false;
        size_t end = start + 1;
        while (end < lines.size())
        {
            const std::string next = ConfigFile::Trim(StripComment(lines[end]));
            if (next.size() >= 2 && next.front() == '[' && next.back() == ']') break;
            ++end;
        }
        std::vector<std::string> block(lines.begin() + start, lines.begin() + end);
        if (beforeLine > start) beforeLine -= end - start;
        lines.erase(lines.begin() + start, lines.begin() + end);
        lines.insert(lines.begin() + beforeLine, block.begin(), block.end());
        return true;
    }

    bool MoveSectionAfter(const std::string& wantedSection, const std::string& afterSection)
    {
        if (ConfigFile::NormalizeName(wantedSection) == ConfigFile::NormalizeName(afterSection)) return true;
        size_t start = 0, nameStart = 0, nameEnd = 0;
        size_t afterLine = 0, afterNameStart = 0, afterNameEnd = 0;
        if (!FindSectionHeader(wantedSection, start, nameStart, nameEnd) ||
            !FindSectionHeader(afterSection, afterLine, afterNameStart, afterNameEnd)) return false;
        size_t end = start + 1;
        while (end < lines.size())
        {
            const std::string next = ConfigFile::Trim(StripComment(lines[end]));
            if (next.size() >= 2 && next.front() == '[' && next.back() == ']') break;
            ++end;
        }
        std::vector<std::string> block(lines.begin() + start, lines.begin() + end);
        lines.erase(lines.begin() + start, lines.begin() + end);

        afterLine = 0;
        if (!FindSectionHeader(afterSection, afterLine, nameStart, nameEnd)) return false;
        size_t insertAt = afterLine + 1;
        while (insertAt < lines.size())
        {
            const std::string next = ConfigFile::Trim(StripComment(lines[insertAt]));
            if (next.size() >= 2 && next.front() == '[' && next.back() == ']') break;
            ++insertAt;
        }
        lines.insert(lines.begin() + insertAt, block.begin(), block.end());
        return true;
    }

    std::vector<std::string> SectionNamesWithPrefix(const std::string& prefix) const
    {
        std::vector<std::string> result;
        const std::string normalizedPrefix = ConfigFile::NormalizeName(prefix);
        for (const std::string& raw : lines)
        {
            const std::string text = ConfigFile::Trim(StripComment(raw));
            if (text.size() < 2 || text.front() != '[' || text.back() != ']') continue;
            const std::string section = ConfigFile::NormalizeName(text.substr(1, text.size() - 2));
            if (section == normalizedPrefix || section.rfind(normalizedPrefix + ".", 0) == 0)
                result.push_back(section);
        }
        return result;
    }

    std::string Serialize() const
    {
        std::ostringstream output;
        for (size_t index = 0; index < lines.size(); ++index)
        {
            output << lines[index];
            if (index + 1 < lines.size()) output << lineEnding;
        }
        return output.str();
    }
};

bool ValidateCandidate(const ConfigDocument& document, std::wstring& error)
{
    const std::wstring temporary = document.path + L".vpconfig-validate.tmp";
    {
        std::ofstream output(ToNarrow(temporary), std::ios::binary | std::ios::trunc);
        output << document.Serialize();
        if (!output) { error = L"Cannot create a validation copy beside the configuration."; return false; }
    }
    ConfigFile config;
    const bool loaded = config.Load(ToNarrow(temporary));
    DeleteFileW(temporary.c_str());
    if (!loaded) { error = L"The candidate configuration could not be read."; return false; }
    if (!config.GetWarnings().empty()) { error = ToWide(config.GetWarnings().front()); return false; }
    std::string schemaError;
    RendererProfileConfig::Model rendererModel;
    if (!MainConfigSchema::Validate(config, schemaError) ||
        !RendererProfileConfig::Read(config, rendererModel, schemaError))
    {
        error = ToWide(schemaError);
        return false;
    }
    return true;
}

bool PromptViewportName(HWND owner, std::wstring& value,
    const std::wstring& caption, const std::wstring& prompt,
    const std::wstring& confirmCaption, const std::wstring& initial = L"");

class EditorWindow
{
public:
    HWND window = nullptr;
    ConfigDocument document;
    NOTIFYICONDATAW tray = {};
    HWND controls[23] = {};
    HWND startupLabels[6] = {};
    HWND startupFields[6] = {};
    std::string startupEffectiveDefaults[2];
    HWND cards[9] = {};
    HWND navigation[6] = {};
    HWND contentHost = nullptr;
    HWND pageTitle = nullptr;
    HWND pageDescription = nullptr;
    HWND outputLabels[15] = {};
    HWND outputUnits[3] = {};
    HWND cardTitles[9] = {};
    HWND cardSubtitles[9] = {};
    HWND lldvLabels[4] = {};
    HWND lldvFields[4] = {};
    HWND lldvUnits[4] = {};
    HWND shortcutLabels[ARRAYSIZE(kShortcutFields)] = {};
    HWND shortcutFields[ARRAYSIZE(kShortcutFields)] = {};
    HWND profileSelector = nullptr;
    HWND profileListHint = nullptr;
    HWND addProfileButton = nullptr;
    HWND removeProfileButton = nullptr;
    HWND renameProfileButton = nullptr;
    HWND moveProfileUpButton = nullptr;
    HWND moveProfileDownButton = nullptr;
    HWND profileRuleLabel = nullptr;
    HWND profileRule = nullptr;
    HWND profileRuleHelp = nullptr;
    HWND profileKeyLabel = nullptr;
    HWND profileKey = nullptr;
    HWND profileKeyHelp = nullptr;
    HWND profileNameLabel = nullptr;
    HWND profileNameEdit = nullptr;
    HWND queueRecoveryTitle = nullptr;
    HWND queueRecoveryHelp = nullptr;
    HWND queueRecoveryLabels[2] = {};
    HWND queueRecoveryFields[2] = {};
    HWND queueRecoveryUnits[2] = {};
    std::vector<std::string> queueSections;
    std::vector<std::string> rendererSections;
    std::vector<std::string> lldvSections;
    int activeQueueProfile = -1;
    int activeRendererProfile = -1;
    int activeLldvProfile = -1;
    HWND viewportLabels[11] = {};
    HWND viewportDetailTitle = nullptr;
    HWND viewportDetailSubtitle = nullptr;
    HWND viewportListHint = nullptr;
    HWND viewportUnits[3] = {};
    int activePage = 0;
    HWND viewportSelector = nullptr;
    HWND viewportFields[8] = {};
    HWND viewportRule = nullptr;
    HWND viewportKeyLabel = nullptr;
    HWND viewportKey = nullptr;
    HWND viewportKeyHelp = nullptr;
    HWND viewportNameLabel = nullptr;
    HWND viewportNameEdit = nullptr;
    HWND addViewportButton = nullptr;
    HWND removeViewportButton = nullptr;
    HWND nameViewportButton = nullptr;
    HWND moveViewportUpButton = nullptr;
    HWND moveViewportDownButton = nullptr;
    std::vector<std::string> viewportSections;
    int activeViewport = -1;
    int viewportDragSource = -1;
    int viewportDragInsertion = -1;
    POINT viewportDragStart = {};
    POINT viewportDragPoint = {};
    bool viewportDragging = false;
    HWND status = nullptr;
    HWND summary = nullptr;
    HWND headerTitle = nullptr;
    HWND headerSubtitle = nullptr;
    HWND headerIcon = nullptr;
    HWND navigationCaption = nullptr;
    HFONT bodyFont = nullptr;
    HFONT smallFont = nullptr;
    HFONT headingFont = nullptr;
    HFONT captionFont = nullptr;
    HFONT sectionFont = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HBRUSH headerBrush = nullptr;
    HBRUSH validationBrush = nullptr;
    UINT dpi = 96;
    int contentScroll = 0;
    int contentViewHeight = 0;
    int contentHeight = 0;
    bool closeTipShown = false;
    bool dirty = false;
    bool generatedNamesPending = false;
    bool profileNameCommitFailed = false;
    HWND validationControl = nullptr;

    int Px(int dip) const { return MulDiv(dip, static_cast<int>(dpi), 96); }

    HFONT CreateUiFont(int points, int weight) const
    {
        NONCLIENTMETRICSW metrics = { sizeof(metrics) };
        if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,
            sizeof(metrics), &metrics, 0, dpi) &&
            !SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,
                sizeof(metrics), &metrics, 0))
        {
            metrics.lfMessageFont = {};
            wcscpy_s(metrics.lfMessageFont.lfFaceName,
                L"Segoe UI Variable Text");
        }
        LOGFONTW font = metrics.lfMessageFont;
        font.lfHeight = -MulDiv(points, static_cast<int>(dpi), 72);
        font.lfWidth = 0;
        font.lfWeight = weight;
        font.lfQuality = DEFAULT_QUALITY;
        return CreateFontIndirectW(&font);
    }

    int MeasureTextHeight(HWND control, int width, int minimum) const
    {
        if (control == nullptr || width <= 0) return minimum;
        const int length = GetWindowTextLengthW(control);
        std::wstring value(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(control, &value[0], length + 1);
        HDC dc = GetDC(control);
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
        HGDIOBJ previous = font == nullptr ? nullptr : SelectObject(dc, font);
        RECT measured = { 0, 0, width, 0 };
        DrawTextW(dc, value.c_str(), length, &measured,
            DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
        if (previous != nullptr) SelectObject(dc, previous);
        ReleaseDC(control, dc);
        return std::max(minimum,
            static_cast<int>(measured.bottom - measured.top) + Px(2));
    }

    void CreateFonts()
    {
        DeleteObject(bodyFont); DeleteObject(smallFont); DeleteObject(headingFont);
        DeleteObject(captionFont); DeleteObject(sectionFont);
        bodyFont = CreateUiFont(9, FW_NORMAL);
        smallFont = CreateUiFont(9, FW_NORMAL);
        captionFont = CreateUiFont(9, FW_SEMIBOLD);
        sectionFont = CreateUiFont(11, FW_SEMIBOLD);
        headingFont = CreateUiFont(17, FW_SEMIBOLD);
    }

    void SetFont(HWND control, HFONT font)
    {
        if (control != nullptr) SendMessageW(control, WM_SETFONT,
            reinterpret_cast<WPARAM>(font), TRUE);
    }

    void ApplyFonts()
    {
        EnumChildWindows(window, [](HWND child, LPARAM font) -> BOOL {
            SendMessageW(child, WM_SETFONT, font, TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(bodyFont));
        SetFont(headerTitle, headingFont);
        SetFont(headerSubtitle, captionFont);
        SetFont(pageTitle, headingFont);
        SetFont(navigationCaption, smallFont);
        SetFont(summary, smallFont);
        SetFont(status, smallFont);
        for (HWND title : cardTitles) SetFont(title, sectionFont);
        SetFont(viewportDetailTitle, sectionFont);
        SetFont(queueRecoveryTitle, sectionFont);
        for (HWND subtitle : cardSubtitles) SetFont(subtitle, smallFont);
        SetFont(viewportDetailSubtitle, smallFont);
        SetFont(viewportListHint, smallFont);
        SetFont(profileListHint, smallFont);
        SetFont(profileRuleHelp, smallFont);
        SetFont(profileKeyHelp, smallFont);
        SetFont(viewportKeyHelp, smallFont);
        for (HWND unit : outputUnits) SetFont(unit, smallFont);
        for (HWND unit : viewportUnits) SetFont(unit, smallFont);
        for (HWND unit : queueRecoveryUnits) SetFont(unit, smallFont);
        for (HWND unit : lldvUnits) SetFont(unit, smallFont);
        SetFont(queueRecoveryHelp, smallFont);
        SetFont(viewportLabels[1], smallFont);
        SetFont(viewportLabels[4], smallFont);
        SetFont(viewportLabels[6], smallFont);
    }

    void SetDirty(bool value = true)
    {
        if (value && validationControl != nullptr)
        {
            InvalidateRect(GetParent(validationControl), nullptr, TRUE);
            InvalidateRect(validationControl, nullptr, TRUE);
            validationControl = nullptr;
        }
        dirty = value;
        EnableWindow(GetDlgItem(window, kSaveButton), dirty ? TRUE : FALSE);
        if (dirty)
            SetWindowTextW(status, L"Unsaved changes. Validate before saving if you want to check the complete configuration.");
    }

    void EnsureControlVisible(HWND control)
    {
        if (control == nullptr || contentHost == nullptr) return;
        RECT field = {}, host = {};
        GetWindowRect(control, &field);
        GetWindowRect(contentHost, &host);
        const int margin = Px(16);
        if (field.top < host.top + margin)
            contentScroll = std::max(0, contentScroll - static_cast<int>(host.top + margin - field.top));
        else if (field.bottom > host.bottom - margin)
            contentScroll += static_cast<int>(field.bottom - (host.bottom - margin));
        LayoutPageContent();
    }

    void FocusValidationControl(HWND control, const std::wstring& message)
    {
        validationControl = control;
        EnsureControlVisible(control);
        SetFocus(control);
        InvalidateRect(GetParent(control), nullptr, TRUE);
        InvalidateRect(control, nullptr, TRUE);
        SetWindowTextW(status, message.c_str());
    }

    bool Load(const std::wstring& path)
    {
        std::wstring error;
        if (!document.Load(path, error)) { MessageBoxW(window, error.c_str(), L"VideoProcessor Config", MB_ICONERROR); return false; }
        generatedNamesPending = false;
        NormalizeUnnamedProfilesForEditing();
        Populate();
        SetDirty(generatedNamesPending);
        if (generatedNamesPending)
            SetWindowTextW(status, L"Legacy profile names or Queue recovery settings were migrated safely. Save changes to persist them.");
        return true;
    }

    static bool ReadBoolean(const std::string& value)
    {
        const std::string normalized = ConfigFile::NormalizeName(value);
        return normalized == "true" || normalized == "yes" || normalized == "on" || normalized == "1";
    }

    static std::wstring StartupDisplayValue(int index, const std::string& value)
    {
        if (value.empty())
        {
            if (index <= 2) return L"";
        }
        const std::string normalized = ConfigFile::NormalizeName(value);
        if (index == 3)
            return normalized == "target-only" ? L"Target monitor only" : L"Keep the existing desktop";
        if (index == 4)
        {
            if (normalized == "follow_input_lldv") return L"Follow input (LLDV aware)";
            if (normalized == "follow_container") return L"Follow container metadata";
            if (normalized == "bt2020") return L"BT.2020";
            if (normalized == "p3") return L"Display P3";
            if (normalized == "rec709") return L"Rec. 709";
            if (normalized == "follow_input" || normalized.empty()) return L"Follow input";
        }
        if (index == 5)
        {
            if (normalized == "follow_input_lldv") return L"Follow input (LLDV aware)";
            if (normalized == "hdr_luminance_user") return L"Use configured HDR luminance";
            if (normalized == "follow_input" || normalized.empty()) return L"Follow input";
        }
        return ToWide(value);
    }

    static std::string StartupStoredValue(int index, const std::string& displayed)
    {
        const std::wstring value = ToWide(displayed);
        if (index <= 2 && value.empty()) return {};
        if (index == 3)
            return value == L"Target monitor only" ? "target-only" : "existing";
        if (index == 4)
        {
            if (value == L"Follow input (LLDV aware)") return "FOLLOW_INPUT_LLDV";
            if (value == L"Follow container metadata") return "FOLLOW_CONTAINER";
            if (value == L"BT.2020") return "BT2020";
            if (value == L"Display P3") return "P3";
            if (value == L"Rec. 709") return "REC709";
            if (value == L"Follow input") return "FOLLOW_INPUT";
        }
        if (index == 5)
        {
            if (value == L"Follow input (LLDV aware)") return "FOLLOW_INPUT_LLDV";
            if (value == L"Use configured HDR luminance") return "HDR_LUMINANCE_USER";
            if (value == L"Follow input") return "FOLLOW_INPUT";
        }
        return displayed;
    }

    void UpdateStartupControlState()
    {
        const bool fullscreen = SendMessageW(controls[0], BM_GETCHECK, 0, 0) == BST_CHECKED;
        for (HWND control : { controls[1], startupLabels[2], startupFields[2], startupLabels[3], startupFields[3] })
            EnableWindow(control, fullscreen ? TRUE : FALSE);
        const bool detectionDisabled = SendMessageW(controls[20], BM_GETCHECK, 0, 0) == BST_CHECKED;
        EnableWindow(controls[2], detectionDisabled ? FALSE : TRUE);
        RedrawWindow(cards[7], nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        RedrawWindow(cards[8], nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }

    void RefreshRendererChoices(bool hideLegacyRenderers)
    {
        std::wstring selected = StartupDisplayValue(1,
            ConfigFile::Trim(GetControlText(startupFields[1])));
        SendMessageW(startupFields[1], CB_RESETCONTENT, 0, 0);
        const std::vector<std::wstring> renderers =
            ConfigurationDiscovery::RendererNames(hideLegacyRenderers);
        for (const std::wstring& value : renderers)
            SendMessageW(startupFields[1], CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(value.c_str()));
        startupEffectiveDefaults[1] = renderers.empty() ? std::string() :
            ToNarrow(renderers.front());
        if (document.Get("general", "renderer").empty())
            selected = renderers.empty() ? std::wstring() : renderers.front();
        if (selected.empty())
            SendMessageW(startupFields[1], CB_SETCURSEL,
                static_cast<WPARAM>(-1), 0);
        SetWindowTextW(startupFields[1], selected.c_str());
    }

    void PopulateStandaloneSettings()
    {
        for (size_t index = 0; index < ARRAYSIZE(kShortcutFields); ++index)
        {
            const std::string configured = document.Get("shortcuts", kShortcutFields[index].key);
            SetWindowTextW(shortcutFields[index], configured.empty() ?
                kShortcutFields[index].defaultValue : ToWide(configured).c_str());
        }
    }

    void SetOptionalKnown(const std::string& section, const char* key,
        const std::string& value)
    {
        const std::string original = document.Get(section.c_str(), key);
        if (value.empty())
        {
            if (!original.empty()) document.RemoveKnown(section, key);
            return;
        }
        if (original == value) return;
        if (!document.SetKnown(section, key, value))
        {
            if (!document.AddSection(section) || !document.SetKnown(section, key, value))
                return;
        }
    }

    void ApplyStandaloneSettings()
    {
        for (size_t index = 0; index < ARRAYSIZE(kShortcutFields); ++index)
        {
            const std::string value = ConfigFile::Trim(GetControlText(shortcutFields[index]));
            const std::string defaultValue = ToNarrow(kShortcutFields[index].defaultValue);
            SetOptionalKnown("shortcuts", kShortcutFields[index].key,
                ConfigFile::NormalizeName(value) == ConfigFile::NormalizeName(defaultValue) ? std::string() : value);
        }
    }

    bool ValidateFixedShortcuts(std::wstring& error, HWND& control)
    {
        std::vector<std::string> used;
        for (size_t index = 0; index < ARRAYSIZE(kShortcutFields); ++index)
        {
            std::string value = ConfigFile::Trim(GetControlText(shortcutFields[index]));
            if (value.empty()) value = ToNarrow(kShortcutFields[index].defaultValue);
            std::string canonical;
            if (!RendererProfileConfig::CanonicalizeKeyChord(value, canonical))
            {
                error = std::wstring(kShortcutFields[index].label) +
                    L" must be one key with optional Ctrl, Alt, or Shift modifiers.";
                control = shortcutFields[index];
                return false;
            }
            if (std::find(used.begin(), used.end(), canonical) != used.end())
            {
                error = L"Fixed command shortcuts must be unique. Duplicate: " + ToWide(canonical);
                control = shortcutFields[index];
                return false;
            }
            used.push_back(canonical);
        }

        for (const char* prefix : { "queue", "vprenderer", "lldv", "shader" })
            for (const std::string& section : document.SectionNamesWithPrefix(prefix))
            {
                const std::string value = ConfigFile::Trim(document.Get(section.c_str(), "shortcut"));
                if (value.empty()) continue;
                std::string canonical;
                if (!RendererProfileConfig::CanonicalizeKeyChord(value, canonical)) continue;
                if (std::find(used.begin(), used.end(), canonical) != used.end())
                {
                    error = L"A fixed command shortcut conflicts with [" + ToWide(section) +
                        L"]: " + ToWide(canonical);
                    control = shortcutFields[0];
                    return false;
                }
            }
        return true;
    }

    static std::wstring ViewportIdentifier(const std::string& section)
    {
        static const std::string prefix = "vprenderer.viewport";
        return section == prefix ? L"" : ToWide(section.substr(prefix.size() + 1));
    }

    static std::wstring FriendlyViewportIdentifier(const std::string& section)
    {
        std::wstring name = ViewportIdentifier(section);
        std::replace(name.begin(), name.end(), L'_', L' ');
        return name;
    }

    std::wstring ViewportName(const std::string& section) const
    {
        if (section == "vprenderer.viewport") return L"Unnamed viewport (legacy)";
        const std::string label = ConfigFile::Trim(document.Get(section.c_str(), "label"));
        return label.empty() ? FriendlyViewportIdentifier(section) : ToWide(label);
    }

    static std::string ViewportIdentifierForLabel(const std::wstring& label)
    {
        std::string identifier;
        const std::string source = ToNarrow(label);
        for (const unsigned char character : source)
        {
            if (std::isalnum(character) != 0)
                identifier.push_back(static_cast<char>(std::tolower(character)));
            else if (std::isspace(character) != 0)
                identifier.push_back('_');
            else if (character == '_')
                identifier.append("__");
            else if (character == '-')
                identifier.push_back('-');
            else
                identifier.push_back('-');
        }
        if (identifier.empty() || std::isalpha(static_cast<unsigned char>(identifier.front())) == 0)
            identifier.insert(0, "viewport_");
        if (identifier.size() > 64) identifier.resize(64);
        while (!identifier.empty() && identifier.back() == '-') identifier.pop_back();
        if (!RendererProfileConfig::IsIdentifier(identifier) ||
            RendererProfileConfig::IsReservedViewportIdentifier(identifier))
            identifier = "viewport";
        return identifier;
    }

    std::string UniqueViewportIdentifier(const std::wstring& label) const
    {
        const std::string base = ViewportIdentifierForLabel(label);
        for (unsigned int suffix = 1; ; ++suffix)
        {
            const std::string ending = suffix == 1 ? "" : "-" + std::to_string(suffix);
            const std::string candidate = base.substr(0, 64 - ending.size()) + ending;
            const std::string section = "vprenderer.viewport." + candidate;
            bool inUse = false;
            for (const std::string& existing : document.SectionNamesWithPrefix("vprenderer.viewport"))
                if (existing == section) { inUse = true; break; }
            if (!inUse) return candidate;
        }
    }

    static std::wstring FriendlyProfileIdentifier(const std::string& identifier)
    {
        std::wstring result;
        const std::wstring source = ToWide(identifier);
        for (size_t index = 0; index < source.size(); ++index)
        {
            if (source[index] != L'_') { result.push_back(source[index]); continue; }
            if (index + 1 < source.size() && source[index + 1] == L'_')
            {
                result.push_back(L'_');
                ++index;
            }
            else result.push_back(L' ');
        }
        return result;
    }

    static const char* ProfilePrefix(int page)
    {
        return page == 1 ? "queue" : (page == 2 ? "vprenderer" : "lldv");
    }

    std::vector<std::string>& ProfileSections(int page)
    {
        return page == 1 ? queueSections : (page == 2 ? rendererSections : lldvSections);
    }

    int& ActiveProfile(int page)
    {
        return page == 1 ? activeQueueProfile : (page == 2 ? activeRendererProfile : activeLldvProfile);
    }

    void RefreshProfileSections(int page)
    {
        const std::string prefix = ProfilePrefix(page);
        auto& sections = ProfileSections(page);
        sections.clear();
        for (const std::string& section : document.SectionNamesWithPrefix(prefix))
        {
            if (section == prefix) { sections.push_back(section); continue; }
            const std::string suffix = section.substr(prefix.size() + 1);
            if (suffix.find('.') != std::string::npos) continue;
            if (page == 2 && (suffix == "input" || suffix == "scaling" || suffix == "viewport"))
                continue;
            sections.push_back(section);
        }
    }

    std::wstring ProfileName(int page, const std::string& section) const
    {
        const std::string prefix = ProfilePrefix(page);
        if (section == prefix)
            return page == 1 ? L"Queue baseline" : (page == 2 ? L"Renderer baseline" : L"LLDV baseline");
        std::wstring name = FriendlyProfileIdentifier(section.substr(prefix.size() + 1));
        if (name.rfind(L"profile ", 0) == 0 && name.size() > 8 &&
            std::all_of(name.begin() + 8, name.end(), [](wchar_t value) { return iswdigit(value) != 0; }))
            name[0] = L'P';
        return name;
    }

    std::string UniqueProfileIdentifier(int page, const std::wstring& label) const
    {
        const std::string prefix = ProfilePrefix(page);
        const std::string base = ViewportIdentifierForLabel(label);
        for (unsigned int suffix = 1; ; ++suffix)
        {
            const std::string ending = suffix == 1 ? "" : "-" + std::to_string(suffix);
            const std::string candidate = base.substr(0, 64 - ending.size()) + ending;
            const std::string section = prefix + "." + candidate;
            bool inUse = false;
            for (const std::string& existing : document.SectionNamesWithPrefix(prefix))
                if (existing == section) { inUse = true; break; }
            if (!inUse) return candidate;
        }
    }

    bool VisibleViewportNameInUse(const std::wstring& wanted,
        const std::string& exceptSection = {}) const
    {
        const std::string normalized = ConfigFile::NormalizeName(ToNarrow(wanted));
        for (const std::string& section : document.SectionNamesWithPrefix("vprenderer.viewport"))
        {
            if (section == "vprenderer.viewport" || section == exceptSection) continue;
            if (ConfigFile::NormalizeName(ToNarrow(ViewportName(section))) == normalized)
                return true;
        }
        return false;
    }

    std::wstring UniqueGeneratedName(const std::string& prefix, int page = 0) const
    {
        for (unsigned int number = 1; ; ++number)
        {
            const std::wstring candidate = L"Profile " + std::to_wstring(number);
            if (prefix == "vprenderer.viewport")
            {
                if (!VisibleViewportNameInUse(candidate)) return candidate;
                continue;
            }
            const std::string wanted = ConfigFile::NormalizeName(ToNarrow(candidate));
            bool used = false;
            for (const std::string& section : document.SectionNamesWithPrefix(prefix))
            {
                if (section == prefix) continue;
                const std::string suffix = section.substr(prefix.size() + 1);
                if (suffix.find('.') != std::string::npos) continue;
                if (page == 2 && (suffix == "input" || suffix == "scaling" || suffix == "viewport")) continue;
                if (ConfigFile::NormalizeName(ToNarrow(FriendlyProfileIdentifier(suffix))) == wanted)
                { used = true; break; }
            }
            if (!used) return candidate;
        }
    }

    void NormalizeUnnamedProfile(const std::string& prefix, int page)
    {
        const auto all = document.SectionNamesWithPrefix(prefix);
        if (std::find(all.begin(), all.end(), prefix) == all.end()) return;
        std::string firstNamed;
        for (const std::string& section : all)
        {
            if (section == prefix) continue;
            const std::string suffix = section.substr(prefix.size() + 1);
            if (suffix.find('.') != std::string::npos) continue;
            if (page == 2 && (suffix == "input" || suffix == "scaling" || suffix == "viewport")) continue;
            firstNamed = section;
            break;
        }
        const std::wstring label = UniqueGeneratedName(prefix, page);
        const std::string renamed = prefix + "." + UniqueProfileIdentifier(page, label);
        if (!document.RenameSection(prefix, renamed)) return;
        if (!firstNamed.empty()) document.MoveSectionBefore(renamed, firstNamed);
        generatedNamesPending = true;
    }

    void NormalizeUnnamedViewport()
    {
        const std::string root = "vprenderer.viewport";
        const auto sections = document.SectionNamesWithPrefix(root);
        if (std::find(sections.begin(), sections.end(), root) == sections.end()) return;
        std::string firstNamed;
        for (const std::string& section : sections)
            if (section != root) { firstNamed = section; break; }
        const std::string existingLabel = ConfigFile::Trim(document.Get(root.c_str(), "label"));
        const std::wstring label = !existingLabel.empty() &&
            !VisibleViewportNameInUse(ToWide(existingLabel), root) ?
            ToWide(existingLabel) : UniqueGeneratedName(root);
        const std::string renamed = root + "." + UniqueViewportIdentifier(label);
        if (!document.RenameSection(root, renamed)) return;
        document.SetKnown(renamed, "label", ToNarrow(label));
        if (!firstNamed.empty()) document.MoveSectionBefore(renamed, firstNamed);
        generatedNamesPending = true;
    }

    void NormalizeMissingViewportLabels()
    {
        for (const std::string& section : document.SectionNamesWithPrefix("vprenderer.viewport"))
        {
            if (section == "vprenderer.viewport" ||
                !ConfigFile::Trim(document.Get(section.c_str(), "label")).empty()) continue;
            std::wstring label = FriendlyViewportIdentifier(section);
            if (ConfigFile::Trim(ToNarrow(label)).empty() ||
                VisibleViewportNameInUse(label, section))
                label = UniqueGeneratedName("vprenderer.viewport");
            document.SetKnown(section, "label", ToNarrow(label));
            generatedNamesPending = true;
        }
    }

    void NormalizeUnnamedProfilesForEditing()
    {
        NormalizeUnnamedProfile("queue", 1);
        NormalizeUnnamedProfile("vprenderer", 2);
        NormalizeUnnamedProfile("lldv", 4);
        NormalizeUnnamedViewport();
        NormalizeMissingViewportLabels();
        std::string defaultQueue;
        for (const std::string& section : document.SectionNamesWithPrefix("queue"))
        {
            if (section == "queue") { defaultQueue = section; break; }
            const std::string suffix = section.substr(std::string("queue.").size());
            if (suffix.find('.') == std::string::npos) { defaultQueue = section; break; }
        }
        if (!defaultQueue.empty())
            for (const char* key : { "reset_after_render_restart_seconds",
                "reset_queue_too_large_percent" })
            {
                const std::string legacy = document.Get("queue_recovery", key);
                if (legacy.empty() || !document.Get(defaultQueue.c_str(), key).empty()) continue;
                document.SetKnown(defaultQueue, key, legacy);
                document.RemoveKnown("queue_recovery", key);
                generatedNamesPending = true;
            }
    }

    void SetProfileSelectableValue(HWND control, const std::string& value,
        const std::string& fallback, bool root)
    {
        const std::wstring inherited = L"Inherited";
        const LRESULT inheritedIndex = SendMessageW(control, CB_FINDSTRINGEXACT,
            static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(inherited.c_str()));
        if (root && inheritedIndex != CB_ERR)
            SendMessageW(control, CB_DELETESTRING, inheritedIndex, 0);
        else if (!root && inheritedIndex == CB_ERR)
            SendMessageW(control, CB_INSERTSTRING, 0, reinterpret_cast<LPARAM>(inherited.c_str()));
        SetSelectableValue(control, !root && value.empty() ? "Inherited" : value, fallback);
    }

    bool ProfileRuleVisible() const
    {
        return SendMessageW(profileRuleLabel, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    bool ViewportRuleVisible() const
    {
        return SendMessageW(viewportLabels[0], BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    void SetProfileRuleVisible(bool visible, bool relayout = false)
    {
        SendMessageW(profileRuleLabel, BM_SETCHECK,
            visible ? BST_CHECKED : BST_UNCHECKED, 0);
        ShowWindow(profileRule, visible ? SW_SHOW : SW_HIDE);
        ShowWindow(profileRuleHelp, visible ? SW_SHOW : SW_HIDE);
        if (relayout) LayoutPageContent();
    }

    void SetViewportRuleVisible(bool visible, bool relayout = false)
    {
        SendMessageW(viewportLabels[0], BM_SETCHECK,
            visible ? BST_CHECKED : BST_UNCHECKED, 0);
        ShowWindow(viewportRule, visible ? SW_SHOW : SW_HIDE);
        ShowWindow(viewportLabels[1], visible ? SW_SHOW : SW_HIDE);
        if (relayout) LayoutPageContent();
    }

    bool UsesNewLldvDefaults() const
    {
        std::string value = document.Get("general", "newlldv");
        if (value.empty()) value = document.Get("general", "new_lldv");
        if (value.empty()) value = document.Get("command_line", "newlldv");
        if (value.empty()) value = document.Get("command_line", "new_lldv");
        return ReadBoolean(value);
    }

    std::string LldvDefault(int index) const
    {
        const RendererProfileConfig::LldvMetadata defaults =
            RendererProfileConfig::DefaultLldvMetadata(UsesNewLldvDefaults());
        const double values[] = { defaults.maxCll, defaults.maxFall,
            defaults.masteringMinLuminance, defaults.masteringMaxLuminance };
        std::ostringstream formatted;
        formatted << values[index];
        return formatted.str();
    }

    std::string LldvInheritedValue(int index) const
    {
        const char* keys[] = { "max_cll", "max_fall", "mastering_min_luminance", "mastering_max_luminance" };
        if (!lldvSections.empty())
        {
            const std::string configured = document.Get(lldvSections.front().c_str(), keys[index]);
            if (!configured.empty()) return configured;
        }
        return LldvDefault(index);
    }

    static const char* QueueDefault(int index)
    {
        static const char* values[] = { "32", "1", "4" };
        return values[index];
    }

    std::string QueueInheritedValue(int index) const
    {
        const char* keys[] = { "queue_size", "lead_frames", "target_frames" };
        if (!queueSections.empty())
        {
            const std::string configured = document.Get(queueSections.front().c_str(), keys[index]);
            if (!configured.empty()) return configured;
        }
        return QueueDefault(index);
    }

    void PopulateSelectedProfile(int page, int index)
    {
        auto& sections = ProfileSections(page);
        if (index < 0 || static_cast<size_t>(index) >= sections.size()) return;
        ActiveProfile(page) = index;
        const std::string& section = sections[static_cast<size_t>(index)];
        const std::string prefix = ProfilePrefix(page);
        const bool root = section == prefix;
        const bool namedFallback = !root &&
            std::find(sections.begin(), sections.end(), prefix) == sections.end() && index == 0;
        for (HWND control : { profileNameLabel, profileNameEdit, profileKeyLabel,
            profileKey, profileKeyHelp, profileRuleLabel, profileRule,
            profileRuleHelp }) EnableWindow(control, TRUE);
        for (HWND control : controls)
            if (control != nullptr && GetParent(control) == cards[1]) EnableWindow(control, TRUE);
        for (HWND control : outputLabels) EnableWindow(control, TRUE);
        for (HWND control : outputUnits) EnableWindow(control, TRUE);
        for (HWND control : lldvLabels) EnableWindow(control, TRUE);
        for (HWND control : lldvFields) EnableWindow(control, TRUE);
        for (HWND control : lldvUnits) EnableWindow(control, TRUE);
        for (HWND control : { queueRecoveryTitle, queueRecoveryHelp }) EnableWindow(control, TRUE);
        for (HWND control : queueRecoveryLabels) EnableWindow(control, TRUE);
        for (HWND control : queueRecoveryFields) EnableWindow(control, TRUE);
        for (HWND control : queueRecoveryUnits) EnableWindow(control, TRUE);
        SetWindowTextW(cardTitles[1], page == 1 ? L"Queue profile" :
            (page == 2 ? L"Renderer profile" : L"LLDV metadata profile"));
        SetWindowTextW(profileNameEdit, ProfileName(page, section).c_str());
        EnableWindow(profileNameEdit, root ? FALSE : TRUE);
        SetWindowTextW(cardSubtitles[1], root ?
            L"Default root profile. Later profiles are checked in file order." :
            (namedFallback ? L"Default because it is first in file order." :
                L"Conditional profile. The first matching rule wins."));
        SetWindowTextW(profileRule, ToWide(document.Get(section.c_str(), "when")).c_str());
        SetProfileRuleVisible(!document.Get(section.c_str(), "when").empty());
        SetWindowTextW(profileKey, ToWide(document.Get(section.c_str(), "shortcut")).c_str());
		SetWindowTextW(profileRuleLabel, L"Use rule");
        SetWindowTextW(profileRuleHelp, root ?
			L"Optional manual-selection rule. Shortcut is simpler for a key." :
            L"Optional source rule. Shortcut is a separate alternative.");
        EnableWindow(removeProfileButton, root ? FALSE : TRUE);
        const int firstMovable = std::find(sections.begin(), sections.end(), prefix) == sections.end() ? 0 : 1;
        EnableWindow(moveProfileUpButton, !root && index > firstMovable ? TRUE : FALSE);
        EnableWindow(moveProfileDownButton, !root && static_cast<size_t>(index + 1) < sections.size() ? TRUE : FALSE);
        if (page == 1)
        {
            const char* queueKeys[] = { "queue_size", "lead_frames", "target_frames" };
            for (int field = 0; field < 3; ++field)
            {
                const std::string configured = document.Get(section.c_str(), queueKeys[field]);
                const bool defaultProfile = root || namedFallback;
                SetWindowTextW(controls[17 + field], ToWide(configured.empty() && defaultProfile ?
                    QueueDefault(field) : configured).c_str());
                const std::wstring cue = defaultProfile ? L"Use VP default" :
                    L"Inherited: " + ToWide(QueueInheritedValue(field));
                SendMessageW(controls[17 + field], EM_SETCUEBANNER, TRUE,
                    reinterpret_cast<LPARAM>(cue.c_str()));
            }
            EnableWindow(controls[18], root || namedFallback ? TRUE : FALSE);
            EnableWindow(outputLabels[4], root || namedFallback ? TRUE : FALSE);
            EnableWindow(outputUnits[1], root || namedFallback ? TRUE : FALSE);
            const std::string defaultSection = sections.empty() ? section : sections.front();
            const char* recoveryKeys[] = {
                "reset_after_render_restart_seconds", "reset_queue_too_large_percent" };
            const char* recoveryDefaults[] = { "5", "75" };
            for (int recovery = 0; recovery < 2; ++recovery)
            {
                const std::string configured = document.Get(defaultSection.c_str(), recoveryKeys[recovery]);
                SetWindowTextW(queueRecoveryFields[recovery],
                    ToWide(configured.empty() ? recoveryDefaults[recovery] : configured).c_str());
                EnableWindow(queueRecoveryFields[recovery], index == 0 ? TRUE : FALSE);
            }
            SetWindowTextW(queueRecoveryHelp, index == 0 ?
                L"Optional startup safeguards for the default Queue profile. Blank uses VP's built-in value." :
                L"Recovery safeguards belong to the first/default Queue profile. Select it to edit them.");
        }
        else if (page == 2)
        {
            const struct { int control; const char* key; const char* fallback; } fields[] = {
                { 5, "quality", "balanced" }, { 6, "output_presentation", "AUTO" },
                { 7, "output_range", "AUTO" }, { 8, "output_gamma", "AUTO" },
                { 9, "sdr_target_primaries", "REC709" }, { 10, "tone_mapping", "AUTO" },
                { 11, "gamut_mapping", "AUTO" }, { 12, "peak_detection", "AUTO" },
                { 13, "upscaler", "AUTO" }, { 14, "downscaler", "AUTO" },
                { 15, "deband", "AUTO" }, { 16, "dithering", "AUTO" } };
            for (const auto& field : fields)
                SetProfileSelectableValue(controls[field.control], document.Get(section.c_str(), field.key),
                    field.fallback, root);
        }
        else
        {
            const char* keys[] = { "max_cll", "max_fall", "mastering_min_luminance", "mastering_max_luminance" };
            const bool defaultProfile = root || namedFallback;
            for (int field = 0; field < 4; ++field)
            {
                const std::string configured = document.Get(section.c_str(), keys[field]);
                SetWindowTextW(lldvFields[field], ToWide(configured.empty() && defaultProfile ?
                    LldvDefault(field) : configured).c_str());
                const std::wstring cue = defaultProfile ? L"Use VP default" :
                    L"Inherited: " + ToWide(LldvInheritedValue(field));
                SendMessageW(lldvFields[field], EM_SETCUEBANNER, TRUE,
                    reinterpret_cast<LPARAM>(cue.c_str()));
            }
        }
    }

    bool CommitProfileName(int page)
    {
        auto& sections = ProfileSections(page);
        const int index = ActiveProfile(page);
        if (index < 0 || static_cast<size_t>(index) >= sections.size()) return false;
        const std::string current = sections[static_cast<size_t>(index)];
        const std::string prefix = ProfilePrefix(page);
        if (current == prefix) return true;
        const std::string requested = ConfigFile::Trim(GetControlText(profileNameEdit));
        if (requested.empty())
        {
            profileNameCommitFailed = true;
            SetWindowTextW(profileNameEdit, ProfileName(page, current).c_str());
            MessageBoxW(window, L"A profile name cannot be empty.",
                L"Profile name", MB_ICONWARNING);
            return false;
        }
        const std::string renamed = prefix + "." +
            ViewportIdentifierForLabel(ToWide(requested));
        if (renamed == current) return true;
        for (const std::string& existing : document.SectionNamesWithPrefix(prefix))
            if (existing == renamed)
            {
                profileNameCommitFailed = true;
                SetWindowTextW(profileNameEdit, ProfileName(page, current).c_str());
                MessageBoxW(window, L"Another profile already uses that name.",
                    L"Profile name", MB_ICONWARNING);
                return false;
            }
        if (!document.RenameSection(current, renamed)) return false;
        sections[static_cast<size_t>(index)] = renamed;
        SetDirty();
        SetWindowTextW(status, L"Profile name updated; settings, comments, and order were preserved.");
        return true;
    }

    void PopulateProfileList(int page, const std::string& selectSection = {})
    {
        RefreshProfileSections(page);
        auto& sections = ProfileSections(page);
        SendMessageW(profileSelector, LB_RESETCONTENT, 0, 0);
        const std::string prefix = ProfilePrefix(page);
        const bool hasRoot = std::find(sections.begin(), sections.end(), prefix) != sections.end();
        SetWindowTextW(profileListHint, hasRoot ?
            L"The root is default. Later profiles are checked in file order." :
            L"The first profile is default. Later profiles are checked in file order.");
        for (size_t index = 0; index < sections.size(); ++index)
        {
            const bool root = sections[index] == prefix;
            const std::wstring summary = root ? L"Default (root profile)" :
                (!hasRoot && index == 0 ? L"Default (first in file order)" : L"Conditional profile");
            const std::wstring item = ProfileName(page, sections[index]) + L"\r\n" + summary;
            SendMessageW(profileSelector, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
        }
        int selected = 0;
        if (!selectSection.empty())
            for (size_t index = 0; index < sections.size(); ++index)
                if (sections[index] == selectSection) { selected = static_cast<int>(index); break; }
        if (!sections.empty())
        {
            SendMessageW(profileSelector, LB_SETCURSEL, selected, 0);
            PopulateSelectedProfile(page, selected);
        }
        else
        {
            ActiveProfile(page) = -1;
            SetWindowTextW(cardTitles[1], page == 1 ? L"No queue profile" :
                (page == 2 ? L"No renderer profile" : L"No LLDV profile"));
            SetWindowTextW(cardSubtitles[1], L"Add a named profile to begin.");
            EnableWindow(renameProfileButton, FALSE);
            EnableWindow(removeProfileButton, FALSE);
            EnableWindow(moveProfileUpButton, FALSE);
            EnableWindow(moveProfileDownButton, FALSE);
            SetWindowTextW(profileNameEdit, L"");
            SetWindowTextW(profileKey, L"");
            SetWindowTextW(profileRule, L"");
            for (HWND control : { profileNameLabel, profileNameEdit,
                profileKeyLabel, profileKey, profileKeyHelp, profileRuleLabel,
                profileRule, profileRuleHelp }) EnableWindow(control, FALSE);
            for (HWND control : controls)
                if (control != nullptr && GetParent(control) == cards[1]) EnableWindow(control, FALSE);
            for (HWND control : outputLabels) EnableWindow(control, FALSE);
            for (HWND control : outputUnits) EnableWindow(control, FALSE);
            for (HWND control : lldvLabels) EnableWindow(control, FALSE);
            for (HWND control : lldvFields) { SetWindowTextW(control, L""); EnableWindow(control, FALSE); }
            for (HWND control : lldvUnits) EnableWindow(control, FALSE);
            for (HWND control : { queueRecoveryTitle, queueRecoveryHelp }) EnableWindow(control, FALSE);
            for (HWND control : queueRecoveryLabels) EnableWindow(control, FALSE);
            for (HWND control : queueRecoveryFields) { SetWindowTextW(control, L""); EnableWindow(control, FALSE); }
            for (HWND control : queueRecoveryUnits) EnableWindow(control, FALSE);
        }
    }

    void ApplyProfile(int page)
    {
        auto& sections = ProfileSections(page);
        const int index = ActiveProfile(page);
        if (index < 0 || static_cast<size_t>(index) >= sections.size()) return;
        if (!CommitProfileName(page)) return;
        const std::string section = sections[static_cast<size_t>(index)];
        const bool root = section == ProfilePrefix(page);
        const std::string rule = ProfileRuleVisible() ?
            ConfigFile::Trim(GetControlText(profileRule)) : std::string();
        const std::string key = ConfigFile::Trim(GetControlText(profileKey));
        if (rule.empty()) document.RemoveKnown(section, "when");
        else if (rule != document.Get(section.c_str(), "when")) document.SetKnown(section, "when", rule);
        if (key.empty()) document.RemoveKnown(section, "shortcut");
        else if (key != document.Get(section.c_str(), "shortcut")) document.SetKnown(section, "shortcut", key);
        const bool namedFallback = !root && index == 0 &&
            std::find(sections.begin(), sections.end(), ProfilePrefix(page)) == sections.end();
        if (page == 1)
        {
            const struct { int control; const char* key; } fields[] = {
                { 17, "queue_size" }, { 18, "lead_frames" }, { 19, "target_frames" } };
            for (const auto& field : fields)
            {
                if (field.control == 18 && !root && !namedFallback) continue;
                const std::string value = ConfigFile::Trim(GetControlText(controls[field.control]));
                const int queueIndex = field.control - 17;
                const std::string original = document.Get(section.c_str(), field.key);
                if ((root || namedFallback) && original.empty() && value == QueueDefault(queueIndex))
                    continue;
                if (value.empty()) document.RemoveKnown(section, field.key);
                else if (value != original) document.SetKnown(section, field.key, value);
            }
            if (index == 0)
            {
                const char* recoveryKeys[] = {
                    "reset_after_render_restart_seconds", "reset_queue_too_large_percent" };
                const char* recoveryDefaults[] = { "5", "75" };
                for (int recovery = 0; recovery < 2; ++recovery)
                {
                    const std::string value = ConfigFile::Trim(GetControlText(queueRecoveryFields[recovery]));
                    const std::string original = document.Get(section.c_str(), recoveryKeys[recovery]);
                    if (original.empty() && value == recoveryDefaults[recovery]) continue;
                    SetOptionalKnown(section, recoveryKeys[recovery], value);
                }
            }
        }
        else if (page == 2)
        {
            const struct { int control; const char* key; const char* fallback; } fields[] = {
                { 5, "quality", "balanced" }, { 6, "output_presentation", "AUTO" },
                { 7, "output_range", "AUTO" }, { 8, "output_gamma", "AUTO" },
                { 9, "sdr_target_primaries", "REC709" }, { 10, "tone_mapping", "AUTO" },
                { 11, "gamut_mapping", "AUTO" }, { 12, "peak_detection", "AUTO" },
                { 13, "upscaler", "AUTO" }, { 14, "downscaler", "AUTO" },
                { 15, "deband", "AUTO" }, { 16, "dithering", "AUTO" } };
            for (const auto& field : fields)
            {
                const std::string value = GetControlText(controls[field.control]);
                if (!root && ConfigFile::NormalizeName(value) == "inherited")
                    document.RemoveKnown(section, field.key);
                else
                {
                    const std::string original = document.Get(section.c_str(), field.key);
                    const std::string effective = root && original.empty() ? field.fallback : original;
                    if (ConfigFile::NormalizeName(value) != ConfigFile::NormalizeName(effective))
                        document.SetKnown(section, field.key, value);
                }
            }
        }
        else
        {
            const char* keys[] = { "max_cll", "max_fall", "mastering_min_luminance", "mastering_max_luminance" };
            for (int field = 0; field < 4; ++field)
            {
                const std::string value = ConfigFile::Trim(GetControlText(lldvFields[field]));
                const std::string original = document.Get(section.c_str(), keys[field]);
                if ((root || namedFallback) && original.empty() && value == LldvDefault(field))
                    continue;
                SetOptionalKnown(section, keys[field], value);
            }
        }
    }

    void SelectProfile()
    {
        const int selected = static_cast<int>(SendMessageW(profileSelector, LB_GETCURSEL, 0, 0));
        const auto& sections = ProfileSections(activePage);
        const std::string destination = selected >= 0 && static_cast<size_t>(selected) < sections.size() ?
            sections[static_cast<size_t>(selected)] : std::string();
        ApplyProfile(activePage);
        PopulateProfileList(activePage, destination);
        LayoutPageContent();
    }

    void AddProfile()
    {
        ApplyProfile(activePage);
        std::wstring requested;
        if (!PromptViewportName(window, requested, L"Add profile",
            L"Give this profile a readable name.", L"Create")) return;
        requested = ToWide(ConfigFile::Trim(ToNarrow(requested)));
        if (requested.empty()) return;
        const std::string section = std::string(ProfilePrefix(activePage)) + "." +
            UniqueProfileIdentifier(activePage, requested);
        if (!document.AddSection(section)) return;
        PopulateProfileList(activePage, section);
        SetDirty();
        SetWindowTextW(status, L"Profile added. Add an activation condition and the values it should override.");
    }

    void RenameProfile()
    {
        auto& sections = ProfileSections(activePage);
        const int index = ActiveProfile(activePage);
        if (index < 0 || static_cast<size_t>(index) >= sections.size()) return;
        const std::string current = sections[static_cast<size_t>(index)];
        if (current == ProfilePrefix(activePage)) return;
        ApplyProfile(activePage);
        std::wstring requested;
        if (!PromptViewportName(window, requested, L"Rename profile",
            L"Change the profile name used in the configuration section.", L"Rename",
            ProfileName(activePage, current))) return;
        requested = ToWide(ConfigFile::Trim(ToNarrow(requested)));
        if (requested.empty()) return;
        const std::string renamed = std::string(ProfilePrefix(activePage)) + "." +
            UniqueProfileIdentifier(activePage, requested);
        if (!document.RenameSection(current, renamed)) return;
        PopulateProfileList(activePage, renamed);
        SetDirty();
        SetWindowTextW(status, L"Profile renamed. Its settings and comments were preserved.");
    }

    void RemoveProfile()
    {
        auto& sections = ProfileSections(activePage);
        const int index = ActiveProfile(activePage);
        if (index < 0 || static_cast<size_t>(index) >= sections.size()) return;
        const std::string section = sections[static_cast<size_t>(index)];
        if (section == ProfilePrefix(activePage)) return;
        const std::wstring prompt = L"Remove profile '" + ProfileName(activePage, section) +
            L"'? Save will create a timestamped backup before this takes effect.";
        if (MessageBoxW(window, prompt.c_str(), L"Remove profile",
            MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) return;
        if (activePage == 1 && index == 0 && sections.size() > 1)
            TransferQueueRecovery(section, sections[1]);
        document.RemoveSection(section);
        PopulateProfileList(activePage);
        SetDirty();
        SetWindowTextW(status, L"Profile removed from the pending configuration.");
    }

    void TransferQueueRecovery(const std::string& from, const std::string& to)
    {
        if (from.empty() || to.empty() || from == to) return;
        for (const char* key : { "reset_after_render_restart_seconds",
            "reset_queue_too_large_percent" })
        {
            const std::string value = document.Get(from.c_str(), key);
            if (value.empty()) continue;
            document.RemoveKnown(from, key);
            SetOptionalKnown(to, key, value);
        }
    }

    void MoveSelectedProfileBy(int delta)
    {
        auto& sections = ProfileSections(activePage);
        const int source = ActiveProfile(activePage);
        if (source < 0 || static_cast<size_t>(source) >= sections.size()) return;
        const std::string prefix = ProfilePrefix(activePage);
        if (sections[static_cast<size_t>(source)] == prefix) return;
        const int first = std::find(sections.begin(), sections.end(), prefix) == sections.end() ? 0 : 1;
        const int destination = std::max(first, std::min(source + delta, static_cast<int>(sections.size()) - 1));
        if (destination == source) return;
        ApplyProfile(activePage);
        const std::string moved = sections[static_cast<size_t>(source)];
        std::vector<std::string> remaining = sections;
        remaining.erase(remaining.begin() + source);
        const bool ok = destination >= static_cast<int>(remaining.size()) ?
            document.MoveSectionAfter(moved, remaining.back()) :
            document.MoveSectionBefore(moved, remaining[static_cast<size_t>(destination)]);
        if (!ok) return;
        if (activePage == 1)
        {
            const std::string previousDefault = sections.front();
            RefreshProfileSections(1);
            if (!queueSections.empty())
                TransferQueueRecovery(previousDefault, queueSections.front());
        }
        PopulateProfileList(activePage, moved);
        SetDirty();
        SetWindowTextW(status, L"Profile order updated.");
    }

    static std::string GetControlText(HWND control)
    {
        const int length = GetWindowTextLengthW(control);
        std::wstring text(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(control, &text[0], length + 1);
        text.resize(static_cast<size_t>(length));
        return ToNarrow(text);
    }

    static void SetSelectableValue(HWND control, const std::string& value,
        const std::string& fallback)
    {
        const std::wstring displayed = ToWide(value.empty() ? fallback : value);
        LRESULT index = SendMessageW(control, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
            reinterpret_cast<LPARAM>(displayed.c_str()));
        if (index == CB_ERR)
            index = SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(displayed.c_str()));
        SendMessageW(control, CB_SETCURSEL, index, 0);
    }

    void ConfigureOutputComboBox(HWND control) const
    {
        // The layout supplies the native combo's full hidden popup height.
        // These metrics keep the visible selection face compact and give the
        // popup readable rows and enough width for long renderer names.
        const int itemHeight = Px(26);
        SendMessageW(control, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), itemHeight);
        SendMessageW(control, CB_SETITEMHEIGHT, 0, itemHeight);
        SendMessageW(control, CB_SETMINVISIBLE, 8, 0);
        SendMessageW(control, CB_SETDROPPEDWIDTH, Px(260), 0);
        COMBOBOXINFO combo = { sizeof(combo) };
        if (GetComboBoxInfo(control, &combo) && combo.hwndList != nullptr)
        {
            SendMessageW(combo.hwndList, WM_SETFONT,
                reinterpret_cast<WPARAM>(bodyFont), TRUE);
            SetWindowTheme(combo.hwndList, L"Explorer", nullptr);
        }
    }

    void SetViewportValue(HWND control, const std::string& section, const char* key,
        const std::string& fallback)
    {
        const std::string value = document.Get(section.c_str(), key);
        SetWindowTextW(control, ToWide(value.empty() ? fallback : value).c_str());
    }

    void SetViewportCheck(HWND control, const std::string& section, const char* key)
    {
        SendMessageW(control, BM_SETCHECK,
            ReadBoolean(document.Get(section.c_str(), key)) ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    void PopulateViewportList()
    {
        activeViewport = -1;
        viewportSections = document.SectionNamesWithPrefix("vprenderer.viewport");
        const auto root = std::find(viewportSections.begin(), viewportSections.end(), "vprenderer.viewport");
        const bool hasLegacyRoot = root != viewportSections.end();
        if (root != viewportSections.end() && root != viewportSections.begin())
        {
            const std::string section = *root;
            viewportSections.erase(root);
            viewportSections.insert(viewportSections.begin(), section);
        }
        SetWindowTextW(viewportListHint, hasLegacyRoot ?
            L"Name the legacy base before reordering profiles." :
            L"Drag to reorder. The first profile is the default.");
        if (activePage == 3)
            SetWindowTextW(pageDescription, hasLegacyRoot ?
                L"The legacy base is the default until you name it; named rules can override it without changing manual settings." :
                L"The first profile is the default; rules and shortcuts can select another profile.");
        SendMessageW(viewportSelector, LB_RESETCONTENT, 0, 0);
        for (size_t index = 0; index < viewportSections.size(); ++index)
        {
            const std::string& section = viewportSections[index];
            const bool legacy = section == "vprenderer.viewport";
            const std::wstring name = legacy ?
                L"Unnamed profile (legacy)\r\nName it before reordering" :
                ViewportName(section) + (index == 0 ?
                    L"\r\nDefault (first in file order)" : L"\r\nConditional profile");
            SendMessageW(viewportSelector, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        if (!viewportSections.empty())
        {
            SendMessageW(viewportSelector, LB_SETCURSEL, 0, 0);
            PopulateViewport(0);
        }
        else
        {
            SetWindowTextW(viewportRule, L"No [vprenderer.viewport] section is configured.");
            SetWindowTextW(viewportDetailTitle, L"No viewport configured");
            SetWindowTextW(viewportDetailSubtitle, L"Add a viewport to begin configuring output geometry and subtitles.");
            SetWindowTextW(viewportNameEdit, L"");
            EnableWindow(viewportNameEdit, FALSE);
            ShowWindow(nameViewportButton, SW_HIDE);
            EnableWindow(removeViewportButton, FALSE);
            EnableWindow(nameViewportButton, FALSE);
            EnableWindow(moveViewportUpButton, FALSE);
            EnableWindow(moveViewportDownButton, FALSE);
        }
    }

    void PopulateViewport(int index)
    {
        if (index < 0 || static_cast<size_t>(index) >= viewportSections.size()) return;
        activeViewport = index;
        const std::string& section = viewportSections[static_cast<size_t>(index)];
        const bool hasRoot = std::find(viewportSections.begin(), viewportSections.end(), "vprenderer.viewport") != viewportSections.end();
        const bool legacy = section == "vprenderer.viewport";
        const bool isDefault = legacy || (!hasRoot && index == 0);
        SetWindowTextW(viewportDetailTitle, legacy ? L"Unnamed profile (legacy)" :
            ViewportName(section).c_str());
        SetWindowTextW(viewportNameEdit, legacy ? L"" : ViewportName(section).c_str());
        EnableWindow(viewportNameEdit, TRUE);
        ShowWindow(nameViewportButton, legacy ? SW_SHOW : SW_HIDE);
        SetWindowTextW(viewportDetailSubtitle, legacy ?
            L"Legacy default. Name it to migrate safely to ordered profiles." :
            (isDefault ? L"Default because it is first in file order." :
                L"Used when its rule or shortcut matches."));
        EnableWindow(nameViewportButton, legacy ? TRUE : FALSE);
        EnableWindow(removeViewportButton, isDefault ? FALSE : TRUE);
        const bool canReorder = CanReorderViewports();
        EnableWindow(moveViewportUpButton, canReorder && index > 0 ? TRUE : FALSE);
        EnableWindow(moveViewportDownButton,
            canReorder && static_cast<size_t>(index + 1) < viewportSections.size() ? TRUE : FALSE);
        SetViewportValue(viewportFields[0], section, "screen_aspect", "16:9");
        const std::string anamorphic = document.Get(section.c_str(), "anamorphic_scale");
        SendMessageW(viewportFields[1], BM_SETCHECK, anamorphic.empty() ? BST_UNCHECKED : BST_CHECKED, 0);
        SetWindowTextW(viewportFields[2], ToWide(anamorphic.empty() ? "1:1" : anamorphic).c_str());
        EnableWindow(viewportFields[2], anamorphic.empty() ? FALSE : TRUE);
        SetViewportCheck(viewportFields[3], section, "automatic_crop");
        SetViewportCheck(viewportFields[4], section, "subtitle_fit");
        SetViewportValue(viewportFields[5], section, "subtitle_hold_seconds", "2");
        SetViewportValue(viewportFields[6], section, "subtitle_release_drift_seconds", "0");
        SetViewportValue(viewportFields[7], section, "subtitle_padding_pixels", "20");
        SetWindowTextW(viewportRule, ToWide(document.Get(section.c_str(), "when")).c_str());
        SetViewportRuleVisible(!document.Get(section.c_str(), "when").empty());
        SetWindowTextW(viewportKey, ToWide(document.Get(section.c_str(), "shortcut")).c_str());
    }

    void ApplyViewport()
    {
        if (activeViewport < 0 || static_cast<size_t>(activeViewport) >= viewportSections.size()) return;
        const std::string& section = viewportSections[static_cast<size_t>(activeViewport)];
        if (section != "vprenderer.viewport")
        {
            const std::string name = ConfigFile::Trim(GetControlText(viewportNameEdit));
            if (name != document.Get(section.c_str(), "label"))
                document.SetKnown(section, "label", name);
        }
        const struct { int control; const char* key; const char* fallback; } textFields[] = {
            { 0, "screen_aspect", "16:9" }, { 5, "subtitle_hold_seconds", "2" },
            { 6, "subtitle_release_drift_seconds", "0" }, { 7, "subtitle_padding_pixels", "20" } };
        for (const auto& field : textFields)
        {
            const std::string original = document.Get(section.c_str(), field.key);
            const std::string value = GetControlText(viewportFields[field.control]);
            if (value != (original.empty() ? field.fallback : original))
                document.SetKnown(section, field.key, value);
        }
        const bool anamorphicEnabled = SendMessageW(viewportFields[1], BM_GETCHECK, 0, 0) == BST_CHECKED;
        const std::string originalAnamorphic = document.Get(section.c_str(), "anamorphic_scale");
        if (anamorphicEnabled)
        {
            const std::string value = GetControlText(viewportFields[2]);
            if (value != (originalAnamorphic.empty() ? "1:1" : originalAnamorphic))
                document.SetKnown(section, "anamorphic_scale", value);
            else if (originalAnamorphic.empty())
                document.SetKnown(section, "anamorphic_scale", value);
        }
        else if (!originalAnamorphic.empty())
            document.RemoveKnown(section, "anamorphic_scale");
        const struct { int control; const char* key; } checkFields[] = {
            { 3, "automatic_crop" }, { 4, "subtitle_fit" } };
        for (const auto& field : checkFields)
        {
            const bool original = ReadBoolean(document.Get(section.c_str(), field.key));
            const bool value = SendMessageW(viewportFields[field.control], BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (value != original) document.SetKnown(section, field.key, value ? "true" : "false");
        }
        const std::string rule = ViewportRuleVisible() ?
            ConfigFile::Trim(GetControlText(viewportRule)) : std::string();
        const std::string key = ConfigFile::Trim(GetControlText(viewportKey));
        if (rule.empty()) document.RemoveKnown(section, "when");
        else if (rule != document.Get(section.c_str(), "when")) document.SetKnown(section, "when", rule);
        if (key.empty()) document.RemoveKnown(section, "shortcut");
        else if (key != document.Get(section.c_str(), "shortcut")) document.SetKnown(section, "shortcut", key);
    }

    void SelectViewport()
    {
        const int selected = static_cast<int>(SendMessageW(viewportSelector, LB_GETCURSEL, 0, 0));
        const std::string destination = selected >= 0 && static_cast<size_t>(selected) < viewportSections.size() ?
            viewportSections[static_cast<size_t>(selected)] : std::string();
        ApplyViewport();
        PopulateViewportList();
        for (size_t index = 0; index < viewportSections.size(); ++index)
            if (viewportSections[index] == destination)
            {
                SendMessageW(viewportSelector, LB_SETCURSEL, index, 0);
                PopulateViewport(static_cast<int>(index));
                break;
            }
        LayoutPageContent();
    }

    bool CanReorderViewports() const
    {
        return std::find(viewportSections.begin(), viewportSections.end(), "vprenderer.viewport") == viewportSections.end();
    }

    void BeginViewportDrag(POINT point)
    {
        if (!CanReorderViewports())
        {
            SetWindowTextW(status, L"Name the legacy viewport first; then drag named viewports to set their file order.");
            return;
        }
        const LRESULT hit = SendMessageW(viewportSelector, LB_ITEMFROMPOINT, 0,
            MAKELPARAM(point.x, point.y));
        if (HIWORD(hit) != 0 || LOWORD(hit) >= viewportSections.size()) return;
        viewportDragSource = static_cast<int>(LOWORD(hit));
        viewportDragInsertion = viewportDragSource;
        viewportDragStart = point;
        viewportDragPoint = point;
        viewportDragging = false;
        SetCapture(viewportSelector);
        SetTimer(viewportSelector, kViewportDragTimer, 80, nullptr);
    }

    void ScrollViewportListForDrag()
    {
        if (GetCapture() != viewportSelector || !viewportDragging) return;
        RECT client = {};
        GetClientRect(viewportSelector, &client);
        const int edge = Px(24);
        const int top = static_cast<int>(SendMessageW(viewportSelector, LB_GETTOPINDEX, 0, 0));
        const int itemHeight = std::max(1, static_cast<int>(SendMessageW(viewportSelector, LB_GETITEMHEIGHT, 0, 0)));
        const int visible = std::max(1, static_cast<int>(client.bottom - client.top) / itemHeight);
        int wantedTop = top;
        if (viewportDragPoint.y < edge) wantedTop = std::max(0, top - 1);
        else if (viewportDragPoint.y > client.bottom - edge)
            wantedTop = std::min(std::max(0, static_cast<int>(viewportSections.size()) - visible), top + 1);
        if (wantedTop != top) SendMessageW(viewportSelector, LB_SETTOPINDEX, wantedTop, 0);
    }

    void UpdateViewportDrag(POINT point)
    {
        if (GetCapture() != viewportSelector || viewportDragSource < 0) return;
        viewportDragPoint = point;
        const int threshold = Px(4);
        if (!viewportDragging &&
            std::abs(point.x - viewportDragStart.x) < threshold &&
            std::abs(point.y - viewportDragStart.y) < threshold) return;
        viewportDragging = true;
        ScrollViewportListForDrag();
        const int itemHeight = std::max(1, static_cast<int>(SendMessageW(viewportSelector, LB_GETITEMHEIGHT, 0, 0)));
        const int top = static_cast<int>(SendMessageW(viewportSelector, LB_GETTOPINDEX, 0, 0));
        int insertion = top + point.y / itemHeight;
        if (point.y % itemHeight > itemHeight / 2) ++insertion;
        insertion = std::max(0, std::min(insertion, static_cast<int>(viewportSections.size())));
        if (insertion != viewportDragInsertion)
        {
            viewportDragInsertion = insertion;
            InvalidateRect(viewportSelector, nullptr, FALSE);
        }
    }

    void CancelViewportDrag()
    {
        KillTimer(viewportSelector, kViewportDragTimer);
        viewportDragSource = -1;
        viewportDragInsertion = -1;
        viewportDragging = false;
        if (GetCapture() == viewportSelector) ReleaseCapture();
        InvalidateRect(viewportSelector, nullptr, FALSE);
    }

    void MoveViewportToIndex(int source, int destination)
    {
        if (!CanReorderViewports() || source < 0 || source >= static_cast<int>(viewportSections.size())) return;
        destination = std::max(0, std::min(destination, static_cast<int>(viewportSections.size()) - 1));
        if (destination == source) return;
        ApplyViewport();
        const std::string moved = viewportSections[static_cast<size_t>(source)];
        std::vector<std::string> remaining = viewportSections;
        remaining.erase(remaining.begin() + source);
        const bool movedSuccessfully = destination >= static_cast<int>(remaining.size()) ?
            (!remaining.empty() && document.MoveSectionAfter(moved, remaining.back())) :
            document.MoveSectionBefore(moved, remaining[static_cast<size_t>(destination)]);
        if (!movedSuccessfully)
        {
            MessageBoxW(window, L"The viewport order could not be updated. The pending configuration was not changed.",
                L"Reorder viewports", MB_ICONERROR);
            return;
        }
        PopulateViewportList();
        SendMessageW(viewportSelector, LB_SETCURSEL, destination, 0);
        PopulateViewport(destination);
        SetDirty();
        SetWindowTextW(status, L"Viewport order updated. The first named viewport is now the fallback.");
    }

    void MoveSelectedViewportBy(int delta)
    {
        const int selected = static_cast<int>(SendMessageW(viewportSelector, LB_GETCURSEL, 0, 0));
        if (!CanReorderViewports())
        {
            SetWindowTextW(status, L"Name the legacy viewport first; then reorder named viewports with drag or Ctrl+Up/Down.");
            return;
        }
        MoveViewportToIndex(selected, selected + delta);
    }

    void EndViewportDrag()
    {
        const int source = viewportDragSource;
        const int insertion = viewportDragInsertion;
        const bool dragging = viewportDragging;
        KillTimer(viewportSelector, kViewportDragTimer);
        if (GetCapture() == viewportSelector) ReleaseCapture();
        viewportDragSource = -1;
        viewportDragInsertion = -1;
        viewportDragging = false;
        InvalidateRect(viewportSelector, nullptr, FALSE);
        if (!dragging || source < 0 || insertion < 0 || source >= static_cast<int>(viewportSections.size())) return;
        int destination = insertion;
        if (source < destination) --destination;
        if (destination == source) return;
        MoveViewportToIndex(source, destination);
    }

    void AddViewport()
    {
        ApplyViewport();
        std::wstring requestedName;
        if (!PromptViewportName(window, requestedName, L"Add viewport",
            L"Give this viewport a display name.", L"Create")) return;
        requestedName = ToWide(ConfigFile::Trim(ToNarrow(requestedName)));
        if (requestedName.empty())
        {
            MessageBoxW(window, L"Enter a display name for the new viewport.",
                L"Add viewport", MB_ICONWARNING);
            return;
        }
        if (VisibleViewportNameInUse(requestedName))
        {
            MessageBoxW(window, L"Another viewport profile already uses that name. Choose a unique name.",
                L"Add viewport", MB_ICONWARNING);
            return;
        }
        const std::string name = UniqueViewportIdentifier(requestedName);
        const std::string section = "vprenderer.viewport." + name;
        if (!document.AddSection(section))
        {
            MessageBoxW(window, L"That viewport name already exists.", L"Add viewport", MB_ICONWARNING);
            return;
        }
        document.SetKnown(section, "label", ToNarrow(requestedName));
        document.SetKnown(section, "screen_aspect", "16:9");
        document.SetKnown(section, "subtitle_hold_seconds", "2");
        document.SetKnown(section, "subtitle_release_drift_seconds", "0");
        document.SetKnown(section, "subtitle_padding_pixels", "20");
        PopulateViewportList();
        for (size_t index = 0; index < viewportSections.size(); ++index)
            if (viewportSections[index] == section)
            {
                SendMessageW(viewportSelector, LB_SETCURSEL, index, 0);
                PopulateViewport(static_cast<int>(index));
                break;
            }
        SetDirty();
        SetWindowTextW(status, L"New viewport added. Set its activation rule, then Save changes to create the backup and apply it.");
    }

    void NameViewport()
    {
        if (activeViewport < 0 || static_cast<size_t>(activeViewport) >= viewportSections.size()) return;
        ApplyViewport();
        const std::string section = viewportSections[static_cast<size_t>(activeViewport)];
        const bool legacy = section == "vprenderer.viewport";
        std::wstring requestedName;
        if (!PromptViewportName(window, requestedName,
            legacy ? L"Name legacy viewport" : L"Rename viewport",
            legacy ? L"Name this legacy fallback viewport to migrate it to the ordered format." :
                L"Change the display name shown in this editor.",
            legacy ? L"Migrate" : L"Save name",
            legacy ? L"" : ViewportName(section))) return;
        requestedName = ToWide(ConfigFile::Trim(ToNarrow(requestedName)));
        if (requestedName.empty())
        {
            MessageBoxW(window, L"Enter a display name for this viewport.",
                legacy ? L"Name legacy viewport" : L"Rename viewport", MB_ICONWARNING);
            return;
        }
        if (!legacy)
        {
            document.SetKnown(section, "label", ToNarrow(requestedName));
            PopulateViewportList();
            for (size_t index = 0; index < viewportSections.size(); ++index)
                if (viewportSections[index] == section)
                {
                    SendMessageW(viewportSelector, LB_SETCURSEL, index, 0);
                    PopulateViewport(static_cast<int>(index));
                    break;
                }
            SetDirty();
            SetWindowTextW(status, L"Viewport display name updated. Save changes to write the label.");
            return;
        }

        const std::wstring confirmation = L"Migrate the legacy base to a named viewport?\n\n"
            L"The current section, comments, and manual settings are preserved. "
            L"It becomes the first named viewport, so it remains the default fallback. "
            L"If the legacy base has a when rule, VP keeps it as an explicit rule for the new default instead of a legacy reset rule.";
        if (MessageBoxW(window, confirmation.c_str(), L"Migrate legacy viewport",
            MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) return;
        const std::string migrated = "vprenderer.viewport." + UniqueViewportIdentifier(requestedName);
        std::string firstNamed;
        const std::string previousLabel = document.Get(section.c_str(), "label");
        for (const std::string& candidate : viewportSections)
            if (candidate != "vprenderer.viewport") { firstNamed = candidate; break; }
        if (!document.RenameSection(section, migrated))
        {
            MessageBoxW(window, L"The legacy viewport could not be renamed. The configuration was not changed.",
                L"Migrate legacy viewport", MB_ICONERROR);
            return;
        }
        document.SetKnown(migrated, "label", ToNarrow(requestedName));
        if (!firstNamed.empty() && !document.MoveSectionBefore(migrated, firstNamed))
        {
            document.RenameSection(migrated, section);
            if (previousLabel.empty()) document.RemoveKnown(section, "label");
            else document.SetKnown(section, "label", previousLabel);
            MessageBoxW(window, L"The legacy viewport could not be moved into its default position. The pending configuration was restored.",
                L"Migrate legacy viewport", MB_ICONERROR);
            return;
        }
        PopulateViewportList();
        for (size_t index = 0; index < viewportSections.size(); ++index)
            if (viewportSections[index] == migrated)
            {
                SendMessageW(viewportSelector, LB_SETCURSEL, index, 0);
                PopulateViewport(static_cast<int>(index));
                break;
            }
        SetDirty();
        SetWindowTextW(status, L"Legacy viewport migrated. It is first in file order and remains the default fallback.");
    }

    void RemoveViewport()
    {
        if (activeViewport < 0 || static_cast<size_t>(activeViewport) >= viewportSections.size()) return;
        const std::string& section = viewportSections[static_cast<size_t>(activeViewport)];
        if (activeViewport == 0)
        {
            MessageBoxW(window, L"The first configured viewport is the fallback and cannot be removed. Drag another named viewport above it first.",
                L"Remove viewport", MB_ICONINFORMATION);
            return;
        }
        const std::wstring prompt = L"Remove viewport '" + ViewportName(section) +
            L"'? This takes effect only after Save; the Save operation creates a timestamped backup.";
        if (MessageBoxW(window, prompt.c_str(), L"Remove viewport", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) return;
        document.RemoveSection(section);
        PopulateViewportList();
        SetDirty();
        SetWindowTextW(status, L"Viewport removed in the pending configuration. Save changes to apply it.");
    }

    void UpdateOutputControlVisibility()
    {
        const bool queuePage = activePage == 1;
        const bool rendererPage = activePage == 2;
        const bool lldvPage = activePage == 4;
        for (int index = 5; index <= 7; ++index) ShowWindow(controls[index], rendererPage ? SW_SHOW : SW_HIDE);
        for (int index = 8; index <= 16; ++index)
            ShowWindow(controls[index], rendererPage ? SW_SHOW : SW_HIDE);
        for (int index = 17; index <= 19; ++index) ShowWindow(controls[index], queuePage ? SW_SHOW : SW_HIDE);
        for (int index = 0; index <= 2; ++index) ShowWindow(outputLabels[index], rendererPage ? SW_SHOW : SW_HIDE);
        for (int index = 3; index <= 5; ++index) ShowWindow(outputLabels[index], queuePage ? SW_SHOW : SW_HIDE);
        for (int index = 6; index <= 14; ++index)
            ShowWindow(outputLabels[index], rendererPage ? SW_SHOW : SW_HIDE);
        for (HWND unit : outputUnits) ShowWindow(unit, queuePage ? SW_SHOW : SW_HIDE);
        ShowWindow(queueRecoveryTitle, queuePage ? SW_SHOW : SW_HIDE);
        ShowWindow(queueRecoveryHelp, queuePage ? SW_SHOW : SW_HIDE);
        for (HWND label : queueRecoveryLabels) ShowWindow(label, queuePage ? SW_SHOW : SW_HIDE);
        for (HWND field : queueRecoveryFields) ShowWindow(field, queuePage ? SW_SHOW : SW_HIDE);
        for (HWND unit : queueRecoveryUnits) ShowWindow(unit, queuePage ? SW_SHOW : SW_HIDE);
        for (HWND label : lldvLabels) ShowWindow(label, lldvPage ? SW_SHOW : SW_HIDE);
        for (HWND field : lldvFields) ShowWindow(field, lldvPage ? SW_SHOW : SW_HIDE);
        for (HWND unit : lldvUnits) ShowWindow(unit, lldvPage ? SW_SHOW : SW_HIDE);
    }

    void ShowPage(int page)
    {
        if (activePage == 1 || activePage == 2 || activePage == 4) ApplyProfile(activePage);
        activePage = std::max(0, std::min(page, 5));
        ShowWindow(cards[0], activePage == 0 ? SW_SHOW : SW_HIDE);
        ShowWindow(cards[1], activePage == 1 || activePage == 2 || activePage == 4 ? SW_SHOW : SW_HIDE);
        ShowWindow(cards[2], activePage == 3 ? SW_SHOW : SW_HIDE);
        ShowWindow(cards[3], activePage == 3 ? SW_SHOW : SW_HIDE);
        ShowWindow(cards[4], activePage == 1 || activePage == 2 || activePage == 4 ? SW_SHOW : SW_HIDE);
        ShowWindow(cards[5], SW_HIDE);
        ShowWindow(cards[6], activePage == 5 ? SW_SHOW : SW_HIDE);
        ShowWindow(cards[7], activePage == 0 ? SW_SHOW : SW_HIDE);
        ShowWindow(cards[8], activePage == 0 ? SW_SHOW : SW_HIDE);
        UpdateOutputControlVisibility();
        for (HWND item : navigation) InvalidateRect(item, nullptr, TRUE);
        if (activePage == 0)
        {
            SetWindowTextW(pageTitle, L"Startup");
            SetWindowTextW(pageDescription, L"Choose how VideoProcessor opens and prepares a source.");
        }
        else if (activePage == 1)
        {
            SetWindowTextW(pageTitle, L"Queue");
            SetWindowTextW(pageDescription, L"Configure ordered queue profiles. The first profile is the default.");
            const std::string selected = activeQueueProfile >= 0 &&
                static_cast<size_t>(activeQueueProfile) < queueSections.size() ?
                queueSections[static_cast<size_t>(activeQueueProfile)] : std::string();
            PopulateProfileList(1, selected);
        }
        else if (activePage == 2)
        {
            SetWindowTextW(pageTitle, L"VP Renderer");
            SetWindowTextW(pageDescription, L"Configure ordered renderer profiles. The first profile is the default.");
            const std::string selected = activeRendererProfile >= 0 &&
                static_cast<size_t>(activeRendererProfile) < rendererSections.size() ?
                rendererSections[static_cast<size_t>(activeRendererProfile)] : std::string();
            PopulateProfileList(2, selected);
        }
        else if (activePage == 3)
        {
            SetWindowTextW(pageTitle, L"Viewports");
            const bool hasLegacyRoot = std::find(viewportSections.begin(), viewportSections.end(),
                "vprenderer.viewport") != viewportSections.end();
            SetWindowTextW(pageDescription, hasLegacyRoot ?
                L"The legacy base is the default until you name it; named rules can override it without changing manual settings." :
                L"The first profile is the default; rules and shortcuts can select another profile.");
        }
        else if (activePage == 4)
        {
            SetWindowTextW(pageTitle, L"LLDV metadata");
            SetWindowTextW(pageDescription,
                L"Configure ordered LLDV metadata profiles. The first profile is the default.");
            const std::string selected = activeLldvProfile >= 0 &&
                static_cast<size_t>(activeLldvProfile) < lldvSections.size() ?
                lldvSections[static_cast<size_t>(activeLldvProfile)] : std::string();
            PopulateProfileList(4, selected);
        }
        else
        {
            SetWindowTextW(pageTitle, L"Shortcuts");
            SetWindowTextW(pageDescription,
                L"Configure fixed VideoProcessor commands. Profile and shader shortcuts remain with their settings.");
        }
        contentScroll = 0;
        LayoutPageContent();
    }

    void Populate()
    {
        const auto populateDiscovered = [](HWND control,
            const std::vector<std::wstring>& values)
        {
            SendMessageW(control, CB_RESETCONTENT, 0, 0);
            for (const std::wstring& value : values)
                SendMessageW(control, CB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>(value.c_str()));
        };
        const std::vector<std::wstring> captureDevices =
            ConfigurationDiscovery::CaptureDeviceNames();
        populateDiscovered(startupFields[0], captureDevices);
        startupEffectiveDefaults[0] = captureDevices.empty() ? std::string() :
            ToNarrow(captureDevices.front());
        const std::string hideLegacyRaw = document.Get("general", "hide_legacy_renderers");
        const std::vector<std::wstring> renderers =
            ConfigurationDiscovery::RendererNames(hideLegacyRaw.empty() ||
                ReadBoolean(hideLegacyRaw));
        populateDiscovered(startupFields[1], renderers);
        startupEffectiveDefaults[1] = renderers.empty() ? std::string() :
            ToNarrow(renderers.front());
        populateDiscovered(startupFields[2],
            ConfigurationDiscovery::ActiveMonitorNames());
        const struct { int control; const char* section; const char* key; } fields[] = {
            { 0, "general", "fullscreen" }, { 1, "general", "windowed_fullscreen_mode" },
            { 2, "general", "scene_detect" }, { 3, "general", "hide_legacy_renderers" },
            { 4, "general", "startminimized" },
            { 20, "general", "disable_detection_features" },
            { 21, "general", "scene_correction_basic" },
            { 22, "general", "newlldv" } };
        for (const auto& field : fields)
        {
            std::string raw = document.Get(field.section, field.key);
            if (field.control == 22 && raw.empty())
                raw = document.Get("general", "new_lldv");
            const bool value = raw.empty() ? field.control == 3 : ReadBoolean(raw);
            SendMessageW(controls[field.control], BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        const char* startupKeys[] = { "capture_device", "renderer", "fullscreen_monitor_name",
            "fullscreen_monitor_session_mode", "hdr_colorspace", "hdr_luminance" };
        for (int index = 0; index < 6; ++index)
        {
            const std::string value = document.Get("general", startupKeys[index]);
            const std::wstring displayed = StartupDisplayValue(index,
                value.empty() && index < 2 ? startupEffectiveDefaults[index] : value);
            if (index >= 3) SetSelectableValue(startupFields[index], ToNarrow(displayed), ToNarrow(displayed));
            else
            {
                if (displayed.empty()) SendMessageW(startupFields[index],
                    CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
                SetWindowTextW(startupFields[index], displayed.c_str());
            }
        }
        UpdateStartupControlState();
        PopulateProfileList(1);
        PopulateProfileList(2);
        PopulateProfileList(4);
        PopulateViewportList();
        PopulateStandaloneSettings();
        SetWindowTextW(status, L"Loaded safely. Save makes a backup; restart VP for startup settings.");
    }

    void ApplyControls()
    {
        const struct { int control; const char* section; const char* key; bool defaultValue; } booleans[] = {
            { 0, "general", "fullscreen", false }, { 1, "general", "windowed_fullscreen_mode", false },
            { 2, "general", "scene_detect", false }, { 3, "general", "hide_legacy_renderers", true },
            { 4, "general", "startminimized", false },
            { 20, "general", "disable_detection_features", false },
            { 21, "general", "scene_correction_basic", false },
            { 22, "general", "newlldv", false } };
        for (const auto& field : booleans)
        {
            const bool current = SendMessageW(controls[field.control], BM_GETCHECK, 0, 0) == BST_CHECKED;
            std::string original = document.Get(field.section, field.key);
            if (field.control == 22 && original.empty())
                original = document.Get("general", "new_lldv");
            if (original.empty() && current == field.defaultValue) continue;
            if (current != ReadBoolean(original))
                document.SetKnown(field.section, field.key, current ? "true" : "false");
        }
        const char* startupKeys[] = { "capture_device", "renderer", "fullscreen_monitor_name",
            "fullscreen_monitor_session_mode", "hdr_colorspace", "hdr_luminance" };
        const char* startupDefaults[] = { "", "", "", "existing", "FOLLOW_INPUT", "FOLLOW_INPUT" };
        for (int index = 0; index < 6; ++index)
        {
            const std::string value = StartupStoredValue(index,
                ConfigFile::Trim(GetControlText(startupFields[index])));
            const std::string original = document.Get("general", startupKeys[index]);
            if (original.empty() && index < 2 &&
                value == startupEffectiveDefaults[index]) continue;
            if (original.empty() && value == startupDefaults[index]) continue;
            SetOptionalKnown("general", startupKeys[index], value);
        }
        if (activePage == 1 || activePage == 2 || activePage == 4) ApplyProfile(activePage);
        ApplyViewport();
        ApplyStandaloneSettings();
    }

    bool FocusKnownValidationError(const std::wstring& error)
    {
        const std::string text = ToNarrow(error);
        const size_t keyStart = text.find("key '");
        if (keyStart == std::string::npos) return false;
        const size_t nameStart = keyStart + 5;
        const size_t nameEnd = text.find('\'', nameStart);
        if (nameEnd == std::string::npos) return false;
        const std::string key = text.substr(nameStart, nameEnd - nameStart);
        const size_t sectionStart = text.find('[');
        const size_t sectionEnd = text.find(']', sectionStart);
        const std::string section = sectionStart != std::string::npos && sectionEnd != std::string::npos ?
            ConfigFile::NormalizeName(text.substr(sectionStart + 1, sectionEnd - sectionStart - 1)) : std::string();
        const auto focusGeneral = [&](int control) {
            ShowPage(0);
            FocusValidationControl(controls[control], L"Validation error in " + ToWide(key) + L": " + error);
            return true;
        };
        const std::pair<const char*, int> generalControls[] = {
            { "fullscreen", 0 }, { "windowed_fullscreen_mode", 1 }, { "scene_detect", 2 },
            { "hide_legacy_renderers", 3 }, { "startminimized", 4 }, { "start_minimized", 4 },
            { "disable_detection_features", 20 }, { "scene_correction_basic", 21 },
            { "newlldv", 22 }, { "new_lldv", 22 } };
        if (section == "general")
        {
            for (const auto& field : generalControls)
                if (key == field.first) return focusGeneral(field.second);
            const char* startupKeys[] = { "capture_device", "renderer", "fullscreen_monitor_name",
                "fullscreen_monitor_session_mode", "hdr_colorspace", "hdr_luminance" };
            for (int index = 0; index < 6; ++index)
                if (key == startupKeys[index])
                {
                    ShowPage(0);
                    FocusValidationControl(startupFields[index],
                        L"Validation error in " + ToWide(key) + L": " + error);
                    return true;
                }
        }

        const std::pair<const char*, int> outputControls[] = {
            { "quality", 5 }, { "output_presentation", 6 }, { "output_range", 7 },
            { "output_gamma", 8 }, { "sdr_target_primaries", 9 }, { "tone_mapping", 10 },
            { "gamut_mapping", 11 }, { "peak_detection", 12 }, { "upscaler", 13 },
            { "downscaler", 14 }, { "deband", 15 }, { "dithering", 16 },
            { "queue_size", 17 }, { "lead_frames", 18 }, { "target_frames", 19 } };
        int profilePage = -1;
        if (section == "queue" || (section.rfind("queue.", 0) == 0 &&
            section.substr(6).find('.') == std::string::npos)) profilePage = 1;
        if (section == "vprenderer") profilePage = 2;
        else if (section.rfind("vprenderer.", 0) == 0)
        {
            const std::string suffix = section.substr(11);
            if (suffix.find('.') == std::string::npos && suffix != "viewport" &&
                suffix != "input" && suffix != "scaling") profilePage = 2;
        }
        if (section == "lldv" || (section.rfind("lldv.", 0) == 0 &&
            section.substr(5).find('.') == std::string::npos)) profilePage = 4;
        if (profilePage >= 0)
        {
            ShowPage(profilePage);
            auto& sections = ProfileSections(profilePage);
            for (size_t index = 0; index < sections.size(); ++index)
                if (sections[index] == section)
                {
                    SendMessageW(profileSelector, LB_SETCURSEL, index, 0);
                    PopulateSelectedProfile(profilePage, static_cast<int>(index));
                    break;
                }
            if (key == "when" || key == "shortcut")
            {
                FocusValidationControl(key == "when" ? profileRule : profileKey,
                    L"Validation error in " + ToWide(key) + L": " + error);
                return true;
            }
            if (profilePage == 1 && (key == "reset_after_render_restart_seconds" ||
                key == "reset_queue_too_large_percent"))
            {
                FocusValidationControl(queueRecoveryFields[
                    key == "reset_after_render_restart_seconds" ? 0 : 1],
                    L"Validation error in " + ToWide(key) + L": " + error);
                return true;
            }
            if (profilePage == 4)
            {
                const char* lldvKeys[] = { "max_cll", "max_fall", "mastering_min_luminance", "mastering_max_luminance" };
                for (int index = 0; index < 4; ++index)
                    if (key == lldvKeys[index])
                    {
                        FocusValidationControl(lldvFields[index],
                            L"Validation error in " + ToWide(key) + L": " + error);
                        return true;
                    }
            }
        }
        for (const auto& field : outputControls)
            if (key == field.first &&
                ((field.second <= 16 && profilePage == 2) ||
                 (field.second >= 17 && profilePage == 1)))
            {
                FocusValidationControl(controls[field.second], L"Validation error in " + ToWide(key) + L": " + error);
                return true;
            }

        if (section.rfind("vprenderer.viewport", 0) != 0) return false;
        for (size_t index = 0; index < viewportSections.size(); ++index)
            if (viewportSections[index] == section)
            {
                ShowPage(3);
                SendMessageW(viewportSelector, LB_SETCURSEL, index, 0);
                PopulateViewport(static_cast<int>(index));
                const std::pair<const char*, int> viewportControls[] = {
                    { "when", -1 }, { "shortcut", -2 }, { "screen_aspect", 0 }, { "anamorphic_scale", 2 },
                    { "automatic_crop", 3 }, { "subtitle_fit", 4 }, { "subtitle_hold_seconds", 5 },
                    { "subtitle_release_drift_seconds", 6 }, { "subtitle_padding_pixels", 7 } };
                for (const auto& field : viewportControls)
                    if (key == field.first)
                    {
                        if (key == "anamorphic_scale")
                        {
                            SendMessageW(viewportFields[1], BM_SETCHECK, BST_CHECKED, 0);
                            EnableWindow(viewportFields[2], TRUE);
                        }
                        FocusValidationControl(field.second == -1 ? viewportRule :
                            (field.second == -2 ? viewportKey : viewportFields[field.second]),
                            L"Validation error in " + ToWide(key) + L": " + error);
                        return true;
                    }
            }
        return false;
    }

    bool ValidateProfileNames(std::wstring& error, int& errorPage, std::string& errorSection)
    {
        const auto validateGroup = [&](const std::vector<std::pair<std::string, std::wstring>>& names,
            int page) -> bool
        {
            std::vector<std::string> seen;
            for (const auto& entry : names)
            {
                const std::string normalized = ConfigFile::NormalizeName(
                    ConfigFile::Trim(ToNarrow(entry.second)));
                if (normalized.empty())
                {
                    error = L"Profile names cannot be empty.";
                    errorPage = page;
                    errorSection = entry.first;
                    return false;
                }
                if (std::find(seen.begin(), seen.end(), normalized) != seen.end())
                {
                    error = L"Profile names must be unique (names are compared without regard to capitalization). Duplicate: " + entry.second;
                    errorPage = page;
                    errorSection = entry.first;
                    return false;
                }
                seen.push_back(normalized);
            }
            return true;
        };

        for (int page : { 1, 2, 4 })
        {
            std::vector<std::pair<std::string, std::wstring>> names;
            const std::string prefix = ProfilePrefix(page);
            for (const std::string& section : document.SectionNamesWithPrefix(prefix))
            {
                if (section == prefix)
                {
                    error = L"An unnamed legacy profile must be given a name before saving.";
                    errorPage = page;
                    errorSection = section;
                    return false;
                }
                const std::string suffix = section.substr(prefix.size() + 1);
                if (suffix.find('.') != std::string::npos) continue;
                if (page == 2 && (suffix == "input" || suffix == "scaling" || suffix == "viewport")) continue;
                names.push_back({ section, FriendlyProfileIdentifier(suffix) });
            }
            if (!validateGroup(names, page)) return false;
        }

        std::vector<std::pair<std::string, std::wstring>> viewportNames;
        for (const std::string& section : document.SectionNamesWithPrefix("vprenderer.viewport"))
        {
            if (section == "vprenderer.viewport")
            {
                error = L"An unnamed legacy viewport profile must be given a name before saving.";
                errorPage = 3;
                errorSection = section;
                return false;
            }
            viewportNames.push_back({ section, ToWide(ConfigFile::Trim(document.Get(section.c_str(), "label"))) });
        }
        return validateGroup(viewportNames, 3);
    }

    void FocusProfileNameValidation(int page, const std::string& section,
        const std::wstring& error)
    {
        ShowPage(page);
        if (page == 1 || page == 2 || page == 4)
        {
            PopulateProfileList(page, section);
            FocusValidationControl(profileNameEdit, error);
            return;
        }
        PopulateViewportList();
        for (size_t index = 0; index < viewportSections.size(); ++index)
            if (viewportSections[index] == section)
            {
                SendMessageW(viewportSelector, LB_SETCURSEL, index, 0);
                PopulateViewport(static_cast<int>(index));
                break;
            }
        FocusValidationControl(viewportNameEdit, error);
    }

    void Validate()
    {
        profileNameCommitFailed = false;
        ApplyControls();
        if (profileNameCommitFailed) return;
        std::wstring error;
        HWND shortcutControl = nullptr;
        if (!ValidateFixedShortcuts(error, shortcutControl))
        {
            SetDirty();
            ShowPage(5);
            FocusValidationControl(shortcutControl, error);
            MessageBoxW(window, error.c_str(), L"Shortcut validation failed", MB_ICONERROR);
            return;
        }
        int errorPage = -1;
        std::string errorSection;
        if (!ValidateProfileNames(error, errorPage, errorSection))
        {
            SetDirty();
            FocusProfileNameValidation(errorPage, errorSection, error);
            MessageBoxW(window, error.c_str(), L"Profile name validation failed", MB_ICONERROR);
        }
        else if (ValidateCandidate(document, error))
        {
            SetDirty();
            SetWindowTextW(status, L"Validated successfully. Changes are not saved yet.");
        }
        else
        {
            SetDirty();
            if (!FocusKnownValidationError(error))
            {
                MessageBoxW(window, error.c_str(), L"Configuration validation failed", MB_ICONERROR);
                SetWindowTextW(status, L"Validation found a document-level configuration error. Your edits are still here.");
            }
        }
    }

    void Save()
    {
        profileNameCommitFailed = false;
        ApplyControls();
        if (profileNameCommitFailed) return;
        std::wstring error;
        HWND shortcutControl = nullptr;
        if (!ValidateFixedShortcuts(error, shortcutControl))
        {
            SetDirty();
            ShowPage(5);
            FocusValidationControl(shortcutControl, error);
            MessageBoxW(window, error.c_str(), L"Shortcut validation failed", MB_ICONERROR);
            return;
        }
        int errorPage = -1;
        std::string errorSection;
        if (!ValidateProfileNames(error, errorPage, errorSection))
        {
            SetDirty();
            FocusProfileNameValidation(errorPage, errorSection, error);
            MessageBoxW(window, error.c_str(), L"Profile name validation failed", MB_ICONERROR);
            return;
        }
        if (!ValidateCandidate(document, error))
        {
            SetDirty();
            if (!FocusKnownValidationError(error))
            {
                MessageBoxW(window, error.c_str(), L"Configuration validation failed", MB_ICONERROR);
                SetWindowTextW(status, L"Save was not attempted because of a document-level validation error. Your edits are still here.");
            }
            return;
        }
        const std::wstring backup = document.path + L".backup-" + Timestamp();
        const std::wstring temporary = document.path + L".vpconfig-write.tmp";
        if (!CopyFileW(document.path.c_str(), backup.c_str(), TRUE))
        {
            MessageBoxW(window, L"Could not create a backup. The configuration was not changed.", L"VideoProcessor Config", MB_ICONERROR);
            SetDirty();
            return;
        }
        {
            std::ofstream output(ToNarrow(temporary), std::ios::binary | std::ios::trunc);
            output << document.Serialize();
            if (!output) { DeleteFileW(temporary.c_str()); MessageBoxW(window, L"Could not write the temporary configuration. The backup was retained.", L"VideoProcessor Config", MB_ICONERROR); SetDirty(); return; }
        }
        if (!MoveFileExW(temporary.c_str(), document.path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(temporary.c_str());
            MessageBoxW(window, (L"Could not replace the configuration. Backup retained at:\n" + backup).c_str(), L"VideoProcessor Config", MB_ICONERROR);
            SetDirty(); return;
        }
        SetDirty(false);
        SetWindowTextW(status, L"Saved safely. A timestamped backup was created. Restart VP to apply startup settings.");
    }

    void AddTray()
    {
        tray = {};
        tray.cbSize = sizeof(tray); tray.hWnd = window; tray.uID = kTrayIconId;
        tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; tray.uCallbackMessage = kTrayMessage;
        tray.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_VIDEOPROCESSOR_CONFIG));
        wcscpy_s(tray.szTip, L"VideoProcessor Configuration - left-click to open, right-click for menu");
        Shell_NotifyIconW(NIM_ADD, &tray);
        tray.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &tray);
    }
    void RemoveTray() { Shell_NotifyIconW(NIM_DELETE, &tray); }

    void Open()
    {
        ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
    }

    void ShowTrayMenu()
    {
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr) return;
        AppendMenuW(menu, MF_STRING, kTrayOpenCommand, L"Open configuration");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kTrayExitCommand, L"Exit");
        POINT point = {};
        GetCursorPos(&point);
        SetForegroundWindow(window);
        const UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            point.x, point.y, 0, window, nullptr);
        DestroyMenu(menu);
        // Lets the notification area dismiss the menu correctly after a click elsewhere.
        PostMessageW(window, WM_NULL, 0, 0);
        if (choice == kTrayOpenCommand) Open();
        if (choice == kTrayExitCommand) DestroyWindow(window);
    }

    void ShowCloseTip()
    {
        if (closeTipShown) return;
        closeTipShown = true;
        tray.uFlags = NIF_INFO;
        wcscpy_s(tray.szInfoTitle, L"VideoProcessor Configuration");
        wcscpy_s(tray.szInfo, L"The editor is still running in the notification area. Click the VP icon to reopen it.");
        tray.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &tray);
        tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    }

    void Layout(int width, int height)
    {
        // A resize moves a large number of nested native controls.  Letting each
        // MoveWindow repaint independently exposes intermediate layouts and leaves
        // stale pixels behind (especially while the user is dragging a window edge).
        // Freeze the tree until every child has reached its final rectangle, then
        // repaint the complete hierarchy in one pass.
        SendMessageW(window, WM_SETREDRAW, FALSE, 0);
        const int margin = Px(24);
        const int headerHeight = Px(64);
        const int footerHeight = Px(64);
        const int sidebarWidth = Px(208);
        const int gap = Px(24);
        const int contentLeft = margin + sidebarWidth + gap;
        const int contentWidth = std::max(Px(320), width - contentLeft - margin);
        const int top = headerHeight + Px(14);
        const int footerTop = std::max(top + Px(240), height - footerHeight);

        MoveWindow(headerIcon, margin, Px(15), Px(34), Px(34), TRUE);
        MoveWindow(headerTitle, margin + Px(46), Px(12), Px(280), Px(28), TRUE);
        MoveWindow(headerSubtitle, margin + Px(47), Px(40), Px(280), Px(18), TRUE);
        MoveWindow(navigationCaption, margin, top, sidebarWidth, Px(18), TRUE);
        for (int index = 0; index < 6; ++index)
            MoveWindow(navigation[index], margin, top + Px(24) + index * Px(40), sidebarWidth, Px(36), TRUE);
        MoveWindow(pageTitle, contentLeft, top, contentWidth, Px(30), TRUE);
        MoveWindow(pageDescription, contentLeft, top + Px(31), contentWidth, Px(36), TRUE);
        MoveWindow(contentHost, contentLeft, top + Px(78), contentWidth,
            std::max(Px(120), footerTop - (top + Px(78))), TRUE);

        MoveWindow(status, margin, footerTop + Px(20),
            std::max(Px(180), width - margin * 2 - Px(338)), Px(22), TRUE);
        MoveWindow(GetDlgItem(window, kReloadButton), width - margin - Px(314), footerTop + Px(14), Px(86), Px(36), TRUE);
        MoveWindow(GetDlgItem(window, kValidateButton), width - margin - Px(218), footerTop + Px(14), Px(94), Px(36), TRUE);
        MoveWindow(GetDlgItem(window, kSaveButton), width - margin - Px(114), footerTop + Px(14), Px(114), Px(36), TRUE);

        RECT content = {};
        GetClientRect(contentHost, &content);
        contentViewHeight = content.bottom - content.top;
        LayoutPageContent(false);
        SendMessageW(window, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    }

    void LayoutPageContent(bool repaint = true)
    {
        if (contentHost == nullptr) return;
        if (repaint) SendMessageW(contentHost, WM_SETREDRAW, FALSE, 0);
        RECT host = {};
        GetClientRect(contentHost, &host);
        RECT hostWindow = {};
        GetWindowRect(contentHost, &hostWindow);
        // Measure first with the full host width so a currently visible scrollbar
        // cannot trap the page in a narrower, stacked layout after content shrinks.
        int width = hostWindow.right - hostWindow.left;
        const int cardGap = Px(24);
        const int cardPadding = Px(20);
        const int inputHeight = Px(28);
        const int scrollBarWidth = GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
        const int columnThreshold = Px(630) + scrollBarWidth;
        const bool startupTwoColumn = width >= columnThreshold;
        const int startupHeight = startupTwoColumn ? Px(620) : Px(1000);
        const int queueHeight = Px(690);
        const int rendererHeight = Px(855);
        const int profileListHeight = Px(440);
        const int viewportHeight = Px(820);
        const int viewportListHeight = Px(440);
        const int shortcutsHeight = Px(650);
        // Base the responsive mode on the stable outer width and reserve room
        // for a possible scrollbar.  The scrollbar may change client width,
        // but it must not make the page oscillate between columns and stacking.
        const bool viewportTwoColumn = width >= columnThreshold;
        const bool profileTwoColumn = width >= columnThreshold;
        const int estimatedAvailableWidth = std::max(0, width - scrollBarWidth);
        const int estimatedProfileListWidth = profileTwoColumn ?
            std::min(Px(238), estimatedAvailableWidth * 38 / 100) : 0;
        const int estimatedProfileDetailWidth = profileTwoColumn ?
            estimatedAvailableWidth - estimatedProfileListWidth - cardGap - cardPadding * 2 :
            estimatedAvailableWidth - cardPadding * 2;
        const bool lldvCompactRows = estimatedProfileDetailWidth < Px(370);
        const int lldvHeight = lldvCompactRows ? Px(790) : Px(690);
        auto measureContentHeight = [&]()
        {
            if (activePage == 0) return startupHeight;
            if (activePage == 1)
                return profileTwoColumn ? std::max(profileListHeight, queueHeight) : profileListHeight + cardGap + queueHeight;
            if (activePage == 2)
                return profileTwoColumn ? std::max(profileListHeight, rendererHeight) : profileListHeight + cardGap + rendererHeight;
            if (activePage == 3)
                return viewportTwoColumn ? viewportHeight : viewportListHeight + cardGap + viewportHeight;
            if (activePage == 4)
                return profileTwoColumn ? std::max(profileListHeight, lldvHeight) : profileListHeight + cardGap + lldvHeight;
            return shortcutsHeight;
        };
        contentHeight = measureContentHeight();
        // Keep the vertical gutter reserved even when the page currently fits.
        // Showing it only for Advanced content changes the client width and makes
        // both cards visibly jump sideways when the section is expanded.
        ShowScrollBar(contentHost, SB_VERT, TRUE);
        GetClientRect(contentHost, &host);
        width = host.right - host.left;
        contentHeight = measureContentHeight();
        const int maximumScroll = std::max(0, contentHeight - contentViewHeight);
        contentScroll = std::max(0, std::min(contentScroll, maximumScroll));
        SCROLLINFO scroll = { sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL };
        scroll.nMin = 0; scroll.nMax = std::max(0, contentHeight - 1);
        scroll.nPage = std::max(1, contentViewHeight); scroll.nPos = contentScroll;
        SetScrollInfo(contentHost, SB_VERT, &scroll, TRUE);

        const int y = -contentScroll;
        if (activePage == 0)
        {
            const bool twoColumn = startupTwoColumn;
            const int hardwareHeight = Px(220);
            const int lowerTop = y + hardwareHeight + cardGap;
            const int lowerWidth = twoColumn ? (width - cardGap) / 2 : width;
            const int windowHeight = Px(350);
            const int sourceHeight = Px(370);
            const int sourceLeft = twoColumn ? lowerWidth + cardGap : 0;
            const int sourceTop = twoColumn ? lowerTop : lowerTop + windowHeight + cardGap;
            MoveWindow(cards[0], 0, y, width, hardwareHeight, TRUE);
            MoveWindow(cards[7], 0, lowerTop, lowerWidth, windowHeight, TRUE);
            MoveWindow(cards[8], sourceLeft, sourceTop,
                twoColumn ? width - sourceLeft : width, sourceHeight, TRUE);

            auto layoutCardTitle = [&](int card, int cardWidth)
            {
                MoveWindow(cardTitles[card], cardPadding, cardPadding,
                    cardWidth - cardPadding * 2, Px(24), TRUE);
                MoveWindow(cardSubtitles[card], cardPadding, cardPadding + Px(26),
                    cardWidth - cardPadding * 2, Px(38), TRUE);
            };
            layoutCardTitle(0, width);
            layoutCardTitle(7, lowerWidth);
            layoutCardTitle(8, twoColumn ? width - sourceLeft : width);

            const int hardwareLabelWidth = Px(150);
            const int hardwareFieldLeft = cardPadding + hardwareLabelWidth + Px(12);
            const int hardwareFieldWidth = width - hardwareFieldLeft - cardPadding;
            for (int index = 0; index < 2; ++index)
            {
                const int rowTop = cardPadding + Px(74) + index * Px(42);
                MoveWindow(startupLabels[index], cardPadding, rowTop + Px(5),
                    hardwareLabelWidth, Px(20), TRUE);
                MoveWindow(startupFields[index], hardwareFieldLeft, rowTop,
                    hardwareFieldWidth, Px(240), TRUE);
            }
            MoveWindow(controls[3], cardPadding, cardPadding + Px(160),
                width - cardPadding * 2, Px(26), TRUE);

            const int windowFieldWidth = std::max(Px(150), lowerWidth - cardPadding * 2);
            MoveWindow(controls[0], cardPadding, cardPadding + Px(72), windowFieldWidth, Px(26), TRUE);
            MoveWindow(controls[1], cardPadding, cardPadding + Px(106), windowFieldWidth, Px(26), TRUE);
            for (int index = 2; index <= 3; ++index)
            {
                const int rowTop = cardPadding + Px(146) + (index - 2) * Px(66);
                MoveWindow(startupLabels[index], cardPadding, rowTop, windowFieldWidth, Px(20), TRUE);
                MoveWindow(startupFields[index], cardPadding, rowTop + Px(22), windowFieldWidth, Px(240), TRUE);
            }
            MoveWindow(controls[4], cardPadding, cardPadding + Px(286), windowFieldWidth, Px(26), TRUE);

            const int sourceWidth = twoColumn ? width - sourceLeft : width;
            const int sourceFieldWidth = std::max(Px(150), sourceWidth - cardPadding * 2);
            for (int index = 4; index <= 5; ++index)
            {
                const int rowTop = cardPadding + Px(72) + (index - 4) * Px(66);
                MoveWindow(startupLabels[index], cardPadding, rowTop, sourceFieldWidth, Px(20), TRUE);
                MoveWindow(startupFields[index], cardPadding, rowTop + Px(22), sourceFieldWidth, Px(240), TRUE);
            }
            MoveWindow(controls[22], cardPadding, cardPadding + Px(210), sourceFieldWidth, Px(26), TRUE);
            MoveWindow(controls[20], cardPadding, cardPadding + Px(244), sourceFieldWidth, Px(26), TRUE);
            MoveWindow(controls[2], cardPadding, cardPadding + Px(278), sourceFieldWidth, Px(26), TRUE);
            MoveWindow(controls[21], cardPadding, cardPadding + Px(312), sourceFieldWidth, Px(26), TRUE);
        }
        else if (activePage == 1 || activePage == 2 || activePage == 4)
        {
            const bool queuePage = activePage == 1;
            const bool lldvPage = activePage == 4;
            const int cardHeight = queuePage ? queueHeight : (lldvPage ? lldvHeight : rendererHeight);
            const int listWidth = profileTwoColumn ? std::min(Px(238), width * 38 / 100) : width;
            const int detailLeft = profileTwoColumn ? listWidth + cardGap : 0;
            const int detailTop = profileTwoColumn ? y : y + profileListHeight + cardGap;
            const int cardWidth = profileTwoColumn ? width - detailLeft : width;
            const int listHeight = profileTwoColumn ? cardHeight : profileListHeight;
            MoveWindow(cards[4], 0, profileTwoColumn ? 0 : y, listWidth, listHeight, TRUE);
            MoveWindow(cards[1], detailLeft, detailTop, cardWidth, cardHeight, TRUE);
            const int profileTextWidth = listWidth - cardPadding * 2;
            const int profileTitleHeight = MeasureTextHeight(cardTitles[4], profileTextWidth, Px(24));
            int profileY = cardPadding;
            MoveWindow(cardTitles[4], cardPadding, profileY, profileTextWidth, profileTitleHeight, TRUE);
            profileY += profileTitleHeight + Px(5);
            const int profileHintHeight = MeasureTextHeight(profileListHint, profileTextWidth, Px(32));
            MoveWindow(profileListHint, cardPadding, profileY, profileTextWidth, profileHintHeight, TRUE);
            profileY += profileHintHeight + Px(10);
            MoveWindow(addProfileButton, cardPadding, profileY, profileTextWidth, Px(32), TRUE);
            profileY += Px(40);
            const int halfButton = (listWidth - cardPadding * 2 - Px(8)) / 2;
            MoveWindow(removeProfileButton, cardPadding, profileY, profileTextWidth, Px(30), TRUE);
            profileY += Px(38);
            MoveWindow(moveProfileUpButton, cardPadding, profileY, halfButton, Px(30), TRUE);
            MoveWindow(moveProfileDownButton, cardPadding + halfButton + Px(8), profileY, halfButton, Px(30), TRUE);
            profileY += Px(42);
            MoveWindow(profileSelector, cardPadding, profileY, profileTextWidth,
                std::max(Px(90), listHeight - cardPadding - profileY), TRUE);

            MoveWindow(cardTitles[1], cardPadding, cardPadding, cardWidth - cardPadding * 2, Px(24), TRUE);
            MoveWindow(cardSubtitles[1], cardPadding, cardPadding + Px(26), cardWidth - cardPadding * 2, Px(38), TRUE);
            const int profileDetailWidth = cardWidth - cardPadding * 2;
            int profileDetailY = cardPadding + Px(72);
            MoveWindow(profileNameLabel, cardPadding, profileDetailY, profileDetailWidth, Px(20), TRUE);
            profileDetailY += Px(22);
            MoveWindow(profileNameEdit, cardPadding, profileDetailY,
                std::min(Px(260), profileDetailWidth), inputHeight, TRUE);
            profileDetailY += inputHeight + Px(14);
            MoveWindow(profileKeyLabel, cardPadding, profileDetailY, profileDetailWidth, Px(20), TRUE);
            profileDetailY += Px(22);
            MoveWindow(profileKey, cardPadding, profileDetailY, std::min(Px(220), profileDetailWidth), inputHeight, TRUE);
            profileDetailY += inputHeight + Px(7);
            const int profileKeyHelpHeight = MeasureTextHeight(profileKeyHelp, profileDetailWidth, Px(22));
            MoveWindow(profileKeyHelp, cardPadding, profileDetailY, profileDetailWidth, profileKeyHelpHeight, TRUE);
            profileDetailY += profileKeyHelpHeight + Px(12);
            MoveWindow(profileRuleLabel, cardPadding, profileDetailY, profileDetailWidth, Px(24), TRUE);
            profileDetailY += Px(30);
            if (ProfileRuleVisible())
            {
                MoveWindow(profileRule, cardPadding, profileDetailY, profileDetailWidth, Px(72), TRUE);
                profileDetailY += Px(78);
                const int profileRuleHelpHeight = MeasureTextHeight(profileRuleHelp, profileDetailWidth, Px(22));
                MoveWindow(profileRuleHelp, cardPadding, profileDetailY, profileDetailWidth, profileRuleHelpHeight, TRUE);
                profileDetailY += profileRuleHelpHeight + Px(14);
            }
            const int profileSettingsTop = profileDetailY + Px(8);
            const int labelWidth = Px(118);
            const int fieldLeft = cardPadding + labelWidth + Px(10);
            const int fieldWidth = cardWidth - fieldLeft - cardPadding;
            if (lldvPage)
            {
                if (lldvCompactRows)
                {
                    for (int index = 0; index < 4; ++index)
                    {
                        const int rowTop = profileSettingsTop + index * Px(60);
                        MoveWindow(lldvLabels[index], cardPadding, rowTop,
                            profileDetailWidth, Px(20), TRUE);
                        MoveWindow(lldvFields[index], cardPadding, rowTop + Px(24),
                            Px(115), inputHeight, TRUE);
                        MoveWindow(lldvUnits[index], cardPadding + Px(123),
                            rowTop + Px(28), Px(40), Px(20), TRUE);
                    }
                }
                else
                {
                    const int lldvFieldWidth = std::min(Px(115), std::max(Px(78), profileDetailWidth / 3));
                    const int lldvUnitWidth = Px(40);
                    const int lldvLabelWidth = std::max(Px(120), profileDetailWidth -
                        lldvFieldWidth - lldvUnitWidth - Px(18));
                    const int lldvFieldLeft = cardPadding + lldvLabelWidth + Px(10);
                    for (int index = 0; index < 4; ++index)
                    {
                        const int rowTop = profileSettingsTop + index * Px(38);
                        MoveWindow(lldvLabels[index], cardPadding, rowTop + Px(4),
                            lldvLabelWidth, Px(20), TRUE);
                        MoveWindow(lldvFields[index], lldvFieldLeft, rowTop,
                            lldvFieldWidth, inputHeight, TRUE);
                        MoveWindow(lldvUnits[index], lldvFieldLeft + lldvFieldWidth + Px(8),
                            rowTop + Px(4), lldvUnitWidth, Px(20), TRUE);
                    }
                }
            }
            else
            {
                const int first = queuePage ? 3 : 0;
                const int basicControls[] = { queuePage ? 17 : 5, queuePage ? 18 : 6, queuePage ? 19 : 7 };
                for (int index = 0; index < 3; ++index)
                {
                    const int rowTop = profileSettingsTop + index * Px(34);
                    MoveWindow(outputLabels[first + index], cardPadding, rowTop + Px(4), labelWidth - Px(6), Px(20), TRUE);
                    const bool numeric = queuePage;
                    const int controlWidth = numeric ? fieldWidth - Px(48) : fieldWidth;
                    MoveWindow(controls[basicControls[index]], fieldLeft, rowTop, controlWidth,
                        numeric ? inputHeight : Px(240), TRUE);
                    if (numeric)
                        MoveWindow(outputUnits[index], fieldLeft + controlWidth + Px(8), rowTop + Px(4), Px(40), Px(20), TRUE);
                }
                if (queuePage)
                {
                    const int recoveryTop = profileSettingsTop + Px(126);
                    MoveWindow(queueRecoveryTitle, cardPadding, recoveryTop,
                        profileDetailWidth, Px(24), TRUE);
                    const int helpHeight = MeasureTextHeight(queueRecoveryHelp,
                        profileDetailWidth, Px(36));
                    MoveWindow(queueRecoveryHelp, cardPadding, recoveryTop + Px(28),
                        profileDetailWidth, helpHeight, TRUE);
                    const int recoveryFieldWidth = Px(70);
                    const int recoveryUnitWidth = Px(62);
                    const int recoveryLabelWidth = std::max(Px(150), profileDetailWidth -
                        recoveryFieldWidth - recoveryUnitWidth - Px(18));
                    const int recoveryFieldLeft = cardPadding + recoveryLabelWidth + Px(10);
                    for (int index = 0; index < 2; ++index)
                    {
                        const int rowTop = recoveryTop + helpHeight + Px(42) + index * Px(38);
                        MoveWindow(queueRecoveryLabels[index], cardPadding, rowTop + Px(4),
                            recoveryLabelWidth, Px(20), TRUE);
                        MoveWindow(queueRecoveryFields[index], recoveryFieldLeft, rowTop,
                            recoveryFieldWidth, inputHeight, TRUE);
                        MoveWindow(queueRecoveryUnits[index], recoveryFieldLeft + recoveryFieldWidth + Px(8),
                            rowTop + Px(4), recoveryUnitWidth, Px(20), TRUE);
                    }
                }
                else
                {
                    const int advancedControls[] = { 8, 9, 10, 11, 12, 13, 14, 15, 16 };
                    for (int index = 0; index < 9; ++index)
                    {
                        const int rowTop = profileSettingsTop + Px(116) + index * Px(34);
                        MoveWindow(outputLabels[6 + index], cardPadding, rowTop + Px(4), labelWidth - Px(6), Px(20), TRUE);
                        MoveWindow(controls[advancedControls[index]], fieldLeft, rowTop, fieldWidth, Px(240), TRUE);
                    }
                }
            }
        }
        else if (activePage == 3)
        {
            const int listWidth = viewportTwoColumn ? std::min(Px(238), width * 38 / 100) : width;
            const int detailLeft = viewportTwoColumn ? listWidth + cardGap : 0;
            const int detailTop = viewportTwoColumn ? y : y + viewportListHeight + cardGap;
            const int detailWidth = viewportTwoColumn ? width - detailLeft : width;
            const int listHeight = viewportTwoColumn ? viewportHeight : viewportListHeight;
            MoveWindow(cards[3], 0, viewportTwoColumn ? 0 : y, listWidth, listHeight, TRUE);
            MoveWindow(cards[2], detailLeft, detailTop, detailWidth, viewportHeight, TRUE);

            const int viewportListTextWidth = listWidth - cardPadding * 2;
            const int viewportListTitleHeight = MeasureTextHeight(cardTitles[3], viewportListTextWidth, Px(24));
            int viewportListY = cardPadding;
            MoveWindow(cardTitles[3], cardPadding, viewportListY, viewportListTextWidth, viewportListTitleHeight, TRUE);
            viewportListY += viewportListTitleHeight + Px(5);
            const int viewportHintHeight = MeasureTextHeight(viewportListHint, viewportListTextWidth, Px(32));
            MoveWindow(viewportListHint, cardPadding, viewportListY, viewportListTextWidth, viewportHintHeight, TRUE);
            viewportListY += viewportHintHeight + Px(10);
            MoveWindow(addViewportButton, cardPadding, viewportListY, viewportListTextWidth, Px(32), TRUE);
            viewportListY += Px(40);
            const int reorderWidth = (listWidth - cardPadding * 2 - Px(8)) / 2;
            MoveWindow(removeViewportButton, cardPadding, viewportListY, viewportListTextWidth, Px(30), TRUE);
            viewportListY += Px(38);
            MoveWindow(moveViewportUpButton, cardPadding, viewportListY, reorderWidth, Px(30), TRUE);
            MoveWindow(moveViewportDownButton, cardPadding + reorderWidth + Px(8), viewportListY,
                reorderWidth, Px(30), TRUE);
            viewportListY += Px(42);
            MoveWindow(viewportSelector, cardPadding, viewportListY, viewportListTextWidth,
                std::max(Px(90), listHeight - cardPadding - viewportListY), TRUE);

            const int detailTextWidth = detailWidth - cardPadding * 2;
            int viewportDetailY = cardPadding;
            const int viewportTitleHeight = MeasureTextHeight(viewportDetailTitle, detailTextWidth, Px(24));
            MoveWindow(viewportDetailTitle, cardPadding, viewportDetailY, detailTextWidth, viewportTitleHeight, TRUE);
            viewportDetailY += viewportTitleHeight + Px(4);
            const int viewportSubtitleHeight = MeasureTextHeight(viewportDetailSubtitle, detailTextWidth, Px(24));
            MoveWindow(viewportDetailSubtitle, cardPadding, viewportDetailY, detailTextWidth, viewportSubtitleHeight, TRUE);
            viewportDetailY += viewportSubtitleHeight + Px(14);
            MoveWindow(viewportNameLabel, cardPadding, viewportDetailY, detailTextWidth, Px(20), TRUE);
            viewportDetailY += Px(22);
            MoveWindow(viewportNameEdit, cardPadding, viewportDetailY,
                std::min(Px(260), detailTextWidth), inputHeight, TRUE);
            viewportDetailY += inputHeight + Px(10);
            if (IsWindowVisible(nameViewportButton))
            {
                MoveWindow(nameViewportButton, cardPadding, viewportDetailY, Px(160), Px(30), TRUE);
                viewportDetailY += Px(40);
            }
            MoveWindow(viewportKeyLabel, cardPadding, viewportDetailY, detailTextWidth, Px(20), TRUE);
            viewportDetailY += Px(22);
            MoveWindow(viewportKey, cardPadding, viewportDetailY, std::min(Px(220), detailTextWidth), inputHeight, TRUE);
            viewportDetailY += inputHeight + Px(7);
            const int keyHelpHeight = MeasureTextHeight(viewportKeyHelp, detailTextWidth, Px(24));
            MoveWindow(viewportKeyHelp, cardPadding, viewportDetailY, detailTextWidth, keyHelpHeight, TRUE);
            viewportDetailY += keyHelpHeight + Px(12);
            MoveWindow(viewportLabels[0], cardPadding, viewportDetailY, detailTextWidth, Px(24), TRUE);
            viewportDetailY += Px(30);
            if (ViewportRuleVisible())
            {
                MoveWindow(viewportRule, cardPadding, viewportDetailY, detailTextWidth, Px(72), TRUE);
                viewportDetailY += Px(78);
                const int ruleHelpHeight = MeasureTextHeight(viewportLabels[1], detailTextWidth, Px(24));
                MoveWindow(viewportLabels[1], cardPadding, viewportDetailY, detailTextWidth, ruleHelpHeight, TRUE);
                viewportDetailY += ruleHelpHeight + Px(14);
            }
            viewportDetailY += Px(4);
            MoveWindow(viewportLabels[2], cardPadding, viewportDetailY, detailTextWidth, Px(20), TRUE);
            const int geometryTop = viewportDetailY;

            const int fieldLabelWidth = Px(142);
            const int fieldLeft = cardPadding + fieldLabelWidth + Px(10);
            const int fieldWidth = std::min(Px(128), detailWidth - fieldLeft - cardPadding);
            MoveWindow(viewportLabels[3], cardPadding, geometryTop + Px(30), fieldLabelWidth, Px(20), TRUE);
            MoveWindow(viewportFields[0], fieldLeft, geometryTop + Px(24), fieldWidth, inputHeight, TRUE);
            MoveWindow(viewportLabels[4], cardPadding, geometryTop + Px(60), detailTextWidth, Px(22), TRUE);
            MoveWindow(viewportFields[1], cardPadding, geometryTop + Px(88), detailTextWidth, Px(26), TRUE);
            MoveWindow(viewportLabels[5], cardPadding, geometryTop + Px(126), fieldLabelWidth, Px(20), TRUE);
            MoveWindow(viewportFields[2], fieldLeft, geometryTop + Px(120), fieldWidth, inputHeight, TRUE);
            MoveWindow(viewportLabels[6], cardPadding, geometryTop + Px(154), detailTextWidth, Px(22), TRUE);
            MoveWindow(viewportFields[3], cardPadding, geometryTop + Px(184), detailTextWidth, Px(26), TRUE);

            MoveWindow(viewportLabels[7], cardPadding, geometryTop + Px(226), detailTextWidth, Px(20), TRUE);
            MoveWindow(viewportFields[4], cardPadding, geometryTop + Px(252), detailTextWidth, Px(26), TRUE);
            MoveWindow(viewportLabels[8], cardPadding, geometryTop + Px(288), fieldLabelWidth, Px(20), TRUE);
            MoveWindow(viewportFields[5], fieldLeft, geometryTop + Px(282), Px(78), inputHeight, TRUE);
            MoveWindow(viewportUnits[0], fieldLeft + Px(86), geometryTop + Px(288), Px(64), Px(20), TRUE);
            MoveWindow(viewportLabels[9], cardPadding, geometryTop + Px(322), fieldLabelWidth, Px(20), TRUE);
            MoveWindow(viewportFields[6], fieldLeft, geometryTop + Px(316), Px(78), inputHeight, TRUE);
            MoveWindow(viewportUnits[1], fieldLeft + Px(86), geometryTop + Px(322), Px(64), Px(20), TRUE);
            MoveWindow(viewportLabels[10], cardPadding, geometryTop + Px(356), fieldLabelWidth, Px(20), TRUE);
            MoveWindow(viewportFields[7], fieldLeft, geometryTop + Px(350), Px(78), inputHeight, TRUE);
            MoveWindow(viewportUnits[2], fieldLeft + Px(86), geometryTop + Px(356), Px(48), Px(20), TRUE);
        }
        else if (activePage == 4)
        {
            const int cardWidth = std::min(width, Px(680));
            MoveWindow(cards[5], 0, y, cardWidth, lldvHeight, TRUE);
            MoveWindow(cardTitles[5], cardPadding, cardPadding,
                cardWidth - cardPadding * 2, Px(24), TRUE);
            const int subtitleHeight = MeasureTextHeight(cardSubtitles[5],
                cardWidth - cardPadding * 2, Px(48));
            MoveWindow(cardSubtitles[5], cardPadding, cardPadding + Px(28),
                cardWidth - cardPadding * 2, subtitleHeight, TRUE);
            const int labelWidth = Px(190);
            const int fieldLeft = cardPadding + labelWidth + Px(12);
            for (int index = 0; index < 4; ++index)
            {
                const int rowTop = cardPadding + subtitleHeight + Px(58) + index * Px(54);
                MoveWindow(lldvLabels[index], cardPadding, rowTop + Px(4), labelWidth, Px(20), TRUE);
                MoveWindow(lldvFields[index], fieldLeft, rowTop, Px(150), inputHeight, TRUE);
                MoveWindow(lldvUnits[index], fieldLeft + Px(160), rowTop + Px(4), Px(48), Px(20), TRUE);
            }
        }
        else
        {
            const int cardWidth = std::min(width, Px(720));
            MoveWindow(cards[6], 0, y, cardWidth, shortcutsHeight, TRUE);
            MoveWindow(cardTitles[6], cardPadding, cardPadding,
                cardWidth - cardPadding * 2, Px(24), TRUE);
            const int subtitleHeight = MeasureTextHeight(cardSubtitles[6],
                cardWidth - cardPadding * 2, Px(40));
            MoveWindow(cardSubtitles[6], cardPadding, cardPadding + Px(28),
                cardWidth - cardPadding * 2, subtitleHeight, TRUE);
            const int labelWidth = Px(220);
            const int fieldLeft = cardPadding + labelWidth + Px(12);
            for (size_t index = 0; index < ARRAYSIZE(kShortcutFields); ++index)
            {
                const int rowTop = cardPadding + subtitleHeight + Px(44) +
                    static_cast<int>(index) * Px(36);
                MoveWindow(shortcutLabels[index], cardPadding, rowTop + Px(4), labelWidth, Px(20), TRUE);
                MoveWindow(shortcutFields[index], fieldLeft, rowTop,
                    std::min(Px(210), cardWidth - fieldLeft - cardPadding), inputHeight, TRUE);
            }
        }
        if (repaint)
        {
            SendMessageW(contentHost, WM_SETREDRAW, TRUE, 0);
            RedrawWindow(contentHost, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
    }

    void ScrollContent(int command, int)
    {
        const int page = std::max(Px(48), contentViewHeight - Px(48));
        const int maximum = std::max(0, contentHeight - contentViewHeight);
        switch (command)
        {
        case SB_LINEUP: contentScroll -= Px(24); break;
        case SB_LINEDOWN: contentScroll += Px(24); break;
        case SB_PAGEUP: contentScroll -= page; break;
        case SB_PAGEDOWN: contentScroll += page; break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
        {
            SCROLLINFO track = { sizeof(track), SIF_POS | SIF_TRACKPOS };
            GetScrollInfo(contentHost, SB_VERT, &track);
            contentScroll = command == SB_THUMBTRACK ? track.nTrackPos : track.nPos;
            break;
        }
        case SB_TOP: contentScroll = 0; break;
        case SB_BOTTOM: contentScroll = maximum; break;
        default: return;
        }
        contentScroll = std::max(0, std::min(contentScroll, maximum));
        LayoutPageContent();
        if (command == SB_THUMBTRACK)
            RedrawWindow(contentHost, nullptr, nullptr, RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
};

struct ViewportNameDialogState
{
    std::wstring value;
    std::wstring caption;
    std::wstring prompt;
    std::wstring confirmCaption;
    std::wstring initial;
};

INT_PTR CALLBACK AddViewportDialogProcedure(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        if (const auto* state = reinterpret_cast<ViewportNameDialogState*>(lParam))
        {
            SetWindowTextW(dialog, state->caption.c_str());
            SetDlgItemTextW(dialog, IDC_VIEWPORT_NAME_PROMPT, state->prompt.c_str());
            SetDlgItemTextW(dialog, IDOK, state->confirmCaption.c_str());
            SetDlgItemTextW(dialog, IDC_VIEWPORT_NAME, state->initial.c_str());
        }
        SendMessageW(GetDlgItem(dialog, IDC_VIEWPORT_NAME), EM_SETCUEBANNER, TRUE,
            reinterpret_cast<LPARAM>(L"e.g. Scope Cinema"));
        SetFocus(GetDlgItem(dialog, IDC_VIEWPORT_NAME));
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            auto* state = reinterpret_cast<ViewportNameDialogState*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
            wchar_t name[128] = {};
            GetDlgItemTextW(dialog, IDC_VIEWPORT_NAME, name, ARRAYSIZE(name));
            if (state != nullptr) state->value = name;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

bool PromptViewportName(HWND owner, std::wstring& value,
    const std::wstring& caption, const std::wstring& prompt,
    const std::wstring& confirmCaption, const std::wstring& initial)
{
    ViewportNameDialogState state;
    state.caption = caption;
    state.prompt = prompt;
    state.confirmCaption = confirmCaption;
    state.initial = initial;
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ADD_VIEWPORT), owner,
        AddViewportDialogProcedure, reinterpret_cast<LPARAM>(&state)) == IDOK &&
        (value = state.value, true);
}

EditorWindow* GetEditor(HWND window) { return reinterpret_cast<EditorWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA)); }

constexpr COLORREF kCanvasColor = RGB(245, 247, 250);
constexpr COLORREF kSurfaceColor = RGB(255, 255, 255);
constexpr COLORREF kBorderColor = RGB(215, 222, 232);
constexpr COLORREF kInkColor = RGB(0, 0, 0);
constexpr COLORREF kMutedColor = RGB(105, 105, 105);
constexpr COLORREF kHeaderColor = RGB(22, 59, 100);
constexpr COLORREF kPrimaryColor = RGB(15, 94, 168);
constexpr COLORREF kActiveNavColor = RGB(234, 243, 255);

bool IsHighContrastMode()
{
    HIGHCONTRASTW contrast = { sizeof(contrast) };
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

EditorWindow* FindEditor(HWND window)
{
    for (HWND current = window; current != nullptr; current = GetParent(current))
    {
        if (EditorWindow* editor = GetEditor(current)) return editor;
        if (EditorWindow* editor = reinterpret_cast<EditorWindow*>(GetPropW(current, kEditorProperty))) return editor;
    }
    return nullptr;
}

void FillSolid(HDC device, const RECT& rectangle, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(device, &rectangle, brush);
    DeleteObject(brush);
}

void DrawRoundedSurface(HDC device, RECT rectangle, COLORREF fill, COLORREF border, int radius)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(device, brush);
    HGDIOBJ oldPen = SelectObject(device, pen);
    RoundRect(device, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom, radius, radius);
    SelectObject(device, oldBrush);
    SelectObject(device, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawOwnerButton(DRAWITEMSTRUCT* draw, EditorWindow* editor)
{
    if (draw == nullptr) return;
    const int id = static_cast<int>(draw->CtlID);
    const bool disabled = (draw->itemState & ODS_DISABLED) != 0;
    const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
    const bool focused = (draw->itemState & ODS_FOCUS) != 0;
    const bool hot = (draw->itemState & ODS_HOTLIGHT) != 0;
    const bool highContrast = IsHighContrastMode();
    const bool navigation = id >= kNavigationStartup && id <= kNavigationShortcuts;
    const bool selectedNavigation = navigation && editor != nullptr &&
        id == kNavigationStartup + editor->activePage;
    const bool primary = id == kSaveButton;
    const bool add = id == kViewportAddButton || id == kViewportNameButton ||
        id == kViewportMoveUpButton || id == kViewportMoveDownButton ||
        id == kProfileAddButton || id == kProfileRenameButton ||
        id == kProfileMoveUpButton || id == kProfileMoveDownButton;
    const bool remove = id == kViewportRemoveButton || id == kProfileRemoveButton;
    RECT rectangle = draw->rcItem;
    COLORREF fill = kSurfaceColor;
    COLORREF border = kBorderColor;
    COLORREF text = kInkColor;
    if (highContrast)
    {
        const bool selected = selectedNavigation || pressed;
        fill = GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_BTNFACE);
        border = GetSysColor(COLOR_WINDOWTEXT);
        text = GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT);
    }
    else if (navigation)
    {
        fill = selectedNavigation ? kActiveNavColor : (hot ? RGB(238, 245, 253) : kCanvasColor);
        border = fill;
        text = selectedNavigation ? kPrimaryColor : kInkColor;
    }
    else if (primary)
    {
        fill = disabled ? RGB(177, 207, 237) : (pressed ? RGB(16, 94, 173) : kPrimaryColor);
        border = fill;
        text = RGB(255, 255, 255);
    }
    else if (add)
    {
        fill = pressed ? kActiveNavColor : (hot ? RGB(242, 248, 254) : kSurfaceColor);
        border = kPrimaryColor;
        text = kPrimaryColor;
    }
    else if (remove && !disabled)
    {
        fill = pressed ? RGB(252, 241, 241) : (hot ? RGB(255, 247, 247) : kSurfaceColor);
        border = RGB(200, 59, 59);
        text = RGB(166, 48, 48);
    }
    else if (hot)
    {
        fill = RGB(247, 250, 253);
    }
    if (disabled && !highContrast && !primary)
    {
        fill = RGB(247, 248, 250);
        border = kBorderColor;
        text = kMutedColor;
    }
    if (navigation)
    {
        FillSolid(draw->hDC, rectangle, fill);
        if (selectedNavigation)
        {
            RECT accent = rectangle;
            accent.right = accent.left + (editor ? editor->Px(3) : 3);
            FillSolid(draw->hDC, accent, highContrast ? GetSysColor(COLOR_HIGHLIGHTTEXT) : kPrimaryColor);
        }
    }
    else
    {
        const int radius = editor ? editor->Px(6) : 6;
        DrawRoundedSurface(draw->hDC, rectangle, fill, border, radius);
    }
    wchar_t label[128] = {};
    GetWindowTextW(draw->hwndItem, label, ARRAYSIZE(label));
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, disabled ? (highContrast ? GetSysColor(COLOR_GRAYTEXT) : kMutedColor) : text);
    HGDIOBJ oldFont = SelectObject(draw->hDC, reinterpret_cast<HFONT>(SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0)));
    RECT textRectangle = rectangle;
    if (navigation) textRectangle.left += editor ? editor->Px(14) : 14;
    DrawTextW(draw->hDC, label, -1, &textRectangle,
        DT_SINGLELINE | DT_VCENTER | (navigation ? DT_LEFT : DT_CENTER));
    SelectObject(draw->hDC, oldFont);
    if (focused)
    {
        RECT focus = rectangle;
        InflateRect(&focus, -2, -2);
        DrawFocusRect(draw->hDC, &focus);
    }
}

void DrawViewportListItem(DRAWITEMSTRUCT* draw, EditorWindow* editor)
{
    if (draw == nullptr || draw->itemID == static_cast<UINT>(-1)) return;
    const bool selected = (draw->itemState & ODS_SELECTED) != 0;
    const bool highContrast = IsHighContrastMode();
    FillSolid(draw->hDC, draw->rcItem, highContrast ?
        GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW) :
        (selected ? kActiveNavColor : kSurfaceColor));
    if (editor != nullptr && editor->viewportDragging)
    {
        const int insertion = editor->viewportDragInsertion;
        const int count = static_cast<int>(editor->viewportSections.size());
        if (insertion == static_cast<int>(draw->itemID) ||
            (insertion == count && draw->itemID + 1 == static_cast<UINT>(count)))
        {
            RECT marker = draw->rcItem;
            if (insertion == count) marker.top = marker.bottom - editor->Px(3);
            else marker.bottom = marker.top + editor->Px(3);
            FillSolid(draw->hDC, marker, highContrast ? GetSysColor(COLOR_HIGHLIGHT) : kPrimaryColor);
        }
    }
    const LRESULT textLength = SendMessageW(draw->hwndItem, LB_GETTEXTLEN,
        draw->itemID, 0);
    if (textLength == LB_ERR) return;
    std::wstring itemText(static_cast<size_t>(textLength) + 1, L'\0');
    SendMessageW(draw->hwndItem, LB_GETTEXT, draw->itemID,
        reinterpret_cast<LPARAM>(&itemText[0]));
    wchar_t* text = &itemText[0];
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, highContrast ? GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT) :
        (selected ? kPrimaryColor : kInkColor));
    RECT label = draw->rcItem;
    label.left += editor ? editor->Px(12) : 12;
    label.right -= editor ? editor->Px(8) : 8;
    HGDIOBJ oldFont = SelectObject(draw->hDC, reinterpret_cast<HFONT>(SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0)));
    wchar_t* summary = wcsstr(text, L"\r\n");
    if (summary != nullptr)
    {
        *summary = L'\0';
        summary += 2;
        RECT name = label;
        name.bottom = name.top + (editor ? editor->Px(23) : 23);
        DrawTextW(draw->hDC, text, -1, &name, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        HFONT detailFont = editor ? editor->smallFont : reinterpret_cast<HFONT>(SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0));
        SelectObject(draw->hDC, detailFont);
        SetTextColor(draw->hDC, highContrast ? GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_GRAYTEXT) :
            (selected ? kPrimaryColor : kMutedColor));
        RECT detail = label;
        detail.top += editor ? editor->Px(22) : 22;
        DrawTextW(draw->hDC, summary, -1, &detail, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }
    else
        DrawTextW(draw->hDC, text, -1, &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(draw->hDC, oldFont);
    if (draw->itemState & ODS_FOCUS) DrawFocusRect(draw->hDC, &draw->rcItem);
}

LRESULT CALLBACK CardProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    EditorWindow* editor = reinterpret_cast<EditorWindow*>(GetPropW(window, kEditorProperty));
    switch (message)
    {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT paint = {};
        HDC device = BeginPaint(window, &paint);
        RECT rectangle = {};
        GetClientRect(window, &rectangle);
        const bool highContrast = IsHighContrastMode();
        DrawRoundedSurface(device, rectangle,
            highContrast ? GetSysColor(COLOR_WINDOW) : kSurfaceColor,
            highContrast ? GetSysColor(COLOR_WINDOWTEXT) : kBorderColor,
            editor ? editor->Px(8) : 8);
        if (editor != nullptr && editor->validationControl != nullptr &&
            GetParent(editor->validationControl) == window)
        {
            RECT errorBounds = {};
            GetWindowRect(editor->validationControl, &errorBounds);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&errorBounds), 2);
            wchar_t controlClass[32] = {};
            GetClassNameW(editor->validationControl, controlClass, ARRAYSIZE(controlClass));
            if (_wcsicmp(controlClass, L"COMBOBOX") == 0)
                errorBounds.bottom = errorBounds.top + editor->Px(28);
            InflateRect(&errorBounds, editor->Px(2), editor->Px(2));
            HBRUSH errorBorder = CreateSolidBrush(highContrast ? GetSysColor(COLOR_HIGHLIGHT) : RGB(200, 59, 59));
            FrameRect(device, &errorBounds, errorBorder);
            DeleteObject(errorBorder);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        const bool highContrast = IsHighContrastMode();
        const bool enabled = reinterpret_cast<HWND>(lParam) == nullptr || IsWindowEnabled(reinterpret_cast<HWND>(lParam));
        SetTextColor(reinterpret_cast<HDC>(wParam), highContrast ?
            GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT) : (enabled ? kInkColor : kMutedColor));
        SetBkColor(reinterpret_cast<HDC>(wParam), highContrast ? GetSysColor(COLOR_WINDOW) : kSurfaceColor);
        return reinterpret_cast<LRESULT>(highContrast ? GetSysColorBrush(COLOR_WINDOW) : GetStockObject(WHITE_BRUSH));
    }
    case WM_CTLCOLOREDIT:
        if (editor != nullptr && reinterpret_cast<HWND>(lParam) == editor->validationControl && !IsHighContrastMode())
        {
            SetTextColor(reinterpret_cast<HDC>(wParam), RGB(166, 48, 48));
            SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 242, 242));
            return reinterpret_cast<LRESULT>(editor->validationBrush);
        }
    {
        const bool highContrast = IsHighContrastMode();
        const bool enabled = reinterpret_cast<HWND>(lParam) == nullptr || IsWindowEnabled(reinterpret_cast<HWND>(lParam));
        SetTextColor(reinterpret_cast<HDC>(wParam), highContrast ?
            GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT) : (enabled ? kInkColor : kMutedColor));
        SetBkColor(reinterpret_cast<HDC>(wParam), highContrast ? GetSysColor(COLOR_WINDOW) : kSurfaceColor);
        return reinterpret_cast<LRESULT>(highContrast ? GetSysColorBrush(COLOR_WINDOW) : GetStockObject(WHITE_BRUSH));
    }
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wParam), IsHighContrastMode() ? GetSysColor(COLOR_WINDOWTEXT) : kInkColor);
        SetBkColor(reinterpret_cast<HDC>(wParam), IsHighContrastMode() ? GetSysColor(COLOR_WINDOW) : kSurfaceColor);
        return reinterpret_cast<LRESULT>(IsHighContrastMode() ? GetSysColorBrush(COLOR_WINDOW) : GetStockObject(WHITE_BRUSH));
    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (draw->CtlID == kViewportSelector || draw->CtlID == kProfileSelector)
            DrawViewportListItem(draw, editor);
        else DrawOwnerButton(draw, editor);
        return TRUE;
    }
    case WM_COMMAND:
        SendMessageW(GetParent(GetParent(window)), WM_COMMAND, wParam, lParam);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK ContentProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    EditorWindow* editor = reinterpret_cast<EditorWindow*>(GetPropW(window, kEditorProperty));
    switch (message)
    {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT paint = {};
        HDC device = BeginPaint(window, &paint);
        RECT rectangle = {};
        GetClientRect(window, &rectangle);
        FillSolid(device, rectangle, IsHighContrastMode() ? GetSysColor(COLOR_WINDOW) : kCanvasColor);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_VSCROLL:
        if (editor) editor->ScrollContent(LOWORD(wParam), HIWORD(wParam));
        return 0;
    case WM_MOUSEWHEEL:
        if (editor) editor->ScrollContent(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? SB_LINEUP : SB_LINEDOWN, 0);
        return 0;
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), IsHighContrastMode() ? GetSysColor(COLOR_WINDOWTEXT) : kInkColor);
        SetBkColor(reinterpret_cast<HDC>(wParam), IsHighContrastMode() ? GetSysColor(COLOR_WINDOW) : kCanvasColor);
        return reinterpret_cast<LRESULT>(IsHighContrastMode() ? GetSysColorBrush(COLOR_WINDOW) : GetStockObject(WHITE_BRUSH));
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK ViewportListProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR)
{
    EditorWindow* editor = FindEditor(window);
    if (editor == nullptr) return DefSubclassProc(window, message, wParam, lParam);
    switch (message)
    {
    case WM_LBUTTONDOWN:
    {
        POINT point = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
        editor->BeginViewportDrag(point);
        break;
    }
    case WM_MOUSEMOVE:
        if (GetCapture() == window)
        {
            editor->UpdateViewportDrag({ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) });
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (GetCapture() == window)
        {
            editor->EndViewportDrag();
            return 0;
        }
        break;
    case WM_TIMER:
        if (wParam == kViewportDragTimer && editor->viewportDragging)
        {
            editor->UpdateViewportDrag(editor->viewportDragPoint);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && editor->viewportDragSource >= 0)
        {
            editor->CancelViewportDrag();
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && (wParam == VK_UP || wParam == VK_DOWN))
        {
            editor->MoveSelectedViewportBy(wParam == VK_UP ? -1 : 1);
            return 0;
        }
        break;
    case WM_CANCELMODE:
        if (editor->viewportDragSource >= 0) editor->CancelViewportDrag();
        break;
    case WM_CAPTURECHANGED:
        if (editor->viewportDragSource >= 0) editor->CancelViewportDrag();
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK ProfileListProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR)
{
    EditorWindow* editor = FindEditor(window);
    if (editor != nullptr && message == WM_KEYDOWN &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0 && (wParam == VK_UP || wParam == VK_DOWN))
    {
        editor->MoveSelectedProfileBy(wParam == VK_UP ? -1 : 1);
        return 0;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    EditorWindow* editor = GetEditor(window);
    switch (message)
    {
    case WM_NCCREATE:
        editor = reinterpret_cast<EditorWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        // WM_CREATE runs inside CreateWindowExW, before wWinMain can assign
        // editor.window.  The tray icon needs the real owner HWND at that point.
        editor->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editor));
        return TRUE;
    case WM_CREATE:
    {
        editor = GetEditor(window);
        editor->dpi = GetDpiForWindow(window);
        editor->backgroundBrush = CreateSolidBrush(kCanvasColor);
        editor->headerBrush = CreateSolidBrush(kHeaderColor);
        editor->validationBrush = CreateSolidBrush(RGB(255, 242, 242));
        editor->CreateFonts();
        HINSTANCE module = GetModuleHandleW(nullptr);
        editor->headerIcon = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ICON, 0, 0, 0, 0, window, nullptr, module, nullptr);
        SendMessageW(editor->headerIcon, STM_SETICON,
            reinterpret_cast<WPARAM>(LoadIconW(module, MAKEINTRESOURCEW(IDI_VIDEOPROCESSOR_CONFIG))), 0);
        editor->headerTitle = CreateWindowW(L"STATIC", L"VideoProcessor", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        editor->headerSubtitle = CreateWindowW(L"STATIC", L"Configuration", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        editor->summary = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_LEFT, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        editor->navigationCaption = CreateWindowW(L"STATIC", L"SETTINGS", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        const wchar_t* navigationLabels[] = {
            L"Startup", L"Queue", L"VP Renderer", L"Viewports", L"LLDV metadata", L"Shortcuts" };
        for (int index = 0; index < 6; ++index)
            editor->navigation[index] = CreateWindowW(L"BUTTON", navigationLabels[index], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNavigationStartup + index)), nullptr, nullptr);
        editor->pageTitle = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        editor->pageDescription = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        editor->contentHost = CreateWindowW(kContentClass, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
            0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        SetPropW(editor->contentHost, kEditorProperty, editor);
        auto createCard = [&]()
        {
            // Native drop-down combos use the child window height for their popup
            // list.  WS_CLIPCHILDREN would exclude that full (mostly hidden) area
            // from the card's background paint and produce selector/button ghosts.
            HWND card = CreateWindowW(kCardClass, L"", WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0, editor->contentHost, nullptr, nullptr, nullptr);
            SetPropW(card, kEditorProperty, editor);
            return card;
        };
        for (HWND& card : editor->cards) card = createCard();
        auto text = [](HWND parent, const wchar_t* value) {
            return CreateWindowW(L"STATIC", value, WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, parent, nullptr, nullptr, nullptr);
        };
        editor->cardTitles[0] = text(editor->cards[0], L"Hardware");
        editor->cardSubtitles[0] = text(editor->cards[0], L"Choose the capture device and renderer used when VP starts.");
        editor->cardTitles[1] = text(editor->cards[1], L"Queue");
        editor->cardSubtitles[1] = text(editor->cards[1], L"Frame buffering defaults used while VideoProcessor prepares output.");
        editor->cardTitles[4] = text(editor->cards[4], L"Profiles");
        editor->profileListHint = text(editor->cards[4], L"Choose a baseline or conditional named profile.");
        editor->cardTitles[3] = text(editor->cards[3], L"Profiles");
        editor->viewportListHint = text(editor->cards[3], L"Drag to reorder. The first profile is the default.");
        editor->cardTitles[5] = text(editor->cards[5], L"LLDV luminance overrides");
        editor->cardSubtitles[5] = text(editor->cards[5],
            L"Optional. Leave a value blank to use VP's normal metadata. These values apply only when LLDV input is recognized.");
        editor->cardTitles[6] = text(editor->cards[6], L"Fixed commands");
        editor->cardSubtitles[6] = text(editor->cards[6],
            L"Profile and shader shortcuts are edited with their owning settings. Returning a command to its shown default removes its override.");
        editor->cardTitles[7] = text(editor->cards[7], L"Window and fullscreen");
        editor->cardSubtitles[7] = text(editor->cards[7],
            L"Control startup window state and the display used for fullscreen playback.");
        editor->cardTitles[8] = text(editor->cards[8], L"Source analysis and HDR");
        editor->cardSubtitles[8] = text(editor->cards[8],
            L"Set startup metadata policies and source-detection behavior.");
        const struct { int control; const wchar_t* label; } startupChecks[] = {
            { 0, L"Start fullscreen" }, { 1, L"Use borderless windowed fullscreen" },
            { 2, L"Enable scene-aware timing" }, { 3, L"Hide legacy renderers" },
            { 4, L"Start minimized" }, { 20, L"Disable source detection features" },
            { 21, L"Use basic scene correction" },
            { 22, L"Use new LLDV detection heuristic" } };
        for (const auto& setting : startupChecks)
        {
            HWND parent = editor->cards[0];
            if (setting.control == 0 || setting.control == 1 || setting.control == 4)
                parent = editor->cards[7];
            else if (setting.control == 2 || setting.control == 20 ||
                setting.control == 21 || setting.control == 22)
                parent = editor->cards[8];
            editor->controls[setting.control] = CreateWindowW(L"BUTTON", setting.label, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlFirst + setting.control)), nullptr, nullptr);
        }
        const wchar_t* startupValueLabels[] = { L"Capture device", L"Renderer", L"Fullscreen monitor",
            L"Monitor session mode", L"HDR colorspace policy", L"HDR luminance policy" };
        for (int index = 0; index < 6; ++index)
        {
            HWND parent = index < 2 ? editor->cards[0] : (index < 4 ? editor->cards[7] : editor->cards[8]);
            editor->startupLabels[index] = text(parent, startupValueLabels[index]);
            const DWORD style = index < 3 ? CBS_DROPDOWN : CBS_DROPDOWNLIST;
            editor->startupFields[index] = CreateWindowW(L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | style, 0, 0, 0, 0,
                parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartupFieldFirst + index)), nullptr, nullptr);
            SetWindowTheme(editor->startupFields[index], L"Explorer", nullptr);
            editor->ConfigureOutputComboBox(editor->startupFields[index]);
        }
        for (const wchar_t* value : { L"Keep the existing desktop", L"Target monitor only" })
            SendMessageW(editor->startupFields[3], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        for (const wchar_t* value : { L"Follow input", L"Follow input (LLDV aware)", L"Follow container metadata",
            L"BT.2020", L"Display P3", L"Rec. 709" })
            SendMessageW(editor->startupFields[4], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        for (const wchar_t* value : { L"Follow input", L"Follow input (LLDV aware)", L"Use configured HDR luminance" })
            SendMessageW(editor->startupFields[5], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        const wchar_t* selectorValues[][10] = {
            { L"fast", L"balanced", L"high", nullptr },
            { L"AUTO", L"composed", L"direct", nullptr },
            { L"AUTO", L"full", L"limited", nullptr },
            { L"AUTO", L"bt1886", L"srgb", L"1.8", L"2.0", L"2.2", L"2.4", L"2.6", L"2.8", nullptr },
            { L"REC709", L"BT2020", nullptr },
            { L"AUTO", L"spline", L"bt2390", L"st2094-40", L"reinhard", nullptr },
            { L"AUTO", L"perceptual", L"softclip", L"relative", L"desaturate", nullptr },
            { L"AUTO", L"off", L"default", L"high_quality", L"on", nullptr },
            { L"AUTO", L"ewa_lanczossharp", L"ewa_lanczos", L"bicubic", L"bilinear", nullptr },
            { L"AUTO", L"ewa_lanczos", L"bicubic", L"bilinear", nullptr },
            { L"AUTO", L"on", L"off", nullptr },
            { L"AUTO", L"on", L"off", nullptr } };
        const wchar_t* outputLabels[] = {
            L"Quality", L"Presentation mode", L"Output range", L"Queue depth",
            L"Lead frames", L"Target frames", L"Output gamma", L"SDR primaries",
            L"Tone mapping", L"Gamut mapping", L"Peak detection", L"Upscaler",
            L"Downscaler", L"Debanding", L"Dithering" };
        for (int index = 0; index < 12; ++index)
        {
            editor->controls[5 + index] = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                0, 0, 0, 0, editor->cards[1], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlFirst + 5 + index)), nullptr, nullptr);
            SendMessageW(editor->controls[5 + index], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Inherited"));
            for (int option = 0; selectorValues[index][option]; ++option)
                SendMessageW(editor->controls[5 + index], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(selectorValues[index][option]));
            SetWindowTheme(editor->controls[5 + index], L"Explorer", nullptr);
            editor->ConfigureOutputComboBox(editor->controls[5 + index]);
        }
        for (int index = 17; index < 20; ++index)
        {
            editor->controls[index] = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                0, 0, 0, 0, editor->cards[1], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlFirst + index)), nullptr, nullptr);
            SetWindowTheme(editor->controls[index], L"Explorer", nullptr);
        }
        for (int index = 0; index < 15; ++index) editor->outputLabels[index] = text(editor->cards[1], outputLabels[index]);
        editor->outputUnits[0] = text(editor->cards[1], L"frames");
        editor->outputUnits[1] = text(editor->cards[1], L"frames");
        editor->outputUnits[2] = text(editor->cards[1], L"frames");
        editor->queueRecoveryTitle = text(editor->cards[1], L"Recovery safeguards");
        editor->queueRecoveryHelp = text(editor->cards[1],
            L"Optional startup safeguards for the default Queue profile. Blank uses VP's built-in value.");
        editor->queueRecoveryLabels[0] = text(editor->cards[1], L"Settle / re-prime delay");
        editor->queueRecoveryLabels[1] = text(editor->cards[1], L"Recovery high-water threshold");
        editor->queueRecoveryUnits[0] = text(editor->cards[1], L"seconds");
        editor->queueRecoveryUnits[1] = text(editor->cards[1], L"percent");
        for (int index = 0; index < 2; ++index)
        {
            editor->queueRecoveryFields[index] = CreateWindowW(L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                0, 0, 0, 0, editor->cards[1],
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kQueueRecoveryFieldFirst + index)),
                nullptr, nullptr);
            SetWindowTheme(editor->queueRecoveryFields[index], L"Explorer", nullptr);
            SendMessageW(editor->queueRecoveryFields[index], EM_SETCUEBANNER, TRUE,
                reinterpret_cast<LPARAM>(L"Use VP default"));
        }

        editor->addProfileButton = CreateWindowW(L"BUTTON", L"+ Add profile", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, editor->cards[4], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProfileAddButton)), nullptr, nullptr);
        editor->removeProfileButton = CreateWindowW(L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, editor->cards[4], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProfileRemoveButton)), nullptr, nullptr);
        editor->moveProfileUpButton = CreateWindowW(L"BUTTON", L"Move up", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, editor->cards[4], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProfileMoveUpButton)), nullptr, nullptr);
        editor->moveProfileDownButton = CreateWindowW(L"BUTTON", L"Move down", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, editor->cards[4], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProfileMoveDownButton)), nullptr, nullptr);
        editor->profileSelector = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
            0, 0, 0, 0, editor->cards[4], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProfileSelector)), nullptr, nullptr);
        SendMessageW(editor->profileSelector, LB_SETITEMHEIGHT, 0, editor->Px(48));
        SetWindowTheme(editor->profileSelector, L"Explorer", nullptr);
        SetWindowSubclass(editor->profileSelector, ProfileListProcedure, 1, 0);
        editor->profileNameLabel = text(editor->cards[1], L"Name");
        editor->profileNameEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE |
            WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, editor->cards[1],
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProfileName)), nullptr, nullptr);
        SetWindowTheme(editor->profileNameEdit, L"Explorer", nullptr);
        editor->profileRuleLabel = CreateWindowW(L"BUTTON", L"Use rule",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0, editor->cards[1],
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProfileRuleToggle)), nullptr, nullptr);
        editor->profileRule = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
            0, 0, 0, 0, editor->cards[1], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProfileRule)), nullptr, nullptr);
        editor->profileRuleHelp = text(editor->cards[1], L"Key and source-property conditions are supported.");
        editor->profileKeyLabel = text(editor->cards[1], L"Shortcut key");
        editor->profileKey = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 0, 0, editor->cards[1], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProfileKey)), nullptr, nullptr);
        editor->profileKeyHelp = text(editor->cards[1], L"Optional. Also activates this profile.");
        SetWindowTheme(editor->profileRule, L"Explorer", nullptr);
        SetWindowTheme(editor->profileKey, L"Explorer", nullptr);
        SendMessageW(editor->profileRule, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Example: ${transfer} == \"PQ\""));
        SendMessageW(editor->profileKey, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"e.g. Shift+N"));

        editor->addViewportButton = CreateWindowW(L"BUTTON", L"+ Add profile", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, editor->cards[3], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportAddButton)), nullptr, nullptr);
        editor->moveViewportUpButton = CreateWindowW(L"BUTTON", L"Move up", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, editor->cards[3], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportMoveUpButton)), nullptr, nullptr);
        editor->moveViewportDownButton = CreateWindowW(L"BUTTON", L"Move down", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, editor->cards[3], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportMoveDownButton)), nullptr, nullptr);
        editor->viewportSelector = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
            0, 0, 0, 0, editor->cards[3], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportSelector)), nullptr, nullptr);
        SendMessageW(editor->viewportSelector, LB_SETITEMHEIGHT, 0, editor->Px(48));
        SetWindowTheme(editor->viewportSelector, L"Explorer", nullptr);
        SetWindowSubclass(editor->viewportSelector, ViewportListProcedure, 1, 0);
        editor->viewportDetailTitle = text(editor->cards[2], L"Default viewport");
        editor->viewportDetailSubtitle = text(editor->cards[2], L"");
        editor->viewportNameLabel = text(editor->cards[2], L"Name");
        editor->viewportNameEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE |
            WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, editor->cards[2],
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportName)), nullptr, nullptr);
        SetWindowTheme(editor->viewportNameEdit, L"Explorer", nullptr);
        editor->nameViewportButton = CreateWindowW(L"BUTTON", L"Migrate legacy profile", WS_CHILD | BS_OWNERDRAW,
            0, 0, 0, 0, editor->cards[2], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportNameButton)), nullptr, nullptr);
        editor->removeViewportButton = CreateWindowW(L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, editor->cards[3], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportRemoveButton)), nullptr, nullptr);
        const wchar_t* viewportCopy[] = {
            L"Use rule",
            L"Matches source properties. Leave blank for the default profile.",
            L"Geometry",
            L"Screen aspect ratio",
            L"A ratio such as 16:9 or 2.35:1.",
            L"Anamorphic scale",
            L"Optional override. Unchecking restores the inherited 1:1 behavior.",
            L"Subtitles",
            L"Hold time",
            L"Release drift",
            L"Padding" };
        editor->viewportLabels[0] = CreateWindowW(L"BUTTON", viewportCopy[0],
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0, editor->cards[2],
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportRuleToggle)), nullptr, nullptr);
        for (int index = 1; index < 11; ++index) editor->viewportLabels[index] = text(editor->cards[2], viewportCopy[index]);
        editor->viewportRule = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
            0, 0, 0, 0, editor->cards[2], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportRule)), nullptr, nullptr);
        SetWindowTheme(editor->viewportRule, L"Explorer", nullptr);
        editor->viewportKeyLabel = text(editor->cards[2], L"Shortcut key");
        editor->viewportKey = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 0, 0, editor->cards[2], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportKey)), nullptr, nullptr);
        editor->viewportKeyHelp = text(editor->cards[2], L"Also activates this profile; for example F2 or Shift+N.");
        SetWindowTheme(editor->viewportKey, L"Explorer", nullptr);
        SendMessageW(editor->viewportKey, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"e.g. F2"));
        editor->viewportFields[0] = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 0, 0, editor->cards[2], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportFieldFirst)), nullptr, nullptr);
        editor->viewportFields[1] = CreateWindowW(L"BUTTON", L"Override anamorphic scale", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0, editor->cards[2], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportFieldFirst + 1)), nullptr, nullptr);
        editor->viewportFields[2] = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 0, 0, editor->cards[2], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportFieldFirst + 2)), nullptr, nullptr);
        editor->viewportFields[3] = CreateWindowW(L"BUTTON", L"Automatically crop black bars", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0, editor->cards[2], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportFieldFirst + 3)), nullptr, nullptr);
        editor->viewportFields[4] = CreateWindowW(L"BUTTON", L"Keep detected subtitles inside the viewport", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0, editor->cards[2], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportFieldFirst + 4)), nullptr, nullptr);
        for (int index = 5; index < 8; ++index)
        {
            editor->viewportFields[index] = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                0, 0, 0, 0, editor->cards[2], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewportFieldFirst + index)), nullptr, nullptr);
            SetWindowTheme(editor->viewportFields[index], L"Explorer", nullptr);
        }
        SetWindowTheme(editor->viewportFields[0], L"Explorer", nullptr);
        SetWindowTheme(editor->viewportFields[2], L"Explorer", nullptr);
        editor->viewportUnits[0] = text(editor->cards[2], L"seconds");
        editor->viewportUnits[1] = text(editor->cards[2], L"seconds");
        editor->viewportUnits[2] = text(editor->cards[2], L"pixels");
        SendMessageW(editor->viewportRule, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Example: ${width} >= 1920"));
        SendMessageW(editor->viewportFields[0], EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"16:9"));
        SendMessageW(editor->viewportFields[2], EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"1:1"));
        const wchar_t* lldvLabels[] = {
            L"Maximum content light level", L"Maximum frame-average light level",
            L"Mastering black level", L"Mastering peak luminance" };
        for (int index = 0; index < 4; ++index)
        {
            editor->lldvLabels[index] = text(editor->cards[1], lldvLabels[index]);
            editor->lldvFields[index] = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE |
                WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, editor->cards[1],
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLldvFieldFirst + index)), nullptr, nullptr);
            editor->lldvUnits[index] = text(editor->cards[1], L"nits");
            SetWindowTheme(editor->lldvFields[index], L"Explorer", nullptr);
            SendMessageW(editor->lldvFields[index], EM_SETCUEBANNER, TRUE,
                reinterpret_cast<LPARAM>(L"Use VP default"));
        }
        for (size_t index = 0; index < ARRAYSIZE(kShortcutFields); ++index)
        {
            editor->shortcutLabels[index] = text(editor->cards[6], kShortcutFields[index].label);
            editor->shortcutFields[index] = CreateWindowW(L"EDIT", kShortcutFields[index].defaultValue,
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0,
                editor->cards[6], reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShortcutFieldFirst + index)),
                nullptr, nullptr);
            SetWindowTheme(editor->shortcutFields[index], L"Explorer", nullptr);
        }
        editor->status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Validate", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kValidateButton)), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Reload", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReloadButton)), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Save changes", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSaveButton)), nullptr, nullptr);
        RECT client = {};
        GetClientRect(window, &client);
        editor->Layout(client.right - client.left, client.bottom - client.top);
        editor->AddTray();
        editor->ApplyFonts();
        editor->ShowPage(0);
        editor->SetDirty(false);
        return 0;
    }
    case WM_SIZE: if (editor) editor->Layout(LOWORD(lParam), HIWORD(lParam)); return 0;
    case WM_GETMINMAXINFO:
        if (editor) reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = { editor->Px(900), editor->Px(620) };
        return 0;
    case WM_COMMAND:
        if (!editor) break;
        if (LOWORD(wParam) == kSaveButton) { editor->Save(); return 0; }
        if (LOWORD(wParam) == kReloadButton) { editor->Load(editor->document.path); return 0; }
        if (LOWORD(wParam) == kValidateButton) { editor->Validate(); return 0; }
        if (LOWORD(wParam) == kViewportAddButton) { editor->AddViewport(); return 0; }
        if (LOWORD(wParam) == kViewportRemoveButton) { editor->RemoveViewport(); return 0; }
        if (LOWORD(wParam) == kViewportMoveUpButton) { editor->MoveSelectedViewportBy(-1); return 0; }
        if (LOWORD(wParam) == kViewportMoveDownButton) { editor->MoveSelectedViewportBy(1); return 0; }
        if (LOWORD(wParam) == kViewportNameButton) { editor->NameViewport(); return 0; }
        if (LOWORD(wParam) == kProfileAddButton) { editor->AddProfile(); return 0; }
        if (LOWORD(wParam) == kProfileRemoveButton) { editor->RemoveProfile(); return 0; }
        if (LOWORD(wParam) == kProfileRenameButton) { editor->RenameProfile(); return 0; }
        if (LOWORD(wParam) == kProfileMoveUpButton) { editor->MoveSelectedProfileBy(-1); return 0; }
        if (LOWORD(wParam) == kProfileMoveDownButton) { editor->MoveSelectedProfileBy(1); return 0; }
        if (LOWORD(wParam) == kProfileRuleToggle && HIWORD(wParam) == BN_CLICKED)
        {
            editor->SetProfileRuleVisible(editor->ProfileRuleVisible(), true);
            editor->SetDirty();
            return 0;
        }
        if (LOWORD(wParam) == kViewportRuleToggle && HIWORD(wParam) == BN_CLICKED)
        {
            editor->SetViewportRuleVisible(editor->ViewportRuleVisible(), true);
            editor->SetDirty();
            return 0;
        }
        if (LOWORD(wParam) == kNavigationStartup) { editor->ShowPage(0); return 0; }
        if (LOWORD(wParam) == kNavigationQueue) { editor->ShowPage(1); return 0; }
        if (LOWORD(wParam) == kNavigationRenderer) { editor->ShowPage(2); return 0; }
        if (LOWORD(wParam) == kNavigationViewports) { editor->ShowPage(3); return 0; }
        if (LOWORD(wParam) == kNavigationLldv) { editor->ShowPage(4); return 0; }
        if (LOWORD(wParam) == kNavigationShortcuts) { editor->ShowPage(5); return 0; }
        if (LOWORD(wParam) == kViewportSelector && HIWORD(wParam) == LBN_SELCHANGE)
        {
            editor->SelectViewport();
            return 0;
        }
        if (LOWORD(wParam) == kProfileSelector && HIWORD(wParam) == LBN_SELCHANGE)
        {
            editor->SelectProfile();
            return 0;
        }
        if (LOWORD(wParam) == kViewportFieldFirst + 1 && HIWORD(wParam) == BN_CLICKED)
        {
            const BOOL enabled = SendMessageW(editor->viewportFields[1], BM_GETCHECK, 0, 0) == BST_CHECKED ? TRUE : FALSE;
            EnableWindow(editor->viewportFields[2], enabled);
            // Repaint the enabled state immediately so the ratio changes from
            // disabled gray to the normal active black text without waiting for
            // another expose or scroll event.
            RedrawWindow(editor->viewportFields[2], nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
            editor->SetDirty();
            return 0;
        }
        if ((LOWORD(wParam) == kControlFirst || LOWORD(wParam) == kControlFirst + 20) &&
            HIWORD(wParam) == BN_CLICKED)
        {
            editor->UpdateStartupControlState();
            editor->SetDirty();
            return 0;
        }
        if (LOWORD(wParam) == kControlFirst + 3 && HIWORD(wParam) == BN_CLICKED)
        {
            const bool hideLegacy = SendMessageW(editor->controls[3], BM_GETCHECK, 0, 0) == BST_CHECKED;
            editor->RefreshRendererChoices(hideLegacy);
            editor->SetDirty();
            return 0;
        }
        if ((LOWORD(wParam) >= kControlFirst && LOWORD(wParam) < kControlFirst + 23) ||
            LOWORD(wParam) == kViewportRule ||
            LOWORD(wParam) == kProfileRule || LOWORD(wParam) == kProfileKey ||
            LOWORD(wParam) == kProfileName || LOWORD(wParam) == kViewportKey ||
            LOWORD(wParam) == kViewportName ||
            (LOWORD(wParam) >= kLldvFieldFirst && LOWORD(wParam) < kLldvFieldFirst + 4) ||
            (LOWORD(wParam) >= kShortcutFieldFirst &&
                LOWORD(wParam) < kShortcutFieldFirst + ARRAYSIZE(kShortcutFields)) ||
            (LOWORD(wParam) >= kQueueRecoveryFieldFirst &&
                LOWORD(wParam) < kQueueRecoveryFieldFirst + 2) ||
            (LOWORD(wParam) >= kStartupFieldFirst &&
                LOWORD(wParam) < kStartupFieldFirst + 6) ||
            (LOWORD(wParam) >= kViewportFieldFirst && LOWORD(wParam) < kViewportFieldFirst + 8))
        {
            if (HIWORD(wParam) == EN_CHANGE || HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == BN_CLICKED)
                editor->SetDirty();
            return 0;
        }
        break;
    case WM_DRAWITEM:
        DrawOwnerButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam), editor);
        return TRUE;
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
        if (editor)
        {
            InvalidateRect(window, nullptr, TRUE);
            EnumChildWindows(window, [](HWND child, LPARAM) -> BOOL { InvalidateRect(child, nullptr, TRUE); return TRUE; }, 0);
        }
        return 0;
    case WM_DPICHANGED:
        if (editor)
        {
            editor->dpi = HIWORD(wParam);
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            editor->CreateFonts();
            editor->ApplyFonts();
            for (int index = 5; index < 17; ++index)
                editor->ConfigureOutputComboBox(editor->controls[index]);
            SendMessageW(editor->viewportSelector, LB_SETITEMHEIGHT, 0, editor->Px(48));
            RECT client = {}; GetClientRect(window, &client);
            editor->Layout(client.right - client.left, client.bottom - client.top);
        }
        return 0;
    case kTrayMessage:
        if (editor)
        {
            const UINT trayEvent = LOWORD(static_cast<DWORD>(lParam));
            if (trayEvent == WM_LBUTTONUP || trayEvent == WM_LBUTTONDBLCLK || trayEvent == NIN_SELECT)
            {
                editor->Open();
                return 0;
            }
            if (trayEvent == WM_RBUTTONUP || trayEvent == WM_CONTEXTMENU || trayEvent == NIN_KEYSELECT)
            {
                editor->ShowTrayMenu();
                return 0;
            }
        }
        break;
    case WM_CTLCOLORSTATIC:
        if (editor)
        {
            const bool highContrast = IsHighContrastMode();
            if (reinterpret_cast<HWND>(lParam) == editor->headerTitle ||
                reinterpret_cast<HWND>(lParam) == editor->headerSubtitle)
            {
                SetTextColor(reinterpret_cast<HDC>(wParam), highContrast ? GetSysColor(COLOR_CAPTIONTEXT) : RGB(255, 255, 255));
                SetBkColor(reinterpret_cast<HDC>(wParam), highContrast ? GetSysColor(COLOR_ACTIVECAPTION) : kHeaderColor);
                return reinterpret_cast<LRESULT>(highContrast ? GetSysColorBrush(COLOR_ACTIVECAPTION) : editor->headerBrush);
            }
            SetTextColor(reinterpret_cast<HDC>(wParam), highContrast ? GetSysColor(COLOR_WINDOWTEXT) : kInkColor);
            SetBkColor(reinterpret_cast<HDC>(wParam), highContrast ? GetSysColor(COLOR_WINDOW) : kCanvasColor);
            return reinterpret_cast<LRESULT>(highContrast ? GetSysColorBrush(COLOR_WINDOW) : editor->backgroundBrush);
        }
        break;
    case WM_ERASEBKGND:
        if (editor)
        {
            const bool highContrast = IsHighContrastMode();
            RECT rectangle; GetClientRect(window, &rectangle);
            FillRect(reinterpret_cast<HDC>(wParam), &rectangle,
                highContrast ? GetSysColorBrush(COLOR_WINDOW) : editor->backgroundBrush);
            rectangle.bottom = editor->Px(64);
            FillRect(reinterpret_cast<HDC>(wParam), &rectangle,
                highContrast ? GetSysColorBrush(COLOR_ACTIVECAPTION) : editor->headerBrush);
            RECT footer = {};
            GetClientRect(window, &footer);
            footer.top = std::max(0, static_cast<int>(footer.bottom) - editor->Px(64));
            footer.bottom = footer.top + 1;
            FillSolid(reinterpret_cast<HDC>(wParam), footer,
                highContrast ? GetSysColor(COLOR_WINDOWTEXT) : kBorderColor);
            return 1;
        }
        break;
    case WM_CLOSE: if (editor) editor->ShowCloseTip(); ShowWindow(window, SW_HIDE); return 0;
    case WM_DESTROY:
        if (editor)
        {
            editor->RemoveTray();
            DeleteObject(editor->backgroundBrush);
            DeleteObject(editor->headerBrush);
            DeleteObject(editor->validationBrush);
            DeleteObject(editor->bodyFont);
            DeleteObject(editor->smallFont);
            DeleteObject(editor->headingFont);
            DeleteObject(editor->captionFont);
            DeleteObject(editor->sectionFont);
        }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT comInitialization = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND existing = FindWindowW(kWindowClass, nullptr);
        if (existing) { ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); }
        if (mutex) CloseHandle(mutex);
        if (SUCCEEDED(comInitialization)) CoUninitialize();
        return 0;
    }
    INITCOMMONCONTROLSEX common = { sizeof(common), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&common);
    WNDCLASSEXW windowClass = { sizeof(windowClass) };
    windowClass.hInstance = instance; windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_VIDEOPROCESSOR_CONFIG));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass; windowClass.lpfnWndProc = WindowProcedure;
    RegisterClassExW(&windowClass);
    WNDCLASSEXW cardClass = { sizeof(cardClass) };
    cardClass.hInstance = instance; cardClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cardClass.lpszClassName = kCardClass; cardClass.lpfnWndProc = CardProcedure;
    RegisterClassExW(&cardClass);
    WNDCLASSEXW contentClass = { sizeof(contentClass) };
    contentClass.hInstance = instance; contentClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    contentClass.lpszClassName = kContentClass; contentClass.lpfnWndProc = ContentProcedure;
    RegisterClassExW(&contentClass);
    std::wstring configPath;
    bool hasExplicitConfigPath = false;
    HWND owner = nullptr;
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    for (int index = 1; arguments != nullptr && index < argumentCount; ++index)
    {
        const std::wstring token(arguments[index]);
        if (token == L"--config" && index + 1 < argumentCount)
        {
            configPath = arguments[++index];
            hasExplicitConfigPath = true;
        }
        else if (token == L"--owner" && index + 1 < argumentCount) owner = reinterpret_cast<HWND>(_wcstoui64(arguments[++index], nullptr, 10));
    }
    if (arguments != nullptr) LocalFree(arguments);
    if (!hasExplicitConfigPath) configPath = DirectLaunchConfigPath();
    EditorWindow editor;
    const UINT launchDpi = GetDpiForSystem();
    RECT initialClient = { 0, 0, MulDiv(960, static_cast<int>(launchDpi), 96),
        MulDiv(640, static_cast<int>(launchDpi), 96) };
    constexpr DWORD editorExtendedStyle = WS_EX_APPWINDOW | WS_EX_COMPOSITED;
    constexpr DWORD editorWindowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN;
    AdjustWindowRectExForDpi(&initialClient, editorWindowStyle, FALSE, editorExtendedStyle, launchDpi);
    // Classic Win32 child controls otherwise paint directly to the screen one
    // after another during live resize.  Composite the complete hierarchy into
    // an off-screen surface so a finished frame is presented atomically.
    HWND window = CreateWindowExW(editorExtendedStyle, kWindowClass, L"VideoProcessor Configuration", editorWindowStyle,
        CW_USEDEFAULT, CW_USEDEFAULT, initialClient.right - initialClient.left,
        initialClient.bottom - initialClient.top, owner, nullptr, instance, &editor);
    editor.window = window;
    if (window != nullptr)
    {
        RECT bounds = {};
        MONITORINFO monitor = { sizeof(monitor) };
        GetWindowRect(window, &bounds);
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        const RECT& work = monitor.rcWork;
        const int x = std::max(work.left, work.left + ((work.right - work.left) - width) / 2);
        const int y = std::max(work.top, work.top + ((work.bottom - work.top) - height) / 2);
        SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    editor.Load(configPath);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    if (SUCCEEDED(comInitialization)) CoUninitialize();
    return 0;
}
