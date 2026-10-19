# 12-07-journal 核心逻辑设计

> 配套 `spec.md` / `interfaces.md`。本文只写**核心逻辑**——多线程分块下载中，
> 内存进度、文件数据、`xxx.meta.json` 三者的落盘顺序与并发模型。
> CLI、journal schema、probe、Range 校验等见 `spec.md`，此处不重复。

---

## 核心逻辑总览

> 工作线程 **写数据 → flush → 推进内存进度**；checkpoint 线程定时
> **加锁 → 快照 → 原子写 meta.json**。铁律：**meta 里记录的进度永远 ≤ 实际已 flush 的字节数。**

---

## 1. 磁盘布局

一个任务三个文件，都躺在 `--output` 指定目录：

```
file.zip            最终产物，存在 == 下载完成
file.zip.part       下载中的数据（预分配到 file_size）
file.zip.meta.json  journal（sidecar），进度真相
```

进度真相只在这一份 `meta.json` 里，`.part` 大小不能说明进度（预分配从第一秒就是满大小）。

---

## 2. 实体

### 2.1 Piece（字节级进度，支持片内续传）

```cpp
struct Piece {
    uint64_t index;
    uint64_t offset;      // 文件偏移
    uint64_t length;      // 分片总长度
    uint64_t downloaded;  // 已下载字节，唯一进度真相
    PieceState state;     // 可选；权威判据是 downloaded == length
    std::string hash;     // 可选校验
};
```

- **片内续传**：`downloaded > 0` 表示这块下了一半，续传从 `offset + downloaded` 发 Range。
- `state` 与 `downloaded` 有冗余关系，两者在**同一把锁内**更新，保证不会出现
  「`state=Done` 但 `downloaded < length`」这种两字段对不上的竞态。
  最简做法是干脆去掉 `state`，由 `downloaded == length` 推导 done。

### 2.2 FileMeta（序列化 DTO + 运行时状态）

```cpp
struct FileMeta {
    std::string name;
    std::string target_url;
    std::string local_url;
    uint64_t totalSize;
    size_t pieceNum;
    uint64_t pieceSize;
    std::vector<PiecePtr> pieces;   // 构造后不可变（见 §6.5）
};
```

- 它是**纯 DTO**：只负责和 JSON 对应、被序列化，不携带任何运行时行为。
- **锁不放在这里**——记账/快照的锁属于运行时状态，归 `PieceManager` 管（见 §2.3）。

### 2.3 PieceManager（进度队列 + 记账）

```cpp
class PieceManager {
    std::optional<PiecePtr> acquire();              // 弹出一块，空则 nullopt
    void advanceDownloaded(const PiecePtr& piece, uint64_t flushed); // flush 之后推进进度
    std::vector<Piece> snapshot() const;            // 供 checkpoint 用，返回值拷贝
    uint64_t generation() const;                    // 记账次数；供 checkpoint 判断"有没有变"
    bool all_done() const;
    void cancel();
    bool cancelled() const;

private:
    std::vector<PiecePtr> queue_;   // 待下载块工作队列（存 PiecePtr，不存 index）
    mutable std::mutex mtx_;        // 唯一一把锁：acquire / 记账 / snapshot 共用
};
```

完整签名见 §11.3。

- `acquire` 把每块只交给一个 worker（线程亲和的来源）。
- `queue_` 存 `PiecePtr` 而非 index：worker 拿到 piece 直接读 `offset/length/downloaded`
  就能构造 Range，不用经 `FileMeta` 反查，worker 不依赖 `FileMeta`。
  `queue_` 里的 `PiecePtr` 与 `FileMeta::pieces` 指向**同一个对象**。
- `advanceDownloaded` 是「记账」的唯一入口，内部负责 `downloaded += flushed`，
  并在 `downloaded == length` 时置 done，同时 `++generation_`。
- `snapshot` 返回 `std::vector<Piece>`（**值**），不是 `vector<PiecePtr>`，更不是引用。

  这一条是整个 checkpoint 的地基，值得说清楚。`vector<PiecePtr>` 有两种含义，两种都不行：

  | 写法 | 问题 |
  | --- | --- |
  | 指向**活对象**（省拷贝） | worker 正在锁外改 `downloaded`/`state`，序列化时无锁并发读 = 数据竞争。而且两字段是两次独立的读，可能写出 `state:"Done"` 配 `downloaded < length` —— 正是 §2.1 警告的跨字段不一致 |
  | 指向 `make_shared<Piece>(*p)` 的**深拷贝** | 安全，但没省拷贝：`*p` 那次拷贝构造和 `push_back(*p)` 是同一次，只是在它之上多了 N 次堆分配。而且这 N 次 `operator new` 发生在**锁内**，还会和 worker 抢分配器 —— 把唯一该优化的指标（`mtx_` 持有时间）反而拉长了 |

  快照的定义就是拷贝，指针绕不过去；能绕过去的那个版本就是会 race 的版本。真想省，
  省的方向是 §7.4 的 generation 跳过（省掉整次毫秒级 checkpoint），而不是这 1µs 的 memcpy。

