
#include<string>
#include<iostream>
#include<functional>
#include<thread>
struct Result{
    bool success_;
    std::string url_;
    std::string msg_;
};
using CallBack=std::function<void(const Result&)>;
void asyncDownLoadImage(const std::string& url,CallBack callback){
    std::thread([url,callback](){
        Result result;
        result.success_=true;
        result.msg_="succes";
        result.url_=url;
        callback(result);
    }).detach();
}
int main(){
    std::cout<<"main thread running..."<<std::endl;
    asyncDownLoadImage("http://www.ayncExample.com",[](const Result&result){
        if(result.success_){
            std::cout<<"download success!"<<std::endl;
        }else{
            std::cout<<"download false"<<std::endl;
        }
    });
    std::cout<<"main thread still running"<<std::endl;
    return 0;
}