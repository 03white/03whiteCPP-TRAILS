#ifndef DOWNLOADER_H
#define DOWNLOADER_H
#include "repository/idownloadrepository.h"
#include "piecemanager.h"
#include "checkpoint.h"
#include <filesystem>
#include <vector>
#include <thread>
class Downloader {
public:
    /**
     * @brief 构造下载器，恢复或新建一次下载任务的元信息。
     *
     * 构造时先扫描 @p local_url 所在目录：
     * - 若已存在 xxx.meta.json / xxx.part，视为断点续传，读取 meta 还原 File；
     * - 否则向 @p target_url 发起请求，从响应头构造 File，并同步写出
     *   xxx.meta.json 完成持久化。
     *
     * 所有与 xxx.meta.json 的交互均委托给 FileDAO，本类不直接读写该文件。
     *
     * @param target_url 待下载资源的完整 URL。
     * @param local_url  本地保存路径，同时决定 .meta.json / .part 的存放位置。
     * @param pieceSize  分块大小（字节），决定 File 被切分成多少个 Piece。
     * @param threadNum  工作线程数，即并发下载的 Piece 数量上限。
     *
     * @throws std::runtime_error 元信息请求失败，或本地 meta 与远端资源不一致。
     *
     * @note 服务器不支持 Range 时退化为单线程整文件下载。
     *
     */
    Downloader(const std::string& target_url,
               const std::string& local_url,
               size_t pieceSize,
               size_t threadNum);
    ~Downloader();

    /**
     * @brief 启动下载，阻塞至全部分块完成或失败。
     *
     * 按构造时确定的 threadNum 拉起工作线程，各线程从待下载队列中取 Piece
     * 并发拉取；每完成一块通过 FileDAO 回写进度。全部完成后将 .part
     * 重命名为最终文件并清理 .meta.json。
     *
     * @throws std::runtime_error 达到重试上限后仍有分块失败。
     *
     * @note 调用前 main() 中必须已完成 curl_global_init()，
     *       libcurl 的全局初始化不是线程安全的。
     */
    void start();

private:
    void preparePartFile(const std::filesystem::path& partPath) const;
    static size_t header_tramplie(void* ptr, size_t nmem, size_t size, void* stream);
    void header_callback(void* ptr, size_t size);
    FileMetaPtr fileMetaPtr_;
    PieceManagerPtr pieceManagerPtr_;
    IDownloadRepositoryPtr filedaoPtr_;
    CheckpointPtr checkpointPtr_;
    size_t threadNum_;
    std::vector<std::thread> pieceTasks_;
    std::thread checkpointTask_;
    bool started_ = false;
};
#endif // !DOWNLOADER_H
