#include "../../include/trie.h"
#include <stdio.h>

static TrieNode* create_node() {
    TrieNode* node = (TrieNode*)calloc(1, sizeof(TrieNode));
    node->is_end_of_word = 0;
    node->filename = NULL;
    for (int i = 0; i < ALPHABET_SIZE; i++) {
        node->children[i] = NULL;
    }
    return node;
}

Trie* trie_create() {
    Trie* trie = (Trie*)malloc(sizeof(Trie));
    trie->root = create_node();
    trie->size = 0;
    return trie;
}

void trie_insert(Trie* trie, const char* filename) {
    TrieNode* current = trie->root;
    
    for (int i = 0; filename[i] != '\0'; i++) {
        unsigned char index = (unsigned char)filename[i];
        if (current->children[index] == NULL) {
            current->children[index] = create_node();
        }
        current = current->children[index];
    }
    
    if (!current->is_end_of_word) {
        trie->size++;
    }
    current->is_end_of_word = 1;
    if (current->filename) {
        free(current->filename);
    }
    current->filename = strdup(filename);
}

int trie_search(Trie* trie, const char* filename) {
    TrieNode* current = trie->root;
    
    for (int i = 0; filename[i] != '\0'; i++) {
        unsigned char index = (unsigned char)filename[i];
        if (current->children[index] == NULL) {
            return 0;
        }
        current = current->children[index];
    }
    
    return (current != NULL && current->is_end_of_word);
}

static int delete_helper(TrieNode* node, const char* filename, int depth) {
    if (node == NULL) return 0;
    
    if (filename[depth] == '\0') {
        if (node->is_end_of_word) {
            node->is_end_of_word = 0;
            if (node->filename) {
                free(node->filename);
                node->filename = NULL;
            }
        }
        
        // Check if node has any children
        for (int i = 0; i < ALPHABET_SIZE; i++) {
            if (node->children[i] != NULL) {
                return 0; // Don't delete this node
            }
        }
        return 1; // Can delete this node
    }
    
    unsigned char index = (unsigned char)filename[depth];
    if (delete_helper(node->children[index], filename, depth + 1)) {
        free(node->children[index]);
        node->children[index] = NULL;
        
        // Check if current node should be deleted
        if (!node->is_end_of_word) {
            for (int i = 0; i < ALPHABET_SIZE; i++) {
                if (node->children[i] != NULL) {
                    return 0;
                }
            }
            return 1;
        }
    }
    
    return 0;
}

void trie_delete(Trie* trie, const char* filename) {
    if (trie_search(trie, filename)) {
        delete_helper(trie->root, filename, 0);
        trie->size--;
    }
}

static void free_node(TrieNode* node) {
    if (node == NULL) return;
    
    for (int i = 0; i < ALPHABET_SIZE; i++) {
        if (node->children[i] != NULL) {
            free_node(node->children[i]);
        }
    }
    
    if (node->filename) {
        free(node->filename);
    }
    free(node);
}

void trie_free(Trie* trie) {
    if (trie) {
        free_node(trie->root);
        free(trie);
    }
}

static void collect_words(TrieNode* node, char* prefix, int depth, char** results, int* count, int max_results) {
    if (node == NULL || *count >= max_results) return;
    
    if (node->is_end_of_word && node->filename) {
        results[*count] = strdup(node->filename);
        (*count)++;
    }
    
    for (int i = 0; i < ALPHABET_SIZE && *count < max_results; i++) {
        if (node->children[i] != NULL) {
            prefix[depth] = (char)i;
            prefix[depth + 1] = '\0';
            collect_words(node->children[i], prefix, depth + 1, results, count, max_results);
        }
    }
}

int trie_search_prefix(Trie* trie, const char* prefix, char** results, int max_results) {
    TrieNode* current = trie->root;
    
    // Navigate to prefix node
    for (int i = 0; prefix[i] != '\0'; i++) {
        unsigned char index = (unsigned char)prefix[i];
        if (current->children[index] == NULL) {
            return 0;
        }
        current = current->children[index];
    }
    
    // Collect all words with this prefix
    char buffer[512];
    strncpy(buffer, prefix, sizeof(buffer) - 1);
    int count = 0;
    collect_words(current, buffer, strlen(prefix), results, &count, max_results);
    return count;
}
