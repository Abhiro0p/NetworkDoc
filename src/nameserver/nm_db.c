#include "../../include/nameserver.h"

int init_database() {
    int rc = sqlite3_open("data/nameserver.db", &server_state.db);
    if (rc != SQLITE_OK) {
        log_message("NameServer", "Failed to open database");
        return -1;
    }
    
    const char* sqls[] = {
        "CREATE TABLE IF NOT EXISTS files ("
        "filename TEXT PRIMARY KEY, "
        "owner TEXT NOT NULL, "
        "storage_server_id INTEGER, "
        "replica_server_id INTEGER, "
        "word_count INTEGER DEFAULT 0, "
        "char_count INTEGER DEFAULT 0, "
        "sentence_count INTEGER DEFAULT 0, "
        "created_at INTEGER, "
        "modified_at INTEGER, "
        "accessed_at INTEGER, "
        "is_folder INTEGER DEFAULT 0"
        ");",
        
        "CREATE TABLE IF NOT EXISTS access_control ("
        "filename TEXT, "
        "username TEXT, "
        "permissions INTEGER, "
        "PRIMARY KEY (filename, username)"
        ");",
        
        "CREATE TABLE IF NOT EXISTS checkpoints ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "filename TEXT, "
        "tag TEXT, "
        "content TEXT, "
        "created_at INTEGER"
        ");",
        
        "CREATE TABLE IF NOT EXISTS undo_history ("
        "filename TEXT PRIMARY KEY, "
        "content TEXT, "
        "username TEXT, "
        "timestamp INTEGER"
        ");",
        
        "CREATE TABLE IF NOT EXISTS access_requests ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "filename TEXT, "
        "requester TEXT, "
        "access_type INTEGER, "
        "requested_at INTEGER, "
        "status TEXT DEFAULT 'pending'"
        ");"
    };
    
    for (int i = 0; i < 5; i++) {
        char* err_msg = NULL;
        rc = sqlite3_exec(server_state.db, sqls[i], 0, 0, &err_msg);
        if (rc != SQLITE_OK) {
            log_message("NameServer", err_msg);
            sqlite3_free(err_msg);
        }
    }
    
    return 0;
}

int load_files_from_db() {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT filename FROM files;";
    
    if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* filename = (const char*)sqlite3_column_text(stmt, 0);
        trie_insert(server_state.file_trie, filename);
        count++;
    }
    
    sqlite3_finalize(stmt);
    
    char log_buf[128];
    snprintf(log_buf, sizeof(log_buf), "Loaded %d files from database", count);
    log_message("NameServer", log_buf);
    
    return 0;
}

int check_permission(const char* username, const char* filename, int required_perm) {
    sqlite3_stmt* stmt;
    
    // Check if user is owner (owners have all permissions)
    const char* sql = "SELECT owner FROM files WHERE filename = ?;";
    if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* owner = (const char*)sqlite3_column_text(stmt, 0);
            if (strcmp(owner, username) == 0) {
                sqlite3_finalize(stmt);
                return 1;
            }
        }
        sqlite3_finalize(stmt);
    }
    
    // Check access control list
    sql = "SELECT permissions FROM access_control WHERE filename = ? AND username = ?;";
    if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int perms = sqlite3_column_int(stmt, 0);
            int has_perm = (perms & required_perm) == required_perm;
            sqlite3_finalize(stmt);
            return has_perm;
        }
        sqlite3_finalize(stmt);
    }
    
    return 0;
}

StorageServerInfo* get_ss_by_id(int ss_id) {
    for (int i = 0; i < server_state.ss_count; i++) {
        if (server_state.storage_servers[i].id == ss_id && 
            server_state.storage_servers[i].is_alive) {
            return &server_state.storage_servers[i];
        }
    }
    return NULL;
}

void release_lock(const char* filename, int sentence_num, const char* username, int client_socket) {
    for (int i = 0; i < server_state.lock_count; i++) {
        if (strcmp(server_state.locks[i].filename, filename) == 0 &&
            server_state.locks[i].sentence_number == sentence_num &&
            strcmp(server_state.locks[i].username, username) == 0 &&
            server_state.locks[i].client_socket == client_socket) {
            
            // Remove lock by shifting remaining locks
            for (int j = i; j < server_state.lock_count - 1; j++) {
                server_state.locks[j] = server_state.locks[j + 1];
            }
            server_state.lock_count--;
            
            char log_buf[256];
            snprintf(log_buf, sizeof(log_buf), "Lock released: %s sentence %d by %s", 
                    filename, sentence_num, username);
            log_message("NameServer", log_buf);
            break;
        }
    }
}
