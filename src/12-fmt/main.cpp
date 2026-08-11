#include <fmt/core.h>
#include <fmt/color.h>

int main() {
    std::string name = "World";
    int answer = 42;
    
    // 基础用法
    fmt::print("Hello, {}!\n", name);
    
    // 多个参数
    fmt::print("The answer is {} and the name is {}\n", answer, name);
    
    // 带颜色的输出（fmt特色）
    fmt::print(fg(fmt::color::green), "Success!\n");
    
    // 带背景色
    fmt::print(fg(fmt::color::steel_blue) | bg(fmt::color::yellow), 
               "Warning!\n");
}