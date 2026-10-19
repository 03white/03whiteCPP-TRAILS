#include "repository/jsondownloadrepository.h"
#include <fstream>
#include <stdexcept>

/*
*
* 这是一个file_meta.json示例
{
    "name":"file1.txt",
    "target_url":"D:/xxx/xxx.txt",
    "local_url":"C:/jkjlk/jkljl.txt",
    "totalSize":1014,
    "pieceNum":10,
    "pieceSize":1024,
    "pieces":
    [
        {
            "index":0,
            "offset":xxx,
            "length":yyy,
            "downloaded":xxx,
            "State":"None",
            "Hash":"sha256"
        }
    ]
}
*/
JSONDownloadRepository::JSONDownloadRepository(const std::string& metaJson_url)
    : metaJson_url_(metaJson_url) 
{

}

FileMetaPtr JSONDownloadRepository::readFileMeta() {
    std::ifstream input(metaJson_url_);
    if (!input) {
        throw std::runtime_error("Failed to open metadata file: "
                                 + metaJson_url_);
    }

    json fileMetaJson;
    input >> fileMetaJson;
    cacheHeader(fileMetaJson);
    FileMetaPtr fileMetaPtr = fileMetaJson;
    return fileMetaPtr;
}

void JSONDownloadRepository::saveFileMeta(const FileMetaPtr& fileMetaPtr) {
    if (!fileMetaPtr) {
        throw std::invalid_argument("fileMetaPtr is null");
    }

    json fileMetaJson = fileMetaPtr;
    cacheHeader(fileMetaJson);
    const std::string serialized = fileMetaJson.dump(4);
    util::writeAtomic(metaJson_url_, serialized);
}

void JSONDownloadRepository::updateFileMetaPiece(const std::vector<Piece>& piecePool) {
    if (headerJson_.empty()) {
        throw std::logic_error("metadata header is not initialized");
    }

    json fileMetaJson = headerJson_;
    fileMetaJson["pieces"] = piecePool;
    const std::string serialized = fileMetaJson.dump(4);
    util::writeAtomic(metaJson_url_, serialized);
}

void JSONDownloadRepository::cacheHeader(const json& full) {
    headerJson_["name"] = full["name"];
    headerJson_["target_url"] = full["target_url"];
    headerJson_["local_url"] = full["local_url"];
    headerJson_["totalSize"] = full["totalSize"];
    headerJson_["pieceNum"] = full["pieceNum"];
    headerJson_["pieceSize"] = full["pieceSize"];
}

