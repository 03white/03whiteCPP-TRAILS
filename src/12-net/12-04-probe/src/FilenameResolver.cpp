#include "FilenameResolver.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kMaxFilenameBytes = 200;   // 给续传的 ".part" 后缀留余量

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// percent-decode。遇到残缺的 % 序列就原样保留那个 %，不报错——
// 文件名解析没必要因为一个畸形转义就整体失败。
std::string percent_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = hex_value(s[i + 1]);
            const int lo = hex_value(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

// 把 Content-Disposition 切成 "; " 分隔的参数段。
// 不做完整的 RFC 5987 分词——引号内的分号会被切错，但那需要一个真正的
// tokenizer。这里的取舍是：切错只会退到下一级来源（URL），不会产生坏文件名。
std::vector<std::string> split_params(const std::string& value) {
    std::vector<std::string> parts;
    std::string cur;
    bool in_quotes = false;
    for (const char c : value) {
        if (c == '"') {
            in_quotes = !in_quotes;
            cur.push_back(c);
        } else if (c == ';' && !in_quotes) {
            parts.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    parts.push_back(trim(cur));
    return parts;
}

std::string unquote(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

bool is_windows_reserved(const std::string& stem) {
    static const std::array<const char*, 22> kReserved = {
        "con", "prn", "aux", "nul",
        "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    const std::string lower = to_lower(stem);
    return std::any_of(kReserved.begin(), kReserved.end(),
                       [&lower](const char* r) { return lower == r; });
}

}   // namespace

namespace FilenameResolver {

std::optional<std::string> from_content_disposition(const std::string& header_value) {
    if (header_value.empty()) {
        return std::nullopt;
    }

    std::optional<std::string> plain;      // filename="..."
    std::optional<std::string> extended;   // filename*=UTF-8''...

    for (const auto& part : split_params(header_value)) {
        const auto eq = part.find('=');
        if (eq == std::string::npos) {
            continue;   // "attachment" / "inline" 这种无值参数
        }
        const std::string key = to_lower(trim(part.substr(0, eq)));
        const std::string raw = trim(part.substr(eq + 1));

        if (key == "filename" && !plain) {
            plain = unquote(raw);
        } else if (key == "filename*" && !extended) {
            // RFC 5987 三段式：charset'lang'percent-encoded-name
            const auto q1 = raw.find('\'');
            const auto q2 = (q1 == std::string::npos) ? std::string::npos
                                                      : raw.find('\'', q1 + 1);
            if (q2 == std::string::npos) {
                continue;   // 不符合三段式，当它不存在
            }
            const std::string charset = to_lower(raw.substr(0, q1));
            std::string name = percent_decode(raw.substr(q2 + 1));

            // 解出来的是 charset 指定编码的**字节**，不是宽字符。
            // UTF-8 直接用：Windows 上 std::ofstream(const char*) 走的是 ANSI 代码页，
            // 中文名会乱码甚至开不了——真要正确落盘得转 UTF-16 走 _wfopen/wofstream，
            // 那是 14-io 专题的事。这里只负责把正确的字节交出去。
            // 非 UTF-8 的 charset（ISO-8859-1 等）这里不转码，交给下面 sanitize
            // 兜底；转码需要 iconv/ICU，不值得为一个文件名引依赖。
            if (charset == "utf-8" || charset == "iso-8859-1" || charset.empty()) {
                extended = std::move(name);
            }
        }
    }

    // RFC 6266 §4.3：两者都在时 filename* 优先——它能表达非 ASCII，
    // filename 只是给老客户端的降级副本。
    if (extended && !extended->empty()) {
        return extended;
    }
    if (plain && !plain->empty()) {
        return plain;
    }
    return std::nullopt;

    // 注意这里**不** sanitize。分开做的好处：这个函数是纯粹的 RFC 解析，
    // 可以单独对着 RFC 的例子测；落盘安全是另一个关注点，由 sanitize 统一负责，
    // 三个来源共用一份规则，不会漏掉某一条路径。
}

std::optional<std::string> from_url(const std::string& url) {
    if (url.empty()) {
        return std::nullopt;
    }

    // 先定位 authority 之后的位置，否则 "http://host" 里的 "//" 会被当成路径分隔符。
    std::size_t start = 0;
    const auto scheme = url.find("://");
    if (scheme != std::string::npos) {
        start = scheme + 3;
    }

    // query 和 fragment 必须先砍掉，否则 "c.bin?x=1" 会整个当成文件名。
    std::size_t end = url.size();
    for (const char c : {'?', '#'}) {
        const auto pos = url.find(c, start);
        if (pos != std::string::npos && pos < end) {
            end = pos;
        }
    }

    const auto path_begin = url.find('/', start);
    if (path_begin == std::string::npos || path_begin >= end) {
        return std::nullopt;   // "http://host" —— 根本没有 path
    }

    const std::string path = url.substr(path_begin, end - path_begin);
    const auto slash = path.rfind('/');
    const std::string last = path.substr(slash + 1);
    if (last.empty()) {
        return std::nullopt;   // 以 / 结尾，是目录不是文件
    }
    return percent_decode(last);
}

std::string sanitize(const std::string& raw, const std::string& fallback) {
    // 这个名字来自服务端，是不可信输入。逐条挡：
    //
    //   "../../autoexec.bat" -> 剥目录成分后剩 "autoexec.bat"，跑不出工作目录
    //   "C:\\Windows\\x"     -> 反斜杠也算目录分隔符，剥完剩 "x"
    //   ".." / "."           -> 剥完是纯点，整体拒掉走 fallback
    //   "NUL"                -> Windows 设备名，open 会打开设备而不是文件，加前缀
    //   "a\x01b.bin"         -> 控制字符，NTFS 直接拒绝，剔除
    //
    // 关键顺序：**先剥目录，再过滤字符**。反过来的话 "..%2F..%2Fx" 解码出的
    // 斜杠会被先删成 "....x"，路径穿越是挡住了，但名字面目全非；而且一旦以后
    // 有人在过滤表里漏掉某个分隔符，穿越就直接漏过去了。

    // 1. 剥掉一切目录成分，只留最后一段。
    std::string name = raw;
    const auto cut = name.find_last_of("/\\");
    if (cut != std::string::npos) {
        name = name.substr(cut + 1);
    }

    // 2. 剔除控制字符（含 NUL）和 Windows 保留字符。
    std::string clean;
    clean.reserve(name.size());
    for (const char c : name) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F) {
            continue;
        }
        if (std::strchr("<>:\"|?*", c) != nullptr) {
            continue;
        }
        clean.push_back(c);
    }

    // 3. Windows 会静默吃掉结尾的点和空格（"a.txt." 实际打开的是 "a.txt"），
    //    留着会让「我写的名字」和「实际的名字」对不上，续传时找不到 .part 文件。
    while (!clean.empty() && (clean.back() == '.' || clean.back() == ' ')) {
        clean.pop_back();
    }
    clean = trim(clean);

    if (clean.empty()) {
        return fallback;
    }

    // 4. 截断。按字节截会切断 UTF-8 多字节序列，退回到最近的字符边界。
    if (clean.size() > kMaxFilenameBytes) {
        std::size_t cutpos = kMaxFilenameBytes;
        while (cutpos > 0 && (static_cast<unsigned char>(clean[cutpos]) & 0xC0) == 0x80) {
            --cutpos;
        }
        clean = clean.substr(0, cutpos);
        while (!clean.empty() && (clean.back() == '.' || clean.back() == ' ')) {
            clean.pop_back();
        }
        if (clean.empty()) {
            return fallback;
        }
    }

    // 5. 设备名判断只看第一个点之前的部分——"NUL.txt" 在 Windows 上一样是设备。
    const auto dot = clean.find('.');
    const std::string stem = (dot == std::string::npos) ? clean : clean.substr(0, dot);
    if (is_windows_reserved(stem)) {
        clean.insert(clean.begin(), '_');
    }

    return clean;
}

std::string resolve(const std::optional<std::string>& content_disposition,
                    const std::string&                effective_url,
                    const std::string&                fallback) {
    // 优先级：Content-Disposition > URL 末段 > 兜底常量。
    // 每一级的「失败」都是返回 nullopt 或 sanitize 后为空，退到下一级。
    if (content_disposition) {
        if (const auto from_cd = from_content_disposition(*content_disposition)) {
            const std::string name = sanitize(*from_cd, fallback);
            if (name != fallback) {
                return name;
            }
            // sanitize 把它清成空了（比如服务端只发了 ".."）——不能就此收工，
            // 继续退到 URL，那里往往有个正常名字。
        }
    }

    if (const auto from_u = from_url(effective_url)) {
        const std::string name = sanitize(*from_u, fallback);
        if (name != fallback) {
            return name;
        }
    }

    return fallback;
}

}   // namespace FilenameResolver
