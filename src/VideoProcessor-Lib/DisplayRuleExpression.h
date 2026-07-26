#pragma once

#include "ConfigFile.h"

#include <cctype>
#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

// Small, deliberately restricted expression language for display rules. It is
// shared by startup validation and runtime selection so a rule cannot validate
// successfully and then be interpreted differently by the renderer.
namespace DisplayRuleExpression
{
	enum class ValueType
	{
		Text,
		Number,
		Boolean
	};

	using ValueLookup = std::function<bool(const std::string& name, std::string& value)>;

	inline bool GetVariableType(const std::string& name, ValueType& type)
	{
		// Keep rule conditions independent of their source.  `$key` is empty
		// during ordinary source evaluation and contains the canonical shortcut
		// chord only while processing a configured shortcut.
		if (name == "eotf" || name == "transfer" || name == "colorspace" ||
			name == "primaries" || name == "format" || name == "resolution" ||
			name == "range" || name == "scan" || name == "key")
		{
			type = ValueType::Text;
			return true;
		}
		if (name == "hdr_metadata" || name == "interlaced")
		{
			type = ValueType::Boolean;
			return true;
		}
		if (name == "source_rate" || name == "width" || name == "height")
		{
			type = ValueType::Number;
			return true;
		}
		return false;
	}

	inline bool ParseNumber(const std::string& text, double& value)
	{
		try
		{
			size_t consumed = 0;
			value = std::stod(text, &consumed);
			return consumed == text.size() && std::isfinite(value);
		}
		catch (const std::exception&)
		{
			return false;
		}
	}

	inline bool ParseNumberOrRange(const std::string& text, double& minimum, double& maximum)
	{
		const size_t separator = text.find('-', 1);
		if (separator == std::string::npos)
		{
			if (!ParseNumber(text, minimum)) return false;
			maximum = minimum;
			return true;
		}
		return ParseNumber(text.substr(0, separator), minimum) &&
			ParseNumber(text.substr(separator + 1), maximum) && minimum <= maximum;
	}

	class Parser
	{
	public:
		Parser(const std::string& expression, const ValueLookup* lookup)
			: m_expression(expression), m_lookup(lookup)
		{
			Next();
		}

		bool Parse(bool& value, int& specificity, std::string& error)
		{
			if (m_current.kind == TokenKind::End)
				return Fail("expression is empty", error);
			if (!ParseOr(value, specificity, error))
				return false;
			if (m_current.kind != TokenKind::End)
				return Fail("unexpected token '" + m_current.text + "'", error);
			return true;
		}

	private:
		enum class TokenKind
		{
			End, Word, LParen, RParen, And, Or, Not, Equal, NotEqual,
			Less, LessEqual, Greater, GreaterEqual, Alternative
		};

		struct Token
		{
			TokenKind kind = TokenKind::End;
			std::string text;
		};

		const std::string& m_expression;
		const ValueLookup* m_lookup = nullptr;
		size_t m_position = 0;
		Token m_current;

		bool Fail(const std::string& message, std::string& error) const
		{
			error = "display rule '" + m_expression + "': " + message;
			return false;
		}

