/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2026

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLD/include/global.h>

#include <TAO/Ledger/include/enum.h> //for internal flags

#include <Util/include/args.h>
#include <Util/include/signals.h>

#include <algorithm>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace LLD
{
    /* The LLD global instance pointers. */
    LogicalDB*    Logical;
    SessionDB*    Sessions;
    ContractDB*   Contract;
    RegisterDB*   Register;
    LedgerDB*     Ledger;
    LocalDB*      Local;
    ClientDB*     Client;

    /* For Legacy LLD objects. */
    TrustDB*      Trust;
    LegacyDB*     Legacy;

    class DurableCommitCoordinator
    {
    public:
        struct Context
        {
            bool fOwner{false};
            bool fMemoryOnly{false};
            uint8_t nFlags{0};
            uint16_t nParticipants{0};
            uint64_t nIdentity{0};
            bool fDurableDecision{false};
            TXN_OUTCOME nOutcome{TXN_OUTCOME::NONE};
        };

        bool Recover();
        bool HasOpenTransaction(uint8_t nFlags, uint16_t nInstances);
        bool Begin(uint8_t nFlags, uint16_t nInstances);
        bool Abort(uint8_t nFlags, uint16_t nInstances);
        bool Commit(uint8_t nFlags, uint16_t nInstances, uint32_t nSyncCommitBlocks);
        TXN_OUTCOME LastOutcome() const;
        void ResetRecoveryRequired();
        void RetainPendingJournals();
        bool MayDiscardPendingJournals() const;

    private:
        using PhysicalDB = SectorDatabase<BinaryHashMap, BinaryLRU>;

        void ReleaseMemoryTransactions(uint8_t nFlags, uint16_t nInstances);
        bool ReleasePhysicalTransactions(uint16_t nInstances);
        void ReleaseOwnership();
        bool ParkGroup(uint16_t nInstances, uint64_t nSequence);
        bool DiscardGroup(uint16_t nInstances);
        bool DiscardSequences(uint16_t nInstances, const std::vector<uint64_t>& vSequences);
        bool CollectPendingSequences(PhysicalDB* pDatabase, std::set<uint64_t>& setSequences);
        bool RecoverPendingSequences(uint16_t nInstances,
                                     const std::vector<PhysicalDB*>& vParticipants,
                                     const std::set<uint64_t>& setSequences);
        bool RecoverGroup(uint16_t nInstances, const std::vector<PhysicalDB*>& vParticipants);
        void FailRecovery(const std::string& strError);
        void RequireRecovery();
        void AllowPendingJournalDiscard();

        std::mutex cMutex;
        std::atomic<bool> fRecoveryRequired{false};
        std::atomic<bool> fDiscardPendingJournals{false};
        /* CONSENSUS and MERKLE share Contract and Register, but each group has
         * its own parked journals. A shared counter lets one group's flush reset
         * the barrier while the other group's journals and dirty data remain. */
        struct DeferredBatch
        {
            uint32_t nCommits{0};
            uint64_t nJournalBytes{0};
            std::vector<uint64_t> vSequences;
        };

        DeferredBatch& DeferredFor(uint16_t nInstances);

        uint64_t nNextIdentity{1};
        DeferredBatch cDeferredConsensus;
        DeferredBatch cDeferredMerkle;
        uint64_t nNextParkSequence{1};
        static thread_local Context cContext;
    };

    thread_local DurableCommitCoordinator::Context DurableCommitCoordinator::cContext;
    static DurableCommitCoordinator cTxnCoordinator;

    #ifdef UNIT_TESTS
    static std::function<void()> fnTxnCoordinatorWaitHook;
    #endif


    void DurableCommitCoordinator::ReleaseMemoryTransactions(
        const uint8_t nFlags, const uint16_t nInstances)
    {
        if(Contract && (nInstances & INSTANCES::CONTRACT))
            Contract->MemoryRelease(nFlags);

        if(Register && (nInstances & INSTANCES::REGISTER))
            Register->MemoryRelease(nFlags);

        if(Ledger && (nInstances & INSTANCES::LEDGER))
            Ledger->MemoryRelease(nFlags);
    }


    bool DurableCommitCoordinator::ReleasePhysicalTransactions(const uint16_t nInstances)
    {
        bool fReleased = true;

        if(Logical && (nInstances & INSTANCES::LOGICAL))
            fReleased = Logical->TxnRelease() && fReleased;

        if(Contract && (nInstances & INSTANCES::CONTRACT))
            fReleased = Contract->TxnRelease() && fReleased;

        if(Register && (nInstances & INSTANCES::REGISTER))
            fReleased = Register->TxnRelease() && fReleased;

        if(Ledger && (nInstances & INSTANCES::LEDGER))
            fReleased = Ledger->TxnRelease() && fReleased;

        if(Client && (nInstances & INSTANCES::CLIENT))
            fReleased = Client->TxnRelease() && fReleased;

        if(Trust && (nInstances & INSTANCES::TRUST))
            fReleased = Trust->TxnRelease() && fReleased;

        if(Legacy && (nInstances & INSTANCES::LEGACY))
            fReleased = Legacy->TxnRelease() && fReleased;

        return fReleased;
    }


    void DurableCommitCoordinator::ReleaseOwnership()
    {
        if(!cContext.fOwner)
            return;

        cContext.fOwner = false;
        cContext.fMemoryOnly = false;
        cContext.nFlags = 0;
        cContext.nParticipants = 0;
        cContext.nIdentity = 0;
        cContext.fDurableDecision = false;
        cMutex.unlock();
    }


    bool DurableCommitCoordinator::ParkGroup(const uint16_t nInstances, const uint64_t nSequence)
    {
        bool fParked = true;

        if(Logical && (nInstances & INSTANCES::LOGICAL))
            fParked = Logical->TxnParkJournal(nSequence) && fParked;
        if(Contract && (nInstances & INSTANCES::CONTRACT))
            fParked = Contract->TxnParkJournal(nSequence) && fParked;
        if(Register && (nInstances & INSTANCES::REGISTER))
            fParked = Register->TxnParkJournal(nSequence) && fParked;
        if(Ledger && (nInstances & INSTANCES::LEDGER))
            fParked = Ledger->TxnParkJournal(nSequence) && fParked;
        if(Client && (nInstances & INSTANCES::CLIENT))
            fParked = Client->TxnParkJournal(nSequence) && fParked;
        if(Trust && (nInstances & INSTANCES::TRUST))
            fParked = Trust->TxnParkJournal(nSequence) && fParked;
        if(Legacy && (nInstances & INSTANCES::LEGACY))
            fParked = Legacy->TxnParkJournal(nSequence) && fParked;

        return fParked;
    }


    bool DurableCommitCoordinator::DiscardGroup(const uint16_t nInstances)
    {
        bool fDiscarded = true;

        if(Logical && (nInstances & INSTANCES::LOGICAL))
            fDiscarded = Logical->TxnDiscardPendingJournals(true) && fDiscarded;
        if(Contract && (nInstances & INSTANCES::CONTRACT))
            fDiscarded = Contract->TxnDiscardPendingJournals(true) && fDiscarded;
        if(Register && (nInstances & INSTANCES::REGISTER))
            fDiscarded = Register->TxnDiscardPendingJournals(true) && fDiscarded;
        if(Ledger && (nInstances & INSTANCES::LEDGER))
            fDiscarded = Ledger->TxnDiscardPendingJournals(true) && fDiscarded;
        if(Client && (nInstances & INSTANCES::CLIENT))
            fDiscarded = Client->TxnDiscardPendingJournals(true) && fDiscarded;
        if(Trust && (nInstances & INSTANCES::TRUST))
            fDiscarded = Trust->TxnDiscardPendingJournals(true) && fDiscarded;
        if(Legacy && (nInstances & INSTANCES::LEGACY))
            fDiscarded = Legacy->TxnDiscardPendingJournals(true) && fDiscarded;

        return fDiscarded;
    }


    bool DurableCommitCoordinator::DiscardSequences(const uint16_t nInstances,
                                                    const std::vector<uint64_t>& vSequences)
    {
        if(vSequences.empty())
            return true;

        bool fDiscarded = true;

        if(Logical && (nInstances & INSTANCES::LOGICAL))
            fDiscarded = Logical->TxnDiscardPendingSequences(vSequences) && fDiscarded;
        if(Contract && (nInstances & INSTANCES::CONTRACT))
            fDiscarded = Contract->TxnDiscardPendingSequences(vSequences) && fDiscarded;
        if(Register && (nInstances & INSTANCES::REGISTER))
            fDiscarded = Register->TxnDiscardPendingSequences(vSequences) && fDiscarded;
        if(Ledger && (nInstances & INSTANCES::LEDGER))
            fDiscarded = Ledger->TxnDiscardPendingSequences(vSequences) && fDiscarded;
        if(Client && (nInstances & INSTANCES::CLIENT))
            fDiscarded = Client->TxnDiscardPendingSequences(vSequences) && fDiscarded;
        if(Trust && (nInstances & INSTANCES::TRUST))
            fDiscarded = Trust->TxnDiscardPendingSequences(vSequences) && fDiscarded;
        if(Legacy && (nInstances & INSTANCES::LEGACY))
            fDiscarded = Legacy->TxnDiscardPendingSequences(vSequences) && fDiscarded;

        return fDiscarded;
    }


    DurableCommitCoordinator::DeferredBatch&
    DurableCommitCoordinator::DeferredFor(const uint16_t nInstances)
    {
        /* Match Begin(): CLIENT or LOGICAL selects MERKLE even when the mask
         * also names shared databases. CONSENSUS-only bits select the other
         * batch. Shared-only masks follow the client/full-node recovery group. */
        if(nInstances & (INSTANCES::CLIENT | INSTANCES::LOGICAL))
            return cDeferredMerkle;
        if(nInstances & (INSTANCES::LEDGER | INSTANCES::TRUST | INSTANCES::LEGACY))
            return cDeferredConsensus;

        return config::fClient.load() ? cDeferredMerkle : cDeferredConsensus;
    }


    void DurableCommitCoordinator::RequireRecovery()
    {
        fRecoveryRequired.store(true);
        fDiscardPendingJournals.store(false);
    }


    void DurableCommitCoordinator::RetainPendingJournals()
    {
        fDiscardPendingJournals.store(false);
    }


    void DurableCommitCoordinator::AllowPendingJournalDiscard()
    {
        fDiscardPendingJournals.store(true);
    }


    bool DurableCommitCoordinator::MayDiscardPendingJournals() const
    {
        return fDiscardPendingJournals.load();
    }


    void DurableCommitCoordinator::FailRecovery(const std::string& strError)
    {
        RequireRecovery();
        cContext.nOutcome = TXN_OUTCOME::RECOVERY_REQUIRED;
        debug::error(FUNCTION, strError);
    }


    bool DurableCommitCoordinator::CollectPendingSequences(PhysicalDB* pDatabase,
                                                            std::set<uint64_t>& setSequences)
    {
        if(!pDatabase)
            return true;

        std::vector<uint64_t> vPending;
        if(!pDatabase->TxnPendingSequences(vPending))
            return false;

        setSequences.insert(vPending.begin(), vPending.end());
        return true;
    }


    /* Replay one recovery group's parked sequences, including journals those
     * sequences left on databases shared with the other group. Discard only
     * those sequences so the other group's crash records stay replayable. */
    bool DurableCommitCoordinator::RecoverPendingSequences(
        const uint16_t nInstances,
        const std::vector<PhysicalDB*>& vParticipants,
        const std::set<uint64_t>& setSequences)
    {
        if(setSequences.empty())
            return true;

        for(const uint64_t nSequence : setSequences)
        {
            for(PhysicalDB* pDatabase : vParticipants)
            {
                if(pDatabase && !pDatabase->TxnReplayPending(nSequence))
                {
                    FailRecovery("failed to replay parked transaction journal");
                    return false;
                }
            }
        }

        for(PhysicalDB* pDatabase : vParticipants)
        {
            if(pDatabase && !pDatabase->TxnSyncDeferred())
            {
                FailRecovery("failed to sync replayed transaction data; parked journals retained");
                return false;
            }
        }

        const std::vector<uint64_t> vSequences(setSequences.begin(), setSequences.end());
        if(!DiscardSequences(nInstances, vSequences))
        {
            FailRecovery("failed to discard parked journals after data sync");
            return false;
        }

        return true;
    }


    bool DurableCommitCoordinator::RecoverGroup(const uint16_t nInstances,
                                                const std::vector<PhysicalDB*>& vParticipants)
    {
        std::set<uint64_t> setSequences;
        for(PhysicalDB* pDatabase : vParticipants)
        {
            if(!pDatabase)
                continue;

            std::vector<uint64_t> vPending;
            if(!pDatabase->TxnPendingSequences(vPending))
            {
                FailRecovery("failed to list parked transaction journals");
                return false;
            }

            for(const uint64_t nSequence : vPending)
                setSequences.insert(nSequence);
        }

        /* Older parked commits replay before the current journal.dat group.
         * Replay is idempotent, so a crash after data sync but before discard
         * rolls forward again instead of losing the decision. */
        for(const uint64_t nSequence : setSequences)
        {
            for(PhysicalDB* pDatabase : vParticipants)
            {
                if(pDatabase && !pDatabase->TxnReplayPending(nSequence))
                {
                    FailRecovery("failed to replay parked transaction journal");
                    return false;
                }
            }
        }

        const uint64_t nLatestSequence = setSequences.empty() ? 0 : *setSequences.rbegin();
        bool fAllSatisfied = true;
        std::vector<PhysicalDB*> vComplete;
        vComplete.reserve(vParticipants.size());

        for(PhysicalDB* pDatabase : vParticipants)
        {
            if(!pDatabase)
                continue;

            const uint64_t nJournalBytes = pDatabase->TxnJournalBytes();
            const RECOVERY nRecovery = pDatabase->TxnRecovery();
            if(nRecovery == RECOVERY::FAILED)
            {
                FailRecovery("failed to recover transaction journal");
                return false;
            }

            if(nRecovery == RECOVERY::COMPLETE)
                vComplete.push_back(pDatabase);

            std::vector<uint64_t> vPending;
            if(!pDatabase->TxnPendingSequences(vPending))
            {
                FailRecovery("failed to list parked transaction journals");
                return false;
            }

            const bool fHasLatestPending = nLatestSequence > 0
                && std::find(vPending.begin(), vPending.end(), nLatestSequence) != vPending.end();

            /* A missing journal.dat is a completed parked commit only when this
             * participant has the group's latest pending sequence. A non-empty
             * journal without a commit marker is still an incomplete checkpoint. */
            const bool fSatisfied = nRecovery == RECOVERY::COMPLETE
                || (nRecovery == RECOVERY::INCOMPLETE && nJournalBytes == 0 && fHasLatestPending);
            if(!fSatisfied)
                fAllSatisfied = false;
        }

        if(fAllSatisfied && !vComplete.empty())
        {
            debug::log(0, FUNCTION, "all transactions are complete, recovering...");

            for(PhysicalDB* pDatabase : vComplete)
            {
                if(!pDatabase->TxnCommit())
                {
                    FailRecovery("transaction recovery commit failed; journals retained for restart");
                    return false;
                }
            }
        }

        if(!setSequences.empty())
        {
            for(PhysicalDB* pDatabase : vParticipants)
            {
                if(pDatabase && !pDatabase->TxnSyncDeferred())
                {
                    FailRecovery("failed to sync replayed transaction data; parked journals retained");
                    return false;
                }
            }

            if(!DiscardGroup(nInstances))
            {
                FailRecovery("failed to discard parked journals after data sync");
                return false;
            }
        }

        /* Absent journals are the normal startup path. Truncate only when a
         * commit was applied, a parked journal was discarded, or an incomplete
         * journal.dat still has bytes. */
        bool fNeedsRelease = !vComplete.empty() || !setSequences.empty();
        if(!fAllSatisfied)
        {
            for(PhysicalDB* pDatabase : vParticipants)
            {
                if(pDatabase && pDatabase->TxnJournalBytes() > 0)
                    fNeedsRelease = true;
            }
        }

        if(fNeedsRelease)
        {
            if(!ReleasePhysicalTransactions(nInstances))
            {
                FailRecovery("failed to durably release transaction journals");
                return false;
            }
        }

        return true;
    }


    /*  Initialize the global LLD instances. */
    bool Initialize()
    {
        debug::log(0, FUNCTION, "Initializing LLD");

        /* Construction can fail before recovery runs. Keep any parked journals
         * until Recover() reports success. */
        cTxnCoordinator.RetainPendingJournals();

        try
        {
        /* Create the contract database instance. */
        const uint32_t nContractCacheSize = config::GetArg("-contractcache", 1);
        Contract = new ContractDB(
                        FLAGS::CREATE | FLAGS::FORCE,
                        77773,
                        nContractCacheSize * 1024 * 1024);

        /* Create the contract database instance. */
        const uint32_t nRegisterCacheSize = config::GetArg("-registercache", 2);
        Register = new RegisterDB(
                        FLAGS::CREATE | FLAGS::FORCE,
                        77773,
                        nRegisterCacheSize * 1024 * 1024);

        /* Create the ledger database instance.
         * Default cache raised to 64 MB for mining node workloads.
         * Blocks average 216 bytes, so 64 MB holds ~300K typical blocks in
         * BinaryLRU cache, dramatically reducing SECTOR_MUTEX contention
         * between P2P block-serving and mining template creation. */
        const uint32_t nLedgerCacheSize = config::GetArg("-ledgercache", 64);
        Ledger    = new LedgerDB(
                        FLAGS::CREATE | FLAGS::FORCE,
                        config::fClient.load() ? 77773 : (256 * 256 * 64),
                        nLedgerCacheSize * 1024 * 1024);


        /* Create the legacy database instance. */
        const uint32_t nLegacyCacheSize = config::GetArg("-legacycache", 1);
        Legacy = new LegacyDB(
                        FLAGS::CREATE | FLAGS::FORCE,
                        config::fClient.load() ? 77773 : 256 * 256 * 64,
                        nLegacyCacheSize * 1024 * 1024);


        /* Create the trust database instance. */
        Trust  = new TrustDB(
                        FLAGS::CREATE | FLAGS::FORCE);


        /* Create the local database instance. */
        Local    = new LocalDB(
                        FLAGS::CREATE | FLAGS::FORCE);


        /* Create the local database instance. */
        const uint32_t nLogicalCacheSize = config::GetArg("-logicalcache", 2);
        Logical    = new LogicalDB(
                        FLAGS::CREATE | FLAGS::FORCE,
                        256 * 256 * config::GetArg("-logicalbuckets", 16), nLogicalCacheSize * 1024 * 1024);

        /* Create the local database instance. */
        const uint32_t nSessionsCacheSize = config::GetArg("-sessionscache", 2);
        Sessions    = new SessionDB(
                        FLAGS::CREATE | FLAGS::FORCE,
                        256 * 256 * config::GetArg("-sessionsbuckets", 16), nSessionsCacheSize * 1024 * 1024);

        if(config::fClient.load())
        {
            /* Create new client database if enabled. */
            Client    = new ClientDB(
                            FLAGS::CREATE | FLAGS::FORCE,
                            1000000);
        }

        /* Handle database recovery mode. */
        if(!TxnRecovery())
            return false;

        return true;
        }
        catch(const std::exception& e)
        {
            debug::error(FUNCTION, "failed to initialize LLD: ", e.what());
            Shutdown();
            return false;
        }
    }


    /* Run our indexing entries and routines. */
    void Indexing() //TODO: combine all of these into one single indexing routine (include -indexheight)
    {
        debug::log(0, FUNCTION, "Indexing LLD");


        /* Check for reindexing entries. */
        Logical->IndexRegisters();


        /* Check for reindexing entries. */
        Register->IndexAddress();


        /* Check for reindexing entries. */
        Ledger->IndexProofs();
    }


    /*  Shutdown and cleanup the global LLD instances. */
    void Shutdown()
    {
        debug::log(0, FUNCTION, "Shutting down LLD");

        /* Cleanup the contract database. */
        delete Contract;
        Contract = nullptr;

        /* Cleanup the ledger database. */
        delete Ledger;
        Ledger = nullptr;

        /* Cleanup the register database. */
        delete Register;
        Register = nullptr;

        /* Cleanup the local database. */
        delete Local;
        Local = nullptr;

        /* Cleanup the client database. */
        delete Client;
        Client = nullptr;

        /* Cleanup the legacy database. */
        delete Legacy;
        Legacy = nullptr;

        /* Cleanup the trust database. */
        delete Trust;
        Trust = nullptr;

        /* Cleanup the logical database. */
        delete Logical;
        Logical = nullptr;

        /* Cleanup the sessions database. */
        delete Sessions;
        Sessions = nullptr;
    }


    /* Check the transactions for recovery. */
    bool DurableCommitCoordinator::Recover()
    {
        std::vector<PhysicalDB*> vParticipants;
        uint16_t nInstances = INSTANCES::CONSENSUS;

        if(config::fClient.load())
        {
            nInstances = INSTANCES::MERKLE;
            vParticipants = {Contract, Register, Logical, Client};
        }
        else
        {
            /* Lookup paths open MERKLE transactions on a full node. Selecting
             * the group solely from fClient leaves _API/journal.<seq>.pending
             * unreplayed and its dirty data unsynced. Replay those sequences,
             * including the shared Contract/Register copies, before CONSENSUS
             * recovery discards every parked journal on those databases. */
            std::set<uint64_t> setMerkleSequences;
            if(!CollectPendingSequences(Logical, setMerkleSequences)
            || !CollectPendingSequences(Client, setMerkleSequences))
            {
                FailRecovery("failed to list parked transaction journals");
                return false;
            }

            if(!RecoverPendingSequences(INSTANCES::MERKLE,
                                        {Logical, Contract, Register, Client},
                                        setMerkleSequences))
                return false;

            DeferredFor(INSTANCES::MERKLE) = {};
            vParticipants = {Contract, Register, Trust, Legacy, Ledger};
        }

        if(!RecoverGroup(nInstances, vParticipants))
            return false;

        /* Recovery synced and discarded this group's parked journals. Drop the
         * in-memory batch so the next commit does not inherit a stale barrier. */
        DeferredFor(nInstances) = {};

        cContext.nOutcome = TXN_OUTCOME::RECOVERED;
        fRecoveryRequired.store(false);
        AllowPendingJournalDiscard();
        return true;
    }


    /* Global handler for all LLD instances. */
    bool DurableCommitCoordinator::HasOpenTransaction(
        const uint8_t nFlags, const uint16_t nInstances)
    {
        /* Memory-only flag modes do not use physical SectorDatabase transactions. */
        if(nFlags == TAO::Ledger::FLAGS::MEMPOOL || nFlags == TAO::Ledger::FLAGS::MINER || nFlags == TAO::Ledger::FLAGS::SANITIZE)
            return false;

        /* An open transaction on another thread is not owned by this caller.
         * TxnBegin() will wait for it rather than joining or replacing it. */
        if(!cContext.fOwner)
            return false;

        /* Check each database instance that would be opened by TxnBegin(nFlags, nInstances).
         * Any single instance having pTransaction != nullptr means a transaction is open. */
        if(Logical  && (nInstances & INSTANCES::LOGICAL)  && Logical->HasTransaction())
            return true;
        if(Ledger   && (nInstances & INSTANCES::LEDGER)   && Ledger->HasTransaction())
            return true;
        if(Client   && (nInstances & INSTANCES::CLIENT)   && Client->HasTransaction())
            return true;
        if(Contract && (nInstances & INSTANCES::CONTRACT) && Contract->HasTransaction())
            return true;
        if(Register && (nInstances & INSTANCES::REGISTER) && Register->HasTransaction())
            return true;
        if(Trust    && (nInstances & INSTANCES::TRUST)    && Trust->HasTransaction())
            return true;
        if(Legacy   && (nInstances & INSTANCES::LEGACY)   && Legacy->HasTransaction())
            return true;

        return false;
    }


    /* Global handler for all LLD instances. */
    bool DurableCommitCoordinator::Begin(const uint8_t nFlags, const uint16_t nInstances)
    {
        const bool fMemoryOnly =
            (nFlags == TAO::Ledger::FLAGS::MEMPOOL || nFlags == TAO::Ledger::FLAGS::MINER || nFlags == TAO::Ledger::FLAGS::SANITIZE);
        uint16_t nOwnedInstances = nInstances;
        if(!fMemoryOnly)
        {
            if(nInstances & (INSTANCES::CLIENT | INSTANCES::LOGICAL))
                nOwnedInstances = INSTANCES::MERKLE;
            else if(nInstances & (INSTANCES::LEDGER | INSTANCES::TRUST | INSTANCES::LEGACY))
                nOwnedInstances = INSTANCES::CONSENSUS;
            else
                nOwnedInstances = config::fClient.load() ? INSTANCES::MERKLE : INSTANCES::CONSENSUS;
        }

        {
            /* Existing callers intentionally flatten ownership through SetBest(),
             * which checks HasOpenTransaction() before beginning. Refuse any other
             * nested begin rather than deleting the active transaction. */
            if(cContext.fOwner)
            {
                debug::error(FUNCTION, "nested transaction begin refused");
                return false;
            }

            #ifdef UNIT_TESTS
            if(!cMutex.try_lock())
            {
                if(fnTxnCoordinatorWaitHook)
                    fnTxnCoordinatorWaitHook();

                cMutex.lock();
            }
            #else
            cMutex.lock();
            #endif

            if(fRecoveryRequired.load())
            {
                cMutex.unlock();
                debug::error(FUNCTION, "transaction recovery is required; refusing to begin");
                return false;
            }

            cContext.fOwner = true;
            cContext.fMemoryOnly = fMemoryOnly;
            cContext.nFlags = nFlags;
            cContext.nParticipants = nOwnedInstances;
            cContext.nIdentity = nNextIdentity++;
            cContext.fDurableDecision = false;
            cContext.nOutcome = TXN_OUTCOME::NONE;
        }

        if(fRecoveryRequired.load())
        {
            ReleaseOwnership();
            debug::error(FUNCTION, "transaction recovery is required; refusing to begin");
            return false;
        }

        try
        {
            /* Start the contract DB transaction. */
            if(Contract && (nOwnedInstances & INSTANCES::CONTRACT))
                Contract->MemoryBegin(nFlags);

            /* Start the register DB transacdtion. */
            if(Register && (nOwnedInstances & INSTANCES::REGISTER))
                Register->MemoryBegin(nFlags);

            /* Start the ledger DB transaction. */
            if(Ledger && (nOwnedInstances & INSTANCES::LEDGER))
                Ledger->MemoryBegin(nFlags);

            /* Handle memory commits if in memory m ode. */
            if(nFlags == TAO::Ledger::FLAGS::MEMPOOL || nFlags == TAO::Ledger::FLAGS::MINER || nFlags == TAO::Ledger::FLAGS::SANITIZE)
                return true;

            /* Start the Logical DB transaction. */
            if(Logical && (nOwnedInstances & INSTANCES::LOGICAL))
                Logical->TxnBegin();

            /* Start the contract DB transaction. */
            if(Contract && (nOwnedInstances & INSTANCES::CONTRACT))
                Contract->TxnBegin();

            /* Start the register DB transacdtion. */
            if(Register && (nOwnedInstances & INSTANCES::REGISTER))
                Register->TxnBegin();

            /* Start the ledger DB transaction. */
            if(Ledger && (nOwnedInstances & INSTANCES::LEDGER))
                Ledger->TxnBegin();

            /* Start the client DB transaction. */
            if(Client && (nOwnedInstances & INSTANCES::CLIENT))
                Client->TxnBegin();

            /* Start the trust DB transaction. */
            if(Trust && (nOwnedInstances & INSTANCES::TRUST))
                Trust->TxnBegin();

            /* Start the legacy DB transaction. */
            if(Legacy && (nOwnedInstances & INSTANCES::LEGACY))
                Legacy->TxnBegin();
        }
        catch(const std::exception& e)
        {
            TxnAbort(nFlags, nOwnedInstances);
            return debug::error(FUNCTION, "failed to start transaction participants: ", e.what());
        }
        catch(...)
        {
            TxnAbort(nFlags, nOwnedInstances);
            return debug::error(FUNCTION, "failed to start transaction participants");
        }

        return true;
    }


    /* Global handler for all LLD instances. */
    bool DurableCommitCoordinator::Abort(const uint8_t nFlags, const uint16_t nInstances)
    {
        /* A redundant outer abort after SetBest() consumed the transaction is a
         * safe no-op, and another thread must never release the owner's state. */
        if(!cContext.fOwner)
            return true;

        const bool fMemoryOnly =
            (nFlags == TAO::Ledger::FLAGS::MEMPOOL || nFlags == TAO::Ledger::FLAGS::MINER || nFlags == TAO::Ledger::FLAGS::SANITIZE);
        if(fMemoryOnly != cContext.fMemoryOnly || (fMemoryOnly && nFlags != cContext.nFlags))
        {
            debug::error(FUNCTION, "transaction mode does not match current owner");
            return false;
        }

        const uint16_t nReleaseInstances = (nInstances | cContext.nParticipants);

        if(cContext.fDurableDecision || fRecoveryRequired.load())
        {
            ReleaseMemoryTransactions(nFlags, nReleaseInstances);
            cContext.nOutcome = TXN_OUTCOME::RECOVERY_REQUIRED;
            RequireRecovery();
            ReleaseOwnership();
            return debug::error(FUNCTION,
                "durable transaction decision exists; abort refused and recovery required");
        }

        ReleaseMemoryTransactions(nFlags, nReleaseInstances);

        /* Handle memory commits if in memory mode. */
        if(fMemoryOnly)
        {
            cContext.nOutcome = TXN_OUTCOME::ABORTED;
            ReleaseOwnership();
            return true;
        }

        if(!ReleasePhysicalTransactions(nReleaseInstances))
        {
            RequireRecovery();
            cContext.nOutcome = TXN_OUTCOME::RECOVERY_REQUIRED;
            ReleaseOwnership();
            ::Shutdown();
            return debug::error(FUNCTION,
                "failed to durably release aborted transaction journals; shutdown requested");
        }

        cContext.nOutcome = TXN_OUTCOME::ABORTED;
        ReleaseOwnership();
        return true;
    }


    /* Global handler for all LLD instances. */
    bool DurableCommitCoordinator::Commit(const uint8_t nFlags, const uint16_t nInstances,
                                         const uint32_t nSyncCommitBlocks)
    {
        /* Special check if using MINER or SANITIZE flags — intentional short-circuit,
         * not a failure: callers use these flags to prevent accidental commits. */
        if(nFlags == TAO::Ledger::FLAGS::MINER || nFlags == TAO::Ledger::FLAGS::SANITIZE)
            return Abort(nFlags, nInstances);

        if(!cContext.fOwner)
            return false;

        const bool fMemoryOnly = (nFlags == TAO::Ledger::FLAGS::MEMPOOL);
        if(fMemoryOnly != cContext.fMemoryOnly || (fMemoryOnly && nFlags != cContext.nFlags))
            return debug::error(FUNCTION, "transaction mode does not match current owner");

        const uint16_t nReleaseInstances = (nInstances | cContext.nParticipants);

        /* Memory-pool transactions have no physical journal. */
        if(nFlags == TAO::Ledger::FLAGS::MEMPOOL)
        {
            if(Contract && (nReleaseInstances & INSTANCES::CONTRACT))
                Contract->MemoryCommit();
            if(Register && (nReleaseInstances & INSTANCES::REGISTER))
                Register->MemoryCommit();
            if(Ledger && (nReleaseInstances & INSTANCES::LEDGER))
                Ledger->MemoryCommit();

            cContext.nOutcome = TXN_OUTCOME::COMMITTED;
            ReleaseOwnership();
            return true;
        }

        if(fRecoveryRequired.load())
        {
            ReleaseMemoryTransactions(nFlags, nReleaseInstances);
            cContext.nOutcome = TXN_OUTCOME::RECOVERY_REQUIRED;
            ReleaseOwnership();
            return debug::error(FUNCTION, "transaction recovery is required; refusing to commit");
        }

        /* Every selected journal must reach its commit marker before any participant
         * is applied. A checkpoint failure has no durable global decision and is
         * therefore safe to abort in full. Values below 2, and shutdown, keep a
         * data barrier on this commit. Otherwise only newly created keychain files
         * are synced here so dirty hashmap pages are not written back per block. */
        const bool fDeferDirtySync =
            nSyncCommitBlocks > 1 && !config::fShutdown.load();
        bool fCheckpointsComplete = true;

        /* Set a checkpoint for Logical DB. */
        if(Logical && (nReleaseInstances & INSTANCES::LOGICAL))
            fCheckpointsComplete = Logical->TxnCheckpoint(fDeferDirtySync) && fCheckpointsComplete;

        /* Set a checkpoint for contract DB. */
        if(Contract && (nReleaseInstances & INSTANCES::CONTRACT))
            fCheckpointsComplete = Contract->TxnCheckpoint(fDeferDirtySync) && fCheckpointsComplete;

        /* Set a checkpoint for register DB. */
        if(Register && (nReleaseInstances & INSTANCES::REGISTER))
            fCheckpointsComplete = Register->TxnCheckpoint(fDeferDirtySync) && fCheckpointsComplete;

        /* Set a checkpoint for ledger DB. */
        if(Ledger && (nReleaseInstances & INSTANCES::LEDGER))
            fCheckpointsComplete = Ledger->TxnCheckpoint(fDeferDirtySync) && fCheckpointsComplete;

        /* Set a checkpoint for client DB. */
        if(Client && (nReleaseInstances & INSTANCES::CLIENT))
            fCheckpointsComplete = Client->TxnCheckpoint(fDeferDirtySync) && fCheckpointsComplete;

        /* Set a checkpoint for trust DB. */
        if(Trust && (nReleaseInstances & INSTANCES::TRUST))
            fCheckpointsComplete = Trust->TxnCheckpoint(fDeferDirtySync) && fCheckpointsComplete;

        /* Set a checkpoint for legacy DB. */
        if(Legacy && (nReleaseInstances & INSTANCES::LEGACY))
            fCheckpointsComplete = Legacy->TxnCheckpoint(fDeferDirtySync) && fCheckpointsComplete;

        if(!fCheckpointsComplete)
        {
            Abort(nFlags, nReleaseInstances);
            return debug::error(FUNCTION, "transaction checkpoint failed; all staged changes aborted");
        }

        cContext.fDurableDecision = true;

        uint64_t nThisBytes = 0;
        const auto AccountJournal = [&nThisBytes, nReleaseInstances](auto* pDatabase, const uint16_t nBit)
        {
            if(pDatabase && (nReleaseInstances & nBit))
                nThisBytes += pDatabase->TxnJournalBytes();
        };
        AccountJournal(Logical, INSTANCES::LOGICAL);
        AccountJournal(Contract, INSTANCES::CONTRACT);
        AccountJournal(Register, INSTANCES::REGISTER);
        AccountJournal(Ledger, INSTANCES::LEDGER);
        AccountJournal(Client, INSTANCES::CLIENT);
        AccountJournal(Trust, INSTANCES::TRUST);
        AccountJournal(Legacy, INSTANCES::LEGACY);

        /* Each recovery group has its own 32-commit and 8 MiB barrier. Counting
         * the other group's parked journals here would let this flush reset both
         * batches while leaving that group's journals and dirty data behind.
         * The begun group, not extra bits OR'd into this call, owns the batch. */
        const uint16_t nGroup = cContext.nParticipants != 0
            ? cContext.nParticipants
            : nReleaseInstances;
        DeferredBatch& cBatch = DeferredFor(nGroup);
        const bool fFlush = !fDeferDirtySync
            || config::fShutdown.load()
            || (cBatch.nCommits + 1 >= nSyncCommitBlocks)
            || (cBatch.nJournalBytes + nThisBytes >= SYNC_COMMIT_BYTES);

        /* Apply participants in a deterministic order, with the database carrying
         * the authoritative best-chain pointer last. Stop on the first failure;
         * the complete journals are retained so startup can roll the decision
         * forward before the node resumes. */
        bool fAllSucceeded = true;

        /* Commit Logical DB transaction. */
        if(fAllSucceeded && Logical && (nReleaseInstances & INSTANCES::LOGICAL))
        {
            if(!Logical->TxnCommit(fFlush))
            {
                debug::error(FUNCTION, "Logical DB commit failed");
                fAllSucceeded = false;
            }
        }

        /* Commit contract DB transaction. */
        if(fAllSucceeded && Contract && (nReleaseInstances & INSTANCES::CONTRACT))
        {
            if(!Contract->TxnCommit(fFlush))
            {
                debug::error(FUNCTION, "Contract DB commit failed");
                fAllSucceeded = false;
            }
        }

        /* Commit register DB transaction. */
        if(fAllSucceeded && Register && (nReleaseInstances & INSTANCES::REGISTER))
        {
            if(!Register->TxnCommit(fFlush))
            {
                debug::error(FUNCTION, "Register DB commit failed");
                fAllSucceeded = false;
            }
        }

        /* Commit the trust DB transaction. */
        if(fAllSucceeded && Trust && (nReleaseInstances & INSTANCES::TRUST))
        {
            if(!Trust->TxnCommit(fFlush))
            {
                debug::error(FUNCTION, "Trust DB commit failed");
                fAllSucceeded = false;
            }
        }

        /* Commit the legacy DB transaction. */
        if(fAllSucceeded && Legacy && (nReleaseInstances & INSTANCES::LEGACY))
        {
            if(!Legacy->TxnCommit(fFlush))
            {
                debug::error(FUNCTION, "Legacy DB commit failed");
                fAllSucceeded = false;
            }
        }

        /* Commit the authoritative full-node pointer last. */
        if(fAllSucceeded && Ledger && (nReleaseInstances & INSTANCES::LEDGER))
        {
            if(!Ledger->TxnCommit(fFlush))
            {
                debug::error(FUNCTION, "Ledger DB commit failed");
                fAllSucceeded = false;
            }
        }

        /* Commit the authoritative client pointer last in MERKLE mode. */
        if(fAllSucceeded && Client && (nReleaseInstances & INSTANCES::CLIENT))
        {
            if(!Client->TxnCommit(fFlush))
            {
                debug::error(FUNCTION, "Client DB commit failed");
                fAllSucceeded = false;
            }
        }

        if(!fAllSucceeded)
        {
            /* Physical state may now be partially applied. Keep every journal for
             * deterministic roll-forward and stop the process before publication
             * or another transaction can build on the partial state. */
            ReleaseMemoryTransactions(nFlags, nReleaseInstances);

            RequireRecovery();
            cContext.nOutcome = TXN_OUTCOME::RECOVERY_REQUIRED;
            ReleaseOwnership();
            ::Shutdown();
            return debug::error(FUNCTION,
                "durable transaction apply failed; journals retained and shutdown requested");
        }

        /* Publish the in-memory database deltas only after durable apply succeeds.
         * A deferred barrier has already fsynced the journal decision. Data fsync
         * is owed by the parked journals until the next flush. */
        if(Contract && (nReleaseInstances & INSTANCES::CONTRACT))
            Contract->MemoryCommit();
        if(Register && (nReleaseInstances & INSTANCES::REGISTER))
            Register->MemoryCommit();
        if(Ledger && (nReleaseInstances & INSTANCES::LEDGER))
            Ledger->MemoryCommit();

        if(fFlush)
        {
            /* Data is synced for this group. Drop only its parked journals so the
             * other group's sequences on shared databases remain replayable.
             * A discard failure keeps both records. */
            if(!cBatch.vSequences.empty() && !DiscardSequences(nReleaseInstances, cBatch.vSequences))
            {
                RequireRecovery();
                cContext.nOutcome = TXN_OUTCOME::RECOVERY_REQUIRED;
                ReleaseOwnership();
                ::Shutdown();
                return debug::error(FUNCTION,
                    "failed to discard parked journals after data sync; shutdown requested");
            }

            if(!ReleasePhysicalTransactions(nReleaseInstances))
            {
                RequireRecovery();
                cContext.nOutcome = TXN_OUTCOME::RECOVERY_REQUIRED;
                ReleaseOwnership();
                ::Shutdown();
                return debug::error(FUNCTION,
                    "failed to durably release transaction journals; shutdown requested");
            }

            if(cBatch.nCommits > 0)
                debug::log(0, FUNCTION, "data durability barrier after ",
                    cBatch.nCommits + 1, " commits");

            cBatch = {};
        }
        else
        {
            const uint64_t nSequence = nNextParkSequence++;
            if(!ParkGroup(nReleaseInstances, nSequence))
            {
                RequireRecovery();
                cContext.nOutcome = TXN_OUTCOME::RECOVERY_REQUIRED;
                ReleaseOwnership();
                ::Shutdown();
                return debug::error(FUNCTION,
                    "failed to park transaction journals; shutdown requested");
            }

            cBatch.vSequences.push_back(nSequence);
            ++cBatch.nCommits;
            cBatch.nJournalBytes += nThisBytes;
            debug::log(2, FUNCTION, "deferred data sync; parked journals at sequence ", nSequence);
        }

        cContext.nOutcome = TXN_OUTCOME::COMMITTED;
        ReleaseOwnership();

        return fAllSucceeded;
    }

    TXN_OUTCOME DurableCommitCoordinator::LastOutcome() const
    {
        return cContext.nOutcome;
    }


    void DurableCommitCoordinator::ResetRecoveryRequired()
    {
        fRecoveryRequired.store(false);
        AllowPendingJournalDiscard();
    }


    bool MayDiscardPendingJournals()
    {
        return cTxnCoordinator.MayDiscardPendingJournals();
    }


    bool TxnRecovery()
    {
        return cTxnCoordinator.Recover();
    }


    bool HasOpenTransaction(const uint8_t nFlags, const uint16_t nInstances)
    {
        return cTxnCoordinator.HasOpenTransaction(nFlags, nInstances);
    }


    bool TxnBegin(const uint8_t nFlags, const uint16_t nInstances)
    {
        return cTxnCoordinator.Begin(nFlags, nInstances);
    }


    bool TxnAbort(const uint8_t nFlags, const uint16_t nInstances)
    {
        return cTxnCoordinator.Abort(nFlags, nInstances);
    }


    bool TxnCommit(const uint8_t nFlags, const uint16_t nInstances, const uint32_t nSyncCommitBlocks)
    {
        return cTxnCoordinator.Commit(nFlags, nInstances, nSyncCommitBlocks);
    }


    TXN_OUTCOME LastTxnOutcome()
    {
        return cTxnCoordinator.LastOutcome();
    }

    TransactionGuard::TransactionGuard(const uint8_t nFlagsIn, const uint16_t nInstancesIn)
    : nFlags(nFlagsIn)
    , nInstances(nInstancesIn)
    , fAcquired(TxnBegin(nFlags, nInstances))
    {
    }


    TransactionGuard::~TransactionGuard()
    {
        if(fAcquired)
            TxnAbort(nFlags, nInstances);
    }


    TransactionGuard::operator bool() const
    {
        return fAcquired;
    }


    #ifdef UNIT_TESTS
    void SetTxnCoordinatorWaitHook(const std::function<void()>& fnHook)
    {
        fnTxnCoordinatorWaitHook = fnHook;
    }


    void ResetTxnRecoveryRequired()
    {
        cTxnCoordinator.ResetRecoveryRequired();
    }
    #endif
}
