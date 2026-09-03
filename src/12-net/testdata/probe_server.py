#!/usr/bin/env python3
"""12-04-probe 的验收用服务器。

    python probe_server.py [port]        # 默认 8001

路由 —— 对应 main.cpp 里的四类验收目标：

    /ranged/<name>      支持 Range，返回 206 + Content-Range   -> RangedParallel
    /ignore/<name>      收到 Range 也照样返回 200 + 全文        -> SingleResumable
    /chunked            Transfer-Encoding: chunked，无总长      -> StreamingChunked
    /redirect/<name>    302 跳到 /ranged/<name>                 -> effective_url 是跳完的
    /empty              零长文件，Range 0-0 落空                 -> 416
    /cd                 Content-Disposition 中文名 + 路径穿越   -> 文件名解析/消毒
    /liar/<name>        声称 Accept-Ranges: bytes 但返回 200    -> 声明与行为矛盾

文件从本目录下取（test_100m.bin / test_1g.bin）。
"""
import os
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote

HERE = os.path.dirname(os.path.abspath(__file__))
CHUNK = 64 * 1024


def safe_path(name):
    name = unquote(name).lstrip("/")
    if not name or "/" in name or "\\" in name or name.startswith("."):
        return None
    path = os.path.join(HERE, name)
    return path if os.path.isfile(path) else None


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "probe-testbed/1.0"

    def log_message(self, fmt, *args):
        rng = self.headers.get("Range", "-")
        sys.stderr.write("%-6s %-28s Range:%-14s -> %s\n"
                         % (self.command, self.path, rng, args[1]))

    # ---- 公共 ----

    def _stat(self, path):
        st = os.stat(path)
        return st.st_size, time.strftime("%a, %d %b %Y %H:%M:%S GMT",
                                         time.gmtime(st.st_mtime))

    def _send_body(self, path, start, end, head_only):
        """[start, end] 闭区间，按 curl/HTTP 语义。"""
        if head_only:
            return
        remaining = end - start + 1
        with open(path, "rb") as f:
            f.seek(start)
            while remaining > 0:
                buf = f.read(min(CHUNK, remaining))
                if not buf:
                    break
                remaining -= len(buf)
                try:
                    self.wfile.write(buf)
                except (BrokenPipeError, ConnectionAbortedError, ConnectionResetError):
                    # 探测器读够 1 字节就掐断连接，这里断掉是预期行为，不是错误。
                    return

    def _parse_range(self, total):
        """只支持 'bytes=a-b' / 'bytes=a-'。返回 (start, end) 或 None，越界返回 'unsat'。"""
        raw = self.headers.get("Range")
        if not raw:
            return None
        m = re.fullmatch(r"bytes=(\d*)-(\d*)", raw.strip())
        if not m:
            return None
        a, b = m.group(1), m.group(2)
        if a == "" and b == "":
            return None
        if a == "":                       # bytes=-N 后缀区间
            n = int(b)
            if n == 0 or total == 0:
                return "unsat"
            return (max(0, total - n), total - 1)
        start = int(a)
        end = int(b) if b else total - 1
        if start >= total or start > end:
            return "unsat"
        return (start, min(end, total - 1))

    # ---- 路由 ----

    def do_GET(self):
        self._route(head_only=False)

    def do_HEAD(self):
        self._route(head_only=True)

    def _route(self, head_only):
        path = self.path.split("?", 1)[0].split("#", 1)[0]

        if path == "/chunked":
            return self.route_chunked(head_only)
        if path == "/empty":
            return self.route_empty()
        if path == "/cd":
            return self.route_content_disposition(head_only)
        for prefix, fn in (("/ranged/", self.route_ranged),
                           ("/ignore/", self.route_ignore),
                           ("/liar/", self.route_liar),
                           ("/redirect/", self.route_redirect)):
            if path.startswith(prefix):
                return fn(path[len(prefix):], head_only)

        self.send_error(404, "no such route")

    def route_ranged(self, name, head_only):
        path = safe_path(name)
        if not path:
            return self.send_error(404)
        total, mtime = self._stat(path)
        rng = self._parse_range(total)

        if rng == "unsat":
            self.send_response(416)
            self.send_header("Content-Range", "bytes */%d" % total)
            self.send_header("Content-Length", "0")
            self.send_header("Accept-Ranges", "bytes")
            self.end_headers()
            return

        if rng is None:
            self.send_response(200)
            self.send_header("Content-Length", str(total))
            start, end = 0, total - 1
        else:
            start, end = rng
            self.send_response(206)
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, total))
            self.send_header("Content-Length", str(end - start + 1))

        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Last-Modified", mtime)
        self.send_header("ETag", '"%x-%x"' % (total, int(os.stat(path).st_mtime)))
        self.end_headers()
        self._send_body(path, start, end, head_only)

    def route_ignore(self, name, head_only):
        """收到 Range 也装没看见。这是最危险的服务端行为。"""
        path = safe_path(name)
        if not path:
            return self.send_error(404)
        total, mtime = self._stat(path)
        self.send_response(200)
        self.send_header("Content-Length", str(total))
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Last-Modified", mtime)
        self.end_headers()
        self._send_body(path, 0, total - 1, head_only)

    def route_liar(self, name, head_only):
        """声明和行为矛盾：说支持 Range，实际忽略。探测器必须信实测。"""
        path = safe_path(name)
        if not path:
            return self.send_error(404)
        total, mtime = self._stat(path)
        self.send_response(200)
        self.send_header("Content-Length", str(total))
        self.send_header("Accept-Ranges", "bytes")        # <- 谎言
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Last-Modified", mtime)
        self.end_headers()
        self._send_body(path, 0, total - 1, head_only)

    def route_chunked(self, head_only):
        self.send_response(200)
        self.send_header("Transfer-Encoding", "chunked")
        self.send_header("Content-Type", "application/octet-stream")
        self.end_headers()
        if head_only:
            return
        try:
            for _ in range(16):
                blk = b"x" * CHUNK
                self.wfile.write(b"%x\r\n" % len(blk) + blk + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        except (BrokenPipeError, ConnectionAbortedError, ConnectionResetError):
            pass

    def route_empty(self):
        """零长文件：Range: bytes=0-0 必然落空 -> 416 + Content-Range: bytes */0"""
        self.send_response(416)
        self.send_header("Content-Range", "bytes */0")
        self.send_header("Content-Length", "0")
        self.send_header("Accept-Ranges", "bytes")
        self.end_headers()

    def route_content_disposition(self, head_only):
        """文件名来自服务端 = 不可信输入。这里同时塞路径穿越和 RFC 5987 中文名。"""
        body = b"hello"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.send_header(
            "Content-Disposition",
            'attachment; filename="../../evil.exe"; '
            "filename*=UTF-8''%E4%B8%AD%E6%96%87%20%E6%8A%A5%E5%91%8A.bin")
        self.send_header("Content-Type", "application/octet-stream")
        self.end_headers()
        if not head_only:
            self.wfile.write(body)

    def route_redirect(self, name, head_only):
        self.send_response(302)
        self.send_header("Location", "/ranged/" + name)
        self.send_header("Content-Length", "0")     # 这一跳的头不该污染最终结果
        self.end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8001
    srv = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print("probe testbed on http://127.0.0.1:%d  (Ctrl-C to stop)" % port)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
