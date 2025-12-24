#pragma once
#include<iostream>
#include<thread>
#include<memory>
#include<mutex>
std::mutex mtx;
class Task{
public:
      Task(){
        std::lock_guard<std::mutex>lck(mtx);
        std::cout<<"construct function"<<std::endl;
    }
      ~Task(){
        std::lock_guard<std::mutex>lck(mtx);
        std::cout<<"destory function"<<std::endl;
    }
      void execute(){
        std::lock_guard<std::mutex>lck(mtx);
        std::cout<<"execute task"<<std::endl;
      }
};
void worker(std::shared_ptr<Task>ptr){
    ptr->execute();
}
void test(){
    int count1(0);
    int count2(0);
    std::shared_ptr<Task> ptr=std::make_shared<Task>();
    std::thread t1(worker,ptr);
    std::thread t2(worker,ptr);
    count1=ptr.use_count();
    t1.join();
    t2.join();
    count2=ptr.use_count();
    std::cout<<"count1:"<<count1<<std::endl<<"count2:"<<count2<<std::endl;
    return;
}