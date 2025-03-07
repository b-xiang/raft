#include "../../src/byte.h"

#include "../lib/configuration.h"
#include "../lib/heap.h"
#include "../lib/runner.h"

TEST_MODULE(configuration);

/******************************************************************************
 *
 * Fixture
 *
 *****************************************************************************/

struct fixture
{
    FIXTURE_HEAP;
    FIXTURE_CONFIGURATION;
};

static void *setup(const MunitParameter params[], void *user_data)
{
    struct fixture *f = munit_malloc(sizeof *f);
    (void)user_data;
    SETUP_HEAP;
    SETUP_CONFIGURATION;
    return f;
}

static void tear_down(void *data)
{
    struct fixture *f = data;
    TEAR_DOWN_CONFIGURATION;
    TEAR_DOWN_HEAP;
    free(f);
}

/******************************************************************************
 *
 * Helper macros
 *
 *****************************************************************************/

/* Accessors */
#define N_VOTING configurationNumVoting(&f->configuration)
#define INDEX_OF(ID) configurationIndexOf(&f->configuration, ID)
#define INDEX_OF_VOTING(ID) configurationIndexOfVoting(&f->configuration, ID)
#define GET(ID) configurationGet(&f->configuration, ID)

/* Add a server to the fixture's configuration. */
#define ADD_RV(ID, ADDRESS, VOTING) \
    raft_configuration_add(&f->configuration, ID, ADDRESS, VOTING)
#define ADD(...) munit_assert_int(ADD_RV(__VA_ARGS__), ==, 0)
#define ADD_ERROR(RV, ...) munit_assert_int(ADD_RV(__VA_ARGS__), ==, RV)

/* Remove a server from the fixture's configuration */
#define REMOVE_RV(ID) configurationRemove(&f->configuration, ID)
#define REMOVE(...) munit_assert_int(REMOVE_RV(__VA_ARGS__), ==, 0)
#define REMOVE_ERROR(RV, ...) munit_assert_int(REMOVE_RV(__VA_ARGS__), ==, RV)

/* Copy the fixture's configuration into the given one. */
#define COPY_RV(CONF) configurationCopy(&f->configuration, CONF)
#define COPY(...) munit_assert_int(COPY_RV(__VA_ARGS__), ==, 0)
#define COPY_ERROR(RV, ...) munit_assert_int(COPY_RV(__VA_ARGS__), ==, RV)

/* Encode the fixture's configuration into the given buffer. */
#define ENCODE_RV(BUF) configurationEncode(&f->configuration, BUF)
#define ENCODE(...) munit_assert_int(ENCODE_RV(__VA_ARGS__), ==, 0)
#define ENCODE_ERROR(RV, ...) munit_assert_int(ENCODE_RV(__VA_ARGS__), ==, RV)

/* Decode the given buffer into the fixture's configuration. */
#define DECODE_RV(BUF) configurationDecode(BUF, &f->configuration)
#define DECODE(...) munit_assert_int(DECODE_RV(__VA_ARGS__), ==, 0)
#define DECODE_ERROR(RV, ...) munit_assert_int(DECODE_RV(__VA_ARGS__), ==, RV)

/******************************************************************************
 *
 * Assertions
 *
 *****************************************************************************/

/* Assert that the fixture's configuration has n servers. */
#define ASSERT_N(N)                                              \
    {                                                            \
        munit_assert_int(f->configuration.n, ==, N);             \
        if (N == 0) {                                            \
            munit_assert_ptr_null(f->configuration.servers);     \
        } else {                                                 \
            munit_assert_ptr_not_null(f->configuration.servers); \
        }                                                        \
    }

/* Assert that the attributes of the I'th server in the fixture's configuration
 * match the given values. */
#define ASSERT_SERVER(I, ID, ADDRESS, VOTING)                \
    {                                                        \
        struct raft_server *server;                          \
        munit_assert_int(I, <, f->configuration.n);          \
        server = &f->configuration.servers[I];               \
        munit_assert_int(server->id, ==, ID);                \
        munit_assert_string_equal(server->address, ADDRESS); \
        munit_assert_int(server->voting, ==, VOTING);        \
    }

