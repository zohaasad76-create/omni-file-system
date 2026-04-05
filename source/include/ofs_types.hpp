
#ifndef ODF_TYPES_HPP
#define ODF_TYPES_HPP

#include <cstdint>
#include <string>
#include <cstring>
#include <type_traits>
#include <ctime>
enum class UserRole : uint32_t {
    NORMAL = 0,
    ADMIN = 1
};
enum class OFSErrorCodes : int32_t {
    SUCCESS = 0,
    ERROR_NOT_FOUND = -1,
    ERROR_PERMISSION_DENIED = -2,
    ERROR_IO_ERROR = -3,
    ERROR_INVALID_PATH = -4,
    ERROR_FILE_EXISTS = -5,
    ERROR_NO_SPACE = -6,
    ERROR_INVALID_CONFIG = -7,
    ERROR_NOT_IMPLEMENTED = -8,
    ERROR_INVALID_SESSION = -9,
    ERROR_DIRECTORY_NOT_EMPTY = -10,
    ERROR_INVALID_OPERATION = -11
};
enum class EntryType : uint8_t {
    FILE = 0,
    DIRECTORY = 1
};
enum class FilePermissions : uint32_t {
    OWNER_READ = 0400,
    OWNER_WRITE = 0200,
    OWNER_EXECUTE = 0100,
    GROUP_READ = 0040,
    GROUP_WRITE = 0020,
    GROUP_EXECUTE = 0010,
    OTHERS_READ = 0004,
    OTHERS_WRITE = 0002,
    OTHERS_EXECUTE = 0001
};

struct FSInstance; 
#pragma pack(push, 1)
struct OMNIHeader {
    char magic[8];              
    uint32_t format_version;   
    uint64_t total_size;       
    uint64_t header_size;     
    uint64_t block_size;    
    char student_id[32];        
    char submission_date[16];   
    char config_hash[64];       
    uint64_t config_timestamp;  
    uint32_t user_table_offset; 
    uint32_t max_users;        

    uint32_t file_state_storage_offset; 
    uint32_t change_log_offset;     

    uint8_t reserved[340];      

    OMNIHeader() {
        std::memset(this, 0, sizeof(OMNIHeader));
    }
};

struct UserInfo {
    char username[32];         
    char password_hash[64];    
    UserRole role;              
    uint64_t created_time;     
    uint64_t last_login;        
    uint8_t is_active;          
    uint8_t reserved[11];     
    UserInfo() : role(UserRole::NORMAL), created_time(0), last_login(0), is_active(0) {
        std::memset(username, 0, sizeof(username));
        std::memset(password_hash, 0, sizeof(password_hash));
        std::memset(reserved, 0, sizeof(reserved));
    }
    UserInfo(const std::string& user, const std::string& hash, UserRole r, uint64_t created)
    : role(r), created_time(created), last_login(0), is_active(1) {
    std::memset(username, 0, sizeof(username));
    std::memset(password_hash, 0, sizeof(password_hash));
    std::strncpy(username, user.c_str(), sizeof(username) - 1);
    size_t copy_len = (hash.size() < sizeof(password_hash)) ? hash.size() : sizeof(password_hash);
    std::memcpy(password_hash, hash.data(), copy_len);
    std::memset(reserved, 0, sizeof(reserved));
}
};

struct FileEntry {
    char name[256];           
    uint8_t type;            
    uint64_t size;          
    uint32_t permissions;   
    uint64_t created_time;    
    uint64_t modified_time;    
    char owner[32];           
    uint32_t inode;          
    uint8_t reserved[95];      

    FileEntry() : type(0), size(0), permissions(0), created_time(0), modified_time(0), inode(0) {
        std::memset(name, 0, sizeof(name));
        std::memset(owner, 0, sizeof(owner));
        std::memset(reserved, 0, sizeof(reserved));
    }

    FileEntry(const std::string& filename, EntryType entry_type, uint64_t file_size,
              uint32_t perms, const std::string& file_owner, uint32_t file_inode)
        : type(static_cast<uint8_t>(entry_type)), size(file_size), permissions(perms),
          created_time(0), modified_time(0), inode(file_inode) {
        std::memset(name, 0, sizeof(name));
        std::memset(owner, 0, sizeof(owner));
        std::strncpy(name, filename.c_str(), sizeof(name) - 1);
        std::strncpy(owner, file_owner.c_str(), sizeof(owner) - 1);
        std::memset(reserved, 0, sizeof(reserved));
    }

    EntryType getType() const { return static_cast<EntryType>(type); }
    void setType(EntryType entry_type) { type = static_cast<uint8_t>(entry_type); }
};

#pragma pack(pop)
struct FileMetadata {
    char path[512];
    FileEntry entry;
    uint64_t blocks_used;
    uint64_t actual_size;
    uint8_t reserved[64];

    FileMetadata() : blocks_used(0), actual_size(0) {
        std::memset(path, 0, sizeof(path));
        std::memset(&entry, 0, sizeof(entry));
        std::memset(reserved, 0, sizeof(reserved));
    }

    FileMetadata(const std::string& file_path, const FileEntry& file_entry)
        : entry(file_entry), blocks_used(0), actual_size(0) {
        std::strncpy(path, file_path.c_str(), sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        std::memset(reserved, 0, sizeof(reserved));
    }
};

struct SessionInfo {
    char session_id[64];
    UserInfo user;
    uint64_t login_time;
    uint64_t last_activity;
    uint32_t operations_count;
    uint8_t reserved[32];
    FSInstance* inst;
    SessionInfo() : login_time(0), last_activity(0), operations_count(0), inst(nullptr) {
        std::memset(session_id, 0, sizeof(session_id));
        std::memset(&user, 0, sizeof(user));
        std::memset(reserved, 0, sizeof(reserved));
    }
    SessionInfo(const std::string& id, const UserInfo& user_info, uint64_t login, FSInstance* instance = nullptr)
        : user(user_info), login_time(login), last_activity(login), operations_count(0), inst(instance) {
        std::memset(session_id, 0, sizeof(session_id));
        std::strncpy(session_id, id.c_str(), sizeof(session_id) - 1);
        std::memset(reserved, 0, sizeof(reserved));
    }
};
struct FSStats {
    uint64_t total_size;
    uint64_t used_space;
    uint64_t free_space;
    uint32_t total_files;
    uint32_t total_directories;
    uint32_t total_users;
    uint32_t active_sessions;
    double fragmentation;
    uint8_t reserved[64];

    FSStats() : total_size(0), used_space(0), free_space(0),
                total_files(0), total_directories(0),
                total_users(0), active_sessions(0), fragmentation(0.0) {
        std::memset(reserved, 0, sizeof(reserved));
    }
};
static_assert(sizeof(OMNIHeader) == 512, "OMNIHeader must be exactly 512 bytes");
static_assert(sizeof(UserInfo) == 128, "UserInfo must be exactly 128 bytes");
static_assert(sizeof(FileEntry) == 416, "FileEntry must be exactly 416 bytes");

#endif 
