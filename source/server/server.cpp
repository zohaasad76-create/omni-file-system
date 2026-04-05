
#include "server.hpp"
#include "../core/ofs_core.hpp"
#include <iostream>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <regex>
#include <iomanip>
#include <ctime>

int r = -1;          
std::string msg;      
OFSServer::OFSServer() : server_fd(-1), running(false), fs_inst(nullptr) {}
OFSServer::~OFSServer() { stop(); }

bool OFSServer::start(uint16_t port, void* _fs_inst, uint32_t max_conn, uint32_t queue_tmo) {
    fs_inst = _fs_inst;
    this->max_connections = max_conn;
    this->queue_timeout = queue_tmo;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0){ perror("socket"); return false; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if(bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0){ perror("bind"); return false; }

    if(listen(server_fd, max_connections) < 0){ perror("listen"); return false; }

    running = true;
    accept_thread = std::thread(&OFSServer::acceptLoop, this);

    
    for(int i = 0; i < 4; ++i)
        worker_threads.emplace_back(&OFSServer::workerLoop, this);

    std::cout << "[OFS] Server started on port " << port 
              << " | Max connections: " << max_connections 
              << " | Queue timeout: " << queue_timeout << "s\n";

    return true;
}

void OFSServer::stop() {
    running = false;
    if(server_fd >=0) close(server_fd);
    if(accept_thread.joinable()) accept_thread.join();
    for(auto &t: worker_threads) if(t.joinable()) t.join();
}

void OFSServer::acceptLoop() {
    fd_set master, read_fds;
    FD_ZERO(&master);
    FD_SET(server_fd, &master);
    int max_fd = server_fd;
    std::vector<int> clients;

    while(running){
        read_fds = master;
        timeval tv{1,0};
        int ret = select(max_fd+1, &read_fds, nullptr, nullptr, &tv);
        if(ret < 0) continue;

        if(FD_ISSET(server_fd, &read_fds)){
            sockaddr_in cli_addr{};
            socklen_t len = sizeof(cli_addr);
            int cli_fd = accept(server_fd,(sockaddr*)&cli_addr,&len);
            if(cli_fd >= 0){
                fcntl(cli_fd, F_SETFL, O_NONBLOCK);
                FD_SET(cli_fd,&master);
                if(cli_fd>max_fd) max_fd = cli_fd;
                clients.push_back(cli_fd);
                std::cout << "[OFS] New client FD=" << cli_fd << "\n";
            }
        }

        char buffer[4096];
        for(auto it=clients.begin(); it!=clients.end();){
            int fd = *it;
            if(FD_ISSET(fd,&read_fds)){
                ssize_t n = recv(fd, buffer,sizeof(buffer)-1,0);
                if(n<=0){
                    close(fd);
                    std::lock_guard<std::mutex> lock(session_mtx);
                    client_sessions.erase(fd);
                    it = clients.erase(it);
                    continue;
                }
                std::string s(buffer,n);
                std::istringstream iss(s);
                std::string line;

                while (std::getline(iss, line)) {
                    if (line.empty()) continue;
                    std::vector<std::string> tokens = parseArgs(line);
                    if (tokens.empty()) continue;

                    OFSRequest req;
                    req.client_fd = fd;
                    req.cmd = tokens[0];
                    req.args.assign(tokens.begin() + 1, tokens.end());
                    op_queue.push(req);
                }
            }
            ++it;
        }
    }
}

std::vector<std::string> OFSServer::parseArgs(const std::string& line) {
    std::vector<std::string> args;
    std::istringstream iss(line);
    std::string token;
    bool in_quotes = false;
    std::string buf;

    for(char c : line) {
        if(c == '"') {
            in_quotes = !in_quotes;
            continue;
        }
        if(c == ' ' && !in_quotes) {
            if(!buf.empty()) {
                args.push_back(buf);
                buf.clear();
            }
        } else {
            buf += c;
        }
    }
    if(!buf.empty()) args.push_back(buf);
    return args;
}


std::string OFSServer::make_response_json(const std::string& status,const std::string& op,const std::string& error,const std::string& data){
    std::string res = "{\"status\":\""+status+"\",\"operation\":\""+op+"\",\"request_id\":\"0\",\"error_message\":\""+error+"\"";
    if(!data.empty()) {
      
        std::string d = data;
       
        std::string out;
        for(char ch : d){
            if(ch == '\\') out += "\\\\";
            else if(ch == '"') out += "\\\"";
            else if(ch == '\n') out += "\\n";
            else out += ch;
        }
        res += ",\"data\":\""+ out + "\"";
    }
    res+="}";
    return res;
}

