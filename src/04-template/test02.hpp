/*模板（偏）特化练习*/
/*
练习一：识别指针类型
写一个类模板 is_pointer，它有一个静态布尔常量 value，当模板参数是指针类型时为 true，否则为 false。
要求：
    用主模板 + 一个偏特化实现
    用 std::cout 打印测试结果
*/
template<typename T>
struct is_pointer{
    static const bool value=false;
};
template<typename T>
struct is_pointer<T*>{
    static const bool value=true;
};
/*
练习二：去掉类型的所有修饰（多级偏特化）
写一个类模板 remove_all_extents，用来获取数组的原始元素类型。
要求：
    处理普通类型（非数组）：直接返回该类型
    处理一维数组 T[]：返回 T
    处理有界数组 T[N]：返回 T
    递归处理多维数组：int[3][4] 应该得到 int
*/
template<typename T>
struct remove_all_extents{
    using type=T;
};
template<typename T>
struct remove_all_extents<T[]>{
    using type=typename remove_all_extents<T>::type;
};
template<typename T,size_t N>
struct remove_all_extents<T[N]>{
    using type=typename remove_all_extents<T>::type;
};
/*
练习三：判断两个类型是否相同
写一个类模板 is_same，判断两个类型是否完全一致。

要求：
    两个模板参数 T 和 U
    当 T 和 U 相同时，value = true
    不同时，value = false
*/
template<typename T,typename U>
struct is_same{
    static const bool value=false;
};
template<typename T>
struct is_same<T,T>{
    static const bool value=true;
};
/*
练习四：编译期判断类型是否可引用
写一个类模板 is_dereferenceable，判断一个类型是否可以用 * 解引用。
简化要求（用特化实现，不依赖 SFINAE）：
    只判断原生指针和智能指针 std::shared_ptr、std::unique_ptr
    其他类型返回 false
*/
#include<memory>
template<typename T>
struct is_dereferenceable{
    static const bool value=false;
};
template<typename T>
struct is_dereferenceable<T*>{
    static const bool value=true;
};
template<typename T>
struct is_dereferenceable<std::shared_ptr<T>>{
    static const bool value=true;
};
template<typename T>
struct is_dereferenceable<std::unique_ptr<T>>{
    static const bool value=true;
};
/*
练习五：类型前置（可选，检验综合理解）
C++11 标准库中有 std::remove_reference，它能把 int& 变成 int，把 int&& 也变成 int。
请自己实现一个，并测试
*/
template<typename T>
struct remove_reference{
    using type=T;
};
template<typename T>
struct remove_reference<T&>{
    using type=T;
};
template<typename T>
struct remove_reference<T&&>{
    using type=T;
};
