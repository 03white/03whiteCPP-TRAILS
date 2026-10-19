#include <iostream>
#include <fstream>
#include <vector>
#include <nlohmann/json.hpp>
using Json = nlohmann::ordered_json;
int main() {
    // 修改json值
    //1.数组操作
    Json test;                 //null
    test.push_back(1);         //[1]
    test.emplace_back(2);      //[1,2]
    test.emplace_back(3);      //[1,2,3]
    test += 4;                 //[1,2,3,4]
    //任意位置插入
    test.insert(test.begin(), 0);//[0,1,2,3,4]
    //删除
    test.erase(test.begin());
    try {
        test.erase("key");
    } catch (const std::exception& e) { 
        std::cerr << e.what() << std::endl; 
    }
    std::cout << test << std::endl;
    //2.对象操作
    Json object;
    object["name"] = "YiXuan";
    object["name"] = "Xing";
    object["age"] = 100;
    auto [it, insert] = object.emplace("sex", "female");
    std::cout << object << std::endl;
    //合并对象
    Json o1 = {{"color", "red"}, {"price", 17.99}};
    Json o2 = {{"color", "blue"}, {"speed", 100}};
    std::cout << "old o1:" << o1 << std::endl;
    std::cout << "old o2:" << o2 << std::endl;
    o1.update(o2,true);
    std::cout << "new o1" << o1 << std::endl;
    std::cout << "new o2" << o2 << std::endl;
    
    return 0;
}