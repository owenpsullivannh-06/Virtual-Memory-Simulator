#include <stdio.h>
#include <stdlib.h>
#include "symtab.h"
#include <string.h>
#define CHAINSIZE 100

struct Node {
    char *symbol;
    void *value;
    struct Node *left;
    struct Node *right;
    struct Node *parent;
};

struct bstable {
    struct node *head;
};

struct hashitem {
    char *symbol;
    void *value;
    struct hashitem *next;
};

struct SymbolTable {
    struct hashitem* buckets[100];
};

struct Iterator {
    struct SymbolTable* table;
    struct hashitem* current;
    int currentBucket;
} iterator;

struct bstiterator {
    struct Node *current;
};
static unsigned int hash(const char *str) {
    const unsigned int p = 16777619;
    unsigned int hash = 2166136261u;
    while (*str) {
        hash = (hash ^ *str) * p;
        str += 1;
    }
    hash += hash << 13;
    hash ^= hash >> 7;
    hash += hash << 3;
    hash ^= hash >> 17;
    hash += hash << 5;
    return hash;
}

void *symtabCreate(int sizeHint) {
    struct SymbolTable* table = malloc(sizeof(struct SymbolTable));
    if (table == NULL) return NULL;

    for (int i = 0; i < CHAINSIZE; i++) {
        table ->buckets[i] = NULL;
    }
    return (void*) table; //This is the handle.
}

void symtabDelete(void *symtabHandle) {
    struct SymbolTable *table = (struct SymbolTable *) symtabHandle;
    if (table != NULL) {
        for (int i = 0; i < CHAINSIZE; i++) {
            struct hashitem *item = table -> buckets[i];
            while (item!=  NULL) {
                struct hashitem *temp = item;
                item = item->next;
                free(temp->symbol);
                free(temp);
            } //It is necessary to actually free the individual variables. If only the table if freed, the values that populated it are not freed.
        }
    }
    free(table);
}

int symtabInstall(void *symtabHandle, const char *symbol, void *data) {
    struct SymbolTable *table = (struct SymbolTable *) symtabHandle;
    unsigned int index = hash(symbol) % CHAINSIZE;
    struct hashitem *item = table -> buckets[index]; //The individual "bucket"
    while (item != NULL) {
        if (strcmp(item->symbol, symbol) == 0) { //Compares the two strings properly.
            item->value = data;
            return 1; //If its found early, swap the values and return 1 early.
        }
        item = item -> next;
    }
    //Otherwise... Create a new hashitem, and add it at the appropriate index of the list.
    struct hashitem *addthis = malloc(sizeof(struct hashitem));
    if (addthis == NULL) {return 0;}
    
    addthis->symbol = strdup(symbol); //Duplicates the symbol into the symbol position of addthis. 
    addthis->value = data;

    addthis->next = table->buckets[index];
    table->buckets[index] = addthis;
    return 1;
}

void *symtabLookup(void *symtabHandle, const char *symbol) {
    struct SymbolTable *table = (struct SymbolTable *) symtabHandle;
    unsigned int index = hash(symbol) % CHAINSIZE;
    struct hashitem *item = table -> buckets[index]; //The individual "bucket"
    while (item != NULL) {
        if (strcmp(item->symbol, symbol) == 0) {
            return item->value;
        }//The same as install, accept without adding anything. 
        item = item -> next;
    }
    return NULL;
}

void *symtabCreateIterator(void *symtabHandle) {
    struct Iterator* it = (struct Iterator *) malloc(sizeof(struct Iterator));
    if (it == NULL) return NULL;
    it->table = (struct SymbolTable *)symtabHandle;
    
    int index = 0;
    while (index < CHAINSIZE && it->table->buckets[index] == NULL) {
        index += 1;
    } //Making sure to start at the actual starting point rather than a null spot.

    if (index == CHAINSIZE) {
        it->current = NULL;
    }
    it->currentBucket = index;
    if (index < CHAINSIZE) {
        it->current = it->table->buckets[index];
    }

    return it;
}

