#include<iostream>
//非类型参数
template<typename T,int size>
struct CArray{
    T array[size];
    T& operator[](int index){
        return array[index];
    }
};
int main(){
    CArray<int,20>array;
    array[0]=1;
    std::cout<<array[0]<<std::endl;
    return 0;
}