#include <pch.h>

#include "StateVariables.h"

#include <StringUtils.h>

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>


namespace StateVariables
{
	Value Value::Text(const std::string& value)
	{
		Value result;
		result.type = ValueType::Text;
		result.text = value;
		return result;
	}


	Value Value::Number(double value)
	{
		Value result;
		result.type = ValueType::Number;
		result.number = value;
		return result;
	}


	Value Value::Boolean(bool value)
	{
		Value result;
		result.type = ValueType::Boolean;
		result.boolean = value;
		return result;
	}


	Value Value::Aspect(const AspectRatio& value)
	{
		Value result;
		result.type = ValueType::Aspect;
		result.aspect = value;
		result.number = value.value;
		return result;
	}


	bool Value::ToExpressionText(std::string& value) const
	{
		switch (type)
		{
		case ValueType::Text:
			value = text;
			return true;
		case ValueType::Boolean:
			value = boolean ? "true" : "false";
			return true;
		case ValueType::Aspect:
			if (!aspect.IsValid())
				return false;
			{
				// The legacy expression adapter expects a numeric value. Typed
				// consumers retain the exact normalized ratio through Find().
				std::ostringstream stream;
				stream.imbue(std::locale::classic());
				stream << std::setprecision(17) << aspect.value;
				value = stream.str();
			}
			return true;
		case ValueType::Number:
			if (!std::isfinite(number))
				return false;
			{
				std::ostringstream stream;
				stream.imbue(std::locale::classic());
				stream << std::setprecision(17) << number;
				value = stream.str();
			}
			return true;
		default:
			return false;
		}
	}


	Snapshot::Snapshot(uint64_t generation,
		const std::map<std::string, Value>& values):
		m_generation(generation),
		m_values(values)
	{
	}


	const Value* Snapshot::Find(const std::string& name) const
	{
		std::string normalized = ConfigFile::NormalizeName(name);
		if (!normalized.empty() && normalized.front() == '$')
			normalized.erase(0, 1);
		const auto value = m_values.find(normalized);
		return value == m_values.end() ? nullptr : &value->second;
	}


	bool Snapshot::Lookup(const std::string& name, std::string& value) const
	{
		const Value* found = Find(name);
		return found && found->ToExpressionText(value);
	}


	DisplayRuleExpression::ValueLookup Snapshot::ExpressionLookup() const
	{
		// A Snapshot is immutable. Callers must keep it alive for as long as the
		// returned lookup is used.
		return [this](const std::string& name, std::string& value)
		{
			return Lookup(name, value);
		};
	}


	bool LookupVideoState(const VideoState& state,
		const std::string& name, std::string& value)
	{
		if (name == "eotf" || name == "transfer")
		{
			switch (state.eotf)
			{
			case EOTF::SDR: value = "sdr"; break;
			case EOTF::HDR: value = "hdr"; break;
			case EOTF::PQ: value = "pq"; break;
			case EOTF::HLG: value = "hlg"; break;
			default: value = "unknown"; break;
			}
			return true;
		}
		if (name == "colorspace" || name == "primaries")
		{
			switch (state.colorspace)
			{
			case ColorSpace::REC_601_525: value = "rec601_525"; break;
			case ColorSpace::REC_601_576:
			case ColorSpace::REC_601_625: value = "rec601_625"; break;
			case ColorSpace::REC_709: value = "rec709"; break;
			case ColorSpace::P3_D65: value = "p3_d65"; break;
			case ColorSpace::P3_DCI: value = "p3_dci"; break;
			case ColorSpace::P3_D60: value = "p3_d60"; break;
			case ColorSpace::BT_2020: value = "bt2020"; break;
			default: value = "unknown"; break;
			}
			return true;
		}
		if (name == "format")
		{
			value = CStringA(ToString(state.videoFrameEncoding)).GetString();
			return true;
		}
		if (name == "hdr_metadata")
		{
			value = state.hdrData && state.hdrData->IsValid()
				? "true" : "false";
			return true;
		}
		if (!state.displayMode)
			return false;
		if (name == "interlaced")
			value = state.displayMode->IsInterlaced()
				? "true" : "false";
		else if (name == "scan")
			value = state.displayMode->IsInterlaced()
				? "interlaced" : "progressive";
		else if (name == "source_rate")
			value = std::to_string(static_cast<int>(
				std::floor(state.displayMode->RefreshRateHz())));
		else if (name == "cadence")
		{
			std::ostringstream cadence;
			cadence.imbue(std::locale::classic());
			cadence.precision(17);
			cadence << state.displayMode->RefreshRateHz();
			value = cadence.str();
		}
		else if (name == "width")
			value = std::to_string(state.displayMode->FrameWidth());
		else if (name == "height")
			value = std::to_string(state.displayMode->FrameHeight());
		else if (name == "resolution")
			value = std::to_string(state.displayMode->FrameWidth()) + "x" +
				std::to_string(state.displayMode->FrameHeight());
		else
			return false;
		return true;
	}


	DisplayRuleExpression::ValueLookup VideoStateLookup(
		const VideoStateComPtr& state)
	{
		return [state](const std::string& name, std::string& value)
		{
			return state && LookupVideoState(*state, name, value);
		};
	}
}
