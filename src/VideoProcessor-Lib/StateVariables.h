#pragma once

#include "AspectRatio.h"
#include "ConfigFile.h"
#include "DisplayRuleExpression.h"
#include "VideoState.h"

#include <cstdint>
#include <map>
#include <string>


namespace StateVariables
{
	enum class ValueType
	{
		Text,
		Number,
		Boolean,
		Aspect
	};

	// C++14-compatible typed value. Only the field identified by type is
	// authoritative. Factory methods keep construction explicit at publishers.
	struct Value
	{
		ValueType type = ValueType::Text;
		std::string text;
		double number = 0.0;
		bool boolean = false;
		AspectRatio aspect{ 1, 1, 1.0 };

		static Value Text(const std::string& value);
		static Value Number(double value);
		static Value Boolean(bool value);
		static Value Aspect(const AspectRatio& value);

		// Adapter for the existing expression evaluator. Consumers that support
		// typed values should use Find() and avoid the string round trip.
		bool ToExpressionText(std::string& value) const;
	};

	class Snapshot
	{
	public:
		Snapshot() = default;
		Snapshot(uint64_t generation,
			const std::map<std::string, Value>& values);

		uint64_t Generation() const { return m_generation; }
		const std::map<std::string, Value>& Values() const { return m_values; }
		const Value* Find(const std::string& name) const;
		bool Lookup(const std::string& name, std::string& value) const;
		DisplayRuleExpression::ValueLookup ExpressionLookup() const;

	private:
		uint64_t m_generation = 0;
		std::map<std::string, Value> m_values;
	};

	// Shared publication boundary for capture/video state. Keeping this mapping
	// here prevents renderer backends and the application from inventing
	// different names or string representations for the same state.
	bool LookupVideoState(const VideoState& state,
		const std::string& name, std::string& value);
	DisplayRuleExpression::ValueLookup VideoStateLookup(
		const VideoStateComPtr& state);
}
