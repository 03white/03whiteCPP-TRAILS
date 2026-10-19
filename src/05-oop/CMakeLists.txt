project(05-oop)

# 纯聚合层，不建 target。原来这里直接 add_executable 了 copyctl.cpp 和
# 05-02-Plugin 下的三个源文件，有两个问题：
#   1. copyctl.cpp 已经移进 05-04-copyctl/，路径失效，generate 阶段直接报错；
#   2. Plugin 那一组 host.cpp + plugin_*.cpp 被编进同一个 exe，插件根本没做成
#      DLL，host 里的 LoadLibrary 永远扫不到东西，demo 跑不出效果。
# 按 12-net / 13-windows 的惯例拆成子目录，各管各的。

add_subdirectory(05-01-Observer)
add_subdirectory(05-02-Plugin)
# 05-03-Inherit 暂不挂：目录里只有 Inherit.h，而 main() 就写在这个头文件里，
# 没有 .cpp 可编。等拆出 Inherit.cpp 再放开。
# add_subdirectory(05-03-Inherit)
add_subdirectory(05-04-copyctl)
