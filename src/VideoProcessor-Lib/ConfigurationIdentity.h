#pragma once
#include <cstdint>
#include <string>

// Conservative identity of the loaded text, for correlating Config with the
// renderer's applied document. Line-ending style is deliberately insignificant.
namespace ConfigurationIdentity
{
    constexpr uint64_t Seed = 14695981039346656037ull;
    inline void AppendLine(uint64_t& hash, const std::string& line)
    {
        for (unsigned char c : line)
            if (c != '\r') { hash ^= c; hash *= 1099511628211ull; }
        hash ^= '\n'; hash *= 1099511628211ull;
    }
    inline uint64_t FromText(const std::string& text)
    {
        uint64_t hash = Seed;
        size_t start = 0;
        while (start < text.size()) {
            const size_t end = text.find('\n', start);
            AppendLine(hash, text.substr(start, end == std::string::npos ? end : end-start));
            if (end == std::string::npos) break;
            start = end+1;
        }
        return hash;
    }
}