---

## 3. 三类线程

| 线程 | 数量 | 职责 | 碰不碰磁盘 |
| --- | --- | --- | --- |
| main | 1 | `GlobalCurlGuard`、`Downloader`、收尾 rename | 碰（rename/删 meta） |
| worker | N | `acquire → 下载 → 写盘 → flush → 记账` | 碰 `.part` |
| checkpoint | 1 | 定时快照 + 写 meta.json | 碰 `.meta.json` |

worker **绝不直接写 meta.json**；checkpoint **绝不写 `.part`**。写盘与记账的边界一清二楚。

---

## 4. 铁律（唯一不变量）

### 4.1 字节级铁律

> **meta.json 里记录的 `downloaded`，必须 ≤ 已经 flush 到 OS 缓冲的字节数。**

顺序永远是：

```
写数据(write_at)  →  flush  →  推进 downloaded  →  checkpoint 落 meta.json
```

「推进 downloaded」只能发生在 flush 之后，而且只推进「刚 flush 掉」的量。
反过来写：先改 downloaded 再 flush，崩溃后 meta 多报进度，续传从错误偏移接着写，
中间留一段垃圾——**文件大小仍对、退出码 0、只有哈希能发现**。

### 4.2 「落盘」的两个层次

- `fflush`：CRT 缓冲 → **OS 缓冲**。进程被强杀后 OS 缓冲还在，数据不丢。
  防进程崩溃（本阶段验收的 `taskkill /F`），这层就够。
- `FlushFileBuffers`（`fsync`）：OS 缓冲 → **物理盘**。防掉电才需要，速度掉一个数量级。

本阶段只用 `fflush` 层。

---

## 5. worker 循环

```cpp
while (true) {
    auto piece = pieceManager_->acquire();      // 弹出一块 PiecePtr，空则退出
    if (!piece) break;

    ByteRange r = { piece->offset + piece->downloaded,
                    piece->offset + piece->length - 1 }; // 片内续传：从 downloaded 接着下

    HttpSession s;                              // 每 worker 各持一个，不跨线程
    s.setopt(RANGE, "bytes=...");
    uint64_t pending = 0;
    s.on_write([&](data, len) {
        writer.write_at(piece->offset + piece->downloaded + pending, data, len);
        pending += len;                         // 只累计，不动 downloaded
    });
    s.perform();                                // 网络 + 写盘，全程无锁，并行

    writer.flush();                             // ① 数据推进 OS
    pieceManager_->advanceDownloaded(piece, pending); // ② 记账（锁，见 §6）
}
```

要点：

- **写盘与 flush 都在锁外**，`perform()` 是毫秒~秒级，四个 worker 完全并行。
- `pending` 只记「本次还没 flush 的字节」，`downloaded` 只在 flush 后推进，
  铁律在内存态就成立。

---

## 6. 并发与同步

### 6.1 线程亲和：worker ↔ worker

`acquire` 保证一块同时只有一个 worker。所以：

- **写-写竞争（两个 worker 写同一块）不存在**，`downloaded` 是单写者变量；
- 文件写（`write_at`）也不用锁，每个 worker 各持一个 `FileWriter`/句柄。

### 6.2 读-写竞争：checkpoint ↔ worker

checkpoint 快照要读 `downloaded`/`state`，worker 正在写——这是真正的竞争，
**线程亲和消除不了**。要么共用一把锁，要么 `downloaded` 用 atomic。

### 6.3 推荐方案：共用一把锁（PieceManager::mtx_）

```cpp
// worker 记账（每 flush 一次，纳秒级）：
void PieceManager::advanceDownloaded(const PiecePtr& piece, uint64_t flushed) {
    std::lock_guard lk(mtx_);
    piece->downloaded += flushed;
    if (piece->downloaded == piece->length) { piece->state = PieceState::Done; ++done_count_; }
    ++generation_;                      // checkpoint 靠它判断"有没有变"，见 §7.1
}

// checkpoint 快照（每 T 秒一次）：
std::vector<Piece> PieceManager::snapshot() const {
    std::lock_guard lk(mtx_);
    return /* 逐个 *piece 值拷贝，见 §2.3 */;
}
```

