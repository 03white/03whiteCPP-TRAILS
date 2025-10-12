#include<iostream>
#include<tuple>
#include<type_traits>  // 用于编译期计算

// 可变参数模板，Args是参数包
template<typename...Args>
void myPrint(){
    // sizeof...(Args) 获取参数包中参数的数量
    std::cout<<sizeof...(Args)<<std::endl;

    // 其它常见操作举例（仅注释说明）:
    // 1. 展开参数包: (void)std::initializer_list<int>{(std::cout << typeid(Args).name() << " ", 0)...};
    // 2. 构造tuple: auto tup = std::tuple<Args...>{};
    // 3. 转发参数: template<typename...Args> void foo(Args&&... args) { bar(std::forward<Args>(args)...); }
    // 4. 折叠表达式(C++17): (std::cout << ... << args);
}

// 1. 递归方式展开参数包
template<typename T>
void print(T t) {
    std::cout << t << " ";
}

template<typename First, typename... Rest>
void print(First first, Rest... rest) {
    std::cout << first << " ";
    print(rest...);  // 递归调用
}

// 2. 使用折叠表达式(C++17)
template<typename... Args>
void printFold(Args... args) {
    (std::cout << ... << args) << std::endl;  // 折叠表达式
}

// 3. 使用初始化列表展开
template<typename... Args>
void printList(Args... args) {
    (void)std::initializer_list<int>{(std::cout << args << " ", 0)...};
    std::cout << std::endl;
}

// 1. 基础版本：处理参数包的最后一个参数
template<typename T>
void simpleShow(T value) {
    std::cout << "最后一个参数: " << value << std::endl;
}

// 2. 递归版本：每次处理第一个参数，然后递归处理剩余参数
template<typename First, typename... Rest>
void simpleShow(First first, Rest... rest) {
    std::cout << "当前参数: " << first << std::endl;
    simpleShow(rest...);  // 递归调用，参数包逐渐减少
}

// 编译期求和示例
template<typename T>
constexpr T sum(T value) {
    return value;
}

template<typename First, typename... Rest>
constexpr auto sum(First first, Rest... rest) {
    // 在编译期完成计算
    return first + sum(rest...);
}

// 编译期类型检查示例
template<typename... Args>
constexpr bool are_all_integers() {
    // 编译期展开检查每个类型
    return (std::is_integral_v<Args> && ...);
}

int main(){
    myPrint<int,double>(); // 输出2，因为有两个类型参数

    // 测试新增的函数
    print(1, 2.5, "hello", 'c');  // 递归方式
    std::cout << std::endl;

    printFold(1, 2.5, 3);  // 折叠表达式方式

    printList(10, 20, 30);  // 初始化列表方式

    // 测试简单可变参数模板
    std::cout << "=== 调用开始 ===" << std::endl;
    simpleShow(100, "hello", 3.14);
    std::cout << "=== 调用结束 ===" << std::endl;

    // 编译期计算示例
    constexpr auto result = sum(1, 2, 3, 4);  // 编译期计算结果
    static_assert(result == 10, "Compilation-time assertion");

    // 编译期类型检查
    static_assert(are_all_integers<int, short, long>(), "All types are integers");
    static_assert(!are_all_integers<int, double, char>(), "Not all types are integers");

    return 0;
}
