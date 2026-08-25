#include<iostream>
#include<memory>
#include<vector>
/*
在观察者模式中，使用weak_ptr来解决shared_ptr的循环引用问题
*/
class AbstractObserver;
class Subject{
public:
   void addObserver(std::shared_ptr<AbstractObserver>obs);
   void notify();
private:
   std::vector<std::weak_ptr<AbstractObserver>>observers;//相比于直接存储值对象，存储指针的好处更多
};

class AbstractObserver{
public:
    static std::shared_ptr<AbstractObserver> create(std::shared_ptr<Subject>subj){
        //静态成员函数不属于对象模型,可以不实例化任何对象就可以调用，是工厂方法
        std::shared_ptr<AbstractObserver>observer=std::shared_ptr<AbstractObserver>(new AbstractObserver(subj));
        subj->addObserver(observer);
        return observer; 
    }
    virtual ~AbstractObserver(){}
    virtual void update(){
        std::cout<<"update observer survive"<<std::endl;
    }   
private:
    std::shared_ptr<Subject>subject;
protected:
    AbstractObserver(std::shared_ptr<Subject>subj):subject(subj){ 
    }
};

void Subject::addObserver(std::shared_ptr<AbstractObserver>obs){
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
class ObserverImpl_1:public AbstractObserver{
public:
    virtual void update()override{
        std::cout<<"update observerImpl_1 survive"<<std::endl;
    }
    ObserverImpl_1() : AbstractObserver() {
    }
};
int main(){
    auto subj=std::make_shared<Subject>();
    auto obs1=AbstractObserver::create(subj);
    std::shared_ptr<AbstractObserver> obs2=std::make_shared<ObserverImpl_1>();
    subj->addObserver(obs2);
    {
        auto obs2=AbstractObserver::create(subj);
    }
    subj->notify();
    return 0;
}