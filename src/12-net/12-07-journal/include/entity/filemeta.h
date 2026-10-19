#ifndef FILEMETA_H
#define FILEMETA_H
#include "piece.h"
#include <memory>
#include <nlohmann/json.hpp>
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
struct FileMeta {
    std::string name;
    std::string target_url;
    std::string local_url;
    uint64_t totalSize = 0;
    size_t pieceNum = 0;
    uint64_t pieceSize = 0;
    std::vector<PiecePtr> pieces; 

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(FileMeta, name, target_url, local_url, totalSize, pieceNum, pieceSize,pieces)
};
using FileMetaPtr = std::shared_ptr<FileMeta>;

NLOHMANN_JSON_NAMESPACE_BEGIN //全特化FileMeta的shared_ptr，保证可以直接json j=xxxptr;
template <>
struct adl_serializer<FileMetaPtr> {
    static void to_json(json& j, const FileMetaPtr& p) {
        if (p) {
            j = *p;
        } else {
            j = nullptr;
        }
    }

    static void from_json(const json& j, FileMetaPtr& p) {
        if (j.is_null()) {
            p = nullptr;
        } else {
            p = std::make_shared<FileMeta>();
            j.get_to(*p);
        }
    }
};
NLOHMANN_JSON_NAMESPACE_END

#endif // !FILE_H
