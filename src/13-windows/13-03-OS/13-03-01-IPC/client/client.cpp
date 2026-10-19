#include <windows.h>
#include <iostream>
#include <string>
#include <conio.h>

#define PIPE_NAME L"\\\\.\\pipe\\SimpleInputServer"
#define BUF_SIZE 1024

int main() {
    std::cout << "[Client] 连接服务端..." << std::endl;

    HANDLE hPipe = CreateFile(PIPE_NAME,
                              GENERIC_READ | GENERIC_WRITE,
                              0,
                              NULL,
                              OPEN_EXISTING,
                              0,
                              NULL);

    if (hPipe == INVALID_HANDLE_VALUE) {
        std::cerr << "连接失败，请先启动 Server.exe。错误码: " << GetLastError()
                  << std::endl;
        return 1;
    }

    std::cout << "[Client] 已连接。输入字符，ESC 退出。" << std::endl;
    std::cout << "Backspace = 退格，Enter = 提交" << std::endl;

    char buffer[BUF_SIZE];

    while (true) {
        int ch = _getch();
        if (ch == 27)
            break; // ESC 退出

        std::string key;
        if (ch == 8) {
            key = "BACKSPACE";
        } else if (ch == 13) {
            key = "ENTER";
        } else if (ch >= 32 && ch < 127) {
            key = std::string(1, (char)ch);
        } else {
            continue; // 忽略其他特殊键
        }

        std::cout << "[Client] 按键: " << key << std::endl;

        // 发送给服务端
        DWORD bytesWritten;
        WriteFile(hPipe, key.c_str(), (DWORD)key.length(), &bytesWritten, NULL);

        // 接收服务端返回
        DWORD bytesRead;
        if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL)
            && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            std::cout << "[Client] 服务端返回: " << buffer << std::endl;
        }
    }

    CloseHandle(hPipe);
    std::cout << "[Client] 退出。" << std::endl;
    return 0;
}