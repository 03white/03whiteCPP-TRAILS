#include<iostream>
class Parent{
public:
  virtual void function()const{
    std::cout<<"the parent's virtual function is exec"<<std::endl;
  }
};
class Child:public Parent{
public:
  void function()const{
    std::cout<<"the Child's function is exec"<<std::endl;
    Parent::function();
  }
};
void doTest(){
    Child c;
    c.function();
}