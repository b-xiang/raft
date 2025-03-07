/**
 * Create server-side sockets to be used in tests.
 */

#ifndef TEST_TCP_H
#define TEST_TCP_H

#include "munit.h"

/**
 * A test TCP host that can listen for incoming connections and establish
 * outgoing connections.
 */
struct test_tcp
{
    struct
    {
        int socket;        /* Socket listening to incoming connections */
        char address[128]; /* IPv4 address of the server, with port */
    } server;
    struct
    {
        int socket; /* Socket connected to another host */
    } client;
};

/* Fixture helpers. */
#define FIXTURE_TCP struct test_tcp tcp;
#define SETUP_TCP test_tcp_setup(params, &f->tcp)
#define TEAR_DOWN_TCP test_tcp_tear_down(&f->tcp)

/**
 * Bind the server socket of the given test TCP host to localhost and start
 * listening to it.
 */
void test_tcp_setup(const MunitParameter params[], struct test_tcp *t);

void test_tcp_tear_down(struct test_tcp *t);

/**
 * Connect the client socket to the given port on localhost.
 */
void test_tcp_connect(struct test_tcp *t, int port);

/**
 * Close the client socket.
 */
void test_tcp_close(struct test_tcp *t);

/**
 * Send data using the client socket.
 */
void test_tcp_send(struct test_tcp *t, const void *buf, int len);

/**
 * Accept inbound client connection and return the relevant socket.
 */
int test_tcp_accept(struct test_tcp *t);

/**
 * Close the server socket.
 */
void test_tcp_stop(struct test_tcp *t);


#endif /* TEST_TCP_H */
