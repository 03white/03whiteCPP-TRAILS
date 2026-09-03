#include<iostream>
#include<fstream>
#include<curl/curl.h>

//纯C风格的回调函数
static size_t write_data(void*ptr,size_t size,size_t nmemb,void*stream){
    std::ofstream *fp = static_cast<std::ofstream*>(stream);
    fp->write(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}
int main(){
    CURL*curl;
    CURLcode res;
    std::ofstream fp;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl=curl_easy_init();//创建一个句柄
    if(curl){
        fp.open("example.html",std::ios::binary|std::ios::out);
        if(!fp){
            std::cerr<<"can't create file"<<std::endl;
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return 1;
        }
        curl_easy_setopt(curl,CURLOPT_URL,"http://www.example.com");
        curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_data);
        curl_easy_setopt(curl,CURLOPT_WRITEDATA,fp);
        res=curl_easy_perform(curl);
        if(res!=CURLE_OK){
            std::cerr<<"download failure"<<curl_easy_strerror(res)<<std::endl;
        }else{
            std::cout<<"download success as example.html"<<std::endl;
        }
        fp.close();
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    return 0;
}