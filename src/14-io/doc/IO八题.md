# IO 八题

> 本目录的练习清单。知识点见同目录的 [CPP_IO_文件流学习笔记.md](CPP_IO_文件流学习笔记.md)，
> 这里只写**题面、必须踩到的坑、验收标准**——代码自己写。

八题分三档，难度递增，后一档依赖前一档的结论：

| 档 | 题 | 主题 | 源文件 |
| --- | --- | --- | --- |
| 基础 | 1 | `cin` 循环读取与 EOF | `q1.cc` |
| 基础 | 2 | `<iomanip>` 格式化输出 | `q2.cc` |
| 进阶 | 3 | `stringstream` 解析与拼接 | `q3.cc` |
| 进阶 | 4 | `ifstream` + `getline` + 错误处理 | `q4.cc` |
| 进阶 | 5 | `ifstream`/`ofstream` 配合、逐字符转换 | `q5.cc` |
| 高级 | 6 | 二进制模式读写结构体 | `q6.cc` |
| 高级 | 7 | CSV 解析 + `map` 计数 + 自定义排序 | `q7.cc` |
| 高级 | 8 | 自定义 `streambuf`，输出分流 | `q8.cc` |

四条贯穿全篇的原则，八题都在反复考它：

1. **打开必查**（`is_open()` / `if (!fs)`），失败要有明确的错误信息和非零退出码。
2. **`>>` 和 `getline` 不能随手混用**——`>>` 会把换行留在缓冲区里。
3. **操纵符里只有 `setw` 是一次性的**，`setprecision`/`fixed`/`left` 都是粘性的。
4. **"传输成功"不等于"落盘成功"**——`close()` 之后再判一次流状态。

---

## 构建

`CMakeLists.txt` 已经写好，`src/CMakeLists.txt` 里也接上了 `add_subdirectory(14-io)`，
当前只挂了 `q1`。**做完一题再接一个 target**——把空的 `.cc` 全挂上去会因为缺 `main` 直接链接失败。

```cmake
project(14-io)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# 每完成一题，把对应的 qN 从注释里挪进 foreach 的列表
foreach(q q1) #  q2 q3 q4 q5 q6 q7 q8
    add_executable(14-io-${q} ${q}.cc)
endforeach()
```

C++ 标准不用在这里写——根 `CMakeLists.txt` 统一设了 `CMAKE_CXX_STANDARD 17`。
（MSVC 不给 `/std` 会按 C++14 编译，`compile_commands.json` 里也跟着缺这个 flag，
clangd 就会对 `std::string_view` 之类的报假错。）

构建走 MSVC（vcpkg 是 `x64-windows` triplet），命令行需要 vcvars64 包一层，参考根目录 `mb.bat`：

```
cmake --build --preset msvc-debug --target 14-io-q1
```

产物在 `src/14-io/build/bin/`。第 4、5、7 题需要的输入文件统一放 `src/14-io/testdata/`。

---

# 基础篇

## 1. 多组整数求和

从标准输入读取多组整数，每组两个 `a` 和 `b`，逐行输出它们的和。输入以 EOF 结束。

```
输入            输出
1 2             3
3 4       →     7
5 6             11
```

**考察点**：`cin` 的循环读取、EOF 判断、`cout` 基本输出。

**坑**

- 循环条件写的是**流对象本身**，不是 `cin.eof()`。`eof()` 只有在**已经读失败之后**才为真——先判 `eof()` 再读，最后一组会被多算一次；先读再判 `eof()`，遇到非数字输入（`failbit`）会死循环。流对象转 `bool` 检查的是 `!fail()`，同时覆盖两种情况。
- 两个数是一次条件里连着读的，`a` 读成功而 `b` 失败时**不能**输出——所以条件要包住两次提取，不是分开写。
- Windows 终端里 EOF 是**行首**按 `Ctrl+Z` 再回车，行中间按无效。
- `>>` 不区分空格和换行，`1 2 3 4` 写成一行和写成两行是同一件事。想清楚这是不是你要的语义。

**验收**

