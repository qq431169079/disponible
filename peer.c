/// implementing header
#include "self.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/// static function declarations
//static void write_peer_table(struct peers *);
static void split_bucket(struct peers *, struct bucket *);
static void merge_buckets(struct bucket *);
static void parse_bucket(struct peers *, char *, int);

// peer_create_list initializes a peers object
void peer_create_tree(struct peers *peers){
    peers->bucket_size = DEFAULT_BUCKET_SIZE;
    peers->max_depth = DEFAULT_BUCKET_MAX_DEPTH;
}

// peer_read_tree loads the local peer tree and updates peers
// the local tree is arranged as one file per leaf bucket
// the directory hierarchy mimics the tree structure of buckets, but skips
// nodes in groups of 8, i.e. up to 2^8 nodes in a given directory
// nodes (directories and bucket files) are named corresponding to their sub-
// prefix in relation to their parent directory (in hexadecimal), and pre-pended
// by their prefix_length (in decimal) and a dash
// e.g. file '32-ef' in the directory that represents prefix 'deadbe' (i.e. the
//  hierarchy 'peers/8-de/16-ad/24-be/') would represent the leaf bucket with
//  prefix 'deadbeef'
void peer_read_tree(struct peers *peers, DIR *rootdp){
    struct dirent de, *result;
    int err;
    while (!(err = readdir_r(rootdp, &de, &result)) && result){
        // ignore hidden files and directories
        if (*de.d_name == '.') continue;
        struct stat sb;
        int rootfd = dirfd(rootdp);
        assert(rootfd > 0);
        if (fstatat(rootfd, de.d_name, &sb, 0)) break; // error
        if (S_ISDIR(sb.st_mode) || S_ISREG(sb.st_mode)){
            int fd = openat(rootfd, de.d_name, O_RDONLY);
            if (fd == -1){
                if (errno == EACCES); // user error
                else ; // system error
            }
            else if (S_ISDIR(sb.st_mode)){
                DIR *dp = fdopendir(fd);
                if (!dp); //error 
                peer_read_tree(peers, dp);
            }
            else if (S_ISREG(sb.st_mode)){
                byte *buf = malloc(sb.st_size);
                if (!buf); // system error
                int i = 0;
                for (int ret = 0; sb.st_size - i &&
                        (ret = read(fd, buf + i, sb.st_size - i)); i += ret)
                    if (ret == -1); // system error
                if (close(fd)); // system error
                parse_bucket(peers, (char *) buf, i);
                free(buf);
            }
        }
        else ; // user error
    }
    assert(!err);
    if (closedir(rootdp)) assert(false);
}

// peer_find traverses the peer tree and returns the peer corresponding to the
// passed fingerprint, and updates *b to the peer's bucket
// if the peer is absent, peer_find returns NULL and updates *b to the bucket
// it would belong to
// *b needs to be initialized to where peer_find should begin the search,
// usually to peers.root
struct peer *peer_find(struct bucket **b, byte *fingerprint){
    assert(*b);
    while (!(*b)->head){
        if (hash_cmp(fingerprint, (*b)->left->prefix) < 0) *b = (*b)->right;
        else *b = (*b)->left;
    }
    struct peer *peer = (*b)->head;
    for (; peer; peer = peer->next)
        if (!hash_cmp(peer->fingerprint, fingerprint)) return peer;
    return NULL;
}

// peer_add initializes a peer in its respective bucket
// returns the initialized peer, or NULL if it already existed
struct peer *peer_add(struct peers *peers, byte *fingerprint){
    struct bucket *b = &peers->root;
    //TODO add bucket structure mutex
    if (peer_find(&b, fingerprint)) return NULL;
    if (b->count == peers->bucket_size){
        if (b->depth == peers->max_depth)
            // bucket can't be split, range is full
            //TODO: test responsiveness of existing peers
            return NULL;
        split_bucket(peers, b);
        b = hash_cmp(fingerprint, b->left->prefix) < 0 ? b->right : b->left;
    }
    struct peer *peer = calloc(1, sizeof(struct peer));
    if (!peer); // system error
    memcpy(peer->fingerprint, fingerprint, DIGEST_LENGTH);
    time(&peer->last_recv);
    if (b->head){
        peer->next = b->head;
        b->head->prev = peer;
        b->head = peer;
        b->count++;
    }
    else b->head = peer;
    return peer;
}

// peer_remove deletes a peer from the tree
void peer_remove(struct peers *peers, struct peer *peer){
    assert(peers && peer);
    struct bucket *b = &peers->root;
    if (!peer->prev){
        peer_find(&b, peer->fingerprint);
        if (!peer->next){
            b->head = NULL;
            b = b->parent;
            merge_buckets(b);
        }
        else b->head = peer->next;
    }
    else peer->prev->next = peer->next;
    if (peer->next)
        peer->next->prev = peer->prev;
    // assert no duplicates
    assert(peer_find(&b, peer->fingerprint));
    free(peer);
}

