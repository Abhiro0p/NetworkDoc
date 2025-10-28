#include "../../include/client.h"

void cmd_create(const char* filename) {
    if (strlen(filename) == 0) {
        printf("Usage: CREATE <filename>\n");
        return;
    }
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_CREATE);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.filename, filename, MAX_FILENAME - 1);
    
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
        Message ss_msg;
        init_message(&ss_msg);
        strcpy(ss_msg.type, MSG_CREATE);
        strncpy(ss_msg.username, client_state.username, MAX_USERNAME - 1);
        strncpy(ss_msg.filename, filename, MAX_FILENAME - 1);
        
        Message ss_resp;
        if (contact_storage_server(resp.data, &ss_msg, &ss_resp) == 0) {
            if (ss_resp.error_code == ERR_SUCCESS) {
                printf("File '%s' created successfully\n", filename);
            } else {
                printf("Error: %s\n", ss_resp.error_msg);
            }
        } else {
            printf("Error: Failed to contact storage server\n");
        }
    } else {
        printf("Error: %s\n", resp.error_msg);
    }
}

void cmd_read(const char* filename) {
    if (strlen(filename) == 0) {
        printf("Usage: READ <filename>\n");
        return;
    }
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_READ);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.filename, filename, MAX_FILENAME - 1);
    
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
        Message ss_msg;
        init_message(&ss_msg);
        strcpy(ss_msg.type, MSG_READ);
        strncpy(ss_msg.username, client_state.username, MAX_USERNAME - 1);
        strncpy(ss_msg.filename, filename, MAX_FILENAME - 1);
        
        Message ss_resp;
        if (contact_storage_server(resp.data, &ss_msg, &ss_resp) == 0) {
            if (ss_resp.error_code == ERR_SUCCESS) {
                printf("\n=== Content of '%s' ===\n%s\n", filename, ss_resp.data);
            } else {
                printf("Error: %s\n", ss_resp.error_msg);
            }
        } else {
            printf("Error: Failed to contact storage server\n");
        }
    } else {
        printf("Error: %s\n", resp.error_msg);
    }
}

