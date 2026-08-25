#include "CRTP.hpp"
int main(){
    Circle c(5);
    Rectangle r(3,4);
    process<Circle>(c);
    process<Rectangle>(r);
}