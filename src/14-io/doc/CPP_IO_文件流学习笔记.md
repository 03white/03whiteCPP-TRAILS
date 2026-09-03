# C++ IO 文件流（fstream）学习笔记

## 1. 三个核心文件流类

需要包含：

``` cpp
#include <fstream>
```

C++ 中常用的文件流有：

  类                含义                 方向
  ----------------- -------------------- -------------
  `std::ifstream`   input file stream    文件 → 程序
  `std::ofstream`   output file stream   程序 → 文件
  `std::fstream`    file stream          文件 ↔ 程序

可以简单理解为：

``` text
std::cout        → 输出到控制台
std::ofstream    → 输出到文件

std::cin         ← 从控制台输入
std::ifstream    ← 从文件读取
```

------------------------------------------------------------------------

## 2. 使用 `ofstream` 写文件

``` cpp
#include <iostream>
#include <fstream>

int main()
{
    std::ofstream file("test.txt");

    if (!file.is_open())
    {
        std::cerr << "文件打开失败\n";
        return 1;
    }

    file << "Hello World\n";
    file << 123 << '\n';

    file.close();

    return 0;
}
```

运行后会生成 `test.txt`：

``` text
Hello World
123
```

`ofstream` 和 `std::cout` 的使用方式很像：

``` cpp
std::cout << "Hello";
file << "Hello";
```

区别只是数据最终流向不同：

``` text
程序
 │
 ├── std::cout ──────→ 控制台
 │
 └── std::ofstream ──→ 文件
```

------------------------------------------------------------------------

## 3. 默认写入会覆盖原文件

``` cpp
std::ofstream file("test.txt");
file << "hello";
```

如果原文件内容是：

``` text
AAAA
BBBB
CCCC
```

默认打开写文件通常会清空原文件，然后写入：

``` text
hello
```

### 追加模式

如果希望保留原内容，在文件末尾继续写：

``` cpp
std::ofstream file(
    "test.txt",
    std::ios::app
);

file << "new data\n";
```

结果：

``` text
hello
new data
```

------------------------------------------------------------------------

## 4. 使用 `ifstream` 读取文件

假设 `test.txt`：

``` text
Hello World
123
```

代码：

``` cpp
#include <iostream>
#include <fstream>
#include <string>

int main()
{
    std::ifstream file("test.txt");

    if (!file.is_open())
    {
        std::cerr << "文件打开失败\n";
        return 1;
    }

    std::string line;

    while (std::getline(file, line))
    {
        std::cout << line << '\n';
    }

    file.close();
}
```

输出：

``` text
Hello World
123
```

------------------------------------------------------------------------

## 5. `>>` 与 `std::getline()` 的区别

假设文件内容：

``` text
hello world cpp
```

### 使用 `>>`

``` cpp
std::string str;
file >> str;
```

结果：

``` text
hello
```

因为 `operator>>` 默认按照空白字符分隔。

### 使用 `std::getline()`

``` cpp
std::string str;
std::getline(file, str);
```

结果：

``` text
hello world cpp
```

`getline()` 默认读取到换行符 `\n` 为止。

简单记忆：

``` text
>>          按空白分隔读取
getline()   按行读取
```

------------------------------------------------------------------------

## 6. 常见文件打开模式

### `std::ios::in`

读取模式：

``` cpp
std::ifstream file(
    "test.txt",
    std::ios::in
);
```

### `std::ios::out`

写入模式：

``` cpp
std::ofstream file(
    "test.txt",
    std::ios::out
);
```

### `std::ios::app`

追加模式：

``` cpp
std::ofstream file(
    "log.txt",
    std::ios::app
);
```

### `std::ios::binary`

二进制模式：

``` cpp
std::ifstream file(
    "data.bin",
    std::ios::binary
);
```

### `std::ios::trunc`

打开文件时截断，也就是清空原内容。

多个模式可以组合：

``` cpp
std::ofstream file(
    "log.txt",
    std::ios::out | std::ios::app
);
```

------------------------------------------------------------------------

## 7. `std::fstream`：同时读写文件

``` cpp
std::fstream file(
    "test.txt",
    std::ios::in | std::ios::out
);
```

此时既可以读取：

``` cpp
std::string str;
file >> str;
```

也可以写入：

``` cpp
file << "Hello";
```

这里会涉及两个重要的位置概念：

``` text
get pointer → 读取位置
put pointer → 写入位置
```

------------------------------------------------------------------------

## 8. 文件位置指针

假设文件：

``` text
Hello World
```

开始时：

``` text
^Hello World
```

读取几个字符：

``` cpp
char c;

file.get(c);
file.get(c);
file.get(c);
```

位置会向后移动：

``` text
Hel^lo World
```

### `seekg()`

移动读取位置：

