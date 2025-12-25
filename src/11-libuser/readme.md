# CPP的包（库）
## 1.关于库的介绍
CPP不同于有着强大的平台无关性的JAVA,Python,它最终编译后的结果就是对应目标平台(windows,linux,macos)的二进制机器码；再者，加之CPP编译器的御三家（GUN/LLVM/MSVC）割据的局面，它的包管理是比较复杂的，往往让我们十分之头疼。我们按照对库的使用方式（其实就是链接方式）不同，可以分为**静态链接库**和**动态链接库**。
## 2.静态链接库与动态链接库
### 2.1 静态链接库
#### 2.1.1 g++命令来做
##### 2.1.1.1 g++创建静态库
```shell
# 1. 编译源文件为目标文件（.o）
g++ -c file1.cpp file2.cpp file3.cpp
# 2. 将目标文件打包成静态库
ar rcs libmylib.a file1.o file2.o file3.o
```
> [Tips]
ar rcs libmylib.a file1.o file2.o fil3.o的意思是archive（归档）
##### 2.1.1.2 g++编译时链接静态库
###### 2.1.1.2.1 直接指定链接法
直接指定库的位置和名字来链接，不用-L搜索库的所在路径
```shell
g++ main.cpp mylib.a(这是完整名字) -o program
```
###### 2.1.1.2.2 搜索链接法

这种方法，我们需要给出两个参数：
- 1.库的绝对路径或者相对路径：-L+路径
- 2.库除去lib前缀的名字：-lmylib
*注意！我们使用这种方式，静态库的名字必须要有lib前缀！！！*
```shell
#相对路径：本路径
g++ main.cpp -L. -lmylib -o program
#绝对路径：xxx
g++ main.cpp -L/path/to/lib -lmylib -o program
```

#### 2.1.2 cmake来管理（现代做法）
##### 2.1.2.1 cmake创建静态库
这是一种比较适合自己写东西时，用的方法，说实话，没有config.cmake，确实优点古代了。
```cmake
project(cmathlib)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/lib)
add_library(${PROJECT_NAME} STATIC math.cpp)
```
##### 2.1.2.2 cmake链接静态库
这里我们也只是提供一种比较古代的手动搜索.h和.a库的方法，牢。
```cmake
project(umathlib)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/bin)
# 1.包含所有头文件的目录
include_directories(${PROJECT_SOURCE_DIR}/include)  # 对应 g++ -I
add_executable(${PROJECT_NAME} main.cpp)    
# 3. 链接静态库
link_directories(${PROJECT_SOURCE_DIR}/lib)   #  对应g++ -L路径
target_link_libraries(${PROJECT_NAME} PRIVATE #  对应 g++ -lcmathlib
    cmathlib
)
```
实际上，这里头的语句，可以对应成为g++的行为，所以我们结合cmake关于g++的对应关系，就可以很好的理解这些语句了。
关于target_link_libraries()里头的PRIVATE,PUBLIC...参数，实际上，是一种控制最终编译单元尽可能小的上层抽象控制手段，我们使用PRIVATE，那么，依赖不会传递下去，PUBLIC反之。
### 2.2 动态连接库[windows下，MinGW为例子]
#### 2.2.1 g++命令来做
##### 2.2.1.1 g++创建动态库
1. 一步实现
```shell
g++ shared main.cpp -o main.dll -BUILD_DLL '-Wl,--out-implib,libmath.a'
```
2. 分步实现（更专业）
```shell

```
##### 2.2.1.2 g++运行时链接动态库(动态库的使用)
我们可以做一个实验，来证明dll是运行时链接。
我们可以修改math.cpp里的add函数体的实现，然后重新编译出libmath.a（导入库）和libmath.dll，神奇的来了，然后我们只需要./main.exe，就会起效果！！！
###### 2.2.1.2.1 g++运行时隐士链接动态库
```shell
g++ main.cpp -o main.exe -L. -lmatn
// 链接导入库 libmath.a，编译器在可执行文件中记录
// 运行时操作系统自动加载 libmath.dll
```
###### 2.2.1.2.2 g++运行时显示链接动态库
使用显示链接是，不需要.h文件，不需要.a的导入库，只需要在代码里用一些系统函数就可以了。
```cpp
#include <windows.h>
#include <iostream>
using AddFunc=int(*) (int,int);
int main(){
    HMODULE dll = LoadLibrary("libmath.dll");  // 手动加载DLL
    if (!dll) {
        std::cerr << "Failed to load DLL" << std::endl;
        return 1;
    }
    
    AddFunc add = (AddFunc)GetProcAddress(dll, "add");  // 手动获取函数
    if (!add) {
        std::cerr << "Failed to get function" << std::endl;
        FreeLibrary(dll);
        return 1;
    }
    
    int result = add(3, 4);
    std::cout << "3 + 4 = " << result << std::endl;
    FreeLibrary(dll);  // 手动卸载DLL
}
```
```shell
g++ main1.cpp -o main1.exe
./main1.exe
```
*注意：使用显示链接的时候，需要解除函数的名称修饰（使用extern"C"，不然我们在GetProcAddress(dll,"add')的时候，"add"这个名字会找不到，因为会被修饰*
![alt text](image.png)
> [Tips]
nm xxx.dll 命令是查看.dll的符号信息；

学一个小指令：
```shell
objdump -M intel -d libmath.dll >disasm_intel.txt 2>&1
```
- objdump：GUN二进制里的反汇编/符号查看工具
- -d：disassemble（反汇编所有可执行/代码段）
- -M intel：使用Intel风格语法（默认是AT&T）
- libmath.dll：要反汇编的输入文件。
- (>) 把标准输出（stdout）重定向到文件（覆盖写入）---这里是disasm_intel.txt
- 2>&1：把标准错误（stderr，描述警告/错误信息）重定向到标准输出，保证stderr也写进同一个文件

#### 2.2.2 cmake来管理
##### 2.2.2.1 cmake创建动态库
##### 2.2.2.2 cmake链接动态库