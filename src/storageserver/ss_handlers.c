#include "../../include/storageserver.h"

// Security Note: Storage Server trusts that clients have been authorized by Name Server
// Direct connections to SS bypass permission checks - ensure network isolation
static int validate_basic_request(Message* msg) {
    // Basic validation: ensure username is provided
    if (msg->username == NULL || strlen(msg->username) == 0) {
        return 0;
    }
    // Validate filename is not empty and doesn't contain path traversal
    if (msg->filename == NULL || strlen(msg->filename) == 0) {
        return 0;
    }
    if (strstr(msg->filename, "..") != NULL || strchr(msg->filename, '/') != NULL) {
        return 0;  // Path traversal attempt
    }
    return 1;
}

void handle_create(int sock, Message* msg) {
    pthread_mutex_lock(&ss_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    // Validate request
    if (!validate_basic_request(msg)) {
        set_message_error(&resp, ERR_PERMISSION_DENIED, "Invalid request parameters");
        send_message(sock, &resp);
        pthread_mutex_unlock(&ss_state.mutex);
        return;
    }
    
    char* path = get_file_path(msg->filename);
    
    // Check if file already exists
    if (access(path, F_OK) == 0) {
        set_message_error(&resp, ERR_FILE_EXISTS, "File already exists");
        send_message(sock, &resp);
        pthread_mutex_unlock(&ss_state.mutex);
        return;
    }
    
    // Create empty file
    FILE* fp = fopen(path, "w");
    if (!fp) {
        set_message_error(&resp, ERR_SERVER_ERROR, "Failed to create file");
        send_message(sock, &resp);
        pthread_mutex_unlock(&ss_state.mutex);
        return;
    }
    fclose(fp);
    
    // Initialize metadata
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO file_metadata (filename, word_count, char_count, sentence_count, last_modified) "
                     "VALUES (?, 0, 0, 0, ?);";
    
    if (sqlite3_prepare_v2(ss_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, time(NULL));
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    resp.error_code = ERR_SUCCESS;
    strcpy(resp.data, "File created");
    send_message(sock, &resp);
    
    char log_buf[256];
    snprintf(log_buf, sizeof(log_buf), "File created: %s", msg->filename);
    log_message("StorageServer", log_buf);
    
    pthread_mutex_unlock(&ss_state.mutex);
}

void handle_read(int sock, Message* msg) {
    pthread_mutex_lock(&ss_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    // Validate request
    if (!validate_basic_request(msg)) {
        set_message_error(&resp, ERR_PERMISSION_DENIED, "Invalid request parameters");
        send_message(sock, &resp);
        pthread_mutex_unlock(&ss_state.mutex);
        return;
    }
    
    char content[BUFFER_SIZE];
    int result = load_file_content(msg->filename, content, sizeof(content));
    
    if (result < 0) {
        set_message_error(&resp, ERR_FILE_NOT_FOUND, "Failed to read file");
        send_message(sock, &resp);
        pthread_mutex_unlock(&ss_state.mutex);
        return;
    }
    
    resp.error_code = ERR_SUCCESS;
    strncpy(resp.data, content, sizeof(resp.data) - 1);
    send_message(sock, &resp);
    
    pthread_mutex_unlock(&ss_state.mutex);
}

void handle_write(int sock, Message* msg) {
    pthread_mutex_lock(&ss_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    // Validate request
    if (!validate_basic_request(msg)) {
        set_message_error(&resp, ERR_PERMISSION_DENIED, "Invalid request parameters");
        send_message(sock, &resp);
        pthread_mutex_unlock(&ss_state.mutex);
        return;
    }
    
    // Parse: sentence_num|word_idx|new_content or just new content for full write
    int sentence_num = -1, word_idx = -1;
    char new_content[BUFFER_SIZE] = {0};
    
    // Try to parse as sentence edit
    if (sscanf(msg->data, "%d|%d|%[^\n]", &sentence_num, &word_idx, new_content) == 3) {
        // Word-level edit
        char current_content[BUFFER_SIZE];
        if (load_file_content(msg->filename, current_content, sizeof(current_content)) < 0) {
            set_message_error(&resp, ERR_FILE_NOT_FOUND, "File not found");
            send_message(sock, &resp);
            pthread_mutex_unlock(&ss_state.mutex);
            return;
        }
        
        // Save undo state
        save_undo_state(msg->filename, current_content);
        
        // Parse sentences
        char sentences[MAX_SENTENCES][MAX_SENTENCE];
        int sentence_count = parse_sentences(current_content, sentences, MAX_SENTENCES);
        
        if (sentence_num < 0 || sentence_num >= sentence_count) {
            set_message_error(&resp, ERR_INVALID_PARAM, "Invalid sentence number");
            send_message(sock, &resp);
            pthread_mutex_unlock(&ss_state.mutex);
            return;
        }
        
        // Parse words in the sentence
        char words[MAX_WORDS_PER_SENTENCE][MAX_WORD];
        int word_count = parse_words(sentences[sentence_num], words, MAX_WORDS_PER_SENTENCE);
        
        if (word_idx < 0 || word_idx >= word_count) {
            set_message_error(&resp, ERR_INVALID_PARAM, "Invalid word index");
            send_message(sock, &resp);
            pthread_mutex_unlock(&ss_state.mutex);
            return;
        }
        
        // Replace word
        strcpy(words[word_idx], new_content);
        
        // Reconstruct sentence
        char new_sentence[MAX_SENTENCE] = {0};
        for (int i = 0; i < word_count; i++) {
            if (i > 0) strcat(new_sentence, " ");
            strcat(new_sentence, words[i]);
        }
        strcpy(sentences[sentence_num], new_sentence);
        
        // Reconstruct content
        char final_content[BUFFER_SIZE] = {0};
        for (int i = 0; i < sentence_count; i++) {
            if (i > 0) strcat(final_content, " ");
            strcat(final_content, sentences[i]);
        }
        
        save_file_content(msg->filename, final_content);
    } else {
        // Full content write or sentence replacement
        char current_content[BUFFER_SIZE];
        if (load_file_content(msg->filename, current_content, sizeof(current_content)) >= 0) {
            save_undo_state(msg->filename, current_content);
        }
        
        save_file_content(msg->filename, msg->data);
    }
    
    resp.error_code = ERR_SUCCESS;
    strcpy(resp.data, "Write successful");
    send_message(sock, &resp);
    
    char log_buf[256];
    snprintf(log_buf, sizeof(log_buf), "File written: %s", msg->filename);
    log_message("StorageServer", log_buf);
    
    pthread_mutex_unlock(&ss_state.mutex);
}

void handle_delete(int sock, Message* msg) {
    pthread_mutex_lock(&ss_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    // Validate request
    if (!validate_basic_request(msg)) {
        set_message_error(&resp, ERR_PERMISSION_DENIED, "Invalid request parameters");
        send_message(sock, &resp);
        pthread_mutex_unlock(&ss_state.mutex);
        return;
    }
    
    char* path = get_file_path(msg->filename);
    
    if (unlink(path) < 0) {
        set_message_error(&resp, ERR_FILE_NOT_FOUND, "Failed to delete file");
        send_message(sock, &resp);
        pthread_mutex_unlock(&ss_state.mutex);
        return;
    }
    
    // Delete metadata
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(ss_state.db, "DELETE FROM file_metadata WHERE filename = ?;", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    // Delete undo state
    char undo_path[MAX_PATH];
    snprintf(undo_path, sizeof(undo_path), "%s/undo/%s", ss_state.data_dir, msg->filename);
    unlink(undo_path);
    
    resp.error_code = ERR_SUCCESS;
    strcpy(resp.data, "File deleted");
    send_message(sock, &resp);
    
    char log_buf[256];
    snprintf(log_buf, sizeof(log_buf), "File deleted: %s", msg->filename);
    log_message("StorageServer", log_buf);
    
    pthread_mutex_unlock(&ss_state.mutex);
}

void handle_stream(int sock, Message* msg) {
    pthread_mutex_lock(&ss_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    // Validate request
    if (!validate_basic_request(msg)) {
        set_message_error(&resp, ERR_PERMISSION_DENIED, "Invalid request parameters");
        send_message(sock, &resp);
        pthread_mutex_unlock(&ss_state.mutex);
        return;
    }
    
    char content[BUFFER_SIZE];
    int result = load_file_content(msg->filename, content, sizeof(content));
    
    if (result < 0) {
        set_message_error(&resp, ERR_FILE_NOT_FOUND, "Failed to read file");
        send_message(sock, &resp);
        pthread_mutex_unlock(&ss_state.mutex);
        return;
    }
    
    // Send success response first
    resp.error_code = ERR_SUCCESS;
    strcpy(resp.data, "STREAM_START");
    send_message(sock, &resp);
    
    // Parse words and stream them
    char sentences[MAX_SENTENCES][MAX_SENTENCE];
    int sentence_count = parse_sentences(content, sentences, MAX_SENTENCES);
    
    for (int i = 0; i < sentence_count; i++) {
        char words[MAX_WORDS_PER_SENTENCE][MAX_WORD];
        int word_count = parse_words(sentences[i], words, MAX_WORDS_PER_SENTENCE);
        
        for (int j = 0; j < word_count; j++) {
            Message word_msg;
            init_message(&word_msg);
            strcpy(word_msg.type, "STREAM_WORD");
            strcpy(word_msg.data, words[j]);
            
            if (send_message(sock, &word_msg) < 0) {
                log_message("StorageServer", "Stream interrupted");
                pthread_mutex_unlock(&ss_state.mutex);
                return;
            }
            
            usleep(100000); // 0.1 second delay
        }
    }
    
    // Send end marker
    Message end_msg;
    init_message(&end_msg);
    strcpy(end_msg.type, "STREAM_END");
    send_message(sock, &end_msg);
    
    char log_buf[256];
    snprintf(log_buf, sizeof(log_buf), "File streamed: %s", msg->filename);
    log_message("StorageServer", log_buf);
    
    pthread_mutex_unlock(&ss_state.mutex);
}

void handle_info(int sock, Message* msg) {
    pthread_mutex_lock(&ss_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    // Validate request
    if (!validate_basic_request(msg)) {
        set_message_error(&resp, ERR_PERMISSION_DENIED, "Invalid request parameters");
        send_message(sock, &resp);
        pthread_mutex_unlock(&ss_state.mutex);
        return;
    }
    
    // Get metadata from database
    sqlite3_stmt* stmt;
    const char* sql = "SELECT word_count, char_count, sentence_count, last_modified FROM file_metadata WHERE filename = ?;";
    
    if (sqlite3_prepare_v2(ss_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int word_count = sqlite3_column_int(stmt, 0);
            int char_count = sqlite3_column_int(stmt, 1);
            int sentence_count = sqlite3_column_int(stmt, 2);
            time_t modified = sqlite3_column_int64(stmt, 3);
            
            char time_str[64];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&modified));
            
            resp.error_code = ERR_SUCCESS;
            snprintf(resp.data, sizeof(resp.data), 
                    "Words: %d | Characters: %d | Sentences: %d | Modified: %s",
                    word_count, char_count, sentence_count, time_str);
        } else {
            set_message_error(&resp, ERR_FILE_NOT_FOUND, "File metadata not found");
        }
        sqlite3_finalize(stmt);
    } else {
        set_message_error(&resp, ERR_SERVER_ERROR, "Database error");
    }
    
    send_message(sock, &resp);
    pthread_mutex_unlock(&ss_state.mutex);
}

void handle_undo(int sock, Message* msg) {
    pthread_mutex_lock(&ss_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    // Validate request
    if (!validate_basic_request(msg)) {
        set_message_error(&resp, ERR_PERMISSION_DENIED, "Invalid request parameters");
        send_message(sock, &resp);
        pthread_mutex_unlock(&ss_state.mutex);
        return;
    }
    
    char undo_content[BUFFER_SIZE];
    int result = load_undo_state(msg->filename, undo_content, sizeof(undo_content));
    
    if (result < 0) {
        set_message_error(&resp, ERR_FILE_NOT_FOUND, "No undo history available");
        send_message(sock, &resp);
        pthread_mutex_unlock(&ss_state.mutex);
        return;
    }
    
    // Save current state as new undo (for redo capability)
    char current_content[BUFFER_SIZE];
    if (load_file_content(msg->filename, current_content, sizeof(current_content)) >= 0) {
        // Restore undo content
        save_file_content(msg->filename, undo_content);
        
        resp.error_code = ERR_SUCCESS;
        strcpy(resp.data, "Undo successful");
        
        char log_buf[256];
        snprintf(log_buf, sizeof(log_buf), "Undo performed: %s", msg->filename);
        log_message("StorageServer", log_buf);
    } else {
        set_message_error(&resp, ERR_FILE_NOT_FOUND, "Current file not found");
    }
    
    send_message(sock, &resp);
    pthread_mutex_unlock(&ss_state.mutex);
}

void handle_replicate(int sock, Message* msg) {
    pthread_mutex_lock(&ss_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    // Save replicated content
    if (save_file_content(msg->filename, msg->data) < 0) {
        set_message_error(&resp, ERR_SERVER_ERROR, "Replication failed");
    } else {
        resp.error_code = ERR_SUCCESS;
        strcpy(resp.data, "Replicated successfully");
        
        char log_buf[256];
        snprintf(log_buf, sizeof(log_buf), "Replicated: %s", msg->filename);
        log_message("StorageServer", log_buf);
    }
    
    send_message(sock, &resp);
    pthread_mutex_unlock(&ss_state.mutex);
}

void handle_checkpoint_ops(int sock, Message* msg) {
    pthread_mutex_lock(&ss_state.mutex);
    
    Message resp;
    init_message(&resp);
    
    // Parse command from data: CREATE|tag or LIST or REVERT|tag
    char cmd[32], tag[64];
    if (sscanf(msg->data, "%[^|]|%s", cmd, tag) < 1) {
        set_message_error(&resp, ERR_INVALID_PARAM, "Invalid checkpoint command");
        send_message(sock, &resp);
        pthread_mutex_unlock(&ss_state.mutex);
        return;
    }
    
    if (strcmp(cmd, "CREATE") == 0) {
        // Load current content
        char content[BUFFER_SIZE];
        if (load_file_content(msg->filename, content, sizeof(content)) < 0) {
            set_message_error(&resp, ERR_FILE_NOT_FOUND, "File not found");
            send_message(sock, &resp);
            pthread_mutex_unlock(&ss_state.mutex);
            return;
        }
        
        // Save checkpoint
        char checkpoint_file[MAX_FILENAME];
        snprintf(checkpoint_file, sizeof(checkpoint_file), "%s_%s_%ld", msg->filename, tag, time(NULL));
        char checkpoint_path[MAX_PATH];
        snprintf(checkpoint_path, sizeof(checkpoint_path), "%s/checkpoints/%s", ss_state.data_dir, checkpoint_file);
        
        FILE* fp = fopen(checkpoint_path, "w");
        if (fp) {
            fputs(content, fp);
            fclose(fp);
            
            // Save to database
            sqlite3_stmt* stmt;
            const char* sql = "INSERT INTO checkpoints (filename, tag, checkpoint_file, created_at) VALUES (?, ?, ?, ?);";
            if (sqlite3_prepare_v2(ss_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 2, tag, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 3, checkpoint_file, -1, SQLITE_STATIC);
                sqlite3_bind_int64(stmt, 4, time(NULL));
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
            
            resp.error_code = ERR_SUCCESS;
            snprintf(resp.data, sizeof(resp.data), "Checkpoint '%s' created", tag);
        } else {
            set_message_error(&resp, ERR_SERVER_ERROR, "Failed to create checkpoint");
        }
    }
    else if (strcmp(cmd, "LIST") == 0) {
        sqlite3_stmt* stmt;
        const char* sql = "SELECT tag, created_at FROM checkpoints WHERE filename = ? ORDER BY created_at DESC;";
        
        if (sqlite3_prepare_v2(ss_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
            
            char result[BUFFER_SIZE] = "Checkpoints:\n";
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* cp_tag = (const char*)sqlite3_column_text(stmt, 0);
                time_t created = sqlite3_column_int64(stmt, 1);
                char time_str[64];
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&created));
                
                char line[256];
                snprintf(line, sizeof(line), "  %s - %s\n", cp_tag, time_str);
                if (strlen(result) + strlen(line) < sizeof(result) - 1) {
                    strcat(result, line);
                }
            }
            
            resp.error_code = ERR_SUCCESS;
            strncpy(resp.data, result, sizeof(resp.data) - 1);
            sqlite3_finalize(stmt);
        }
    }
    else if (strcmp(cmd, "REVERT") == 0) {
        // Find checkpoint
        sqlite3_stmt* stmt;
        const char* sql = "SELECT checkpoint_file FROM checkpoints WHERE filename = ? AND tag = ?;";
        
        if (sqlite3_prepare_v2(ss_state.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, msg->filename, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, tag, -1, SQLITE_STATIC);
            
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* checkpoint_file = (const char*)sqlite3_column_text(stmt, 0);
                char checkpoint_path[MAX_PATH];
                snprintf(checkpoint_path, sizeof(checkpoint_path), "%s/checkpoints/%s", ss_state.data_dir, checkpoint_file);
                
                // Load checkpoint content
                char content[BUFFER_SIZE];
                FILE* fp = fopen(checkpoint_path, "r");
                if (fp) {
                    size_t read = fread(content, 1, sizeof(content) - 1, fp);
                    content[read] = '\0';
                    fclose(fp);
                    
                    // Save current as undo before reverting
                    char current[BUFFER_SIZE];
                    if (load_file_content(msg->filename, current, sizeof(current)) >= 0) {
                        save_undo_state(msg->filename, current);
                    }
                    
                    // Revert to checkpoint
                    save_file_content(msg->filename, content);
                    
                    resp.error_code = ERR_SUCCESS;
                    snprintf(resp.data, sizeof(resp.data), "Reverted to checkpoint '%s'", tag);
                } else {
                    set_message_error(&resp, ERR_CHECKPOINT_NOT_FOUND, "Checkpoint file not found");
                }
            } else {
                set_message_error(&resp, ERR_CHECKPOINT_NOT_FOUND, "Checkpoint not found");
            }
            sqlite3_finalize(stmt);
        }
    }
    
    send_message(sock, &resp);
    pthread_mutex_unlock(&ss_state.mutex);
}
