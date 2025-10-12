#ifndef MOVE_H_
#define MOVE_H_
#include<iostream>
namespace ll{
namespace utility{
class A{
public:
   A()=default;
   A(int data){
     data_=new int(data);
   }
   A(const A&other){
    data_=new int(*other.data_);
   }
   A& operator=(const A&other){
      if(data_!=nullptr){
        delete data_;
        data_=nullptr;
      }
      data_=new int(*other.data_);
   }

   //移动构造
   A(A&&other)noexcept:data_(other.data_){
    other.data_=nullptr;
   }
   //移动赋值
   A& operator=(A&&other){
     data_=other.data_;
     other.data_=nullptr;
   }
   ~A(){
      if(data_!=nullptr){
        delete data_;
      }
      data_=nullptr;
   }
   int getData(){
    if(data_!=nullptr)
    return *data_;
    return -1;
   }
private:
   int*data_;
};
}
}
void doTest(){
    using namespace ll::utility;
    A a(20);
    std::cout<<"a="<<a.getData()<<std::endl;
    A b(a);
    std::cout<<"b="<<b.getData()<<std::endl;
    A c=std::move(a);//使用移动复制
    std::cout<<"c="<<c.getData()<<std::endl;
    std::cout<<"a="<<a.getData()<<std::endl;
}
#endif