#include "../../include/client.h"

void cmd_requestaccess(const char* args) {
    char filename[MAX_FILENAME], access_type[4];
    
    if (sscanf(args, "%s %s", filename, access_type) != 2) {
        printf("Usage: REQUESTACCESS <filename> <R|W>\n");
        return;
    }
    
    int atype = (strcmp(access_type, "W") == 0) ? ACCESS_WRITE : ACCESS_READ;
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_REQUESTACCESS);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.filename, filename, MAX_FILENAME - 1);
    snprintf(msg.data, sizeof(msg.data), "REQUEST|%d", atype);
    
    if (send_message(client_state.nm_socket, &msg) < 0) {
        printf("Error: Failed to send request\n");
        return;
    }
    
    Message resp;
    if (receive_message(client_state.nm_socket, &resp) < 0) {
        printf("Error: Failed to receive response\n");
        return;
    }
    
    if (resp.error_code == ERR_SUCCESS) {
        printf("%s\n", resp.data);
    } else {
        printf("Error: %s\n", resp.error_msg);
    }
}
