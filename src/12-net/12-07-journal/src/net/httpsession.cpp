#include "net/httpsession.h"
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

HttpHeaderlist::~HttpHeaderlist() {
    reset();
}

HttpHeaderlist::HttpHeaderlist(HttpHeaderlist&& other) noexcept :
    list_(std::exchange(other.list_, nullptr)) {
}

HttpHeaderlist& HttpHeaderlist::operator=(HttpHeaderlist&& other) noexcept {
    if (this != &other) {
        reset();
        list_ = std::exchange(other.list_, nullptr);
    }
    return *this;
}
void HttpHeaderlist::append(const std::string& header) {
    auto newList_ = curl_slist_append(list_, header.c_str());
    if (!newList_) {
        throw std::bad_alloc{};
    }
    list_ = newList_;
}

void HttpHeaderlist::reset() noexcept {
    if (list_) {
        curl_slist_free_all(list_);
        list_ = nullptr;
    }
}

HttpSession::HttpSession() : httpSession_(curl_easy_init()) {
    if (!httpSession_) {
        throw std::runtime_error("curl_easy_init failed");
    }
}

void HttpSession::throw_setopt_failed(CURLcode code) {
    throw std::runtime_error("curl_easy_setopt failed："
                             + std::string(curl_easy_strerror(code)));
}

CURLcode HttpSession::perform() noexcept {
    return curl_easy_perform(httpSession_.get());
}

void HttpSession::reset() noexcept {
    curl_easy_reset(httpSession_.get());
}