void cmd_write(const char* args) {
    char filename[MAX_FILENAME];
    int sentence_num;
    
    if (sscanf(args, "%s %d", filename, &sentence_num) != 2) {
        printf("Usage: WRITE <filename> <sentence_number>\n");
        return;
    }
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_WRITE_LOCK);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.filename, filename, MAX_FILENAME - 1);
    snprintf(msg.data, sizeof(msg.data), "%d", sentence_num);
    
    if (send_message(client_state.nm_socket, &msg) < 0) {
        printf("Error: Failed to send request\n");
        return;
    }
    
    Message resp;
    if (receive_message(client_state.nm_socket, &resp) < 0) {
        printf("Error: Failed to receive response\n");
        return;
    }
    
    if (resp.error_code != ERR_SUCCESS) {
        printf("Error: %s\n", resp.error_msg);
        return;
    }
    
    char ss_info[BUFFER_SIZE];
    strncpy(ss_info, resp.data, sizeof(ss_info) - 1);
    
    printf("Lock acquired for sentence %d. Enter word edits:\n", sentence_num);
    printf("Format: <word_index> <new_content>\n");
    printf("Type 'ETIRW' when done.\n\n");
    
    Message read_msg;
    init_message(&read_msg);
    strcpy(read_msg.type, MSG_READ);
    strncpy(read_msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(read_msg.filename, filename, MAX_FILENAME - 1);
    
    Message read_resp;
    if (contact_storage_server(ss_info, &read_msg, &read_resp) != 0 || read_resp.error_code != ERR_SUCCESS) {
        printf("Error: Could not read current content\n");
        return;
    }
    
    char current_content[BUFFER_SIZE];
    strncpy(current_content, read_resp.data, sizeof(current_content) - 1);
    
    char sentences[MAX_SENTENCES][MAX_SENTENCE];
    int sentence_count = parse_sentences(current_content, sentences, MAX_SENTENCES);
    
    // Allow creating sentence 0 for empty files, or editing existing sentences
    if (sentence_num > sentence_count || (sentence_num == sentence_count && sentence_num > 0)) {
        printf("Error: Invalid sentence number (max: %d)\n", sentence_count);
        return;
    }
    
    // For new sentence (empty file), start with empty sentence
    if (sentence_num == sentence_count) {
        sentences[sentence_num][0] = '\0';
        sentence_count++;
    }
    
    printf("Current sentence: %s\n\n", sentences[sentence_num]);
    
    char words[MAX_WORDS_PER_SENTENCE][MAX_WORD];
    int word_count = parse_words(sentences[sentence_num], words, MAX_WORDS_PER_SENTENCE);
    
    char line[256];
    while (1) {
        printf("> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        
        char* trimmed = trim(line);
        if (strcmp(trimmed, "ETIRW") == 0) {
            break;
        }
        
        int word_idx;
        char new_word[MAX_WORD];
        if (sscanf(trimmed, "%d %s", &word_idx, new_word) == 2) {
            // Allow adding new words sequentially or editing existing ones
            if (word_idx >= 0 && word_idx <= word_count && word_idx < MAX_WORDS_PER_SENTENCE) {
                strcpy(words[word_idx], new_word);
                if (word_idx == word_count) {
                    word_count++; // Added new word
                    printf("Word %d added: '%s'\n", word_idx, new_word);
                } else {
                    printf("Word %d updated to '%s'\n", word_idx, new_word);
                }
            } else {
                printf("Error: Invalid word index (0-%d)\n", word_count);
            }
        } else {
            printf("Invalid format. Use: <word_index> <new_content>\n");
        }
    }
    
    char new_sentence[MAX_SENTENCE] = {0};
    for (int i = 0; i < word_count; i++) {
        if (i > 0) strcat(new_sentence, " ");
        strcat(new_sentence, words[i]);
    }
    strcpy(sentences[sentence_num], new_sentence);
    
    char new_content[BUFFER_SIZE] = {0};
    for (int i = 0; i < sentence_count; i++) {
        if (i > 0) strcat(new_content, " ");
        strcat(new_content, sentences[i]);
    }
    
    Message write_msg;
    init_message(&write_msg);
    strcpy(write_msg.type, MSG_WRITE);
    strncpy(write_msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(write_msg.filename, filename, MAX_FILENAME - 1);
    strncpy(write_msg.data, new_content, sizeof(write_msg.data) - 1);
    
    Message write_resp;
    if (contact_storage_server(ss_info, &write_msg, &write_resp) == 0) {
        if (write_resp.error_code == ERR_SUCCESS) {
            Message commit_msg;
            init_message(&commit_msg);
            strcpy(commit_msg.type, MSG_WRITE_COMMIT);
            strncpy(commit_msg.username, client_state.username, MAX_USERNAME - 1);
            strncpy(commit_msg.filename, filename, MAX_FILENAME - 1);
            snprintf(commit_msg.data, sizeof(commit_msg.data), "%d", sentence_num);
            
            send_message(client_state.nm_socket, &commit_msg);
            receive_message(client_state.nm_socket, &resp);
            
            printf("Write completed successfully\n");
        } else {
            printf("Error: %s\n", write_resp.error_msg);
        }
    } else {
        printf("Error: Failed to contact storage server\n");
    }
}

void cmd_delete(const char* filename) {
    if (strlen(filename) == 0) {
        printf("Usage: DELETE <filename>\n");
        return;
    }
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_DELETE);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.filename, filename, MAX_FILENAME - 1);
    
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
        Message ss_msg;
        init_message(&ss_msg);
        strcpy(ss_msg.type, MSG_DELETE);
        strncpy(ss_msg.username, client_state.username, MAX_USERNAME - 1);
        strncpy(ss_msg.filename, filename, MAX_FILENAME - 1);
        
        Message ss_resp;
        if (contact_storage_server(resp.data, &ss_msg, &ss_resp) == 0) {
            if (ss_resp.error_code == ERR_SUCCESS) {
                printf("File '%s' deleted successfully\n", filename);
            } else {
                printf("Error: %s\n", ss_resp.error_msg);
            }
        } else {
            printf("Error: Failed to contact storage server\n");
        }
    } else {
        printf("Error: %s\n", resp.error_msg);
    }
}

