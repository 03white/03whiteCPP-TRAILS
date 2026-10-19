#include "client.hpp"

int main() {
    Client client("https://api.example.com");

    // GET
    client.endpoint<GET>("/users/1", [](const Response& res) {
        if (res.ok())
            std::cout << "body: " << res.body << "\n";
        else
            std::cout << "HTTP " << res.status_code << "\n";
    });

    // GET + 自定义 header
    client.endpoint<GET>(
        "/me",
        [](const Response& res) { std::cout << res.body << "\n"; },
        /*body=*/"",
        /*headers=*/
        {"Authorization: Bearer TOKEN", "Accept: application/json"});

    // POST
    client.endpoint<POST>("/users",
                          [](const Response& res) {
                              std::cout << "created: " << res.body << "\n";
                          },
                          R"({"name":"alice"})",
                          {"Content-Type: application/json"});

    return 0;
}