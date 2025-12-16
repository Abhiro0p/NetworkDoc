#ifndef STORAGESERVER_H
#define STORAGESERVER_H

#include "common.h"

#define MAX_FILES 1000
#define SS_DATA_DIR "data/storage"

typedef struct {
    int ss_id;
    int port;
    char data_dir[MAX_PATH];
    sqlite3* db;
    pthread_mutex_t mutex;
    int nm_socket;
} StorageServerState;

extern StorageServerState ss_state;

// Core functions
int init_storage_server(int port);
int register_with_nameserver();
void* handle_client(void* arg);
char* get_file_path(const char* filename);
int save_file_content(const char* filename, const char* content);
int load_file_content(const char* filename, char* buffer, size_t max_size);
int save_undo_state(const char* filename, const char* content);
int load_undo_state(const char* filename, char* buffer, size_t max_size);

// Command handlers
void handle_create(int sock, Message* msg);
void handle_read(int sock, Message* msg);
void handle_write(int sock, Message* msg);
void handle_delete(int sock, Message* msg);
void handle_stream(int sock, Message* msg);
void handle_info(int sock, Message* msg);
void handle_undo(int sock, Message* msg);
void handle_replicate(int sock, Message* msg);
void handle_checkpoint_ops(int sock, Message* msg);

#endif