锁放在 `PieceManager` 而不是 `FileMeta`：`FileMeta` 是纯 DTO，不带运行时行为；
`acquire` / `advanceDownloaded` / `snapshot` 都发生在 `PieceManager`，用它自己的
`mtx_` 一把锁统一保护队列 + 进度即可，不需要在 DTO 里再放一把 mutex。

选锁而不是 atomic 的理由：

- `downloaded` 保持普通 `uint64_t`，现有的 `adl_serializer` 宏直接可用；
- 锁只覆盖**记账**（一次加法），不覆盖写盘；频率 = 每 flush 一次，占空比 ~百万分之一，
  **不损失并发写盘速度**。

> atomic 是备选（`downloaded` 用 `std::atomic<uint64_t>` + `relaxed`），但会踩两个坑：
> atomic 不可拷贝 → `snapshot()` 不能直接返回 `vector<Piece>`；
> 宏 `NLOHMANN_DEFINE_TYPE_INTRUSIVE` 无法序列化 atomic → 得手写 `to_json/from_json`
> 或把运行时进度与序列化 DTO 拆开。没有特殊理由不推荐。

### 6.4 锁只包记账，不包写盘

纪律：**锁里只有「改数字」，绝不能有 `perform()` / `write_at` / `flush`。**
一旦把锁套到下载外面，worker 就会排队等锁、串行下载——那才是真的拖慢并发。

### 6.5 不变量：Piece 对象创建后不被替换

`FileMeta::pieces` 在构造时一次性建好，下载期间**不增删、不重新赋值**。
worker 经 `acquire()` 拿到的 `PiecePtr`、`queue_` 里的 `PiecePtr`、`FileMeta::pieces`
里的 `PiecePtr`，三者指向**同一个 Piece 对象**——这是「改 downloaded 就是改唯一真相」的前提。

因为全程用 `shared_ptr`，worker 手里的 `PiecePtr` 不因 vector 重新分配而悬空
（它持有引用计数）。真正要守住的是**不要中途 `pieces[i] = make_shared<Piece>()`
重新指向新对象**——那样 worker / queue / snapshot 就各看到不同的对象了。

---

## 7. checkpoint 线程

### 7.1 节奏

```
每隔 T 秒：
    gen = pieceManager_->generation();          // ① 加锁读一个整数
    if (gen == lastGeneration_) continue;       // ② 没有任何记账发生 → 整次跳过
    snapshot = pieceManager_->snapshot();       // ③ 加锁值拷贝
    filedao_->updateFileMetaPiece(snapshot);    // ④ 锁外序列化 + 原子写 meta.json
    lastGeneration_ = gen;
```

T 建议 1~2 秒起步。低频、异步。崩溃最多丢一个窗口（T 秒）内的进度，代价是多重下一点。

② 那道 generation 闸门省掉的是整次「拷贝 + 序列化 + 写文件 + rename」（毫秒级）。
网络卡住、或接近尾声只剩一两块在下的时候，绝大多数 checkpoint 会被直接消掉。

`lastGeneration_` 和 `generation_` 都从 0 起，所以**第一个窗口必然被跳过** —— 这是对的：
此刻一个字节都还没记账，而 meta.json 在构造期（§8.1）已经写过一份当前的了。

### 7.2 原子写

每次写 meta.json 都是：

```
写 <name>.meta.tmp
  → 显式 close，并检查流状态
  → 失败就抛，绝不 rename
  → std::filesystem::rename(tmp, meta)     // MSVC 下即 MoveFileEx(..., REPLACE_EXISTING)
```

崩在写 meta 中途会留下半截 JSON，下次 `readFileMeta` 解析失败 → 整个续传废掉。

**「显式 close + 检查」这一步不是可选的。** 原子写的承诺是「要么旧的完整版本，要么新的完整版本」，
少了这道检查只剩后半句：磁盘满 / 无权限 → 写失败 → 流进 fail 状态 →
靠析构关文件**不会告诉任何人** → 代码继续走到 rename → 一个截断的 tmp 覆盖掉那份好的 meta.json。
**原子写反而成了毁数据的那一步。**

落盘层次按 §4.2：tmp 的 close 把数据推到 OS 缓冲，rename 在 NTFS 上是 journaled 的元数据操作，
防进程崩溃够用，本阶段不上 `FlushFileBuffers`。

### 7.3 快照不需要全局一致点

