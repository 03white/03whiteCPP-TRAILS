// 阶段 1：Winsock 对照实验
// 目标：用裸 socket 发一次 HTTP GET，把原始响应字节原样打出来，然后手写解析。
// 详见 ../downloader-roadmap.md 阶段 1。
//
// 这一份是骨架，只保证能编能跑。实现自己填。

// 头文件顺序是有讲究的：winsock2.h 必须在 windows.h 之前。
// windows.h 会自动 include winsock.h（Winsock 1），和 winsock2.h 里的
// 同名结构体/宏大面积冲突，报一屏 C2011 redefinition。
// CMakeLists 里定义的 WIN32_LEAN_AND_MEAN 是第二道保险。
#include <winsock2.h>
#include <ws2tcpip.h>  // getaddrinfo / addrinfo，必须在 winsock2.h 之后

#include <cstdio>
#include <string>
#include<algorithm>   // std::min
#include<stdexcept>   // std::runtime_error
#include<memory>
#include<iostream>
#include<string>
#include<map>
#include<sstream>
#include<cctype>
#include "ChunkDecoder.h"
// 对WSAStartup/WSACleanup进行RAII封装
class WSAGuard {
public:
    WSAGuard() {
        WSADATA data_{};
        int ret = WSAStartup(MAKEWORD(2, 2), &data_);
        if(ret!=0){
            throw std::runtime_error("WSAStartup failed: "
                                     + std::to_string(ret));
        }
    }
    ~WSAGuard(){
        WSACleanup();
    }
    //防止多个对象重复调用 WSACleanup，破坏 Winsock 的引用计数，确保资源所有权清晰。
    WSAGuard(const WSAGuard&)=delete;
    WSAGuard& operator=(const WSAGuard&)=delete;
private:
    bool start_{false};
};
struct AddrinfoDeleter{
    void operator()(addrinfo*p)const{
        if(p!=nullptr)freeaddrinfo(p);
    }
};
class SockGuard{
public:
    SockGuard():sock_(INVALID_SOCKET){}
    SockGuard(SOCKET sock):sock_(sock){
        if(sock_==INVALID_SOCKET){
            throw std::runtime_error("socket is invalid!");
        }
    };
    ~SockGuard(){
        if(sock_!=INVALID_SOCKET){
            closesocket(sock_);
        }
    }
    //禁止拷贝
    SockGuard(const SockGuard&)=delete;
    SockGuard& operator=(const SockGuard&)=delete;
    //赋予移动语义
    SockGuard(SockGuard&&other)noexcept{
        sock_=other.sock_;
        other.sock_=INVALID_SOCKET;
    }
    SockGuard& operator=(SockGuard&&other){
        if(this!=&other){
            if(sock_!=INVALID_SOCKET)closesocket(sock_);
            sock_=other.sock_;
            other.sock_=INVALID_SOCKET;
        }
        return *this;
    }
    SOCKET get()const{return sock_;}
    bool valid()const{return sock_!=INVALID_SOCKET;}
private:
    SOCKET sock_;
};
using AddrinfoPtr=std::unique_ptr<addrinfo,AddrinfoDeleter>;
// 发送完整数据，成功返回发送的总字节数，失败返回 -1
int send_all(SOCKET sock,const char*buf,int len){
    int total_sent=0;
    while(total_sent<len){
        int n=send(sock,buf+total_sent,len-total_sent,0);
        if(n==SOCKET_ERROR){
            return -1;
        }
        if(n==0){
            return -1;
        }
        total_sent+=n;
    }
    return total_sent;
}
//如果事先知道需要接收多少字节（如通过 Content-Length 头），可以循环直到收满指定长度
bool recv_fixed(SOCKET sock, char* buf, int len);
//发送一个请求后，一直接受服务端数据直到服务端主动关闭链接close(fd)
std::string recv_all(SOCKET sock){
    std::string data;
    char buf[4096];
    while(true){
        int n=recv(sock,buf,sizeof(buf),0);
        if(n>0){
            data.append(buf,n);
        }else if(n==0){
            break;
        }else{
            break;
        }
    }
    return data;
}
void func_TODO_56(SOCKET sock){
    const std::string response=recv_all(sock); 
    auto pos=response.find("\r\n\r\n");
    std::string header=response.substr(0,pos);
    std::string body=response.substr(pos+4);
    std::cout<<"==========Header==========="<<std::endl;
    std::cout<<header<<std::endl;
    std::cout<<"==========Body==========="<<std::endl;
    std::cout<<body<<std::endl;
    std::cout<<"==========over==========="<<std::endl;
    std::istringstream stream(header);
    std::string status;
    //1.读取状态行
    if(!std::getline(stream,status))return;
    //去掉尾行的'\r'
    if(!status.empty()&&status.back()=='\r')status.pop_back();
    std::map<std::string,std::string>headers;
    std::string line;
    while(std::getline(stream,line)){
        if(line=="\r"||line.empty())break;
        if(!line.empty() && line.back()=='\r')line.pop_back();
        size_t pos=line.find(':');
        if(pos!=std::string::npos){
            std::string key=line.substr(0,pos);
            std::string value=line.substr(pos+1);
            headers[key]=value;
        }
    }
    std::cout<<"=========AOP==========="<<std::endl;
    std::string version;
    int status_code;
    std::string reason;
    std::istringstream status_stream(status);
    status_stream>>version;
    status_stream>>status_code;
    std::getline(status_stream,reason);
    std::cout<<"==============Status============="<<std::endl;
    std::cout<<"version:"<<version<<std::endl;
    std::cout<<"status_code:"<<status_code<<std::endl;
    std::cout<<"reason:"<<reason<<std::endl;
    std::cout<<"==============Key-Value-Headers==========="<<std::endl;
    for(auto [key,value]:headers){
        std::cout<<key<<":"<<value<<std::endl;
    }
}
void func_TODO_7(SOCKET sock){
    std::string buffer;
    while(true){
        char temp[1024];
        int n=recv(sock,temp,sizeof(temp),0);
        if(n>0){
            buffer.append(temp,n);
        }
        // 头部结束的标志是空行，也就是 \r\n\r\n（四个字节），不是 \r\n。
        // 找 \r\n 的话状态行一读完就退出了，头部根本没收全。
        if(buffer.find("\r\n\r\n")!=std::string::npos){
            break;
        }
        if(n==0){
            throw std::runtime_error(
            "Connection closed before header complete"
            );
        }
        if (n == SOCKET_ERROR) {
            // Winsock 的错误码不进 errno，只能问 WSAGetLastError()
            throw std::runtime_error("recv failed: "
                                     + std::to_string(WSAGetLastError()));
        }
    }
    auto header_end=buffer.find("\r\n\r\n");
    std::string header=buffer.substr(0,header_end);
    std::string body=buffer.substr(header_end+4);
    std::istringstream stream(header);
    std::string status;
    //1.读取状态行
    if(!std::getline(stream,status))return;
    //去掉尾行的'\r'
    if(!status.empty()&&status.back()=='\r')status.pop_back();
    std::map<std::string,std::string>headers;
    std::string line;
    while(std::getline(stream,line)){
        if(line=="\r"||line.empty())break;
        if(!line.empty() && line.back()=='\r')line.pop_back();
        size_t pos=line.find(':');
        if(pos!=std::string::npos){
            std::string key=line.substr(0,pos);
            std::string value=line.substr(pos+1);
            headers[key]=value;
        }
    }
    // 三个坑叠在一起：
    //   1. 头名是 Content-Length（连字符），写成下划线永远查不到；
    //   2. map::operator[] 查不到会**插入**一个空串，静默给你个 0，用 find 更诚实；
    //   3. value 是 std::string，不会自动变成 size_t，得显式转。
    size_t content_length=0;
    if(auto it=headers.find("Content-Length");it!=headers.end()){
        content_length=std::stoull(it->second);  // stoull 会自己跳过冒号后的前导空格
    }
    while(body.size()<content_length){
        char temp[1024];
        size_t remain=content_length-body.size();
        // std::main 不存在，取小值的是 std::min，在 <algorithm> 里。
        // recv 第三参是 int，min 的结果是 size_t，显式收窄消掉 C4267。
        int want=static_cast<int>(std::min(remain,sizeof(temp)));
        int n=recv(sock,temp,want,0);
        if(n>0){
            body.append(temp,n);
        }else if(n==0){
            throw std::runtime_error(
            "Connection closed before body complete"
            );
        }else{
            throw std::runtime_error("recv failed: "
                                     + std::to_string(WSAGetLastError()));
        }
    }
    std::cout<<"====================Header================"<<std::endl;
    std::cout<<header<<std::endl;
    std::cout<<"====================Body=================="<<std::endl;
    std::cout<<body<<std::endl;
}
// ==================== TODO 8 的骨架 ====================
// 下面四个函数体都是 throw，编得过、链得上、跑到哪一步没实现就炸在哪一步，
// 不会给你"好像跑通了其实全是空数据"的假象。填一个删一个，随时能构建。

