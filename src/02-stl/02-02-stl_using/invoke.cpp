#include <functional>
#include <iostream>
#include <memory>

struct Foo {
    int x = 10;
    void bar(int n) const {
        std::cout << x + n << '\n';
    }
};

int add(int a, int b) {
    return a + b;
}

int main() {
    Foo f;

    // 成员函数：对象
    std::invoke(&Foo::bar, f, 5);

    // 成员函数：指针
    std::invoke(&Foo::bar, &f, 5);

    // 成员函数：reference_wrapper
    std::invoke(&Foo::bar, std::ref(f), 5);

    // 成员对象：返回引用
    std::invoke(&Foo::x, f) = 42;
    std::cout << std::invoke(&Foo::x, f) << '\n';

    // 智能指针
    auto p = std::make_unique<Foo>();
    std::invoke(&Foo::bar, p, 5);
    std::invoke(&Foo::x, p) = 100;

    // 普通函数
    std::cout << std::invoke(add, 1, 2) << '\n';

    // lambda
    std::cout << std::invoke([](int x) { return x * x; }, 4) << '\n';
}