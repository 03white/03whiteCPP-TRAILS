# CRTP - 奇异递归模板模式

## 什么是 CRTP？

CRTP（Curiously Recurring Template Pattern，奇异递归模板模式）是 C++ 中一种重要的模板编程技巧，由 James Coplien 在 1995 年提出。

**核心特征**：派生类将自己作为模板参数传递给基类。

```cpp
template<typename Derived>
class Base {
    // 基类可以通过 static_cast<Derived*>(this) 访问派生类
};

class Derived : public Base<Derived> {
    // 派生类继承自以自己为模板参数的基类
};
```

这种"递归"体现在：派生类的定义依赖基类，而基类的实例化又依赖派生类的类型。

## 为什么需要 CRTP？

### 1. **静态多态（编译期多态）**

传统的虚函数实现运行时多态会带来：
- 虚函数表（vtable）的内存开销
- 虚函数调用的间接跳转开销
- 无法内联优化

CRTP 通过模板在**编译期**实现多态，零运行时开销。

### 2. **为派生类添加通用功能**

基类可以实现通用算法，通过 CRTP 调用派生类的特定实现，无需虚函数。

### 3. **对象计数等元编程技巧**

可以在基类中实现对每个派生类的实例计数等功能。

## CRTP 的典型用法

### 用法 1：静态多态

```cpp
// 基类定义通用接口
template<typename Derived>
class Shape {
public:
    double area() const {
        // 编译期调用派生类的实现
        return static_cast<const Derived*>(this)->area_impl();
    }
    
    void draw() const {
        static_cast<const Derived*>(this)->draw_impl();
    }
};

// 派生类实现具体逻辑
class Circle : public Shape<Circle> {
    double radius;
public:
    Circle(double r) : radius(r) {}
    
    double area_impl() const {
        return 3.14159 * radius * radius;
    }
    
    void draw_impl() const {
        std::cout << "Drawing circle\n";
    }
};

class Rectangle : public Shape<Rectangle> {
    double width, height;
public:
    Rectangle(double w, double h) : width(w), height(h) {}
    
    double area_impl() const {
        return width * height;
    }
    
    void draw_impl() const {
        std::cout << "Drawing rectangle\n";
    }
};

// 使用模板函数实现多态
template<typename Derived>
void process(Shape<Derived>& shape) {
    std::cout << "Area: " << shape.area() << "\n";
    shape.draw();
}
```

**优势**：
- 编译期确定调用目标，可以内联优化
- 无虚函数表开销
- 类型安全

**劣势**：
- 无法将不同派生类对象存储在同一容器中（无公共基类指针）
- 无法运行时选择具体类型

### 用法 2：为派生类添加通用功能

```cpp
template<typename Derived>
class Comparable {
public:
    bool operator!=(const Derived& other) const {
        return !static_cast<const Derived*>(this)->operator==(other);
    }
    
    bool operator>(const Derived& other) const {
        return other < static_cast<const Derived&>(*this);
    }
    
    bool operator<=(const Derived& other) const {
        return !(static_cast<const Derived&>(*this) > other);
    }
    
    bool operator>=(const Derived& other) const {
        return !(static_cast<const Derived&>(*this) < other);
    }
};

class MyInt : public Comparable<MyInt> {
    int value;
public:
    MyInt(int v) : value(v) {}
    
    // 只需实现 < 和 ==，其他比较运算符自动生成
    bool operator<(const MyInt& other) const {
        return value < other.value;
    }
    
    bool operator==(const MyInt& other) const {
        return value == other.value;
    }
};
```

### 用法 3：对象计数

```cpp
template<typename Derived>
class Counter {
    inline static size_t count = 0;  // C++17 inline static
    
protected:
    Counter() { ++count; }
    Counter(const Counter&) { ++count; }
    Counter(Counter&&) { ++count; }
    ~Counter() { --count; }
    
public:
    static size_t getCount() { return count; }
};

class Dog : public Counter<Dog> {};
class Cat : public Counter<Cat> {};

// 每个类型有独立的计数
Dog d1, d2;
Cat c1;
std::cout << Dog::getCount() << "\n";  // 输出 2
std::cout << Cat::getCount() << "\n";  // 输出 1
```

**关键点**：每个不同的 `Derived` 类型会实例化出不同的 `Counter<Derived>` 基类，因此每个派生类有独立的静态成员。

### 用法 4：Mixin 模式

```cpp
template<typename Derived>
class Printable {
public:
    void print() const {
        std::cout << static_cast<const Derived*>(this)->toString() << "\n";
    }
};

template<typename Derived>
class Serializable {
public:
    std::string serialize() const {
        return static_cast<const Derived*>(this)->toJson();
    }
};

// 组合多个功能
class Person : public Printable<Person>, public Serializable<Person> {
    std::string name;
    int age;
    
public:
    Person(std::string n, int a) : name(n), age(a) {}
    
    std::string toString() const {
        return name + " (" + std::to_string(age) + ")";
    }
    
    std::string toJson() const {
        return R"({"name":")" + name + R"(","age":)" + std::to_string(age) + "}";
    }
};
```