// 先解决一个绕不开的前提：**必须开一条新连接**。
// 请求头里写了 Connection: close，服务端发完 /range/1024 的响应就把连接关了，
// TODO 7 用的那个 sock 已经是个死 fd，不可能拿去发第二个请求。
//
// 所以把 main 里 TODO 2 + TODO 3 那一整段（getaddrinfo + 遍历 socket/connect）
// 原样搬进这个函数，main 里调两次。这不是为了好看：两次实验共用同一段连接代码，
// 才能保证观察到的差异只来自"响应体编码方式不同"，而不是别处。
//
// 注意返回类型是 SockGuard（值返回）。SockGuard 禁拷贝但有移动构造，
// 所以 `SockGuard s = connect_to(...)` 是合法的 —— 这正是你在 05/07 章写移动
// 语义的意义所在，回头对照 07-move 看一眼。
SockGuard connect_to(const std::string& host, const std::string& port) {
    // TODO 2  getaddrinfo(host, "80", &hints, &result)
    //         hints 里 ai_family = AF_UNSPEC, ai_socktype = SOCK_STREAM
    //         返回的是一个链表 —— 为什么是链表？失败时该不该换下一个再试？
    // const std::string host="httpbin.org";
    // const std::string port="80";
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;      // 允许 IPv4 或 IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP
    addrinfo* raw_result = nullptr;
    int rc=getaddrinfo(host.c_str(),port.c_str(),&hints,&raw_result);
    if(rc!=0){
        throw std::runtime_error("getaddrinfo failed: " +
                                     std::string(gai_strerror(rc)));
    }
    AddrinfoPtr result(raw_result);  // RAII 管理链表
    // TODO 3  socket() + connect()
    SockGuard sock;                  //最终成功链接的socket
    for(addrinfo*p=result.get();p!=nullptr;p=p->ai_next){
        // SOCKET 是 UINT_PTR(无符号)，失败返回的是 INVALID_SOCKET == (SOCKET)~0。
        // 别写成 == -1：这里靠的是有符号->无符号的隐式转换才碰巧成立，
        // 语义上是错的，读代码的人也看不出你在判失败。
        SOCKET sockfd=socket(p->ai_family,p->ai_socktype,p->ai_protocol);
        if(sockfd==INVALID_SOCKET){
            continue;   // 这个地址族本机不支持(比如没有 IPv6)，换下一个
        }
        SockGuard tmp(sockfd);
        // ai_addrlen 是 size_t(8字节)，connect 第三参是 int —— 显式收窄，消掉 C4267。
        if(connect(sockfd,p->ai_addr,static_cast<int>(p->ai_addrlen))==0){
            sock=std::move(tmp);
            break;
        }
        // 连不上不是终点：getaddrinfo 返回链表的意义就是"这个不行换下一个"。
        // tmp 在这里析构，closesocket 自动收掉这个连失败的 fd。
    }
    // 判定成败要在遍历完之后，不能放在循环体里 —— 否则第一个地址失败就直接抛，
    // 后面的地址根本没机会试。
    if (!sock.valid()) {
        throw std::runtime_error("Failed to connect to " + host + ":" + port);
    }
    std::cout << "Connected successfully!" << std::endl;
    return std::move(sock);
}

