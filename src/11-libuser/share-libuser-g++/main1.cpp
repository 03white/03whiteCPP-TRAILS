#include <windows.h>
#include <iostream>
using AddFunc=int(*) (int,int);
int main(){
    HMODULE dll = LoadLibrary("libmath.dll");  // 手动加载DLL
    if (!dll) {
        std::cerr << "Failed to load DLL" << std::endl;
        return 1;
    }
    
    AddFunc add = (AddFunc)GetProcAddress(dll, "add");  // 手动获取函数
    if (!add) {
        std::cerr << "Failed to get function" << std::endl;
        FreeLibrary(dll);
        return 1;
    }
    
    int result = add(3, 4);
    std::cout << "3 + 4 = " << result << std::endl;
    FreeLibrary(dll);  // 手动卸载DLL
}