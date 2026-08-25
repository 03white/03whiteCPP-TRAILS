#include<mutex>
#include<conio.h>
#include<iostream>
#include<windows.h>
#include<vector>
#include<string>
#include<functional>
class EventLoop{
private:
    using CallBack=std::function<void(void)>;
    bool looping_;
    std::vector<CallBack>callbacks_;
public:
    EventLoop():looping_(false){
        
    }
    void loop(){
        looping_=true;
        while(looping_){
            int key=_getch();
            if(key==13||key==10){
                for(auto it:callbacks_){
                    it();
                }
           }else if(key==27){
                looping_=false;
           }
        }
    }    
    void addCallback(CallBack callback){
        callbacks_.push_back(std::move(callback));
    }
};
class EventCallback{
private:
    EventLoop*loop_;
    void MyCallback(const std::string&data){
        std::cout<<"enter:num="<<data<<std::endl;
    }
public:
    EventCallback(EventLoop*loop,int num):loop_(loop){
        loop_->addCallback(std::bind(&EventCallback::MyCallback,this,"jjjjj"));//设置绑定回调
    }
};


int main(){
    EventLoop loop;
    EventCallback(&loop,100);
    loop.loop();
    return 0;
}







