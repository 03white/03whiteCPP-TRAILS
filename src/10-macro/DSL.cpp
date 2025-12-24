#include<iostream>
#define ADD(_NUM1_,_NUM2_,_T_)\
template<typename _T_> \
auto add(_T_ _NUM1_, _T_ NUM2_)->decltype(_NUM1_) \
{                               \
    return _NUM1_+_NUM2_;       \
};                               \
ADD(a,b,int)

int main(){
    std::cout<<add(1,2,int)<<std::endl;
    return 0;
}
