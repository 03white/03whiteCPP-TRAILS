#include "core/piecemanager.h"
#include <stdexcept>

PieceManager::PieceManager(FileMetaPtr file) : fileMetaPtr_(file) {
    if (!fileMetaPtr_) {
        throw std::invalid_argument("file metadata is null");
    }
    for (const auto& piece : fileMetaPtr_->pieces) {
        if (!piece || piece->downloaded > piece->length) {
            throw std::runtime_error("invalid piece metadata");
        }
        if (piece->downloaded == piece->length) {
            piece->state = PieceState::Done;
            done_count_++;
        } else {
            piece->state = PieceState::None;
            queue_.push_back(piece);
        }
    }
}

auto PieceManager::acquire() -> std::optional<PiecePtr> {
    std::lock_guard<std::mutex> lock(mtx_);
    if (queue_.empty() || cancelled_) {
        return std::nullopt;
    }
    PiecePtr piece = std::move(queue_.back());      // move 出来，省一次引用计数
    queue_.pop_back();
    return piece;
}

void PieceManager::advanceDownloaded(const PiecePtr& piece, uint64_t flushed) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (flushed > piece->length - piece->downloaded) {
        throw std::runtime_error("piece downloaded bytes exceed its length");
    }
    piece->downloaded += flushed;
    if (piece->downloaded == piece->length) {       // 满了一块 → 自动置 Done
        piece->state = PieceState::Done;
        done_count_++;
    }
    generation_++;
}

std::vector<Piece> PieceManager::snapshot() const {
    std::vector<Piece> snapshot;
    std::lock_guard<std::mutex> lock(mtx_);
    snapshot.reserve(fileMetaPtr_->pieces.size());
    for (const auto& piece : fileMetaPtr_->pieces) {
        snapshot.push_back(*piece);                 // 拷贝，不是引用
    }
    return snapshot;
}

uint64_t PieceManager::generation() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return generation_;
}

size_t PieceManager::done_count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return done_count_;
}

size_t PieceManager::total() const {
    return fileMetaPtr_->pieces.size();             // 总块数，固定不变
}

bool PieceManager::all_done() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return done_count_ == fileMetaPtr_->pieces.size();
}

void PieceManager::cancel() {
    std::lock_guard<std::mutex> lock(mtx_);
    cancelled_ = true;
}

bool PieceManager::cancelled() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return cancelled_;
}
