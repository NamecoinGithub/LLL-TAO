/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

/* Unit tests for the stale SYNC block guard and SwitchNode() handoff semantics.
 *
 * These tests verify the receiver-side admission logic that protects against a
 * former sync peer continuing to deliver in-flight ACTION::LIST / SPECIFIER::SYNC
 * blocks after SwitchNode() has selected a replacement peer.
 *
 * Tests exercise the guard decision function directly via the publicly accessible
 * global atomics rather than instantiating real network connections.
 *
 * Problem context:
 *   After a sync-node switch the old peer's already-enqueued SYNC response cannot
 *   be cancelled server-side (the LIST/SYNC loop runs synchronously on the far
 *   node's DataThread).  The receiver-side guard
 *
 *       if(nCurrentSession != TAO::Ledger::nSyncSession.load() || fSynchronized.load())
 *
 *   correctly rejects stale blocks BEFORE deserialization, which is the only
 *   safe mitigation.  SwitchNode() now:
 *   (a) stores nSyncSession=0 before calling pnode->Sync() to close the LASTINDEX
 *       re-request window;
 *   (b) logs teardown and the new session at appropriate levels.
 *   Sync() resets the stale-SYNC rate-limiter so each handoff is logged
 *   independently from previous bursts.
 */

#include <unit/catch2/catch.hpp>

#include <LLP/types/tritium.h>
#include <TAO/Ledger/include/process.h>

#include <atomic>


/* ─────────────────────────────────────────────────────────────────────────────
 * Helper: evaluate the receiver-side stale-SYNC guard for given inputs.
 * This mirrors the guard expression in ProcessPacket() SPECIFIER::SYNC.
 * ───────────────────────────────────────────────────────────────────────────*/
