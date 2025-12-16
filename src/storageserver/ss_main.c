#include "../../include/storageserver.h"

StorageServerState ss_state;
volatile sig_atomic_t keep_running = 1;

void handle_shutdown(int sig) {
    (void)sig; // Unused parameter
    keep_running = 0;
    log_message("StorageServer", "Shutdown signal received, stopping server...");
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }
    
    int port = atoi(argv[1]);
    if (port <= 1024 || port > 65535) {
        printf("Invalid port number (must be 1025-65535)\n");
        return 1;
    }
    
    if (port == NM_PORT) {
        printf("Error: Port %d is reserved for Name Server\n", NM_PORT);
        return 1;
    }
    
    if (init_storage_server(port) < 0) {
        log_message("StorageServer", "Failed to initialize");
        return 1;
    }
    
    if (register_with_nameserver() < 0) {
        log_message("StorageServer", "Failed to register with Name Server");
        return 1;
    }
    
    // Register signal handlers for graceful shutdown
    signal(SIGINT, handle_shutdown);
    signal(SIGTERM, handle_shutdown);
    
    char log_buf[128];
    snprintf(log_buf, sizeof(log_buf), "Storage Server listening on port %d", port);
    log_message("StorageServer", log_buf);
    
    int server_fd = create_server_socket(port);
    if (server_fd < 0) {
        log_message("StorageServer", "Failed to create server socket");
        return 1;
    }
    
    // Set socket timeout so accept() doesn't block indefinitely
    struct timeval timeout;
    timeout.tv_sec = 1;  // 1 second timeout
    timeout.tv_usec = 0;
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_sock < 0) {
            // Timeout or error - check if we should continue
            if (!keep_running) break;
            continue;
        }
        
        // Clear timeout on client socket (inherited from server socket)
        struct timeval no_timeout;
        no_timeout.tv_sec = 0;
        no_timeout.tv_usec = 0;
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &no_timeout, sizeof(no_timeout));
        
        pthread_t thread;
        int* sock_ptr = malloc(sizeof(int));
        *sock_ptr = client_sock;
        
        if (pthread_create(&thread, NULL, handle_client, sock_ptr) != 0) {
            log_message("StorageServer", "Failed to create thread");
            free(sock_ptr);
            close(client_sock);
            continue;
        }
        pthread_detach(thread);
    }
    
    close(server_fd);
    sqlite3_close(ss_state.db);
    pthread_mutex_destroy(&ss_state.mutex);
    return 0;
}

int init_storage_server(int port) {
    memset(&ss_state, 0, sizeof(ss_state));
    ss_state.port = port;
    pthread_mutex_init(&ss_state.mutex, NULL);
    
    // Create storage directory
    snprintf(ss_state.data_dir, sizeof(ss_state.data_dir), "%s_%d", SS_DATA_DIR, port);
    mkdir("data", 0755);
    mkdir(ss_state.data_dir, 0755);
    
    // Create subdirectories
    char undo_dir[MAX_PATH], checkpoint_dir[MAX_PATH];
    snprintf(undo_dir, sizeof(undo_dir), "%s/undo", ss_state.data_dir);
    snprintf(checkpoint_dir, sizeof(checkpoint_dir), "%s/checkpoints", ss_state.data_dir);
    mkdir(undo_dir, 0755);
    mkdir(checkpoint_dir, 0755);
    
    // Initialize database for metadata
    char db_path[MAX_PATH];
    snprintf(db_path, sizeof(db_path), "%s/metadata.db", ss_state.data_dir);
    
    int rc = sqlite3_open(db_path, &ss_state.db);
    if (rc != SQLITE_OK) {
        log_message("StorageServer", "Failed to open metadata database");
        return -1;
    }
    
    // Create metadata tables
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS file_metadata ("
        "filename TEXT PRIMARY KEY, "
        "word_count INTEGER DEFAULT 0, "
        "char_count INTEGER DEFAULT 0, "
        "sentence_count INTEGER DEFAULT 0, "
        "last_modified INTEGER"
        ");";
    
    sqlite3_exec(ss_state.db, sql, 0, 0, NULL);
    
    sql = "CREATE TABLE IF NOT EXISTS checkpoints ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "filename TEXT, "
        "tag TEXT, "
        "checkpoint_file TEXT, "
        "created_at INTEGER"
        ");";
    
    sqlite3_exec(ss_state.db, sql, 0, 0, NULL);
    
    log_message("StorageServer", "Storage Server initialized");
    return 0;
}

int register_with_nameserver() {
    ss_state.nm_socket = connect_to_server(NM_IP, NM_PORT);
    if (ss_state.nm_socket < 0) {
        log_message("StorageServer", "Failed to connect to Name Server");
        return -1;
    }
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_REGISTER_SS);
    snprintf(msg.data, sizeof(msg.data), "127.0.0.1:%d", ss_state.port);
    
    if (send_message(ss_state.nm_socket, &msg) < 0) {
        log_message("StorageServer", "Failed to send registration");
        close(ss_state.nm_socket);
        return -1;
    }
    
    Message resp;
    if (receive_message(ss_state.nm_socket, &resp) < 0) {
        log_message("StorageServer", "Failed to receive registration response");
        close(ss_state.nm_socket);
        return -1;
    }
    
    if (resp.error_code == ERR_SUCCESS) {
        sscanf(resp.data, "SS_ID:%d", &ss_state.ss_id);
        
        char log_buf[128];
        snprintf(log_buf, sizeof(log_buf), "Registered with Name Server, ID: %d", ss_state.ss_id);
        log_message("StorageServer", log_buf);
        return 0;
    } else {
        log_message("StorageServer", resp.error_msg);
        close(ss_state.nm_socket);
        return -1;
    }
}

