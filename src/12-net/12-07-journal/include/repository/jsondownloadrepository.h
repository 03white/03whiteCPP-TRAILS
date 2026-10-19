#ifndef JSONDOWNLOADREPOSITORY_H
#define JSONDOWNLOADREPOSITORY_H
#include "entity/filemeta.h"
#include "idownloadrepository.h"
#include "util/atomicfile.h"
#include <fstream>
class JSONDownloadRepository:public IDownloadRepository {
    using json=nlohmann::json;
public:
    explicit JSONDownloadRepository(const std::string& metaJson_url);
    FileMetaPtr readFileMeta() override;
    void saveFileMeta(const FileMetaPtr& fileMetaPtr)override;
    void updateFileMetaPiece(const std::vector<Piece>& piecePool)override;
private:
    /**
     * @brief 
     * 缓存Json头，这样在落盘时，就不需要读xxx.meta.json，
     * 而是直接序列化piecePool的内存快照后直接写
     */
    void cacheHeader(const json& full); 
    std::string metaJson_url_;
    json headerJson_=json::object();
};
#endif // !JSONDOWNLOADREPOSITORY_H