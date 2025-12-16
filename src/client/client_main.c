#include "../../include/client.h"
#include <readline/readline.h>
#include <readline/history.h>

ClientState client_state;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <username>\n", argv[0]);
        return 1;
    }
    
    // Ignore SIGPIPE to handle broken connections gracefully
    signal(SIGPIPE, SIG_IGN);
    
    if (init_client(argv[1]) < 0) {
        printf("Failed to initialize client\n");
        return 1;
    }
    
    if (connect_to_nameserver() < 0) {
        printf("Failed to connect to Name Server\n");
        return 1;
    }
    
    printf("Welcome to Docs++, %s!\n", client_state.username);
    printf("Type 'help' for available commands or 'exit' to quit.\n\n");
    
    run_interactive_shell();
    
    close(client_state.nm_socket);
    return 0;
}

int init_client(const char* username) {
    memset(&client_state, 0, sizeof(client_state));
    strncpy(client_state.username, username, MAX_USERNAME - 1);
    client_state.connected = 0;
    return 0;
}

int connect_to_nameserver() {
    client_state.nm_socket = connect_to_server(NM_IP, NM_PORT);
    if (client_state.nm_socket < 0) {
        return -1;
    }
    
    // Register with name server
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_REGISTER_CLIENT);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    
    if (send_message(client_state.nm_socket, &msg) < 0) {
        close(client_state.nm_socket);
        return -1;
    }
    
    Message resp;
    if (receive_message(client_state.nm_socket, &resp) < 0) {
        close(client_state.nm_socket);
        return -1;
    }
    
    if (resp.error_code == ERR_SUCCESS) {
        client_state.connected = 1;
        return 0;
    }
    
    printf("Registration failed: %s\n", resp.error_msg);
    close(client_state.nm_socket);
    return -1;
}

void run_interactive_shell() {
    char* line;
    
    while (1) {
        line = readline("docs++> ");
        
        if (!line) break;
        
        if (strlen(line) > 0) {
            add_history(line);
        }
        
        char* trimmed = trim(line);
        if (strlen(trimmed) == 0) {
            free(line);
            continue;
        }
        
        // Parse command
        char cmd[32] = {0};
        char args[BUFFER_SIZE] = {0};
        
        sscanf(trimmed, "%s %[^\n]", cmd, args);
        
        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            free(line);
            break;
        } else if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "CREATE") == 0) {
            cmd_create(args);
        } else if (strcmp(cmd, "READ") == 0) {
            cmd_read(args);
        } else if (strcmp(cmd, "WRITE") == 0) {
            cmd_write(args);
        } else if (strcmp(cmd, "DELETE") == 0) {
            cmd_delete(args);
        } else if (strcmp(cmd, "VIEW") == 0) {
            cmd_view(args);
        } else if (strcmp(cmd, "INFO") == 0) {
            cmd_info(args);
        } else if (strcmp(cmd, "STREAM") == 0) {
            cmd_stream(args);
        } else if (strcmp(cmd, "LIST") == 0) {
            cmd_list();
        } else if (strcmp(cmd, "UNDO") == 0) {
            cmd_undo(args);
        } else if (strcmp(cmd, "ADDACCESS") == 0) {
            cmd_addaccess(args);
        } else if (strcmp(cmd, "REMACCESS") == 0) {
            cmd_remaccess(args);
        } else if (strcmp(cmd, "EXEC") == 0) {
            cmd_exec(args);
        } else if (strcmp(cmd, "CREATEFOLDER") == 0) {
            cmd_createfolder(args);
        } else if (strcmp(cmd, "CHECKPOINT") == 0) {
            cmd_checkpoint(args);
        } else if (strcmp(cmd, "LISTCHECKPOINTS") == 0) {
            cmd_listcheckpoints(args);
        } else if (strcmp(cmd, "REVERT") == 0) {
            cmd_revert(args);
        } else if (strcmp(cmd, "REQUESTACCESS") == 0) {
            cmd_requestaccess(args);
        } else {
            printf("Unknown command: %s\n", cmd);
            printf("Type 'help' for available commands.\n");
        }
        
        free(line);
    }
}

