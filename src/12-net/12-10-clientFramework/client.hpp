#pragma once
#include <curl/curl.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <iostream>

/*
POST /api/report HTTP/1.1
Host: iot.example.com
Content-Type: application/json; charset=utf-8
Content-Length: 58
{"deviceId":"sensor-001","temperature":25.6,"humidity":60}
*/

/*
HTTP/1.1 200 OK
Content-Type: application/json; charset=utf-8
Content-Length: 59
{"code":0,"message":"success","data":{"id":1,"name":"Tom"}}
*/

enum HttpMethod { GET, POST, PUT, DELETE_ };

struct Response {
    long status_code = 0;
    std::string body;
    std::string headers;

    bool ok() const {
        return status_code >= 200 && status_code < 300;
    }
};

class Client {
public:
    explicit Client(std::string base_url) : base_url_(std::move(base_url)) {
        // 这是什么东西
        static std::once_flag once;
        std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    }

    // client.endpoint<GET>("/path", [](const Response& res){ ... });
    // 同步阻塞请求
    template <HttpMethod M> // 非类型模板参数
    void endpoint(const std::string& path,
                  std::function<void(const Response&)> callback,
                  const std::string& body = "",
                  const std::vector<std::string>& headers = {}) {
        CURL* curl = curl_easy_init();
        if (!curl)
            return;

        Response res;
        const std::string url = base_url_ + path;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &res.headers);

        // 根据模板参数选择 HTTP 方法
        if constexpr (M == GET) {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        } else if constexpr (M == POST) {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
        } else if constexpr (M == PUT) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
        } else if constexpr (M == DELETE_) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        }

        struct curl_slist* hlist = nullptr;
        for (const auto& h : headers)
            hlist = curl_slist_append(hlist, h.c_str());
        if (hlist)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hlist);

        CURLcode rc = curl_easy_perform(curl); // 执行请求,阻塞直到完成
        if (rc == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status_code);
            if (callback)
                callback(res);
        } else {
            std::cerr << "[curl] " << curl_easy_strerror(rc) << " : " << url
                      << "\n";
        }

        if (hlist)
            curl_slist_free_all(hlist);
        curl_easy_cleanup(curl);
    }

private:
    std::string base_url_;

    static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* ud) {
        static_cast<std::string*>(ud)->append(ptr, size * nmemb);
        return size * nmemb;
    }
    static size_t header_cb(char* buf, size_t size, size_t nitems, void* ud) {
        static_cast<std::string*>(ud)->append(buf, size * nitems);
        return size * nitems;
    }
};