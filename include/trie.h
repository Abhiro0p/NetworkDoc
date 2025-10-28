#ifndef TRIE_H
#define TRIE_H

#include <stdlib.h>
#include <string.h>

#define ALPHABET_SIZE 256

typedef struct TrieNode {
    struct TrieNode* children[ALPHABET_SIZE];
    int is_end_of_word;
    char* filename;
} TrieNode;

typedef struct {
    TrieNode* root;
    int size;
} Trie;

// Trie functions
Trie* trie_create();
void trie_insert(Trie* trie, const char* filename);
int trie_search(Trie* trie, const char* filename);
void trie_delete(Trie* trie, const char* filename);
void trie_free(Trie* trie);
int trie_search_prefix(Trie* trie, const char* prefix, char** results, int max_results);

#endif // TRIE_H
