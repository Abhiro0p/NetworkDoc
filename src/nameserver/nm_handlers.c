#include "../../include/nameserver.h"

void handle_register_ss(int sock, Message* msg) {
    pthread_mutex_lock(&server_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    if (server_state.ss_count >= MAX_STORAGE_SERVERS) {
        set_message_error(&resp, ERR_SERVER_ERROR, "Max storage servers reached");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    char ip[INET_ADDRSTRLEN];
    int port;
    sscanf(msg->data, "%[^:]:%d", ip, &port);
    
    int ss_id = server_state.next_ss_id++;
    StorageServerInfo* ss = &server_state.storage_servers[server_state.ss_count++];
    ss->id = ss_id;
    strncpy(ss->ip, ip, INET_ADDRSTRLEN - 1);
    ss->port = port;
    ss->is_alive = 1;
    ss->last_heartbeat = time(NULL);
    ss->file_count = 0;
    
    resp.error_code = ERR_SUCCESS;
    snprintf(resp.data, sizeof(resp.data), "SS_ID:%d", ss_id);
    send_message(sock, &resp);
    
    char log_buf[256];
    snprintf(log_buf, sizeof(log_buf), "Storage Server registered: ID=%d, %s:%d", ss_id, ip, port);
    log_message("NameServer", log_buf);
    
    pthread_mutex_unlock(&server_state.mutex);
}

void handle_register_client(int sock, Message* msg) {
    pthread_mutex_lock(&server_state.mutex);
    
    int exists = 0;
    for (int i = 0; i < server_state.user_count; i++) {
        if (strcmp(server_state.users[i].username, msg->username) == 0) {
            exists = 1;
            break;
        }
    }
    
    if (!exists && server_state.user_count < MAX_USERS) {
        UserInfo* user = &server_state.users[server_state.user_count++];
        strncpy(user->username, msg->username, MAX_USERNAME - 1);
        user->registered_at = time(NULL);
        
        char log_buf[256];
        snprintf(log_buf, sizeof(log_buf), "Client registered: %s", msg->username);
        log_message("NameServer", log_buf);
    }
    
    Message resp;
    init_message(&resp);
    resp.error_code = ERR_SUCCESS;
    strcpy(resp.data, "Registered successfully");
    send_message(sock, &resp);
    
    pthread_mutex_unlock(&server_state.mutex);
}

void handle_create(int sock, Message* msg) {
    pthread_mutex_lock(&server_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    if (trie_search(server_state.file_trie, msg->filename)) {
        set_message_error(&resp, ERR_FILE_EXISTS, "File already exists");
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
    
    // Select SS with least files (load balancing)
    int ss_idx = 0;
    for (int i = 1; i < server_state.ss_count; i++) {
        if (server_state.storage_servers[i].file_count < server_state.storage_servers[ss_idx].file_count &&
            server_state.storage_servers[i].is_alive) {
            ss_idx = i;
        }
    }
    
    StorageServerInfo* ss = &server_state.storage_servers[ss_idx];
    
    // Select replica server (different from primary)
    int replica_idx = -1;
    for (int i = 0; i < server_state.ss_count; i++) {
        if (i != ss_idx && server_state.storage_servers[i].is_alive) {
            replica_idx = i;
            break;
        }
    }
    
    // Insert into database
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO files (filename, owner, storage_server_id, replica_server_id, "
                     "created_at, modified_at, accessed_at) VALUES (?, ?, ?, ?, ?, ?, ?);";
    
    if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        time_t now = time(NULL);
        sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, msg->username, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, ss->id);
        sqlite3_bind_int(stmt, 4, replica_idx >= 0 ? server_state.storage_servers[replica_idx].id : -1);
        sqlite3_bind_int64(stmt, 5, now);
        sqlite3_bind_int64(stmt, 6, now);
        sqlite3_bind_int64(stmt, 7, now);
        
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            trie_insert(server_state.file_trie, msg->filename);
            ss->file_count++;
            
            resp.error_code = ERR_SUCCESS;
            snprintf(resp.data, sizeof(resp.data), "SS:%s:%d", ss->ip, ss->port);
            if (replica_idx >= 0) {
                char replica_info[128];
                snprintf(replica_info, sizeof(replica_info), "|REPLICA:%s:%d",
                        server_state.storage_servers[replica_idx].ip,
                        server_state.storage_servers[replica_idx].port);
                strncat(resp.data, replica_info, sizeof(resp.data) - strlen(resp.data) - 1);
            }
            
            char log_buf[512];
            snprintf(log_buf, sizeof(log_buf), "File created: %s by %s on SS%d", 
                    msg->filename, msg->username, ss->id);
            log_message("NameServer", log_buf);
        } else {
            set_message_error(&resp, ERR_SERVER_ERROR, "Failed to create file metadata");
        }
        sqlite3_finalize(stmt);
    } else {
        set_message_error(&resp, ERR_SERVER_ERROR, "Database error");
    }
    
    send_message(sock, &resp);
    pthread_mutex_unlock(&server_state.mutex);
}

void handle_read(int sock, Message* msg) {
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
    
    // Get SS info from database
    sqlite3_stmt* stmt;
    const char* sql = "SELECT storage_server_id, replica_server_id FROM files WHERE filename = ?;";
    
    if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int ss_id = sqlite3_column_int(stmt, 0);
            int replica_id = sqlite3_column_int(stmt, 1);
            
            StorageServerInfo* ss = get_ss_by_id(ss_id);
            if (!ss && replica_id >= 0) {
                ss = get_ss_by_id(replica_id);
            }
            
            if (ss) {
                resp.error_code = ERR_SUCCESS;
                snprintf(resp.data, sizeof(resp.data), "SS:%s:%d", ss->ip, ss->port);
            } else {
                set_message_error(&resp, ERR_SS_NOT_FOUND, "Storage server not available");
            }
        } else {
            set_message_error(&resp, ERR_FILE_NOT_FOUND, "File metadata not found");
        }
        sqlite3_finalize(stmt);
    } else {
        set_message_error(&resp, ERR_SERVER_ERROR, "Database error");
    }
    
    send_message(sock, &resp);
    
    // Update accessed_at timestamp
    sql = "UPDATE files SET accessed_at = ? WHERE filename = ?;";
    if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, time(NULL));
        sqlite3_bind_text(stmt, 2, msg->filename, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    pthread_mutex_unlock(&server_state.mutex);
}

