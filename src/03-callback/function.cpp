#include<mutex>
#include<conio.h>
#include<iostream>
#include<windows.h>
#include<functional>
class EventLoop{
private:
    bool looping_;
    std::function<void(void)>callback_;
public:
    EventLoop():looping_(false){
        
    }
    void loop(){
        looping_=true;
        while(looping_){
            int key=_getch();
            if(key==13||key==10){
                callback_();
           }else if(key==27){
                looping_=false;
           }
        }
    }    
    void setCallback(std::function<void(void)>callback){
        callback_=callback;
    }
};
class EventCallback{
private:
    EventLoop*loop_;
    int num_;
    void MyCallback(){
        std::cout<<"enter:num="<<num_<<std::endl;
    }
public:
    EventCallback(EventLoop*loop,int num):loop_(loop),num_(num){
        loop_->setCallback(std::bind(&EventCallback::MyCallback,this));//设置绑定回调
    }
};


int main(){
    EventLoop loop;
    EventCallback(&loop,100);
    loop.loop();
    return 0;
}