## CRTP vs 虚函数

| 特性 | CRTP | 虚函数 |
|------|------|--------|
| 多态时机 | 编译期（静态多态） | 运行期（动态多态） |
| 性能开销 | 无（可内联） | 有（vtable 查找） |
| 内存开销 | 无额外开销 | 每个对象有 vptr |
| 类型擦除 | 不支持（每个类型不同） | 支持（统一基类指针） |
| 容器存储 | 需要 `std::variant` 等 | 可用基类指针 |
| 使用场景 | 性能敏感、编译期确定类型 | 需要运行时多态 |

## CRTP 的注意事项

### 1. 派生类必须实现基类期望的接口

```cpp
template<typename Derived>
class Interface {
public:
    void foo() {
        static_cast<Derived*>(this)->foo_impl();
    }
};

class Bad : public Interface<Bad> {
    // 编译错误：没有 foo_impl()
};
```

**解决方案**：使用 `concept`（C++20）或 `static_assert` 检查接口完整性。

### 2. 派生类不完整类型问题

```cpp
template<typename Derived>
class Base {
public:
    void foo() {
        // 此时 Derived 还不是完整类型
        // 不能使用 sizeof(Derived) 或访问其成员
        static_cast<Derived*>(this)->bar();  // OK，只是指针转换
    }
};
```

在基类定义时，派生类还未完全定义，因此：
- ✅ 可以使用派生类的指针/引用
- ❌ 不能使用 `sizeof`、访问成员变量
- ✅ 可以调用派生类的成员函数（函数体在派生类完整后才实例化）

### 3. 访问控制

```cpp
template<typename Derived>
class Base {
    void helper() {
        static_cast<Derived*>(this)->private_method();  // 可能无法访问
    }
};

class Derived : public Base<Derived> {
    friend class Base<Derived>;  // 需要声明友元
    void private_method() {}
};
```

### 4. 多重继承与菱形继承

```cpp
template<typename Derived>
class Base1 { /*...*/ };

template<typename Derived>
class Base2 { /*...*/ };

class Derived : public Base1<Derived>, public Base2<Derived> {
    // 两个基类都使用 CRTP，互不干扰
};
```

## 实际应用案例

### 1. **标准库中的应用**

- `std::enable_shared_from_this<T>` - 允许对象获取指向自己的 `shared_ptr`

```cpp
class MyClass : public std::enable_shared_from_this<MyClass> {
public:
    std::shared_ptr<MyClass> getPtr() {
        return shared_from_this();  // CRTP 提供的功能
    }
};
```

### 2. **表达式模板（Expression Templates）**

用于优化数学库的性能：

```cpp
template<typename E>
class VecExpression {
public:
    double operator[](size_t i) const {
        return static_cast<const E&>(*this)[i];
    }
    
    size_t size() const {
        return static_cast<const E&>(*this).size();
    }
};

class Vec : public VecExpression<Vec> {
    std::vector<double> data;
public:
    double operator[](size_t i) const { return data[i]; }
    size_t size() const { return data.size(); }
};

// 表达式类型延迟求值
template<typename E1, typename E2>
class VecSum : public VecExpression<VecSum<E1, E2>> {
    const E1& lhs;
    const E2& rhs;
public:
    VecSum(const E1& l, const E2& r) : lhs(l), rhs(r) {}
    
    double operator[](size_t i) const {
        return lhs[i] + rhs[i];
    }
    
    size_t size() const { return lhs.size(); }
};
```

### 3. **策略模式的静态版本**

```cpp
template<typename Derived>
class Algorithm {
public:
    void execute() {
        auto& self = static_cast<Derived&>(*this);
        self.step1();
        self.step2();
        self.step3();
    }
};

class ConcreteAlgorithm : public Algorithm<ConcreteAlgorithm> {
public:
    void step1() { /* ... */ }
    void step2() { /* ... */ }
    void step3() { /* ... */ }
};
```

## 总结

CRTP 是 C++ 中实现**零成本抽象**的重要技术：

**适用场景**：
- 需要静态多态，编译期确定类型
- 性能敏感代码（游戏引擎、数值计算）
- 为派生类批量添加功能（mixin）
- 实现编译期接口检查

**不适用场景**：
- 需要运行时多态（类型擦除）
- 需要将不同类型对象存储在同一容器
- 接口经常变化（虚函数更灵活）

**核心思想**：通过模板和编译期类型推导，将运行时的动态分发转换为编译期的静态分发，在保持抽象性的同时获得最佳性能。

## CRTP 的实例化顺序

