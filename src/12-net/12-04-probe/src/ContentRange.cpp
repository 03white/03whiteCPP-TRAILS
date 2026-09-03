#include "ContentRange.hpp"

#include "ContentRange.hpp"

std::uint64_t ContentRange::length() const noexcept
{
    return last - first + 1;
}

bool ContentRange::matches(
    std::uint64_t expect_first,
    std::uint64_t expect_last
) const noexcept
{
    return first == expect_first &&
           last == expect_last;
}

std::optional<ContentRange> ContentRange::parse(const std::string& value) {
    const std::string prefix = "bytes ";
    if (value.size() < prefix.size()
        || value.compare(0, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }
    const auto range = value.substr(prefix.size());
    const auto dash = range.find('-');
    const auto slash = range.find('/');
    if (dash == std::string::npos || slash == std::string::npos
        || dash >= slash) {
        return std::nullopt;
    }
    const auto first_str = range.substr(0, dash);
    const auto last_str = range.substr(dash + 1, slash - dash - 1);
    const auto total_str = range.substr(slash + 1);
    ContentRange result;
    try {
        result.first = std::stoull(first_str);
        result.last = std::stoull(last_str);

        if (total_str != "*") {
            result.total = std::stoull(total_str);
        }
        result.has_range = true;
    } catch (...) { return std::nullopt; }

    if (result.first > result.last) {
        return std::nullopt;
    }
    return result;
}

std::string ContentRange::to_string() const
{
    std::string result = "bytes ";

    result += std::to_string(first);
    result += "-";
    result += std::to_string(last);
    result += "/";

    if (total.has_value())
    {
        result += std::to_string(*total);
    }
    else
    {
        result += "*";
    }

    return result;
}