快照读各块 `downloaded` 的时刻略有先后，没关系——续传时每块各自从自己的
`downloaded` 恢复，块与块之间没有跨块不变量。只要每个 `downloaded` 单独满足
「≤ 已 flush」，快照就安全。这也是「每块单写者 + 铁律」带来的好处。

### 7.4 pieces 怎么更新：不读、不打补丁、整份重写

一句话：**用内存里缓存的不变头部 + 新快照，整份重新序列化、原子换名。**

`FileMeta` 里除了 `pieces`，其余六个字段（`name` / `target_url` / `local_url` /
`totalSize` / `pieceNum` / `pieceSize`）**构造后永不变**。所以磁盘上那份文件里
没有任何「内存里没有、必须读回来」的信息 —— 读它纯属多余，而且多一个失败点
（上次崩溃留下半截 JSON 的话，`json::parse` 会抛，checkpoint 线程就此死掉，之后再不记进度）。

repository 因此只需存两样东西，三个方法的流程是：

```
私有状态：
    metaFile_url_    文件名规则的唯一真相（构造时定，见 §7.6）
    headerJson_      不含 pieces 的头部模板

readFileMeta()              parse 文件 → FileMetaPtr，顺手 cacheHeader
saveFileMeta(meta)          json j = meta;  cacheHeader(j);  writeAtomic(j)
updateFileMetaPiece(snap)   json j = headerJson_;  j["pieces"] = snap;  writeAtomic(j)

cacheHeader(full)           headerJson_ = full;  headerJson_.erase("pieces")
writeAtomic(j)              见 §7.2
```

关键那一行是 `j["pieces"] = snap`：**用 json 的 key 覆盖，而不是为快照再定义一个 DTO。**
`std::vector<Piece>` 靠 `Piece` 自己的 `NLOHMANN_DEFINE_TYPE_INTRUSIVE` 直接可序列化，
所以 schema 只在 `piece.h` / `filemeta.h` 定义一次，没有第二份会漂移的副本
（若另立 `FileMetaSnapshot`，日后给 `FileMeta` 加字段忘了同步，meta.json 会静默少一个键）。

**缓存头部是正确性要求，不只是省一次读。** `json j = meta;` 走的是
`adl_serializer<FileMetaPtr>`，全字段序列化，会**读到 worker 正在改的活 `pieces`**。
所以 `saveFileMeta` 只能在构造期调用（那时 worker 还没起，序列化活对象是安全的），
头部就在那一刻冻结进 `headerJson_`；此后 checkpoint 线程只用这份冻结的副本 + 值快照，
全程不碰任何活对象。

随之而来一条前置条件：**`updateFileMetaPiece` 只在 `readFileMeta` 或 `saveFileMeta`
至少调过一次之后才合法**。`headerJson_` 为空就抛 `std::logic_error`，别让它静默写出
一份没有头部的 meta.json —— 那种文件下次 `readFileMeta` 会解析成一堆默认值，
比直接崩更难查。

#### 另外两条路，以及为什么现在不走

| 做法 | 为什么不 |
| --- | --- |
| **读-改-写**（parse 旧文件 → 换 pieces → 写回） | 多一次读和一次 parse，输出量完全一样，没省任何 IO；还把「磁盘上那份必须完好」变成了前置条件。头部本来就在内存里，没东西可读 |
| **定宽原地覆盖**（`downloaded` 前导零补齐 + `seekp` 覆盖那几个字节） | JSON 数字不允许前导零（RFC 8259），只能改成字符串、污染 schema、手写 to_json/from_json；而且原地覆盖不原子，撕裂后得到**语法合法但数值错误**的数字，解析不报错，数值偏大就直接破铁律 —— §9 说的「无症状的坏文件」。aria2 确实这么干，但它用的是**二进制控制文件**，定宽这招和 JSON 天然不搭 |
| **append-only journal**（每次推进追加一行，攒够 K 行再压实） | 真正的 WAL 做法，目录叫 `12-07-journal` 的话以后往这走很自然。但代价是两个文件 + 重放逻辑 + 压实时机，恢复路径的测试面大一圈。当前是「每 1~2 秒一次、128 块 ~15KB、整份重写 1~5ms」，先把 §7.1 的 generation 闸门做掉收益大得多 |

门槛：等 `pieceNum` 上到几万、或 checkpoint 间隔要压到 100ms 级，再回头考虑 journal。

### 7.5 停止：condition_variable，不是 sleep

项目是 `cxx_std_17`（见 `CMakeLists.txt`），**没有 `std::jthread` / `stop_token`**。
停止机制用 `std::condition_variable::wait_for` + 谓词，不要 `sleep_for(T)` + `atomic<bool>` 轮询：

