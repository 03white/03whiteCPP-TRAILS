#ifndef PIECEMANAGER_H
#define PIECEMANAGER_H
#include "entity/filemeta.h"
#include <mutex>
#include <optional>
class PieceManager {
public:
    explicit PieceManager(FileMetaPtr file);        // 构造时，把 downloaded != length 的块入队
    auto acquire()->std::optional<PiecePtr>;        // 弹出一块；队列空或 cancelled 返回 nullopt，非阻塞
    void advanceDownloaded(const PiecePtr& piece, uint64_t flushed);  // flush 之后推进 downloaded，满则置 done
    std::vector<Piece> snapshot() const;            // 供 checkpoint 用，返回拷贝
    uint64_t generation() const; // 记账次数，供 checkpoint 判断有没有变
    size_t done_count() const;
    size_t total() const;
    bool all_done() const;
    void cancel();
    bool cancelled() const;
private:
    FileMetaPtr fileMetaPtr_;
    std::vector<PiecePtr> queue_;                   // 待下载块工作队列（与 FileMeta::pieces 指向同一对象）
    mutable std::mutex mtx_;                        // 供 const 方法使用，用 mutable 移除 const 限制
    size_t done_count_ = 0;
    uint64_t generation_ = 0; 
    bool cancelled_ = false;
};
using PieceManagerPtr=std::shared_ptr<PieceManager>;
#endif // !PIECEMANAGER_H