- [ ] 管道喂输入（`... < in.txt`）和手敲 `Ctrl+Z` 两种方式结果一致
- [ ] 输入里混进一个非数字，程序**干净退出**而不是死循环刷屏
- [ ] 最后一组不会被重复输出

---

## 2. 格式化输出成绩单

输入学生姓名（**可能含空格**）和三门课成绩，输出格式化成绩单：

- 姓名左对齐，宽度 20；
- 每门成绩右对齐，宽度 8，保留 2 位小数；
- 平均分右对齐，宽度 10，保留 2 位小数。

```
输入：Zhang San 87.5 92 78
输出：Zhang San              87.50   92.00   78.00      85.83
```

**考察点**：`setw`、`setprecision`、`fixed`、`left`/`right`。

**坑**

- **姓名含空格，所以不能用 `>>` 读姓名。** 整行 `getline` 进来之后自己切：从右边数**最后三个**空白分隔的 token 是成绩，剩下的全是姓名。用 `>>` 读会得到 `Zhang`，然后把 `San` 当成第一门成绩去解析。
- `setw` **只对紧跟其后的那一次输出生效**，四个字段就要设四次。`setprecision`/`fixed`/`left`/`right` 是粘性的，设一次一直有效——`left` 设完不改回 `right`，后面三个成绩就全靠左了。
- `setprecision(2)` 单独用是**有效数字**2 位（`87.5` → `88`），必须配 `fixed` 才是小数点后 2 位。
- 对齐宽度自己验一遍：20 + 8 + 8 + 8 + 10 = 54，输出行长度应当正好是 54（姓名不超宽的前提下）。姓名超过 20 字符时 `setw` **不截断**，字段会被撑开——这是标准行为，不用处理，但要知道。
- 平均分是 `(87.5+92+78)/3 = 85.8333...`，`fixed` 输出自带四舍五入，不用手工 `round`。注意别整数除法。

**验收**

- [ ] 输出与题面示例**逐字节相同**（用 `fc` 或重定向后 diff 比对）
- [ ] 姓名换成单字 `Li`，各列仍然对齐
- [ ] 成绩输入 `92`（整数形式）也输出 `92.00`

---

# 进阶篇

## 3. 字符串解析与拼接

用 `std::stringstream` 完成两件事：从一行字符串里提取若干整数（空格分隔），求和输出；再把这些整数按 `数字1,数字2,...` 拼成新字符串输出。

```
输入：10 20 30 40
输出：Sum: 100
      Joined: 10,20,30,40
```

**考察点**：`istringstream` 提取、`ostringstream` 构建。

**坑**

- 整数**个数不定**，所以是 `while (iss >> n)` 循环到提取失败，不是读固定次数。这和第 1 题是同一个模式。
- 分隔符**不能有尾巴**。`10,20,30,40,` 是错的。两种写法：要么"第一个之外每个前面加逗号"，要么"每个后面加逗号最后 `pop_back`"。前者不用回头改字符串。
- 输入为空行时，`Sum: 0` 和 `Joined: `（空）都是合理输出，但别崩在 `pop_back` 上。
- `ostringstream` 取结果是 `.str()`，返回的是**拷贝**。别写 `oss.str().c_str()` 然后存指针——临时 string 立刻析构，指针悬空。
- 一个 `stringstream` 读完再当写用要 `clear()` + `str("")`，两件事都要做（`clear` 清状态位，`str("")` 清内容）。更省事的做法是开两个对象。

**验收**

- [ ] 输入 `10 20 30 40` 输出与题面一致
- [ ] 输入单个数字 `7` → `Sum: 7` / `Joined: 7`（没有多余逗号）
- [ ] 输入空行不崩

---

## 4. 文本文件行数统计

命令行参数给文件名，统计其中**非空行**数（只含空白字符的行算空行），输出结果。文件打不开时输出错误信息并返回非零。

```
文件内容                          输出
Hello world
                            →     Non-empty lines: 2
   （只有空格的一行）
This is a test.
```

**考察点**：`ifstream` 打开文件、`getline` 读取、错误处理。

**坑**

