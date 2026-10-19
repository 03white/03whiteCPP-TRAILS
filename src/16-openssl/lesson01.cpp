/* lesson01.c
 * 摘要：EVP_MD_CTX + EVP_DigestInit/Update/Final，算一个 SHA-256
 */
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <openssl/evp.h>
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

private:
    EVP_MD_CTX* ctx_;
};
struct Data_sha256 {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_len;
};
int main(void) {
    const char* data = "hello world";
    Data_sha256 sha256_result;
    EVP evp; // RAII wrapper for EVP_MD_CTX
    if (EVP_DigestInit(evp.get(), EVP_sha256()) != 1) {
        fprintf(stderr, "EVP_DigestInit failed\n");
        return 1;
    }

    if (EVP_DigestUpdate(evp.get(), data, strlen(data)) != 1) {
        fprintf(stderr, "EVP_DigestUpdate failed\n");
        return 1;
    }

    if (EVP_DigestFinal(evp.get(), sha256_result.md, &sha256_result.md_len)
        != 1) {
        fprintf(stderr, "EVP_DigestFinal failed\n");
        return 1;
    }
    printf("SHA-256(\"%s\") = ", data);
    for (unsigned int i = 0; i < sha256_result.md_len; i++) {
        printf("%02x", sha256_result.md[i]);
    }
    printf("\n");
    return 0;
}