		void Next()
		{
			while (m_position < m_expression.size() &&
				std::isspace(static_cast<unsigned char>(m_expression[m_position])))
				++m_position;
			if (m_position >= m_expression.size())
			{
				m_current = {};
				return;
			}

			const char c = m_expression[m_position];
			auto two = [&](char first, char second, TokenKind kind)
			{
				if (c == first && m_position + 1 < m_expression.size() &&
					m_expression[m_position + 1] == second)
				{
					m_current = { kind, std::string() + first + second };
					m_position += 2;
					return true;
				}
				return false;
			};
			if (two('&', '&', TokenKind::And) || two('|', '|', TokenKind::Or) ||
				two('=', '=', TokenKind::Equal) ||
				two('!', '=', TokenKind::NotEqual) || two('<', '=', TokenKind::LessEqual) ||
				two('>', '=', TokenKind::GreaterEqual))
				return;

			switch (c)
			{
			case '(': m_current = { TokenKind::LParen, "(" }; ++m_position; return;
			case ')': m_current = { TokenKind::RParen, ")" }; ++m_position; return;
			case '!': m_current = { TokenKind::Not, "!" }; ++m_position; return;
			case '=': m_current = { TokenKind::Equal, "=" }; ++m_position; return;
			case '<': m_current = { TokenKind::Less, "<" }; ++m_position; return;
			case '>': m_current = { TokenKind::Greater, ">" }; ++m_position; return;
			case '|': m_current = { TokenKind::Alternative, "|" }; ++m_position; return;
			case '\'':
			case '"':
			{
				const char quote = c;
				++m_position;
				const size_t start = m_position;
				while (m_position < m_expression.size() && m_expression[m_position] != quote)
					++m_position;
				m_current = { TokenKind::Word, m_expression.substr(start, m_position - start) };
				if (m_position < m_expression.size()) ++m_position;
				return;
			}
			default:
				break;
			}

			const size_t start = m_position;
			while (m_position < m_expression.size())
			{
				const char next = m_expression[m_position];
				if (std::isspace(static_cast<unsigned char>(next)) || next == '(' || next == ')' ||
					next == '!' || next == '=' || next == '<' || next == '>' || next == '&' ||
					next == '|')
					break;
				++m_position;
			}
			m_current = { TokenKind::Word, m_expression.substr(start, m_position - start) };
		}

		bool ParseOr(bool& value, int& specificity, std::string& error)
		{
			if (!ParseAnd(value, specificity, error)) return false;
			while (m_current.kind == TokenKind::Or)
			{
				Next();
				bool right = false;
				int rightSpecificity = 0;
				if (!ParseAnd(right, rightSpecificity, error)) return false;
				value = value || right;
				specificity = std::max(specificity, rightSpecificity);
			}
			return true;
		}

		bool ParseAnd(bool& value, int& specificity, std::string& error)
		{
			if (!ParsePrimary(value, specificity, error)) return false;
			while (m_current.kind == TokenKind::And)
			{
				Next();
				bool right = false;
				int rightSpecificity = 0;
				if (!ParsePrimary(right, rightSpecificity, error)) return false;
				value = value && right;
				specificity += rightSpecificity;
			}
			return true;
		}

		bool ParsePrimary(bool& value, int& specificity, std::string& error)
		{
			if (m_current.kind == TokenKind::Not)
			{
				Next();
				if (!ParsePrimary(value, specificity, error)) return false;
				value = !value;
				return true;
			}
			if (m_current.kind == TokenKind::LParen)
			{
				Next();
				if (!ParseOr(value, specificity, error)) return false;
				if (m_current.kind != TokenKind::RParen)
					return Fail("missing closing ')'", error);
				Next();
				return true;
			}
			return ParseComparison(value, specificity, error);
		}

