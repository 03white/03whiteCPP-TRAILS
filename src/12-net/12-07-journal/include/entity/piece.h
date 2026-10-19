#ifndef PIECESTATE_H
#define PIECESTATE_H
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
using json=nlohmann::json;
enum class PieceState { None, Done };//分片只有二态：下载完，未下载完

NLOHMANN_JSON_SERIALIZE_ENUM(PieceState, {
    {PieceState::None, "None"},
    {PieceState::Done, "Done"},
})

struct Piece {
    uint64_t index = 0;
    uint64_t offset = 0;     // 文件偏移
    uint64_t length = 0;     // 分片总长度
    uint64_t downloaded = 0; // 已下载字节（支持分片内续传）
    PieceState state = PieceState::None;
    std::string hash; // 可选：分片校验

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Piece, index, offset, length, downloaded, state, hash)
};
using PiecePtr = std::shared_ptr<Piece>;

NLOHMANN_JSON_NAMESPACE_BEGIN //全特化Piece的shared_ptr，保证可以直接json j=xxxptr;
template <>
struct adl_serializer<PiecePtr> {
    static void to_json(json& j, const PiecePtr& p) {
        if (p) {
            j = *p;
        } else {
            j = nullptr;
        }
    }

    static void from_json(const json& j, PiecePtr& p) {
        if (j.is_null()) {
            p = nullptr;
        } else {
            p = std::make_shared<Piece>();
            j.get_to(*p);
        }
    }
};
NLOHMANN_JSON_NAMESPACE_END

#endif // !PIECESTATE_H
