#include<string>
#include<iostream>
#include<unordered_set>
struct Person{
    std::string name_;
    size_t age_;
    bool operator==(const Person&other)const{
        return name_==other.name_&&age_==other.age_;
    }
};
namespace std{
    template<>//全特化
    struct hash<Person>{
       size_t operator()(const Person&p)const{
            return hash<string>()(p.name_)^hash<size_t>()(p.age_);
       } 
    };
};
int main(){
    std::unordered_set<Person>hash;
    Person p1{"陈",24};
    Person p2{"符华",26};
    Person p3{"爱丽丝",15};
    hash.insert(p1);
    hash.insert(p2);
    hash.insert(p3);
    for(auto it:hash){
        std::cout<<it.name_<<":"<<it.age_<<std::endl;
    } 
    return 0;
}
