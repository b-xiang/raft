#include "../include/raft.h"

#include "assert.h"
#include "configuration.h"
#include "log.h"
#include "logging.h"
#include "membership.h"
#include "progress.h"
#include "queue.h"
#include "replication.h"
#include "request.h"

/* Set to 1 to enable tracing. */
#if 0
#define tracef(MSG, ...) debugf(r, "apply: " MSG, __VA_ARGS__)
#else
#define tracef(MSG, ...)
#endif

int raft_apply(struct raft *r,
               struct raft_apply *req,
               const struct raft_buffer bufs[],
               const unsigned n,
               raft_apply_cb cb)
{
    raft_index index;
    int rv;

    assert(r != NULL);
    assert(bufs != NULL);
    assert(n > 0);

    if (r->state != RAFT_LEADER) {
        rv = RAFT_NOTLEADER;
        goto err;
    }

    /* Index of the first entry being appended. */
    index = logLastIndex(&r->log) + 1;
    tracef("%u commands starting at %lld", n, index);
    req->type = RAFT_COMMAND;
    req->index = index;
    req->cb = cb;

    /* Append the new entries to the log. */
    rv = logAppendCommands(&r->log, r->current_term, bufs, n);
    if (rv != 0) {
        goto err;
    }

    QUEUE_PUSH(&r->leader_state.requests, &req->queue);

    rv = replicationTrigger(r, index);
    if (rv != 0) {
        goto err_after_log_append;
    }

    return 0;

err_after_log_append:
    logDiscard(&r->log, index);
    QUEUE_REMOVE(&req->queue);
err:
    assert(rv != 0);
    return rv;
}

int raft_barrier(struct raft *r, struct raft_barrier *req, raft_barrier_cb cb)
{
    raft_index index;
    struct raft_buffer buf;
    int rv;

    if (r->state != RAFT_LEADER) {
        rv = RAFT_NOTLEADER;
        goto err;
    }

    /* TODO: use a completely empty buffer */
    buf.len = 8;
    buf.base = raft_malloc(buf.len);

    if (buf.base == NULL) {
        rv = RAFT_NOMEM;
        goto err;
    }

    /* Index of the barrier entry being appended. */
    index = logLastIndex(&r->log) + 1;
    tracef("barrier starting at %lld", index);
    req->type = RAFT_BARRIER;
    req->index = index;
    req->cb = cb;

    rv = logAppend(&r->log, r->current_term, RAFT_BARRIER, &buf, NULL);
    if (rv != 0) {
        goto err_after_buf_alloc;
    }

    QUEUE_PUSH(&r->leader_state.requests, &req->queue);

    rv = replicationTrigger(r, index);
    if (rv != 0) {
        goto err_after_log_append;
    }

    return 0;

err_after_log_append:
    logDiscard(&r->log, index);
    QUEUE_REMOVE(&req->queue);
err_after_buf_alloc:
    raft_free(buf.base);
err:
    return rv;
}

static int changeConfiguration(struct raft *r,
                               struct raft_change *req,
                               const struct raft_configuration *configuration)
{
    raft_index index;
    raft_term term = r->current_term;
    int rv;

    /* Index of the entry being appended. */
    index = logLastIndex(&r->log) + 1;

    /* Encode the new configuration and append it to the log. */
    rv = logAppendConfiguration(&r->log, term, configuration);
    if (rv != 0) {
        goto err;
    }

    if (configuration->n != r->configuration.n) {
        rv = progressRebuildArray(r, configuration);
        if (rv != 0) {
            goto err;
        }
    }

    /* Update the current configuration if we've created a new object. */
    if (configuration != &r->configuration) {
        raft_configuration_close(&r->configuration);
        r->configuration = *configuration;
    }

    req->type = RAFT_CHANGE;
    req->index = index;
    QUEUE_PUSH(&r->leader_state.requests, &req->queue);

    /* Start writing the new log entry to disk and send it to the followers. */
    rv = replicationTrigger(r, index);
    if (rv != 0) {
        /* TODO: restore the old next/match indexes and configuration. */
        goto err_after_log_append;
    }

    r->configuration_uncommitted_index = index;

    return 0;

err_after_log_append:
    logTruncate(&r->log, index);

err:
    assert(rv != 0);
    return rv;
}