```
run():
    while (true):
        {
            unique_lock lk(stopMtx_)
            if (stopCv_.wait_for(lk, interval_, [&]{ return stop_; })) break
        }                                   // ← 出作用域放锁，绝不持 stopMtx_ 做 IO
        writeOnce()
    writeOnce()                             // 退出前补最后一次

stop():
    { lock_guard lk(stopMtx_); stop_ = true; }
    stopCv_.notify_one()
```

- 用 `sleep_for` 的话，下载完成时最坏要干等一个完整的 T 才退出；T 取 2 秒就是白卡 2 秒。
  `wait_for` 被 `notify_one` 立刻打断。谓词形式顺带处理虚假唤醒。
- `stopMtx_` 是 `Checkpoint` 自己的锁，**和 `PieceManager::mtx_` 毫无关系**，只保护 `stop_` 一个 bool。
  两把锁没有嵌套关系，不存在死锁顺序问题。
- **退出前补的最后一次 checkpoint**：失败路径上很值钱，它把最后一个窗口内已 flush 的进度落盘，
  下次续传不用重下。成功路径上是白写，但只要 §8.2 的收尾顺序对就无害。

### 7.6 meta 文件名规则只定义一次

现在 `downloader.cpp` 用 `tailName(target_url) + ".meta.json"`，`saveFileMeta` 用
`local_url + name + ".meta.json"`，而 `name` 从未赋值 —— 两套拼法，其中一套是坏的。

规则归 **`JSONDownloadRepository` 的构造函数**：`Downloader` 构造时算一次完整路径传进去，
之后 `readFileMeta` / `saveFileMeta` / `updateFileMetaPiece` 都不再带路径参数。
这同时解决了 `updateFileMetaPiece(snapshot)` 签名里本来就没地方放路径的问题。

---

## 8. 生命周期

### 8.1 启动 / 恢复

```
Downloader 构造：
  ① 算出 metaFile_url（§7.6 的唯一一处）→ 构造 JSONDownloadRepository → 赋给 filedao_
  ② 有 meta.json ?  →  readFileMeta 反序列化恢复（pieces 里 downloaded 各归各位）
     无             →  HEAD probe 拿 totalSize → 切块 → 立即 saveFileMeta 落盘
     ——— 无论走哪条，头部都在这一步冻结进 headerJson_（§7.4）———
  ③ PieceManager 构造：把所有 downloaded < length 的块入队
  ④ Checkpoint 构造（拿 PieceManager + filedao_ + interval）
```

① 必须在 ② 之前 —— 现在 `filedao_` 从头到尾没被赋值过（坑 #5）。
②（`saveFileMeta` 会序列化活 `pieces`）必须在 worker / checkpoint 线程启动之前，理由见 §7.4。

恢复看 meta.json 里的 `downloaded`，**不看 `.part` 大小**（`.part` 是预分配的）。

### 8.2 完成

```
all_done()
  → join 所有 worker
  → checkpoint_.stop();  checkpointTask_.join();   ← 必须在删 meta 之前
  → 关闭所有 .part 写句柄
  → MoveFileExA(.part → file.zip)
  → 删 meta.json
  → 退出码 0
```

**第 2 步是新增的，漏掉它有一个真实的竞态：**

```
main:        all_done → rename .part → 删 meta.json
checkpoint:                                        ← 恰好在这之后写完一次 meta.json
结果：file.zip 旁边躺着一份幽灵 meta.json → 下次启动误判成续传任务
```

`Downloader` 的析构里也要兜一道 `stop() + join()`，否则 `start()` 中途抛异常时，
线程还引用着已经析构的成员。

### 8.3 失败

```
保留 .part 和 meta.json（下次能续）
不做 rename（绝不留下名叫 file.zip 的残缺文件）
打印失败块下标 + 原因，退出码非 0
```

失败路径同样要先 `stop() + join()` checkpoint 线程 —— 这里那「最后一次 checkpoint」
（§7.5）正好把最后一个窗口的进度捞回来。

### 8.4 谁在什么时候碰 meta.json

```
阶段            线程          对 meta.json 的动作
─────────────────────────────────────────────────────────────
Downloader 构造  main          readFileMeta / saveFileMeta   ← 头部在此冻结
start() 拉线程   main          （无）
运行期           checkpoint    updateFileMetaPiece × M       ← 唯一写者
stop + join      main          （无）
收尾             main          删 meta.json
```

