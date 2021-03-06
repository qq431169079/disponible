#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "dsp.h"

// Extern functions

void bump_node (struct node *node)
{
    if (!node->previous) return;
    node->previous->next = node->next;
    if (node->next) node->next->previous = node->previous;
    node->previous = NULL;
    node->next = *node->bucket;
    // Assert bucket is non-empty
    assert(node->next);
    node->next->previous = node;
    *node->bucket = node;
}

struct node *return_node (struct hash *fingerprint, struct self *self)
{
    int i = hash_distance(key_fingerprint(keys_public_key(self->keys)),
            fingerprint);
    struct node *node = self->nodes->buckets[i];
    while (node) {
        if (!hash_distance(key_fingerprint(node->key), fingerprint))
            return node;
        node = node->next;
    }
    return NULL;
}

struct node *closest_nodes (struct hash *hash, int limit, struct self *self)
{
    int i = hash_distance(key_fingerprint(keys_public_key(self->keys)), hash);
    struct node **array = calloc(limit + 1, sizeof(struct node *));
    struct node *node = self->nodes->buckets[i];
    for (int j = 0; j < limit;) {
        if (node) {
            array[j++] = node;
            node = node->next;
            continue;
        }
        int offset = node->bucket - self->nodes->buckets - i;
        offset = (offset >= 0) ? 0 - offset - 1 : 0 - offset;
        if (i + offset < 0) {
            if (i - offset > 8 * HASH_LENGTH - 1) return *array;
            node = self->nodes->buckets[i - offset + 1];
        } else if (i + offset > 8 * HASH_LENGTH - 1) {
            if (i - offset == 0) return *array;
            node = self->nodes->buckets[i - offset - 1];
        } else {
            node = self->nodes->buckets[i + offset];
        }
    }
    return *array;
}
