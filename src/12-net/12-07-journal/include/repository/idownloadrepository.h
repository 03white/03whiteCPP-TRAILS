#ifndef IDOWNLOADREPOSITORY_H
#define IDOWNLOADREPOSITORY_H
#include "entity/filemeta.h"
class IDownloadRepository {
public:
    virtual ~IDownloadRepository() = default;
    virtual FileMetaPtr readFileMeta() = 0;
    virtual void saveFileMeta(const FileMetaPtr& fileMetaPtr) = 0;
    virtual void updateFileMetaPiece(const std::vector<Piece>& piecePool) = 0;
};
using IDownloadRepositoryPtr=std::shared_ptr<IDownloadRepository>;
#endif // !IDOWNLOADREPOSITORY_H
