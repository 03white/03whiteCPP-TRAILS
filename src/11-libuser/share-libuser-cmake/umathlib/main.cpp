#include <iostream>
#include "math.h"

int main(){
    int a = 10, b = 5;
    std::cout << "a = " << a << ", b = " << b << std::endl;
    std::cout << "add(a, b) = " << add(a, b) << std::endl;
    std::cout << "sub(a, b) = " << sub(a, b) << std::endl;
    std::cout << "mul(a, b) = " << mul(a, b) << std::endl;
    std::cout << "del(a, b) = " << del(a, b) << std::endl;
    return 0;
}
