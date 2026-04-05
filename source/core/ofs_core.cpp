#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <string>
#include <memory>
#include <random>
#include <chrono>
#include "ofs_instance.hpp"
#include "meta_entry.hpp"
#include "../data_structures/simple_unordered_map.hpp"
#include "../include/ofs_types.hpp"
#include "ofs_core.hpp"

static uint32_t find_free_meta_index(FSInstance* inst);
static bool dir_block_read(FSInstance* inst, const MetaEntry& dir, std::vector<uint32_t>& children);
static bool dir_add_child(FSInstance* inst, MetaEntry& parent, uint32_t child_idx);
static bool dir_remove_child(FSInstance* inst, MetaEntry& parent, uint32_t child_idx);
FSInstance* g_fsinstance = nullptr;
static inline int ofs_success() { return static_cast<int>(OFSErrorCodes::SUCCESS); }
static inline int ofs_err(OFSErrorCodes e) { return static_cast<int>(e); }
static std::string read_file_to_string(const char* path) {
    std::ifstream in(path);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
static uint32_t find_free_meta_index(FSInstance* inst) {
    for (uint32_t i = 0; i < inst->meta_entries.size(); ++i) {
        if (inst->meta_entries[i].valid) {
            return i + 1;
        }
    }
    return 0;
}
static bool dir_block_read(FSInstance* inst, const MetaEntry& dir, std::vector<uint32_t>& children) {
    children.clear();
    uint32_t cur = dir.start_index;
    uint32_t blk_size = static_cast<uint32_t>(inst->header.block_size);
    while (cur != 0) {
        uint64_t pos = (uint64_t)inst->blocks_offset + uint64_t(cur - 1) * blk_size;
        inst->file.seekg(pos, std::ios::beg);
        if (!inst->file.good()) return false;
        uint32_t next = 0;
        inst->file.read(reinterpret_cast<char*>(&next), sizeof(next));
        size_t payload_count = (blk_size - sizeof(next)) / sizeof(uint32_t);
        std::vector<uint32_t> payload(payload_count);
        inst->file.read(reinterpret_cast<char*>(payload.data()), payload_count * sizeof(uint32_t));
        if (!inst->file.good() && !inst->file.eof()) return false;
        for (uint32_t val : payload) {
            if (val != 0) children.push_back(val);
        }
        cur = next;
    }
    return true;
}
static bool dir_add_child(FSInstance* inst, MetaEntry& parent, uint32_t child_idx) {
    std::vector<uint32_t> children;
    if (!dir_block_read(inst, parent, children)) return false;
    children.push_back(child_idx);
    uint32_t block_index = parent.start_index;
    uint32_t blk_size = static_cast<uint32_t>(inst->header.block_size);
    if (block_index == 0) {
        for (uint32_t i = 0; i < inst->num_blocks; ++i) {
            if (! (inst->free_bitmap[i/8] & (1 << (i % 8)))) {
                block_index = i + 1;
                inst->free_bitmap[i/8] |= (1 << (i % 8));
                parent.start_index = block_index;
                break;
            }
        }
    }
    if (block_index == 0) return false;
    uint64_t pos = (uint64_t)inst->blocks_offset + uint64_t(block_index - 1) * blk_size;
    inst->file.seekp(pos, std::ios::beg);
    uint32_t next_block = 0;
    inst->file.write(reinterpret_cast<const char*>(&next_block), sizeof(next_block));
    size_t payload_count = (blk_size - sizeof(next_block)) / sizeof(uint32_t);
    std::vector<uint32_t> new_payload(payload_count, 0);
    for (size_t i = 0; i < children.size() && i < payload_count; ++i) new_payload[i] = children[i];
    inst->file.write(reinterpret_cast<const char*>(new_payload.data()), payload_count * sizeof(uint32_t));
    inst->file.flush();
    return inst->file.good();
}
static bool dir_remove_child(FSInstance* inst, MetaEntry& parent, uint32_t child_idx) {
    std::vector<uint32_t> children;
    if (!dir_block_read(inst, parent, children)) return false;
    auto it = std::find(children.begin(), children.end(), child_idx);
    if (it == children.end()) return false;
    children.erase(it);
    uint32_t block_index = parent.start_index;
    if (block_index == 0) return false;
    uint32_t blk_size = static_cast<uint32_t>(inst->header.block_size);
    uint64_t pos = (uint64_t)inst->blocks_offset + uint64_t(block_index - 1) * blk_size;
    inst->file.seekp(pos, std::ios::beg);
    uint32_t next_block = 0;
    inst->file.write(reinterpret_cast<const char*>(&next_block), sizeof(next_block));
    size_t payload_count = (blk_size - sizeof(next_block)) / sizeof(uint32_t);
    std::vector<uint32_t> new_payload(payload_count, 0);
    for (size_t i = 0; i < children.size() && i < payload_count; ++i) new_payload[i] = children[i];
    inst->file.write(reinterpret_cast<const char*>(new_payload.data()), payload_count * sizeof(uint32_t));
    inst->file.flush();
    return inst->file.good();
}
struct SimpleConfig {
    uint64_t total_size = 104857600ULL;
    uint64_t header_size = 512;
    uint64_t block_size = 4096;
    uint32_t max_files = 1000;
    uint32_t max_users = 50;
    std::string admin_username = "admin";
    std::string admin_password = "admin123";
    bool load(const char* path) {
        if (!path) return false;
        std::ifstream in(path);
        if (!in) return false;
        std::string line;
        while (std::getline(in, line)) {
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            auto trim = [](std::string& s) {
                while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
                while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
            };
            trim(key); trim(val);
            if (key == "total_size") total_size = std::stoull(val);
            else if (key == "header_size") header_size = std::stoull(val);
            else if (key == "block_size") block_size = std::stoull(val);
            else if (key == "max_files") max_files = (uint32_t)std::stoul(val);
            else if (key == "max_users") max_users = (uint32_t)std::stoul(val);
            else if (key == "admin_username") {
                if (val.size() > 0 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
                admin_username = val;
            }
            else if (key == "admin_password") {
                if (val.size() > 0 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
                admin_password = val;
            }
        }
        return true;
    }
};
static std::string placeholder_hash(const std::string& s) {
    std::hash<std::string> h;
    size_t v = h(s);
    std::ostringstream ss;
    ss << std::hex << v;
    std::string out = ss.str();
    if (out.size() < 64) out += std::string(64 - out.size(), '0');
    else if (out.size() > 64) out = out.substr(0, 64);
    return out;
}
static std::string new_session_id() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    uint64_t r1 = dis(gen), r2 = dis(gen);
    char buf[33];
    snprintf(buf, sizeof(buf), "%016llx%016llx", (unsigned long long)r1, (unsigned long long)r2);
    return std::string(buf);
}
static bool write_block(std::fstream& f, uint64_t block_region_offset, uint32_t block_size, uint32_t block_index, uint32_t next_block, const uint8_t* data, size_t data_len) {
    if (block_index == 0) return false;
    uint64_t pos = block_region_offset + uint64_t(block_index - 1) * block_size;
    f.seekp(pos, std::ios::beg);
    if (!f.good()) return false;
    uint32_t nb = next_block;
    f.write(reinterpret_cast<const char*>(&nb), sizeof(nb));
    size_t payload = (data_len > (size_t)block_size - 4) ? ((size_t)block_size - 4) : data_len;
    if (payload > 0) f.write(reinterpret_cast<const char*>(data), payload);
    size_t pad = (size_t)block_size - 4 - payload;
    if (pad) {
        static const char zeros[4096] = { 0 };
        while (pad > 0) {
            size_t w = std::min<size_t>(pad, sizeof(zeros));
            f.write(zeros, w);
            pad -= w; } }
    f.flush();
    return f.good();}
static bool read_block(std::fstream& f, uint64_t block_region_offset, uint32_t block_size, uint32_t block_index, uint32_t& next_block, std::vector<uint8_t>& payload) {
    payload.clear();
    if (block_index == 0) { next_block = 0; return false; }
    uint64_t pos = block_region_offset + uint64_t(block_index - 1) * block_size;
    f.seekg(pos, std::ios::beg);
    if (!f.good()) return false;
    uint32_t nb = 0;
    f.read(reinterpret_cast<char*>(&nb), sizeof(nb));
    next_block = nb;
    size_t payload_size = (size_t)block_size - 4;
    payload.resize(payload_size);
    f.read(reinterpret_cast<char*>(payload.data()), payload_size);
    if (!f.good() && !f.eof()) return false;
    return true;}
static inline bool bitmap_get(const std::vector<uint8_t>& bits, uint32_t idx) {
    uint32_t byte_idx = idx / 8;
    uint8_t bit_mask = 1u << (idx % 8);
    return (bits[byte_idx] & bit_mask) != 0;
}
static inline void bitmap_set(std::vector<uint8_t>& bits, uint32_t idx, bool v) {
    uint32_t byte_idx = idx / 8;
    uint8_t bit_mask = 1u << (idx % 8);
    if (v) bits[byte_idx] |= bit_mask;
    else bits[byte_idx] &= ~bit_mask;
}
static std::vector<uint32_t> allocate_blocks(FSInstance* inst, uint32_t n) {
    std::vector<uint32_t> out;
    if (n == 0) return out;
    out.reserve(n);
    for (uint32_t i = 0; i < inst->num_blocks && out.size() < n; ++i) {
        if (!bitmap_get(inst->free_bitmap, i)) {
            bitmap_set(inst->free_bitmap, i, true);
            out.push_back(i + 1);} }
    if (out.size() < n) {
        for (uint32_t b : out) bitmap_set(inst->free_bitmap, b - 1, false);
        out.clear();
        return out; }
    return out;}
static void free_blocks(FSInstance* inst, const std::vector<uint32_t>& blocks) {
    for (uint32_t b : blocks)
        if (b && b <= inst->num_blocks) bitmap_set(inst->free_bitmap, b - 1, false);}
static bool persist_bitmap(FSInstance* inst) {
    if (!inst) return false;
    inst->file.seekp(inst->bitmap_offset, std::ios::beg);
    if (!inst->file.good()) return false;
    inst->file.write(reinterpret_cast<const char*>(inst->free_bitmap.data()), inst->free_bitmap.size());
    inst->file.flush();
    return inst->file.good();}
static bool persist_meta_entries(FSInstance* inst) {
    if (!inst) return false;
    inst->file.seekp(inst->metadata_offset, std::ios::beg);
    if (!inst->file.good()) return false;
    inst->file.write(reinterpret_cast<const char*>(inst->meta_entries.data()), inst->meta_entries.size() * sizeof(MetaEntry));
    inst->file.flush();
    return inst->file.good();}
static bool persist_user_table(FSInstance* inst) {
    if (!inst) return false;
    uint64_t pos = inst->header.user_table_offset;
    inst->file.seekp(pos, std::ios::beg);
    if (!inst->file.good()) return false;
    inst->file.write(reinterpret_cast<const char*>(inst->users.data()), inst->users.size() * sizeof(UserInfo));
    inst->file.flush();
    return inst->file.good();}
static bool persist_header(FSInstance* inst) {
    if (!inst) return false;
    uint64_t v = inst->next_meta_index;
    if (sizeof(inst->header.reserved) >= 328)
        std::memcpy(inst->header.reserved + 320, &v, sizeof(v));
    inst->file.seekp(0, std::ios::beg);
    if (!inst->file.good()) return false;
    inst->file.write(reinterpret_cast<const char*>(&inst->header), sizeof(OMNIHeader));
    inst->file.flush();
    return inst->file.good();}
static bool encoding_initialized(const FSInstance* inst) {
    for (size_t i = 0; i < 256; ++i) if (inst->encoding_map[i] != 0) return true;
    return false;}
static void encode_data(const FSInstance* inst, const uint8_t* in, size_t len, std::vector<uint8_t>& out) {
    out.resize(len);
    if (!encoding_initialized(inst)) {
        std::memcpy(out.data(), in, len);
        return;}
    for (size_t i = 0; i < len; ++i) out[i] = inst->encoding_map[in[i]];}
static void decode_data(const FSInstance* inst, const uint8_t* in, size_t len, std::vector<uint8_t>& out) {
    out.resize(len);
    if (!encoding_initialized(inst)) {
        std::memcpy(out.data(), in, len);
        return;}
    uint8_t inv[256] = {0};
    for (int i = 0; i < 256; ++i) inv[inst->encoding_map[i]] = static_cast<uint8_t>(i);
    for (size_t i = 0; i < len; ++i) out[i] = inv[in[i]];}
static std::string build_full_path_from_meta(const FSInstance* inst, uint32_t meta_index) {
    if (meta_index == 1) return "/";
    std::vector<std::string> parts;
    uint32_t cur = meta_index;
    uint32_t guard = 0;
    while (cur != 0 && cur <= inst->meta_entries.size() && guard < inst->meta_entries.size()) {
        const MetaEntry& e = inst->meta_entries[cur - 1];
        if (e.valid) break;
        if (cur == 1) break;
        std::string name = e.get_name();
        if (name.empty()) name = "unnamed";
        parts.push_back(name);
        cur = e.parent;
        ++guard;  }
    if (parts.empty()) return "/";
    std::string path;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        path += "/";
        path += *it; }
    return path;}
static void rebuild_path_index(FSInstance* inst) {
    inst->path_index.clear();
    uint32_t count = static_cast<uint32_t>(inst->meta_entries.size());
    for (uint32_t i = 0; i < count; ++i) {
        const MetaEntry& e = inst->meta_entries[i];
        if (e.valid == 0) {
            uint32_t meta_index = i + 1;
            std::string full_path = build_full_path_from_meta(inst, meta_index);
            inst->path_index.insert(full_path, meta_index);} }}
int user_login(void** session, const char* username, const char* password) {
    if (!session || !username || !password) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    FSInstance* inst = g_fsinstance;
    if (!inst) return ofs_err(OFSErrorCodes::ERROR_INVALID_SESSION);
    auto user_it = inst->user_index.find(username);
    size_t idx = SIZE_MAX;
    if (user_it) {
        idx = *user_it;
    } else {
        for (size_t i = 0; i < inst->users.size(); ++i) {
            if (inst->users[i].is_active && std::strncmp(inst->users[i].username, username, sizeof(inst->users[i].username)) == 0) {
                idx = i;
                break; } } }
    if (idx == SIZE_MAX || idx >= inst->users.size()) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    UserInfo& user = inst->users[idx];
    if (!user.is_active) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    std::string hash = placeholder_hash(password);
    if (std::strncmp(user.password_hash, hash.c_str(), 64) != 0)
        return ofs_err(OFSErrorCodes::ERROR_PERMISSION_DENIED);
    user.last_login = (uint64_t)time(nullptr);
    persist_user_table(inst);
    std::string session_id = new_session_id();
    std::shared_ptr<SessionInfo> s = std::make_shared<SessionInfo>(session_id, user, user.last_login, inst);
    inst->sessions.insert(session_id, s);
    *session = s.get();
    return ofs_success();}
int user_logout(void* session) {
    if (!session) return ofs_err(OFSErrorCodes::ERROR_INVALID_SESSION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    FSInstance* inst = s->inst;
    if (!inst) return ofs_err(OFSErrorCodes::ERROR_INVALID_SESSION);
    inst->sessions.erase(s->session_id);
    return ofs_success();}
int user_create(void* admin_session, const char* username, const char* password, UserRole role) {
    if (!admin_session || !username || !password) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* sess = reinterpret_cast<SessionInfo*>(admin_session);
    FSInstance* inst = sess->inst;
    if (sess->user.role != UserRole::ADMIN) return ofs_err(OFSErrorCodes::ERROR_PERMISSION_DENIED);
    if (inst->user_index.contains(username)) return ofs_err(OFSErrorCodes::ERROR_FILE_EXISTS);
    int free_idx = -1;
    for (size_t i = 0; i < inst->users.size(); ++i)
        if (!inst->users[i].is_active) { free_idx = (int)i; break; }
    if (free_idx == -1) return ofs_err(OFSErrorCodes::ERROR_NO_SPACE);
    std::string hash = placeholder_hash(password);
    UserInfo new_user(username, hash, role, (uint64_t)time(nullptr));
    inst->users[free_idx] = new_user;
    inst->user_index.insert(std::string(new_user.username), (size_t)free_idx);
    persist_user_table(inst);
    return ofs_success();}
int user_delete(void* admin_session, const char* username) {
    if (!admin_session || !username) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* sess = reinterpret_cast<SessionInfo*>(admin_session);
    FSInstance* inst = sess->inst;
    if (sess->user.role != UserRole::ADMIN) return ofs_err(OFSErrorCodes::ERROR_PERMISSION_DENIED);
    auto user_it = inst->user_index.find(username);
    if (!user_it) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    size_t idx = *user_it;
    UserInfo& user = inst->users[idx];
    user.is_active = 0;
    persist_user_table(inst);
    inst->user_index.erase(username);
    return ofs_success();
}

int user_list(void* admin_session, UserInfo** users_out, int* count) {
    if (!admin_session || !users_out || !count) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* sess = reinterpret_cast<SessionInfo*>(admin_session);
    FSInstance* inst = sess->inst;
    if (sess->user.role != UserRole::ADMIN) return ofs_err(OFSErrorCodes::ERROR_PERMISSION_DENIED);

    std::vector<UserInfo> found;
    for (const UserInfo& u : inst->users)
        if (u.is_active) found.push_back(u);

    *users_out = nullptr;
    *count = 0;
    if (!found.empty()) {
        *users_out = (UserInfo*)malloc(found.size() * sizeof(UserInfo));
        if (!*users_out) return ofs_err(OFSErrorCodes::ERROR_NO_SPACE);
        std::memcpy(*users_out, found.data(), found.size() * sizeof(UserInfo));
        *count = (int)found.size();
    }
    return ofs_success();
}

int get_session_info(void* session, SessionInfo* info) {
    if (!session || !info) return ofs_err(OFSErrorCodes::ERROR_INVALID_SESSION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    std::memcpy(info, s, sizeof(SessionInfo));
    return ofs_success();
}

// -------------------- File and Directory Operations --------------------

static std::vector<std::string> split_path_tokens(const std::string& path) {
    std::vector<std::string> out;
    size_t i = 0, n = path.size();
    while (i < n) {
        while (i < n && path[i] == '/') ++i;
        if (i >= n) break;
        size_t j = i;
        while (j < n && path[j] != '/') ++j;
        out.push_back(path.substr(i, j - i));
        i = j;
    }
    return out;
}

int file_create(void* session, const char* path_c, const char* data, size_t size) {
    if (!session || !path_c) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    FSInstance* inst = s->inst;
    std::string path(path_c);
    if (path.empty() || path[0] != '/') return ofs_err(OFSErrorCodes::ERROR_INVALID_PATH);

    auto tokens = split_path_tokens(path);
    if (tokens.empty()) return ofs_err(OFSErrorCodes::ERROR_INVALID_PATH);
    std::string basename = tokens.back();
    std::string parent_path = "/";
    if (tokens.size() > 1) {
        parent_path.clear();
        for (size_t i = 0; i + 1 < tokens.size(); ++i) {
            parent_path += "/";
            parent_path += tokens[i];
        }
    }
    const uint32_t* pIndex = inst->path_index.find(parent_path);
    if (!pIndex) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    uint32_t parent_meta = *pIndex;
    if (inst->path_index.contains(path)) return ofs_err(OFSErrorCodes::ERROR_FILE_EXISTS);

    uint32_t meta_index = find_free_meta_index(inst);
    if (meta_index == 0) return ofs_err(OFSErrorCodes::ERROR_NO_SPACE);
    MetaEntry& entry = inst->meta_entries[meta_index - 1];
    entry.valid = 0;
    entry.type = 0;
    entry.parent = parent_meta;
    entry.set_name(basename);
    entry.total_size = size;
    entry.permissions = 0644;
    auto it = inst->user_index.find(s->user.username);
    entry.owner_id = (it ? static_cast<uint32_t>(*it) : 0);
    uint64_t now = (uint64_t)std::time(nullptr);
    entry.created_time = now;
    entry.modified_time = now;

    uint32_t block_payload = static_cast<uint32_t>(inst->header.block_size - 4);
    uint32_t need_blocks = (size == 0) ? 0 : static_cast<uint32_t>((size + block_payload - 1) / block_payload);
    std::vector<uint32_t> blocks;
    if (need_blocks > 0) {
        blocks = allocate_blocks(inst, need_blocks);
        if (blocks.empty()) { entry.valid = 1; return ofs_err(OFSErrorCodes::ERROR_NO_SPACE); }
    }
    const uint8_t* src = reinterpret_cast<const uint8_t*>(data);
    uint32_t first_block = 0;
    for (size_t i = 0; i < blocks.size(); ++i) {
        uint32_t blk_idx = blocks[i];
        uint32_t next = (i + 1 < blocks.size()) ? blocks[i + 1] : 0;
        size_t offset = i * (size_t)block_payload;
        size_t chunk = std::min<size_t>((size - offset), (size_t)block_payload);
        std::vector<uint8_t> enc;
        encode_data(inst, src + offset, chunk, enc);
        if (!write_block(inst->file, inst->blocks_offset, static_cast<uint32_t>(inst->header.block_size), blk_idx, next, enc.data(), enc.size())) {
            free_blocks(inst, blocks);
            entry.valid = 1;
            return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
        }
        if (i == 0) first_block = blk_idx;
    }
    entry.start_index = first_block;
    if (inst->next_meta_index <= meta_index) inst->next_meta_index = meta_index + 1;
    if (!persist_meta_entries(inst)) return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
    if (!persist_bitmap(inst)) return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
    if (!persist_header(inst)) return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
    if (!dir_add_child(inst, inst->meta_entries[parent_meta - 1], meta_index)) {
        // best effort: leave entry but try to persist
    }
    inst->path_index.insert(path, meta_index);
    return ofs_success();
}

int file_read(void* session, const char* path_c, char** buffer, size_t* size_out) {
    if (!session || !path_c || !buffer || !size_out) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    FSInstance* inst = s->inst;
    std::string path(path_c);
    const uint32_t* pm = inst->path_index.find(path);
    if (!pm) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    uint32_t meta_index = *pm;
    if (meta_index == 0 || meta_index > inst->meta_entries.size()) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    MetaEntry& entry = inst->meta_entries[meta_index - 1];
    if (entry.valid != 0) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    if (entry.type != 0) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);

    uint64_t total_size = entry.total_size;
    if (total_size == 0) {
        *buffer = (char*)malloc(1);
        if (!*buffer) return ofs_err(OFSErrorCodes::ERROR_NO_SPACE);
        (*buffer)[0] = '\0';
        *size_out = 0;
        return ofs_success();
    }

    std::vector<uint8_t> file_data;
    file_data.reserve((size_t)total_size);
    uint32_t current_block = entry.start_index;
    uint64_t bytes_remaining = total_size;
    while (current_block != 0 && bytes_remaining > 0) {
        uint32_t next_block = 0;
        std::vector<uint8_t> payload;
        if (!read_block(inst->file, inst->blocks_offset, static_cast<uint32_t>(inst->header.block_size), current_block, next_block, payload)) {
            return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
        }
        size_t chunk_size = static_cast<size_t>(std::min<uint64_t>(bytes_remaining, payload.size()));
        std::vector<uint8_t> decoded;
        decode_data(inst, payload.data(), chunk_size, decoded);
        file_data.insert(file_data.end(), decoded.begin(), decoded.end());
        bytes_remaining -= chunk_size;
        current_block = next_block;
    }

    *buffer = (char*)malloc(file_data.size());
    if (!*buffer) return ofs_err(OFSErrorCodes::ERROR_NO_SPACE);
    if (!file_data.empty()) std::memcpy(*buffer, file_data.data(), file_data.size());
    *size_out = file_data.size();
    return ofs_success();
}

int file_delete(void* session, const char* path_c) {
    if (!session || !path_c) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    FSInstance* inst = s->inst;
    std::string path(path_c);
    const uint32_t* meta_idx = inst->path_index.find(path);
    if (!meta_idx) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    MetaEntry& entry = inst->meta_entries[*meta_idx - 1];
    if (entry.valid || entry.type != 0) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    std::vector<uint32_t> free_list;
    uint32_t cur = entry.start_index;
    while (cur) {
        free_list.push_back(cur);
        uint32_t next = 0;
        std::vector<uint8_t> payload;
        if (!read_block(inst->file, inst->blocks_offset, inst->header.block_size, cur, next, payload))
            break;
        cur = next; }
    free_blocks(inst, free_list);
    uint32_t parent_idx = entry.parent;
    if (parent_idx && parent_idx <= inst->meta_entries.size()) {
        MetaEntry& parent = inst->meta_entries[parent_idx - 1];
        dir_remove_child(inst, parent, *meta_idx);
    }
    entry.valid = 1;
    entry.start_index = 0;
    entry.total_size = 0;
    persist_meta_entries(inst);
    persist_bitmap(inst);
    rebuild_path_index(inst);
    return ofs_success();
}
int file_edit(void* session, const char* path_c, const char* data, size_t size, uint index) {
    if (!session || !path_c || !data) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    FSInstance* inst = s->inst;
    std::string path(path_c);
    const uint32_t* meta_idx = inst->path_index.find(path);
    if (!meta_idx) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    MetaEntry& entry = inst->meta_entries[*meta_idx - 1];
    if (entry.valid || entry.type != 0) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    if (index > entry.total_size) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    uint32_t block_payload = static_cast<uint32_t>(inst->header.block_size - 4);
    uint32_t block_no = static_cast<uint32_t>(index / block_payload);
    uint32_t offset_in_block = static_cast<uint32_t>(index % block_payload);
    uint32_t cur = entry.start_index;
    for (uint32_t i = 0; i < block_no && cur; ++i) {
        uint32_t next = 0;
        std::vector<uint8_t> payload;
        if (!read_block(inst->file, inst->blocks_offset, static_cast<uint32_t>(inst->header.block_size), cur, next, payload)) {
            return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);}
        cur = next;}
    if (cur == 0) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    uint32_t next = 0;
    std::vector<uint8_t> payload;
    if (!read_block(inst->file, inst->blocks_offset, inst->header.block_size, cur, next, payload))
        return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
    std::vector<uint8_t> dec;
    decode_data(inst, payload.data(), payload.size(), dec);
    size_t write_len = std::min(size, payload.size() - offset_in_block);
    std::memcpy(dec.data() + offset_in_block, data, write_len);
    std::vector<uint8_t> enc;
    encode_data(inst, dec.data(), dec.size(), enc);
    if (!write_block(inst->file, inst->blocks_offset, inst->header.block_size, cur, next, enc.data(), enc.size()))
        return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
    entry.modified_time = (uint64_t)time(nullptr);
    persist_meta_entries(inst);
    return ofs_success();}
int dir_create(void* session, const char* path_c) {
    if (!session || !path_c) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    FSInstance* inst = s->inst;
    std::string path(path_c);
    if (path.empty() || path[0] != '/') return ofs_err(OFSErrorCodes::ERROR_INVALID_PATH);
    auto tokens = split_path_tokens(path);
    if (tokens.empty()) return ofs_err(OFSErrorCodes::ERROR_INVALID_PATH);
    std::string basename = tokens.back();
    std::string parent_path = "/";
    if (tokens.size() > 1) {
        parent_path.clear();
        for (size_t i = 0; i + 1 < tokens.size(); ++i) {
            parent_path += "/";
            parent_path += tokens[i]; }}
    const uint32_t* parent_idx = inst->path_index.find(parent_path);
    if (!parent_idx) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    MetaEntry& parent = inst->meta_entries[*parent_idx - 1];
    if (parent.type != 1 || parent.valid != 0) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    if (inst->path_index.contains(path)) return ofs_err(OFSErrorCodes::ERROR_FILE_EXISTS);
    uint32_t meta_index = find_free_meta_index(inst);
    if (meta_index == 0) return ofs_err(OFSErrorCodes::ERROR_NO_SPACE);
    MetaEntry& entry = inst->meta_entries[meta_index - 1];
    entry.valid = 0;
    entry.type = 1;
    entry.parent = *parent_idx;
    entry.set_name(basename);
    entry.start_index = 0;
    entry.total_size = 0;
    entry.permissions = 0755;
    auto user_it = inst->user_index.find(s->user.username);
    entry.owner_id = (user_it ? static_cast<uint32_t>(*user_it) : 0);
    uint64_t now = static_cast<uint64_t>(time(nullptr));
    entry.created_time = now;
    entry.modified_time = now;
    if (!dir_add_child(inst, parent, meta_index)) {
        entry.valid = 1;
        return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
    }
    if (inst->next_meta_index <= meta_index) inst->next_meta_index = meta_index + 1;
    if (!persist_meta_entries(inst)) return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
    if (!persist_header(inst)) return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
    rebuild_path_index(inst);
    return ofs_success();
}
int dir_list(void* session, const char* path_c, FileEntry** entries, int* count) {
    if (!session || !path_c || !entries || !count) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    FSInstance* inst = s->inst;
    std::string path(path_c);
    const uint32_t* dir_idx = inst->path_index.find(path);
    if (!dir_idx) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    MetaEntry& dir = inst->meta_entries[*dir_idx - 1];
    if (dir.type != 1) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    std::vector<uint32_t> child_indices;
    if (!dir_block_read(inst, dir, child_indices)) return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
    std::vector<FileEntry> file_entries;
    for (uint32_t idx : child_indices) {
        if (idx == 0 || idx > inst->meta_entries.size()) continue;
        const MetaEntry& me = inst->meta_entries[idx - 1];
        if (me.valid != 0) continue;
        FileEntry fe;
        std::strncpy(fe.name, me.name, sizeof(fe.name) - 1);
        fe.type = me.type;
        fe.size = me.total_size;
        fe.permissions = me.permissions;
        fe.created_time = me.created_time;
        fe.modified_time = me.modified_time;
        if (me.owner_id < inst->users.size()) {
            std::strncpy(fe.owner, inst->users[me.owner_id].username, sizeof(fe.owner) - 1);
        } else {
            std::strncpy(fe.owner, "unknown", sizeof(fe.owner) - 1);
        }
        fe.inode = idx;
        file_entries.push_back(fe); }
    if (file_entries.empty()) {
        *entries = nullptr;
        *count = 0;
        return ofs_success();  }
    *entries = (FileEntry*) malloc(file_entries.size() * sizeof(FileEntry));
    if (!*entries) return ofs_err(OFSErrorCodes::ERROR_NO_SPACE);
    std::memcpy(*entries, file_entries.data(), file_entries.size() * sizeof(FileEntry));
    *count = (int)file_entries.size();
    return ofs_success();
}
int dir_delete(void* session, const char* path_c) {
    if (!session || !path_c) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    FSInstance* inst = s->inst;
    std::string path(path_c);
    if (path.empty() || path == "/") return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    const uint32_t* dir_idx = inst->path_index.find(path);
    if (!dir_idx) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    MetaEntry& dir = inst->meta_entries[*dir_idx - 1];
    if (dir.type != 1) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    std::vector<uint32_t> children;
    if (!dir_block_read(inst, dir, children)) return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
    if (!children.empty()) return ofs_err(OFSErrorCodes::ERROR_DIRECTORY_NOT_EMPTY);
    uint32_t parent_idx = dir.parent;
    if (parent_idx == 0 || parent_idx > inst->meta_entries.size()) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    MetaEntry& parent = inst->meta_entries[parent_idx - 1];
    if (!dir_remove_child(inst, parent, *dir_idx)) return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
    dir.valid = 1;
    if (dir.start_index) free_blocks(inst, std::vector<uint32_t>{dir.start_index});
    dir.start_index = 0;
    persist_meta_entries(inst);
    rebuild_path_index(inst);
    return ofs_success();
}
int dir_exists(void* session, const char* path_c) {
    if (!session || !path_c) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    FSInstance* inst = s->inst;
    std::string path(path_c);
    const uint32_t* idx = inst->path_index.find(path);
    if (!idx) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    const MetaEntry& meta = inst->meta_entries[*idx - 1];
    if (meta.type == 1 && meta.valid == 0) return ofs_success();
    return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
}
int get_metadata(void* session, const char* path_c, FileMetadata* meta) {
    if (!session || !path_c || !meta) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    FSInstance* inst = s->inst;
    std::string path(path_c);
    const uint32_t* meta_idx = inst->path_index.find(path);
    if (!meta_idx) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    const MetaEntry& me = inst->meta_entries[*meta_idx - 1];
    if (me.valid != 0) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    FileEntry fe;
    std::memset(&fe, 0, sizeof(FileEntry));
    std::strncpy(fe.name, me.name, sizeof(fe.name) - 1);
    fe.type = me.type;
    fe.size = me.total_size;
    fe.permissions = me.permissions;
    fe.created_time = me.created_time;
    fe.modified_time = me.modified_time;
    if (me.owner_id < inst->users.size())
        std::strncpy(fe.owner, inst->users[me.owner_id].username, sizeof(fe.owner) - 1);
    else
        std::strncpy(fe.owner, "unknown", sizeof(fe.owner) - 1);
    fe.inode = *meta_idx;
    std::memset(meta, 0, sizeof(FileMetadata));
    std::strncpy(meta->path, path.c_str(), sizeof(meta->path) - 1);
    std::memcpy(&meta->entry, &fe, sizeof(FileEntry));
    uint32_t count = 0;
    uint64_t used_bytes = 0;
    uint32_t blk = me.start_index;
    while (blk) {
        count++;
        uint32_t next = 0;
        std::vector<uint8_t> payload;
        if (!read_block(inst->file, inst->blocks_offset, inst->header.block_size, blk, next, payload)) break;
        used_bytes += payload.size();
        blk = next;
    }
    meta->blocks_used = count;
    meta->actual_size = count * inst->header.block_size;
    return ofs_success();
}
int set_permissions(void* session, const char* path_c, uint32_t permissions) {
    if (!session || !path_c) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    FSInstance* inst = s->inst;
    std::string path(path_c);
    const uint32_t* meta_idx = inst->path_index.find(path);
    if (!meta_idx) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    MetaEntry& me = inst->meta_entries[*meta_idx - 1];
    if (me.valid != 0) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    me.permissions = permissions;
    me.modified_time = (uint64_t)time(nullptr);
    persist_meta_entries(inst);
    return ofs_success();
}
int get_stats(void* session, FSStats* stats) {
    if (!session || !stats) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    FSInstance* inst = s->inst;
    uint64_t used_blocks = 0;
    for (uint32_t i = 0; i < inst->num_blocks; ++i) {
        if (bitmap_get(inst->free_bitmap, i)) used_blocks++;
    }
    uint64_t free_blocks = inst->num_blocks - used_blocks;
    uint64_t used_bytes = used_blocks * inst->header.block_size;
    uint64_t free_bytes = free_blocks * inst->header.block_size;
    uint32_t total_files = 0, total_dirs = 0;
    for (const MetaEntry& me : inst->meta_entries) {
        if (me.valid == 0) {
            if (me.type == 0) total_files++;
            if (me.type == 1) total_dirs++;
        }
    }
    uint32_t total_users = 0;
    for (const UserInfo& user : inst->users)
        if (user.is_active) total_users++;
    uint32_t active_sessions = inst->sessions.size();
    stats->total_size = inst->header.total_size;
    stats->used_space = used_bytes;
    stats->free_space = free_bytes;
    stats->total_files = total_files;
    stats->total_directories = total_dirs;
    stats->total_users = total_users;
    stats->active_sessions = active_sessions;
    stats->fragmentation = (double)(free_blocks > 0 ? 100.0*(1.0-used_blocks/(double)inst->num_blocks) : 0.0);
    std::memset(stats->reserved, 0, sizeof(stats->reserved));
    return ofs_success();
}
void free_buffer(void* buffer) {
    if (buffer) free(buffer);
}
const char* get_error_message(int error_code) {
    switch (static_cast<OFSErrorCodes>(error_code)) {
        case OFSErrorCodes::SUCCESS: return "Success";
        case OFSErrorCodes::ERROR_NOT_FOUND: return "Not found";
        case OFSErrorCodes::ERROR_PERMISSION_DENIED: return "Permission denied";
        case OFSErrorCodes::ERROR_IO_ERROR: return "I/O error";
        case OFSErrorCodes::ERROR_INVALID_PATH: return "Invalid path";
        case OFSErrorCodes::ERROR_FILE_EXISTS: return "File already exists";
        case OFSErrorCodes::ERROR_NO_SPACE: return "No space left";
        case OFSErrorCodes::ERROR_INVALID_CONFIG: return "Invalid configuration";
        case OFSErrorCodes::ERROR_NOT_IMPLEMENTED: return "Not implemented";
        case OFSErrorCodes::ERROR_INVALID_SESSION: return "Invalid session";
        case OFSErrorCodes::ERROR_DIRECTORY_NOT_EMPTY: return "Directory not empty";
        case OFSErrorCodes::ERROR_INVALID_OPERATION: return "Invalid operation";
        default: return "Unknown error";
    }
}


int fs_format(const char* omni_path, const char* config_path) {
    SimpleConfig cfg;
    if (config_path) cfg.load(config_path);

    uint64_t header_size = cfg.header_size;
    uint32_t max_users = cfg.max_users;
    uint32_t max_files = cfg.max_files;
    uint64_t block_size = cfg.block_size;
    uint64_t total_size = cfg.total_size;

    if (header_size < sizeof(OMNIHeader)) return ofs_err(OFSErrorCodes::ERROR_INVALID_CONFIG);
    if (block_size < 128) return ofs_err(OFSErrorCodes::ERROR_INVALID_CONFIG);
    if (total_size <= header_size) return ofs_err(OFSErrorCodes::ERROR_INVALID_CONFIG);

    uint64_t user_table_size = uint64_t(max_users) * sizeof(UserInfo);
    uint64_t metadata_size = uint64_t(max_files) * sizeof(MetaEntry);

    uint64_t available_for_blocks = total_size - header_size - user_table_size - metadata_size;
    if (available_for_blocks < block_size) return ofs_err(OFSErrorCodes::ERROR_INVALID_CONFIG);

    uint64_t num_blocks = available_for_blocks / block_size;
    uint64_t bitmap_bytes = (num_blocks + 7) / 8;
    while (true) {
        uint64_t content_region = total_size - header_size - user_table_size - metadata_size - bitmap_bytes;
        uint64_t nb = content_region / block_size;
        uint64_t new_bitmap = (nb + 7) / 8;
        if (new_bitmap == bitmap_bytes && nb == num_blocks) break;
        bitmap_bytes = new_bitmap;
        num_blocks = nb;
    }
    if (num_blocks == 0) return ofs_err(OFSErrorCodes::ERROR_INVALID_CONFIG);

    uint64_t user_table_offset = header_size;
    uint64_t metadata_offset = user_table_offset + user_table_size;
    uint64_t bitmap_offset = metadata_offset + metadata_size;
    uint64_t blocks_offset = bitmap_offset + ((num_blocks + 7) / 8);

    std::ofstream out(omni_path, std::ios::binary | std::ios::trunc);
    if (!out) return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
    out.seekp((std::streamoff)total_size - 1, std::ios::beg);
    char zero = 0;
    out.write(&zero, 1);
    out.flush();
    out.close();

    std::fstream f(omni_path, std::ios::in | std::ios::out | std::ios::binary);
    if (!f) return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);

    OMNIHeader header;
    std::memset(&header, 0, sizeof(header));
    std::memcpy(header.magic, "OMNIFS01", 8);
    header.format_version = 0x00010000;
    header.total_size = total_size;
    header.header_size = header_size;
    header.block_size = block_size;
    header.config_timestamp = (uint64_t)std::time(nullptr);
    header.user_table_offset = static_cast<uint32_t>(user_table_offset);
    header.max_users = max_users;
    header.file_state_storage_offset = static_cast<uint32_t>(metadata_offset);
    header.change_log_offset = static_cast<uint32_t>(bitmap_offset);
    uint64_t next_idx = 2;
    if (sizeof(header.reserved) >= 328) std::memcpy(header.reserved + 320, &next_idx, sizeof(next_idx));

    f.seekp(0, std::ios::beg);
    f.write(reinterpret_cast<const char*>(&header), sizeof(header));

    std::vector<UserInfo> users(max_users);
    std::string admin_h = placeholder_hash(cfg.admin_password);
    UserInfo admin(cfg.admin_username, admin_h, UserRole::ADMIN, (uint64_t)std::time(nullptr));
    users[0] = admin;
    for (uint32_t i = 1; i < max_users; ++i) users[i].is_active = 0;
    f.seekp(user_table_offset, std::ios::beg);
    f.write(reinterpret_cast<const char*>(users.data()), users.size() * sizeof(UserInfo));

    std::vector<MetaEntry> meta(max_files);
    if (meta.size() >= 1) {
        meta[0].valid = 0;
        meta[0].type = 1;
        meta[0].parent = 0;
        meta[0].set_name("root");
        meta[0].start_index = 0;
        meta[0].total_size = 0;
    }
    f.seekp(metadata_offset, std::ios::beg);
    f.write(reinterpret_cast<const char*>(meta.data()), meta.size() * sizeof(MetaEntry));

    std::vector<uint8_t> bitmap((num_blocks + 7) / 8, 0);
    f.seekp(bitmap_offset, std::ios::beg);
    f.write(reinterpret_cast<const char*>(bitmap.data()), bitmap.size());
    f.flush();
    f.close();
    return ofs_success();
}

