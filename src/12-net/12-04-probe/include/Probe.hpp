#pragma once

// 探测器：ProbeContext 进，DownloadPlan 出。
//
// 两枪结构（roadmap 阶段 3）：
//   第一枪  GET + "Range: bytes=0-0"     —— 主力
//   第二枪  HEAD                          —— 仅当第一枪没有结论
//
// 第一枪可能返回 206 / 200 / 405 / 416 / 别的。把这几档分别意味着什么、
// 各自该往 DownloadPlan 里填什么，先在纸上列清楚再写代码。
// 这是整个下载器最容易写错的一环，写错了不会报错，只会默默产出坏文件。

#include "DownloadPlan.hpp"
#include "ProbeContext.hpp"
#include "ResponseHeaders.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

class CurlEasy;   // 前置声明，头文件不拖 curl/curl.h 进来

class Probe {
public:
    explicit Probe(ProbeContext ctx);
    DownloadPlan run();
    const ProbeContext& context() const noexcept { return ctx_; }
    static TransferMode decide_mode(const DownloadPlan& plan) noexcept;
private:
    ProbeContext    ctx_;//输入参数
    ResponseHeaders headers_;//回调填充的响应头
    std::uint64_t   body_bytes_seen_ = 0; //响应体已经收到的字节数
    static constexpr std::uint64_t kMaxProbeBody = 64 * 1024; //响应体最大允许接受字节数

    static std::size_t discard_body(char* ptr, std::size_t size,std::size_t nmemb, void* userdata);
    bool probe_range_get(CurlEasy& curl, DownloadPlan& plan); //利用curl进行第一次请求，结果存进plan
    bool probe_head(CurlEasy& curl, DownloadPlan& plan);     //利用curl进行第二次请求，结果存进plan
    void fill_common_fields(DownloadPlan& plan) const; //请求前填充公共头
    void apply_common_options(CurlEasy& curl);  //设置回调前的选项
};
