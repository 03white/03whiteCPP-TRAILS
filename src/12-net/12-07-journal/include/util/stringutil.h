#ifndef STRINGUTIL_H
#define STRINGUTIL_H
#include <string>
#include <string_view>
#include <filesystem>

namespace util {

class StringUtil {
public:
    static std::string tailName(const std::string& path);
    static bool hasFile(std::string_view path, std::string_view filename);
    static std::string buildRange(size_t start, size_t end);
};

} // namespace util

#endif // !STRINGUTIL_H
