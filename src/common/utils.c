#include "../../include/common.h"

void get_current_timestamp(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

void log_message(const char* component, const char* message) {
    char timestamp[64];
    get_current_timestamp(timestamp, sizeof(timestamp));
    
    // Create logs directory if it doesn't exist
    mkdir("logs", 0755);
    
    // Create log file path
    char logfile[256];
    snprintf(logfile, sizeof(logfile), "logs/%s.log", component);
    
    // Append to log file
    FILE* fp = fopen(logfile, "a");
    if (fp) {
        fprintf(fp, "[%s] %s\n", timestamp, message);
        fclose(fp);
    }
    
    // Also print to console
    printf("[%s] %s\n", component, message);
    fflush(stdout);
}

char* trim(char* str) {
    if (!str) return NULL;
    
    // Trim leading spaces
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }
    
    if (*str == 0) return str;
    
    // Trim trailing spaces
    char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        end--;
    }
    *(end + 1) = '\0';
    
    return str;
}

int split_string(const char* str, char delimiter, char results[][MAX_SENTENCE], int max_results) {
    int count = 0;
    const char* start = str;
    const char* end;
    
    while (*start && count < max_results) {
        end = strchr(start, delimiter);
        if (end == NULL) {
            strncpy(results[count], start, MAX_SENTENCE - 1);
            results[count][MAX_SENTENCE - 1] = '\0';
            count++;
            break;
        }
        
        int len = end - start;
        if (len >= MAX_SENTENCE) len = MAX_SENTENCE - 1;
        strncpy(results[count], start, len);
        results[count][len] = '\0';
        count++;
        start = end + 1;
    }
    
    return count;
}

int parse_sentences(const char* content, char sentences[][MAX_SENTENCE], int max_sentences) {
    int count = 0;
    int len = strlen(content);
    
    char current[MAX_SENTENCE] = {0};
    int curr_pos = 0;
    
    for (int i = 0; i < len && count < max_sentences; i++) {
        if (curr_pos < MAX_SENTENCE - 1) {
            current[curr_pos++] = content[i];
        }
        
        // STRICT REQUIREMENT: Every '.', '!', '?' is a sentence delimiter
        // Even if it appears in middle of words like "e.g." or "Umm..."
        if (content[i] == '.' || content[i] == '!' || content[i] == '?') {
            // End sentence immediately after delimiter
            current[curr_pos] = '\0';
            char* trimmed = trim(current);
            if (strlen(trimmed) > 0) {
                strncpy(sentences[count], trimmed, MAX_SENTENCE - 1);
                sentences[count][MAX_SENTENCE - 1] = '\0';
                count++;
            }
            memset(current, 0, MAX_SENTENCE);
            curr_pos = 0;
        }
    }
    
    // Handle remaining content
    if (curr_pos > 0) {
        current[curr_pos] = '\0';
        char* trimmed = trim(current);
        if (strlen(trimmed) > 0 && count < max_sentences) {
            strncpy(sentences[count], trimmed, MAX_SENTENCE - 1);
            sentences[count][MAX_SENTENCE - 1] = '\0';
            count++;
        }
    }
    
    return count;
}

int parse_words(const char* sentence, char words[][MAX_WORD], int max_words) {
    int count = 0;
    const char* ptr = sentence;
    char current[MAX_WORD] = {0};
    int curr_pos = 0;
    
    while (*ptr && count < max_words) {
        if (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r') {
            if (curr_pos > 0) {
                current[curr_pos] = '\0';
                strncpy(words[count], current, MAX_WORD - 1);
                words[count][MAX_WORD - 1] = '\0';
                count++;
                memset(current, 0, MAX_WORD);
                curr_pos = 0;
            }
        } else {
            if (curr_pos < MAX_WORD - 1) {
                current[curr_pos++] = *ptr;
            }
        }
        ptr++;
    }
    
    // Handle last word
    if (curr_pos > 0 && count < max_words) {
        current[curr_pos] = '\0';
        strncpy(words[count], current, MAX_WORD - 1);
        words[count][MAX_WORD - 1] = '\0';
        count++;
    }
    
    return count;
}

