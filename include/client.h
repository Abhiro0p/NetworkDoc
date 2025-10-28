#ifndef CLIENT_H
#define CLIENT_H

#include "common.h"

typedef struct {
    char username[MAX_USERNAME];
    int nm_socket;
    int connected;
} ClientState;

extern ClientState client_state;

// Core functions
int init_client(const char* username);
int connect_to_nameserver();
void run_interactive_shell();
void print_help();

// Command handlers
void cmd_create(const char* filename);
void cmd_read(const char* filename);
void cmd_write(const char* args);
void cmd_delete(const char* filename);
void cmd_view(const char* flags);
void cmd_info(const char* filename);
void cmd_stream(const char* filename);
void cmd_list();
void cmd_undo(const char* filename);
void cmd_addaccess(const char* args);
void cmd_remaccess(const char* args);
void cmd_exec(const char* filename);
void cmd_createfolder(const char* foldername);
void cmd_checkpoint(const char* args);
void cmd_listcheckpoints(const char* filename);
void cmd_revert(const char* args);
void cmd_requestaccess(const char* args);

// Helper functions
int contact_storage_server(const char* ss_info, Message* msg, Message* resp);
void parse_ss_info(const char* data, char* ip, int* port, char* replica_ip, int* replica_port);

#endif
