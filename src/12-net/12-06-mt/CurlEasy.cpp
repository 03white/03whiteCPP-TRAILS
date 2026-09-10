#include "CurlEasy.hpp"
#include <stdexcept>
#include <utility>

GlobalCurlGuard::GlobalCurlGuard() {
    CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (code != CURLE_OK) {
        throw std::runtime_error("curl_global_init failed: "
                                 + std::string(curl_easy_strerror(code)));
    }
}

GlobalCurlGuard::~GlobalCurlGuard() {
    curl_global_cleanup();
}


CurlSlist::~CurlSlist() {
    reset();
}

CurlSlist::CurlSlist(CurlSlist&& other) noexcept :
    list_(std::exchange(other.list_, nullptr)) {

}

CurlSlist& CurlSlist::operator=(CurlSlist&& other) noexcept {
    if (this != &other) {
        reset();
        list_=std::exchange(other.list_, nullptr);
    }
    return *this;
}
void CurlSlist::append(const std::string& header) {
    auto newList_=curl_slist_append(list_, header.c_str());
    if (!newList_) {
        throw std::bad_alloc{};
    }
    list_ = newList_;
}

void CurlSlist::reset() noexcept {
    if (list_) {
        curl_slist_free_all(list_);
        list_ = nullptr;
    }
}


CurlEasy::CurlEasy():handle_(curl_easy_init()){
    if (!handle_) {
        throw std::runtime_error("curl_easy_init failed");
    }
}

void CurlEasy::throw_setopt_failed(CURLcode code) {
    throw std::runtime_error("curl_easy_setopt failed：" + std::string(curl_easy_strerror(code)));
}

CURLcode CurlEasy::perform() noexcept {
    return curl_easy_perform(handle_.get());
}

void CurlEasy::reset() noexcept {
    curl_easy_reset(handle_.get());
}