``` cpp
file.seekg(0);
```

回到文件开头。

### `seekp()`

移动写入位置：

``` cpp
file.seekp(0);
```

回到文件开头进行写入。

例如：

``` cpp
std::fstream file(
    "test.txt",
    std::ios::in | std::ios::out
);

file.seekp(0);
file << "ABC";
```

原文件：

``` text
Hello World
```

可能变成：

``` text
ABClo World
```

因为这里只是覆盖，没有自动删除后面的内容。

### 获取当前位置

``` cpp
file.tellg(); // 获取读取位置
file.tellp(); // 获取写入位置
```

------------------------------------------------------------------------

## 9. 二进制文件读写

假设有一个结构体：

``` cpp
struct Person
{
    int age;
    double score;
};
```

### 写入二进制文件

``` cpp
Person p{20, 99.5};

std::ofstream file(
    "data.bin",
    std::ios::binary
);

file.write(
    reinterpret_cast<const char*>(&p),
    sizeof(Person)
);
```

### 读取二进制文件

``` cpp
Person p;

std::ifstream file(
    "data.bin",
    std::ios::binary
);

file.read(
    reinterpret_cast<char*>(&p),
    sizeof(Person)
);
```

这里：

``` cpp
read()
write()
```

操作的是原始字节：

``` text
Person 对象
     │
     ▼
+-----------+
| age       |
| score     |
+-----------+
     │
     ▼
data.bin
010101010...
```

> 注意：直接把结构体内存写入文件适合学习和一些受控场景，但如果涉及跨平台、不同编译器、不同版本的数据存储，需要考虑字节序、对齐、padding
> 和数据格式兼容性。

------------------------------------------------------------------------

## 10. 一次读取整个文件

常见写法：

``` cpp
#include <fstream>
#include <string>
#include <iterator>

std::ifstream file("test.txt");

std::string content(
    (std::istreambuf_iterator<char>(file)),
    std::istreambuf_iterator<char>()
);
```

此时：

``` cpp
content
```

中保存了整个文件内容。

适用于：

-   配置文件
-   JSON
-   HTML
-   Shader
-   文本资源

------------------------------------------------------------------------

## 11. 常用 API 总结

### 打开文件

``` cpp
std::ifstream file("a.txt");
std::ofstream file("a.txt");
```

### 判断是否打开成功

``` cpp
if (!file.is_open())
{
}
```

也可以：

``` cpp
if (!file)
{
}
```

### 文本写入

``` cpp
file << "hello";
```

### 格式化读取

``` cpp
file >> str;
```

### 按行读取

``` cpp
std::getline(file, line);
```

### 二进制读取

``` cpp
file.read(buffer, size);
```

### 二进制写入

``` cpp
file.write(buffer, size);
```

### 移动位置

``` cpp
file.seekg();
file.seekp();
```

### 获取位置

``` cpp
file.tellg();
file.tellp();
```

------------------------------------------------------------------------

## 12. 整体知识结构

``` text
                         文件
                          │
              ┌───────────┴───────────┐
              │                       │
            读取                     写入
              │                       │
              ▼                       ▼
          ifstream                ofstream
              │                       │
              └──────────┬────────────┘
                         │
                      fstream
                    读 + 写


文本操作：

    <<
    >>
    getline()


二进制操作：

    read()
    write()


文件位置：

    seekg()
    seekp()

    tellg()
    tellp()
```

------------------------------------------------------------------------

# 13. 推荐学习路线

建议按下面顺序练习：

### 第一阶段：文本文件

1.  创建文件
2.  写入文本
3.  读取文本
4.  使用 `getline()`
5.  使用追加模式

### 第二阶段：文件位置

1.  `seekg()`
2.  `seekp()`
3.  `tellg()`
4.  `tellp()`

理解文件内部的"当前位置"。

### 第三阶段：二进制文件

1.  `read()`
2.  `write()`
3.  对象与字节
4.  文件偏移

### 第四阶段：小项目练习

可以尝试：

1.  **文件复制工具** ------ 练习 `read/write`
2.  **日志系统** ------ 练习追加写入
3.  **简单配置文件读取器** ------ 练习文本解析
4.  **简单 KV 文件存储** ------ 练习二进制、偏移量和索引

------------------------------------------------------------------------

# 最后总结

可以把 C++ 文件 IO 理解成一句话：

> **流对象负责管理"程序"和"文件"之间的数据传输。**

``` text
ifstream

文件 ─────────→ 程序


ofstream

程序 ─────────→ 文件


fstream

文件 ←────────→ 程序
```

核心 API：

``` text
文本：
<<
>>
getline()

二进制：
read()
write()

位置：
seekg()
seekp()
tellg()
tellp()

状态：
is_open()
good()
eof()
fail()
```
