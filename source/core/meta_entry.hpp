#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#pragma pack(push, 1)
struct MetaEntry {
    uint8_t valid;         
    uint8_t type;         
    uint32_t parent;      
    char name[12];         
    uint32_t start_index;  
    uint64_t total_size;  
    uint32_t owner_id;     
    uint32_t permissions; 
    uint64_t created_time; 
    uint64_t modified_time;
    uint8_t reserved[18]; 

    MetaEntry() {
        valid = 1; 
        type = 0;
        parent = 0;
        std::memset(name, 0, sizeof(name));
        start_index = 0;
        total_size = 0;
        owner_id = 0;
        permissions = 0644;
        created_time = 0;
        modified_time = 0;
        std::memset(reserved, 0, sizeof(reserved));
    }

    void set_name(const std::string &n) {
        std::memset(name, 0, sizeof(name));
        std::strncpy(name, n.c_str(), sizeof(name)-1);
    }

    std::string get_name() const { return std::string(name); }
};
#pragma pack(pop)

static_assert(sizeof(MetaEntry) == 72, "MetaEntry must be exactly 72 bytes");