**`IDownloadRepository` 的实现不需要线程安全**，依据就是这张表：任何时刻最多只有
一个线程碰它，构造期在 checkpoint 线程启动**之前**，收尾期在 join **之后**，两端不重叠。
这是一条纪律，不是编译器能保证的事 —— 新加的代码若要在运行期从 main 或 worker 调
repository，这条就破了（worker 绝不写 meta.json，见 §3）。

---

## 9. 崩溃窗口分析

| 崩溃时机 | 结果 |
| --- | --- |
| 写数据后、flush 前 | 数据在 CRT 缓冲，丢了；meta 没记 → 重下这一小段，安全 |
| flush 后、记账前 | 数据在 OS，但 meta 没记 → 重下这一小段，安全（略浪费） |
| 记账后、checkpoint 前 | 内存记了，meta 没记 → 重下这一块，安全 |
| checkpoint 写 meta 中途 | 半截 JSON → 下次解析失败 → 从头下（靠 §7.2 原子写避免） |

只要铁律成立，上面每一档都只会「多重下一点」，**不会产出坏文件**。
顺序颠倒（先记账后 flush / 先写 meta 后写数据）才会产出「无症状的坏文件」。

---

## 10. 落地映射与当前待办

| 设计元素 | 当前代码 | 状态 |
| --- | --- | --- |
| Piece（downloaded 字节级） | `entity/piece.h` | ✅ 已具备 |
| FileMeta + 序列化 | `entity/filemeta.h`（adl_serializer） | ✅ 已具备 |
| 记账 `advanceDownloaded` | `PieceManager` | ✅ 已实现（签名待按 §11.3 微调） |
| `acquire` / `snapshot` / `cancel` | `core/piecemanager.h` | 🟡 已实现，缺 `generation()`；`snapshot_done` 待改名 |
| worker 循环 | `core/worker.cpp` | 🟡 有骨架，Range/退出条件/记账频率都要改（坑 6~8） |
| **checkpoint 线程** | `core/checkpoint.{h,cpp}` + `Downloader::checkpointTask_` | ⬜ **本轮目标**，签名见 §11.4 |
| 持久化三方法 | `repository/*.h` | 🟡 签名要按 §11.1 / §11.2 重定，原子写待补 |
| 收尾 rename / 删 meta | `Downloader::start()` | ⬜ 待补（顺序见 §8.2） |
| main / GlobalCurlGuard | `src/main.cpp` | ⬜ 空 |

已知坑（代码里现存的，实现时一起处理）：

1. `downloader.cpp:28`：建块的循环 `for (i=0; i<pieceSize; i++)` 应为 `i < pieceNum`。
   最后一块的 `length` 也要收成 `totalSize - offset`，不能一律给 `pieceSize`。
2. `downloader.cpp:9`：meta 文件名两套拼法，`name` 从未赋值 —— 按 §7.6 收进 repository 构造函数。
3. `jsondownloadrepository.cpp:23`：`updateFileMetaPiece` 写成了 `IDownloadRepository::`，
   不是 `JSONDownloadRepository::`，接口与实现对不上。改成纯虚（§11.1）后这类错编译期就报。
4. 头回调里 `content-length` 用 `find`（`downloader.cpp:82`），会误匹配
   `Access-Control-Expose-Headers`，应改用前缀匹配。
5. **`Downloader::filedao_` 从未赋值**，`downloader.cpp:11` 和 `:38` 直接解引用空 `shared_ptr`
   —— 构造函数第一件事就崩。必须 `make_shared<JSONDownloadRepository>(metaFile_url)`。
6. `worker.cpp:29-30`：Range 算错。`end = offset + downloaded - 1` 应为 `offset + length - 1`，
   `start` 应为 `offset + downloaded`；`downloaded == 0` 时现在会算出 `end = offset - 1`。
   `seekp` 同样要定位到 `offset + downloaded`（`:23` 只给了 `offset`）。
7. `worker.cpp:20`：`acquire().value()` 在队列空时抛 `bad_optional_access`，
   下一行的 `if (!currentPiece_)` 永远走不到。
8. `worker.cpp:50-52`：在**每个 curl write 回调里**做 `flush()` + `advanceDownloaded()`，
   锁频率是 §5 设计的几百倍。应按 §5 用 `pending` 只累计，`perform()` 返回后统一
   flush + 记账一次。（注意：当前写法虽然慢，但 flush 在前、记账在后，铁律没破。）
9. `piecemanager.cpp:3`：构造函数只初始化了 `fileMetaPtr_`，`done_count_` / `cancelled_`
   是不定值，而 `all_done()` 正是 checkpoint 和 worker 的退出判据。