/******************************************************************************
 *
 * configurationNumVoting
 *
 *****************************************************************************/

TEST_SUITE(n_voting);

TEST_SETUP(n_voting, setup);
TEST_TEAR_DOWN(n_voting, tear_down);

/* All servers are voting. */
TEST_CASE(n_voting, all_voters, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "192.168.1.1:666", true);
    ADD(2, "192.168.1.2:666", true);
    munit_assert_int(N_VOTING, ==, 2);
    return MUNIT_OK;
}

/* Return only voting servers. */
TEST_CASE(n_voting, filter, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "192.168.1.1:666", true);
    ADD(2, "192.168.1.2:666", false);
    munit_assert_int(N_VOTING, ==, 1);
    return MUNIT_OK;
}

/******************************************************************************
 *
 * configurationIndexOf
 *
 *****************************************************************************/

TEST_SUITE(index_of);

TEST_SETUP(index_of, setup);
TEST_TEAR_DOWN(index_of, tear_down);

/* If a matching server is found, it's index is returned. */
TEST_CASE(index_of, match, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "192.168.1.1:666", true);
    ADD(2, "192.168.1.2:666", false);
    munit_assert_int(INDEX_OF(2), ==, 1);
    return MUNIT_OK;
}

/* If no matching server is found, the length of the configuration is
 * returned. */
TEST_CASE(index_of, no_match, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "127.0.0.1:666", true);
    munit_assert_int(INDEX_OF(3), ==, f->configuration.n);
    return MUNIT_OK;
}

/******************************************************************************
 *
 * configurationIndexOfVoting
 *
 *****************************************************************************/

TEST_SUITE(index_of_voting);

TEST_SETUP(index_of_voting, setup);
TEST_TEAR_DOWN(index_of_voting, tear_down);

/* The index of the matching voting server (relative to the number of voting
   servers) is returned. */
TEST_CASE(index_of_voting, match, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "192.168.1.1:666", false);
    ADD(2, "192.168.1.2:666", true);
    ADD(3, "192.168.1.3:666", true);
    munit_assert_int(INDEX_OF_VOTING(3), ==, 1);
    return MUNIT_OK;
}

/* If no matching server is found, the length of the configuration is
 * returned. */
TEST_CASE(index_of_voting, no_match, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "192.168.1.1:666", true);
    munit_assert_int(INDEX_OF_VOTING(3), ==, 1);
    return MUNIT_OK;
}

/* If the server exists but is non-voting, the length of the configuration is
 * returned. */
TEST_CASE(index_of_voting, non_voting, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "192.168.1.1:666", false);
    munit_assert_int(INDEX_OF_VOTING(1), ==, 1);
    return MUNIT_OK;
}

/******************************************************************************
 *
 * configurationGet
 *
 *****************************************************************************/

TEST_SUITE(get);

TEST_SETUP(get, setup);
TEST_TEAR_DOWN(get, tear_down);

/* If a matching server is found, it's returned. */
TEST_CASE(get, match, NULL)
{
    struct fixture *f = data;
    const struct raft_server *server;
    (void)params;
    ADD(1, "192.168.1.1:666", true);
    ADD(2, "192.168.1.2:666", false);
    server = GET(2);
    munit_assert_ptr_not_null(server);
    munit_assert_int(server->id, ==, 2);
    munit_assert_string_equal(server->address, "192.168.1.2:666");
    return MUNIT_OK;
}

/* If no matching server is found, NULL is returned. */
TEST_CASE(get, no_match, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "127.0.0.1:666", true);
    munit_assert_ptr_null(GET(3));
    return MUNIT_OK;
}

/******************************************************************************
 *
 * configurationCopy
 *
 *****************************************************************************/

TEST_SUITE(copy);

TEST_SETUP(copy, setup);
TEST_TEAR_DOWN(copy, tear_down);

