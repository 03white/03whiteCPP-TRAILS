#include "util/stringutil.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace util {

std::string StringUtil::tailName(const std::string& path) {
    if (path.empty())
        return "";
    // 1) 跳过尾部的分隔符，处理 "foo/bar/" 这种情况
    size_t end = path.size();
    while (end > 0 && (path[end - 1] == '/' || path[end - 1] == '\\'))
        --end;
    // 2) 整个路径全是分隔符，例如 "/" 或 "C:\\"
    if (end == 0)
        return path.substr(0, 1); // 返回根分隔符
    // 3) 从后往前找最后一个分隔符（在 [0, end-1] 范围内找）
    size_t slash = path.find_last_of("/\\", end - 1);
    if (slash == std::string::npos)
        return path.substr(0, end); // 没有目录部分
    return path.substr(slash + 1, end - slash - 1);
}

bool StringUtil::hasFile(std::string_view path, std::string_view filename) {
    if (path.empty() || filename.empty())
        return false;

    fs::path dir{path};
    fs::path name{filename};

    // filename 必须是相对名，不能是绝对路径或带目录穿越
    if (name.is_absolute())
        return false;
    if (name.has_parent_path() && name.parent_path() != "."
        && name.parent_path() != "") {
        return false;
    }

    std::error_code ec;
    fs::path target = dir / name;

    // 只用 exists + is_regular_file，目录不算
    if (!fs::exists(target, ec) || ec)
        return false;
    return fs::is_regular_file(target, ec) && !ec;
}

std::string StringUtil::buildRange(size_t start, size_t end) {
    return "Range: bytes=" + std::to_string(start) + "-" + std::to_string(end);
}

} // namespace util
