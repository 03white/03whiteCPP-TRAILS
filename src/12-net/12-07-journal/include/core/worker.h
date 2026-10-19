#ifndef WORKER_H
#define WORKER_H
#include "piecemanager.h"
#include <exception>
#include <fstream>
class Worker {
public:
    Worker(PieceManagerPtr pieceManagerPtr,
           const std::string& target_url,
           const std::string& local_url,
           const std::string& file_name);
    /**
     * @brief
     * 从pieceManager中获取一个未下载完成的分片，根据downloaded值，构造range头，发起网络请求，写缓冲，落盘之后
     * 锁内更新fileMeta的piece downloaded进度
     */
    void run();

private:
    static size_t write_trampoline(void* ptr, size_t size, size_t nmem, void* stream);
    size_t write_callback(void* ptr, size_t size) noexcept;
    std::fstream f_;
    PieceManagerPtr pieceManagerPtr_;
    PiecePtr currentPiece_;
    bool writeFailed_ = false;
    std::exception_ptr callbackError_;
    std::string target_url_;
    std::string local_url_;
    std::string file_name_;
};

#endif // !WORKER_H
