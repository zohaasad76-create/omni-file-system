
#include "server.hpp"
#include "../core/ofs_core.hpp"
#include <iostream>
#include <fstream>
#include <string>

int main() {
    void* fs_instance = nullptr;
    std::ifstream file("compiled/default.uconf");
    uint16_t port = 8080;
    uint32_t max_connections = 20;
    uint32_t queue_timeout = 30;

    if (file.is_open()) {
        std::string line;
        bool in_server = false;
        while (std::getline(file, line)) {
            if (line.find("[server]") != std::string::npos) {
                in_server = true;
                continue;
            }
            if (in_server) {
                if (line.find("port") != std::string::npos) {
                    size_t eq = line.find('=');
                    if (eq != std::string::npos)
                        port = static_cast<uint16_t>(std::stoi(line.substr(eq + 1)));
                }
                if (line.find("max_connections") != std::string::npos) {
                    size_t eq = line.find('=');
                    if (eq != std::string::npos)
                        max_connections = static_cast<uint32_t>(std::stoi(line.substr(eq + 1)));
                }
                if (line.find("queue_timeout") != std::string::npos) {
                    size_t eq = line.find('=');
                    if (eq != std::string::npos)
                        queue_timeout = static_cast<uint32_t>(std::stoi(line.substr(eq + 1)));
                }
                if (line.find("[") != std::string::npos && line.find("[server]") == std::string::npos)
                    break;
            }
        }
    }

    std::cout << "[OFS] Loaded config: port=" << port 
              << ", max_connections=" << max_connections 
              << ", queue_timeout=" << queue_timeout << "\n";
    int r = fs_init(&fs_instance, "compiled/sample.omni", "compiled/default.uconf");
    if (r != 0) {
        std::cerr << "Failed to init filesystem: " << get_error_message(r) << "\n";
        return 1;
    }
    OFSServer server;
    if (!server.start(port, fs_instance, max_connections, queue_timeout)) {
        std::cerr << "Failed to start server\n";
        return 1;
    }

    std::cout << "Press Enter to stop server...\n";
    std::cin.get();

    server.stop();
    fs_shutdown(fs_instance);
    return 0;
}
