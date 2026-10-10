/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2026

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLD/include/global.h>

#include <TAO/Ledger/include/enum.h> //for internal flags

#include <mutex>
#include <thread>

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


    /*  Initialize the global LLD instances. */
    void Initialize()
    {
        debug::log(0, FUNCTION, "Initializing LLD");

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
        TxnRecovery();

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
        if(Contract)
            delete Contract;

        /* Cleanup the ledger database. */
        if(Ledger)
            delete Ledger;

        /* Cleanup the register database. */
        if(Register)
            delete Register;

        /* Cleanup the local database. */
        if(Local)
            delete Local;

        /* Cleanup the client database. */
        if(Client)
            delete Client;

        /* Cleanup the legacy database. */
        if(Legacy)
            delete Legacy;

        /* Cleanup the trust database. */
        if(Trust)
            delete Trust;

        /* Cleanup the logical database. */
        if(Logical)
            delete Logical;

        /* Cleanup the sessions database. */
        if(Sessions)
            delete Sessions;
    }


    /* Check the transactions for recovery. */
    void TxnRecovery()
    {
        /* Flag to determine if there are any failures. */
        bool fRecovery = true;

        /* Special handle for -client mode. */
        if(config::fClient.load())
        {
            /* Check the contract DB journal. */
            if(Contract && !Contract->TxnRecovery())
                fRecovery = false;

            /* Check the register DB journal. */
            if(Register && !Register->TxnRecovery())
                fRecovery = false;

            /* Check the ledger DB journal. */
            if(Client && !Client->TxnRecovery())
                fRecovery = false;

            /* Check the ledger DB journal. */
            if(Logical && !Logical->TxnRecovery())
                fRecovery = false;

            /* Commit the transactions if journals are recovered. */
            if(fRecovery)
            {
                debug::log(0, FUNCTION, "all transactions are complete, recovering...");

                /* Commit Contract DB transaction. */
                if(Contract)
                    Contract->TxnCommit();

                /* Commit Register DB transaction. */
                if(Register)
                    Register->TxnCommit();

                /* Commit the Client DB transaction. */
                if(Client)
                    Client->TxnCommit();

                /* Commit the Logical DB transaction. */
                if(Logical)
                    Logical->TxnCommit();
            }

            /* Abort all the transactions. */
            TxnAbort(TAO::Ledger::FLAGS::BLOCK, INSTANCES::MERKLE);
        }

        /* Regular mainnet mode recovery. */
        else
        {
            /* Check the contract DB journal. */
            if(Contract && !Contract->TxnRecovery())
                fRecovery = false;

            /* Check the register DB journal. */
            if(Register && !Register->TxnRecovery())
                fRecovery = false;

            /* Check the ledger DB journal. */
            if(Ledger && !Ledger->TxnRecovery())
                fRecovery = false;

            /* Check the ledger DB journal. */
            if(Trust && !Trust->TxnRecovery())
                fRecovery = false;

            /* Check the ledger DB journal. */
            if(Legacy && !Legacy->TxnRecovery())
                fRecovery = false;

            /* Commit the transactions if journals are recovered. */
            if(fRecovery)
            {
                debug::log(0, FUNCTION, "all transactions are complete, recovering...");

                /* Commit contract DB transaction. */
                if(Contract)
                    Contract->TxnCommit();

                /* Commit register DB transaction. */
                if(Register)
                    Register->TxnCommit();

                /* Commit ledger DB transaction. */
                if(Ledger)
                    Ledger->TxnCommit();

                /* Commit the trust DB transaction. */
                if(Trust)
                    Trust->TxnCommit();

                /* Commit the legacy DB transaction. */
                if(Legacy)
                    Legacy->TxnCommit();
            }

            /* Abort all the transactions. */
            TxnAbort(TAO::Ledger::FLAGS::BLOCK, INSTANCES::CONSENSUS);
        }


    }


    /* Global handler for all LLD instances. */
    bool HasOpenTransaction(const uint8_t nFlags, const uint16_t nInstances)
    {
        /* Memory-only flag modes do not use physical SectorDatabase transactions. */
        if(nFlags == TAO::Ledger::FLAGS::MEMPOOL || nFlags == TAO::Ledger::FLAGS::MINER || nFlags == TAO::Ledger::FLAGS::SANITIZE)
            return false;

        /* Check each database instance that would be opened by TxnBegin(nFlags, nInstances).
         * Any single instance having pTransaction != nullptr means a transaction is open. */
        if(Ledger   && (nInstances & INSTANCES::LEDGER)   && Ledger->HasTransaction())
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


    /* Process-wide owner of each in-flight transaction mode.
     *
     * pMemory, pMiner, pSanitize, and the physical journal are process-wide,
     * not thread-local. MINER and SANITIZE stay on distinct slots so they can
     * run beside MEMPOOL. MEMPOOL and physical BLOCK both map to pMemory, so
     * those two modes are coordinated at begin time: the second begin is
     * rejected before MemoryBegin. A duplicate begin of a slot this thread
     * already owns is also rejected before MemoryBegin, so ClaimSlot cannot
     * be followed by a replacement of the existing overlay. There is no
     * cross-mode coordinator and no post-acquire recovery-required flag. A
     * slot mutex is held across the mutation of that slot only. Shared-overlay
     * checks lock MEMPOOL before PHYSICAL, matching abort and commit.
     *
     * An unowned memory abort or MEMPOOL commit is rejected and does not touch
     * the overlay. MINER and SANITIZE commits remain a no-mutation short-circuit
     * when this thread owns that slot, including when MEMPOOL coexists with it.
     * Sole ownership is not required. A foreign owner, another thread holding
     * the requested slot, rejects that commit even if this thread owns nothing,
     * and does not release the owner. A physical abort or commit owned by
     * another thread is rejected before TxnRelease or TxnCommit.
     * MemoryRelease/MemoryCommit of pMemory is skipped when another thread owns
     * the MEMPOOL slot. */
    struct TxnSlot
    {
        std::mutex MUTEX;
        bool fOwned = false;
        std::thread::id idOwner{};
    };


    enum class TxnKind : uint8_t
    {
        MEMPOOL,
        MINER,
        SANITIZE,
        PHYSICAL
    };


    static TxnSlot& MempoolSlot()
    {
        static TxnSlot slot;
        return slot;
    }


    static TxnSlot& MinerSlot()
    {
        static TxnSlot slot;
        return slot;
    }


    static TxnSlot& SanitizeSlot()
    {
        static TxnSlot slot;
        return slot;
    }


    static TxnSlot& PhysicalSlot()
    {
        static TxnSlot slot;
        return slot;
    }


    static TxnKind KindOf(const uint8_t nFlags)
    {
        if(nFlags == TAO::Ledger::FLAGS::MEMPOOL)
            return TxnKind::MEMPOOL;

        if(nFlags == TAO::Ledger::FLAGS::MINER)
            return TxnKind::MINER;

        if(nFlags == TAO::Ledger::FLAGS::SANITIZE)
            return TxnKind::SANITIZE;

        return TxnKind::PHYSICAL;
    }


    static TxnSlot& SlotFor(const TxnKind kind)
    {
        switch(kind)
        {
        case TxnKind::MEMPOOL:
            return MempoolSlot();
        case TxnKind::MINER:
            return MinerSlot();
        case TxnKind::SANITIZE:
            return SanitizeSlot();
        default:
            return PhysicalSlot();
        }
    }


    /* Caller holds slot.MUTEX. */
    static bool OwnedByThisThread(const TxnSlot& slot)
    {
        return slot.fOwned && slot.idOwner == std::this_thread::get_id();
    }


    /* Caller holds slot.MUTEX. */
    static bool OwnedByOther(const TxnSlot& slot)
    {
        return slot.fOwned && slot.idOwner != std::this_thread::get_id();
    }


    /* Bits this thread currently owns. Updated only by this thread, under the
     * corresponding slot mutex, so a same-thread mode mismatch can be rejected
     * without locking every slot. */
    static thread_local uint8_t nThreadOwnedMask = 0;


    static uint8_t MaskOf(const TxnKind kind)
    {
        switch(kind)
        {
        case TxnKind::MEMPOOL:
            return 0x01;
        case TxnKind::MINER:
            return 0x02;
        case TxnKind::SANITIZE:
            return 0x04;
        default:
            return 0x08;
        }
    }


    /* Caller holds slot.MUTEX. */
    static void ClaimSlot(TxnSlot& slot, const TxnKind kind)
    {
        slot.fOwned = true;
        slot.idOwner = std::this_thread::get_id();
        nThreadOwnedMask |= MaskOf(kind);
    }


    /* Caller holds slot.MUTEX. */
    static void ReleaseSlot(TxnSlot& slot, const TxnKind kind)
    {
        slot.fOwned = false;
        slot.idOwner = std::thread::id();
        nThreadOwnedMask &= static_cast<uint8_t>(~MaskOf(kind));
    }


    static void MemoryReleaseSelected(const uint8_t nFlags, const uint16_t nInstances)
    {
        if(Contract && (nInstances & INSTANCES::CONTRACT))
            Contract->MemoryRelease(nFlags);

        if(Register && (nInstances & INSTANCES::REGISTER))
            Register->MemoryRelease(nFlags);

        if(Ledger && (nInstances & INSTANCES::LEDGER))
            Ledger->MemoryRelease(nFlags);
    }


    static void MemoryCommitSelected(const uint16_t nInstances)
    {
        if(Contract && (nInstances & INSTANCES::CONTRACT))
            Contract->MemoryCommit();

        if(Register && (nInstances & INSTANCES::REGISTER))
            Register->MemoryCommit();

        if(Ledger && (nInstances & INSTANCES::LEDGER))
            Ledger->MemoryCommit();
    }


    /* Global handler for all LLD instances. */
    void TxnBegin(const uint8_t nFlags, const uint16_t nInstances)
    {
        /* Claim the slot before creating its overlay or journal. A second begin,
         * including one from the current owner, must not reach MemoryBegin:
         * that call deletes the existing process-wide overlay. MEMPOOL and
         * physical BLOCK share pMemory, so either mode being owned rejects the
         * other begin before MemoryBegin. Lock MEMPOOL before PHYSICAL. */
        const TxnKind kind = KindOf(nFlags);
        std::unique_lock<std::mutex> lkPrimary;
        std::unique_lock<std::mutex> lkSecondary;

        if(kind == TxnKind::MEMPOOL || kind == TxnKind::PHYSICAL)
        {
            lkPrimary = std::unique_lock<std::mutex>(MempoolSlot().MUTEX);
            lkSecondary = std::unique_lock<std::mutex>(PhysicalSlot().MUTEX);

            TxnSlot& slot = (kind == TxnKind::MEMPOOL) ? MempoolSlot() : PhysicalSlot();
            const TxnSlot& other = (kind == TxnKind::MEMPOOL) ? PhysicalSlot() : MempoolSlot();
            /* OwnedByOther misses a same-thread duplicate. slot.fOwned covers both. */
            if(slot.fOwned)
            {
                debug::error(FUNCTION, OwnedByThisThread(slot)
                    ? "transaction mode is already open"
                    : "transaction mode is owned by another thread");
                return;
            }

            if(other.fOwned)
            {
                debug::error(FUNCTION, "shared memory overlay is already owned");
                return;
            }

            ClaimSlot(slot, kind);

            /* The claim makes a concurrent begin of the other mode fail. Drop
             * the unused lock before creating the overlay. */
            if(kind == TxnKind::MEMPOOL)
                lkSecondary.unlock();
            else
                lkPrimary.unlock();
        }
        else
        {
            TxnSlot& slot = SlotFor(kind);
            lkPrimary = std::unique_lock<std::mutex>(slot.MUTEX);
            /* Same check as the shared-overlay path: a same-thread duplicate
             * would otherwise delete pMiner or pSanitize inside MemoryBegin. */
            if(slot.fOwned)
            {
                debug::error(FUNCTION, OwnedByThisThread(slot)
                    ? "transaction mode is already open"
                    : "transaction mode is owned by another thread");
                return;
            }

            ClaimSlot(slot, kind);
        }

        /* Start the contract DB transaction. */
        if(Contract && (nInstances & INSTANCES::CONTRACT))
            Contract->MemoryBegin(nFlags);

        /* Start the register DB transacdtion. */
        if(Register && (nInstances & INSTANCES::REGISTER))
            Register->MemoryBegin(nFlags);

        /* Start the ledger DB transaction. */
        if(Ledger && (nInstances & INSTANCES::LEDGER))
            Ledger->MemoryBegin(nFlags);

        /* Handle memory commits if in memory m ode. */
        if(nFlags == TAO::Ledger::FLAGS::MEMPOOL || nFlags == TAO::Ledger::FLAGS::MINER || nFlags == TAO::Ledger::FLAGS::SANITIZE)
            return;

        /* Start the Logical DB transaction. */
        if(Logical && (nInstances & INSTANCES::LOGICAL))
            Logical->TxnBegin();

        /* Start the contract DB transaction. */
        if(Contract && (nInstances & INSTANCES::CONTRACT))
            Contract->TxnBegin();

        /* Start the register DB transacdtion. */
        if(Register && (nInstances & INSTANCES::REGISTER))
            Register->TxnBegin();

        /* Start the ledger DB transaction. */
        if(Ledger && (nInstances & INSTANCES::LEDGER))
            Ledger->TxnBegin();

        /* Start the client DB transaction. */
        if(Client && (nInstances & INSTANCES::CLIENT))
            Client->TxnBegin();

        /* Start the trust DB transaction. */
        if(Trust && (nInstances & INSTANCES::TRUST))
            Trust->TxnBegin();

        /* Start the legacy DB transaction. */
        if(Legacy && (nInstances & INSTANCES::LEGACY))
            Legacy->TxnBegin();
    }


    /* Global handler for all LLD instances. */
    void TxnAbort(const uint8_t nFlags, const uint16_t nInstances)
    {
        const TxnKind kind = KindOf(nFlags);

        /* Memory overlays are process-wide. An unowned thread, or a thread that
         * owns a different mode, must not MemoryRelease this slot. */
        if(kind != TxnKind::PHYSICAL)
        {
            TxnSlot& slot = SlotFor(kind);
            std::lock_guard<std::mutex> lk(slot.MUTEX);
            if(!OwnedByThisThread(slot))
            {
                debug::error(FUNCTION, "transaction mode does not match current owner");
                return;
            }

            MemoryReleaseSelected(nFlags, nInstances);
            ReleaseSlot(slot, kind);
            return;
        }

        /* MemoryRelease(BLOCK) deletes pMemory, including the overlay created by
         * TxnBegin(BLOCK). Do that only for the physical owner, and skip it when
         * another thread owns the MEMPOOL slot. An unowned abort must not delete
         * either overlay. Lock MEMPOOL before PHYSICAL. */
        std::unique_lock<std::mutex> lkMem(MempoolSlot().MUTEX);
        std::lock_guard<std::mutex> lkPhys(PhysicalSlot().MUTEX);
        if(OwnedByOther(PhysicalSlot()))
        {
            debug::error(FUNCTION, "transaction mode does not match current owner");
            return;
        }

        const bool fReleasePhysical = OwnedByThisThread(PhysicalSlot());
        if(fReleasePhysical && !OwnedByOther(MempoolSlot()))
        {
            const bool fReleaseMempool = OwnedByThisThread(MempoolSlot());
            MemoryReleaseSelected(nFlags, nInstances);
            if(fReleaseMempool)
                ReleaseSlot(MempoolSlot(), TxnKind::MEMPOOL);
        }

        lkMem.unlock();

        /* Abort the Logical DB transaction. */
        if(Logical && (nInstances & INSTANCES::LOGICAL))
            Logical->TxnRelease();

        /* Abort the contract DB transaction. */
        if(Contract && (nInstances & INSTANCES::CONTRACT))
            Contract->TxnRelease();

        /* Abort the register DB transaction. */
        if(Register && (nInstances & INSTANCES::REGISTER))
            Register->TxnRelease();

        /* Abort the ledger DB transaction. */
        if(Ledger && (nInstances & INSTANCES::LEDGER))
            Ledger->TxnRelease();

        /* Abort the client DB transaction. */
        if(Client && (nInstances & INSTANCES::CLIENT))
            Client->TxnRelease();

        /* Abort the trust DB transaction. */
        if(Trust && (nInstances & INSTANCES::TRUST))
            Trust->TxnRelease();

        /* Abort the legacy DB transaction. */
        if(Legacy && (nInstances & INSTANCES::LEGACY))
            Legacy->TxnRelease();

        if(fReleasePhysical)
            ReleaseSlot(PhysicalSlot(), TxnKind::PHYSICAL);
    }


    /* Global handler for all LLD instances. */
    bool TxnCommit(const uint8_t nFlags, const uint16_t nInstances)
    {
        const TxnKind kind = KindOf(nFlags);

        /* MINER and SANITIZE commits do not call MemoryCommit and do not release
        * the owner. Decide from the requested slot, not sole ownership: MEMPOOL
        * may coexist with this mode. Reject a foreign owner, and reject when
        * this thread owns a different mode but not this slot. A globally
        * unowned slot on a thread that owns nothing stays a true short-circuit. */
        if(kind == TxnKind::MINER || kind == TxnKind::SANITIZE)
        {
           TxnSlot& slot = SlotFor(kind);
           std::lock_guard<std::mutex> lk(slot.MUTEX);
           if(OwnedByOther(slot) || (!OwnedByThisThread(slot) && nThreadOwnedMask != 0))
               return debug::error(FUNCTION, "transaction mode does not match current owner");

           return true;
        }

        /* An unowned MEMPOOL commit, or one owned by another thread, must not
        * apply pMemory. Owning MINER or SANITIZE as well is not a conflict and
        * does not release those slots. */
        if(kind == TxnKind::MEMPOOL)
        {
           std::lock_guard<std::mutex> lk(MempoolSlot().MUTEX);
           if(!OwnedByThisThread(MempoolSlot()))
               return debug::error(FUNCTION, "transaction mode does not match current owner");

            MemoryCommitSelected(nInstances);
            ReleaseSlot(MempoolSlot(), TxnKind::MEMPOOL);
            return true;
        }

        /* TxnBegin(BLOCK) creates pMemory before the physical journal. Commit that
         * overlay only when this thread owns the physical slot, and never when
         * another thread owns MEMPOOL. Lock MEMPOOL before PHYSICAL, then drop the
         * MEMPOOL lock before the disk commit. */
        std::unique_lock<std::mutex> lkMem(MempoolSlot().MUTEX);
        std::lock_guard<std::mutex> lkPhys(PhysicalSlot().MUTEX);
        if(OwnedByOther(PhysicalSlot()))
            return debug::error(FUNCTION, "transaction mode does not match current owner");

        const bool fReleasePhysical = OwnedByThisThread(PhysicalSlot());
        if(fReleasePhysical && !OwnedByOther(MempoolSlot()))
        {
            const bool fReleaseMempool = OwnedByThisThread(MempoolSlot());
            MemoryCommitSelected(nInstances);
            if(fReleaseMempool)
                ReleaseSlot(MempoolSlot(), TxnKind::MEMPOOL);
        }

        lkMem.unlock();

        /* Set a checkpoint for Logical DB. */
        if(Logical && (nInstances & INSTANCES::LOGICAL))
            Logical->TxnCheckpoint();

        /* Set a checkpoint for contract DB. */
        if(Contract && (nInstances & INSTANCES::CONTRACT))
            Contract->TxnCheckpoint();

        /* Set a checkpoint for register DB. */
        if(Register && (nInstances & INSTANCES::REGISTER))
            Register->TxnCheckpoint();

        /* Set a checkpoint for ledger DB. */
        if(Ledger && (nInstances & INSTANCES::LEDGER))
            Ledger->TxnCheckpoint();

        /* Set a checkpoint for client DB. */
        if(Client && (nInstances & INSTANCES::CLIENT))
            Client->TxnCheckpoint();

        /* Set a checkpoint for trust DB. */
        if(Trust && (nInstances & INSTANCES::TRUST))
            Trust->TxnCheckpoint();

        /* Set a checkpoint for legacy DB. */
        if(Legacy && (nInstances & INSTANCES::LEGACY))
            Legacy->TxnCheckpoint();


        /* Aggregate the per-instance TxnCommit() results.  All selected
         * instances are attempted regardless of individual failures — do NOT
         * short-circuit on the first failure, so that state doesn't diverge
         * further than necessary if one instance fails. */
        bool fAllSucceeded = true;

        /* Commit Logical DB transaction. */
        if(Logical && (nInstances & INSTANCES::LOGICAL))
        {
            if(!Logical->TxnCommit())
            {
                debug::error(FUNCTION, "Logical DB commit failed");
                fAllSucceeded = false;
            }
        }

        /* Commit contract DB transaction. */
        if(Contract && (nInstances & INSTANCES::CONTRACT))
        {
            if(!Contract->TxnCommit())
            {
                debug::error(FUNCTION, "Contract DB commit failed");
                fAllSucceeded = false;
            }
        }

        /* Commit register DB transaction. */
        if(Register && (nInstances & INSTANCES::REGISTER))
        {
            if(!Register->TxnCommit())
            {
                debug::error(FUNCTION, "Register DB commit failed");
                fAllSucceeded = false;
            }
        }

        /* Commit ledger DB transaction. */
        if(Ledger && (nInstances & INSTANCES::LEDGER))
        {
            if(!Ledger->TxnCommit())
            {
                debug::error(FUNCTION, "Ledger DB commit failed");
                fAllSucceeded = false;
            }
        }

        /* Commit the client DB transaction. */
        if(Client && (nInstances & INSTANCES::CLIENT))
        {
            if(!Client->TxnCommit())
            {
                debug::error(FUNCTION, "Client DB commit failed");
                fAllSucceeded = false;
            }
        }

        /* Commit the trust DB transaction. */
        if(Trust && (nInstances & INSTANCES::TRUST))
        {
            if(!Trust->TxnCommit())
            {
                debug::error(FUNCTION, "Trust DB commit failed");
                fAllSucceeded = false;
            }
        }

        /* Commit the legacy DB transaction. */
        if(Legacy && (nInstances & INSTANCES::LEGACY))
        {
            if(!Legacy->TxnCommit())
            {
                debug::error(FUNCTION, "Legacy DB commit failed");
                fAllSucceeded = false;
            }
        }


        /* Release the checkpoint markers for all selected instances.
         * TxnRelease runs unconditionally after TxnCommit, regardless of commit
         * outcome.  On success, SectorDatabase::TxnCommit() already nulled
         * pTransaction; on failure it may still be set and TxnRelease will clean
         * it up here.  Leaving a failed transaction intact for retry is not
         * supported at the LLD layer — callers that receive false should abort
         * their higher-level operation and rebuild rather than retrying the same
         * transaction object.
         *
         * Note: TxnRelease is called for every selected instance below,
         * regardless of whether that instance's TxnCommit succeeded or failed. */
        if(Logical && (nInstances & INSTANCES::LOGICAL))
            Logical->TxnRelease();

        /* Release the contract DB transaction. */
        if(Contract && (nInstances & INSTANCES::CONTRACT))
            Contract->TxnRelease();

        /* Release the register DB transaction. */
        if(Register && (nInstances & INSTANCES::REGISTER))
            Register->TxnRelease();

        /* Release the ledger DB transaction. */
        if(Ledger && (nInstances & INSTANCES::LEDGER))
            Ledger->TxnRelease();

        /* Release the client DB transaction. */
        if(Client && (nInstances & INSTANCES::CLIENT))
            Client->TxnRelease();

        /* Release the trust DB transaction. */
        if(Trust && (nInstances & INSTANCES::TRUST))
            Trust->TxnRelease();

        /* Release the legacy DB transaction. */
        if(Legacy && (nInstances & INSTANCES::LEGACY))
            Legacy->TxnRelease();

        if(fReleasePhysical)
            ReleaseSlot(PhysicalSlot(), TxnKind::PHYSICAL);

        return fAllSucceeded;
    }
}
