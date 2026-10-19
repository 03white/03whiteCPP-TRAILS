#pragma once
#include <openssl/evp.h>
#include <stdexcept>
class EVP {
public:
    EVP() {
        ctx_ = EVP_MD_CTX_new();
        if (ctx_ == NULL) {
            throw std::runtime_error("EVP_MD_CTX_new failed");
        }
    }
    ~EVP() {
        if (ctx_)
            EVP_MD_CTX_free(ctx_);
    }
    EVP_MD_CTX* get() const noexcept {
        return ctx_;
    }
    int init(const EVP_MD* type) {
        return EVP_DigestInit(ctx_, type);
    }
    int update(const char* data, size_t len) {
        return EVP_DigestUpdate(ctx_, data, len);
    }
    int final(unsigned char* sha256sum, unsigned int& len) {
        return EVP_DigestFinal(ctx_, sha256sum, &len);
    }

private:
    EVP_MD_CTX* ctx_;
};