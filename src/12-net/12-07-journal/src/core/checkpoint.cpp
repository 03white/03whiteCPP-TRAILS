#include "core/checkpoint.h"

Checkpoint::Checkpoint(PieceManagerPtr pieceManagerPtr,
                       IDownloadRepositoryPtr filedao,
                       std::chrono::milliseconds interval) 
                    : pieceManagerPtr_(pieceManagerPtr)
                    , filedao_(filedao)
                    , interval_(interval)
{
}

void Checkpoint::run() {
    while (true) {
        { 
            std::unique_lock<std::mutex> lock(stopMtx_);
            if (stopCv_.wait_for(lock, interval_, [&]{ return stop_;
                })) {
                break;
            }
        }
        writeOnce();
    }
    writeOnce();

}

void Checkpoint::stop() {
    std::lock_guard<std::mutex> lock(stopMtx_);
    stop_ = true;
    stopCv_.notify_all();
}

bool Checkpoint::writeOnce() {
    auto generation = pieceManagerPtr_->generation();
    if (lastGeneration_ == generation) {
        return false;
    } else {
        auto piecePool = pieceManagerPtr_->snapshot();
        filedao_->updateFileMetaPiece(piecePool);
        lastGeneration_ = generation;
        return true;
    }
}
