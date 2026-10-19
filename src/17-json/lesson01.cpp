#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
using Json = nlohmann::ordered_json;

int main() {
    Json j = {{"name", "Alice"},
              {"age", 30},
              {"skills", {"C++", "Python"}},
              {"address", {{"city", "Beijing"}, {"zip", "100000"}}}};
    std::string name = j["name"];
    try {
        auto age = j.at("sex").get<int>();
    } catch (const Json::out_of_range& e) {
        std::cerr << "key does't exist:" << e.what() << std::endl;
    }
    int sex = j.value("sex", 0);
    if (!j.contains("skin")) {
        std::cout << "no skin" << std::endl;
    }
    if (j.is_object()) {
        std::cout << "it's object" << std::endl;
    }
    for (auto& [key, value] : j.items()) {
        std::cout << "key:" << key << "|" << "value:" << value << std::endl;
    }
    for (const auto& item : j["skills"]) {
        std::cout << item.get<std::string>() << std::endl;
    }
    return 0;
}
