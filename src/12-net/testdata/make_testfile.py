#!/usr/bin/env python3
"""生成下载器测试用的固定内容大文件。

用随机内容而不是全零：阶段 5 预分配的文件区域也是零，
如果某个分块根本没下载成功，全零测试文件的 MD5 依然会对上，
最要命的 bug 反而被掩盖。

种子固定 -> 同样的大小永远生成同样的字节，MD5 可以直接对照。

    python make_testfile.py 100M
    python make_testfile.py 1G
"""
import hashlib
import random
import sys

SEED = 20260826
CHUNK = 1 << 20  # 1 MiB

UNITS = {"K": 1 << 10, "M": 1 << 20, "G": 1 << 30}


def parse_size(text):
    text = text.strip().upper().rstrip("B")
    if text and text[-1] in UNITS:
        return int(float(text[:-1]) * UNITS[text[-1]])
    return int(text)


def main():
    spec = sys.argv[1] if len(sys.argv) > 1 else "100M"
    size = parse_size(spec)
    path = sys.argv[2] if len(sys.argv) > 2 else f"test_{spec.lower()}.bin"

    rng = random.Random(SEED)
    digest = hashlib.md5()
    written = 0

    with open(path, "wb") as fp:
        while written < size:
            n = min(CHUNK, size - written)
            block = rng.randbytes(n)
            fp.write(block)
            digest.update(block)
            written += n

    print(f"{path}  {written} bytes  MD5={digest.hexdigest()}")


if __name__ == "__main__":
    main()
