#ifndef UTIL_ATOMICFILE_H
#define UTIL_ATOMICFILE_H

#include <filesystem>
#include <string_view>

namespace util {

/**
 * @brief 崩溃安全地把 data 原子写入 target。
 *
 * 语义：
 *   - 成功返回后，target 内容一定是完整的 data
 *   - 失败抛异常后，target 保持原内容不变（要么旧、要么新，绝不半截）
 *
 * 步骤：
 *   1. 同目录生成唯一 tmp 路径
 *   2. open(tmp, O_WRONLY|O_CREAT|O_EXCL) / CreateFileW(CREATE_NEW)
 *   3. write 循环处理短写
 *   4. fsync / FlushFileBuffers
 *   5. close / CloseHandle
 *   6. rename(tmp, target) 原子替换 / MoveFileEx(REPLACE_EXISTING)
 *   7. fsync 父目录
 *
 * 任何一步失败：删 tmp + 抛 std::system_error / std::runtime_error。
 *
 * @throws std::system_error / std::runtime_error
 */
void writeAtomic(const std::filesystem::path& target, std::string_view data);

} // namespace util

#endif // UTIL_ATOMICFILE_H
