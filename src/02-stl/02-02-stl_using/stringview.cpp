#include<string>
#include<string_view>
#include<iostream>
int main() {
    // 从字符串字面量创建 string_view
    std::string_view = "hello world";
    // 从std::string创建string_view
    std::string str = "hello world";
    std::string_view str_v= str;
    // 指定长度和起始位置创建string_view
    std::string_view str_v2(str.data(), 11);
    // 从字符数组创建string_view
    char arr[] = {'a', 'b', 'c', 'd'};
    std::string_view str_v3 = (arr, sizeof(arr));
    //默认构造为空视图
    std::string_view str_v4;


	return 0;
}