void cmd_view(const char* flags) {
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_VIEW);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.data, flags, sizeof(msg.data) - 1);
    
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
        printf("\n%s\n", resp.data);
    } else {
        printf("Error: %s\n", resp.error_msg);
    }
}

void cmd_info(const char* filename) {
    if (strlen(filename) == 0) {
        printf("Usage: INFO <filename>\n");
        return;
    }
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_INFO);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.filename, filename, MAX_FILENAME - 1);
    
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
        Message ss_msg;
        init_message(&ss_msg);
        strcpy(ss_msg.type, MSG_INFO);
        strncpy(ss_msg.username, client_state.username, MAX_USERNAME - 1);
        strncpy(ss_msg.filename, filename, MAX_FILENAME - 1);
        
        Message ss_resp;
        if (contact_storage_server(resp.data, &ss_msg, &ss_resp) == 0) {
            if (ss_resp.error_code == ERR_SUCCESS) {
                printf("\n=== Info for '%s' ===\n", filename);
                printf("%s\n", ss_resp.data);
            } else {
                printf("Error: %s\n", ss_resp.error_msg);
            }
        } else {
            printf("Error: Failed to contact storage server\n");
        }
    } else {
        printf("Error: %s\n", resp.error_msg);
    }
}

void cmd_stream(const char* filename) {
    if (strlen(filename) == 0) {
        printf("Usage: STREAM <filename>\n");
        return;
    }
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_STREAM);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.filename, filename, MAX_FILENAME - 1);
    
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
        char ip[INET_ADDRSTRLEN];
        int port;
        char replica_ip[INET_ADDRSTRLEN];
        int replica_port;
        parse_ss_info(resp.data, ip, &port, replica_ip, &replica_port);
        
        int ss_sock = connect_to_server(ip, port);
        if (ss_sock < 0) {
            printf("Error: Failed to connect to storage server\n");
            return;
        }
        
        Message ss_msg;
        init_message(&ss_msg);
        strcpy(ss_msg.type, MSG_STREAM);
        strncpy(ss_msg.username, client_state.username, MAX_USERNAME - 1);
        strncpy(ss_msg.filename, filename, MAX_FILENAME - 1);
        
        if (send_message(ss_sock, &ss_msg) < 0) {
            printf("Error: Failed to send stream request\n");
            close(ss_sock);
            return;
        }
        
        Message ss_resp;
        if (receive_message(ss_sock, &ss_resp) < 0 || ss_resp.error_code != ERR_SUCCESS) {
            printf("Error: Stream failed to start\n");
            close(ss_sock);
            return;
        }
        
        printf("\n=== Streaming '%s' ===\n", filename);
        
        while (1) {
            Message word_msg;
            if (receive_message(ss_sock, &word_msg) < 0) {
                printf("\n[Stream interrupted]\n");
                break;
            }
            
            if (strcmp(word_msg.type, "STREAM_END") == 0) {
                printf("\n[Stream complete]\n");
                break;
            }
            
            if (strcmp(word_msg.type, "STREAM_WORD") == 0) {
                printf("%s ", word_msg.data);
                fflush(stdout);
            }
        }
        
        close(ss_sock);
    } else {
        printf("Error: %s\n", resp.error_msg);
    }
}