void OFSServer::workerLoop(){
    while(running){
        OFSRequest req;
        if(op_queue.pop(req))
            handleRequest(req);
    }
}

static std::string fmt_time(uint64_t t) {
    if(t == 0) return "0";
    std::time_t tt = (time_t)t;
    char buf[64];
#if defined(__APPLE__) || defined(__MACH__)
    std::tm *tmv = localtime(&tt);
    if(tmv) std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmv);
#else
    std::tm tmv;
    localtime_r(&tt, &tmv);
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
#endif
    return std::string(buf);
}

void OFSServer::handleRequest(const OFSRequest& req){
    OFSResponse resp;
    resp.client_fd = req.client_fd;
    std::string op = req.cmd;
    std::string data, msg;
    void* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(session_mtx);
        if(client_sessions.count(req.client_fd)) session = client_sessions[req.client_fd];
    }

    int r = -1;
    if(op=="login" && req.args.size()>=2){
        r = user_login(&session,req.args[0].c_str(),req.args[1].c_str());
        if(r==0){
            std::lock_guard<std::mutex> lock(session_mtx);
            client_sessions[req.client_fd] = session;
        }
        msg = get_error_message(r);
    }
    else if(op=="logout"){
        if(session){
            r = user_logout(session);
            std::lock_guard<std::mutex> lock(session_mtx);
            client_sessions.erase(req.client_fd);
        } else {
            r = static_cast<int>(OFSErrorCodes::ERROR_INVALID_SESSION);
        }
        msg = get_error_message(r);
    }
    else if(op=="create_user" && req.args.size()>=3){
        UserRole role = (req.args[2]=="admin") ? UserRole::ADMIN : UserRole::NORMAL;
        r = user_create(session,req.args[0].c_str(),req.args[1].c_str(),role);
        msg = get_error_message(r);
    }
    
else if(op == "delete_user" && req.args.size() >= 1){
    if(!session){
        r = static_cast<int>(OFSErrorCodes::ERROR_INVALID_SESSION);
    } else {
        const std::string& username_to_delete = req.args[0];
        r = user_delete(session, username_to_delete.c_str());
    }
    msg = get_error_message(r);
}

    else if(op=="list_users"){
        UserInfo* users=nullptr; int count=0;
        r = user_list(session,&users,&count);
        if(r==0 && users){
            for(int i=0;i<count;i++) {
                data+=std::string(users[i].username);
                data += (users[i].role==UserRole::ADMIN) ? " (admin)\n":"\n";
            }
            free_buffer(users);
        }
        msg = get_error_message(r);
    }

    else if(op=="create_dir" && req.args.size()>=1){
        r = dir_create(session,req.args[0].c_str());
        msg = get_error_message(r);
    }
    else if(op=="delete_dir" && req.args.size()>=1){
        r = dir_delete(session,req.args[0].c_str());
        msg = get_error_message(r);
    }
    else if(op=="dir_exists" && req.args.size()>=1){
       
        int rc = dir_exists(session, req.args[0].c_str());
        if(rc == static_cast<int>(OFSErrorCodes::SUCCESS)) data = "true";
        else data = "false";
        r = static_cast<int>(OFSErrorCodes::SUCCESS);
        msg = get_error_message(rc);
    }
   
