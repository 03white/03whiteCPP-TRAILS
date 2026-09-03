// 用法: 12-04-probe <url> [-v]
//
//   12-04-probe http://127.0.0.1:8000/test_100m.bin
//   12-04-probe http://127.0.0.1:8000/test_100m.bin -v
//
// 测试服务器: cd src/12-net/testdata && python -m http.server 8000
//
// 验收（roadmap 阶段 3）：下面四类目标都要输出正确的计划
//   1. 支持 Range 的静态文件   -> RangedParallel
//   2. 忽略 Range 的服务器     -> SingleResumable（绝不能报 RangedParallel）
//   3. chunked 接口            -> StreamingChunked
//   4. 需要重定向的 URL        -> effective_url 是跟完之后的地址

#include "CurlEasy.hpp"
#include "DownloadPlan.hpp"
#include "Probe.hpp"
#include "ProbeContext.hpp"

#include <iostream>
#include <string>

namespace {

void print_plan(const DownloadPlan& plan);

}   // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <url> [-v]" << std::endl;
        return 2;
    }
    ProbeContext ctx;
    ctx.url = argv[1];
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "-v") ctx.verbose = true;
    }

    try {
        GlobalCurlGuard curl_guard;
        Probe probe(ctx);
        const DownloadPlan plan = probe.run();
        print_plan(plan);
        return plan.mode == TransferMode::Unknown ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << std::endl;
        return 1;
    }
}
namespace{

void print_plan(const DownloadPlan& plan) {
    std::cout << "---- download plan ----\n";
    std::cout << "requested url : " << plan.requested_url << "\n";
    std::cout << "effective url : " << plan.effective_url << "\n";
    std::cout << "probe status  : " << plan.probe_status << "\n";
    std::cout << "total size    : ";
    if (plan.total_size) {
        std::cout << *plan.total_size << " bytes\n";
    } else {
        std::cout << "(unknown)\n";
    }
    std::cout << "range support : " << (plan.supports_range ? "yes (206 verified)" : "no")
              << "\n";
    std::cout << "accept-ranges : " << plan.accept_ranges_header.value_or("(absent)")
              << "   <- just fo reference\n";
    std::cout << "etag          : " << plan.etag.value_or("(absent)") << "\n";
    std::cout << "last-modified : " << plan.last_modified.value_or("(absent)") << "\n";
    std::cout << "content-type  : " << plan.content_type.value_or("(absent)") << "\n";
    std::cout << "filename      : " << plan.suggested_filename << "\n";
    std::cout << "mode          : " << to_string(plan.mode) << "\n";
    if (!plan.notes.empty()) {
        std::cout << "notes:\n" << plan.notes;
    }
    std::cout << "-----------------------" << std::endl;
}

}