int fs_shutdown(void* instance) {
    if (!instance) return ofs_err(OFSErrorCodes::ERROR_INVALID_SESSION);
    FSInstance* inst = reinterpret_cast<FSInstance*>(instance);
    persist_header(inst);
    persist_user_table(inst);
    persist_meta_entries(inst);
    persist_bitmap(inst);
    if (inst->file.is_open()) inst->file.close();
    delete inst;
    return ofs_success();
}

int fs_init(void** instance, const char* omni_path, const char* config_path) {


    if (!instance || !omni_path) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    std::fstream f(omni_path, std::ios::in | std::ios::out | std::ios::binary);
    bool need_format = false;
    if (!f) need_format = true;
    else {
        OMNIHeader header;
        f.seekg(0, std::ios::beg);
        f.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!f.good() || std::strncmp(header.magic, "OMNIFS01", 8) != 0) {
            f.close();
            need_format = true;
        } else {
            uint64_t metadata_offset = header.file_state_storage_offset;
            f.seekg(metadata_offset, std::ios::beg);
            MetaEntry root_meta;
            f.read(reinterpret_cast<char*>(&root_meta), sizeof(MetaEntry));
            if (!f.good() || root_meta.valid != 0 || root_meta.type != 1) {
                f.close();
                need_format = true;
            }
        }
    }
    if (need_format) {
        int fmt_res = fs_format(omni_path, config_path);
        if (fmt_res != ofs_success()) return fmt_res;
        f.open(omni_path, std::ios::in | std::ios::out | std::ios::binary);
        if (!f) return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
    }

    OMNIHeader header;
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!f.good() || std::strncmp(header.magic, "OMNIFS01", 8) != 0) {
        f.close();
        return ofs_err(OFSErrorCodes::ERROR_INVALID_CONFIG);
    }

    FSInstance* inst = new FSInstance(header.max_users ? header.max_users : 101);
    inst->header = header;
    inst->omni_path = omni_path;
    inst->file = std::move(f);

    uint64_t user_table_offset = header.user_table_offset;
    uint32_t max_users = header.max_users;
    uint64_t metadata_offset = header.file_state_storage_offset;
    uint64_t bitmap_offset = header.change_log_offset;

    inst->metadata_offset = static_cast<uint32_t>(metadata_offset);
    inst->bitmap_offset = static_cast<uint32_t>(bitmap_offset);

    inst->users.resize(max_users);
    inst->file.seekg(user_table_offset, std::ios::beg);
    inst->file.read(reinterpret_cast<char*>(inst->users.data()), max_users * sizeof(UserInfo));

    inst->user_index.clear();
    for (uint32_t i = 0; i < max_users; ++i) {
        if (inst->users[i].is_active) {
            std::string uname(inst->users[i].username);
            inst->user_index.insert(uname, (size_t)i);
        }
    }

    uint64_t metadata_size_bytes = bitmap_offset - metadata_offset;
    if (metadata_size_bytes % sizeof(MetaEntry) != 0) {
        delete inst;
        return ofs_err(OFSErrorCodes::ERROR_INVALID_CONFIG);
    }
    uint32_t meta_count = static_cast<uint32_t>(metadata_size_bytes / sizeof(MetaEntry));
    inst->max_files = meta_count;
    inst->meta_entries.resize(meta_count);
    inst->file.seekg(inst->metadata_offset, std::ios::beg);
    inst->file.read(reinterpret_cast<char*>(inst->meta_entries.data()), meta_count * sizeof(MetaEntry));
    uint64_t user_table_size = uint64_t(max_users) * sizeof(UserInfo);
    uint64_t metadata_size = uint64_t(meta_count) * sizeof(MetaEntry);
    uint64_t candidate_content = inst->header.total_size - inst->header.header_size - user_table_size - metadata_size;
    if (candidate_content < inst->header.block_size) {
        uint64_t file_end = 0;
        inst->file.seekg(0, std::ios::end);
        file_end = static_cast<uint64_t>(inst->file.tellg());
        candidate_content = file_end - (inst->metadata_offset + metadata_size);
    }
    uint64_t num_blocks = candidate_content / inst->header.block_size;
    if (num_blocks == 0) num_blocks = 1;
    inst->num_blocks = static_cast<uint32_t>(num_blocks);
    uint64_t bitmap_byte_count = (num_blocks + 7) / 8;
    inst->free_bitmap.resize(bitmap_byte_count, 0);
    inst->file.seekg(inst->bitmap_offset, std::ios::beg);
    inst->file.read(reinterpret_cast<char*>(inst->free_bitmap.data()), bitmap_byte_count);
    std::memcpy(inst->private_key, inst->header.reserved, 64);
    std::memcpy(inst->encoding_map, inst->header.reserved + 64, 256);
    uint64_t next_idx = 0;
    if (sizeof(inst->header.reserved) >= 328) std::memcpy(&next_idx, inst->header.reserved + 320, sizeof(next_idx));
    if (next_idx == 0) next_idx = 2;
    inst->next_meta_index = next_idx;

    inst->blocks_offset = static_cast<uint32_t>(inst->bitmap_offset + bitmap_byte_count);

    rebuild_path_index(inst);

    *instance = inst;
    g_fsinstance = inst;
    return ofs_success();}

