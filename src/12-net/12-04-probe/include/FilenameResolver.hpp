#pragma once
#include <optional>
#include <string>
#include <vector>
#include <cctype>
#include <algorithm>
namespace FilenameResolver {

std::optional<std::string> from_content_disposition(const std::string& header_value);
std::optional<std::string> from_url(const std::string& url);
std::string sanitize(const std::string& raw, const std::string& fallback = "download.bin");
std::string resolve(const std::optional<std::string>& content_disposition,
                    const std::string&                effective_url,
                    const std::string&                fallback = "download.bin");

}   // namespace FilenameResolver