- **先查 `argc`**。`argv[1]` 在没给参数时是 `nullptr`，`ifstream` 构造函数拿到空指针是 UB，不是"打开失败"。
- **"空行"不等于"空字符串"**。`"   "` 和 `"\t"` 都要算空行。判据是"这一行里存不存在非空白字符"，用 `find_first_not_of(" \t\r\n")` 或 `std::all_of` + `isspace`。
- `std::isspace(c)` 的参数是 `int`，传 `char` 时若字符为负（非 ASCII 字节）是 **UB**。必须 `static_cast<unsigned char>(c)`。
- 文件如果是 **LF 结尾但以 `ios::binary` 打开**、或本来就是 CRLF 文件在别的平台产生的，`getline` 切完会在行尾留 `'\r'`。这行看着是空的，`empty()` 却为假——所以空白字符集合里必须带上 `\r`。
- 错误信息打到 `cerr` 不是 `cout`，退出码非零。这是给脚本用的契约。

**验收**

- [ ] 造一个含空行、纯空格行、纯 Tab 行的文件，计数正确
- [ ] 不给参数 → 提示用法，退出码非零
- [ ] 给不存在的文件名 → 明确报错，退出码非零
- [ ] 用 `echo %ERRORLEVEL%` 确认退出码真的不是 0

---

## 5. 文件复制与转换

把源文件内容复制到目标文件，同时把所有小写字母转成大写。两个文件名都从命令行参数取。

```
source.txt:  Hello, C++ IO!
dest.txt:    HELLO, C++ IO!
```

**考察点**：`ifstream` 与 `ofstream` 配合、逐字符/逐行读取、`toupper`。

**坑**

- **`std::toupper` 传负 `char` 是 UB**，同第 4 题：`static_cast<unsigned char>` 之后再传。这是本题真正的考点。
- 逐行 `getline` + `<<` 会**在文件末尾多加一个换行**（原文件最后一行若没有换行符）。要严格字节复制就逐字符 `get`/`put`，或者用 `rdbuf()` 直连——但后者没法插转换。
- 文本模式在 Windows 上会做 CRLF ↔ LF 翻译，读一次写一次**往返是等价的**；但如果源文件行尾混杂（部分 LF 部分 CRLF），复制出来会被统一成 CRLF，字节数变了。想要"复制"名副其实就两边都开 `ios::binary`。
- **源和目标同名时会自毁**：`ofstream` 默认带 `trunc`，构造那一刻源文件就被清空了。开工前比一遍路径。
- 打开检查要做**两次**，输入输出各一次。写完 `close()` 之后再判一次 `ofstream` 的状态——`flush` 失败同样置 `badbit`，只看写的时候没报错会漏掉。

**验收**

- [ ] `source.txt` → `dest.txt` 内容正确，非字母字符（`,` `+` `!` 空格）原样保留
- [ ] 目标文件字节数与源文件相同（binary 模式下）
- [ ] 源文件不存在时报错并返回非零，且**不产生**空的目标文件
- [ ] 参数只给一个时不崩

---

# 高级篇

## 6. 二进制文件读写结构体

定义 `Student { int id; char name[20]; double score; }`。从键盘输入 3 个学生信息，以二进制写入 `students.dat`；再重新打开文件读出并显示。

```
1001 Alice 88.5
1002 Bob 92.0
1003 Charlie 79.5
```

**考察点**：`ios::binary`、`write`/`read`、结构体大小与填充。

**坑**

