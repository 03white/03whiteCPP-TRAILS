#include <curl/curl.h>
#include <stdio.h>
int main(void) {
    printf("libcurl %s\n", curl_version());
    return 0;
}