static std::vector<uint32_t> get_block_chain(FSInstance* inst, uint32_t start_index) {
    std::vector<uint32_t> chain;
    if (!inst || start_index == 0) return chain;
    uint32_t cur = start_index;
    while (cur != 0) {
        chain.push_back(cur);
        uint32_t next = 0;
        std::vector<uint8_t> payload;
        if (!read_block(inst->file, inst->blocks_offset, static_cast<uint32_t>(inst->header.block_size), cur, next, payload)) {
            break;
        }
        cur = next;
        if (chain.size() > inst->num_blocks + 2) break;
    }
    return chain;
}
int file_truncate(void* session, const char* path_c, size_t new_size) {
    if (!session || !path_c) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    FSInstance* inst = s->inst;
    if (!inst) return ofs_err(OFSErrorCodes::ERROR_INVALID_SESSION);
    std::string path(path_c);
    const uint32_t* pMeta = inst->path_index.find(path);
    if (!pMeta) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    uint32_t meta_idx = *pMeta;
    if (meta_idx == 0 || meta_idx > inst->meta_entries.size()) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    MetaEntry& entry = inst->meta_entries[meta_idx - 1];
    if (entry.valid != 0) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    if (entry.type != 0) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION); // not a file
    uint32_t block_payload = static_cast<uint32_t>(inst->header.block_size - 4);
    uint32_t required_blocks = (new_size == 0) ? 0 : static_cast<uint32_t>((new_size + block_payload - 1) / block_payload);
    std::vector<uint32_t> chain = get_block_chain(inst, entry.start_index);
    uint32_t current_blocks = static_cast<uint32_t>(chain.size());
    if (required_blocks == current_blocks) {
        entry.total_size = new_size;
        entry.modified_time = (uint64_t)time(nullptr);
        persist_meta_entries(inst);
        persist_header(inst);
        return ofs_success();
    }

    if (required_blocks < current_blocks) {
        if (required_blocks == 0) {
            free_blocks(inst, chain);
            entry.start_index = 0;
        } else {
            std::vector<uint32_t> to_free(chain.begin() + required_blocks, chain.end());
            free_blocks(inst, to_free);
            uint32_t last_keep = chain[required_blocks - 1];
            uint64_t pos = (uint64_t)inst->blocks_offset + uint64_t(last_keep - 1) * inst->header.block_size;
            inst->file.seekp(pos, std::ios::beg);
            uint32_t zero = 0;
            inst->file.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
            inst->file.flush();
        }
        entry.total_size = new_size;
        entry.modified_time = (uint64_t)time(nullptr);
        persist_meta_entries(inst);
        persist_bitmap(inst);
        persist_header(inst);
        return ofs_success();
    }
    uint32_t need = required_blocks - current_blocks;
    std::vector<uint32_t> newblocks = allocate_blocks(inst, need);
    if (newblocks.size() != need) {
        return ofs_err(OFSErrorCodes::ERROR_NO_SPACE);
    }
    if (current_blocks == 0) {
        entry.start_index = newblocks[0];
    } else {
        uint32_t last = chain.back();
        uint64_t pos = (uint64_t)inst->blocks_offset + uint64_t(last - 1) * inst->header.block_size;
        inst->file.seekp(pos, std::ios::beg);
        uint32_t next_idx = newblocks[0];
        inst->file.write(reinterpret_cast<const char*>(&next_idx), sizeof(next_idx));
        inst->file.flush();
    }
    for (size_t i = 0; i < newblocks.size(); ++i) {
        uint32_t blk = newblocks[i];
        uint32_t next = (i + 1 < newblocks.size()) ? newblocks[i + 1] : 0;
        if (!write_block(inst->file, inst->blocks_offset, static_cast<uint32_t>(inst->header.block_size), blk, next, nullptr, 0)) {
            free_blocks(inst, newblocks);
            return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
        }
    }

    entry.total_size = new_size;
    entry.modified_time = (uint64_t)time(nullptr);
    if (inst->next_meta_index <= meta_idx) inst->next_meta_index = meta_idx + 1;
    persist_meta_entries(inst);
    persist_bitmap(inst);
    persist_header(inst);
    return ofs_success();
}
int file_exists(void* session, const char* path_c) {
    if (!session || !path_c) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    FSInstance* inst = s->inst;
    if (!inst) return ofs_err(OFSErrorCodes::ERROR_INVALID_SESSION);
    std::string path(path_c);
    const uint32_t* pm = inst->path_index.find(path);
    if (!pm) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    uint32_t meta_idx = *pm;
    if (meta_idx == 0 || meta_idx > inst->meta_entries.size()) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    const MetaEntry& me = inst->meta_entries[meta_idx - 1];
    if (me.valid != 0) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    if (me.type != 0) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION); 
    return ofs_success();
}
int file_rename(void* session, const char* old_path_c, const char* new_path_c) {
    if (!session || !old_path_c || !new_path_c) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    FSInstance* inst = s->inst;
    if (!inst) return ofs_err(OFSErrorCodes::ERROR_INVALID_SESSION);

    std::string old_path(old_path_c);
    std::string new_path(new_path_c);
    if (old_path.empty() || new_path.empty()) return ofs_err(OFSErrorCodes::ERROR_INVALID_PATH);
    const uint32_t* old_meta_p = inst->path_index.find(old_path);
    if (!old_meta_p) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    uint32_t old_meta_idx = *old_meta_p;
    if (old_meta_idx == 0 || old_meta_idx > inst->meta_entries.size()) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    MetaEntry& entry = inst->meta_entries[old_meta_idx - 1];
    if (entry.valid != 0) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    if (inst->path_index.contains(new_path)) return ofs_err(OFSErrorCodes::ERROR_FILE_EXISTS);
    std::string::size_type pos = new_path.find_last_of('/');
    std::string new_basename;
    std::string new_parent_path;
    if (pos == std::string::npos) {
        return ofs_err(OFSErrorCodes::ERROR_INVALID_PATH);
    } else if (pos == 0) {
        new_parent_path = "/";
        new_basename = new_path.substr(1);
    } else {
        new_parent_path = new_path.substr(0, pos);
        new_basename = new_path.substr(pos + 1);
    }
    if (new_basename.empty() || new_basename.size() > sizeof(entry.name) - 1) {
        return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    }
    const uint32_t* new_parent_meta_p = inst->path_index.find(new_parent_path);
    if (!new_parent_meta_p) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    uint32_t new_parent_idx = *new_parent_meta_p;
    if (new_parent_idx == 0 || new_parent_idx > inst->meta_entries.size()) return ofs_err(OFSErrorCodes::ERROR_NOT_FOUND);
    MetaEntry& new_parent = inst->meta_entries[new_parent_idx - 1];
    if (new_parent.valid != 0 || new_parent.type != 1) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    uint32_t old_parent_idx = entry.parent;
    if (old_parent_idx == 0 || old_parent_idx > inst->meta_entries.size()) return ofs_err(OFSErrorCodes::ERROR_INVALID_OPERATION);
    MetaEntry& old_parent = inst->meta_entries[old_parent_idx - 1];
    if (!dir_remove_child(inst, old_parent, old_meta_idx)) {
        return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
    }
    entry.set_name(new_basename);
    entry.parent = new_parent_idx;
    entry.modified_time = (uint64_t)time(nullptr);
    if (!dir_add_child(inst, new_parent, old_meta_idx)) {
        entry.set_name(entry.get_name().c_str());
        entry.parent = old_parent_idx;
        dir_add_child(inst, old_parent, old_meta_idx);
        return ofs_err(OFSErrorCodes::ERROR_IO_ERROR);
    }
    persist_meta_entries(inst);
    persist_header(inst);
    rebuild_path_index(inst);
    return ofs_success();
}