void* handle_client(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);
    
    Message msg;
    while (receive_message(client_sock, &msg) == 0) {
        char log_buf[512];
        snprintf(log_buf, sizeof(log_buf), "Request: type=%s file=%s", msg.type, msg.filename);
        log_message("StorageServer", log_buf);
        
        if (strcmp(msg.type, MSG_CREATE) == 0) {
            handle_create(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_READ) == 0) {
            handle_read(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_WRITE) == 0 || strcmp(msg.type, MSG_WRITE_UPDATE) == 0) {
            handle_write(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_DELETE) == 0) {
            handle_delete(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_STREAM) == 0) {
            handle_stream(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_INFO) == 0) {
            handle_info(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_UNDO) == 0) {
            handle_undo(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_REPLICATE) == 0) {
            handle_replicate(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_CHECKPOINT) == 0 || 
                   strcmp(msg.type, MSG_LISTCHECKPOINTS) == 0 ||
                   strcmp(msg.type, MSG_REVERT) == 0) {
            handle_checkpoint_ops(client_sock, &msg);
        } else {
            Message resp;
            init_message(&resp);
            set_message_error(&resp, ERR_INVALID_PARAM, "Unknown command");
            send_message(client_sock, &resp);
        }
    }
    
    close(client_sock);
    return NULL;
}

char* get_file_path(const char* filename) {
    static char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", ss_state.data_dir, filename);
    return path;
}

int save_file_content(const char* filename, const char* content) {
    char* path = get_file_path(filename);
    
    FILE* fp = fopen(path, "w");
    if (!fp) {
        return -1;
    }
    
    fputs(content, fp);
    fclose(fp);
    
    // Update metadata
    char sentences[MAX_SENTENCES][MAX_SENTENCE];
    int sentence_count = parse_sentences(content, sentences, MAX_SENTENCES);
    
    int word_count = 0;
    for (int i = 0; i < sentence_count; i++) {
        char words[MAX_WORDS_PER_SENTENCE][MAX_WORD];
        word_count += parse_words(sentences[i], words, MAX_WORDS_PER_SENTENCE);
    }
    
    int char_count = strlen(content);
    
    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO file_metadata (filename, word_count, char_count, sentence_count, last_modified) "
                     "VALUES (?, ?, ?, ?, ?);";
    
    if (sqlite3_prepare_v2(ss_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, word_count);
        sqlite3_bind_int(stmt, 3, char_count);
        sqlite3_bind_int(stmt, 4, sentence_count);
        sqlite3_bind_int64(stmt, 5, time(NULL));
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    return 0;
}

int load_file_content(const char* filename, char* buffer, size_t max_size) {
    char* path = get_file_path(filename);
    
    FILE* fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }
    
    size_t read = fread(buffer, 1, max_size - 1, fp);
    buffer[read] = '\0';
    fclose(fp);
    
    return read;
}

int save_undo_state(const char* filename, const char* content) {
    char undo_path[MAX_PATH];
    snprintf(undo_path, sizeof(undo_path), "%s/undo/%s", ss_state.data_dir, filename);
    
    FILE* fp = fopen(undo_path, "w");
    if (!fp) {
        return -1;
    }
    
    fputs(content, fp);
    fclose(fp);
    return 0;
}

int load_undo_state(const char* filename, char* buffer, size_t max_size) {
    char undo_path[MAX_PATH];
    snprintf(undo_path, sizeof(undo_path), "%s/undo/%s", ss_state.data_dir, filename);
    
    FILE* fp = fopen(undo_path, "r");
    if (!fp) {
        return -1;
    }
    
    size_t read = fread(buffer, 1, max_size - 1, fp);
    buffer[read] = '\0';
    fclose(fp);
    
    return read;
}
