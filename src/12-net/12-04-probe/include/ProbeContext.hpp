#pragma once

// 探测的输入参数。纯数据，无行为。
// 和 DownloadPlan 是一进一出的关系：ProbeContext -> Probe::run() -> DownloadPlan。

#include <string>
#include <vector>

struct ProbeContext {
    std::string url;
    // ---- 网络行为 ----
    bool follow_redirects = true;    // CURLOPT_FOLLOWLOCATION
    long max_redirects    = 10L;     // CURLOPT_MAXREDIRS，防重定向环
    long connect_timeout_s = 10L;    // CURLOPT_CONNECTTIMEOUT
    long total_timeout_s   = 30L;    // CURLOPT_TIMEOUT，探测请求只传 1 字节，不该久
    std::string user_agent = "12-04-probe/0.1";
    std::vector<std::string> extra_headers;
    bool use_range_get = true;
    bool fallback_to_head = true;
    bool verbose = false;            // CURLOPT_VERBOSE，打印完整的请求/响应头
};
