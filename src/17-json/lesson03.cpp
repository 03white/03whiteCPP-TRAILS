#include <iostream>
#include <fstream>
#include <vector>
#include <nlohmann/json.hpp>
using Json = nlohmann::ordered_json;
int main() {
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