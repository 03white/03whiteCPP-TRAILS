@echo off
call "D:\SoftWare\VS\VS\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d D:\Project\03whiteCPP-TRAILS
cmake --build --preset msvc-debug --target 12-02-winsock-http