else if(op=="dir_list" && req.args.size()>=1){
    FileEntry* entries = nullptr; 
    int count = 0;
    r = dir_list(session, req.args[0].c_str(), &entries, &count);
    if(r==0 && entries){
        std::ostringstream oss;
        for(int i=0;i<count;i++){
            // Escape any double quotes or backslashes
            std::string name = entries[i].name;
            for(size_t j=0;j<name.size();j++){
                if(name[j]=='"' || name[j]=='\\'){
                    oss << '\\';
                }
                oss << name[j];
            }
            if(entries[i].type == static_cast<uint8_t>(EntryType::DIRECTORY)) oss << "/"; 
            oss << "  \t" << entries[i].size << " bytes";
            oss << " \towner:" << entries[i].owner;
            oss << " \tperm:" << entries[i].permissions;
            oss << "\\n"; // escape newline for JSON string
        }
        data = oss.str();
        free_buffer(entries);
    }
    msg = get_error_message(r);
}
    else if(op=="create_file" && req.args.size()>=2){
        r = file_create(session,req.args[0].c_str(),req.args[1].c_str(), req.args[1].size());
        msg = get_error_message(r);
    }
    else if(op=="read_file" && req.args.size()>=1){
        char* buf=nullptr; size_t sz=0;
        r = file_read(session,req.args[0].c_str(),&buf,&sz);
        if(r==0 && buf){
            data.assign(buf,sz);
            free_buffer(buf);
        }
        msg = get_error_message(r);
    }
    
    else if (op == "edit_file" && req.args.size() >= 3) {
        try {
            unsigned int idx = std::stoul(req.args[2]);         
            const std::string& new_data = req.args[1];         
            r = file_edit(session, req.args[0].c_str(),        
                          new_data.c_str(),                     
                          new_data.size(),                      
                          idx);                                
        } catch (const std::exception& e) {
            r = static_cast<int>(OFSErrorCodes::ERROR_INVALID_OPERATION); 
        }
        msg = get_error_message(r);
    }
    else if (op == "truncate_file" && req.args.size() >= 2) {
        unsigned long sz = 0;
        try {
            sz = std::stoul(req.args[1]);
            r = file_truncate(session, req.args[0].c_str(), sz);
        } catch (...) {
            r = static_cast<int>(OFSErrorCodes::ERROR_INVALID_OPERATION);
        }
        msg = get_error_message(r);
    }
    else if(op=="rename_file" && req.args.size() >= 2) {
        const std::string& old_path = req.args[0];
        const std::string& new_path = req.args[1];
        r = file_rename(session, old_path.c_str(), new_path.c_str());
        msg = get_error_message(r);
        if(r != 0) {
            std::cerr << "[OFS] rename_file failed: " << old_path << " -> " << new_path
                      << " code=" << r << " msg=" << msg << "\n";
        }
    }
    else if (op == "delete_file" && req.args.size() >= 1) {
        r = file_delete(session, req.args[0].c_str());
        msg = get_error_message(r);
    }

    else if(op == "get_metadata" && req.args.size() >= 1) {
        FileMetadata meta;
        r = get_metadata(session, req.args[0].c_str(), &meta);
        if(r==0){
            std::ostringstream oss;
            oss << "Path: " << meta.path << "\n";
            oss << "Name: " << meta.entry.name << "\n";
            oss << "Type: " << (meta.entry.type == static_cast<uint8_t>(EntryType::DIRECTORY) ? "directory" : "file") << "\n";
            oss << "Size: " << meta.entry.size << " bytes\n";
            oss << "Owner: " << meta.entry.owner << "\n";
            oss << "Permissions: " << meta.entry.permissions << "\n";
            oss << "Created: " << fmt_time(meta.entry.created_time) << "\n";
            oss << "Modified: " << fmt_time(meta.entry.modified_time) << "\n";
            oss << "Blocks used: " << meta.blocks_used << "\n";
            data = oss.str();
        }
        msg = get_error_message(r);
    }
    else if(op == "set_permissions" && req.args.size() >= 2) {
        uint32_t perms = 0;
        try {
            perms = static_cast<uint32_t>(std::stoul(req.args[1]));
            r = set_permissions(session, req.args[0].c_str(), perms);
        } catch(...) {
            r = static_cast<int>(OFSErrorCodes::ERROR_INVALID_OPERATION);
        }
        msg = get_error_message(r);
    }
    else if(op == "set_owner") {
        r = static_cast<int>(OFSErrorCodes::ERROR_NOT_IMPLEMENTED);
        msg = get_error_message(r);
    }
    else if(op == "get_session_info") {
        if(!session){
            r = static_cast<int>(OFSErrorCodes::ERROR_INVALID_SESSION);
            msg = get_error_message(r);
        } else {
            SessionInfo info;
            r = get_session_info(session, &info);
            if(r==0){
                std::ostringstream oss;
                oss << "Session ID: " << info.session_id << "\n";
                oss << "User: " << info.user.username << "\n";
                oss << "Role: " << ((info.user.role==UserRole::ADMIN)?"admin":"normal") << "\n";
                oss << "Login time: " << fmt_time(info.login_time) << "\n";
                oss << "Last activity: " << fmt_time(info.last_activity) << "\n";
                oss << "Operations: " << info.operations_count << "\n";
                data = oss.str();
            }
            msg = get_error_message(r);
        }
    }

    else {
        r = static_cast<int>(OFSErrorCodes::ERROR_INVALID_OPERATION);
        msg = "Unknown command or wrong arguments";
    }

    resp.json = make_response_json((r == 0) ? "success" : "error", op, msg, data);
    sendResponse(resp);
}

void OFSServer::sendResponse(const OFSResponse& resp){
    std::string s = resp.json + "\n";
    send(resp.client_fd,s.c_str(),s.size(),0);
}
