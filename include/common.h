#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdint.h>
#include <sys/time.h>

// Protocol Constants
#define NM_IP "127.0.0.1"
#define NM_PORT 8080
#define BUFFER_SIZE 65536
#define MAX_CLIENTS 100
#define MAX_FILENAME 256
#define MAX_USERNAME 64
#define MAX_PATH 512
#define MAX_SENTENCE 1024
#define MAX_WORD 128
#define MAX_WORDS_PER_SENTENCE 100
#define MAX_SENTENCES 1000

// Message Types
#define MSG_REGISTER_SS "REGISTER_SS"
#define MSG_REGISTER_CLIENT "REGISTER_CLIENT"
#define MSG_CREATE "CREATE"
#define MSG_READ "READ"
#define MSG_WRITE "WRITE"
#define MSG_WRITE_LOCK "WRITE_LOCK"
#define MSG_WRITE_UPDATE "WRITE_UPDATE"
#define MSG_WRITE_COMMIT "ETIRW"
#define MSG_DELETE "DELETE"
#define MSG_VIEW "VIEW"
#define MSG_INFO "INFO"
#define MSG_STREAM "STREAM"
#define MSG_UNDO "UNDO"
#define MSG_EXEC "EXEC"
#define MSG_LIST "LIST"
#define MSG_ADDACCESS "ADDACCESS"
#define MSG_REMACCESS "REMACCESS"
#define MSG_CREATEFOLDER "CREATEFOLDER"
#define MSG_MOVE "MOVE"
#define MSG_VIEWFOLDER "VIEWFOLDER"
#define MSG_CHECKPOINT "CHECKPOINT"
#define MSG_VIEWCHECKPOINT "VIEWCHECKPOINT"
#define MSG_REVERT "REVERT"
#define MSG_LISTCHECKPOINTS "LISTCHECKPOINTS"
#define MSG_REQUESTACCESS "REQUESTACCESS"
#define MSG_VIEWREQUESTS "VIEWREQUESTS"
#define MSG_APPROVEACCESS "APPROVEACCESS"
#define MSG_REJECTACCESS "REJECTACCESS"
#define MSG_REPLICATE "REPLICATE"
#define MSG_HEARTBEAT "HEARTBEAT"
#define MSG_GET_SS_INFO "GET_SS_INFO"

// Error Codes
#define ERR_SUCCESS 0
#define ERR_FILE_NOT_FOUND 1
#define ERR_FILE_EXISTS 2
#define ERR_PERMISSION_DENIED 3
#define ERR_LOCKED 4
#define ERR_INVALID_PARAM 5
#define ERR_SERVER_ERROR 6
#define ERR_NOT_OWNER 7
#define ERR_USER_NOT_FOUND 8
#define ERR_SS_NOT_FOUND 9
#define ERR_CONNECTION_FAILED 10
#define ERR_FOLDER_NOT_FOUND 11
#define ERR_CHECKPOINT_NOT_FOUND 12

// Access Rights
#define ACCESS_NONE 0
#define ACCESS_READ 1
#define ACCESS_WRITE 2
#define ACCESS_READ_WRITE 3

// Structures
typedef struct {
    char filename[MAX_FILENAME];
    char owner[MAX_USERNAME];
    char path[MAX_PATH];
    int storage_server_id;
    int replica_server_id;
    int word_count;
    int char_count;
    int sentence_count;
    time_t created_at;
    time_t modified_at;
    time_t accessed_at;
    int is_folder;
} FileMetadata;

typedef struct {
    char username[MAX_USERNAME];
    int permissions; // ACCESS_READ, ACCESS_WRITE, or both
} AccessEntry;

typedef struct {
    int id;
    char ip[INET_ADDRSTRLEN];
    int port;
    int is_alive;
    time_t last_heartbeat;
    int file_count;
} StorageServerInfo;

typedef struct {
    char username[MAX_USERNAME];
    char ip[INET_ADDRSTRLEN];
    int port;
    time_t registered_at;
} UserInfo;

typedef struct {
    char filename[MAX_FILENAME];
    int sentence_number;
    char username[MAX_USERNAME];
    int client_socket;  // Identifies specific client connection
    time_t locked_at;
} SentenceLock;

typedef struct {
    char filename[MAX_FILENAME];
    char requester[MAX_USERNAME];
    int access_type;
    time_t requested_at;
    char status[16]; // pending, approved, rejected
} AccessRequest;

typedef struct {
    char tag[64];
    char filename[MAX_FILENAME];
    char content[BUFFER_SIZE];
    time_t created_at;
} Checkpoint;

typedef struct {
    char filename[MAX_FILENAME];
    char content[BUFFER_SIZE];
    char username[MAX_USERNAME];
    time_t timestamp;
} UndoEntry;

// Protocol Message Structure
typedef struct {
    char type[32];
    char username[MAX_USERNAME];
    char filename[MAX_FILENAME];
    char data[BUFFER_SIZE];
    int error_code;
    char error_msg[256];
} Message;

// Utility Functions
void get_current_timestamp(char* buffer, size_t size);
void log_message(const char* component, const char* message);
char* trim(char* str);
int split_string(const char* str, char delimiter, char results[][MAX_SENTENCE], int max_results);
int parse_sentences(const char* content, char sentences[][MAX_SENTENCE], int max_sentences);
int parse_words(const char* sentence, char words[][MAX_WORD], int max_words);
const char* error_code_to_string(int code);

// Network Utilities
int create_server_socket(int port);
int connect_to_server(const char* ip, int port);
int send_message(int socket, const Message* msg);
int receive_message(int socket, Message* msg);
int send_data(int socket, const char* data, size_t len);
int receive_data(int socket, char* buffer, size_t len);

// Message Utilities
void init_message(Message* msg);
void set_message_error(Message* msg, int error_code, const char* error_msg);

#endif // COMMON_H
