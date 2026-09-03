#include <iostream>
#include <string>

int main() {
    std::string s = "Hello, World!";

    // 提取从索引 0 开始的 5 个字符 → "Hello"
    std::string sub1 = s.substr(0, 5);
    std::cout << sub1 << std::endl;

    // 提取从索引 7 开始直到结尾 → "World!"
    std::string sub2 = s.substr(7);
    std::cout << sub2 << std::endl;

    // 提取从索引 7 开始的 3 个字符 → "Wor"
    std::string sub3 = s.substr(7, 3);
    std::cout << sub3 << std::endl;

    // len 超出剩余长度时，只提取到末尾 → "World!"
    std::string sub4 = s.substr(7, 100);
    std::cout << sub4 << std::endl;

    // pos 等于字符串长度 → 返回空字符串
    std::string sub5 = s.substr(s.size());
    std::cout << "空字符串长度: " << sub5.size() << std::endl;

    // pos 超出范围会抛出异常
    try {
        std::string sub6 = s.substr(20);
    } catch (const std::out_of_range& e) {
        std::cout << "异常: " << e.what() << std::endl;
    }

    return 0;
}