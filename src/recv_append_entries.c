#include "recv_append_entries.h"
#include "assert.h"
#include "convert.h"
#include "log.h"
#include "logging.h"
#include "recv.h"
#include "replication.h"

/* Set to 1 to enable tracing. */
#if 0
#define tracef(MSG, ...) debugf(r, MSG, ##__VA_ARGS__)
#else
#define tracef(MSG, ...)
#endif

static void sendCb(struct raft_io_send *req, int status)
{
    (void)status;
    raft_free(req);
}

int recvAppendEntries(struct raft *r,
                      const unsigned id,
                      const char *address,
                      const struct raft_append_entries *args)
{
    struct raft_io_send *req;
    struct raft_message message;
    struct raft_append_entries_result *result = &message.append_entries_result;
    int match;
    bool async;
    int rv;

    assert(r != NULL);
    assert(id > 0);
    assert(args != NULL);
    assert(address != NULL);

    result->rejected = args->prev_log_index;
    result->last_log_index = logLastIndex(&r->log);

    rv = recvEnsureMatchingTerms(r, args->term, &match);
    if (rv != 0) {
        return rv;
    }

    /* From Figure 3.1:
     *
     *   AppendEntries RPC: Receiver implementation: Reply false if term <
     *   currentTerm.
     */
    if (match < 0) {
        tracef("local term is higher -> reject ");
        goto reply;
    }

    /* If we get here it means that the term in the request matches our current
     * term or it was higher and we have possibly stepped down, because we
     * discovered the current leader:
     *
     * From Figure 3.1:
     *
     *   Rules for Servers: Candidates: if AppendEntries RPC is received from
     *   new leader: convert to follower.
     *
     * From Section §3.4:
     *
     *   While waiting for votes, a candidate may receive an AppendEntries RPC
     *   from another server claiming to be leader. If the leader’s term
     *   (included in its RPC) is at least as large as the candidate’s current
     *   term, then the candidate recognizes the leader as legitimate and
     *   returns to follower state. If the term in the RPC is smaller than the
     *   candidate’s current term, then the candidate rejects the RPC and
     *   continues in candidate state.
     *
     * From state diagram in Figure 3.3:
     *
     *   [candidate]: discovers current leader -> [follower]
     *
     * Note that it should not be possible for us to be in leader state, because
     * the leader that is sending us the request should have either a lower term
     * (and in that case we reject the request above), or a higher term (and in
     * that case we step down). It can't have the same term because at most one
     * leader can be elected at any given term.
     */
    assert(r->state == RAFT_FOLLOWER || r->state == RAFT_CANDIDATE);
    assert(r->current_term == args->term);

    if (r->state == RAFT_CANDIDATE) {
        /* The current term and the peer one must match, otherwise we would have
         * either rejected the request or stepped down to followers. */
        assert(match == 0);
        debugf(r, "discovered leader -> step down ");
        convertToFollower(r);
    }

    assert(r->state == RAFT_FOLLOWER);

    /* Update current leader because the term in this AppendEntries RPC is up to
     * date. */
    rv = recvUpdateLeader(r, id, address);
    if (rv != 0) {
        return rv;
    }

    /* Reset the election timer. */
    r->election_timer_start = r->io->time(r->io);

    /* If we are installing a snapshot, ignore these entries. TODO: we should do
     * something smarter, e.g. buffering the entries in the I/O backend, which
     * should be in charge of serializing everything. */
    if (r->snapshot.put.data != NULL && args->n_entries > 0) {
        return 0;
    }

    rv = replicationAppend(r, args, &result->rejected, &async);
    if (rv != 0) {
        return rv;
    }

    if (async) {
        return 0;
    }

    /* Echo back to the leader the point that we reached. */
    result->last_log_index = r->last_stored;

reply:
    result->term = r->current_term;

    /* Free the entries batch, if any. */
    if (args->n_entries > 0 && args->entries[0].batch != NULL) {
        raft_free(args->entries[0].batch);
    }

    if (args->entries != NULL) {
        raft_free(args->entries);
    }

    message.type = RAFT_IO_APPEND_ENTRIES_RESULT;
    message.server_id = id;
    message.server_address = address;

    req = raft_malloc(sizeof *req);
    if (req == NULL) {
        return RAFT_NOMEM;
    }

    rv = r->io->send(r->io, req, &message, sendCb);
    if (rv != 0) {
        raft_free(req);
        return rv;
    }

    return 0;
}
