#include <iostream>
#include <fstream>
#include <vector>
#include <nlohmann/json.hpp>
using Json = nlohmann::ordered_json;
int main() {
    // 解析(string/json文件-->json对象)
    // 方式1:parse
    std::string test = R"({"name":"John","age":30,"skills":["C++","Python"]})";
    Json test_j_1 = Json::parse(test);
    std::cout << test_j_1 << std::endl;
    // 方式2:字面量后缀
    using namespace nlohmann::literals;
    Json test_j_2 = R"({"name":"gugugaga"})"_json;
    std::cout << test_j_2 << std::endl;
    // 方式3:从文件流解析
    std::ifstream file("test_3.json");
    Json test_j_3;
    file >> test_j_3;
    std::cout << test_j_3 << std::endl;
    // 序列化(json对象-->字节流？)
    Json test_j_4 = {{"one", 1}, {"two", 2}};
    std::string test_s_4 = test_j_4.dump();               // 紧凑输出,没有空格
    std::string test_s_4_pretty = test_j_4.dump(4);       // 缩进美化
    std::string test_s_4_tabbed = test_j_4.dump(1, '\t'); // 缩进制表符
    // 直接输出到流（文件里）
    std::ofstream f("test_4.json");
    f << std::setw(4) << test_j_4 << std::endl;
    std::cout << test_s_4 << std::endl;
    std::cout << test_s_4_pretty << std::endl;
    std::cout << test_s_4_tabbed << std::endl;
    // 与STL标准库转换
    // json-->std::vector
    Json test_j_5 = {
        {"list",
         {"a", "b", "c", "d", "e"}}}; // 创建的时候，怎么才算是一个数组？
    auto vec = test_j_5["list"].get<std::vector<std::string>>();
    for (const auto& it : vec) {
        std::cout << it << std::endl;
    }
    // json-->std::map
    Json test_j_6 = {{"number", 20000000}};
    auto map = test_j_6.get<std::map<std::string, int>>();
    for (const auto& [key, value] : map) {
        std::cout << "key:" << key << "||" << value << std::endl;
    }
    return 0;
}