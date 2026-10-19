// plugin_mul.cpp
#include "IPlugin.h"

class MulPlugin : public IPlugin {
public:
    const char* Name() const override {
        return "MulPlugin";
    }
    const char* Description() const override {
        return "乘法插件";
    }
    double Calculate(double a, double b) const override {
        return a * b;
    }
};

EXPORT_PLUGIN(MulPlugin)