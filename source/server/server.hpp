
#pragma once
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <queue>
#include <condition_variable>
#include "../include/ofs_types.hpp"

struct OFSRequest {
    std::string cmd;
    std::vector<std::string> args;
    int client_fd;
};

struct OFSResponse {
    int client_fd;
    std::string json;
};
template<typename T>
class TSQueue {
private:
    std::queue<T> q;
    std::mutex mtx;
    std::condition_variable cv;
public:
    void push(const T& item) {
        std::unique_lock<std::mutex> lock(mtx);
        q.push(item);
        cv.notify_one();
    }

    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mtx);
        if(q.empty()) {
            cv.wait(lock, [this]{ return !q.empty(); });
        }
        if(q.empty()) return false;
        item = q.front();
        q.pop();
        return true;
    }
};
class OFSServer {
private:
    int server_fd;
    bool running;
    std::thread accept_thread;
    std::vector<std::thread> worker_threads;
    TSQueue<OFSRequest> op_queue;
    std::unordered_map<int, void*> client_sessions;
    std::mutex session_mtx;
    void* fs_inst;
uint32_t max_connections;  
uint32_t queue_timeout;  

    void acceptLoop();
    void workerLoop();
    void handleRequest(const OFSRequest& req);
    std::vector<std::string> parseArgs(const std::string& line);
    std::string make_response_json(const std::string& status, const std::string& op, const std::string& error, const std::string& data);
public:
    OFSServer();
    ~OFSServer();
      bool start(uint16_t port, void* _fs_inst, uint32_t max_conn, uint32_t queue_tmo);
   
    void stop();
    void sendResponse(const OFSResponse& resp);
};
