project(16-openssl)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/bin)

# OpenSSL 走的是 CMake 自带的 FindOpenSSL 模块（不是 CONFIG 模式），
# vcpkg 的 toolchain 会把 D:/vcpkg/installed/x64-windows 挂到搜索路径上，
# 所以这里不需要写 OPENSSL_ROOT_DIR。给出的 imported target 有三个：
#   OpenSSL::Crypto   —— libcrypto，摘要/对称加密/大数/RSA/EC/PEM/X509 编解码
#   OpenSSL::SSL      —— libssl，TLS 握手与记录层，依赖 Crypto（link SSL 会自动带上）
#   OpenSSL::applink  —— 仅 MSVC 需要，见下面 APPLINK 的说明
find_package(OpenSSL REQUIRED)

# 每写完一课，把对应的 lessonNN 从注释里挪出来。
# 这里额外加了 EXISTS 判断：文件还没建时静默跳过，不至于 configure 就炸。
set(LESSONS
    lesson01        # 摘要：EVP_MD_CTX + EVP_DigestInit/Update/Final，算一个 SHA-256
    # lesson02      # HMAC / EVP_MAC
    # lesson03      # 对称加密：EVP_CIPHER_CTX，AES-256-CBC 加解密一段数据
    # lesson04      # 随机数与 Base64/PEM 编解码（RAND_bytes、BIO 链）
    # lesson05      # 非对称：EVP_PKEY 生成 RSA/EC 密钥，签名与验签
    # lesson06      # X509 证书解析：读 PEM，打印 subject / issuer / 有效期
    # lesson07      # TLS 客户端：SSL_CTX + BIO_new_ssl_connect，拉一次 https
)

foreach(lesson IN LISTS LESSONS)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${lesson}.cpp)
        message(STATUS "16-openssl: 跳过 ${lesson}.cpp（文件还不存在）")
        continue()
    endif()

    set(tgt ${PROJECT_NAME}-${lesson})
    add_executable(${tgt} ${lesson}.cpp)
    target_compile_features(${tgt} PRIVATE cxx_std_17)

    # 只 link OpenSSL::SSL 也能拿到 Crypto（INTERFACE_LINK_LIBRARIES 里带着），
    # 但前几课根本用不到 TLS，显式写两个更能看出哪一课依赖了什么。
    target_link_libraries(${tgt} PRIVATE OpenSSL::SSL OpenSSL::Crypto)

    # applink：OpenSSL 的 DLL 是用它自己那份 CRT 编的，而 exe 用的是我们这份。
    # 一旦跨边界传 FILE* / fd（比如 PEM_read_PrivateKey(FILE*)、BIO_new_fp(stdout)），
    # 两份 CRT 的 FILE 结构体不是同一个东西，直接崩。OpenSSL::applink 会把
    # include/openssl/applink.c 编进 exe，架一层转发表把这类调用打回本进程的 CRT。
    # 代价只有一个目标文件，无脑挂上就行；MinGW 下这个 target 不存在，所以判一下。
    if(TARGET OpenSSL::applink)
        target_link_libraries(${tgt} PRIVATE OpenSSL::applink)
    endif()

    # 运行期 DLL 跟着 exe 走：libssl-3-x64.dll / libcrypto-3-x64.dll。
    # 文件名由 CMake 从 imported target 上自己算，不用手写，也不用区分 Debug/Release。
    # 空列表时 copy_if_different 会因参数不足报错，所以套一层 $<IF:$<BOOL:...>> 判空。
    add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E
                $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${tgt}>>,copy_if_different,true>
                $<TARGET_RUNTIME_DLLS:${tgt}> $<TARGET_FILE_DIR:${tgt}>
        COMMAND_EXPAND_LISTS
        COMMENT "Copying OpenSSL runtime DLLs next to ${tgt}"
    )
endforeach()

message(STATUS "16-openssl: OpenSSL ${OPENSSL_VERSION} @ ${OPENSSL_INCLUDE_DIR}")
