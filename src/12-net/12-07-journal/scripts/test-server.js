'use strict';

/*
 * 12-07-journal 测试服务器（纯 Node 内置模块，无第三方依赖）。
 *
 * 用途：给多线程分块下载器提供一个可控的 HTTP 下载源，覆盖 core-design.md §12
 * 的验收场景。
 *
 * 特性：
 *   - 确定性虚拟文件（AES-256-CTR 密钥流），任意大小，内容跨请求稳定
 *   - Range 支持（单区间），HEAD 探测（Content-Length / Accept-Ranges）
 *   - 全文件 SHA256（HEAD 头 + /meta 接口），用于下载后对账
 *   - 限速（--rate，字节/秒），用于制造"慢到能手动 taskkill"的窗口
 *   - 中途断连（?drop=N 或 --drop），用于模拟网络中断（§12 #7）
 *
 * 用法：
 *   node scripts/test-server.js --size 128 --rate 2097152
 *   # 128 MB 文件，限速 2 MB/s
 *
 * 验证下载结果（Windows）：
 *   Get-FileHash -Algorithm SHA256 <output>\file.bin
 */

const http = require('http');
const crypto = require('crypto');

const BLOCK = 16;        // AES-CTR 计数器块大小（字节）
const CHUNK = 256 * 1024; // 单次写出的字节数

// ---------------------------------------------------------------------------
// 参数解析
// ---------------------------------------------------------------------------
function parseArgs(argv) {
    const opts = {
        port: 8000,
        sizeMb: 64,      // 默认 64 MB
        seed: 0x12_07,
        rate: 0,         // 0 = 不限速
        drop: 0,         // 0 = 不主动断连
    };

    const needs = new Set(['port', 'size', 'seed', 'rate', 'drop']);
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i];
        if (a === '-h' || a === '--help') {
            printHelp();
            process.exit(0);
        }
        const m = /^--([a-z]+)(?:=(.*))?$/.exec(a);
        if (!m || !needs.has(m[1])) {
            console.error(`unknown argument: ${a}`);
            printHelp();
            process.exit(2);
        }
        const val = m[2] !== undefined ? m[2] : argv[++i];
        if (val === undefined) {
            console.error(`missing value for --${m[1]}`);
            process.exit(2);
        }
        const n = Number(val);
        switch (m[1]) {
            case 'port': opts.port = n; break;
            case 'size': opts.sizeMb = n; break;
            case 'seed': opts.seed = n; break;
            case 'rate': opts.rate = n; break;
            case 'drop': opts.drop = n; break;
        }
    }

    if (!Number.isFinite(opts.port) || opts.port <= 0 || opts.port > 65535) {
        console.error('--port must be 1..65535');
        process.exit(2);
    }
    if (!Number.isFinite(opts.sizeMb) || opts.sizeMb <= 0) {
        console.error('--size must be a positive number of MB');
        process.exit(2);
    }
    if (!Number.isFinite(opts.rate) || opts.rate < 0) {
        console.error('--rate must be >= 0');
        process.exit(2);
    }
    if (!Number.isFinite(opts.drop) || opts.drop < 0) {
        console.error('--drop must be >= 0');
        process.exit(2);
    }
    return opts;
}

function printHelp() {
    console.log(`usage: node test-server.js [options]
  --port N     监听端口 (默认 8000)
  --size N     文件大小，单位 MB (默认 64)
  --seed N     内容种子，影响生成字节 (默认 0x1207)
  --rate N     限速，字节/秒；0 = 不限速 (默认 0)
  --drop N     每请求发送 N 字节后主动断开；0 = 不主动断开 (默认 0)
  --help`);
}

// ---------------------------------------------------------------------------
// 确定性虚拟文件：内容 = AES-256-CTR 密钥流，支持按任意偏移取字节
// ---------------------------------------------------------------------------
class DeterministicFile {
    constructor(sizeBytes, seed) {
        this.size = sizeBytes;
        this.key = crypto
            .createHash('sha256')
            .update(`12-07-journal:${seed}`)
            .digest(); // 32 字节密钥
        this._sha256 = null;
    }

    // 取 [offset, offset+length) 的确定性字节
    bytes(offset, length) {
        const blockStart = Math.floor(offset / BLOCK);
        const skip = offset % BLOCK;

        const iv = Buffer.alloc(16);
        // 计数器起始值 = blockStart（低 32 位足够：offset/16 < 2^32 ≈ 64 GB）
        iv.writeUInt32BE(blockStart >>> 0, 12);

        const cipher = crypto.createCipheriv('aes-256-ctr', this.key, iv);
        const zeros = Buffer.alloc(length + skip);
        const keystream = Buffer.concat([cipher.update(zeros), cipher.final()]);
        return keystream.slice(skip, skip + length);
    }

    sha256() {
        if (this._sha256 === null) {
            const h = crypto.createHash('sha256');
            const step = 4 * 1024 * 1024;
            for (let off = 0; off < this.size; off += step) {
                h.update(this.bytes(off, Math.min(step, this.size - off)));
            }
            this._sha256 = h.digest('hex');
        }
        return this._sha256;
    }
}

