#ifndef DSP_H
#define DSP_H

#define _POSIX_C_SOURCE 200809L 

#include <nacl/crypto_box.h>
#include <pthread.h>
#include <stddef.h>
#include "libdsp.h"

#define HASH_LENGTH DSP_HASH_LENGTH
#define PUBLIC_KEY_LENGTH crypto_box_PUBLICKEYBYTES

// The connection object describes a current connection with a node, or an
//  on-going key-exchange process.
struct connection {
    pthread_t thread;
    pthread_mutex_t mutex;
    int socket;
    unsigned char *fingerprint;
    char *address;
    struct node *node;      // node gets populated after key-exchange has taken
                            //  place
    struct handler *next;   // The list is ordered in an LRU fasion
};

struct dsp {
    unsigned char *public_key;
    unsigned char *secret_key;
    struct db *db;
    char *address;
    int port;
    char *bootstrap_address;
    // Each bucket contains a list of nodes with a currently-established
    //  connection.
    struct node *bucket[HASH_LENGTH];
    pthread_t listener;
    // A linked-list of open connections
    struct connection *connection;
};

// error.c
#ifdef NDEBUG
# define error(code) new_error(code)
# define trace(error) error
    dsp_error new_error (int code);
#else
# define error(code) new_error(code, __func__, __FILE__, __LINE__)
# define trace(error) trace_error(error, __func__, __FILE__, __LINE__)
    dsp_error new_error (
        int code,
        char const *function,
        char const *file,
        int line
    );
    dsp_error trace_error (
        dsp_error error,
        char const *function,
        char const *file,
        int line
    );
#endif
    void set_error_message (dsp_error error, char *message);
    // Writes error message and trace to stderr.
    void log_error (dsp_error error);

// crypto.c
    // Hash functions
        struct hash *hash (void *in, size_t length);
        // hash_distance computes the distance function between two hashes,
        //  i.e. returning the byte-index at which the two hashes begin to
        //  diverge.
        int hash_distance (struct hash *from, struct hash *to);
    // Base-64 functions
        char *base64_encode (void *in, size_t length);
        unsigned char *base64_decode (char *in, size_t *length);
    // Public-key crypto functions
        dsp_error new_keys (unsigned char **public, unsigned char **secret);

// db.c
    dsp_error db_open (struct db **);
    dsp_error db_close (struct db *);
    dsp_error select_node (
        struct db *db,
        unsigned char *fingerprint,
        struct node **node
    );
    dsp_error insert_node (struct db *db, struct node *node);
    dsp_error update_node (struct db *db, struct node *node);

// net.c
    dsp_error net_listen (struct dsp *dsp);
    dsp_error net_connect (
        struct dsp *dsp,
        char *address,
        struct connection **connection      // OUT: the established connection
    );
    dsp_error net_send (struct dsp *dsp, struct connection *connection, void *message);

// node.c
    struct node {
        struct handler *handler;
        unsigned char fingerprint[HASH_LENGTH];
        unsigned char public_key[PUBLIC_KEY_LENGTH];
        char *address;
        struct node *next;
    };
    dsp_error key_exchange (
        struct dsp *dsp,
        char const *address,
        struct node **node          // OUT: the returned node object
    );
    dsp_error find_node (
        struct dsp *dsp,
        struct hash *fingerprint,   // the fingerprint to find
        struct node **node          // OUT: the returned node object
    );

/*
// msg.c
    // msg_find sends a message to the node(s) nearest to <hash>, requesting the
    //      resource identified by <hash>.  The messaged nodes will propagate
    //      the request through the network until a node owning the resource is
    //      found.  The requested resource can itself be a node.
    //  returns
    //      a null-terminated array of node objects claiming to own the
    //      requested resource.
    struct node *msg_find (struct hash *hash, struct self *self);
    void msg_send (void *payload, struct node *node, struct self *self);
    // msg_handler takes an incoming message from <node> and decides how to
    //      respond.
    void msg_handler (void *message, struct node *node, struct dsp *);

// net.c
    // net_serve retrieves incoming messages and hands them off to msg_handler.
    void net_serve (struct dsp *);
    int net_send (void *message, char *address, struct dsp *);

// node.c
*/
#endif