void print_help() {
    printf("\nAvailable Commands:\n");
    printf("==================\n\n");
    printf("File Operations:\n");
    printf("  CREATE <filename>                 - Create a new empty file\n");
    printf("  READ <filename>                   - Display file contents\n");
    printf("  WRITE <filename> <sentence#>      - Edit a sentence (then word edits, end with ETIRW)\n");
    printf("  DELETE <filename>                 - Delete a file (owner only)\n");
    printf("  UNDO <filename>                   - Undo last change to file\n");
    printf("  INFO <filename>                   - Show file metadata\n");
    printf("  STREAM <filename>                 - Stream file word-by-word\n");
    printf("\n");
    printf("Listing:\n");
    printf("  VIEW                              - List your accessible files\n");
    printf("  VIEW -a                           - List all files\n");
    printf("  VIEW -l                           - List with details\n");
    printf("  VIEW -al                          - List all with details\n");
    printf("  LIST                              - List all registered users\n");
    printf("\n");
    printf("Access Control:\n");
    printf("  ADDACCESS -R <file> <user>        - Grant read access\n");
    printf("  ADDACCESS -W <file> <user>        - Grant write access\n");
    printf("  REMACCESS <file> <user>           - Revoke access\n");
    printf("  REQUESTACCESS <file> <R|W>        - Request access to a file\n");
    printf("\n");
    printf("Advanced:\n");
    printf("  EXEC <filename>                   - Execute file as shell script\n");
    printf("  CREATEFOLDER <foldername>         - Create a folder\n");
    printf("  CHECKPOINT <file> <tag>           - Create a checkpoint\n");
    printf("  LISTCHECKPOINTS <file>            - List checkpoints\n");
    printf("  REVERT <file> <tag>               - Revert to checkpoint\n");
    printf("\n");
    printf("System:\n");
    printf("  help                              - Show this help\n");
    printf("  exit                              - Exit the client\n");
    printf("\n");
}

void parse_ss_info(const char* data, char* ip, int* port, char* replica_ip, int* replica_port) {
    *port = 0;
    *replica_port = 0;
    
    // Parse: SS:ip:port or SS:ip:port|REPLICA:ip:port
    char temp[BUFFER_SIZE];
    strncpy(temp, data, BUFFER_SIZE - 1);
    temp[BUFFER_SIZE - 1] = '\0';
    
    char* ss_part = strstr(temp, "SS:");
    if (ss_part) {
        // Limit IP address length to prevent buffer overflow (INET_ADDRSTRLEN = 16)
        sscanf(ss_part, "SS:%15[^:]:%d", ip, port);
    }
    
    char* replica_part = strstr(temp, "REPLICA:");
    if (replica_part) {
        // Limit IP address length to prevent buffer overflow
        sscanf(replica_part, "REPLICA:%15[^:]:%d", replica_ip, replica_port);
    }
}

int contact_storage_server(const char* ss_info, Message* msg, Message* resp) {
    char ip[INET_ADDRSTRLEN] = {0};
    int port = 0;
    char replica_ip[INET_ADDRSTRLEN] = {0};
    int replica_port = 0;
    
    parse_ss_info(ss_info, ip, &port, replica_ip, &replica_port);
    
    if (port == 0) {
        return -1;
    }
    
    int ss_sock = connect_to_server(ip, port);
    if (ss_sock < 0) {
        // Try replica if primary fails
        if (replica_port > 0) {
            ss_sock = connect_to_server(replica_ip, replica_port);
            if (ss_sock < 0) {
                return -1;
            }
        } else {
            return -1;
        }
    }
    
    if (send_message(ss_sock, msg) < 0) {
        close(ss_sock);
        return -1;
    }
    
    if (receive_message(ss_sock, resp) < 0) {
        close(ss_sock);
        return -1;
    }
    
    close(ss_sock);
    return 0;
}