const char *symtabNext(void *iteratorHandle, void **returnData) {
    struct Iterator *it = (struct Iterator *) iteratorHandle;
    if (it->current == NULL) {
        return NULL;
    }
    
    const char *savesym = it->current->symbol;

    if (returnData != NULL) {
        *returnData = it ->current->value; //The "return" value, or data.
    }

    it-> current = it -> current -> next;
    //Automatically increment, then find out the next non - null entry.
    if (it ->current == NULL) {
        it->currentBucket++;

        while (it ->currentBucket < CHAINSIZE && it -> table -> buckets[it->currentBucket] == NULL ) {
            it->currentBucket++;
        }
        if (it->currentBucket < CHAINSIZE) {
            it -> current = it -> table -> buckets[it->currentBucket];
        }
    }
    return savesym; //the returned symbol.

}


void symtabDeleteIterator(void *iteratorHandle) {
    if (iteratorHandle != NULL) {
        free(iteratorHandle);
    }
}

//This wasn't necessary, but adding the insert logic directly to create would create a monolith of a function, so i separated the concerns.
//Plus, that allows me to use it elsewhere, say if I needed it for some other assignment.
static struct Node* bstinsert(struct Node *node, const char *symbol, void *data) {
    if (node == NULL) {
        struct Node* n = malloc(sizeof(struct Node));
        n -> symbol = strdup(symbol);
        n -> value = data;
        n -> left = NULL;
        n -> right = NULL;
        n -> parent = NULL;
        return n; //Create the new node and return it. This is the base case, position found, so all of the pointers are null.
    } else {
        int cmp = strcmp(symbol, node->symbol); //String compare. 
        if (cmp < 0) { //If symbol is less then current node symbol, go left.
            node->left = bstinsert(node->left, symbol, data);   
            if (node -> left != NULL) { node -> left -> parent = node; }
        }
        else if (cmp > 0) { //if symbol is greater than current node symbol, go right.
            node->right = bstinsert(node->right, symbol, data);
            if (node -> right != NULL) { node -> right -> parent = node; }
        }
        else {
            node -> value = data; //if the symbol is found, just update the data/
        }
        return node;
    }

}

void *symtabCreateBST(void *iteratorHandle) {
    struct Node *root = NULL; 
    struct Iterator *it = (struct Iterator*)iteratorHandle;
    void* data = NULL;
    const char* sym = NULL;
    while ((sym = symtabNext(it, &data)) != NULL) {
        root = bstinsert(root, sym, data); //The entire tree is build off of a single node via pointers. So insert the symbol and data into the root,
        //makethe right left and parent pointers null, and return the root.
    }
    return  root;
}

void *symtabCreateBSTIterator(void *BSTRoot){
    struct Node *n  = (struct Node *) BSTRoot;
    if (n == NULL) {
        return NULL;
    }    
    struct bstiterator* it = malloc(sizeof(struct bstiterator));
    it -> current = n;
    while((it -> current != NULL && it -> current -> left != NULL)) {
        it -> current = it -> current -> left;
    } //Get to the leftmost (smallest) value, return the current position.
    return it;
}

const char *symtabBSTNext(void *BSTiteratorNode, void **returnData) {
    struct bstiterator* it = (struct bstiterator *) BSTiteratorNode;
    if (it  == NULL || it -> current == NULL) {
        return NULL;
    }
    *returnData = it -> current -> value;
    const char *sym = it -> current -> symbol; //Get the current symbol value. Then move to the next.
    
    if (it -> current -> right != NULL) { //there is a right value to traverse to.
        it -> current = it->current -> right; //go right
        while (it -> current -> left != NULL) { //While there are left values, 
            it -> current = it -> current -> left; //go left. This is necessary for an in order traversal.
        }
    } 
    else if (it -> current -> right == NULL) { //If there is no right value in this case.
        while (it -> current -> parent != NULL && it -> current -> parent -> right == it -> current) { //While there is a parent and the iterator is the right child 
            it -> current = it -> current -> parent; //Go up the parent value.
        }
        it -> current = it -> current -> parent; //One more
        return sym; 
    }
    return sym;
}

void symtabDeleteBSTIterator(void *BSTiteratorHandle) {
    struct bstiterator *bs = (struct bstiterator *) BSTiteratorHandle;
    free(bs);
}

void symtabBSTDelete(void *BSTRoot) {
    struct Node *n  = (struct Node *) BSTRoot;
    if (n == NULL) {
        return;
    } //If its null, just return: its completely deleted.
    symtabBSTDelete(n->left);//basically just a recursive in order BST delete
    symtabBSTDelete(n->right);
    free (n -> symbol); //Free the symbol...
    free(n); //then free the node itself.
}