#pragma once

#include "ConfigFile.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// A small reusable expression AST shared by startup validation, source
// selection, key discovery, key evaluation, and completed-event matching.
namespace DisplayRuleExpression
{
	enum class ValueType { Text, Number, Boolean };
	using ValueLookup = std::function<bool(const std::string& name, std::string& value)>;

	inline bool IsProfileGroup(const std::string& name)
	{
		return name == "input" || name == "scaling" ||
			name == "display" || name == "color" || name == "output" ||
			name == "viewport" || name == "zoom" || name == "queue" ||
			name == "lldv";
	}

	inline bool IsProfileVariable(const std::string& name,
		const std::string& prefix)
	{
		if (name == prefix + "viewport_name" || name == prefix + "zoom_name")
			return true;
		return name.size() > prefix.size() &&
			name.compare(0, prefix.size(), prefix) == 0 &&
			IsProfileGroup(name.substr(prefix.size()));
	}

	inline bool GetVariableType(const std::string& name, ValueType& type)
	{
		// Event actions evaluate a committed current/previous state pair. Keep
		// the previous-state spelling explicit so profile exits can be matched
		// without adding a separate event token for every profile value.
		if (name.size() > 9 && name.compare(0, 9, "previous.") == 0)
			return GetVariableType(name.substr(9), type);
		if (name == "eotf" || name == "transfer" || name == "colorspace" ||
			name == "primaries" || name == "format" || name == "resolution" ||
			name == "range" || name == "scan" || name == "renderer" ||
			name == "key" ||
			name == "event" || name == "event_reason" ||
			name == "screen_config" || name == "zoom_config" ||
			name == "viewport_profile" || name == "zoom_profile" ||
			name == "vertical_alignment" ||
			IsProfileVariable(name, "profile."))
		{
			type = ValueType::Text;
			return true;
		}
		if (IsProfileVariable(name, "previous_profile."))
		{
			type = ValueType::Text;
			return true;
		}
		if (name == "hdr_metadata" || name == "interlaced")
		{
			type = ValueType::Boolean;
			return true;
		}
		if (name == "source_rate" || name == "cadence" ||
			name == "width" || name == "height" ||
			name == "actual_refresh" || name == "requested_refresh" ||
			name == "previous_refresh" || name == "screen_aspect" ||
			name == "anamorphic_scale" ||
			name == "hdr_peak_analysis_height_percent" ||
			name == "subtitle_hold_seconds" ||
			name == "subtitle_engage_drift_ms" ||
			name == "subtitle_release_drift_ms" ||
			name == "subtitle_padding_pixels" ||
			name == "subtitle_target_buffer_pixels" ||
			name == "viewport_generation")
		{
			type = ValueType::Number;
			return true;
		}
		if (name == "automatic_crop" || name == "subtitle_fit" ||
			name == "hdr_peak_analysis_picture_only")
		{
			type = ValueType::Boolean;
			return true;
		}
		return false;
	}

	inline bool IsSnapshotActionVariable(const std::string& name)
	{
		std::string current = name;
		if (current.size() > 9 && current.compare(0, 9, "previous.") == 0)
			current.erase(0, 9);
		ValueType type = ValueType::Text;
		if (!GetVariableType(name, type))
			return false;
		return current != "key" && current != "renderer" &&
			current != "range" && current != "actual_refresh" &&
			current != "requested_refresh" &&
			current != "previous_refresh";
	}

