#include<iostream>
using namespace std;
class Object{
public:
    Object(string name):name_(name){
        cout<<"construct tor"<<endl;
    }
    Object(const Object&other):name_(other.name_){
        cout<<"copy constructor"<<endl;
    }
    ~Object(){
        cout<<"destror"<<endl;
    }
    void print()const{
        cout<<name_<<endl;
    }
private:
    string name_;
};
template<typename T>
void print(T p){
    p.print();
}
int main(){
    Object obj1("styla");
    Object obj2(obj1);
    print<Object>(obj1);
    return 0;
}