// 读响应头。TODO 6 和 TODO 7 里这段你已经抄了两遍，TODO 8 是第三遍 —— 该抽了。
//
// leftover_body 是唯一容易翻车的字段，也是整个 TODO 8 最经典的 bug：
//   读头的循环是 recv 进 buffer 找 \r\n\r\n，而 recv **不会**好心地在头部结束处
//   停下 —— 它给你多少全看 TCP。所以那一次 recv 极可能已经把头之后的一批 body
//   字节一起读进来了。这些字节必须原样交给 ChunkDecoder，一个都不能丢。
//
//   丢了会怎样：第一个 chunk 的长度行没了，解码器一上来就在数据中间读"长度"，
//   随机解出一个巨大的十六进制数，然后卡住等一个永远收不满的 chunk。
//   现象是"程序卡死在 recv"，看着像网络问题，其实是你自己把长度行吃掉了。
//   TODO 7 里你没被这个咬到，是因为那段代码碰巧把 leftover 留在了 body 变量里。
struct ResponseHead {
    std::string version;
    int status_code{0};
    std::string reason;
    std::map<std::string, std::string> headers;
    std::string leftover_body;   // 读头时顺带多读进来的体字节
};
ResponseHead read_head(SOCKET sock) {
    ResponseHead responseHead;
    std::string buffer;
     while (buffer.find("\r\n\r\n") == std::string::npos) {
          char temp[1024];
          int n = recv(sock, temp, sizeof(temp), 0);
          if (n > 0) {
              buffer.append(temp, n);
          } else if (n == 0) {
              throw std::runtime_error("Connection closed before header complete");
          } else {
              throw std::runtime_error("recv failed: " + std::to_string(WSAGetLastError()));
          }
    }
    auto header_end = buffer.find("\r\n\r\n");
    std::string header = buffer.substr(0, header_end);
    std::string body = buffer.substr(header_end + 4);

    std::istringstream stream(header);
    std::string status;
    if (!std::getline(stream, status)) {
        throw std::runtime_error("Invalid status line");
    }
    if (!status.empty() && status.back() == '\r') status.pop_back();

    std::map<std::string, std::string> headers;
    std::string line;
    while (std::getline(stream, line)) {
        if (line == "\r" || line.empty()) break;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t pos = line.find(':');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            // 转小写（只转 key，value 大小写敏感，但为了演示统一转）
            std::transform(key.begin(), key.end(), key.begin(),
                [](unsigned char c) -> unsigned char { return std::tolower(c); });
            // 去掉 value 前导空格（可选）
            size_t v_start = value.find_first_not_of(" \t");
            if (v_start != std::string::npos) value = value.substr(v_start);
            else value.clear();
            headers[key] = value;
        }
    }

    std::string version, reason;
    int status_code;
    std::istringstream status_stream(status);
    status_stream >> version >> status_code;
    std::getline(status_stream, reason);
    // 去掉 reason 前导空格
    if (!reason.empty() && reason.front() == ' ') reason.erase(0, 1);

    responseHead.version = version;
    responseHead.status_code = status_code;
    responseHead.reason = reason;
    responseHead.headers = headers;
    responseHead.leftover_body = body;
    return responseHead;
}

