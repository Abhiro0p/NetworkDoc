#ifndef NAMESERVER_H
#define NAMESERVER_H

#include "common.h"
#include "trie.h"

#define MAX_STORAGE_SERVERS 10
#define MAX_USERS 100
#define MAX_LOCKS 100

typedef struct {
    sqlite3* db;
    Trie* file_trie;
    pthread_mutex_t mutex;
    StorageServerInfo storage_servers[MAX_STORAGE_SERVERS];
    int ss_count;
    UserInfo users[MAX_USERS];
    int user_count;
    SentenceLock locks[MAX_LOCKS];
    int lock_count;
    int next_ss_id;
} NameServerState;

extern NameServerState server_state;

// Core functions
int init_database();
int load_files_from_db();
int check_permission(const char* username, const char* filename, int required_perm);
StorageServerInfo* get_ss_by_id(int ss_id);
void release_lock(const char* filename, int sentence_num, const char* username, int client_socket);

// Message handlers
void handle_register_ss(int sock, Message* msg);
void handle_register_client(int sock, Message* msg);
void handle_create(int sock, Message* msg);
void handle_read(int sock, Message* msg);
void handle_write(int sock, Message* msg);
void handle_delete(int sock, Message* msg);
void handle_view(int sock, Message* msg);
void handle_list(int sock, Message* msg);
void handle_addaccess(int sock, Message* msg);
void handle_remaccess(int sock, Message* msg);
void handle_undo(int sock, Message* msg);
void handle_exec(int sock, Message* msg);
void handle_createfolder(int sock, Message* msg);
void handle_checkpoint(int sock, Message* msg);
void handle_request_access(int sock, Message* msg);

#endif