void cmd_list() {
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_LIST);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    
    if (send_message(client_state.nm_socket, &msg) < 0) {
        printf("Error: Connection to Name Server lost. Please restart the client.\n");
        return;
    }
    
    Message resp;
    if (receive_message(client_state.nm_socket, &resp) < 0) {
        printf("Error: Connection to Name Server lost. Please restart the client.\n");
        return;
    }
    
    if (resp.error_code == ERR_SUCCESS) {
        printf("\n%s\n", resp.data);
    } else {
        printf("Error: %s\n", resp.error_msg);
    }
}

void cmd_undo(const char* filename) {
    if (strlen(filename) == 0) {
        printf("Usage: UNDO <filename>\n");
        return;
    }
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_UNDO);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.filename, filename, MAX_FILENAME - 1);
    
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
        Message ss_msg;
        init_message(&ss_msg);
        strcpy(ss_msg.type, MSG_UNDO);
        strncpy(ss_msg.username, client_state.username, MAX_USERNAME - 1);
        strncpy(ss_msg.filename, filename, MAX_FILENAME - 1);
        
        Message ss_resp;
        if (contact_storage_server(resp.data, &ss_msg, &ss_resp) == 0) {
            if (ss_resp.error_code == ERR_SUCCESS) {
                printf("Undo successful for '%s'\n", filename);
            } else {
                printf("Error: %s\n", ss_resp.error_msg);
            }
        } else {
            printf("Error: Failed to contact storage server\n");
        }
    } else {
        printf("Error: %s\n", resp.error_msg);
    }
}

void cmd_addaccess(const char* args) {
    char flag[8], filename[MAX_FILENAME], username[MAX_USERNAME];
    
    if (sscanf(args, "%s %s %s", flag, filename, username) != 3) {
        printf("Usage: ADDACCESS -R|-W <filename> <username>\n");
        return;
    }
    
    int permissions = 0;
    if (strcmp(flag, "-R") == 0) {
        permissions = ACCESS_READ;
    } else if (strcmp(flag, "-W") == 0) {
        permissions = ACCESS_WRITE;
    } else {
        printf("Invalid flag. Use -R for read or -W for write\n");
        return;
    }
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_ADDACCESS);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.filename, filename, MAX_FILENAME - 1);
    snprintf(msg.data, sizeof(msg.data), "%s|%d", username, permissions);
    
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

void cmd_remaccess(const char* args) {
    char filename[MAX_FILENAME], username[MAX_USERNAME];
    
    if (sscanf(args, "%s %s", filename, username) != 2) {
        printf("Usage: REMACCESS <filename> <username>\n");
        return;
    }
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_REMACCESS);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.filename, filename, MAX_FILENAME - 1);
    strncpy(msg.data, username, sizeof(msg.data) - 1);
    
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

void cmd_exec(const char* filename) {
    if (strlen(filename) == 0) {
        printf("Usage: EXEC <filename>\n");
        return;
    }
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_EXEC);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.filename, filename, MAX_FILENAME - 1);
    
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
        Message ss_msg;
        init_message(&ss_msg);
        strcpy(ss_msg.type, MSG_READ);
        strncpy(ss_msg.username, client_state.username, MAX_USERNAME - 1);
        strncpy(ss_msg.filename, filename, MAX_FILENAME - 1);
        
        Message ss_resp;
        if (contact_storage_server(resp.data, &ss_msg, &ss_resp) == 0 && ss_resp.error_code == ERR_SUCCESS) {
            printf("\n=== Executing '%s' ===\n", filename);
            
            FILE* fp = popen(ss_resp.data, "r");
            if (fp) {
                char line[256];
                while (fgets(line, sizeof(line), fp)) {
                    printf("%s", line);
                }
                pclose(fp);
            } else {
                printf("Error: Failed to execute\n");
            }
        } else {
            printf("Error: Failed to read file\n");
        }
    } else {
        printf("Error: %s\n", resp.error_msg);
    }
}

