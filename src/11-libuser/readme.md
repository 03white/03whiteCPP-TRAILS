# CPP的包（库）
## 1.关于库的介绍
CPP不同于有着强大的平台无关性的JAVA,Python,它最终编译后的结果就是对应目标平台的二进制机器码，所以，它的包管理是比较复杂的，往往让我们十分之头疼，我们按照对库的使用方式（其实就是链接方式）不同，可以分为**静态链接库**和**动态链接库**。
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
### 2.2 动态连接库
#### 2.2.1 g++命令来做
##### 2.2.1.1 g++创建动态库
##### 2.2.1.2 g++运行时链接动态库
###### 2.2.1.2.1 g++运行时隐士链接动态库
###### 2.2.1.2.2 g++运行时显示链接动态库
#### 2.2.2 cmake来管理
##### 2.2.2.1 cmake创建动态库
##### 2.2.2.2 cmake链接动态库