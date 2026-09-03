#pragma once
#include <cstdint>
#include <optional>
#include <string>


enum class TransferMode {
    Unknown,            // 探测失败或还没跑
    RangedParallel,     // 多线程分块 + 断点续传
    SingleResumable,    // 单线程，可续传
    StreamingChunked    // 单线程流式，无进度百分比，不可续传
};

inline const char* to_string(TransferMode mode) noexcept {
    switch (mode) {
        case TransferMode::RangedParallel:   return "RangedParallel";
        case TransferMode::SingleResumable:  return "SingleResumable";
        case TransferMode::StreamingChunked: return "StreamingChunked";
        case TransferMode::Unknown:
        default:                             return "Unknown";
    }
}

struct DownloadPlan {
    std::string requested_url;      
    std::string effective_url;     
    std::optional<std::uint64_t> total_size;
    bool                       supports_range = false;
    std::optional<std::string> accept_ranges_header;
    std::optional<std::string> etag;
    std::optional<std::string> last_modified;
    std::string                suggested_filename;
    std::optional<std::string> content_type;
    TransferMode mode = TransferMode::Unknown;
    std::string notes;
    long probe_status = 0;         
    bool size_known() const noexcept { return total_size.has_value(); }
};