- **`ios::binary` 不能省**。文本模式下 `0x0A` 会被翻译成 `0x0D 0x0A`，`double` 里凑巧出现 `0x0A` 字节就会多写一个字节，整个文件从那里开始错位。读的时候还可能撞上 `0x1A`（旧 EOF 标记）提前截断。这个 bug 不会报错，只会读出乱数。
- `write` 的第一个参数是 `const char*`，要 `reinterpret_cast<const char*>(&s)`；`read` 那边是 `reinterpret_cast<char*>(&s)`。**不是** `static_cast`。
- **`sizeof(Student)` 不等于 4+20+8=32**。`double` 要求 8 字节对齐，`int(4) + char[20]` 占 24 字节正好对齐，这个结构恰好是 32——但这是巧合，换个字段顺序就不是了。自己 `sizeof` 打出来确认一遍，别心算。加一条 `static_assert(std::is_trivially_copyable_v<Student>)`：一旦有人往结构里塞了 `std::string`，`write` 就会把**指针**写进文件，读出来是野指针。
- `char name[20]` 要保证有 `'\0'`。`>>` 读进 `char[]` 没有长度保护（缓冲区溢出），读到 `std::string` 再 `strncpy` 过去，并手动补 `name[19] = '\0'`。
- 读循环写成 `while (in.read(reinterpret_cast<char*>(&s), sizeof s))`——`read` 读不满会置 `failbit`，循环自然结束。想知道最后一次读了多少用 `gcount()`。
- 写完必须 `close()`（或让对象析构）再重新打开读。同一个流没 flush 就去读，读到的是旧内容或空文件。

**验收**

- [ ] 写完后 `students.dat` 大小正好是 `3 * sizeof(Student)`
- [ ] 读回来的三条记录与输入完全一致，包括 `88.5` 这种小数
- [ ] 用十六进制查看器打开文件，能指出每个字段的字节位置和填充字节在哪
- [ ] 姓名输入超过 19 个字符时不越界

---

## 7. CSV 文件解析与统计

`data.csv` 每行格式 `姓名,年龄,城市`。统计每个城市的人数，按**人数降序**输出；人数相同时按**城市名升序**。

```
Alice,25,Beijing            Beijing: 2
Bob,30,Shanghai             Shanghai: 2
Charlie,22,Beijing    →     Guangzhou: 1
Dave,28,Guangzhou
Eve,35,Shanghai
```

**考察点**：`getline` 逐行 + `stringstream` 切字段、`map` 计数、自定义排序。

**坑**

- `std::getline` 的**三参数版本**可以指定分隔符：`getline(iss, field, ',')`。不用手写 `find`+`substr`。
- **`map` 本身排不出这个顺序**。`map<string,int>` 按 key 升序，题目要的是按 value 降序。必须把 `map` 的内容倒进 `vector<pair<string,int>>` 再 `sort`，比较器写成"人数不等时比人数（`>`），相等时比城市名（`<`）"。
- 比较器必须是**严格弱序**：两条都相等时要返回 `false`。写成 `a.second >= b.second` 会直接 UB / 崩在 `sort` 里。
- 最后一个字段（城市）会带 `'\r'`——CSV 多半是 CRLF 文件。`Beijing` 和 `Beijing\r` 是两个不同的 key，会统计成两组。**这是本题最容易漏的一处**，读完每行先剪掉尾部空白。
- 空行、字段数不足 3 的行要跳过，别把空字符串当成一个城市统计进去。
- 表头行（如果有）要不要跳过，自己定个规矩并写在注释里。

**验收**

- [ ] 题面样例输出完全一致（`Beijing` 排在 `Shanghai` 前，因为同为 2 且字典序在前）
- [ ] 把 `data.csv` 存成 CRLF 再跑一遍，结果不变
- [ ] 文件里插一个空行和一个只有两列的行，程序不崩且计数不受影响
- [ ] 构造一组三城市同为 1 人的数据，验证城市名升序生效

---

## 8. 自定义流缓冲区（挑战题）

实现日志类 `Logger`，继承 `std::streambuf` 和 `std::ostream`，把输出**同时**写到标准输出和日志文件：

```cpp
Logger logger("log.txt");
logger << "This goes to both console and file." << std::endl;
```

**考察点**：流缓冲区机制、重写 `overflow` 和 `sync`、`ostream` 与自定义 `streambuf` 的结合。

**坑**