const char* error_code_to_string(int code) {
    switch(code) {
        case ERR_SUCCESS: return "Success";
        case ERR_FILE_NOT_FOUND: return "File not found";
        case ERR_FILE_EXISTS: return "File already exists";
        case ERR_PERMISSION_DENIED: return "Permission denied";
        case ERR_LOCKED: return "Resource is locked";
        case ERR_INVALID_PARAM: return "Invalid parameters";
        case ERR_SERVER_ERROR: return "Server error";
        case ERR_NOT_OWNER: return "Not file owner";
        case ERR_USER_NOT_FOUND: return "User not found";
        case ERR_SS_NOT_FOUND: return "Storage server not found";
        case ERR_CONNECTION_FAILED: return "Connection failed";
        case ERR_FOLDER_NOT_FOUND: return "Folder not found";
        case ERR_CHECKPOINT_NOT_FOUND: return "Checkpoint not found";
        default: return "Unknown error";
    }
}

int create_server_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    int opt = 1;
    // SO_REUSEADDR allows quick restart after crash (avoids "Address already in use")
    // DO NOT use SO_REUSEPORT - it allows multiple processes on same port (not desired)
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        close(server_fd);
        return -1;
    }
    
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return -1;
    }
    
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        close(server_fd);
        return -1;
    }
    
    return server_fd;
}

int connect_to_server(const char* ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock);
        return -1;
    }
    
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return -1;
    }
    
    return sock;
}

void init_message(Message* msg) {
    memset(msg, 0, sizeof(Message));
}

void set_message_error(Message* msg, int error_code, const char* error_msg) {
    msg->error_code = error_code;
    strncpy(msg->error_msg, error_msg, sizeof(msg->error_msg) - 1);
    msg->error_msg[sizeof(msg->error_msg) - 1] = '\0';
}

int send_message(int socket, const Message* msg) {
    // Send message size first
    uint32_t size = sizeof(Message);
    uint32_t net_size = htonl(size);
    
    ssize_t sent = send(socket, &net_size, sizeof(net_size), 0);
    if (sent != sizeof(net_size)) {
        return -1;
    }
    
    // Send message
    sent = send(socket, msg, size, 0);
    if (sent != size) {
        return -1;
    }
    
    return 0;
}

int receive_message(int socket, Message* msg) {
    // Receive message size first
    uint32_t net_size;
    ssize_t received = recv(socket, &net_size, sizeof(net_size), MSG_WAITALL);
    if (received != sizeof(net_size)) {
        return -1;
    }
    
    uint32_t size = ntohl(net_size);
    if (size != sizeof(Message)) {
        return -1;
    }
    
    // Receive message
    received = recv(socket, msg, size, MSG_WAITALL);
    if (received != size) {
        return -1;
    }
    
    return 0;
}

int send_data(int socket, const char* data, size_t len) {
    // Send length first
    uint32_t net_len = htonl(len);
    if (send(socket, &net_len, sizeof(net_len), 0) != sizeof(net_len)) {
        return -1;
    }
    
    // Send data
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = send(socket, data + total_sent, len - total_sent, 0);
        if (sent <= 0) {
            return -1;
        }
        total_sent += sent;
    }
    
    return 0;
}

int receive_data(int socket, char* buffer, size_t max_len) {
    // Receive length first
    uint32_t net_len;
    if (recv(socket, &net_len, sizeof(net_len), MSG_WAITALL) != sizeof(net_len)) {
        return -1;
    }
    
    uint32_t len = ntohl(net_len);
    if (len > max_len) {
        return -1;
    }
    
    // Receive data
    size_t total_received = 0;
    while (total_received < len) {
        ssize_t received = recv(socket, buffer + total_received, len - total_received, 0);
        if (received <= 0) {
            return -1;
        }
        total_received += received;
    }
    
    buffer[len] = '\0';
    return len;
}
