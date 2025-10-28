#include "../../include/nameserver.h"

NameServerState server_state;
volatile sig_atomic_t keep_running = 1;

void handle_shutdown(int sig) {
    (void)sig; // Unused parameter
    keep_running = 0;
    log_message("NameServer", "Shutdown signal received, stopping server...");
}

void* handle_client(void* arg);

int main() {
    memset(&server_state, 0, sizeof(server_state));
    pthread_mutex_init(&server_state.mutex, NULL);
    server_state.file_trie = trie_create();
    server_state.next_ss_id = 1;
    
    mkdir("data", 0755);
    
    if (init_database() < 0) {
        log_message("NameServer", "Failed to initialize database");
        return 1;
    }
    
    load_files_from_db();
    log_message("NameServer", "Name Server initialized successfully");
    
    // Register signal handlers for graceful shutdown
    signal(SIGINT, handle_shutdown);
    signal(SIGTERM, handle_shutdown);
    
    int server_fd = create_server_socket(NM_PORT);
    if (server_fd < 0) {
        log_message("NameServer", "Failed to create server socket");
        return 1;
    }
    
    // Set socket timeout so accept() doesn't block indefinitely
    struct timeval timeout;
    timeout.tv_sec = 1;  // 1 second timeout
    timeout.tv_usec = 0;
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    char log_buf[128];
    snprintf(log_buf, sizeof(log_buf), "Name Server listening on port %d", NM_PORT);
    log_message("NameServer", log_buf);
    
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
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        snprintf(log_buf, sizeof(log_buf), "Connection from %s:%d", client_ip, ntohs(client_addr.sin_port));
        log_message("NameServer", log_buf);
        
        pthread_t thread;
        int* sock_ptr = malloc(sizeof(int));
        *sock_ptr = client_sock;
        
        if (pthread_create(&thread, NULL, handle_client, sock_ptr) != 0) {
            log_message("NameServer", "Failed to create thread");
            free(sock_ptr);
            close(client_sock);
            continue;
        }
        pthread_detach(thread);
    }
    
    close(server_fd);
    sqlite3_close(server_state.db);
    trie_free(server_state.file_trie);
    pthread_mutex_destroy(&server_state.mutex);
    return 0;
}

void* handle_client(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);
    
    Message msg;
    while (receive_message(client_sock, &msg) == 0) {
        char log_buf[512];
        snprintf(log_buf, sizeof(log_buf), "Request: type=%s user=%s file=%s", 
                msg.type, msg.username, msg.filename);
        log_message("NameServer", log_buf);
        
        if (strcmp(msg.type, MSG_REGISTER_SS) == 0) {
            handle_register_ss(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_REGISTER_CLIENT) == 0) {
            handle_register_client(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_CREATE) == 0) {
            handle_create(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_READ) == 0 || strcmp(msg.type, MSG_STREAM) == 0 || strcmp(msg.type, MSG_INFO) == 0) {
            handle_read(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_WRITE_LOCK) == 0 || strcmp(msg.type, MSG_WRITE) == 0) {
            handle_write(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_WRITE_COMMIT) == 0) {
            pthread_mutex_lock(&server_state.mutex);
            int sentence_num;
            sscanf(msg.data, "%d", &sentence_num);
            release_lock(msg.filename, sentence_num, msg.username, client_sock);
            
            sqlite3_stmt* stmt;
            sqlite3_prepare_v2(server_state.db, "UPDATE files SET modified_at = ? WHERE filename = ?;", -1, &stmt, NULL);
            sqlite3_bind_int64(stmt, 1, time(NULL));
            sqlite3_bind_text(stmt, 2, msg.filename, -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            
            Message resp;
            init_message(&resp);
            resp.error_code = ERR_SUCCESS;
            send_message(client_sock, &resp);
            pthread_mutex_unlock(&server_state.mutex);
        } else if (strcmp(msg.type, MSG_DELETE) == 0) {
            handle_delete(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_VIEW) == 0) {
            handle_view(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_LIST) == 0) {
            handle_list(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_ADDACCESS) == 0) {
            handle_addaccess(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_REMACCESS) == 0) {
            handle_remaccess(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_UNDO) == 0) {
            handle_undo(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_EXEC) == 0) {
            handle_exec(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_CREATEFOLDER) == 0) {
            handle_createfolder(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_CHECKPOINT) == 0) {
            handle_checkpoint(client_sock, &msg);
        } else if (strcmp(msg.type, MSG_REQUESTACCESS) == 0) {
            handle_request_access(client_sock, &msg);
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