// ---------------------------------------------------------------------------
// Range 解析：只支持单区间 "bytes=start-end" / "bytes=start-" / "bytes=-suffix"
// ---------------------------------------------------------------------------
function parseRange(header, size) {
    const m = /^bytes=(\d*)-(\d*)$/.exec(header.trim());
    if (!m) return { error: true };
    const [, a, b] = m;

    let start, end;
    if (a === '' && b === '') {
        return { error: true };
    } else if (a === '') {
        // 末尾 N 字节
        const suffix = Number(b);
        if (suffix === 0) return { error: true };
        start = Math.max(0, size - suffix);
        end = size - 1;
    } else {
        start = Number(a);
        end = b === '' ? size - 1 : Number(b);
        if (end >= size) end = size - 1;
    }

    if (start > end || start >= size) return { error: true };
    return { error: false, start, end };
}

// ---------------------------------------------------------------------------
// HTTP 处理
// ---------------------------------------------------------------------------
function main() {
    const opts = parseArgs(process.argv.slice(2));
    const file = new DeterministicFile(opts.sizeMb * 1024 * 1024, opts.seed);

    const server = http.createServer((req, res) => {
        const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);

        if (url.pathname === '/meta') {
            handleMeta(req, res, file);
            return;
        }

        if (req.method === 'HEAD') {
            handleHead(req, res, file);
        } else if (req.method === 'GET') {
            handleGet(req, res, url, file, opts);
        } else {
            res.writeHead(405, { Allow: 'GET, HEAD' });
            res.end();
        }
    });

    server.listen(opts.port, () => {
        console.log(`test-server listening on http://127.0.0.1:${opts.port}`);
        console.log(`  file size : ${opts.sizeMb} MB (${file.size} bytes)`);
        console.log(`  seed      : ${opts.seed}`);
        console.log(`  rate      : ${opts.rate === 0 ? 'unlimited' : opts.rate + ' B/s'}`);
        console.log(`  drop      : ${opts.drop === 0 ? 'never' : opts.drop + ' bytes'}`);
        console.log('  sha256    : computing on first request (or see /meta) ...');
    });
}

function baseHeaders(file) {
    return {
        'Content-Type': 'application/octet-stream',
        'Accept-Ranges': 'bytes',
        'X-Checksum-Sha256': file.sha256(),
    };
}

function handleMeta(req, res, file) {
    const body = JSON.stringify({
        size: file.size,
        sha256: file.sha256(),
    });
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(body);
}

function handleHead(req, res, file) {
    res.writeHead(200, {
        ...baseHeaders(file),
        'Content-Length': file.size,
    });
    res.end();
}

function handleGet(req, res, url, file, opts) {
    const q = url.searchParams;
    const rate = q.has('rate') ? Number(q.get('rate')) : opts.rate;
    const drop = q.has('drop') ? Number(q.get('drop')) : opts.drop;

    const rangeHeader = req.headers.range;
    let status = 200;
    let start = 0;
    let end = file.size - 1;

    if (rangeHeader) {
        const r = parseRange(rangeHeader, file.size);
        if (r.error) {
            res.writeHead(416, { 'Content-Range': `bytes */${file.size}` });
            res.end();
            return;
        }
        start = r.start;
        end = r.end;
        status = 206;
    }

    const total = end - start + 1;
    const headers = {
        ...baseHeaders(file),
        'Content-Length': total,
    };
    if (status === 206) {
        headers['Content-Range'] = `bytes ${start}-${end}/${file.size}`;
    }
    res.writeHead(status, headers);

    streamRange(req, res, file, start, total, { rate, drop });
}

function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

function once(emitter, event) {
    return new Promise((resolve) => emitter.once(event, resolve));
}

async function streamRange(req, res, file, start, total, opts) {
    const chunkSize = Math.min(CHUNK, opts.rate > 0 ? Math.max(4096, opts.rate / 10) : CHUNK);

    let sent = 0;
    let pos = start;

    req.on('error', () => res.destroy());
    res.on('error', () => {});

    try {
        while (sent < total) {
            const n = Math.min(chunkSize, total - sent);
            const buf = file.bytes(pos, n);

            if (!res.write(buf)) {
                await once(res, 'drain');
            }
            sent += n;
            pos += n;

            if (opts.drop > 0 && sent >= opts.drop) {
                console.log(`  [drop] connection closed after ${sent} bytes`);
                res.destroy();
                return;
            }

            if (opts.rate > 0) {
                await sleep((n / opts.rate) * 1000);
            }
        }
        res.end();
        log(req, start, sent);
    } catch (err) {
        res.destroy();
    }
}

function log(req, start, bytes) {
    const r = req.headers.range || '-';
    console.log(
        `${req.socket.remoteAddress} "${req.method} ${req.url}" ` +
        `range=${r} offset=${start} sent=${bytes}`
    );
}

main();
