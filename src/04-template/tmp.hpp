/*
模板元编程练习:
A:编译期计算------CPP98版
B:类型操作------稍微现代一点
*/

/**************************************A:编译期计算-----递归+模板特化版******************************************/

/*
练习零：编译期阶乘（热身）
用模板特化实现编译期计算 N!。
要求：
    不能有运行时循环或递归
    结果在编译期就确定了
    用 static constexpr 或 enum 存储结果
思路：使用非类型参数，写一个全特化1，一个普通递归模板
*/
template<size_t N>
struct Factorial{
    static const int value=N*Factorial<N-1>::value;
};
template<>
struct Factorial<1>{
    static const int value=1;
};
template<>
struct Factorial<0>{
    static const int value=1;
};
/*
练习一：编译期斐波那契数列
斐波那契数列：F(0)=0, F(1)=1, F(n)=F(n-1)+F(n-2)
思路：还是写一个普通递归非参数模板，写两个全特化非参数模板
*/
template<size_t N>
struct Fibonacci{
    static const int value=Fibonacci<N-1>::value+Fibonacci<N-2>::value;
};
template<>
struct Fibonacci<2>{
    static const int value=2;
};
template<>
struct Fibonacci<1>{
    static const int value=1;
};
/*
练习二：编译期求最大公约数
用辗转相除法（欧几里得算法）实现编译期 GCD。
*/
template<size_t a,size_t b>
struct GCD{
    static const int value=GCD<b,a%b>::value;
};
template<size_t a>
struct GCD<a,0>{
    static const int value=a;
};
/*
练习三：编译期判断素数
写一个模板，在编译期判断一个数是否为素数。
*/
// 辅助模板：从 Divisor 开始试除
template<size_t N, size_t Divisor>
struct IsPrimeHelper {
   static const bool value=(N%Divisor!=0)&&IsPrimeHelper<N,Divisor+1>::value;//如果是素数，会因为N%Divisor短路返回false
};
template<size_t N>
struct IsPrimeHelper<N,N>{
    static const bool value=true;
};
// 主模板：入口
template<size_t N>
struct IsPrime {
    static const bool value=(N>=2)&&IsPrimeHelper<N,2>::value;
};

/**************************************B:类型操作******************************************/

/*
练习四：实现enable_if
enable_if 是模板元编程的"开关"——根据条件决定是否启用某个模板。
要求：
    主模板：接受一个 bool 和一个类型 T，默认没有 type 成员
    偏特化：当 bool 为 true 时，定义 type 为 T
写完后，用这个 enable_if 实现：一个函数模板，只对整数类型启用，对浮点类型编译报错。
*/
template<bool cond,typename T=void>
struct enable_if{

};
template<typename T>
struct enable_if<true,T>{
    using type=T;
};
template<typename T>
void func(T a){
    std::cout<<"只对整数类型启用"<<endl;
}