/* Copy a configuration containing two servers */
TEST_CASE(copy, two, NULL)
{
    struct fixture *f = data;
    struct raft_configuration configuration;
    (void)params;
    ADD(1, "192.168.1.1:666", false);
    ADD(2, "192.168.1.2:666", true);
    COPY(&configuration);
    munit_assert_int(configuration.n, ==, 2);
    munit_assert_int(configuration.servers[0].id, ==, 1);
    munit_assert_int(configuration.servers[1].id, ==, 2);
    raft_configuration_close(&configuration);
    return MUNIT_OK;
}

TEST_GROUP(copy, error);

/* Out of memory */
TEST_CASE(copy, error, oom, NULL)
{
    struct fixture *f = data;
    struct raft_configuration configuration;
    (void)params;
    ADD(1, "192.168.1.1:666", false);
    test_heap_fault_config(&f->heap, 0, 1);
    test_heap_fault_enable(&f->heap);
    COPY_ERROR(RAFT_NOMEM, &configuration);
    return MUNIT_OK;
}

/******************************************************************************
 *
 * raft_configuration_add
 *
 *****************************************************************************/

TEST_SUITE(add);

TEST_SETUP(add, setup);
TEST_TEAR_DOWN(add, tear_down);

/* Add a server to the configuration. */
TEST_CASE(add, one, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "127.0.0.1:666", true);
    ASSERT_N(1);
    ASSERT_SERVER(0, 1, "127.0.0.1:666", true);
    return MUNIT_OK;
}

/* Add two servers to the configuration. */
TEST_CASE(add, two, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "127.0.0.1:666", true);
    ADD(2, "192.168.1.1:666", false);
    ASSERT_N(2);
    ASSERT_SERVER(0, 1, "127.0.0.1:666", true);
    ASSERT_SERVER(1, 2, "192.168.1.1:666", false);
    return MUNIT_OK;
}

TEST_GROUP(add, error);

/* Add a server with an ID which is already in use. */
TEST_CASE(add, error, dup_id, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "127.0.0.1:666", true);
    ADD_ERROR(RAFT_DUPLICATEID, 1, "192.168.1.1:666", false);
    return MUNIT_OK;
}

/* Add a server with an address which is already in use. */
TEST_CASE(add, error, dup_address, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "127.0.0.1:666", true);
    ADD_ERROR(RAFT_DUPLICATEADDRESS, 2, "127.0.0.1:666", false);
    return MUNIT_OK;
}

static char *add_oom_heap_fault_delay[] = {"0", "1", NULL};
static char *add_oom_heap_fault_repeat[] = {"1", NULL};

static MunitParameterEnum add_oom_params[] = {
    {TEST_HEAP_FAULT_DELAY, add_oom_heap_fault_delay},
    {TEST_HEAP_FAULT_REPEAT, add_oom_heap_fault_repeat},
    {NULL, NULL},
};

/* Out of memory. */
TEST_CASE(add, error, oom, add_oom_params)
{
    struct fixture *f = data;
    (void)params;
    test_heap_fault_enable(&f->heap);
    ADD_ERROR(RAFT_NOMEM, 1, "127.0.0.1:666", true);
    return MUNIT_OK;
}

/******************************************************************************
 *
 * configurationRemove
 *
 *****************************************************************************/

TEST_SUITE(remove);

TEST_SETUP(remove, setup);
TEST_TEAR_DOWN(remove, tear_down);

/* Remove the last and only server. */
TEST_CASE(remove, last, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "127.0.0.1:666", true);
    REMOVE(1);
    ASSERT_N(0);
    return MUNIT_OK;
}

/* Remove the first server. */
TEST_CASE(remove, first, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "127.0.0.1:666", true);
    ADD(2, "192.168.1.1:666", false);
    REMOVE(1);
    ASSERT_N(1);
    ASSERT_SERVER(0, 2, "192.168.1.1:666", false);
    return MUNIT_OK;
}

/* Remove a server in the middle. */
TEST_CASE(remove, middle, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "127.0.0.1:666", true);
    ADD(2, "192.168.1.1:666", false);
    ADD(3, "10.0.1.1:666", true);
    REMOVE(2);
    ASSERT_N(2);
    ASSERT_SERVER(0, 1, "127.0.0.1:666", true);
    ASSERT_SERVER(1, 3, "10.0.1.1:666", true);
    return MUNIT_OK;
}

