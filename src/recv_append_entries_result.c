#include "recv_append_entries_result.h"
#include "assert.h"
#include "configuration.h"
#include "logging.h"
#include "recv.h"
#include "replication.h"

/* Set to 1 to enable tracing. */
#if 0
#define tracef(MSG, ...) debugf(r, MSG, ##__VA_ARGS__)
#else
#define tracef(MSG, ...)
#endif

int recvAppendEntriesResult(struct raft *r,
                            const unsigned id,
                            const char *address,
                            const struct raft_append_entries_result *result)
{
    int match;
    const struct raft_server *server;
    int rv;

    assert(r != NULL);
    assert(id > 0);
    assert(address != NULL);
    assert(result != NULL);

    if (r->state != RAFT_LEADER) {
        tracef("local server is not leader -> ignore");
        return 0;
    }

    rv = recvEnsureMatchingTerms(r, result->term, &match);
    if (rv != 0) {
        return rv;
    }

    if (match < 0) {
        tracef("local term is higher -> ignore ");
        return 0;
    }

    /* If we have stepped down, abort here.
     *
     * From Figure 3.1:
     *
     *   [Rules for Servers] All Servers: If RPC request or response contains
     *   term T > currentTerm: set currentTerm = T, convert to follower.
     */
    if (match > 0) {
        assert(r->state == RAFT_FOLLOWER);
        return 0;
    }

    assert(result->term == r->current_term);

    /* Ignore responses from servers that have been removed */
    server = configurationGet(&r->configuration, id);
    if (server == NULL) {
        warnf(r, "unknown server -> ignore");
        return 0;
    }

    /* Update the progress of this server, possibly sending further entries. */
    rv = replicationUpdate(r, server, result);
    if (rv != 0) {
        return rv;
    }

    /* We might not be leader anymore if we removed ouselves */
    if (r->state != RAFT_LEADER) {
        return 0;
    }

    /* Commit entries if possible.
     *
     * TODO: trigger an heartbeat if the commit index was updated */
    replicationQuorum(r, result->last_log_index);

    rv = replicationApply(r);
    if (rv != 0) {
        return rv;
    }

    return 0;
}
