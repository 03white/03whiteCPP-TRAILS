#include "Probe.hpp"
#include "check.hpp"

namespace {

DownloadPlan plan_with(std::optional<std::uint64_t> size, bool range) {
    DownloadPlan p;
    p.total_size     = size;
    p.supports_range = range;
    return p;
}

}   // namespace

void test_decide_mode() {
    check::suite("Probe::decide_mode");

    // ================= 示范：四种组合中的两种 =================
    {
        // 已知总长 + 实测支持 Range -> 能切块
        CHECK_EQ(Probe::decide_mode(plan_with(1234, true)),
                 TransferMode::RangedParallel);

        // 总长未知 -> supports_range 是什么都不重要，切不了
        CHECK_EQ(Probe::decide_mode(plan_with(std::nullopt, true)),
                 TransferMode::StreamingChunked);
    }
    // =========================================================

    // ---- TODO: 补齐剩下的组合 ----
    //
    //   total_size   supports_range   期望
    //   1234         true             RangedParallel     ✔ 已示范
    //   1234         false            SingleResumable
    //   none         true             StreamingChunked   ✔ 已示范
    //   none         false            StreamingChunked
    //   0            true             SingleResumable    ← 零长文件的短路
    //   0            false            SingleResumable
    //
    // 这张表就是「四种组合为什么只有三档」的答案。写完你会发现
    // 最后两行让它其实是五种输入映射到三档。
    //
    // 零长那两行别漏：放行到 RangedParallel 的话，12-06 会拿 0 去算切点。

    // ---- TODO: 边界大小 ----
    //
    //   total_size = 1        -> RangedParallel？单字节文件切成几块？
    //   total_size = UINT64_MAX -> 不能崩，不能溢出
    //
    // 第一条是个真问题，但答案不在这里 —— decide_mode 只回答「能不能切」，
    // 「切成几块」是 12-06 的事。写用例把这个边界固定住，
    // 免得将来有人图省事在 decide_mode 里塞一个「太小就不并行」的阈值：
    // 那是策略，不是能力判断，塞进来这个函数就不纯了。

    // ---- TODO: Unknown 从哪来 ----
    //
    // decide_mode 永远不返回 Unknown —— 它拿到的 plan 已经是「有结论」的了。
    // Unknown 是 Probe::run() 在两枪都失败时**跳过** decide_mode 留下的初值。
    //
    // 这个区分很重要，写个注释级的用例把它记下来：
    //   DownloadPlan{} 默认构造 -> decide_mode 返回 StreamingChunked，不是 Unknown
    //
    // 也就是说：光看 decide_mode 无法区分「这是流式接口」和「我没探出来」。
    // 那个区分只存在于 run() 的控制流里。这是个设计上的薄弱点 ——
    // 将来如果有人重构时把 run() 里那个 if 删了，测试抓不到。
    // 想想怎么让它抓得到？（提示：给 run() 一个能注入假响应的入口，就是第二层重构）
}
