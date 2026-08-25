#include<future>
#include<chrono>
#include<thread>
#include<string>
#include<iostream>

struct DownloadResult {
    bool success;
    std::string url;
    std::string message;
};
auto asyncDownLoadImage(const std::string&url){
    return std::async(std::launch::async,[url](){
        std::this_thread::sleep_for(std::chrono::seconds(2));
        DownloadResult result;
        result.success=true;
        result.url=url;
        result.message=url;
        std::cout<<"sub thread startting!"<<std::endl;
        return result;
    });
}
int main(){
    std::cout<<"main thread running!"<<std::endl;
    auto task=asyncDownLoadImage("http://www.example.com");
    std::cout<<"main thread is still running!"<<std::endl;
    DownloadResult result = task.get(); // 程序会阻塞在这里，直到子线程返回结果
    return 0;
}