#include<iostream>
template<typename Derived>
class Shape{
public:
    double area()const{
        return static_cast<const Derived*>(this)->area_impl();
    }
    void draw()const{
        static_cast<const Derived*>(this)->draw_impl();
    }
};
class Circle:public Shape<Circle>{
public:
    Circle(double r):radius(r){}
    double area_impl()const{return 3.14*radius*radius;}
    void draw_impl()const{std::cout<<"draw circle"<<std::endl;}
private:
    double radius;
};
class Rectangle:public Shape<Rectangle>{
public:
    Rectangle(double w,double h):width(w),height(h){}
    double area_impl()const{return width*height;}
    void draw_impl()const{std::cout<<"draw rectangle"<<std::endl;}
private:
    double width,height;
};
template<typename Derived>
void process(Shape<Derived>& shape){
    std::cout<<"Area: "<<shape.area()<<std::endl;
    shape.draw();
}