		bool ParseComparison(bool& value, int& specificity, std::string& error)
		{
			if (m_current.kind != TokenKind::Word)
				return Fail("expected a variable", error);
			std::string variable = ConfigFile::NormalizeName(m_current.text);
			if (!variable.empty() && variable.front() == '$') variable.erase(0, 1);
			ValueType type = ValueType::Text;
			if (!GetVariableType(variable, type))
				return Fail("unknown variable '$" + variable + "'", error);
			Next();

			TokenKind operation = m_current.kind;
			if (operation != TokenKind::Equal && operation != TokenKind::NotEqual &&
				operation != TokenKind::Less && operation != TokenKind::LessEqual &&
				operation != TokenKind::Greater && operation != TokenKind::GreaterEqual)
			{
				if (type != ValueType::Boolean)
					return Fail("expected comparison operator after '$" + variable + "'", error);
				operation = TokenKind::Equal;
			}
			else
			{
				Next();
			}

			std::vector<std::string> expected;
			if (m_current.kind == TokenKind::Word)
			{
				expected.push_back(ConfigFile::NormalizeName(m_current.text));
				Next();
				while (m_current.kind == TokenKind::Alternative)
				{
					Next();
					if (m_current.kind != TokenKind::Word)
						return Fail("expected value after '|'", error);
					expected.push_back(ConfigFile::NormalizeName(m_current.text));
					Next();
				}
			}
			else if (type == ValueType::Boolean && operation == TokenKind::Equal)
			{
				expected.push_back("true");
			}
			else
			{
				return Fail("expected comparison value for '$" + variable + "'", error);
			}

			if (type == ValueType::Boolean)
			{
				for (const std::string& item : expected)
					if (item != "true" && item != "false")
						return Fail("'$" + variable + "' accepts only true or false", error);
			}
			if (type == ValueType::Number)
			{
				if (operation != TokenKind::Equal && operation != TokenKind::NotEqual && expected.size() != 1)
					return Fail("numeric comparisons accept exactly one value", error);
				for (const std::string& item : expected)
				{
					double minimum = 0.0, maximum = 0.0;
					if (!ParseNumberOrRange(item, minimum, maximum))
						return Fail("'$" + variable + "' requires a numeric value", error);
					if (operation != TokenKind::Equal && operation != TokenKind::NotEqual && minimum != maximum)
						return Fail("'$" + variable + "' ranges are supported only with = or !=", error);
				}
			}
			else if (operation != TokenKind::Equal && operation != TokenKind::NotEqual)
			{
				return Fail("'$" + variable + "' supports only = and !=", error);
			}

			specificity = 1;
			if (!m_lookup)
			{
				value = true;
				return true;
			}

			std::string actual;
			if (!(*m_lookup)(variable, actual) || actual.empty())
			{
				value = false; // Unavailable is never a match, including !=.
				return true;
			}
			actual = ConfigFile::NormalizeName(actual);
			bool equality = false;
			if (type == ValueType::Number)
			{
				double actualNumber = 0.0;
				if (!ParseNumber(actual, actualNumber))
				{
					value = false;
					return true;
				}
				if (operation == TokenKind::Equal || operation == TokenKind::NotEqual)
				{
					for (const std::string& item : expected)
					{
						double minimum = 0.0, maximum = 0.0;
						ParseNumberOrRange(item, minimum, maximum);
						if (actualNumber >= minimum && actualNumber <= maximum) equality = true;
					}
					value = operation == TokenKind::Equal ? equality : !equality;
					return true;
				}
				double expectedNumber = 0.0;
				ParseNumber(expected.front(), expectedNumber);
				switch (operation)
				{
				case TokenKind::Less: value = actualNumber < expectedNumber; break;
				case TokenKind::LessEqual: value = actualNumber <= expectedNumber; break;
				case TokenKind::Greater: value = actualNumber > expectedNumber; break;
				case TokenKind::GreaterEqual: value = actualNumber >= expectedNumber; break;
				default: value = false; break;
				}
				return true;
			}

			for (const std::string& item : expected)
				if (actual == item) equality = true;
			value = operation == TokenKind::Equal ? equality : !equality;
			return true;
		}
	};

	inline bool Validate(const std::string& expression, std::string& error)
	{
		bool ignoredValue = false;
		int ignoredSpecificity = 0;
		Parser parser(expression, nullptr);
		return parser.Parse(ignoredValue, ignoredSpecificity, error);
	}

	inline bool Matches(const std::string& expression, const ValueLookup& lookup,
		int& specificity, std::string& error)
	{
		bool matches = false;
		Parser parser(expression, &lookup);
		return parser.Parse(matches, specificity, error) && matches;
	}

	inline bool ValidateConfig(const ConfigFile& config, std::string& error)
	{
		std::string ruleList;
		if (!config.TryGetString("display_rules", "rules", ruleList))
			return true;
		std::istringstream rules(ruleList);
		std::string ruleName;
		while (std::getline(rules, ruleName, ','))
		{
			ruleName = ConfigFile::NormalizeName(ruleName);
			if (ruleName.empty()) continue;
			std::string expression;
			const std::string section = "display_rules." + ruleName;
			if (!config.TryGetString(section, "rule", expression))
			{
				error = "display rule '" + ruleName + "' is listed but [" + section + "] has no rule=";
				return false;
			}
			std::string expressionError;
			if (!Validate(expression, expressionError))
			{
				error = "display rule '" + ruleName + "' is invalid: " + expressionError;
				return false;
			}
		}
		return true;
	}
}
