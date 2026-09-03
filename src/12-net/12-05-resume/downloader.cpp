#include"downloader.h"
void Downloader::load(const std::string target_url, const std::string local_url) {
    this->target_url = target_url;
    this->local_url = local_url;
    std::error_code ec;
    uintmax_t file_size = std::filesystem::file_size(local_url, ec);
    if (ec) {
        this->downloaded_size = 0;
    } else {
        this->downloaded_size = static_cast<size_t>(file_size);
    }
    std::cout << "[DEBUG] downloaded_size = " << downloaded_size << std::endl;


    this->output_file.open(local_url, std::ios::binary | std::ios::app);
    if (!output_file.is_open()) {
        throw std::runtime_error("Failed to open file: " + local_url);
    }
    header_list.append(
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    if (downloaded_size > 0) {
        header_list.append("Range: bytes=" + std::to_string(downloaded_size)
                           + "-");
    }
    this->curl.setopt(CURLOPT_URL, target_url.c_str());
    this->curl.setopt(CURLOPT_WRITEFUNCTION, &Downloader::write_triple);
    this->curl.setopt(CURLOPT_WRITEDATA, this);
    this->curl.setopt(CURLOPT_HTTPHEADER, this->header_list.get());
    this->curl.setopt(CURLOPT_FOLLOWLOCATION, 1L);
    this->curl.setopt(CURLOPT_MAXREDIRS, 5L);
    this->curl.setopt(CURLOPT_NOPROGRESS, 0L);
    this->curl.setopt(CURLOPT_XFERINFOFUNCTION, &Downloader::progress_triple);
    this->curl.setopt(CURLOPT_XFERINFODATA, this);
    CURLcode res = curl.perform();
    if (res != CURLE_OK) {
        throw std::runtime_error("curl error: "
                                 + std::string(curl_easy_strerror(res)));
    }
    long http_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200 && http_code != 206) {
        // 可能是 301/302/403/416 等，说明请求没成功
        std::cerr << "Unexpected HTTP status: " << http_code << std::endl;
    }
}

size_t Downloader::write_triple(void* ptr, size_t size, size_t nmemb, void* stream) {
    auto* self = static_cast<Downloader*>(stream);
    self->write_callback(ptr, size * nmemb);
    return size * nmemb;
}

void Downloader::write_callback(void* ptr, size_t size) {
    output_file.write(static_cast<char*>(ptr), size);
}

int Downloader::progress_triple(void* clientp,
    curl_off_t dltotal,
    curl_off_t dlnow,
    curl_off_t ultotal,
    curl_off_t ulnow)
{
    auto* self = static_cast<Downloader*>(clientp);
    self->progress_callback(dltotal, dlnow);
    return 0;
}
int Downloader::progress_callback(curl_off_t dltotal, curl_off_t dlnow) {
    this->callback(dltotal+downloaded_size, dlnow+downloaded_size);
    return 0;
}