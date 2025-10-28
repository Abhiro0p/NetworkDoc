#include "../../include/nameserver.h"

void handle_addaccess(int sock, Message* msg) {
    pthread_mutex_lock(&server_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    // Parse: filename|username|permissions (1=read, 2=write)
    char target_user[MAX_USERNAME];
    int permissions = 0;
    
    if (sscanf(msg->data, "%[^|]|%d", target_user, &permissions) != 2) {
        set_message_error(&resp, ERR_INVALID_PARAM, "Invalid format");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    if (!trie_search(server_state.file_trie, msg->filename)) {
        set_message_error(&resp, ERR_FILE_NOT_FOUND, "File not found");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    // Check if requester is owner
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(server_state.db, "SELECT owner FROM files WHERE filename = ?;", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
    
    int is_owner = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* owner = (const char*)sqlite3_column_text(stmt, 0);
        is_owner = (strcmp(owner, msg->username) == 0);
    }
    sqlite3_finalize(stmt);
    
    if (!is_owner) {
        set_message_error(&resp, ERR_NOT_OWNER, "Only owner can grant access");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    // Check if target user exists
    int user_exists = 0;
    for (int i = 0; i < server_state.user_count; i++) {
        if (strcmp(server_state.users[i].username, target_user) == 0) {
            user_exists = 1;
            break;
        }
    }
    
    if (!user_exists) {
        set_message_error(&resp, ERR_USER_NOT_FOUND, "Target user not found");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    // Insert or update access control
    const char* sql = "INSERT OR REPLACE INTO access_control (filename, username, permissions) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, target_user, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, permissions);
        
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.error_code = ERR_SUCCESS;
            snprintf(resp.data, sizeof(resp.data), "Access granted to %s", target_user);
            
            char log_buf[256];
            snprintf(log_buf, sizeof(log_buf), "Access granted: %s to %s with perms %d by %s",
                    msg->filename, target_user, permissions, msg->username);
            log_message("NameServer", log_buf);
        } else {
            set_message_error(&resp, ERR_SERVER_ERROR, "Failed to grant access");
        }
        sqlite3_finalize(stmt);
    } else {
        set_message_error(&resp, ERR_SERVER_ERROR, "Database error");
    }
    
    send_message(sock, &resp);
    pthread_mutex_unlock(&server_state.mutex);
}

void handle_remaccess(int sock, Message* msg) {
    pthread_mutex_lock(&server_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    // Parse: username
    char target_user[MAX_USERNAME];
    strncpy(target_user, msg->data, MAX_USERNAME - 1);
    target_user[MAX_USERNAME - 1] = '\0';
    trim(target_user);
    
    if (!trie_search(server_state.file_trie, msg->filename)) {
        set_message_error(&resp, ERR_FILE_NOT_FOUND, "File not found");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    // Check if requester is owner
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(server_state.db, "SELECT owner FROM files WHERE filename = ?;", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
    
    int is_owner = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* owner = (const char*)sqlite3_column_text(stmt, 0);
        is_owner = (strcmp(owner, msg->username) == 0);
    }
    sqlite3_finalize(stmt);
    
    if (!is_owner) {
        set_message_error(&resp, ERR_NOT_OWNER, "Only owner can revoke access");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    // Delete access control entry
    const char* sql = "DELETE FROM access_control WHERE filename = ? AND username = ?;";
    if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, target_user, -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.error_code = ERR_SUCCESS;
            snprintf(resp.data, sizeof(resp.data), "Access revoked from %s", target_user);
            
            char log_buf[256];
            snprintf(log_buf, sizeof(log_buf), "Access revoked: %s from %s by %s",
                    msg->filename, target_user, msg->username);
            log_message("NameServer", log_buf);
        } else {
            set_message_error(&resp, ERR_SERVER_ERROR, "Failed to revoke access");
        }
        sqlite3_finalize(stmt);
    } else {
        set_message_error(&resp, ERR_SERVER_ERROR, "Database error");
    }
    
    send_message(sock, &resp);
    pthread_mutex_unlock(&server_state.mutex);
}

void handle_undo(int sock, Message* msg) {
    pthread_mutex_lock(&server_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    if (!trie_search(server_state.file_trie, msg->filename)) {
        set_message_error(&resp, ERR_FILE_NOT_FOUND, "File not found");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    if (!check_permission(msg->username, msg->filename, ACCESS_WRITE)) {
        set_message_error(&resp, ERR_PERMISSION_DENIED, "No write permission");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    // Get SS info
    sqlite3_stmt* stmt;
    const char* sql = "SELECT storage_server_id, replica_server_id FROM files WHERE filename = ?;";
    
    if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int ss_id = sqlite3_column_int(stmt, 0);
            int replica_id = sqlite3_column_int(stmt, 1);
            
            StorageServerInfo* ss = get_ss_by_id(ss_id);
            StorageServerInfo* replica = replica_id >= 0 ? get_ss_by_id(replica_id) : NULL;
            
            if (ss) {
                resp.error_code = ERR_SUCCESS;
                snprintf(resp.data, sizeof(resp.data), "SS:%s:%d", ss->ip, ss->port);
                if (replica) {
                    char replica_info[128];
                    snprintf(replica_info, sizeof(replica_info), "|REPLICA:%s:%d", replica->ip, replica->port);
                    strncat(resp.data, replica_info, sizeof(resp.data) - strlen(resp.data) - 1);
                }
                
                char log_buf[256];
                snprintf(log_buf, sizeof(log_buf), "Undo requested: %s by %s", msg->filename, msg->username);
                log_message("NameServer", log_buf);
            } else {
                set_message_error(&resp, ERR_SS_NOT_FOUND, "Storage server not available");
            }
        } else {
            set_message_error(&resp, ERR_FILE_NOT_FOUND, "File not found");
        }
        sqlite3_finalize(stmt);
    } else {
        set_message_error(&resp, ERR_SERVER_ERROR, "Database error");
    }
    
    send_message(sock, &resp);
    pthread_mutex_unlock(&server_state.mutex);
}

void handle_exec(int sock, Message* msg) {
    pthread_mutex_lock(&server_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    if (!trie_search(server_state.file_trie, msg->filename)) {
        set_message_error(&resp, ERR_FILE_NOT_FOUND, "File not found");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    if (!check_permission(msg->username, msg->filename, ACCESS_READ)) {
        set_message_error(&resp, ERR_PERMISSION_DENIED, "No read permission");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    // First get SS info to read file content
    sqlite3_stmt* stmt;
    const char* sql = "SELECT storage_server_id FROM files WHERE filename = ?;";
    
    if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int ss_id = sqlite3_column_int(stmt, 0);
            StorageServerInfo* ss = get_ss_by_id(ss_id);
            
            if (ss) {
                // Return SS info so client can fetch content and execute
                resp.error_code = ERR_SUCCESS;
                snprintf(resp.data, sizeof(resp.data), "SS:%s:%d", ss->ip, ss->port);
                
                char log_buf[256];
                snprintf(log_buf, sizeof(log_buf), "Exec requested: %s by %s", msg->filename, msg->username);
                log_message("NameServer", log_buf);
            } else {
                set_message_error(&resp, ERR_SS_NOT_FOUND, "Storage server not available");
            }
        } else {
            set_message_error(&resp, ERR_FILE_NOT_FOUND, "File not found");
        }
        sqlite3_finalize(stmt);
    } else {
        set_message_error(&resp, ERR_SERVER_ERROR, "Database error");
    }
    
    send_message(sock, &resp);
    pthread_mutex_unlock(&server_state.mutex);
}

void handle_createfolder(int sock, Message* msg) {
    pthread_mutex_lock(&server_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    if (trie_search(server_state.file_trie, msg->filename)) {
        set_message_error(&resp, ERR_FILE_EXISTS, "Folder already exists");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    if (server_state.ss_count == 0) {
        set_message_error(&resp, ERR_SS_NOT_FOUND, "No storage servers available");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    // Select SS with least files
    int ss_idx = 0;
    for (int i = 1; i < server_state.ss_count; i++) {
        if (server_state.storage_servers[i].file_count < server_state.storage_servers[ss_idx].file_count &&
            server_state.storage_servers[i].is_alive) {
            ss_idx = i;
        }
    }
    
    StorageServerInfo* ss = &server_state.storage_servers[ss_idx];
    
    // Insert into database as folder
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO files (filename, owner, storage_server_id, created_at, modified_at, accessed_at, is_folder) "
                     "VALUES (?, ?, ?, ?, ?, ?, 1);";
    
    if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        time_t now = time(NULL);
        sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, msg->username, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, ss->id);
        sqlite3_bind_int64(stmt, 4, now);
        sqlite3_bind_int64(stmt, 5, now);
        sqlite3_bind_int64(stmt, 6, now);
        
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            trie_insert(server_state.file_trie, msg->filename);
            ss->file_count++;
            
            resp.error_code = ERR_SUCCESS;
            snprintf(resp.data, sizeof(resp.data), "Folder created: %s", msg->filename);
            
            char log_buf[256];
            snprintf(log_buf, sizeof(log_buf), "Folder created: %s by %s", msg->filename, msg->username);
            log_message("NameServer", log_buf);
        } else {
            set_message_error(&resp, ERR_SERVER_ERROR, "Failed to create folder");
        }
        sqlite3_finalize(stmt);
    } else {
        set_message_error(&resp, ERR_SERVER_ERROR, "Database error");
    }
    
    send_message(sock, &resp);
    pthread_mutex_unlock(&server_state.mutex);
}

void handle_checkpoint(int sock, Message* msg) {
    pthread_mutex_lock(&server_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    // Parse command: CREATE|tag or LIST or REVERT|tag
    char cmd[32], tag[64];
    if (sscanf(msg->data, "%[^|]|%s", cmd, tag) < 1) {
        set_message_error(&resp, ERR_INVALID_PARAM, "Invalid checkpoint command");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    if (!trie_search(server_state.file_trie, msg->filename)) {
        set_message_error(&resp, ERR_FILE_NOT_FOUND, "File not found");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    if (!check_permission(msg->username, msg->filename, ACCESS_READ)) {
        set_message_error(&resp, ERR_PERMISSION_DENIED, "No read permission");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    // Get SS info
    sqlite3_stmt* stmt;
    const char* sql = "SELECT storage_server_id, replica_server_id FROM files WHERE filename = ?;";
    
    if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int ss_id = sqlite3_column_int(stmt, 0);
            int replica_id = sqlite3_column_int(stmt, 1);
            
            StorageServerInfo* ss = get_ss_by_id(ss_id);
            StorageServerInfo* replica = replica_id >= 0 ? get_ss_by_id(replica_id) : NULL;
            
            if (ss) {
                resp.error_code = ERR_SUCCESS;
                snprintf(resp.data, sizeof(resp.data), "SS:%s:%d|CMD:%s", ss->ip, ss->port, msg->data);
                if (replica) {
                    char replica_info[128];
                    snprintf(replica_info, sizeof(replica_info), "|REPLICA:%s:%d", replica->ip, replica->port);
                    strncat(resp.data, replica_info, sizeof(resp.data) - strlen(resp.data) - 1);
                }
            } else {
                set_message_error(&resp, ERR_SS_NOT_FOUND, "Storage server not available");
            }
        } else {
            set_message_error(&resp, ERR_FILE_NOT_FOUND, "File not found");
        }
        sqlite3_finalize(stmt);
    } else {
        set_message_error(&resp, ERR_SERVER_ERROR, "Database error");
    }
    
    send_message(sock, &resp);
    pthread_mutex_unlock(&server_state.mutex);
}

void handle_request_access(int sock, Message* msg) {
    pthread_mutex_lock(&server_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    // Parse: REQUEST|access_type or VIEWREQUESTS or APPROVE|requester or REJECT|requester
    char cmd[32], param[MAX_USERNAME];
    int access_type = ACCESS_READ;
    
    if (sscanf(msg->data, "%[^|]|%s", cmd, param) < 1) {
        set_message_error(&resp, ERR_INVALID_PARAM, "Invalid request format");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    if (strcmp(cmd, "REQUEST") == 0) {
        if (strlen(param) > 0) {
            access_type = atoi(param);
        }
        
        if (!trie_search(server_state.file_trie, msg->filename)) {
            set_message_error(&resp, ERR_FILE_NOT_FOUND, "File not found");
            send_message(sock, &resp);
            pthread_mutex_unlock(&server_state.mutex);
            return;
        }
        
        // Insert access request
        sqlite3_stmt* stmt;
        const char* sql = "INSERT INTO access_requests (filename, requester, access_type, requested_at) VALUES (?, ?, ?, ?);";
        
        if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, msg->username, -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 3, access_type);
            sqlite3_bind_int64(stmt, 4, time(NULL));
            
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                resp.error_code = ERR_SUCCESS;
                strcpy(resp.data, "Access request submitted");
            } else {
                set_message_error(&resp, ERR_SERVER_ERROR, "Failed to submit request");
            }
            sqlite3_finalize(stmt);
        }
    }
    else if (strcmp(cmd, "VIEWREQUESTS") == 0) {
        // Show pending requests for files owned by this user
        sqlite3_stmt* stmt;
        const char* sql = "SELECT ar.filename, ar.requester, ar.access_type, ar.requested_at "
                         "FROM access_requests ar JOIN files f ON ar.filename = f.filename "
                         "WHERE f.owner = ? AND ar.status = 'pending';";
        
        if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, msg->username, -1, SQLITE_STATIC);
            
            char result[BUFFER_SIZE] = "Pending Access Requests:\n";
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* filename = (const char*)sqlite3_column_text(stmt, 0);
                const char* requester = (const char*)sqlite3_column_text(stmt, 1);
                int atype = sqlite3_column_int(stmt, 2);
                
                char line[256];
                snprintf(line, sizeof(line), "  %s requests %s access to %s\n", 
                        requester, atype == ACCESS_READ ? "READ" : "WRITE", filename);
                if (strlen(result) + strlen(line) < sizeof(result) - 1) {
                    strcat(result, line);
                }
            }
            
            resp.error_code = ERR_SUCCESS;
            strncpy(resp.data, result, sizeof(resp.data) - 1);
            sqlite3_finalize(stmt);
        }
    }
    
    send_message(sock, &resp);
    pthread_mutex_unlock(&server_state.mutex);
}