10. `piecemanager.cpp:36`：`snapshot_done()` 名不副实 —— 实现遍历的是**全部** pieces，
    正是设计要的行为（片内续传靠它），改名 `snapshot()` 即可，逻辑别动。
11. `jsondownloadrepository.cpp:19`：`std::ofstream out(url)` 是原地截断写，不是原子写；
    且靠析构关文件，写失败被静默吞掉。按 §7.2 补 `writeAtomic`。
12. `idownloadrepository.h`：没有虚析构（经 `IDownloadRepositoryPtr` 析构派生类是 UB），
    三个方法也不是纯虚。

---

## 11. 接口声明清单（本轮定稿）

五个头文件的最终签名。实现待补，但**签名到此不再变**；真要改就回来同步本节，
别让文档和头文件各说各话。

### 11.1 `repository/idownloadrepository.h`

```cpp
class IDownloadRepository {
public:
    virtual ~IDownloadRepository() = default;   // 经 Ptr 析构派生类，少了它是 UB

    // —— 构造期（main 线程，worker / checkpoint 尚未启动）——
    virtual FileMetaPtr readFileMeta() = 0;                  // 路径已在构造时给定
    virtual void saveFileMeta(const FileMetaPtr& meta) = 0;  // 全量落盘 + 冻结头部

    // —— 运行期（checkpoint 线程专用）——
    virtual void updateFileMetaPiece(const std::vector<Piece>& snapshot) = 0;
};
using IDownloadRepositoryPtr = std::shared_ptr<IDownloadRepository>;
```

- 三个方法**必须纯虚**。当前写成带实现的 `virtual`，派生类签名拼错时不报错，
  接口和实现能悄悄对不上 —— 坑 #3 就是这么来的。
- `updateFileMetaPiece` 收 `const std::vector<Piece>&`（**值**），理由见 §2.3 的表。
- 三个方法都**不带路径参数**，路径是 §7.6 的实现细节。

### 11.2 `repository/jsondownloadrepository.h`

```cpp
class JSONDownloadRepository : public IDownloadRepository {
public:
    explicit JSONDownloadRepository(std::string metaFile_url);

    FileMetaPtr readFileMeta() override;
    void saveFileMeta(const FileMetaPtr& meta) override;
    void updateFileMetaPiece(const std::vector<Piece>& snapshot) override;

private:
    void cacheHeader(const nlohmann::json& full);  // full 去掉 pieces 后存进 headerJson_
    void writeAtomic(const nlohmann::json& j);     // tmp → 显式 close 检查 → rename

    std::string    metaFile_url_;   // 文件名规则的唯一真相
    nlohmann::json headerJson_;     // 不含 pieces 的头部模板
};
```

流程见 §7.4，原子写见 §7.2。`updateFileMetaPiece` 在 `headerJson_` 为空时抛 `std::logic_error`。

### 11.3 `core/piecemanager.h`

```cpp
class PieceManager {
public:
    explicit PieceManager(FileMetaPtr file);

    auto acquire() -> std::optional<PiecePtr>;
    void advanceDownloaded(const PiecePtr& piece, uint64_t flushed);

    std::vector<Piece> snapshot() const;   // 值拷贝，供 checkpoint
    uint64_t generation() const;           // 记账次数，供 checkpoint 判断有没有变

    size_t done_count() const;
    size_t total() const;
    bool   all_done() const;
    void   cancel();
    bool   cancelled() const;

private:
    FileMetaPtr           fileMetaPtr_;
    std::vector<PiecePtr> queue_;
    mutable std::mutex    mtx_;         // 唯一一把锁
    size_t   done_count_ = 0;           // ← 显式初始化（坑 #9）
    uint64_t generation_ = 0;           // advanceDownloaded 里 ++
    bool     cancelled_  = false;       // 已在 mtx_ 下，不必 atomic
};
```

相对现有代码的改动：

| 改动 | 为什么 |
| --- | --- |
| `snapshot_done()` → `snapshot()` | 实现本来就返回全部 pieces，只是名字骗人（坑 #10） |
| 新增 `generation()` | §7.1 的闸门 |
| `advanceDownloaded` 收 `const PiecePtr&` | 按值传 `shared_ptr` 白白弹一次原子引用计数，而这是**每次 flush 都走**的热路径 |
| `cancelled_` 从 `std::atomic<bool>` 降为 `bool` | 所有访问都已在 `mtx_` 内，atomic 是冗余的，留着反而让人以为可以锁外读 |
| 三个成员显式初始化 | 坑 #9 |

`total()` 目前锁外读 `fileMetaPtr_->pieces.size()` —— 靠 §6.5「pieces 构造后不增删」这条
不变量才安全，别顺手往里加元素。

