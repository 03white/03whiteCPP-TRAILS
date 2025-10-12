#include<iostream>
#include<memory>//包含头文件
void test(){
   //std::unique_ptr<int>ptr1=new int(100); 不支持隐式类型转换初始化
   std::unique_ptr<int>ptr1(new int(100));//cpp11
   std::unique_ptr<int>ptr2=std::make_unique<int>(1000);//cpp14

   std::cout<<"ptr1:"<<*(ptr1)<<std::endl;
   std::cout<<"ptr2:"<<*(ptr2)<<std::endl;
}