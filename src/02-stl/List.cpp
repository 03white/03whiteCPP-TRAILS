#include<iostream>
template<typename T>
struct ListNode{
    T val_;
    ListNode<T>*next_;
    ListNode(T val=T()):val_(val),next_(nullptr){}
};
template<class T>
class List{
public:
    List(){
        head_=new ListNode<T>;
    }
    ~List(){
        clear();
        delete head_;
    }
    List(const List<T>&other){ // the copy of list 
        head_=new ListNode<T>;
        ListNode<T>*current=other.head_->next_;
        while(current){
            push_back(current->val_);
            current=current->next_;
        }
    }
    List(List<T>&&other){
        head_=new ListNode<T>;
        head_->next_=other.head_->next_;
        other.clear();
        delete other.head_;    
    }
    void push_back(const T& val){
        ListNode<T>*newNode=new ListNode<T>(val);
        ListNode<T>*current=head_;
        while(current->next_){
            current=current->next_;
        }
        current->next_=newNode;
    }
    void push_front(const T&val){
        ListNode<T>*newNode=new ListNode<T>(val);
        newNode->next_=head_->next_;
        head_->next_=newNode;
    }
    void print(){
        ListNode<T>*current=head_->next_;
        if(!current)std::cout<<"the list is empty!"<<std::endl;
        while(current){
            std::cout<<current->val_<<" ";
            current=current->next_;
        }
        std::cout<<std::endl;
    }
    List<T>& operator =(List<T>&&other){
        if(other==*this)return *this; //防止自指
        if(head_->next_){             
            clear();
            head_->next_=other.head_->next_; //将other的资源转移到this中
        }
        delete other.head_;//释放
        other.head_=nullptr;
        return *this;
    }
private:
    ListNode<T>*head_;
    void clear();
};
template<class T>
void List<T>::clear(){
    ListNode<T>*del=head_->next_;
    while(del){
        head_->next_=del->next_;
        delete del;
        del=head_->next_;
    }
    head_=nullptr;
}
int main(){
    std::cout<<"jjjjj"<<std::endl;
    return 0;
}