int raft_add(struct raft *r,
             struct raft_change *req,
             unsigned id,
             const char *address,
             raft_change_cb cb)
{
    struct raft_configuration configuration;
    int rv;

    rv = membershipCanChangeConfiguration(r);
    if (rv != 0) {
        return rv;
    }

    tracef("add server: id %d, address %s", id, address);

    /* Make a copy of the current configuration, and add the new server to
     * it. */
    rv = configurationCopy(&r->configuration, &configuration);
    if (rv != 0) {
        goto err;
    }

    rv = raft_configuration_add(&configuration, id, address, false);
    if (rv != 0) {
        goto err_after_configuration_copy;
    }

    req->cb = cb;

    rv = changeConfiguration(r, req, &configuration);
    if (rv != 0) {
        goto err_after_configuration_copy;
    }

    assert(r->leader_state.change == NULL);
    r->leader_state.change = req;

    return 0;

err_after_configuration_copy:
    raft_configuration_close(&configuration);
err:
    assert(rv != 0);
    return rv;
}

int raft_promote(struct raft *r,
                 struct raft_change *req,
                 unsigned id,
                 raft_change_cb cb)
{
    const struct raft_server *server;
    size_t server_index;
    raft_index last_index;
    int rv;

    rv = membershipCanChangeConfiguration(r);
    if (rv != 0) {
        return rv;
    }

    debugf(r, "promote server: id %d", id);

    server = configurationGet(&r->configuration, id);
    if (server == NULL) {
        rv = RAFT_BADID;
        goto err;
    }

    if (server->voting) {
        rv = RAFT_ALREADYVOTING;
        goto err;
    }

    server_index = configurationIndexOf(&r->configuration, id);
    assert(server_index < r->configuration.n);

    last_index = logLastIndex(&r->log);

    req->cb = cb;

    assert(r->leader_state.change == NULL);
    r->leader_state.change = req;

    /* If the log of this non-voting server is already up-to-date, we can ask
     * its promotion immediately. */
    if (progressMatchIndex(r, server_index) == last_index) {
        r->configuration.servers[server_index].voting = true;

        rv = changeConfiguration(r, req, &r->configuration);
        if (rv != 0) {
            r->configuration.servers[server_index].voting = false;
            return rv;
        }

        return 0;
    }

    r->leader_state.promotee_id = server->id;

    /* Initialize the first catch-up round. */
    r->leader_state.round_number = 1;
    r->leader_state.round_index = last_index;
    r->leader_state.round_start = r->io->time(r->io);

    /* Immediately initiate an AppendEntries request. */
    rv = replicationProgress(r, server_index);
    if (rv != 0 && rv != RAFT_NOCONNECTION) {
        /* This error is not fatal. */
        debugf(r, "failed to send append entries to server %ld: %s (%d)",
               server->id, raft_strerror(rv), rv);
    }

    return 0;

err:
    assert(rv != 0);

    return rv;
}

int raft_remove(struct raft *r,
                struct raft_change *req,
                unsigned id,
                raft_change_cb cb)
{
    const struct raft_server *server;
    struct raft_configuration configuration;
    int rv;

    rv = membershipCanChangeConfiguration(r);
    if (rv != 0) {
        return rv;
    }

    server = configurationGet(&r->configuration, id);
    if (server == NULL) {
        rv = RAFT_BADID;
        goto err;
    }

    debugf(r, "remove server: id %d", id);

    /* Make a copy of the current configuration, and remove the given server
     * from it. */
    rv = configurationCopy(&r->configuration, &configuration);
    if (rv != 0) {
        goto err;
    }

    rv = configurationRemove(&configuration, id);
    if (rv != 0) {
        goto err_after_configuration_copy;
    }

    req->cb = cb;

    rv = changeConfiguration(r, req, &configuration);
    if (rv != 0) {
        goto err_after_configuration_copy;
    }

    assert(r->leader_state.change == NULL);
    r->leader_state.change = req;

    return 0;

err_after_configuration_copy:
    raft_configuration_close(&configuration);

err:
    assert(rv != 0);
    return rv;
}