### 11.4 `core/checkpoint.h`

```cpp
#include "piecemanager.h"
#include "repository/idownloadrepository.h"
#include <chrono>
#include <condition_variable>
#include <mutex>

class Checkpoint {
public:
    Checkpoint(PieceManagerPtr pieceManagerPtr,
               IDownloadRepositoryPtr filedao,
               std::chrono::milliseconds interval);

    void run();    // 线程入口，循环直到 stop()
    void stop();   // 置位 + notify，立即唤醒 run()

private:
    bool writeOnce();   // 有变化才写；返回是否真写了

    PieceManagerPtr           pieceManagerPtr_;
    IDownloadRepositoryPtr    filedao_;
    std::chrono::milliseconds interval_;
    uint64_t                  lastGeneration_ = 0;   // 上次落盘时的 generation

    std::mutex              stopMtx_;   // 只保护 stop_，与 PieceManager::mtx_ 无关
    std::condition_variable stopCv_;
    bool                    stop_ = false;
};
```

- 依赖只有三个，**不持有 `FileMetaPtr`** —— 头部是 repository 的事（§7.4）。
- `run()` / `stop()` 的形状见 §7.5。`run()` 退出前补最后一次 `writeOnce()`。
- 与 `Worker` 对称：类提供 `run()`，`std::thread` 由 `Downloader` 持有。

### 11.5 `core/downloader.h` 的增补

```cpp
private:
    // ... 现有成员 ...
    Checkpoint  checkpoint_;        // 新增：必须声明在 checkpointTask_ 之前
    std::thread checkpointTask_;    // 已有
```

- **声明顺序有讲究**：成员按声明逆序析构，`checkpointTask_` 要先于 `checkpoint_` 析构，
  否则线程还在跑、它引用的 `checkpoint_` 已经没了。
- 析构函数里兜一道 `stop() + join()`（§8.2）。
- 构造函数里补上 `filedao_ = std::make_shared<JSONDownloadRepository>(metaFile_url)`（坑 #5），
  且必须在第一次用 `filedao_` **之前**。

### 11.6 并发契约

| 方法 | 谁调 | 何时 | 内部持 `mtx_` |
| --- | --- | --- | --- |
| `readFileMeta` / `saveFileMeta` | main | 构造期，worker 未起 | — |
| `updateFileMetaPiece` | checkpoint | 运行期，meta.json 唯一写者 | 否（锁外序列化） |
| `acquire` / `advanceDownloaded` | worker | 运行期 | 是 |
| `snapshot` / `generation` | checkpoint | 运行期 | 是 |
| `all_done` / `done_count` / `cancel` | main / worker | 运行期 | 是 |
| `Checkpoint::stop` | main | 收尾，join 之前 | 否（持 `stopMtx_`） |

两把锁从不嵌套：`PieceManager::mtx_` 只在 PieceManager 方法内部持有，
`Checkpoint::stopMtx_` 只包一个 bool，不存在加锁顺序问题。

---

## 12. 验收标准

铁律和原子性没法靠"跑一遍看起来对"来验，得主动制造崩溃：

| # | 做法 | 期望 |
| --- | --- | --- |
| 1 | 下载中任意时刻 `taskkill /F`，续传至完成 | 全文 SHA256 与源文件一致（**这条是总闸**） |
| 2 | 崩溃后读 meta.json，逐块比对 `downloaded` 与 `.part` 实际已写字节 | 每块 `downloaded` ≤ 实际已 flush 字节（铁律） |
| 3 | 在 `writeAtomic` 的 rename 前插 `std::abort()`，重跑 | meta.json 仍是上一次那份完整 JSON，`readFileMeta` 不抛 |
| 4 | 把 tmp 路径改到不存在的目录 | `updateFileMetaPiece` 抛异常，且原 meta.json **一字不变**（卡 §7.2 的 close 检查） |
| 5 | 连续 checkpoint 20 次后检查 meta.json | 六个头部字段与构造时完全一致（卡 `headerJson_` 缓存 + `erase("pieces")`） |
| 6 | 正常下完，连跑 20 次 | 目录里只剩 `file.zip`，无 `.part`、无 `.meta.json`（卡 §8.2 的 join 顺序） |
| 7 | 网络断开若干秒后恢复 | 断开期间 checkpoint 不产生任何磁盘写（卡 §7.1 的 generation 闸门） |

第 1 条最好用一个内容已知的大文件（几百 MB，便于制造"崩在中途"的窗口），
配合限速让下载慢到能手动 kill。
