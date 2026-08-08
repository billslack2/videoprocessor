#pragma once

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <string>
#include <vector>

namespace ApplicationInterface
{
	enum class Mode
	{
		None,
		Classic,
		Modern
	};

	enum class Source
	{
		NoUi,
		CommandLine,
		Configuration,
		DefaultModern
	};

	struct Preference
	{
		bool specified = false;
		bool valid = false;
		Mode mode = Mode::Classic;
		std::string error;
	};

	struct Selection
	{
		Mode mode = Mode::Modern;
		Source source = Source::DefaultModern;
		std::string warning;
	};

	inline std::string Normalize(const std::string& raw)
	{
		auto first = std::find_if_not(raw.begin(), raw.end(), [](unsigned char value)
			{ return std::isspace(value) != 0; });
		auto last = std::find_if_not(raw.rbegin(), raw.rend(), [](unsigned char value)
			{ return std::isspace(value) != 0; }).base();
		if (first >= last)
			return {};

		std::string normalized(first, last);
		std::transform(normalized.begin(), normalized.end(), normalized.begin(),
			[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
		return normalized;
	}

	inline Preference ParsePreference(bool specified, const std::string& raw,
		const char* sourceName)
	{
		Preference result;
		result.specified = specified;
		if (!specified)
			return result;

		const std::string normalized = Normalize(raw);
		if (normalized == "classic")
		{
			result.valid = true;
			result.mode = Mode::Classic;
			return result;
		}
		if (normalized == "modern")
		{
			result.valid = true;
			result.mode = Mode::Modern;
			return result;
		}

		result.error = std::string("Invalid ") + sourceName +
			" interface value '" + raw + "' (expected classic or modern)";
		return result;
	}

	inline bool IsInterfaceOption(const std::wstring& argument)
	{
		return _wcsicmp(argument.c_str(), L"/interface") == 0;
	}

	inline bool IsSwitch(const std::wstring& argument)
	{
		return !argument.empty() && (argument.front() == L'/' || argument.front() == L'-');
	}

	inline std::string NarrowAscii(const std::wstring& value)
	{
		std::string result;
		result.reserve(value.size());
		for (wchar_t character : value)
			result.push_back(character <= 0x7f ? static_cast<char>(character) : '?');
		return result;
	}

	inline Preference ParseCommandLine(const std::vector<std::wstring>& arguments)
	{
		Preference result;
		for (size_t index = 1; index < arguments.size(); ++index)
		{
			const std::wstring& argument = arguments[index];
			if (!IsInterfaceOption(argument))
				continue;

			if (result.specified)
			{
				result.valid = false;
				result.error = "Invalid /interface: option specified more than once";
				return result;
			}

			result.specified = true;
			if (index + 1 >= arguments.size() || IsSwitch(arguments[index + 1]))
			{
				result.error = "Invalid /interface: missing value (expected classic or modern)";
				return result;
			}

			result = ParsePreference(true, NarrowAscii(arguments[++index]), "/interface");
		}
		return result;
	}

	inline void AppendWarning(std::string& warning, const std::string& value)
	{
		if (value.empty())
			return;
		if (!warning.empty())
			warning += "; ";
		warning += value;
	}

	inline Selection Resolve(bool noUi, const Preference& commandLine,
		const Preference& configuration)
	{
		Selection result;
		AppendWarning(result.warning, commandLine.error);
		AppendWarning(result.warning, configuration.error);

		if (noUi)
		{
			result.mode = Mode::None;
			result.source = Source::NoUi;
			return result;
		}
		if (commandLine.specified && commandLine.valid)
		{
			result.mode = commandLine.mode;
			result.source = Source::CommandLine;
			return result;
		}
		if (configuration.specified && configuration.valid)
		{
			result.mode = configuration.mode;
			result.source = Source::Configuration;
			return result;
		}

		result.mode = Mode::Modern;
		result.source = Source::DefaultModern;
		return result;
	}

	inline const char* ModeName(Mode mode)
	{
		switch (mode)
		{
		case Mode::None: return "none";
		case Mode::Modern: return "modern";
		default: return "classic";
		}
	}

	inline const char* SourceName(Source source)
	{
		switch (source)
		{
		case Source::NoUi: return "noui";
		case Source::CommandLine: return "command-line";
		case Source::Configuration: return "configuration";
		default: return "default";
		}
	}
}
