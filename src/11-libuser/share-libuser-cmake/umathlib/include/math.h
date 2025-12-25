#pragma once
#ifdef BUILD_DLL 
#define MATH_API __declspec(dllexport)
#else
#define MATH_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

//声明函数，加上MATH_API
MATH_API int add(int a,int b);
MATH_API int sub(int a,int b);
MATH_API int mul(int a,int b);
MATH_API int del(int a,int b);

#ifdef __cplusplus
}
#endif
