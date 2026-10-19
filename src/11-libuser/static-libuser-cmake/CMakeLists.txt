project(static-libuser-cmake)

# 目录名在磁盘上就是大写 L 的 cmathLib / umathLib。原来这里写的是全小写，
# 只因为 NTFS 大小写不敏感才碰巧能过，换到 Linux 上就是 configure 失败。
add_subdirectory(cmathLib)
add_subdirectory(umathLib)