这是理解 CRTP 为什么能工作的关键，需要区分两个不同的概念：

- **模板实例化**：编译器从模板生成具体类/函数代码的过程（编译期）
- **对象实例化**：程序运行时创建对象、调用构造函数的过程（运行期）

### 模板实例化顺序

以下面代码为例：

```cpp
template<typename Derived>
class Base {
public:
    void interface() {
        static_cast<Derived*>(this)->implementation();  // ← 这里如何工作？
    }
};

class Derived : public Base<Derived> {
public:
    void implementation() {
        std::cout << "Derived::implementation\n";
    }
};
```

编译器处理这段代码的顺序：

**第 1 步：编译器遇到 `class Derived : public Base<Derived>`**

```
Derived 的声明出现
此时 Derived 是「不完整类型」（incomplete type）
——只知道它存在，不知道它有哪些成员
```

**第 2 步：实例化 `Base<Derived>` 的类定义**

```
编译器需要知道基类的大小和布局，所以必须先实例化 Base<Derived>
用 Derived 替换模板参数 T，生成 Base<Derived> 的类定义
——但仅生成「成员声明」，函数体暂不处理
```

此时 `interface()` 的函数体 `static_cast<Derived*>(this)->implementation()` **尚未实例化**，所以编译器不会检查 `Derived` 是否有 `implementation()`，也不会因为 `Derived` 不完整而报错。

**第 3 步：处理 `Derived` 的类体**

```
Derived 的成员被全部处理，Derived 成为「完整类型」（complete type）
此时编译器知道 Derived 有 implementation() 这个成员函数
```

**第 4 步：调用触发函数体实例化**

```cpp
Derived d;
d.interface();  // ← 到这里，Base<Derived>::interface() 的函数体才被实例化
```

函数体实例化时，`Derived` 已经是完整类型，`static_cast<Derived*>(this)->implementation()` 完全合法。

**这就是 CRTP 能工作的根本原因**：C++ 对成员函数体采用**惰性实例化**（lazy instantiation），函数体只在被调用时才实例化，此时派生类已经完整。

---

### 不完整类型的限制

正因为在基类模板定义阶段 `Derived` 是不完整类型，基类的**类定义**（非函数体）中对 `Derived` 的使用有限制：

```cpp
template<typename Derived>
class Base {
    // ❌ 编译错误：类定义阶段 Derived 不完整，sizeof 需要完整类型
    char buf[sizeof(Derived)];

    // ❌ 编译错误：不能声明 Derived 类型的成员变量（需要知道大小）
    Derived member;

    // ✅ 合法：指针/引用不需要知道完整类型的大小
    Derived* ptr;

    // ✅ 合法：函数体是惰性实例化的，等到调用时 Derived 已完整
    void foo() {
        sizeof(Derived);                              // OK（函数体内）
        static_cast<Derived*>(this)->bar();           // OK
        Derived local;                                // OK
    }
};
```

---

### 对象实例化顺序（运行时）

对象构造顺序遵循标准 C++ 规则：**先基类，后派生类**。

```cpp
template<typename Derived>
class Base {
public:
    Base()  { std::cout << "Base ctor\n";  }
    ~Base() { std::cout << "Base dtor\n";  }
};

class Derived : public Base<Derived> {
public:
    Derived()  { std::cout << "Derived ctor\n";  }
    ~Derived() { std::cout << "Derived dtor\n";  }
};

Derived d;
```

输出：

```
Base ctor       ← 基类先构造
Derived ctor    ← 派生类后构造
Derived dtor    ← 析构顺序相反
Base dtor
```

**重要推论**：在 `Base` 的构造函数中，`Derived` 的部分尚未初始化，因此**绝对不能在基类构造函数里调用派生类的虚函数或 CRTP 转发函数**：

```cpp
template<typename Derived>
class Base {
public:
    Base() {
        // ❌ 危险！此时 Derived 对象尚未构造，行为未定义
        static_cast<Derived*>(this)->implementation();
    }
};
```

这和虚函数在构造函数中失效是同一个原因：对象还没完整，不能向下转型。

---

### 完整时序总结

```
编译期（模板实例化）
─────────────────────────────────────────────────────
1. 编译器看到 class Derived : public Base<Derived>
   └─ Derived 为不完整类型
2. 实例化 Base<Derived> 的类定义（仅成员声明，函数体跳过）
3. 处理 Derived 的类体 → Derived 变为完整类型
4. 当某处调用 Base<Derived>::interface() 时
   └─ 函数体被实例化，此时 Derived 完整，调用合法

运行期（对象构造）
─────────────────────────────────────────────────────
1. Base<Derived> 构造函数执行
2. Derived 构造函数执行
   └─ 此后对象完整，可安全使用 CRTP 转发
3. 析构顺序相反：先 Derived，后 Base<Derived>
```
