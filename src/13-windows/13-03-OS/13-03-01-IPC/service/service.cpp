#include <windows.h>
#include <iostream>
#include <string>

#define PIPE_NAME L"\\\\.\\pipe\\SimpleInputServer"
#define BUF_SIZE 1024

int main() {
    std::cout << "[Server] 启动，监听管道..." << std::endl;

    while (true) {
        HANDLE hPipe = CreateNamedPipe(PIPE_NAME,
                                       PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE
                                           | PIPE_WAIT,
                                       PIPE_UNLIMITED_INSTANCES,
                                       BUF_SIZE,
                                       BUF_SIZE,
                                       0,
                                       NULL);

        if (hPipe == INVALID_HANDLE_VALUE) {
            std::cerr << "创建管道失败: " << GetLastError() << std::endl;
            return 1;
        }

        std::cout << "[Server] 等待客户端连接..." << std::endl;
        BOOL connected = ConnectNamedPipe(hPipe, NULL)
                             ? TRUE
                             : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (!connected) {
            CloseHandle(hPipe);
            continue;
        }

        std::cout << "[Server] 客户端已连接" << std::endl;

        // 服务端维护的“输入缓冲”，模拟输入法引擎状态
        std::string inputBuffer;
        char buffer[BUF_SIZE];
        DWORD bytesRead;

        while (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL)
               && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            std::string key(buffer);

            std::cout << "[Server] 收到按键: " << key << std::endl;

            // ===== 这里就是“重服务端”的核心逻辑 =====
            if (key == "BACKSPACE") {
                if (!inputBuffer.empty())
                    inputBuffer.pop_back();
            } else if (key == "ENTER") {
                inputBuffer.clear();
                inputBuffer = "(已提交)";
            } else if (key.size() == 1) {
                inputBuffer += key;
            }
            // =======================================

            std::string output = "缓冲内容: [" + inputBuffer + "]";

            DWORD bytesWritten;
            WriteFile(hPipe,
                      output.c_str(),
                      (DWORD)output.length(),
                      &bytesWritten,
                      NULL);
        }

        std::cout << "[Server] 客户端断开" << std::endl;
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }

    return 0;
}