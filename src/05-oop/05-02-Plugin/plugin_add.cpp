// plugin_add.cpp
#include "IPlugin.h"

class AddPlugin : public IPlugin {
public:
    const char* Name() const override {
        return "AddPlugin";
    }
    const char* Description() const override {
        return "加法插件";
    }
    double Calculate(double a, double b) const override {
        return a + b;
    }
};

EXPORT_PLUGIN(AddPlugin)