#include<iostream>
//非类型参数
template<typename T,int size>
struct CArray{
    T array[size];
    T& operator[](int index){
        return array[index];
    }
};


/*
变参模板参数的处理：
递归+折叠表达式
*/
template<typename... Args>
auto sumLeftFold(const Args&... args)->decltype((args+...)){
    return (args+...);
}

template<typename... Args>
auto sumRightFold(const Args&... args)->decltype((...+args)){
    return (...+args);
} 

template<typename... Args>
auto multiRightFold(const Args&... args)->decltype((...*args)){
    return (...*args);
}

template<typename... Args>
void print(const Args&... args){
   ( std::cout<<...<<args);
}