// split_bucket splits the passed bucket
// only to be called on a leaf bucket, if < max_depth
void split_bucket(struct peers *peers, struct bucket *b){
    assert(b->depth < peers->max_depth);
    assert(!b->left && !b->right);
    struct bucket *left = calloc(1, sizeof(struct bucket));
    if (!b->left); //error
    struct bucket *right = calloc(1, sizeof(struct bucket));
    if (!b->right); //error
    int pref_len = b->prefix_length + 1;
    left->prefix_length = right->prefix_length = pref_len;
    left->depth = right->depth = b->depth + 1;
    left->parent = right->parent = b;
    memcpy(left->prefix, b->prefix, DIGEST_LENGTH);
    left->prefix[pref_len / 8] |= 1 << (8 - (pref_len % 8));
    memcpy(right->prefix, b->prefix, DIGEST_LENGTH);
    left->right = right;
    right->left = left;
    if (b->left){
        b->left->right = left;
        left->left = b->left;
    }
    if (b->right){
        b->right->left = right;
        right->right = b->right;
    }
    b->left = left;
    b->right = right;
    struct peer *p_last_left = NULL;
    struct peer *p_last_right = NULL;
    struct peer *p_next;
    for (struct peer *p = b->head; p; p = p_next){
        p_next = p->next;
        if (hash_cmp(p->fingerprint, left->prefix) < 0){
            if (!p_last_right) right->head = p;
            p->prev = p_last_right;
            p_last_right->next = p;
            right->count++;
        }
        else {
            if (!p_last_left) left->head = p;
            p->prev = p_last_left;
            p_last_left->next = p;
            left->count++;
        }
    }
    p_last_left->next = NULL;
    p_last_right->next = NULL;
    b->head = NULL;
    b->count = 0;
}

// merge_buckets merges two leaves with the same parent
//TODO: merge keeping the LRU property (only if ever used with both buckets non-
// empty)
void merge_buckets(struct bucket *parent){
    if (parent->left->head){
        parent->head = parent->left->head;
        parent->count = parent->left->count;
    }
    if (parent->right->head){
        if (parent->head){
            struct peer *last;
            for (last = parent->head; last->next; last = last->next);
            last->next = parent->right->head;
            parent->right->head->prev = last;
        }
        else parent->head = parent->right->head;
        parent->count += parent->right->count;
    }
    free(parent->left->prefix);
    free(parent->left);
    free(parent->right->prefix);
    free(parent->right);
    parent->left = parent->right = NULL;
}

// parse_bucket parses the file format found in the local tree and adds each
// peer to peers.root
// file format consists of each peer on a 2-line block, the first line
// consisting of their base64-encoded public key, the second line beginning with
// a 4-space indent followed by their fqdn or IP address and port number,
// base64-encoded hmac key, sequence number, and last-received timestamp,
// delimited by spaces
// peer ordering in the bucket file is according to availability/uptime of the
// peer
void parse_bucket(struct peers *peers, char *p, int l){
    while(0 < l){
        // extract public key
        char *tmp = strstr(p, "\n    ");
        if (!tmp || tmp > p); // user error
        if (tmp - p != util_base64_encoded_size(PUB_KEY_LENGTH)); // user error
        tmp[0] = '\0';
        byte pub_key[PUB_KEY_LENGTH];
        util_base64_decode(pub_key, p);
        l -= tmp + 5 - p;
        p = tmp + 5;
        // extract fqdn or IP and port
        int i = 0;
        for (; i < l && p[i] != ' '; i++);
        if (i == l || p[i] != ' '); // user error
        p[i] = '\0';
        struct address addr;
        if (*p == '['){
            tmp = memchr(p, ']', i);
            if (!tmp || *(tmp + 1) != ':'); // user error
            *tmp = '\0';
            if (!inet_pton(AF_INET6, p + 1, addr.ip)); // user error
            addr.ip_version = ipv6;
            tmp += 2;
        }
        else {
            tmp = memchr(p, ':', i);
            if (!tmp); // user error
            *tmp = '\0';
            if (!inet_pton(AF_INET, p, addr.ip)){
                addr.fqdn = malloc(strlen(p) + 1);
                if (!addr.fqdn); // system error
                strcpy(addr.fqdn, p);
                if (strlen(addr.fqdn) != tmp - p); // user error
                //TOOD: check fqdn integrity
            }
            tmp++;
        }
        if (p + i == tmp); // user error
        long port = strtol(tmp, &tmp, 10);
        if (*tmp != '\0' || port > 0xffff); // user error
        addr.udp_port = (uint16_t) port;
        p += i + 1;
        l -= i + 1;
        // extract hmac key
        for (i = 0; i < l && p[i] != ' '; i++);
        if (i == l || p[i] != ' '); // user error
        p[i] = '\0';
        if (i % 4 != 0 || util_base64_decoded_size(p) != HMAC_KEY_LENGTH)
            ; // user error
        byte hmac_key[HMAC_KEY_LENGTH];
        util_base64_decode(hmac_key, p);
        p += i + 1;
        l -= i + 1;
        // extract sequence number
        for (i = 0; i < l && p[i] != ' '; i++);
        if (i == l || p[i] != ' '); // user error
        p[i] = '\0';
        long sequence_no = strtol(p, &tmp, 10);
        if (*tmp != '\0' || sequence_no < 0); // user error
        p += i + 1;
        l -= i + 1;
        // extract last received time
        time_t last_recv = 0;
        for (i = 0; i < l && p[i] != '\n'; i++){
            last_recv *= 10;
            if (!(p[i] <= '9' && p[i] >= '0')); // user error
            last_recv += p[i] - '0';
        }
        if (i == l) break;
        else if (p[i] != '\n'); // user error
        p += i + 1;
        l -= i + 1;
    }
}