TEST_GROUP(remove, error);

/* Attempts to remove a server with an unknown ID result in an error. */
TEST_CASE(remove, error, unknown, NULL)
{
    struct fixture *f = data;
    (void)params;
    REMOVE_ERROR(RAFT_BADID, 1);
    return MUNIT_OK;
}

/* Out of memory. */
TEST_CASE(remove, error, oom, NULL)
{
    struct fixture *f = data;
    (void)params;
    ADD(1, "127.0.0.1:666", true);
    ADD(2, "192.168.1.1:666", false);
    test_heap_fault_config(&f->heap, 0, 1);
    test_heap_fault_enable(&f->heap);
    REMOVE_ERROR(RAFT_NOMEM, 1);
    return MUNIT_OK;
}

/******************************************************************************
 *
 * configurationEncode
 *
 *****************************************************************************/

TEST_SUITE(encode);

TEST_SETUP(encode, setup);
TEST_TEAR_DOWN(encode, tear_down);

/* Encode a configuration with one server. */
TEST_CASE(encode, one_server, NULL)
{
    struct fixture *f = data;
    struct raft_buffer buf;
    size_t len;
    const void *cursor;
    const char *address = "127.0.0.1:666";
    (void)data;
    (void)params;
    ADD(1, address, true);
    ENCODE(&buf);

    len = 1 + 8 +                  /* Version and n of servers */
          8 + strlen(address) + 1; /* Server */
    len = bytePad64(len);

    munit_assert_int(buf.len, ==, len);

    cursor = buf.base;

    munit_assert_int(byteGet8(&cursor), ==, 1);
    munit_assert_int(byteGet64Unaligned(&cursor), ==, 1);

    munit_assert_int(byteGet64Unaligned(&cursor), ==, 1);
    munit_assert_string_equal(byteGetString(&cursor, strlen(address) + 1),
                              address);
    munit_assert_int(byteGet8(&cursor), ==, 1);

    raft_free(buf.base);

    return MUNIT_OK;
}

/* Encode a configuration with two servers. */
TEST_CASE(encode, two_servers, NULL)
{
    struct fixture *f = data;
    struct raft_buffer buf;
    size_t len;
    const void *cursor;
    const char *address1 = "127.0.0.1:666";
    const char *address2 = "192.168.1.1:666";

    (void)data;
    (void)params;

    ADD(1, address1, false);
    ADD(2, address2, true);
    ENCODE(&buf);

    len = 1 + 8 +                        /* Version and n of servers */
          8 + strlen(address1) + 1 + 1 + /* Server 1 */
          8 + strlen(address2) + 1 + 1;  /* Server 2 */
    len = bytePad64(len);

    munit_assert_int(buf.len, ==, len);

    cursor = buf.base;

    munit_assert_int(byteGet8(&cursor), ==, 1);
    munit_assert_int(byteGet64Unaligned(&cursor), ==, 2);

    munit_assert_int(byteGet64Unaligned(&cursor), ==, 1);
    munit_assert_string_equal(byteGetString(&cursor, strlen(address1) + 1),
                              address1);
    munit_assert_int(byteGet8(&cursor), ==, 0);

    munit_assert_int(byteGet64Unaligned(&cursor), ==, 2);
    munit_assert_string_equal(byteGetString(&cursor, strlen(address2) + 1),
                              address2);
    munit_assert_int(byteGet8(&cursor), ==, 1);

    raft_free(buf.base);

    return MUNIT_OK;
}

TEST_GROUP(encode, error);

/* Out of memory. */
TEST_CASE(encode, error, oom, NULL)
{
    struct fixture *f = data;
    struct raft_buffer buf;
    (void)params;
    test_heap_fault_config(&f->heap, 2, 1);
    test_heap_fault_enable(&f->heap);
    ADD(1, "127.0.0.1:666", true);
    ENCODE_ERROR(RAFT_NOMEM, &buf);
    return MUNIT_OK;
}

