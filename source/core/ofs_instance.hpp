
#pragma once
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include "../include/ofs_types.hpp"
#include "../data_structures/simple_unordered_map.hpp"
#include "meta_entry.hpp"

struct FSInstance {
    OMNIHeader header;
    std::string omni_path;
    std::fstream file;

    std::vector<UserInfo> users;
    SimpleHashMap<size_t> user_index;
    std::vector<MetaEntry> meta_entries;
    std::vector<uint8_t> free_bitmap;

    uint8_t encoding_map[256];
    uint8_t private_key[64];

    SimpleHashMap<uint32_t> path_index;
    SimpleHashMap<std::shared_ptr<SessionInfo>> sessions;

    uint32_t max_files;
    uint32_t num_blocks;
    uint32_t blocks_offset;
    uint32_t bitmap_offset;
    uint32_t metadata_offset;
    uint64_t next_meta_index;

    FSInstance(uint32_t max_users_hint = 101)
        : user_index(101), path_index(1009), sessions(409),
          max_files(0), num_blocks(0),
          blocks_offset(0), bitmap_offset(0), metadata_offset(0), next_meta_index(2) {
        std::memset(encoding_map, 0, sizeof(encoding_map));
        std::memset(private_key, 0, sizeof(private_key));
    }
};



