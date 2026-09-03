#include "ResponseHeaders.hpp"

#include <algorithm>
#include <cctype>

std::string ResponseHeaders::to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string ResponseHeaders::trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

void ResponseHeaders::add_line(const char* data, std::size_t size) {
    if (!data || size == 0) {
        return;
    }
    std::string line(data, size);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    if (line.rfind("HTTP/", 0) == 0) {
        clear();
        // "HTTP/1.1 206 Partial Content" / "HTTP/2 206"
        const auto sp = line.find(' ');
        if (sp != std::string::npos) {
            std::size_t i = sp;
            while (i < line.size() && line[i] == ' ') ++i;
            int code = 0;
            int digits = 0;
            while (i < line.size() && digits < 3 &&
                   std::isdigit(static_cast<unsigned char>(line[i]))) {
                code = code * 10 + (line[i] - '0');
                ++i;
                ++digits;
            }
            if (digits == 3) {
                status_code_ = code;
            }
        }
        return;
    }
    // 头区结束的空行。不是错误，直接忽略。
    if (line.empty()) {
        return;
    }
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
        // 没有冒号只剩两种可能：obs-fold 续行（以空白开头，RFC 9110 已废弃，
        // 且 libcurl 不会主动折叠），或者服务端发的垃圾。两者都丢掉。
        // 想留 obs-fold 支持的话，在这里把内容 append 到 entries_.back().second。
        return;
    }

    std::string name = to_lower(trim(line.substr(0, colon)));
    if (name.empty()) {
        return;
    }
    entries_.emplace_back(std::move(name), trim(line.substr(colon + 1)));
}

void ResponseHeaders::clear() noexcept {
    entries_.clear();
    status_code_ = 0;
}

std::optional<std::string> ResponseHeaders::get(const std::string& name) const {
    const std::string key = to_lower(name);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        if (it->first == key) {
            return it->second;
        }
    }
    return std::nullopt;
}

std::vector<std::string> ResponseHeaders::get_all(const std::string& name) const {
    const std::string key = to_lower(name);
    std::vector<std::string> out;
    for (const auto& e : entries_) {
        if (e.first == key) {
            out.push_back(e.second);
        }
    }
    return out;
}

bool ResponseHeaders::has(const std::string& name) const {
    const std::string key = to_lower(name);
    return std::any_of(entries_.begin(), entries_.end(),
                       [&key](const Entry& e) { return e.first == key; });
}

std::size_t ResponseHeaders::curl_callback(char* buffer, std::size_t size,
                                           std::size_t nitems, void* userdata) {
    const std::size_t total = size * nitems;
    if(auto* self=static_cast<ResponseHeaders*>(userdata)){
        self->addline(buffer,total);
    }
    return total;
}