/******************************************************************************
 *
 * configurationDecode
 *
 *****************************************************************************/

TEST_SUITE(decode);

TEST_SETUP(decode, setup);
TEST_TEAR_DOWN(decode, tear_down);

/* The decode a payload encoding a configuration with one server */
TEST_CASE(decode, one_server, NULL)
{
    struct fixture *f = data;
    uint8_t bytes[] = {1,                            /* Version */
                       1,   0,   0,   0, 0, 0, 0, 0, /* Number of servers */
                       5,   0,   0,   0, 0, 0, 0, 0, /* Server ID */
                       'x', '.', 'y', 0,             /* Server address */
                       1};                           /* Voting flag */
    struct raft_buffer buf;
    int rv;

    (void)data;
    (void)params;

    buf.base = bytes;
    buf.len = sizeof bytes;

    rv = configurationDecode(&buf, &f->configuration);
    munit_assert_int(rv, ==, 0);

    ASSERT_N(1);
    ASSERT_SERVER(0, 5, "x.y", true);

    return MUNIT_OK;
}

/* The decode size is the size of a raft_server array plus the length of the
 * addresses. */
TEST_CASE(decode, two_servers, NULL)
{
    struct fixture *f = data;
    uint8_t bytes[] = {1,                                /* Version */
                       2,   0,   0,   0,   0,   0, 0, 0, /* Number of servers */
                       5,   0,   0,   0,   0,   0, 0, 0, /* Server ID */
                       'x', '.', 'y', 0,                 /* Server address */
                       1,                                /* Voting flag */
                       3,   0,   0,   0,   0,   0, 0, 0, /* Server ID */
                       '1', '9', '2', '.', '2', 0,       /* Server address */
                       0};                               /* Voting flag */
    struct raft_buffer buf;
    (void)data;
    (void)params;
    buf.base = bytes;
    buf.len = sizeof bytes;
    DECODE(&buf);
    ASSERT_N(2);
    ASSERT_SERVER(0, 5, "x.y", true);
    ASSERT_SERVER(1, 3, "192.2", false);
    return MUNIT_OK;
}

TEST_GROUP(decode, error);

/* Not enough memory of the servers array. */
TEST_CASE(decode, error, oom, NULL)
{
    struct fixture *f = data;
    uint8_t bytes[] = {1,                            /* Version */
                       1,   0,   0,   0, 0, 0, 0, 0, /* Number of servers */
                       5,   0,   0,   0, 0, 0, 0, 0, /* Server ID */
                       'x', '.', 'y', 0,             /* Server address */
                       1};                           /* Voting flag */
    struct raft_buffer buf;
    (void)params;
    test_heap_fault_config(&f->heap, 0, 1);
    test_heap_fault_enable(&f->heap);
    buf.base = bytes;
    buf.len = sizeof bytes;
    DECODE_ERROR(RAFT_NOMEM, &buf);
    return MUNIT_OK;
}

/* If the encoding version is wrong, an error is returned. */
TEST_CASE(decode, error, bad_version, NULL)
{
    struct fixture *f = data;
    uint8_t bytes = 127;
    struct raft_buffer buf;
    (void)data;
    (void)params;
    buf.base = &bytes;
    buf.len = 1;
    DECODE_ERROR(RAFT_MALFORMED, &buf);
    return MUNIT_OK;
}

/* The address of a server is not a nul-terminated string. */
TEST_CASE(decode, error, bad_address, NULL)
{
    struct fixture *f = data;
    uint8_t bytes[] = {1,                            /* Version */
                       1,   0,   0,   0, 0, 0, 0, 0, /* Number of servers */
                       5,   0,   0,   0, 0, 0, 0, 0, /* Server ID */
                       'x', '.', 'y',                /* Server address */
                       1};                           /* Voting flag */
    struct raft_buffer buf;
    (void)data;
    (void)params;
    buf.base = bytes;
    buf.len = sizeof bytes;
    DECODE_ERROR(RAFT_MALFORMED, &buf);
    return MUNIT_OK;
}
