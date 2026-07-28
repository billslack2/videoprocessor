#pragma once

#include "ConfigFile.h"
#include "DisplayRuleExpression.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

// Shared, side-effect-free schema validation for the application and renderer
// sections in VideoProcessor.cfg.
// This deliberately validates configuration values only; it does not apply
// settings or permit runtime mutation.
namespace ConfigSchema
{
	using Validator = std::function<bool(const std::string&)>;

	struct KeyRule
	{
		std::string key;
		Validator validator;
		std::string expected;
	};

	inline KeyRule Any(const char* key)
	{
		return { key, {}, "a value" };
	}

	inline KeyRule Boolean(const char* key)
	{
		return {
			key,
			[](const std::string& value)
			{
				const std::string normalized = ConfigFile::NormalizeName(value);
				return normalized == "1" || normalized == "0" ||
					normalized == "true" || normalized == "false" ||
					normalized == "yes" || normalized == "no" ||
					normalized == "on" || normalized == "off";
			},
			"true or false"
		};
	}

	inline KeyRule Integer(const char* key, int minimum, int maximum)
	{
		return {
			key,
			[minimum, maximum](const std::string& value)
			{
				try
				{
					const std::string trimmed = ConfigFile::Trim(value);
					size_t consumed = 0;
					const long long parsed = std::stoll(trimmed, &consumed);
					return consumed == trimmed.size() &&
						parsed >= minimum && parsed <= maximum;
				}
				catch (const std::exception&) { return false; }
			},
			"an integer from " + std::to_string(minimum) +
				" to " + std::to_string(maximum)
		};
	}

	inline KeyRule Number(const char* key, double minimum, double maximum,
		bool minimumInclusive = true)
	{
		return {
			key,
			[minimum, maximum, minimumInclusive](const std::string& value)
			{
				double parsed = 0.0;
				return DisplayRuleExpression::ParseNumber(
						ConfigFile::Trim(value), parsed) &&
					std::isfinite(parsed) &&
					(minimumInclusive ? parsed >= minimum : parsed > minimum) &&
					parsed <= maximum;
			},
			"a finite number " + std::string(minimumInclusive ? "from " : "greater than ") +
				std::to_string(minimum) + " through " + std::to_string(maximum)
		};
	}

	inline KeyRule NumberAtLeast(const char* key, double minimum,
		bool inclusive = true)
	{
		return {
			key,
			[minimum, inclusive](const std::string& value)
			{
				double parsed = 0.0;
				return DisplayRuleExpression::ParseNumber(
						ConfigFile::Trim(value), parsed) &&
					std::isfinite(parsed) &&
					(inclusive ? parsed >= minimum : parsed > minimum);
			},
			"a finite number " + std::string(inclusive ? "at least " : "greater than ") +
				std::to_string(minimum)
		};
	}

	inline KeyRule Choice(const char* key,
		std::initializer_list<const char*> choices)
	{
		std::vector<std::string> normalizedChoices;
		std::string expected;
		for (const char* choice : choices)
		{
			normalizedChoices.push_back(ConfigFile::NormalizeName(choice));
			if (!expected.empty()) expected += ", ";
			expected += choice;
		}
		return {
			key,
			[normalizedChoices](const std::string& value)
			{
				const std::string normalized = ConfigFile::NormalizeName(value);
				return std::find(normalizedChoices.begin(), normalizedChoices.end(),
					normalized) != normalizedChoices.end();
			},
			"one of: " + expected
		};
	}

	inline bool ValidateSection(const ConfigFile& config,
		const std::string& section, const std::vector<KeyRule>& rules,
		std::string& error)
	{
		const auto* values = config.GetSectionValues(section);
		if (!values) return true;
		for (const auto& value : *values)
		{
			const auto rule = std::find_if(rules.begin(), rules.end(),
				[&value](const KeyRule& candidate)
				{ return candidate.key == value.first; });
			if (rule == rules.end())
			{
				error = "[" + section + "] unknown key '" + value.first + "'";
				return false;
			}
			if (rule->validator && !rule->validator(value.second))
			{
				error = "[" + section + "] key '" + value.first +
					"' must be " + rule->expected + "; got '" + value.second + "'";
				return false;
			}
		}
		return true;
	}
}
