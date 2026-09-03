#include <iostream>
#include <string>
#include <curl/curl.h>
static size_t write_to_string(void* contents,size_t size,size_t nmemb,void* userp){
    size_t total_size=size*nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents),total_size);
    return total_size;
}
int main(){
    CURL*curl;
    CURLcode res;
    std::string data;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl=curl_easy_init();
    if(curl){
        curl_easy_setopt(curl,CURLOPT_URL,"https://httpbin.org/get");
        curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_to_string);
        curl_easy_setopt(curl,CURLOPT_WRITEDATA,&data);
        res=curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "请求失败: " << curl_easy_strerror(res) << std::endl;
        } else {
            std::cout << "响应数据: " <<data<< std::endl;
        }
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    return 0;
}