void handle_write(int sock, Message* msg) {
    pthread_mutex_lock(&server_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    int sentence_num;
    if (sscanf(msg->data, "%d", &sentence_num) != 1) {
        set_message_error(&resp, ERR_INVALID_PARAM, "Invalid sentence number");
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
    
    if (!check_permission(msg->username, msg->filename, ACCESS_WRITE)) {
        set_message_error(&resp, ERR_PERMISSION_DENIED, "No write permission");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    // Check if sentence is already locked
    int lock_already_held = 0;
    for (int i = 0; i < server_state.lock_count; i++) {
        if (strcmp(server_state.locks[i].filename, msg->filename) == 0 &&
            server_state.locks[i].sentence_number == sentence_num) {
            // Check if it's the same client (same socket)
            if (server_state.locks[i].client_socket == sock) {
                // Same client re-acquiring lock
                lock_already_held = 1;
                break;
            } else {
                // Different client (even if same username)
                char err_buf[256];
                snprintf(err_buf, sizeof(err_buf), "Sentence %d locked by %s (different session)", 
                        sentence_num, server_state.locks[i].username);
                set_message_error(&resp, ERR_LOCKED, err_buf);
                send_message(sock, &resp);
                pthread_mutex_unlock(&server_state.mutex);
                return;
            }
        }
    }
    
    // Acquire new lock if not already held
    if (!lock_already_held) {
        if (server_state.lock_count < MAX_LOCKS) {
            SentenceLock* lock = &server_state.locks[server_state.lock_count++];
            strncpy(lock->filename, msg->filename, MAX_FILENAME - 1);
            lock->sentence_number = sentence_num;
            strncpy(lock->username, msg->username, MAX_USERNAME - 1);
            lock->client_socket = sock;  // Track specific client connection
            lock->locked_at = time(NULL);
            
            char log_buf[256];
            snprintf(log_buf, sizeof(log_buf), "Lock acquired: %s sentence %d by %s",
                    msg->filename, sentence_num, msg->username);
            log_message("NameServer", log_buf);
        } else {
            set_message_error(&resp, ERR_SERVER_ERROR, "Lock table full");
            send_message(sock, &resp);
            pthread_mutex_unlock(&server_state.mutex);
            return;
        }
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
                snprintf(resp.data, sizeof(resp.data), "SS:%s:%d|SENTENCE:%d", ss->ip, ss->port, sentence_num);
                if (replica) {
                    char replica_info[128];
                    snprintf(replica_info, sizeof(replica_info), "|REPLICA:%s:%d", replica->ip, replica->port);
                    strncat(resp.data, replica_info, sizeof(resp.data) - strlen(resp.data) - 1);
                }
            } else {
                set_message_error(&resp, ERR_SS_NOT_FOUND, "Storage server not available");
            }
        } else {
            set_message_error(&resp, ERR_FILE_NOT_FOUND, "File metadata not found");
        }
        sqlite3_finalize(stmt);
    } else {
        set_message_error(&resp, ERR_SERVER_ERROR, "Database error");
    }
    
    send_message(sock, &resp);
    pthread_mutex_unlock(&server_state.mutex);
}

void handle_delete(int sock, Message* msg) {
    pthread_mutex_lock(&server_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    if (!trie_search(server_state.file_trie, msg->filename)) {
        set_message_error(&resp, ERR_FILE_NOT_FOUND, "File not found");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    // Check if user is owner
    sqlite3_stmt* stmt;
    const char* sql = "SELECT owner, storage_server_id, replica_server_id FROM files WHERE filename = ?;";
    
    if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        set_message_error(&resp, ERR_SERVER_ERROR, "Database error");
        send_message(sock, &resp);
        pthread_mutex_unlock(&server_state.mutex);
        return;
    }
    
    sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
    
    int ss_id = -1, replica_id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* owner = (const char*)sqlite3_column_text(stmt, 0);
        if (strcmp(owner, msg->username) != 0) {
            set_message_error(&resp, ERR_NOT_OWNER, "Only owner can delete file");
            send_message(sock, &resp);
            sqlite3_finalize(stmt);
            pthread_mutex_unlock(&server_state.mutex);
            return;
        }
        ss_id = sqlite3_column_int(stmt, 1);
        replica_id = sqlite3_column_int(stmt, 2);
    }
    sqlite3_finalize(stmt);
    
    // Delete from database
    sql = "DELETE FROM files WHERE filename = ?;";
    if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    sql = "DELETE FROM access_control WHERE filename = ?;";
    if (sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    trie_delete(server_state.file_trie, msg->filename);
    
    // Decrease file count
    for (int i = 0; i < server_state.ss_count; i++) {
        if (server_state.storage_servers[i].id == ss_id) {
            server_state.storage_servers[i].file_count--;
            break;
        }
    }
    
    // Get SS info for deletion
    StorageServerInfo* ss = get_ss_by_id(ss_id);
    StorageServerInfo* replica = replica_id >= 0 ? get_ss_by_id(replica_id) : NULL;
    
    resp.error_code = ERR_SUCCESS;
    if (ss) {
        snprintf(resp.data, sizeof(resp.data), "SS:%s:%d", ss->ip, ss->port);
        if (replica) {
            char replica_info[128];
            snprintf(replica_info, sizeof(replica_info), "|REPLICA:%s:%d", replica->ip, replica->port);
            strncat(resp.data, replica_info, sizeof(resp.data) - strlen(resp.data) - 1);
        }
    }
    send_message(sock, &resp);
    
    char log_buf[256];
    snprintf(log_buf, sizeof(log_buf), "File deleted: %s by %s", msg->filename, msg->username);
    log_message("NameServer", log_buf);
    
    pthread_mutex_unlock(&server_state.mutex);
}

void handle_view(int sock, Message* msg) {
    pthread_mutex_lock(&server_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    int show_all = strstr(msg->data, "-a") != NULL;
    int show_detailed = strstr(msg->data, "-l") != NULL;
    
    sqlite3_stmt* stmt;
    const char* sql;
    
    if (show_all) {
        sql = "SELECT filename, owner, is_folder, word_count, sentence_count, created_at FROM files ORDER BY filename;";
        sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL);
    } else {
        sql = "SELECT DISTINCT f.filename, f.owner, f.is_folder, f.word_count, f.sentence_count, f.created_at "
              "FROM files f LEFT JOIN access_control ac ON f.filename = ac.filename "
              "WHERE f.owner = ? OR ac.username = ? ORDER BY f.filename;";
        sqlite3_prepare_v2(server_state.db, sql, -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, msg->username, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, msg->username, -1, SQLITE_STATIC);
    }
    
    char result[BUFFER_SIZE] = "";
    int count = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* filename = (const char*)sqlite3_column_text(stmt, 0);
        const char* owner = (const char*)sqlite3_column_text(stmt, 1);
        int is_folder = sqlite3_column_int(stmt, 2);
        int word_count = sqlite3_column_int(stmt, 3);
        int sentence_count = sqlite3_column_int(stmt, 4);
        time_t created_at = sqlite3_column_int64(stmt, 5);
        
        char line[512];
        if (show_detailed) {
            char time_str[64];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&created_at));
            snprintf(line, sizeof(line), "%s %-30s %-15s %5dw %3ds  %s\n",
                    is_folder ? "d" : "-", filename, owner, word_count, sentence_count, time_str);
        } else {
            snprintf(line, sizeof(line), "%s%s\n", is_folder ? "[DIR] " : "", filename);
        }
        
        if (strlen(result) + strlen(line) < sizeof(result) - 1) {
            strcat(result, line);
            count++;
        }
    }
    
    sqlite3_finalize(stmt);
    
    if (count == 0) {
        strcpy(result, "No files found\n");
    }
    
    resp.error_code = ERR_SUCCESS;
    strncpy(resp.data, result, sizeof(resp.data) - 1);
    send_message(sock, &resp);
    
    pthread_mutex_unlock(&server_state.mutex);
}

void handle_list(int sock, Message* msg) {
    (void)msg; // Unused parameter
    pthread_mutex_lock(&server_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    char result[BUFFER_SIZE] = "Registered Users:\n";
    for (int i = 0; i < server_state.user_count; i++) {
        char line[128];
        snprintf(line, sizeof(line), "  - %s\n", server_state.users[i].username);
        if (strlen(result) + strlen(line) < sizeof(result) - 1) {
            strcat(result, line);
        }
    }
    
    resp.error_code = ERR_SUCCESS;
    strncpy(resp.data, result, sizeof(resp.data) - 1);
    send_message(sock, &resp);
    
    pthread_mutex_unlock(&server_state.mutex);
}
