project(13-windows)

# 13-04-Hack 目前是空目录，还没有 CMakeLists.txt，
# 现在挂上去 configure 会直接报错。开工时再把对应的 add_subdirectory 放开。
add_subdirectory(13-01-apm)
add_subdirectory(13-02-GDI)
add_subdirectory(13-03-OS)
# add_subdirectory(13-04-Hack)