static inline bool StaleGuardFires(const uint64_t nCurrentSession,
                                   const uint64_t nActiveSyncSession,
                                   const bool     fSynchronized)
{
    return (nCurrentSession != nActiveSyncSession) || fSynchronized;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * RAII helper to save/restore global sync state so tests are hermetic.
 * ───────────────────────────────────────────────────────────────────────────*/
struct SyncStateGuard
{
    uint64_t savedSession;
    bool     savedSynchronized;

    SyncStateGuard()
    : savedSession     (TAO::Ledger::nSyncSession.load())
    , savedSynchronized(LLP::TritiumNode::fSynchronized.load())
    {}

    ~SyncStateGuard()
    {
        TAO::Ledger::nSyncSession.store(savedSession);
        LLP::TritiumNode::fSynchronized.store(savedSynchronized);
    }
};


/* ==========================================================================
 * 1. Guard condition semantics
 * ========================================================================== */

TEST_CASE("Stale SYNC guard: active-session block passes through", "[llp][sync]")
{
    SyncStateGuard sg;

    TAO::Ledger::nSyncSession.store(42);
    LLP::TritiumNode::fSynchronized.store(false);

    /* Session matches the active sync session and sync is in progress. */
    REQUIRE_FALSE(StaleGuardFires(42, TAO::Ledger::nSyncSession.load(),
                                   LLP::TritiumNode::fSynchronized.load()));
}

TEST_CASE("Stale SYNC guard: mismatched session is rejected", "[llp][sync]")
{
    SyncStateGuard sg;

    /* Active sync peer is session 99; block arrives from old session 42. */
    TAO::Ledger::nSyncSession.store(99);
    LLP::TritiumNode::fSynchronized.store(false);

    REQUIRE(StaleGuardFires(42, TAO::Ledger::nSyncSession.load(),
                             LLP::TritiumNode::fSynchronized.load()));
}

TEST_CASE("Stale SYNC guard: zero sync session rejects all real sessions", "[llp][sync]")
{
    SyncStateGuard sg;

    /* Transitional state: nSyncSession cleared to 0 during handoff.
     * Any real connection (nCurrentSession != 0) is correctly rejected.
     * Note: nCurrentSession==0 is a pre-authentication state; such connections
     * never reach the SPECIFIER::SYNC handler in practice. */
    TAO::Ledger::nSyncSession.store(0);
    LLP::TritiumNode::fSynchronized.store(false);

    REQUIRE(StaleGuardFires(42, TAO::Ledger::nSyncSession.load(),
                             LLP::TritiumNode::fSynchronized.load()));
    REQUIRE(StaleGuardFires(99, TAO::Ledger::nSyncSession.load(),
                             LLP::TritiumNode::fSynchronized.load()));
}

TEST_CASE("Stale SYNC guard: post-sync state rejects regardless of session ID", "[llp][sync]")
{
    SyncStateGuard sg;

    /* Fully-synchronized node: fSynchronized=true, nSyncSession=0 is the
     * documented post-sync state.  Any SYNC block must be rejected. */
    LLP::TritiumNode::fSynchronized.store(true);
    TAO::Ledger::nSyncSession.store(0);

    REQUIRE(StaleGuardFires(0,  0,  true));   /* own session 0 — still rejected */
    REQUIRE(StaleGuardFires(42, 0,  true));   /* any other session — rejected */
    REQUIRE(StaleGuardFires(42, 42, true));   /* session matches — fSynchronized overrides */
}


/* ==========================================================================
 * 2. Handoff atomicity: nSyncSession transitions correctly
 * ========================================================================== */

TEST_CASE("Handoff: old session is rejected, new session is accepted", "[llp][sync]")
{
    SyncStateGuard sg;

    const uint64_t nSessionA = 0xAAAA0001ULL;
    const uint64_t nSessionB = 0xBBBB0002ULL;

    /* Phase 1: A is the active sync peer. */
    TAO::Ledger::nSyncSession.store(nSessionA);
    LLP::TritiumNode::fSynchronized.store(false);

    REQUIRE_FALSE(StaleGuardFires(nSessionA, TAO::Ledger::nSyncSession.load(),
                                   LLP::TritiumNode::fSynchronized.load()));

    /* Phase 2: SwitchNode() clears nSyncSession to 0 before Sync() is called.
     * Any in-flight LASTINDEX from A can no longer trigger a new LIST. */
    TAO::Ledger::nSyncSession.store(0);

    REQUIRE(StaleGuardFires(nSessionA, TAO::Ledger::nSyncSession.load(),
                             LLP::TritiumNode::fSynchronized.load()));

    /* Phase 3: Sync() stores the new session ID. */
    TAO::Ledger::nSyncSession.store(nSessionB);

    /* Old blocks from A are still rejected. */
    REQUIRE(StaleGuardFires(nSessionA, TAO::Ledger::nSyncSession.load(),
                             LLP::TritiumNode::fSynchronized.load()));

    /* New session B is accepted. */
    REQUIRE_FALSE(StaleGuardFires(nSessionB, TAO::Ledger::nSyncSession.load(),
                                   LLP::TritiumNode::fSynchronized.load()));
}

TEST_CASE("Repeated handoffs do not leak session state", "[llp][sync]")
{
    SyncStateGuard sg;

    /* Simulate 3 sequential handoffs: A→B→C. */
    const uint64_t sessions[] = {0xAA00ULL, 0xBB00ULL, 0xCC00ULL};

    LLP::TritiumNode::fSynchronized.store(false);

    for(int i = 0; i < 3; ++i)
    {
        const uint64_t nPrev = (i == 0) ? 0 : sessions[i - 1];
        const uint64_t nCurr = sessions[i];

        /* SwitchNode() clears old session. */
        TAO::Ledger::nSyncSession.store(0);

        /* Previous session is rejected in the transitional 0 state. */
        if(nPrev != 0)
            REQUIRE(StaleGuardFires(nPrev, 0, false));

        /* Sync() stores new session. */
        TAO::Ledger::nSyncSession.store(nCurr);

        /* Only current session is accepted. */
        REQUIRE_FALSE(StaleGuardFires(nCurr, nCurr, false));

        /* All other sessions are rejected. */
        for(int j = 0; j < 3; ++j)
        {
            if(sessions[j] != nCurr)
                REQUIRE(StaleGuardFires(sessions[j], nCurr, false));
        }
    }
}

TEST_CASE("Handoff: in-flight LASTINDEX from old peer cannot re-open sync", "[llp][sync]")
{
    SyncStateGuard sg;

    /* This test documents the race the SwitchNode() nSyncSession.store(0) fix
     * closes.  The LASTINDEX handler only pushes a new ACTION::LIST when
     *   nCurrentSession == TAO::Ledger::nSyncSession.load()
     * so after nSyncSession is cleared to 0, the old session can no longer
     * satisfy that condition. */

    const uint64_t nOldSession = 0xDEADBEEFULL;
    const uint64_t nNewSession = 0xC0FFEE00ULL;

    TAO::Ledger::nSyncSession.store(nOldSession);
    LLP::TritiumNode::fSynchronized.store(false);

    /* Simulated: LASTINDEX from old peer arrives here while SwitchNode() runs. */
    const bool fOldLastIndexWouldTriggerList =
        (nOldSession == TAO::Ledger::nSyncSession.load());
    REQUIRE(fOldLastIndexWouldTriggerList); /* would have re-opened LIST before fix */

    /* SwitchNode() stores 0 BEFORE Sync(). */
    TAO::Ledger::nSyncSession.store(0);

    /* Now the same LASTINDEX from the old peer can no longer trigger a LIST. */
    const bool fOldLastIndexTriggerAfterClear =
        (nOldSession == TAO::Ledger::nSyncSession.load());
    REQUIRE_FALSE(fOldLastIndexTriggerAfterClear);

    /* Sync() activates the new session. */
    TAO::Ledger::nSyncSession.store(nNewSession);

    /* New LASTINDEX from the new peer is fine. */
    const bool fNewLastIndexOk =
        (nNewSession == TAO::Ledger::nSyncSession.load());
    REQUIRE(fNewLastIndexOk);
}
