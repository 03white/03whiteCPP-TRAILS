#include <iostream>
#include <fstream>
#include <vector>
#include <nlohmann/json.hpp>
using Json = nlohmann::json;
struct Person {
    friend std::ostream& operator<<(std::ostream&out,const Person& p);
    std::string name;
    std::string address;
    int age;
    
};
std::ostream& operator<<(std::ostream&out,const Person& p) {
    out << "name:" << p.name << std::endl;
    out << "address:" << p.address << std::endl;
    out << "age:" << p.age << std::endl;
    return out;
}
//序列化：Person-->json
void to_json(Json& j, const Person& p) {
    j = Json{{"name", p.name}, {"address", p.address}, {"age", 100}};
}
//反序列化：json-->Person
void from_json(const Json& j, Person& p) {
    j.at("name").get_to(p.name);
    j.at("address").get_to(p.address);
    j.at("age").get_to(p.age);
}
//模板简化定义，不用写to_json/from_json
//1.非侵入式定义
struct Student {
    std::string name;
    int age;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Student, name, age);
//2.侵入式定义
struct Human {
    std::string name;
    int age;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Human, name, age)
};
int main() {
    // 自定义类型转换：类对象/结构体对象--->json对象
    Person p{"Ned Flanders", "744 Evergreen Terrace", 60};
    Json j = p;
    std::cout << j << std::endl;
    Json k = {{"name", "kkjj"}, {"address", "shuixianhua street"}, {"age", 1000}
    };
    auto q = k.get<Person>();
    std::cout << q << std::endl;
    Student s{"LiHua", 14};
    Json Li = s;
    std::cout << Li << std::endl;
    Human h{"Jesu", 10000};
    Json je = h;
    std::cout << je << std::endl;
    return 0;
}