- **构造顺序是本题的核心陷阱。** `std::ostream` 的构造函数要接一个已经能用的 `streambuf*`，而基类按**声明顺序**构造。写成 `class Logger : public std::ostream, public std::streambuf` 的话 `ostream` 先构造，此时你传给它的 `this`（作为 streambuf）尚未初始化——UB。正确写法是让 buf 的基类**声明在前**：

  ```cpp
  class Logger : private LogBuf, public std::ostream {
  public:
      explicit Logger(const std::string& path)
          : LogBuf(path), std::ostream(this) {}   // LogBuf 先构造，this 已可用
  };
  ```

  另一条路是把 buf 做成**成员变量**，然后 `std::ostream(&buf_)`——但成员在基类之后构造，同样有序问题，得靠虚基类或者两阶段初始化绕。先做上面那种。

- 只需要重写两个虚函数：

  ```cpp
  int_type overflow(int_type c) override;   // 来一个字符（无缓冲实现最简单）
  int      sync() override;                 // flush：两边都要 flush
  ```

  `overflow` 收到 `traits_type::eof()` 时**不要**当成普通字符写出去，直接返回非 eof 表示成功。正常写成功返回 `c`，失败返回 `traits_type::eof()`——返回值语义是"成功/失败"，不是"写了几个字节"。
- `std::endl` = 换行 + `flush`，`flush` 最终调到你的 `sync()`。`sync()` 里要 flush **文件流和 `cout` 两个**，返回 0 表示成功、-1 表示失败。只 flush 文件的话，控制台那一路在程序崩溃时会丢内容。
- **无缓冲版本先跑通**（`overflow` 每个字符写一次），再考虑 `setp` 做缓冲区。带缓冲版本 `overflow` 的语义变成"缓冲区满了，把它清空并把 `c` 放进去"，析构函数里必须 `sync()`，否则最后一段永远不落盘。
- `Logger` 不该可拷贝——`ostream` 本来就删了拷贝，但自己也把移动禁掉，避免出现一个"被掏空但析构照样跑"的实例（同 12-03 的 `GlobalCurlGuard`）。
- 析构顺序：`ostream` 先析构，`LogBuf` 后析构，所以在 `LogBuf` 的析构里 flush 是安全的。

**验收**

- [ ] `logger << "x" << 42 << std::endl;` 控制台和 `log.txt` 内容完全一致
- [ ] 不写 `std::endl`、只写 `"\n"` 时，内容最终也能落盘（析构时 flush）
- [ ] 日志文件打不开时有明确行为（抛异常或置 `badbit`），不是静默丢日志
- [ ] 能说清 `overflow` 的返回值为什么是 `int_type` 而不是 `bool`

---

## 通用坑速查

| 现象 | 根因 | 结论 |
| --- | --- | --- |
| 最后一组数据被重复处理 | 用 `while (!in.eof())` 当循环条件 | 用流对象本身：`while (in >> x)` |
| `getline` 第一次读到空行 | 前面用过 `>>`，换行还留在缓冲区 | 中间插一次 `ignore`，或全程只用 `getline` |
| `setw` 只有第一列生效 | `setw` 是一次性的 | 每个字段都要重设 |
| `87.5` 输出成 `88` | `setprecision` 没配 `fixed` | 两个一起用 |
| 二进制文件读出乱数 | 忘了 `ios::binary`，`0x0A` 被翻译 | 二进制读写一律加 `binary` |
| 字符串 key 统计成两组 | 行尾残留 `'\r'` | 按行读完先剪尾部空白 |
| `isspace`/`toupper` 偶发崩溃 | 传了负的 `char` | 一律 `static_cast<unsigned char>` |
| `sort` 崩在比较器里 | 比较器不是严格弱序（用了 `>=`） | 相等必须返回 `false` |
| 文件内容少了最后一截 | 没 flush 就退出/去读 | `close()` 后再判一次流状态 |
| 目标文件把源文件清空了 | `ofstream` 默认带 `trunc` | 开工前比对路径 |

## 与学习笔记的对应

| 题 | 笔记章节 |
| --- | --- |
| 4 / 5 | §2 `ofstream` 写、§4 `ifstream` 读、§5 `>>` 与 `getline`、§6 打开模式 |
| 6 | §6.4 `ios::binary`、§8 位置指针、§9 二进制读写 |
| 7 | §5 `getline`、§10 一次读整个文件 |
| 8 | 笔记未覆盖，属于 `streambuf` 层，需另查 cppreference |