void cmd_createfolder(const char* foldername) {
    if (strlen(foldername) == 0) {
        printf("Usage: CREATEFOLDER <foldername>\n");
        return;
    }
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_CREATEFOLDER);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.filename, foldername, MAX_FILENAME - 1);
    
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

void cmd_checkpoint(const char* args) {
    char filename[MAX_FILENAME], tag[64];
    
    if (sscanf(args, "%s %s", filename, tag) != 2) {
        printf("Usage: CHECKPOINT <filename> <tag>\n");
        return;
    }
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_CHECKPOINT);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.filename, filename, MAX_FILENAME - 1);
    snprintf(msg.data, sizeof(msg.data), "CREATE|%s", tag);
    
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
        Message ss_msg;
        init_message(&ss_msg);
        strcpy(ss_msg.type, MSG_CHECKPOINT);
        strncpy(ss_msg.username, client_state.username, MAX_USERNAME - 1);
        strncpy(ss_msg.filename, filename, MAX_FILENAME - 1);
        snprintf(ss_msg.data, sizeof(ss_msg.data), "CREATE|%s", tag);
        
        Message ss_resp;
        if (contact_storage_server(resp.data, &ss_msg, &ss_resp) == 0 && ss_resp.error_code == ERR_SUCCESS) {
            printf("%s\n", ss_resp.data);
        } else {
            printf("Error: Failed to create checkpoint\n");
        }
    } else {
        printf("Error: %s\n", resp.error_msg);
    }
}

void cmd_listcheckpoints(const char* filename) {
    if (strlen(filename) == 0) {
        printf("Usage: LISTCHECKPOINTS <filename>\n");
        return;
    }
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_CHECKPOINT);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.filename, filename, MAX_FILENAME - 1);
    strcpy(msg.data, "LIST");
    
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
        Message ss_msg;
        init_message(&ss_msg);
        strcpy(ss_msg.type, MSG_LISTCHECKPOINTS);
        strncpy(ss_msg.username, client_state.username, MAX_USERNAME - 1);
        strncpy(ss_msg.filename, filename, MAX_FILENAME - 1);
        strcpy(ss_msg.data, "LIST");
        
        Message ss_resp;
        if (contact_storage_server(resp.data, &ss_msg, &ss_resp) == 0 && ss_resp.error_code == ERR_SUCCESS) {
            printf("\n%s\n", ss_resp.data);
        } else {
            printf("Error: Failed to list checkpoints\n");
        }
    } else {
        printf("Error: %s\n", resp.error_msg);
    }
}

void cmd_revert(const char* args) {
    char filename[MAX_FILENAME], tag[64];
    
    if (sscanf(args, "%s %s", filename, tag) != 2) {
        printf("Usage: REVERT <filename> <tag>\n");
        return;
    }
    
    Message msg;
    init_message(&msg);
    strcpy(msg.type, MSG_CHECKPOINT);
    strncpy(msg.username, client_state.username, MAX_USERNAME - 1);
    strncpy(msg.filename, filename, MAX_FILENAME - 1);
    snprintf(msg.data, sizeof(msg.data), "REVERT|%s", tag);
    
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
        Message ss_msg;
        init_message(&ss_msg);
        strcpy(ss_msg.type, MSG_REVERT);
        strncpy(ss_msg.username, client_state.username, MAX_USERNAME - 1);
        strncpy(ss_msg.filename, filename, MAX_FILENAME - 1);
        snprintf(ss_msg.data, sizeof(ss_msg.data), "REVERT|%s", tag);
        
        Message ss_resp;
        if (contact_storage_server(resp.data, &ss_msg, &ss_resp) == 0 && ss_resp.error_code == ERR_SUCCESS) {
            printf("%s\n", ss_resp.data);
        } else {
            printf("Error: Failed to revert\n");
        }
    } else {
        printf("Error: %s\n", resp.error_msg);
    }
}
