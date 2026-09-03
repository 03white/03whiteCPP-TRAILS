#include "check.hpp"

// ============================================================================
// 第二层：状态码解读逻辑的单测。
//
// 现在是空的，因为**这些测试还写不了** —— 需要你先做一次重构。
//
// 现状：Probe::probe_range_get() 里，「设 curl 选项 + perform」和
//       「200/206/416 分别意味着什么」焊在同一个函数里。想测后半段，
//       就必须真的发起一次 HTTP 请求，还得有个会那样返回的服务端。
//
// 重构目标：把决策摘成不碰网络的纯函数。
//
//     // Probe.hpp
//     static bool interpret_range_get(long status,
//                                     const ResponseHeaders& headers,
//                                     DownloadPlan& plan);
//     static bool interpret_head(long status,
//                                const ResponseHeaders& headers,
//                                DownloadPlan& plan);
//
//     // Probe.cpp
//     bool Probe::probe_range_get(CurlEasy& curl, DownloadPlan& plan) {
//         ... 设选项、perform、填 effective_url / probe_status ...
//         return interpret_range_get(plan.probe_status, headers_, plan);
//     }
//
// 摘出来之后，下面每一行表都是一个不用联网、微秒级的用例。
// 构造输入用 test_response_headers.cpp 里那个 feed() 手法。
//
// 建议在动手写 12-05 之前做完 —— 续传要大量复用这套判断，
// 到那时再拆，改动面翻倍。
// ============================================================================

void test_probe_interpret() {
    check::suite("Probe::interpret_* (enable after refactor)");

    // ---- TODO: interpret_range_get ----
    //
    //   status  关键响应头                        返回  supports_range  total_size
    //   206     Content-Range: bytes 0-0/1234     true  true            1234
    //   206     Content-Range: bytes 0-0/*        true  true            none
    //   206     （无 Content-Range）              false false           none
    //   206     Content-Range: bytes 0-99/1234    false false           none
    //   206     Content-Range: 垃圾                false false           none
    //   200     Content-Length: 1234              true  false           1234
    //   200     Content-Length: 0                 true  false           0
    //   200     Content-Length: -1                true  false           none
    //   200     Content-Length: abc               true  false           none
    //   200     （无 Content-Length）             true  false           none
    //   200     CL:1234 + Accept-Ranges: bytes    true  false           1234   + notes 记矛盾
    //   416     Content-Range: bytes */0          true  true            0
    //   416     Content-Range: bytes */1234       true  true            1234
    //   416     （无 Content-Range）              false false           none
    //   405     —                                 false false           none
    //   404     —                                 false false           none
    //   500     —                                 false false           none
    //
    // 第 4 行（206 但给的区间不是 0-0）和第 8/9 行（畸形 Content-Length）
    // 是端到端测试**跑不到**的分支 —— probe_server.py 不会那么返回。
    // 这三行只有在这一层才测得到，而它们恰恰是最危险的：
    //   - 区间不对却当成支持 Range  -> 12-06 拼出内容全错的文件
    //   - "-1" 被 stoull 回绕成 16EB -> 12-06 算出天文数字的块数
    //
    // 第 11 行除了字段，还该断言 notes 里记下了「声明与实测矛盾」——
    // 那条记录是将来排查用户报障时唯一的线索。

    // ---- TODO: interpret_head ----
    //
    //   status  关键响应头                        返回  supports_range  total_size
    //   200     Content-Length: 1234              true  false           1234
    //   200     Accept-Ranges: bytes + CL:1234    true  **false**       1234
    //   200     （无 Content-Length）             false false           none
    //   404     —                                 false false           none
    //   301     —                                 false false           none
    //
    // 第 2 行是这一层的核心断言，而且反直觉：服务端明说支持 Range，
    // 我们**仍然**填 false。
    //
    // 理由：Accept-Ranges 是声明，206 是行为。中间任何一层（CDN、反代、
    // 压缩中间件）都可能透传声明却吃掉行为。而且 HEAD 往往走和 GET
    // 不同的代码路径，声明的可信度更低。
    // 宁可退化成单线程慢一点，也不能误判成 RangedParallel 产出坏文件。
    //
    // 把它写成测试，就是把这个决定钉死 —— 将来有人觉得「这里明明说支持啊」
    // 想改成 true 时，红灯会拦住他。
}
