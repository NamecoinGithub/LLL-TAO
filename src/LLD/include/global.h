/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2026

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLD_INCLUDE_GLOBAL_H
#define NEXUS_LLD_INCLUDE_GLOBAL_H

#include <LLD/types/register.h>
#include <LLD/types/ledger.h>
#include <LLD/types/local.h>
#include <LLD/types/logical.h>
#include <LLD/types/client.h>
#include <LLD/types/legacy.h>
#include <LLD/types/session.h>
#include <LLD/types/trust.h>
#include <LLD/types/contract.h>

#ifdef UNIT_TESTS
#include <functional>
#endif

namespace LLD
{
    extern LogicalDB*    Logical;
    extern SessionDB*    Sessions;
    extern ContractDB*   Contract;
    extern RegisterDB*   Register;
    extern LedgerDB*     Ledger;
    extern LocalDB*      Local;
    extern ClientDB*     Client;

    //for legacy objects
    extern TrustDB*      Trust;
    extern LegacyDB*     Legacy;

    /** Use this to track database instances to use with our ACID transactions. **/
    struct INSTANCES
    {
        enum : uint16_t
        {
            LOGICAL  = (1 << 1),
            CONTRACT = (1 << 2),
            REGISTER = (1 << 3),
            LEDGER   = (1 << 4),
            LOCAL    = (1 << 5),
            CLIENT   = (1 << 6),
            TRUST    = (1 << 7),
            LEGACY   = (1 << 8),

            //combined values
            CONSENSUS = (CONTRACT | REGISTER | LEDGER | TRUST | LEGACY),
            MERKLE    = (CONTRACT | REGISTER | CLIENT | LOGICAL),
            MEMORY    = (CONTRACT | REGISTER | LEDGER),
        };
    };

    /** Outcome of the most recent transaction owned by the calling thread. */
    enum class TXN_OUTCOME
    {
        NONE,
        ABORTED,
        COMMITTED,
        RECOVERED,
        RECOVERY_REQUIRED
    };


    /** Initialize
     *
     *  Initialize the global LLD instances.
     *
     *  @return True if initialization and journal recovery succeeded.
     *
     **/
    bool Initialize();


    /** Indexing
     *
     *  Run our indexing entries and routines.
     *
     **/
    void Indexing();


    /** Shutdown
     *
     *  Shutdown and cleanup the global LLD instances.
     *
     **/
    void Shutdown();


    /** TxnRecover
     *
     *  Check the transactions for recovery.
     *
     *  @return True if no recovery was needed or every recovered database
     *          committed successfully.
     *
     **/
    bool TxnRecovery();


    /** HasOpenTransaction
     *
     *  Check if a physical database transaction is currently open on any
     *  instance that would be touched by TxnBegin(nFlags, nInstances).
     *  Returns false for memory-only flag modes (MEMPOOL, MINER, SANITIZE).
     *
     */
    bool HasOpenTransaction(const uint8_t nFlags = 0, const uint16_t nInstances = INSTANCES::CONSENSUS);


    /** Txn Begin
     *
     *  Global handler for all LLD instances.
     *
     */
    bool TxnBegin(const uint8_t nFlags = 0, const uint16_t nInstances = INSTANCES::CONSENSUS);


    /** Txn Abort
     *
     *  Global handler for all LLD instances.
     *
     */
    bool TxnAbort(const uint8_t nFlags = 0, const uint16_t nInstances = INSTANCES::CONSENSUS);


    /** Committed blocks whose sector and keychain fsync may be deferred.
     *  Passing 1 restores a data barrier on every commit. This is a function
     *  argument, not a nexus.conf option. */
    static constexpr uint32_t SYNC_COMMIT_BLOCKS = 32;

    /** Parked journal bytes that force a data barrier. Not a configuration option. */
    static constexpr uint64_t SYNC_COMMIT_BYTES = 8ull * 1024ull * 1024ull;

    /* Unit tests keep the historical per-commit barrier unless they opt in.
     * The node binary defaults to SYNC_COMMIT_BLOCKS. */
    #ifdef UNIT_TESTS
    static constexpr uint32_t SYNC_COMMIT_BLOCKS_DEFAULT = 1;
    #else
    static constexpr uint32_t SYNC_COMMIT_BLOCKS_DEFAULT = SYNC_COMMIT_BLOCKS;
    #endif


    /** Txn Commit
     *
     *  Global handler for all LLD instances.
     *
     *  @param[in] nSyncCommitBlocks  Data/keychain fsyncs are coalesced until this
     *             many commits in the same CONSENSUS or MERKLE recovery group,
     *             SYNC_COMMIT_BYTES of that group's parked journals, or shutdown.
     *             The other group's commits are not counted toward this threshold.
     *             A barrier is newer than every parked journal, so it syncs both
     *             groups and discards every parked sequence in global order.
     *             Values below 2 sync on this commit. Default is
     *             SYNC_COMMIT_BLOCKS_DEFAULT.
     *
     *  @return True if every selected checkpoint and per-DB commit succeeded.
     *
     */
    bool TxnCommit(const uint8_t nFlags = 0,
                   const uint16_t nInstances = INSTANCES::CONSENSUS,
                   const uint32_t nSyncCommitBlocks = SYNC_COMMIT_BLOCKS_DEFAULT);

    /** Report whether the most recent owned transaction committed or requires recovery. */
    TXN_OUTCOME LastTxnOutcome();


    /** Shutdown may delete parked journals only after recovery succeeded and no
     *  later failure reported RECOVERY_REQUIRED. Replay and data-sync failures
     *  clear this so the journals remain for the next startup. */
    bool MayDiscardPendingJournals();


    /** True when a per-database discard may delete this parked sequence.
     *
     *  Sequences owned by a CONSENSUS or MERKLE batch, and any untracked
     *  sequence on the shared Contract or Register databases, stay until the
     *  coordinator has quiesced that sequence's entire recovery group.
     */
    bool MayDiscardPendingSequence(const void* pDatabase, uint64_t nSequence);


    /** Abort a transaction automatically if its scope exits before it is consumed. */
    class TransactionGuard
    {
    public:
        TransactionGuard(const uint8_t nFlags = 0, const uint16_t nInstances = INSTANCES::CONSENSUS);
        ~TransactionGuard();

        TransactionGuard(const TransactionGuard&) = delete;
        TransactionGuard& operator=(const TransactionGuard&) = delete;

        explicit operator bool() const;

    private:
        const uint8_t nFlags;
        const uint16_t nInstances;
        const bool fAcquired;
    };


    #ifdef UNIT_TESTS
    /** Install a test hook invoked after the coordinator is observed locked. */
    void SetTxnCoordinatorWaitHook(const std::function<void()>& fnHook);

    /** Clear the recovery-required latch so unit tests can continue after a
     *  forced partial-apply failure. */
    void ResetTxnRecoveryRequired();


    /** Quiesce both recovery groups and discard only sequences whose group synced.
     *  Does not destroy database instances. */
    bool TxnShutdownGroupBarrier();


    /** Fail the next nFail parked-journal removals after nAllow successes.
     *  Used to simulate a crash between participant deletions. */
    void SetJournalRemovalFault(uint32_t nAllow, uint32_t nFail);
    #endif
}

#endif
