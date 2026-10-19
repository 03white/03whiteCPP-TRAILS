#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <rfl/json.hpp>

// 对比 lesson05：nlohmann 要手写 to_json/from_json，或者套 NLOHMANN_DEFINE_TYPE_* 宏。
// reflect-cpp 走编译期反射（C++20 concepts），结构体字段名直接当 JSON 的 key 用，
// 不用任何宏、不用任何 to_json/from_json。代价是必须用 C++20 编译。
struct Person {
    std::string name;
    std::string address;
    int age;
};

// 字段名想跟 JSON 里的 key 不一样，用 rfl::Rename<"JSON里的名字", 类型>。
// 比如 C++ 里叫 age，但希望序列化出来是 "age_years"。
struct Human {
    std::string name;
    rfl::Rename<"age_years", int> age;
};

// 容器、optional、嵌套结构体都自动支持，不需要额外声明。
struct Team {
    std::string name;
    std::vector<Person> members;
    std::optional<std::string> slogan;
};

int main() {
    // 1. 序列化：结构体 --> JSON 字符串
    const Person p{"Ned Flanders", "744 Evergreen Terrace", 60};
    const std::string j = rfl::json::write(p);
    std::cout << "write:  " << j << std::endl;

    // 第二个参数给 rfl::json::pretty，会缩进排版，方便肉眼看
    std::cout << "pretty:\n"
              << rfl::json::write(p, rfl::json::pretty) << std::endl;

    // 2. 反序列化：JSON 字符串 --> 结构体
    // read 不抛异常，返回 rfl::Result<T>：要么有值，要么带一条错误信息。
    const auto r = rfl::json::read<Person>(j);
    if (r) {
        std::cout << "read:   " << r.value().name << " / " << r.value().age
                  << std::endl;
    } else {
        std::cout << "read failed: " << r.error().what() << std::endl;
    }

    // 3. 字段改名：C++ 的 age 序列化成 "age_years"
    const Human h{"Jesu", 10000};
    std::cout << "rename: " << rfl::json::write(h) << std::endl;

    // 4. 嵌套 + vector + optional（nullopt 会写成 null）
    const Team t{"The Simpsons",
                 {p, {"Homer", "742 Evergreen Terrace", 39}},
                 std::nullopt};
    std::cout << "team:   " << rfl::json::write(t) << std::endl;

    // 5. 出错时走 Result 的错误分支，而不是抛异常
    const auto bad = rfl::json::read<Person>("{ not json }");
    if (!bad) {
        std::cout << "bad json -> " << bad.error().what() << std::endl;
    }

    return 0;
}