// 头名大小写不敏感（RFC 9110 §5.1）。你现在的 std::map<string,string> 是区分
// 大小写的，headers.find("Content-Length") 碰上发 "content-length" 的服务端就查不到，
// 然后你会当成"没有长度"走错分支。
// httpbin 恰好发的是规范大小写，所以 TODO 7 侥幸过了 —— 那是运气，不是正确。
// 实现思路：要么查的时候两边都转小写线性扫，要么一开始就用带大小写不敏感
// 比较器的 map。后者更好，想想为什么（提示：查一次 O(logN) vs O(N)）。
// 返回指针而不是 std::string：查不到时返回 nullptr，比返回空串诚实
// —— "头不存在"和"头的值是空串"是两件不同的事，后者在 HTTP 里合法。
const std::string* find_header(const std::map<std::string, std::string>& headers,
                               const std::string& name) {
    auto it = headers.find(name);
    if (it != headers.end()) return &it->second;
    return nullptr;
}

// 步骤：
//   1. auto head = read_head(sock);
//   2. 校验 find_header(head.headers, "transfer-encoding") 是 chunked。
//      值可能是 "chunked"，也可能是 "gzip, chunked"（chunked 必须排最后）。
//      顺便断言：chunked 响应**不该**同时带 Content-Length。两个都有时 RFC 9112
//      要求忽略 Content-Length —— 这条规则的由来就是 HTTP 请求走私
//      (request smuggling)：前后两个代理对"信哪个"判断不一致就能被注入请求。
//   3. ChunkDecoder dec; std::string body;
//      dec.feed(head.leftover_body.data(), head.leftover_body.size(), body);  <- 别漏
//   4. while (!dec.done()) { recv 一块 -> dec.feed(...) }
//      recv 返回 0 而 dec 还没 done -> 服务端在 body 说完之前断开了。
//      这是"截断的响应"，对下载器等价于文件损坏，必须抛异常，绝不能当正常结束。
//      对比一下 TODO 7：那里靠 Content-Length 判完整，这里靠 0\r\n\r\n 判完整，
//      两者都比"等对端关连接"可靠 —— 关连接区分不了"发完了"和"网断了"。
//   5. body 是 2048 字节随机二进制，别 cout << body：终端会被控制字符搞乱，
//      Windows 文本模式下 0x1A 还可能被当 EOF 截断。打字节数 + 前 32 字节 hex 就够。
//
// 验收：body.size() 必须正好等于 2048。
//       对照 curl -v --http1.1 http://httpbin.org/stream-bytes/2048 -o nul
//       —— 它的响应头里有 Transfer-Encoding: chunked 且没有 Content-Length。
void func_TODO_8(SOCKET sock) {
    auto head = read_head(sock);
    auto res = find_header(head.headers, "transfer-encoding");
    if (!res || *res != "chunked") {
        std::cout << "不是分块编码" << std::endl;
        return;
    }

    ChunkDecoder decoder;
    std::string body;
    // 先喂 leftover
    decoder.feed(head.leftover_body.data(), head.leftover_body.size(), body);

    while (!decoder.done()) {
        char data[1024];
        int len = recv(sock, data, sizeof(data), 0);
        if (len > 0) {
            decoder.feed(data, len, body);
        } else if (len == 0) {
            throw std::runtime_error("Connection closed before body complete");
        } else if (len == SOCKET_ERROR) {
            throw std::runtime_error("recv failed: " + std::to_string(WSAGetLastError()));
        }
    }

    std::cout << "Body size: " << body.size() << std::endl;
    std::cout << "First 32 bytes: ";
    for (size_t i = 0; i < std::min<size_t>(32, body.size()); ++i) {
      std::printf("%02X ", static_cast<unsigned char>(body[i]));
    }
    std::cout << std::endl;
}
int main() {
try{
    // TODO 1  WSAStartup(MAKEWORD(2, 2), &wsaData)
    //         想清楚：这个初始化该不该用 RAII 包起来？(参考 01-ptr)
    WSAGuard wsa;
    // TODO 2  getaddrinfo(host, "80", &hints, &result)
    //         hints 里 ai_family = AF_UNSPEC, ai_socktype = SOCK_STREAM
    //         返回的是一个链表 —— 为什么是链表？失败时该不该换下一个再试？
    const std::string host="httpbin.org";
    const std::string port="80";
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;      // 允许 IPv4 或 IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP
    addrinfo* raw_result = nullptr;
    int rc=getaddrinfo(host.c_str(),port.c_str(),&hints,&raw_result);
    if(rc!=0){
        throw std::runtime_error("getaddrinfo failed: " +
                                     std::string(gai_strerror(rc)));
    }
    AddrinfoPtr result(raw_result);  // RAII 管理链表
    // TODO 3  socket() + connect()
    SockGuard sock;                  //最终成功链接的socket
    for(addrinfo*p=result.get();p!=nullptr;p=p->ai_next){
        // SOCKET 是 UINT_PTR(无符号)，失败返回的是 INVALID_SOCKET == (SOCKET)~0。
        // 别写成 == -1：这里靠的是有符号->无符号的隐式转换才碰巧成立，
        // 语义上是错的，读代码的人也看不出你在判失败。
        SOCKET sockfd=socket(p->ai_family,p->ai_socktype,p->ai_protocol);
        if(sockfd==INVALID_SOCKET){
            continue;   // 这个地址族本机不支持(比如没有 IPv6)，换下一个
        }
        SockGuard tmp(sockfd);
        // ai_addrlen 是 size_t(8字节)，connect 第三参是 int —— 显式收窄，消掉 C4267。
        if(connect(sockfd,p->ai_addr,static_cast<int>(p->ai_addrlen))==0){
            sock=std::move(tmp);
            break;
        }
        // 连不上不是终点：getaddrinfo 返回链表的意义就是"这个不行换下一个"。
        // tmp 在这里析构，closesocket 自动收掉这个连失败的 fd。
    }
    // 判定成败要在遍历完之后，不能放在循环体里 —— 否则第一个地址失败就直接抛，
    // 后面的地址根本没机会试。
    if (!sock.valid()) {
        throw std::runtime_error("Failed to connect to " + host + ":" + port);
    }
    std::cout << "Connected successfully!" << std::endl;
    // TODO 4  拼请求报文并 send()
    //         GET /range/1024 HTTP/1.1\r\n
    //         Host: httpbin.org\r\n
    //         Connection: close\r\n
    //         \r\n
    //         注意：send() 可能只发出去一部分，返回值是实际发送字节数。
    //               短报文基本不会遇到，但要知道正确写法是循环发。
    const std::string request="GET /range/1024 HTTP/1.1\r\n"
                              "Host: httpbin.org\r\n"
                              "Connection:close\r\n"
                              "\r\n";
    int ret = send_all(sock.get(), request.data(), static_cast<int>(request.size()));
    if (ret < 0) {
        std::cerr << "send failed with error: " << WSAGetLastError() << std::endl;
    } else {
        std::cout << "Sent " << ret << " bytes" << std::endl;
    }
    // TODO 5  循环 recv() 到返回 0，先把原始字节原样 fwrite 到 stdout
    //         这一步就是本阶段的第一个可验证产出：
    //         和 `curl -v --http1.1 http://httpbin.org/range/1024` 的输出对比。
    //const std::string response=recv_all(sock.get()); 
    // TODO 6  解析：找到 \r\n\r\n 边界，切出状态行 + 头部键值对
    //         头名大小写不敏感；冒号后有可选空格。
    //         recv 一次可能只给你半个头 —— 必须自己缓冲，这是 TCP 字节流的本质。
    
    // TODO 7  按 Content-Length 读定长体
    func_TODO_7(sock.get());
    // TODO 8  换 http://httpbin.org/stream-bytes/2048，手写 chunked 解码
    //         <hex长度>[;扩展]\r\n <数据>\r\n ... 0\r\n \r\n
    SockGuard sock8 = connect_to(host, port);   // 新连接，理由见 connect_to 上方
    const std::string request8 = "GET /stream-bytes/2048 HTTP/1.1\r\n"
                                 "Host: " + host + "\r\n"
                                 "Connection: close\r\n"
                                 "\r\n";
    if (send_all(sock8.get(), request8.data(),
                 static_cast<int>(request8.size())) < 0) {
        throw std::runtime_error("send failed: "
                                 + std::to_string(WSAGetLastError()));
    }
    func_TODO_8(sock8.get());
    // TODO 9  清理：closesocket() / freeaddrinfo() / WSACleanup()
    //
    // 排错：Winsock 的错误码不走 errno，用 WSAGetLastError()。
    //       recv 返回 0 = 对端正常关闭，返回 SOCKET_ERROR(-1) 才是出错。
    }
    catch(const std::exception& e)
    {
        std::cerr<<"Error:"<<e.what()<<std::endl;
        return 1;
    }
    std::printf("12-02-winsock-http: skeleton\n");
    return 0;
}
