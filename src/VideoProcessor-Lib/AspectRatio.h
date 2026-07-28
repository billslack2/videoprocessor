#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>


// Exact, normalized representation shared by configuration, renderer, and
// shader paths. Decimal inputs are retained as exact base-10 fractions.
struct AspectRatio
{
	uint64_t numerator = 0;
	uint64_t denominator = 0;
	double value = 0.0;

	bool IsValid() const
	{
		return numerator != 0 && denominator != 0 &&
			std::isfinite(value) && value > 0.0;
	}

	std::string Canonical() const
	{
		return std::to_string(numerator) + ":" +
			std::to_string(denominator);
	}
};


namespace AspectRatioParser
{
	inline std::string Trim(const std::string& text)
	{
		size_t first = 0;
		while (first < text.size() &&
			std::isspace(static_cast<unsigned char>(text[first])))
			++first;
		size_t last = text.size();
		while (last > first &&
			std::isspace(static_cast<unsigned char>(text[last - 1])))
			--last;
		return text.substr(first, last - first);
	}

	struct DecimalFraction
	{
		uint64_t numerator = 0;
		uint64_t denominator = 1;
	};

	inline uint64_t GreatestCommonDivisor(uint64_t left, uint64_t right)
	{
		while (right != 0)
		{
			const uint64_t remainder = left % right;
			left = right;
			right = remainder;
		}
		return left;
	}

	inline bool CheckedMultiply(uint64_t left, uint64_t right,
		uint64_t& product)
	{
		if (right != 0 &&
			left > (std::numeric_limits<uint64_t>::max)() / right)
			return false;
		product = left * right;
		return true;
	}

	inline bool ParsePositiveDecimal(const std::string& raw,
		DecimalFraction& result)
	{
		const std::string text = Trim(raw);
		if (text.empty())
			return false;

		uint64_t digits = 0;
		uint64_t scale = 1;
		bool sawDigit = false;
		bool sawDecimal = false;
		for (char character : text)
		{
			const unsigned char value = static_cast<unsigned char>(character);
			if (std::isdigit(value))
			{
				sawDigit = true;
				const unsigned int digit = character - '0';
				if (digits >
					((std::numeric_limits<uint64_t>::max)() - digit) / 10)
					return false;
				digits = digits * 10 + digit;
				if (sawDecimal)
				{
					if (scale >
						(std::numeric_limits<uint64_t>::max)() / 10)
						return false;
					scale *= 10;
				}
			}
			else if (character == '.' && !sawDecimal)
			{
				sawDecimal = true;
			}
			else
			{
				return false;
			}
		}
		if (!sawDigit || digits == 0 || text.front() == '.' ||
			text.back() == '.')
			return false;

		const uint64_t divisor = GreatestCommonDivisor(digits, scale);
		result.numerator = digits / divisor;
		result.denominator = scale / divisor;
		return true;
	}

	inline bool Parse(const std::string& raw, double minimum,
		double maximum, AspectRatio& result, std::string& error)
	{
		result = {};
		error.clear();
		const std::string text = Trim(raw);
		if (text.empty())
		{
			error = "aspect is empty";
			return false;
		}

		size_t separator = std::string::npos;
		for (size_t index = 0; index < text.size(); ++index)
		{
			if (text[index] != ':' && text[index] != 'x' &&
				text[index] != 'X')
				continue;
			if (separator != std::string::npos)
			{
				error = "aspect contains repeated delimiters";
				return false;
			}
			separator = index;
		}

		DecimalFraction width;
		DecimalFraction height{ 1, 1 };
		if (separator == std::string::npos)
		{
			if (!ParsePositiveDecimal(text, width))
			{
				error = "aspect must be a positive decimal or ratio";
				return false;
			}
		}
		else
		{
			if (!ParsePositiveDecimal(text.substr(0, separator), width) ||
				!ParsePositiveDecimal(text.substr(separator + 1), height))
			{
				error = "aspect components must be positive decimals";
				return false;
			}
		}

		uint64_t numerator = 0;
		uint64_t denominator = 0;
		if (!CheckedMultiply(width.numerator, height.denominator, numerator) ||
			!CheckedMultiply(width.denominator, height.numerator, denominator) ||
			numerator == 0 || denominator == 0)
		{
			error = "aspect is too precise";
			return false;
		}
		const uint64_t divisor =
			GreatestCommonDivisor(numerator, denominator);
		numerator /= divisor;
		denominator /= divisor;
		const double value = static_cast<double>(numerator) /
			static_cast<double>(denominator);
		if (!std::isfinite(value) || value < minimum || value > maximum)
		{
			error = "resolved aspect must be between " +
				std::to_string(minimum) + " and " +
				std::to_string(maximum);
			return false;
		}

		result.numerator = numerator;
		result.denominator = denominator;
		result.value = value;
		return true;
	}
}
