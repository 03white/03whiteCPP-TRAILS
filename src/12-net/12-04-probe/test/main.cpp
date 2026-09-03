// 12-04-probe 的单元测试入口。
//
//   cmake --build --preset msvc-debug --target 12-04-probe-test
//   ctest --preset msvc-debug --output-on-failure          # 或直接跑 exe
//
// 这里的测试**全部不联网**。需要真服务端的端到端验收走 testdata/probe_server.py。

#include "check.hpp"

void test_content_range();
void test_response_headers();
void test_filename_resolver();
void test_decide_mode();
void test_probe_interpret();

int main() {
    test_content_range();
    test_response_headers();
    test_filename_resolver();
    test_decide_mode();
    test_probe_interpret();

    // 退出码非 0 时 CTest 判定失败。
    return check::summary();
}
