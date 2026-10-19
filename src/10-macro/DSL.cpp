#include <memory>
#define PROPERTY(_T_, _NAME)                                                   \
    const _T_& get##_NAME() const noexcept {                                   \
        return _NAME##_;                                                       \
    }                                                                          \
    _T_& get##_NAME() noexcept {                                               \
        return _NAME##_;                                                       \
    }                                                                          \
    void set##_NAME(const _T_& v) {                                            \
        _NAME##_ = v;                                                          \
    }                                                                          \
    void set##_NAME(_T_&& v) {                                                 \
        _NAME##_ = std::move(v);                                               \
    }
class Object {
public:
    PROPERTY(int, value);

private:
    int value_{100};
};
int main() {
    Object obj;
    obj.setvalue(200);
    return 0;
}