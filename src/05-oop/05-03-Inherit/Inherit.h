#include <iostream>
class Parent {
public:
    virtual void function() const {
        std::cout << "the parent's virtual function is exec" << std::endl;
    }
};
class Child : public Parent {
public:
    void function() const {
        std::cout << "the Child's function is exec" << std::endl;
        Parent::function();
    }
};
int main() {
    Child* child = new Child();
    Parent* parent = child;//允许的向上转型
    parent->function();     // 调用的是父类的虚函数  
    return 0;
}