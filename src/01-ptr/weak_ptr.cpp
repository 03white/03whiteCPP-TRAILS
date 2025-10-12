#include<iostream>
#include<memory>
#include<vector>

class Observer;
class Subject{
public:
   void addObserver(std::shared_ptr<Observer>obs);
   void notify();
private:
   std::vector<std::weak_ptr<Observer>>observers;//相比于直接存储值对象，存储指针的好处更多
};

class Observer{
public:
    static std::shared_ptr<Observer> create(std::shared_ptr<Subject>subj){
        //静态成员函数不属于对象模型,可以不实例化任何对象就可以调用，是工厂方法
        std::shared_ptr<Observer>observer=std::shared_ptr<Observer>(new Observer(subj));
        subj->addObserver(observer);
        return observer; 
    }
    void update(){
        std::cout<<"update observer survive"<<std::endl;
    }   
private:
    std::shared_ptr<Subject>subject;
    Observer(std::shared_ptr<Subject>subj):subject(subj){ 
    }
};

void Subject::addObserver(std::shared_ptr<Observer>obs){
    observers.push_back(obs);
}

void Subject::notify(){
        for(auto obs:observers){
            if(auto self=obs.lock()){
                self->update();
            }else{
                std::cout<<" one observer is die"<<std::endl;
            }
        }
}
int main(){
    auto subj=std::make_shared<Subject>();
    auto obs1=Observer::create(subj);
    {
        auto obs2=Observer::create(subj);
    }
    subj->notify();
    return 0;
}