#ifndef CHECKPOINT_H
#define CHECKPOINT_H
#include "piecemanager.h"
#include "repository/idownloadrepository.h"
#include <chrono>
#include <condition_variable>
#include <mutex>

class Checkpoint {
public:
    Checkpoint(PieceManagerPtr pieceManagerPtr,
               IDownloadRepositoryPtr filedao,
               std::chrono::milliseconds interval);
    void run();  
    void stop(); 

private:
    bool writeOnce(); 

    PieceManagerPtr pieceManagerPtr_;
    IDownloadRepositoryPtr filedao_;
    std::chrono::milliseconds interval_;
    uint64_t lastGeneration_ = 0; // 上次落盘时的 generation,如果新的generation与last generation不同，则证明进度有所更新

    std::mutex stopMtx_; 
    std::condition_variable stopCv_;
    bool stop_ = false;
};
using CheckpointPtr=std::shared_ptr<Checkpoint>;
#endif // !CHECKPOINT_H