	inline bool ParseNumber(const std::string& text, double& value)
	{
		try
		{
			const size_t slash = text.find('/');
			if (slash != std::string::npos)
			{
				double numerator = 0.0, denominator = 0.0;
				if (slash == 0 || slash + 1 >= text.size() ||
					!ParseNumber(text.substr(0, slash), numerator) ||
					!ParseNumber(text.substr(slash + 1), denominator) ||
					denominator == 0.0) return false;
				value = numerator / denominator;
				return std::isfinite(value);
			}
			size_t consumed = 0;
			value = std::stod(text, &consumed);
			return consumed == text.size() && std::isfinite(value);
		}
		catch (const std::exception&) { return false; }
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

	// Profile selections have stable configuration identifiers (for example
	// "low_latency") but are presented to operators with spaces ("Low
	// Latency").  Action conditions accept either spelling without changing
	// the value handed to an external action.
	inline std::string NormalizeProfileComparisonName(const std::string& value)
	{
		std::string normalized = ConfigFile::NormalizeName(value);
		for (char& character : normalized)
			if (std::isspace(static_cast<unsigned char>(character)) ||
				character == '-')
				character = '_';
		return normalized;
	}

	inline bool IsProfileSelectionVariable(const std::string& variable)
	{
		return (variable.size() > 8 && variable.compare(0, 8,
			"profile.") == 0) ||
			(variable.size() > 17 && variable.compare(0, 17,
				"previous_profile.") == 0);
	}

	enum class Operation { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };

	struct Node
	{
		enum class Kind { Comparison, Not, And, Or };
		Kind kind = Kind::Comparison;
		std::shared_ptr<const Node> left;
		std::shared_ptr<const Node> right;
		std::string variable;
		ValueType type = ValueType::Text;
		Operation operation = Operation::Equal;
		std::vector<std::string> expected;

		bool Evaluate(const ValueLookup& lookup, bool& value, int& specificity) const
		{
			if (kind == Kind::Not)
			{
				if (!left->Evaluate(lookup, value, specificity)) return false;
				value = !value;
				return true;
			}
			if (kind == Kind::And || kind == Kind::Or)
			{
				bool leftValue = false, rightValue = false;
				int leftSpecificity = 0, rightSpecificity = 0;
				if (!left->Evaluate(lookup, leftValue, leftSpecificity) ||
					!right->Evaluate(lookup, rightValue, rightSpecificity)) return false;
				if (kind == Kind::And)
				{
					value = leftValue && rightValue;
					specificity = leftSpecificity + rightSpecificity;
				}
				else
				{
					value = leftValue || rightValue;
					specificity = leftValue && rightValue ?
						std::max(leftSpecificity, rightSpecificity) :
						(leftValue ? leftSpecificity : rightSpecificity);
				}
				return true;
			}

			specificity = variable == "key" ? 0 : 1;
			std::string actual;
			if (!lookup(variable, actual) || actual.empty())
			{
				value = false;
				return true;
			}
			// Chords use a canonical spelling (for example Shift+L); letter
			// casing alone never denotes Shift, so ${key} remains case-sensitive.
			// Other expression values remain case-insensitive.
			if (variable != "key")
				actual = IsProfileSelectionVariable(variable) ?
					NormalizeProfileComparisonName(actual) :
					ConfigFile::NormalizeName(actual);
			if (type == ValueType::Number)
			{
				double actualNumber = 0.0;
				if (!ParseNumber(actual, actualNumber))
				{
					value = false;
					return true;
				}
				if (operation == Operation::Equal || operation == Operation::NotEqual)
				{
					bool equality = false;
					const double tolerance =
						variable == "cadence" ||
						variable == "actual_refresh" ||
						variable == "requested_refresh" ||
						variable == "previous_refresh" ? 0.0005 : 0.0;
					for (const std::string& item : expected)
					{
						double minimum = 0.0, maximum = 0.0;
						ParseNumberOrRange(item, minimum, maximum);
						if (actualNumber >= minimum - tolerance &&
							actualNumber <= maximum + tolerance)
							equality = true;
					}
					value = operation == Operation::Equal ? equality : !equality;
					return true;
				}
				double expectedNumber = 0.0;
				ParseNumber(expected.front(), expectedNumber);
				switch (operation)
				{
				case Operation::Less: value = actualNumber < expectedNumber; break;
				case Operation::LessEqual: value = actualNumber <= expectedNumber; break;
				case Operation::Greater: value = actualNumber > expectedNumber; break;
				case Operation::GreaterEqual: value = actualNumber >= expectedNumber; break;
				default: value = false; break;
				}
				return true;
			}

			const bool equality = std::find(expected.begin(), expected.end(), actual) != expected.end();
			value = operation == Operation::Equal ? equality : !equality;
			return true;
		}
	};

	enum class TokenKind
	{
		End, Word, LParen, RParen, And, Or, Not, Equal, NotEqual,
		Less, LessEqual, Greater, GreaterEqual, Alternative, Invalid
	};

	struct Token
	{
		TokenKind kind = TokenKind::End;
		std::string text;
		bool quoted = false;
	};

	class Lexer
	{
	public:
		explicit Lexer(const std::string& expression) : m_expression(expression) {}

		Token Next()
		{
			while (m_position < m_expression.size() &&
				std::isspace(static_cast<unsigned char>(m_expression[m_position]))) ++m_position;
			if (m_position >= m_expression.size()) return {};
			const char c = m_expression[m_position];
			auto two = [&](char first, char second, TokenKind kind, Token& token)
			{
				if (c == first && m_position + 1 < m_expression.size() &&
					m_expression[m_position + 1] == second)
				{
					token = { kind, std::string() + first + second, false };
					m_position += 2;
					return true;
				}
				return false;
			};
			Token token;
			if (two('&', '&', TokenKind::And, token) ||
				two('|', '|', TokenKind::Or, token) ||
				two('=', '=', TokenKind::Equal, token) ||
				two('!', '=', TokenKind::NotEqual, token) ||
				two('<', '=', TokenKind::LessEqual, token) ||
				two('>', '=', TokenKind::GreaterEqual, token)) return token;
			switch (c)
			{
			case '(': ++m_position; return { TokenKind::LParen, "(", false };
			case ')': ++m_position; return { TokenKind::RParen, ")", false };
			case '!': ++m_position; return { TokenKind::Not, "!", false };
			case '=': ++m_position; return { TokenKind::Equal, "=", false };
			case '<': ++m_position; return { TokenKind::Less, "<", false };
			case '>': ++m_position; return { TokenKind::Greater, ">", false };
			case '|': ++m_position; return { TokenKind::Alternative, "|", false };
			case '\'':
			case '"':
			{
				const char quote = c;
				const size_t start = ++m_position;
				while (m_position < m_expression.size() && m_expression[m_position] != quote)
					++m_position;
				if (m_position >= m_expression.size())
					return { TokenKind::Invalid, "unterminated quoted value", true };
				const std::string value = m_expression.substr(start, m_position - start);
				++m_position;
				return { TokenKind::Word, value, true };
			}
			default: break;
			}
			const size_t start = m_position;
			while (m_position < m_expression.size())
			{
				const char next = m_expression[m_position];
				if (std::isspace(static_cast<unsigned char>(next)) || next == '(' || next == ')' ||
					next == '!' || next == '=' || next == '<' || next == '>' || next == '&' ||
					next == '|') break;
				++m_position;
			}
			if (m_position == start)
			{
				++m_position;
				return { TokenKind::Invalid, std::string("invalid character '") + c + "'", false };
			}
			return { TokenKind::Word, m_expression.substr(start, m_position - start), false };
		}

	private:
		const std::string& m_expression;
		size_t m_position = 0;
	};

	class AstParser
	{
	public:
		AstParser(const std::string& expression, bool strict)
			: m_expression(expression), m_lexer(expression), m_strict(strict), m_current(m_lexer.Next()) {}

		bool Parse(std::shared_ptr<const Node>& root, std::set<std::string>& variables,
			std::vector<std::string>& keyChords, std::string& error)
		{
			m_variables = &variables;
			m_keyChords = &keyChords;
			if (m_current.kind == TokenKind::End) return Fail("expression is empty", error);
			if (!ParseOr(root, error)) return false;
			if (m_current.kind != TokenKind::End)
				return Fail("unexpected token '" + m_current.text + "'", error);
			return true;
		}

	private:
		bool Fail(const std::string& message, std::string& error) const
		{
			error = "display rule '" + m_expression + "': " + message;
			return false;
		}
		void Next() { m_current = m_lexer.Next(); }

		bool ParseOr(std::shared_ptr<const Node>& node, std::string& error)
		{
			if (!ParseAnd(node, error)) return false;
			while (m_current.kind == TokenKind::Or)
			{
				Next();
				std::shared_ptr<const Node> right;
				if (!ParseAnd(right, error)) return false;
				auto parent = std::make_shared<Node>();
				parent->kind = Node::Kind::Or;
				parent->left = node;
				parent->right = right;
				node = parent;
			}
			return true;
		}

		bool ParseAnd(std::shared_ptr<const Node>& node, std::string& error)
		{
			if (!ParsePrimary(node, error)) return false;
			while (m_current.kind == TokenKind::And)
			{
				Next();
				std::shared_ptr<const Node> right;
				if (!ParsePrimary(right, error)) return false;
				auto parent = std::make_shared<Node>();
				parent->kind = Node::Kind::And;
				parent->left = node;
				parent->right = right;
				node = parent;
			}
			return true;
		}

		bool ParsePrimary(std::shared_ptr<const Node>& node, std::string& error)
		{
			if (m_current.kind == TokenKind::Invalid) return Fail(m_current.text, error);
			if (m_current.kind == TokenKind::Not)
			{
				Next();
				std::shared_ptr<const Node> child;
				if (!ParsePrimary(child, error)) return false;
				auto parent = std::make_shared<Node>();
				parent->kind = Node::Kind::Not;
				parent->left = child;
				node = parent;
				return true;
			}
			if (m_current.kind == TokenKind::LParen)
			{
				Next();
				if (!ParseOr(node, error)) return false;
				if (m_current.kind != TokenKind::RParen)
					return Fail("missing closing ')'", error);
				Next();
				return true;
			}
			return ParseComparison(node, error);
		}

		bool ParseComparison(std::shared_ptr<const Node>& node, std::string& error)
		{
			if (m_current.kind != TokenKind::Word) return Fail("expected a variable", error);
			const std::string reference = m_current.text;
			std::string variableText = reference;
			if (variableText.size() >= 2 && variableText[0] == '$' &&
				variableText[1] == '{')
			{
				if (variableText.size() < 4 || variableText.back() != '}')
					return Fail("unterminated variable reference '" + reference + "'", error);
				variableText = variableText.substr(2, variableText.size() - 3);
			}
			else if (!variableText.empty() && variableText.front() == '$')
				variableText.erase(0, 1);
			std::string variable = ConfigFile::NormalizeName(variableText);
			ValueType type = ValueType::Text;
			if (!GetVariableType(variable, type))
				return Fail("unknown variable '${" + variable + "}'", error);
			m_variables->insert(variable);
			Next();

			Operation operation = Operation::Equal;
			const std::string operatorText = m_current.text;
			switch (m_current.kind)
			{
			case TokenKind::Equal: operation = Operation::Equal; Next(); break;
			case TokenKind::NotEqual: operation = Operation::NotEqual; Next(); break;
			case TokenKind::Less: operation = Operation::Less; Next(); break;
			case TokenKind::LessEqual: operation = Operation::LessEqual; Next(); break;
			case TokenKind::Greater: operation = Operation::Greater; Next(); break;
			case TokenKind::GreaterEqual: operation = Operation::GreaterEqual; Next(); break;
			default:
				if (type != ValueType::Boolean)
					return Fail("expected comparison operator after '${" + variable + "}'", error);
				break;
			}
			if (m_strict && operatorText == "=")
				return Fail("unified expressions require '==' rather than '='", error);

			std::vector<std::string> expected;
			std::vector<bool> quoted;
			if (m_current.kind == TokenKind::Word)
			{
				expected.push_back(variable == "key" ? m_current.text :
					(IsProfileSelectionVariable(variable) ?
						NormalizeProfileComparisonName(m_current.text) :
						ConfigFile::NormalizeName(m_current.text)));
				quoted.push_back(m_current.quoted);
				Next();
				while (m_current.kind == TokenKind::Alternative)
				{
					if (m_strict)
						return Fail("unified expressions require full comparisons joined by '||'", error);
					Next();
					if (m_current.kind != TokenKind::Word)
						return Fail("expected value after '|'", error);
					expected.push_back(variable == "key" ? m_current.text :
						(IsProfileSelectionVariable(variable) ?
							NormalizeProfileComparisonName(m_current.text) :
							ConfigFile::NormalizeName(m_current.text)));
					quoted.push_back(m_current.quoted);
					Next();
				}
			}
			else if (type == ValueType::Boolean && operation == Operation::Equal)
			{
				expected.push_back("true");
				quoted.push_back(false);
			}
			else return Fail("expected comparison value for '${" + variable + "}'", error);

			if (variable == "key")
			{
				if (operation != Operation::Equal || expected.size() != 1 || !quoted.front() ||
					expected.front().empty())
					return Fail("${key} must use == with one non-empty quoted shortcut", error);
				m_keyChords->push_back(expected.front());
			}
			if (type == ValueType::Boolean)
				for (const std::string& item : expected)
					if (item != "true" && item != "false")
						return Fail("'${" + variable + "}' accepts only true or false", error);
			if (type == ValueType::Number)
			{
				if (operation != Operation::Equal && operation != Operation::NotEqual &&
					expected.size() != 1)
					return Fail("numeric comparisons accept exactly one value", error);
				for (const std::string& item : expected)
				{
					double minimum = 0.0, maximum = 0.0;
					if (!ParseNumberOrRange(item, minimum, maximum))
						return Fail("'${" + variable + "}' requires a numeric value", error);
					if (operation != Operation::Equal && operation != Operation::NotEqual &&
						minimum != maximum)
						return Fail("'${" + variable + "}' ranges are supported only with = or !=", error);
				}
			}
			else if (operation != Operation::Equal && operation != Operation::NotEqual)
				return Fail("'${" + variable + "}' supports only = and !=", error);

			auto comparison = std::make_shared<Node>();
			comparison->kind = Node::Kind::Comparison;
			comparison->variable = variable;
			comparison->type = type;
			comparison->operation = operation;
			comparison->expected = std::move(expected);
			node = comparison;
			return true;
		}

		const std::string& m_expression;
		Lexer m_lexer;
		bool m_strict = false;
		Token m_current;
		std::set<std::string>* m_variables = nullptr;
		std::vector<std::string>* m_keyChords = nullptr;
	};

	class Expression
	{
	public:
		bool Compile(const std::string& source, std::string& error, bool strict = false)
		{
			m_source = source;
			m_root.reset();
			m_variables.clear();
			m_keyChords.clear();
			error.clear();
			AstParser parser(source, strict);
			return parser.Parse(m_root, m_variables, m_keyChords, error);
		}

		bool Matches(const ValueLookup& lookup, int& specificity, std::string& error) const
		{
			error.clear();
			specificity = 0;
			if (!m_root)
			{
				error = "expression was not compiled";
				return false;
			}
			bool value = false;
			return m_root->Evaluate(lookup, value, specificity) && value;
		}

		const std::set<std::string>& Variables() const { return m_variables; }
		const std::vector<std::string>& KeyChords() const { return m_keyChords; }
		const std::string& Source() const { return m_source; }

		bool DeclaresKeyChord(const std::string& chord) const
		{
			return std::any_of(m_keyChords.begin(), m_keyChords.end(),
				[&chord](const std::string& item) { return item == chord; });
		}

	private:
		std::string m_source;
		std::shared_ptr<const Node> m_root;
		std::set<std::string> m_variables;
		std::vector<std::string> m_keyChords;
	};

	inline bool Validate(const std::string& expression, std::string& error)
	{
		Expression compiled;
		return compiled.Compile(expression, error, false);
	}

	inline bool Matches(const std::string& expression, const ValueLookup& lookup,
		int& specificity, std::string& error)
	{
		Expression compiled;
		return compiled.Compile(expression, error, false) &&
			compiled.Matches(lookup, specificity, error);
	}

	inline bool ValidateConfig(const ConfigFile& config, std::string& error)
	{
		std::string ruleList;
		if (!config.TryGetString("display_rules", "rules", ruleList)) return true;
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
