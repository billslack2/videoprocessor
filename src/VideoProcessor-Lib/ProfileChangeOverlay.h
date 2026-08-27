#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>


namespace ProfileChangeOverlay
{
	constexpr unsigned int DefaultDisplaySeconds = 5;
	constexpr unsigned int MaximumDisplaySeconds = 60;
	constexpr unsigned long long FadeMilliseconds = 875;

	struct Timing
	{
		unsigned long long totalMilliseconds = 0;
		unsigned long long holdMilliseconds = 0;
		unsigned long long fadeMilliseconds = 0;

		bool Enabled() const { return totalMilliseconds != 0; }
	};

	inline Timing ResolveTiming(unsigned int seconds)
	{
		const unsigned long long total =
			static_cast<unsigned long long>(seconds) * 1000;
		const unsigned long long fade = std::min(total, FadeMilliseconds);
		return { total, total - fade, fade };
	}

	struct Item
	{
		std::string group;
		std::string label;
		std::string value;
	};

	inline std::string GroupLabel(const std::string& group)
	{
		if (group == "display") return "Rendering";
		if (group == "color") return "Color";
		if (group == "output") return "Output";
		if (group == "viewport") return "Screen";
		if (group == "input") return "Input";
		if (group == "scaling") return "Scaling";
		if (group == "queue") return "Queue";
		if (group == "lldv") return "LLDV";
		if (group.empty()) return "Profile";
		std::string label = group;
		label[0] = static_cast<char>(std::toupper(
			static_cast<unsigned char>(label[0])));
		return label;
	}

	inline std::string ProfileLabel(const std::string& profile)
	{
		std::string expanded;
		expanded.reserve(profile.size());
		for (size_t index = 0; index < profile.size(); ++index)
		{
			if (profile[index] != '_')
			{
				expanded.push_back(profile[index]);
				continue;
			}
			if (index + 1 < profile.size() && profile[index + 1] == '_')
			{
				expanded.push_back('_');
				++index;
			}
			else
				expanded.push_back(' ');
		}

		std::string result;
		for (size_t start = 0; start < expanded.size();)
		{
			const size_t end = expanded.find(' ', start);
			std::string word = expanded.substr(start,
				end == std::string::npos ? std::string::npos : end - start);
			std::string normalized = word;
			std::transform(normalized.begin(), normalized.end(),
				normalized.begin(), [](unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});
			if (normalized == "rec709") word = "Rec709";
			else if (normalized == "bt2020") word = "BT2020";
			else if (normalized == "vp") word = "VP";
			else if (normalized == "lldv") word = "LLDV";
			else if (normalized == "hdr") word = "HDR";
			else if (normalized == "sdr") word = "SDR";
			else if (normalized == "pq") word = "PQ";
			else if (!word.empty())
				word[0] = static_cast<char>(std::toupper(
					static_cast<unsigned char>(word[0])));
			if (!result.empty()) result.push_back(' ');
			result += word;
			if (end == std::string::npos) break;
			start = end + 1;
		}
		return result.empty() ? profile : result;
	}

	inline std::vector<Item> CollectChanges(
		const std::map<std::string, std::string>& previous,
		const std::map<std::string, std::string>& current,
		const std::string& visibleScreenName = {})
	{
		static const std::vector<std::string> preferredOrder = {
			"display", "color", "output", "viewport", "input", "scaling",
			"queue", "lldv"
		};
		std::set<std::string> changed;
		for (const auto& selection : current)
		{
			const auto old = previous.find(selection.first);
			if (old == previous.end() || old->second != selection.second)
				changed.insert(selection.first);
		}
		for (const auto& selection : previous)
			if (current.find(selection.first) == current.end())
				changed.insert(selection.first);

		std::vector<std::string> ordered;
		for (const std::string& group : preferredOrder)
			if (changed.erase(group) != 0) ordered.push_back(group);
		ordered.insert(ordered.end(), changed.begin(), changed.end());

		std::vector<Item> items;
		for (const std::string& group : ordered)
		{
			const auto selection = current.find(group);
			if (selection == current.end()) continue;
			Item item{ group, GroupLabel(group), ProfileLabel(selection->second) };
			if (group == "viewport" && !visibleScreenName.empty())
				item.value = visibleScreenName;
			items.push_back(std::move(item));
		}
		return items;
	}
}
