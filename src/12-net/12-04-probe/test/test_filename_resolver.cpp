#include "FilenameResolver.hpp"
#include "check.hpp"

void test_filename_resolver() {
    check::suite("FilenameResolver");

    // ================= 示范 =================
    {
        // sanitize：路径穿越必须被剥掉
        CHECK_EQ(FilenameResolver::sanitize("../../autoexec.bat"),
                 std::string("autoexec.bat"));

        // from_content_disposition：filename* 优先于 filename（RFC 6266 §4.3）
        const auto name = FilenameResolver::from_content_disposition(
            "attachment; filename=\"a.bin\"; filename*=UTF-8''b.bin");
        CHECK_EQ(name, std::optional<std::string>{"b.bin"});

        // from_url：砍掉 query
        CHECK_EQ(FilenameResolver::from_url("http://h/a/b/c.bin?x=1"),
                 std::optional<std::string>{"c.bin"});
    }
    // ========================================

    // ---- TODO: sanitize —— 攻击面清单 ----
    //
    // 这张表就是 FilenameResolver.hpp 里那句「文件名来自服务端，是不可信输入」
    // 的具体展开。每一行都是一个真实存在过的攻击或坑：
    //
    //   输入                     期望                 挡的是什么
    //   "../../autoexec.bat"     "autoexec.bat"       ✔ 上面已示范，路径穿越
    //   "..\\..\\evil.exe"       "evil.exe"           Windows 反斜杠也是分隔符
    //   "C:\\Windows\\x"         "x"                  绝对路径
    //   "/etc/passwd"            "passwd"             绝对路径（POSIX 风格）
    //   ".."                     fallback             剥完只剩点
    //   "."                      fallback
    //   ""                       fallback
    //   "   "                    fallback             全空白
    //   "a\x01b.bin"             "ab.bin"             控制字符，NTFS 直接拒
    //   "a<b>c:d.bin"            "abcd.bin"           Windows 保留字符
    //   "a.txt."                 "a.txt"              Windows 静默吃尾部点
    //   "a.txt   "               "a.txt"              尾部空格同理
    //   "NUL"                    "_NUL"               设备名
    //   "nul"                    "_nul"               大小写不敏感
    //   "NUL.txt"                "_NUL.txt"           带扩展名一样是设备
    //   "COM1" / "LPT9"          "_COM1" / "_LPT9"
    //   "CONSOLE.txt"            "CONSOLE.txt"        ← 不是保留字，别误伤
    //   300 个 'a' + ".bin"      长度 <= 200
    //   150 个汉字 + ".bin"      截断后仍是合法 UTF-8（别切断多字节序列）
    //
    // 倒数第一条怎么验：把结果的每个字节走一遍 UTF-8 状态机，
    // 或者简单点 —— 确认最后一个字节不是 0x80..0xBF 的续字节。
    //
    // 尾部点那两条想清楚：为什么必须处理？
    // 提示：你写 "a.txt." 但系统实际创建的是 "a.txt"。续传时你按 "a.txt.part"
    // 去找，找到的却是别的名字 —— 断点续传直接失效，而且不报错。

    // ---- TODO: from_content_disposition —— RFC 5987 / 6266 ----
    //
    //   输入                                                     期望
    //   "attachment"                                             none（无 filename）
    //   "attachment; filename=a.bin"                             "a.bin"（无引号）
    //   "attachment; filename=\"a.bin\""                         "a.bin"（去引号）
    //   "inline; filename=\"a.bin\""                             "a.bin"（inline 也算）
    //   "attachment; filename*=UTF-8''%E4%B8%AD.bin"             "中.bin"
    //   "attachment; filename=\"a\"; filename*=UTF-8''b"         "b"  ✔ 已示范
    //   "attachment; filename*=UTF-8''b; filename=\"a\""         "b"（顺序无关！）
    //   "attachment; FILENAME=\"a.bin\""                         "a.bin"（参数名不敏感）
    //   "attachment; filename*=''x.bin"                          "x.bin"（charset 为空）
    //   "attachment; filename*=UTF-8'zh-CN'%E4%B8%AD.bin"        "中.bin"（带 lang 段）
    //   "attachment; filename*=broken"                           退回 filename 或 none
    //   "attachment; filename=\"a;b.bin\""                       "a;b.bin"（引号内分号）
    //   ""                                                       none
    //
    //   "顺序无关" 那条值得单独测：很多实现是「边扫边覆盖」，
    //   filename 出现在 filename* 后面就会把它盖掉。
    //
    //   "引号内分号" 那条会考验 split_params 的引号状态机。

    // ---- TODO: from_url ----
    //
    //   输入                              期望
    //   "http://h/a/b/c.bin?x=1"          "c.bin"   ✔ 已示范
    //   "http://h/a/b/c.bin#frag"         "c.bin"
    //   "http://h/a/b/c.bin?x=a#f"        "c.bin"
    //   "http://h/c.bin"                  "c.bin"
    //   "http://h"                        none      没有 path
    //   "http://h/"                       none      path 是根
    //   "http://h/a/b/"                   none      以 / 结尾，是目录
    //   "https://h:8080/a/c.bin"          "c.bin"   端口号别干扰
    //   "http://h/%E4%B8%AD.bin"          "中.bin"  percent-decode
    //   "http://user:pw@h/c.bin"          "c.bin"   userinfo 里有 @ 和 :
    //   ""                                none
    //   "c.bin"                           none 还是 "c.bin"？ ← 你来定，写进用例
    //
    //   "http://h" 那条是关键：如果你直接 rfind('/')，会取到 "//h" 里的斜杠，
    //   返回 "h" —— 把主机名当成文件名。

    // ---- TODO: resolve —— 三级降级 ----
    //
    //   CD 有效                    -> 用 CD 的名字
    //   CD 为 none                 -> 退到 URL
    //   CD 存在但解析不出 filename -> 退到 URL
    //   CD 解析出 ".."（sanitize 后为空）-> 退到 URL，不是直接 fallback
    //   CD 和 URL 都失败           -> fallback
    //
    //   第四条是个坑：如果实现写成「CD 解析成功就 return sanitize(...)」，
    //   服务端发个 filename=".." 就能让你拿到 fallback，而 URL 里明明有好名字。
    //
    //   端到端对照：testdata/probe_server.py 的 /cd 路由同时发了
    //   filename="../../evil.exe" 和 filename*=UTF-8''中文 报告.bin，
    //   正确结果是 "中文 报告.bin"。
}
