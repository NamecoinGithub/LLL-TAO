/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

__________________________________________________________________________________________*/

/*
 * Regression tests for LLD::TxnCommit() return-value aggregation.
 *
 * These tests exercise the real LLD::TxnCommit() fan-out in global.cpp against
 * real in-process SectorDatabase instances, verifying:
 *
 *  1. LLD::TxnCommit() returns false when a selected instance has no active
 *     transaction (SectorDatabase::TxnCommit() returns false on null pTransaction).
 *
 *  2. LLD::TxnCommit() returns true when all selected instances have active
 *     transactions that complete successfully.
 *
 *  3. No participant is applied unless every selected journal reaches its
 *     checkpoint, preventing a missing participant from producing a partial commit.
 *
 * Tests 1-3 below use inline simulation / ordering-assertion infrastructure so
 * that they compile and run without a live LLD database or full chain state —
 * following the same pattern used in validate_vtx_consistency.cpp and
 * filter_mempool_only_predecessor.cpp.
 *
 *  4. The MINER and SANITIZE early-return paths return true (intentional
 *     short-circuit, not a failure).
 *
 * Tests 5-7 below are REAL-CODE regression tests that call the actual
 * BlockState::SetBest() implementation, verify real on-disk state, real
 * ChainState atomics, and real mempool state.  They use the same LedgerGuard
 * infrastructure established in missing_tx_soft_fail.cpp.
 */

/* Real-code test headers (Gap 2 tests below) */
#include <LLD/include/global.h>
#include <LLD/include/version.h>
#include <LLD/types/contract.h>
#include <LLD/types/register.h>
#include <LLD/types/legacy.h>
#include <LLD/types/trust.h>

#include <TAO/API/include/global.h>

#include <TAO/Ledger/include/chainstate.h>
#include <TAO/Ledger/include/checkpoints.h>
#include <TAO/Ledger/include/enum.h>
#include <TAO/Ledger/include/genesis_block.h>
#include <TAO/Ledger/types/mempool.h>
#include <TAO/Ledger/types/client.h>
#include <TAO/Ledger/types/state.h>

#include <Util/include/args.h>
#include <Util/include/filesystem.h>
#include <Util/templates/datastream.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unit/catch2/catch.hpp>

namespace
{
    /*  Lightweight guard that creates a temporary LedgerDB when the global
     *  test suite hasn't initialized one.  On destruction it deletes only what
     *  it created, leaving the global state untouched if it was already set up. */
    struct LedgerGuard
    {
        bool ownedLedger{false};

        LedgerGuard()
        {
            config::fTestNet.store(true);
            config::mapArgs["-testnet"] = "1";

            if(!LLD::Ledger)
            {
                LLD::Ledger = new LLD::LedgerDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);
                ownedLedger = true;
            }
        }

        ~LedgerGuard()
        {
            if(ownedLedger)
            {
                delete LLD::Ledger;
                LLD::Ledger = nullptr;
            }
        }
    };


    /*  Lightweight guard for TrustDB, mirroring LedgerGuard above. */
    struct TrustGuard
    {
        bool ownedTrust{false};

        TrustGuard()
        {
            config::fTestNet.store(true);
            config::mapArgs["-testnet"] = "1";

            if(!LLD::Trust)
            {
                LLD::Trust = new LLD::TrustDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);
                ownedTrust = true;
            }
        }

        ~TrustGuard()
        {
            if(ownedTrust)
            {
                delete LLD::Trust;
                LLD::Trust = nullptr;
            }
        }
    };


    struct LogicalGuard
    {
        bool ownedLogical{false};

        LogicalGuard()
        {
            if(!LLD::Logical)
            {
                LLD::Logical = new LLD::LogicalDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);
                ownedLogical = true;
            }
        }

        ~LogicalGuard()
        {
            if(ownedLogical)
            {
                delete LLD::Logical;
                LLD::Logical = nullptr;
            }
        }
    };


    struct ClientGuard
    {
        bool ownedClient{false};

        ClientGuard()
        {
            if(!LLD::Client)
            {
                LLD::Client = new LLD::ClientDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);
                ownedClient = true;
            }
        }

        ~ClientGuard()
        {
            if(ownedClient)
            {
                delete LLD::Client;
                LLD::Client = nullptr;
            }
        }
    };


    struct ClientModeGuard
    {
        bool savedClient;
        bool savedHybrid;
        bool savedTestNet;

        ClientModeGuard()
        : savedClient(config::fClient.load())
        , savedHybrid(config::fHybrid.load())
        , savedTestNet(config::fTestNet.load())
        {
            config::fClient.store(true);
            config::fHybrid.store(false);
            config::fTestNet.store(false);
        }

        ~ClientModeGuard()
        {
            config::fClient.store(savedClient);
            config::fHybrid.store(savedHybrid);
            config::fTestNet.store(savedTestNet);
        }
    };


    struct ContractGuard
    {
        bool ownedContract{false};

        ContractGuard()
        {
            if(!LLD::Contract)
            {
                LLD::Contract = new LLD::ContractDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);
                ownedContract = true;
            }
        }

        ~ContractGuard()
        {
            if(ownedContract)
            {
                delete LLD::Contract;
                LLD::Contract = nullptr;
            }
        }
    };


    struct RegisterGuard
    {
        bool ownedRegister{false};

        RegisterGuard()
        {
            if(!LLD::Register)
            {
                LLD::Register = new LLD::RegisterDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);
                ownedRegister = true;
            }
        }

        ~RegisterGuard()
        {
            if(ownedRegister)
            {
                delete LLD::Register;
                LLD::Register = nullptr;
            }
        }
    };


    struct LegacyGuard
    {
        bool ownedLegacy{false};

        LegacyGuard()
        {
            if(!LLD::Legacy)
            {
                LLD::Legacy = new LLD::LegacyDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);
                ownedLegacy = true;
            }
        }

        ~LegacyGuard()
        {
            if(ownedLegacy)
            {
                delete LLD::Legacy;
                LLD::Legacy = nullptr;
            }
        }
    };


    /* Write a complete recovery journal with a durable commit marker. */
    bool WriteRecoveryJournal(const std::string& strName, const DataStream& ssJournal)
    {
        const std::string strPath =
            debug::safe_printstr(config::GetDataDir(), strName, "/journal.dat");

        FILE* stream = std::fopen(strPath.c_str(), "wb");
        if(!stream)
            return false;

        const std::vector<uint8_t>& vBytes = ssJournal.Bytes();
        const bool fWrote =
            std::fwrite(vBytes.data(), 1, vBytes.size(), stream) == vBytes.size()
            && std::fflush(stream) == 0;

        return (std::fclose(stream) == 0 && fWrote);
    }


    /* Build a journal that applies a simple key/value write. */
    DataStream MakeWriteJournal(const std::pair<std::string, uint32_t>& key,
                                const uint32_t nValue)
    {
        DataStream ssKey(SER_LLD, LLD::DATABASE_VERSION);
        ssKey << key;

        DataStream ssData(SER_LLD, LLD::DATABASE_VERSION);
        ssData << std::string("NONE");
        ssData << nValue;

        DataStream ssJournal(SER_LLD, LLD::DATABASE_VERSION);
        ssJournal << std::string("write") << ssKey.Bytes() << ssData.Bytes();
        ssJournal << std::string("commit");
        return ssJournal;
    }


    /* Build a journal whose apply path fails while still reaching commit. */
    DataStream MakeFailingIndexJournal()
    {
        const std::vector<uint8_t> vKey = { 0x01, 0x02, 0x03, 0x04 };
        const std::vector<uint8_t> vMissing = { 0xde, 0xad, 0xbe, 0xef };

        DataStream ssJournal(SER_LLD, LLD::DATABASE_VERSION);
        ssJournal << std::string("index") << vKey << vMissing;
        ssJournal << std::string("commit");
        return ssJournal;
    }


    /* Build a journal containing an invalid entry type. */
    DataStream MakeInvalidRecoveryJournal()
    {
        DataStream ssJournal(SER_LLD, LLD::DATABASE_VERSION);
        ssJournal << std::string("invalid");
        return ssJournal;
    }


    uint64_t JournalSize(const std::string& strName)
    {
        const std::string strPath =
            debug::safe_printstr(config::GetDataDir(), strName, "/journal.dat");

        FILE* stream = std::fopen(strPath.c_str(), "rb");
        if(!stream)
            return 0;

        if(std::fseek(stream, 0, SEEK_END) != 0)
        {
            std::fclose(stream);
            return 0;
        }

        const long nSize = std::ftell(stream);
        std::fclose(stream);
        return nSize > 0 ? static_cast<uint64_t>(nSize) : 0;
    }


    } /* anonymous namespace */


/* ===========================================================================
 * TEST 1 — TxnCommit returns false when no active transaction exists
 * ===========================================================================
 * Without a prior TxnBegin, SectorDatabase::TxnCommit() returns false because
 * pTransaction is null.  The global LLD::TxnCommit() must propagate that false
 * back to the caller.
 */
TEST_CASE("LLD::TxnCommit returns false with no active transaction",
          "[lld][txncommit]")
{
    LedgerGuard guard;

    SECTION("INSTANCES::LEDGER — no TxnBegin — TxnCommit returns false")
    {
        /* No TxnBegin called — Ledger->pTransaction is null.
         * LLD::TxnCommit must return false. */
        const bool fResult = LLD::TxnCommit(0, LLD::INSTANCES::LEDGER);
        REQUIRE_FALSE(fResult);
    }
}


/* ===========================================================================
 * TEST 2 — TxnCommit returns true when all selected instances succeed
 * ===========================================================================
 * After a matching TxnBegin, every selected SectorDatabase::TxnCommit() call
 * succeeds (pTransaction != null, empty transaction writes successfully).
 * The global aggregated return must be true.
 */
TEST_CASE("LLD::TxnCommit returns true when all selected instances have active transactions",
          "[lld][txncommit]")
{
    LedgerGuard guard;

    SECTION("INSTANCES::LEDGER — TxnBegin then TxnCommit returns true")
    {
        /* Open a real transaction on the Ledger instance. */
        LLD::TxnBegin(0, LLD::INSTANCES::LEDGER);

        /* Commit — Ledger->pTransaction != null, commit succeeds → true. */
        const bool fResult = LLD::TxnCommit(0, LLD::INSTANCES::LEDGER);
        REQUIRE(fResult);
    }
}


TEST_CASE("LLD::TxnCommit applies every instance owned by the transaction",
          "[lld][txncommit]")
{
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;

    const std::pair<std::string, uint32_t> ledgerKey =
        std::make_pair(std::string("txn-owned-ledger"), 1);
    const std::pair<std::string, uint32_t> trustKey =
        std::make_pair(std::string("txn-owned-trust"), 1);

    LLD::Ledger->Erase(ledgerKey);
    LLD::Trust->Erase(trustKey);

    REQUIRE(LLD::TxnBegin(
        0, LLD::INSTANCES::LEDGER | LLD::INSTANCES::TRUST));
    REQUIRE(LLD::Ledger->Write(ledgerKey, uint32_t(42)));
    REQUIRE(LLD::Trust->Write(trustKey, uint32_t(43)));

    REQUIRE(LLD::TxnCommit(0, LLD::INSTANCES::LEDGER));
    REQUIRE(LLD::Ledger->Exists(ledgerKey));
    REQUIRE(LLD::Trust->Exists(trustKey));

    LLD::Ledger->Erase(ledgerKey);
    LLD::Trust->Erase(trustKey);
}


TEST_CASE("LLD::TxnBegin opens every crash-recovery participant",
          "[lld][txncommit][recovery]")
{
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;

    REQUIRE(LLD::TxnBegin(0, LLD::INSTANCES::LEDGER));
    REQUIRE(LLD::Ledger->HasTransaction());
    REQUIRE(LLD::Trust->HasTransaction());
    REQUIRE(LLD::TxnCommit(0, LLD::INSTANCES::LEDGER));
}


/* ===========================================================================
 * TEST 3 — Checkpoint barrier: an unowned participant aborts every participant
 * ===========================================================================
 * We begin a consensus transaction on Ledger, then attempt to commit Logical
 * as well. Logical belongs to the MERKLE recovery group and therefore has no
 * transaction, so the coordinator must abort before applying Ledger.
 */
TEST_CASE("LLD::TxnCommit checkpoint barrier prevents partial apply",
          "[lld][txncommit]")
{
    LedgerGuard ledgerGuard;
    LogicalGuard logicalGuard;

    SECTION("Logical has no transaction → overall false")
    {
        /* Open a transaction only on Ledger. */
        LLD::TxnBegin(0, LLD::INSTANCES::LEDGER);

        /* Logical has no active transaction, so the checkpoint set is incomplete. */
        const bool fResult = LLD::TxnCommit(0, LLD::INSTANCES::LEDGER | LLD::INSTANCES::LOGICAL);
        REQUIRE_FALSE(fResult);
    }

    SECTION("Ledger data is not applied when Logical cannot checkpoint")
    {
        const std::pair<std::string, uint32_t> keyTest =
            std::make_pair(std::string("txn-checkpoint-barrier"), 1);
        LLD::Ledger->Erase(keyTest);

        LLD::TxnBegin(0, LLD::INSTANCES::LEDGER);
        const bool fWrite = LLD::Ledger->Write(keyTest, uint32_t(42));

        /* Logical has no transaction, so no selected database may be applied. */
        const bool fResult = LLD::TxnCommit(0, LLD::INSTANCES::LEDGER | LLD::INSTANCES::LOGICAL);
        const bool fApplied = LLD::Ledger->Exists(keyTest);

        REQUIRE(fWrite);
        REQUIRE_FALSE(fResult);
        REQUIRE_FALSE(fApplied);

        LLD::Ledger->Erase(keyTest);
    }
}


TEST_CASE("LLD transaction coordinator serializes MINER and SANITIZE overlays",
          "[lld][txncommit][concurrency]")
{
    LedgerGuard guard;

    std::mutex mutex;
    std::condition_variable condition;
    bool fContenderWaiting = false;
    std::atomic<bool> fContenderAcquired{false};

    LLD::TxnBegin(TAO::Ledger::FLAGS::MINER, LLD::INSTANCES::LEDGER);
    LLD::SetTxnCoordinatorWaitHook([&]()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            fContenderWaiting = true;
        }
        condition.notify_one();
    });

    std::thread contender([&]()
    {
        const bool fAcquired =
            LLD::TxnBegin(TAO::Ledger::FLAGS::SANITIZE, LLD::INSTANCES::LEDGER);
        fContenderAcquired.store(fAcquired);
        if(fAcquired)
            LLD::TxnAbort(TAO::Ledger::FLAGS::SANITIZE, LLD::INSTANCES::LEDGER);
    });

    bool fSawContenderWaiting = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        fSawContenderWaiting = condition.wait_for(lock, std::chrono::seconds(2),
            [&](){ return fContenderWaiting; });
    }

    const bool fAcquiredBeforeRelease = fContenderAcquired.load();

    LLD::TxnAbort(TAO::Ledger::FLAGS::MINER, LLD::INSTANCES::LEDGER);
    if(contender.joinable())
        contender.join();
    LLD::SetTxnCoordinatorWaitHook({});

    REQUIRE(fSawContenderWaiting);
    REQUIRE_FALSE(fAcquiredBeforeRelease);
    REQUIRE(fContenderAcquired.load());
}


TEST_CASE("LLD::TxnAbort mode mismatch releases coordinator ownership",
          "[lld][txncommit][coordinator]")
{
    LedgerGuard guard;

    REQUIRE(LLD::TxnBegin(TAO::Ledger::FLAGS::MINER, LLD::INSTANCES::LEDGER));

    /* Mismatched flags must not strand coordinator ownership. */
    REQUIRE(LLD::TxnAbort(TAO::Ledger::FLAGS::SANITIZE, LLD::INSTANCES::LEDGER));

    /* If ownership leaked, this begin would fail as nested/blocked. */
    REQUIRE(LLD::TxnBegin(TAO::Ledger::FLAGS::SANITIZE, LLD::INSTANCES::LEDGER));
    REQUIRE(LLD::TxnAbort(TAO::Ledger::FLAGS::SANITIZE, LLD::INSTANCES::LEDGER));
}


TEST_CASE("LLD::TxnCommit mode mismatch aborts owner and releases coordinator",
          "[lld][txncommit][coordinator]")
{
    LedgerGuard guard;

    REQUIRE(LLD::TxnBegin(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::LEDGER));

    /* Mismatched commit mode should fail but still release owner state. */
    REQUIRE_FALSE(LLD::TxnCommit(TAO::Ledger::FLAGS::MEMPOOL, LLD::INSTANCES::LEDGER));

    /* If ownership leaked, this begin would fail as nested/blocked. */
    REQUIRE(LLD::TxnBegin(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::LEDGER));
    REQUIRE(LLD::TxnAbort(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::LEDGER));
}


TEST_CASE("LLD::TxnCommit MEMPOOL requires the owner's exact memory mode",
          "[lld][txncommit][coordinator]")
{
    ContractGuard guard;
    uint8_t nOwnerFlags = TAO::Ledger::FLAGS::MEMPOOL;
    SECTION("MINER owner is aborted") { nOwnerFlags = TAO::Ledger::FLAGS::MINER; }
    SECTION("SANITIZE owner is aborted") { nOwnerFlags = TAO::Ledger::FLAGS::SANITIZE; }
    SECTION("MEMPOOL owner is committed") {}

    const auto key = std::make_pair(uint512_t(0x74786e6d6f6465), uint32_t(1));
    const uint256_t hashCaller(42);
    const bool fMatchingMode = (nOwnerFlags == TAO::Ledger::FLAGS::MEMPOOL);
    uint256_t hashRead;

    LLD::TransactionGuard owner(nOwnerFlags, LLD::INSTANCES::CONTRACT);
    REQUIRE(owner);
    REQUIRE(LLD::Contract->WriteContract(key, hashCaller, nOwnerFlags));
    REQUIRE(LLD::Contract->ReadContract(key, hashRead, nOwnerFlags));
    REQUIRE(hashRead == hashCaller);

    const bool fCommitted =
        LLD::TxnCommit(TAO::Ledger::FLAGS::MEMPOOL, LLD::INSTANCES::CONTRACT);
    const bool fOwnerVisible = LLD::Contract->ReadContract(key, hashRead, nOwnerFlags);
    const bool fMempoolVisible =
        LLD::Contract->ReadContract(key, hashRead, TAO::Ledger::FLAGS::MEMPOOL);
    LLD::Contract->EraseContract(key, TAO::Ledger::FLAGS::MEMPOOL);

    CHECK(fCommitted == fMatchingMode);
    CHECK(fOwnerVisible == fMatchingMode);
    CHECK(fMempoolVisible == fMatchingMode);
    CHECK_FALSE(LLD::Contract->ReadContract(key, hashRead, TAO::Ledger::FLAGS::BLOCK));

    LLD::TransactionGuard next(TAO::Ledger::FLAGS::MEMPOOL, LLD::INSTANCES::CONTRACT);
    REQUIRE(next);
    REQUIRE(LLD::TxnAbort(TAO::Ledger::FLAGS::MEMPOOL, LLD::INSTANCES::CONTRACT));
}


TEST_CASE("LLD::HasOpenTransaction covers every MERKLE database",
          "[lld][txncommit][merkle]")
{
    LogicalGuard logicalGuard;
    ClientGuard clientGuard;

    LLD::TxnBegin(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::LOGICAL);
    const bool fLogicalDetected =
        LLD::HasOpenTransaction(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::LOGICAL);
    LLD::TxnAbort(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::LOGICAL);

    LLD::TxnBegin(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::CLIENT);
    const bool fClientDetected =
        LLD::HasOpenTransaction(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::CLIENT);
    LLD::TxnAbort(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::CLIENT);

    REQUIRE(fLogicalDetected);
    REQUIRE(fClientDetected);
}


/* ===========================================================================
 * TEST 4 — MINER / SANITIZE early-return paths return true
 * ===========================================================================
 * The MINER and SANITIZE flag paths are intentional short-circuits (preventing
 * accidental commits) — not failures.  The global TxnCommit must return true
 * for these flags regardless of database state.
 */
TEST_CASE("LLD::TxnCommit returns true for MINER and SANITIZE flags",
          "[lld][txncommit]")
{
    SECTION("FLAGS::MINER returns true")
    {
        const bool fResult = LLD::TxnCommit(TAO::Ledger::FLAGS::MINER);
        REQUIRE(fResult);
    }

    SECTION("FLAGS::SANITIZE returns true")
    {
        const bool fResult = LLD::TxnCommit(TAO::Ledger::FLAGS::SANITIZE);
        REQUIRE(fResult);
    }
}


/* ===========================================================================
 * Real-code test infrastructure (Gap 2)
 * ===========================================================================
 * The three tests below call the actual BlockState::SetBest() implementation
 * and assert against real LLD disk state, real ChainState atomics, and the
 * real mempool singleton — not a simulation.
 *
 * Design note (Gap 1 — option (b) chosen):
 *   SectorDatabase<>::TxnBegin() discards any in-flight outer transaction
 *   (it does `delete pTransaction; pTransaction = new SectorTransaction()`),
 *   so adding an unconditional TxnBegin() inside SetBest() would clobber the
 *   vtx writes made by Accept() callers (call sites #1/#2) before Index() is
 *   reached.  Option (b) was therefore chosen: LLD::HasOpenTransaction() was
 *   added as a lightweight check so SetBest() opens its own TxnBegin only
 *   when the caller has not already done so, and calls TxnAbort() on every
 *   failure path so that the on-disk state is always rolled back cleanly
 *   regardless of who owns the transaction.
 * =========================================================================== */
namespace
{
    /* Lightweight guard that creates a temporary LedgerDB when the global
     * test suite has not yet initialised one (e.g. when running only the
     * [setbest_txn] tag in isolation). */
    struct RealCodeLedgerGuard
    {
        bool ownedLedger{false};

        RealCodeLedgerGuard()
        {
            config::fTestNet.store(true);
            config::mapArgs["-testnet"] = "1";

            if(!LLD::Ledger)
            {
                LLD::Ledger = new LLD::LedgerDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);
                ownedLedger = true;
            }

            TAO::Ledger::SetHardenCheckpointHook(
                [](const TAO::Ledger::BlockState&, bool* pfHardened)
                {
                    if(pfHardened)
                        *pfHardened = false;

                    return true;
                });
        }

        ~RealCodeLedgerGuard()
        {
            TAO::Ledger::SetHardenCheckpointHook({});

            if(ownedLedger)
            {
                delete LLD::Ledger;
                LLD::Ledger = nullptr;
            }
        }
    };


    /* RAII guard that saves all relevant ChainState atomics on construction and
     * restores them on destruction, so real-code tests do not pollute the global
     * chain state seen by other tests in the suite. */
    struct ChainStateGuard
    {
        TAO::Ledger::BlockState savedGenesis;
        TAO::Ledger::BlockState savedBest;
        uint1024_t              savedBestHash;
        uint32_t                savedBestHeight;
        uint64_t                savedBestTrust;
        uint1024_t              savedCheckpointHash;
        uint32_t                savedCheckpointHeight;
        uint32_t                savedBlockCounter;

        ChainStateGuard()
        : savedGenesis  (TAO::Ledger::ChainState::tStateGenesis)
        , savedBest     (TAO::Ledger::ChainState::tStateBest.load())
        , savedBestHash (TAO::Ledger::ChainState::hashBestChain.load())
        , savedBestHeight(TAO::Ledger::ChainState::nBestHeight.load())
        , savedBestTrust(TAO::Ledger::ChainState::nBestChainTrust.load())
        , savedCheckpointHash(TAO::Ledger::ChainState::hashCheckpoint.load())
        , savedCheckpointHeight(TAO::Ledger::ChainState::nCheckpointHeight.load())
        , savedBlockCounter(TAO::API::nBlockCounter.load())
        {}

        ~ChainStateGuard()
        {
            TAO::Ledger::ChainState::tStateGenesis   = savedGenesis;
            TAO::Ledger::ChainState::tStateBest      = savedBest;
            TAO::Ledger::ChainState::hashBestChain   = savedBestHash;
            TAO::Ledger::ChainState::nBestHeight     .store(savedBestHeight);
            TAO::Ledger::ChainState::nBestChainTrust .store(savedBestTrust);
            TAO::Ledger::ChainState::hashCheckpoint  = savedCheckpointHash;
            TAO::Ledger::ChainState::nCheckpointHeight.store(savedCheckpointHeight);
            TAO::API::nBlockCounter.store(savedBlockCounter);
        }
    };


    struct ShutdownGuard
    {
        const bool savedShutdown{config::fShutdown.load()};

        ~ShutdownGuard()
        {
            config::fShutdown.store(savedShutdown);
        }
    };


    struct BestChainDiskGuard
    {
        bool hadBest{false};
        uint1024_t hashBest;

        BestChainDiskGuard()
        : hadBest(LLD::Ledger->Read(std::string("hashbestchain"), hashBest))
        {
        }

        ~BestChainDiskGuard()
        {
            if(hadBest)
                LLD::Ledger->Write(std::string("hashbestchain"), hashBest);
            else
                LLD::Ledger->Erase(std::string("hashbestchain"));
        }
    };


    struct GenesisDiskGuard
    {
        const uint1024_t hash = TAO::Ledger::ChainState::Genesis();
        TAO::Ledger::BlockState saved;
        const bool hadGenesis = LLD::Ledger->ReadBlock(hash, saved);

        ~GenesisDiskGuard()
        {
            if(hadGenesis)
                LLD::Ledger->WriteBlock(hash, saved);
            else
                LLD::Ledger->EraseBlock(hash);
        }
    };


    struct ArgsMapGuard
    {
        const std::map<std::string, std::string> saved = config::mapArgs;
        ~ArgsMapGuard()
        {
            config::mapArgs = saved;
        }
    };


    struct FlagGuard
    {
        const bool savedClient  = config::fClient.load();
        const bool savedHybrid  = config::fHybrid.load();
        const bool savedTestNet = config::fTestNet.load();

        ~FlagGuard()
        {
            config::fClient.store(savedClient);
            config::fHybrid.store(savedHybrid);
            config::fTestNet.store(savedTestNet);
        }
    };


    struct CheckpointChainFixture
    {
        TAO::Ledger::BlockState genesis;
        TAO::Ledger::BlockState one;
        TAO::Ledger::BlockState two;
        TAO::Ledger::BlockState three;
        uint1024_t hashGenesis;
        uint1024_t hashOne;
        uint1024_t hashTwo;
        uint1024_t hashThree;
    };

    struct CheckpointBlocksDiskGuard
    {
        std::vector<uint1024_t> hashes;

        ~CheckpointBlocksDiskGuard()
        {
            for(const auto& hash : hashes)
                LLD::Ledger->EraseBlock(hash);
        }
    };


    struct CheckpointRepairHookGuard
    {
        ~CheckpointRepairHookGuard()
        {
            TAO::Ledger::ChainState::SetCheckpointRepairSetBestHook({});
        }
    };


    CheckpointChainFixture BuildCheckpointChainFixture(const uint64_t nNonceBase,
                                                       CheckpointBlocksDiskGuard& blocksGuard)
    {
        CheckpointChainFixture fixture;

        fixture.genesis = TAO::Ledger::ChainState::tStateGenesis;
        REQUIRE_FALSE(fixture.genesis.IsNull());
        fixture.genesis.hashPrevBlock = 0;
        fixture.genesis.hashNextBlock = 0;
        fixture.hashGenesis = fixture.genesis.GetHash();

        fixture.one = fixture.genesis;
        fixture.one.hashPrevBlock = fixture.hashGenesis;
        fixture.one.hashNextBlock = 0;
        fixture.one.nHeight = 1;
        fixture.one.nNonce = nNonceBase + 1;
        fixture.one.nTime = nNonceBase + 1;
        fixture.one.nChainTrust = fixture.genesis.nChainTrust + 10;
        fixture.hashOne = fixture.one.GetHash();

        fixture.two = fixture.one;
        fixture.two.hashPrevBlock = fixture.hashOne;
        fixture.two.hashNextBlock = 0;
        fixture.two.nHeight = 2;
        fixture.two.nNonce = nNonceBase + 2;
        fixture.two.nTime = nNonceBase + 2;
        fixture.two.nChainTrust = fixture.one.nChainTrust + 10;
        fixture.hashTwo = fixture.two.GetHash();

        fixture.three = fixture.two;
        fixture.three.hashPrevBlock = fixture.hashTwo;
        fixture.three.hashNextBlock = 0;
        fixture.three.nHeight = 3;
        fixture.three.nNonce = nNonceBase + 3;
        fixture.three.nTime = nNonceBase + 3;
        fixture.three.nChainTrust = fixture.two.nChainTrust + 10;
        fixture.hashThree = fixture.three.GetHash();

        fixture.genesis.hashPrevBlock = 0;
        fixture.genesis.hashNextBlock = fixture.hashOne;
        fixture.one.hashNextBlock = fixture.hashTwo;
        fixture.two.hashNextBlock = fixture.hashThree;
        fixture.three.hashNextBlock = 0;

        blocksGuard.hashes.insert(blocksGuard.hashes.end(),
            {fixture.hashOne, fixture.hashTwo, fixture.hashThree});

        REQUIRE(LLD::Ledger->WriteBlock(fixture.hashGenesis, fixture.genesis));
        REQUIRE(LLD::Ledger->WriteBlock(fixture.hashOne, fixture.one));
        REQUIRE(LLD::Ledger->WriteBlock(fixture.hashTwo, fixture.two));
        REQUIRE(LLD::Ledger->WriteBlock(fixture.hashThree, fixture.three));
        REQUIRE(LLD::Ledger->WriteBestChain(fixture.hashThree));

        TAO::Ledger::ChainState::tStateGenesis = fixture.genesis;
        TAO::Ledger::ChainState::tStateBest = fixture.three;
        TAO::Ledger::ChainState::hashBestChain = fixture.hashThree;
        TAO::Ledger::ChainState::nBestHeight = fixture.three.nHeight;
        TAO::Ledger::ChainState::nBestChainTrust = fixture.three.nChainTrust;
        TAO::Ledger::ChainState::hashCheckpoint = 0;
        TAO::Ledger::ChainState::nCheckpointHeight = 0;

        return fixture;
    }

} /* anonymous namespace */


/* ===========================================================================
 * TEST 5 — Real SetBest() with Connect() failure rolls back disk index
 * ===========================================================================
 * Calls the actual BlockState::SetBest().  The candidate block's vtx contains
 * a transaction hash that is NOT on disk, so Connect() fails mid-loop.  With
 * the Gap-1 fix, SetBest() calls TxnAbort() before returning false, rolling
 * back the IndexBlock write that Connect() made before detecting the missing
 * transaction.  Asserts: (a) SetBest() returns false, (b) no index entry was
 * committed to disk (TxnAbort rolled it back), (c) ChainState atomics are
 * unchanged.
 */
TEST_CASE("Real SetBest(): Connect() failure rolls back disk index and leaves ChainState unchanged",
          "[ledger][setbest_txn][real]")
{
    RealCodeLedgerGuard ledgerGuard;
    ChainStateGuard     chainGuard;
    BestChainDiskGuard  bestChainGuard;

    /* ---- Minimal genesis block written to disk ---- */
    TAO::Ledger::BlockState genesis;
    genesis.nVersion      = 4;
    genesis.hashPrevBlock = uint1024_t(0);
    genesis.nChannel      = 2;
    genesis.nHeight       = 0;
    genesis.nBits         = 1;
    genesis.nNonce        = 77;

    const uint1024_t hashGenesis = genesis.GetHash();
    REQUIRE(LLD::Ledger->WriteBlock(hashGenesis, genesis));

    /* Set chain state so SetBest() enters the main chain-transition branch */
    TAO::Ledger::ChainState::tStateGenesis   = genesis;
    TAO::Ledger::ChainState::tStateBest      = genesis;
    TAO::Ledger::ChainState::hashBestChain   = hashGenesis;
    TAO::Ledger::ChainState::nBestHeight     .store(0);
    TAO::Ledger::ChainState::nBestChainTrust .store(genesis.nChainTrust);

    /* ---- Candidate block whose Connect() will fail ----
     * vtx contains a transaction hash that is NOT on disk.  Inside Connect()
     * the call sequence is:
     *   HasIndex(fakeTxHash)          → false  (first time)
     *   IndexBlock(fakeTxHash, ...)   → writes pending index to pTransaction
     *   ReadTx(fakeTxHash, ...)       → false  (tx body missing)
     *   Connect() returns false
     * SetBest() then calls TxnAbort(), discarding the pending IndexBlock write. */
    const uint512_t fakeTxHash(0xdeadbeefcafeULL);

    TAO::Ledger::BlockState badBlock;
    badBlock.nVersion      = 4;
    badBlock.hashPrevBlock = hashGenesis;
    badBlock.nChannel      = 2;
    badBlock.nHeight       = 1;
    badBlock.nBits         = 1;
    badBlock.nNonce        = 55;
    badBlock.vtx.push_back({TAO::Ledger::TRANSACTION::TRITIUM, fakeTxHash});

    /* Confirm the fake tx is absent from disk before the call */
    TAO::Ledger::Transaction dummyTx;
    REQUIRE_FALSE(LLD::Ledger->ReadTx(fakeTxHash, dummyTx));

    /* ---- Invoke real SetBest() ---- */
    REQUIRE_FALSE(badBlock.SetBest());

    /* ---- (a) ChainState atomics must be unchanged ---- */
    REQUIRE(TAO::Ledger::ChainState::tStateBest.load().GetHash() == hashGenesis);
    REQUIRE(TAO::Ledger::ChainState::hashBestChain.load()        == hashGenesis);
    REQUIRE(TAO::Ledger::ChainState::nBestHeight.load()          == 0u);

    /* ---- (b) Disk index for fakeTxHash must have been rolled back ----
     * TxnAbort() deleted pTransaction before it could be flushed to disk.
     * HasIndex() checks the on-disk keychain only (pTransaction is null),
     * so a false result confirms the write was discarded. */
    REQUIRE_FALSE(LLD::Ledger->HasIndex(fakeTxHash));

    /* ---- (c) No active transaction remains open ---- */
    REQUIRE_FALSE(LLD::HasOpenTransaction(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::CONSENSUS));

    /* Cleanup */
    LLD::Ledger->EraseBlock(hashGenesis);
}


/* ===========================================================================
 * TEST 6 — Real call-site #4 pattern: outer TxnBegin + SetBest() failure
 * ===========================================================================
 * Mirrors the call-site #4 pattern in chainstate.cpp:
 *   LLD::TxnBegin();
 *   if(!state.SetBest()) { LLD::TxnAbort(); }
 *   else                   LLD::TxnCommit();
 *
 * With the Gap-1 fix SetBest() internally calls TxnAbort() before returning
 * false, so the outer TxnAbort() becomes a safe no-op.  This test verifies:
 *   (a) SetBest() returns false,
 *   (b) no index entry persists after the outer TxnAbort(),
 *   (c) no active transaction remains open.
 */
TEST_CASE("Real call-site #4: outer TxnBegin + SetBest() failure → clean abort, no partial commit",
          "[ledger][chainstate][setbest_txn][real]")
{
    RealCodeLedgerGuard ledgerGuard;
    ChainStateGuard     chainGuard;

    /* ---- Minimal genesis ---- */
    TAO::Ledger::BlockState genesis;
    genesis.nVersion      = 4;
    genesis.hashPrevBlock = uint1024_t(0);
    genesis.nChannel      = 2;
    genesis.nHeight       = 0;
    genesis.nBits         = 1;
    genesis.nNonce        = 88;

    const uint1024_t hashGenesis = genesis.GetHash();
    REQUIRE(LLD::Ledger->WriteBlock(hashGenesis, genesis));

    TAO::Ledger::ChainState::tStateGenesis   = genesis;
    TAO::Ledger::ChainState::tStateBest      = genesis;
    TAO::Ledger::ChainState::hashBestChain   = hashGenesis;
    TAO::Ledger::ChainState::nBestHeight     .store(0);
    TAO::Ledger::ChainState::nBestChainTrust .store(genesis.nChainTrust);

    /* ---- Candidate block whose Connect() will fail ---- */
    const uint512_t fakeTxHash(0xbeefdead1234ULL);

    TAO::Ledger::BlockState badBlock;
    badBlock.nVersion      = 4;
    badBlock.hashPrevBlock = hashGenesis;
    badBlock.nChannel      = 2;
    badBlock.nHeight       = 1;
    badBlock.nBits         = 1;
    badBlock.nNonce        = 33;
    badBlock.vtx.push_back({TAO::Ledger::TRANSACTION::TRITIUM, fakeTxHash});

    /* ---- Call-site #4 pattern ---- */
    LLD::TxnBegin();                       /* outer TxnBegin (as chainstate.cpp does) */
    const bool fOk = badBlock.SetBest();   /* internally calls TxnAbort on failure    */
    if(!fOk)
        LLD::TxnAbort();                   /* outer TxnAbort — safe no-op after Gap-1 */
    else
        LLD::TxnCommit();

    /* (a) SetBest() must have returned false */
    REQUIRE_FALSE(fOk);

    /* (b) The IndexBlock write from Connect() must not have been committed */
    REQUIRE_FALSE(LLD::Ledger->HasIndex(fakeTxHash));

    /* (c) No active transaction may remain open */
    REQUIRE_FALSE(LLD::HasOpenTransaction(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::CONSENSUS));

    /* Cleanup */
    LLD::Ledger->EraseBlock(hashGenesis);
}


/* ===========================================================================
 * TEST 7 — Real mempool: size unchanged after failed SetBest()
 * ===========================================================================
 * Verifies that mempool.Accept() / mempool.Remove() are never reached when
 * the disk phase of SetBest() fails.  Since the mempool mutations happen only
 * after TxnCommit (which is never reached on failure), the mempool size must
 * be identical before and after a failing SetBest() call.
 */
TEST_CASE("Real SetBest(): mempool size is unchanged after disk-phase failure",
          "[ledger][setbest_txn][real]")
{
    RealCodeLedgerGuard ledgerGuard;
    ChainStateGuard     chainGuard;

    /* ---- Minimal genesis ---- */
    TAO::Ledger::BlockState genesis;
    genesis.nVersion      = 4;
    genesis.hashPrevBlock = uint1024_t(0);
    genesis.nChannel      = 2;
    genesis.nHeight       = 0;
    genesis.nBits         = 1;
    genesis.nNonce        = 66;

    const uint1024_t hashGenesis = genesis.GetHash();
    REQUIRE(LLD::Ledger->WriteBlock(hashGenesis, genesis));

    TAO::Ledger::ChainState::tStateGenesis   = genesis;
    TAO::Ledger::ChainState::tStateBest      = genesis;
    TAO::Ledger::ChainState::hashBestChain   = hashGenesis;
    TAO::Ledger::ChainState::nBestHeight     .store(0);
    TAO::Ledger::ChainState::nBestChainTrust .store(genesis.nChainTrust);

    /* Snapshot mempool size before the attempt */
    const uint32_t nMempoolBefore = TAO::Ledger::mempool.Size();

    /* ---- Candidate block whose Connect() will fail ---- */
    const uint512_t fakeTxHash(0xcafebabe9876ULL);

    TAO::Ledger::BlockState badBlock;
    badBlock.nVersion      = 4;
    badBlock.hashPrevBlock = hashGenesis;
    badBlock.nChannel      = 2;
    badBlock.nHeight       = 1;
    badBlock.nBits         = 1;
    badBlock.nNonce        = 44;
    badBlock.vtx.push_back({TAO::Ledger::TRANSACTION::TRITIUM, fakeTxHash});

    REQUIRE_FALSE(badBlock.SetBest());

    /* Mempool must be identical to pre-attempt state */
    REQUIRE(TAO::Ledger::mempool.Size() == nMempoolBefore);

    /* Cleanup */
    LLD::Ledger->EraseBlock(hashGenesis);
}


/* ===========================================================================
 * TEST 8 — Case A regression: outer TxnBegin + SetBest() success →
 *           HasOpenTransaction() false, spurious TxnCommit() returns false
 * ===========================================================================
 * This is the EXACT production bug: Accept() opens an outer TxnBegin, writes
 * vtx, calls Index() which calls SetBest() internally.  SetBest() succeeds and
 * commits the transaction itself (leaving pTransaction null).  The outer
 * TxnCommit() in Accept() then returns false — which, before the fix, was
 * misinterpreted as a hard commit failure and caused Accept() to return false
 * on every single best-chain block.
 *
 * After the fix: Accept() checks HasOpenTransaction() before calling TxnCommit.
 * When SetBest() has already committed (HasOpenTransaction() == false), Accept()
 * skips the outer TxnCommit() and returns true.
 *
 * This test directly validates that the fix is correct:
 *  (a) SetBest() with an outer transaction open returns true.
 *  (b) HasOpenTransaction() is false after SetBest() succeeds.
 *  (c) A subsequent TxnCommit() returns false (no active transaction).
 *  (d) ChainState advanced to the candidate block.
 */
TEST_CASE("Accept() Case A: outer TxnBegin + SetBest() commits internally, HasOpenTransaction false",
          "[ledger][accept_txn][real]")
{
    RealCodeLedgerGuard ledgerGuard;
    ChainStateGuard     chainGuard;
    BestChainDiskGuard  bestChainGuard;

    /* ---- Minimal genesis (nNonce distinct from earlier tests in this file to avoid hash collisions) ---- */
    TAO::Ledger::BlockState genesis;
    genesis.nVersion      = 4;
    genesis.hashPrevBlock = uint1024_t(0);
    genesis.nChannel      = 2;
    genesis.nHeight       = 0;
    genesis.nBits         = 1;
    genesis.nNonce        = 1001;
    genesis.nChainTrust   = 0; /* explicitly 0 so the heavier-than relationship is clear */
    genesis.nMoneySupply  = 1000;
    genesis.nFeesBurned   = 25;

    const uint1024_t hashGenesis = genesis.GetHash();
    REQUIRE(LLD::Ledger->WriteBlock(hashGenesis, genesis));

    TAO::Ledger::ChainState::tStateGenesis   = genesis;
    TAO::Ledger::ChainState::tStateBest      = genesis;
    TAO::Ledger::ChainState::hashBestChain   = hashGenesis;
    TAO::Ledger::ChainState::nBestHeight     .store(0);
    TAO::Ledger::ChainState::nBestChainTrust .store(genesis.nChainTrust);

    /* ---- Candidate block: height 1, empty vtx → Connect() succeeds trivially ---- */
    TAO::Ledger::BlockState candidate;
    candidate.nVersion      = 4;
    candidate.hashPrevBlock = hashGenesis;
    candidate.nChannel      = 2;
    candidate.nHeight       = 1;
    candidate.nBits         = 1;
    candidate.nNonce        = 1002;
    candidate.nChainTrust   = 1; /* heavier than genesis (nChainTrust 1 > 0) for IsHeavierThan */
    candidate.nMint         = 100;

    const uint1024_t hashCandidate = candidate.GetHash();

    /* ---- Simulate Accept()'s outer TxnBegin ---- */
    LLD::TxnBegin();

    /* Confirm outer transaction is open before SetBest() */
    REQUIRE(LLD::HasOpenTransaction(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::CONSENSUS));

    /* ---- Call SetBest() directly (mirrors what Index() → ActivateCandidateBestChain does) ---- */
    const bool fSetBestOk = candidate.SetBest();
    REQUIRE(fSetBestOk); /* (a) SetBest() must succeed */

    /* ---- (b) HasOpenTransaction() must be false: SetBest() committed internally ---- */
    REQUIRE_FALSE(LLD::HasOpenTransaction(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::CONSENSUS));

    /* ---- (c) A subsequent TxnCommit() returns false (no active transaction).
     * Before the fix this false was misinterpreted as a failure in Accept(),
     * causing every best-chain block acceptance to return false.
     * After the fix the HasOpenTransaction() guard prevents this call entirely. ---- */
    const bool fRedundantCommit = LLD::TxnCommit();
    REQUIRE_FALSE(fRedundantCommit); /* expected: no transaction was open; NOT an error */

    /* ---- (d) ChainState must have advanced to the candidate ---- */
    REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == hashCandidate);
    REQUIRE(TAO::Ledger::ChainState::nBestHeight.load()   == 1u);

    uint1024_t hashBestOnDisk;
    REQUIRE(LLD::Ledger->ReadBestChain(hashBestOnDisk));
    REQUIRE(hashBestOnDisk == hashCandidate);

    TAO::Ledger::BlockState committed;
    REQUIRE(LLD::Ledger->ReadBlock(hashCandidate, committed));
    REQUIRE(committed.nMoneySupply == 1100);
    REQUIRE(committed.nFeesBurned == 25);
    REQUIRE(candidate.nMoneySupply == committed.nMoneySupply);
    REQUIRE(candidate.nFeesBurned == committed.nFeesBurned);
    REQUIRE(TAO::Ledger::ChainState::tStateBest.load().nMoneySupply == committed.nMoneySupply);
    REQUIRE(TAO::Ledger::ChainState::tStateBest.load().nFeesBurned == committed.nFeesBurned);

    /* Cleanup */
    LLD::Ledger->EraseBlock(hashCandidate);
    LLD::Ledger->EraseBlock(hashGenesis);
}


TEST_CASE("Real SetBest(): rewind publishes the updated tip without its disconnected successor",
          "[ledger][setbest_txn][real]")
{
    RealCodeLedgerGuard ledgerGuard;
    ChainStateGuard     chainGuard;
    BestChainDiskGuard  bestChainGuard;
    GenesisDiskGuard    genesisDiskGuard;

    const TAO::Ledger::BlockState genesis = TAO::Ledger::ChainState::tStateGenesis;
    const uint1024_t hashGenesis = genesis.GetHash();
    REQUIRE(LLD::Ledger->WriteBlock(hashGenesis, genesis));
    TAO::Ledger::ChainState::tStateBest = genesis;
    TAO::Ledger::ChainState::hashBestChain = hashGenesis;
    TAO::Ledger::ChainState::nBestHeight = genesis.nHeight;
    TAO::Ledger::ChainState::nBestChainTrust = genesis.nChainTrust;

    TAO::Ledger::BlockState first;
    first.nVersion = 4;
    first.hashPrevBlock = hashGenesis;
    first.nChannel = 2;
    first.nHeight = genesis.nHeight + 1;
    first.nBits = 1;
    first.nNonce = 2201;
    first.nChainTrust = genesis.nChainTrust + 1;
    first.nMint = 100;
    REQUIRE(first.SetBest());

    TAO::Ledger::BlockState second = first;
    second.hashPrevBlock = first.GetHash();
    second.nHeight++;
    second.nNonce++;
    second.nChainTrust++;
    REQUIRE(second.SetBest());

    REQUIRE(LLD::Ledger->ReadBlock(first.GetHash(), first));
    REQUIRE(first.hashNextBlock == second.GetHash());
    REQUIRE(first.SetBest());

    TAO::Ledger::BlockState committed;
    REQUIRE(LLD::Ledger->ReadBlock(first.GetHash(), committed));
    REQUIRE(committed.hashNextBlock == 0);
    REQUIRE(first.hashNextBlock == 0);
    REQUIRE(TAO::Ledger::ChainState::tStateBest.load().hashNextBlock == 0);
    REQUIRE(TAO::Ledger::ChainState::tStateBest.load().nMoneySupply == committed.nMoneySupply);
    REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == first.GetHash());
    REQUIRE(TAO::Ledger::ChainState::nBestHeight.load() == first.nHeight);
    REQUIRE_FALSE(LLD::Ledger->HasBlock(second.GetHash()));
    REQUIRE_FALSE(TAO::Ledger::ChainState::fChainReorg.load());

    uint1024_t hashBestOnDisk;
    REQUIRE(LLD::Ledger->ReadBestChain(hashBestOnDisk));
    REQUIRE(hashBestOnDisk == first.GetHash());

    LLD::Ledger->EraseBlock(first.GetHash());
}


TEST_CASE("Real SetBest(): debugreorg reads transactions before rewind erases them",
          "[ledger][setbest_txn][real]")
{
    RealCodeLedgerGuard ledgerGuard;
    ChainStateGuard     chainGuard;
    BestChainDiskGuard  bestChainGuard;
    GenesisDiskGuard    genesisDiskGuard;
    struct ArgsGuard
    {
        const std::map<std::string, std::string> saved = config::mapArgs;
        ~ArgsGuard() { config::mapArgs = saved; }
    } argsGuard;

    SECTION("diagnostics disabled")
    {
        config::mapArgs["-debugreorg"] = "0";
    }
    SECTION("diagnostics enabled")
    {
        config::mapArgs["-debugreorg"] = "1";
    }

    TAO::Ledger::BlockState genesis = TAO::Ledger::ChainState::tStateGenesis;
    const uint1024_t hashGenesis = genesis.GetHash();

    TAO::Ledger::Transaction firstTx;
    firstTx.nSequence = 1;
    firstTx.hashGenesis = uint256_t(0x2203);
    TAO::Ledger::Transaction secondTx = firstTx;
    secondTx.nSequence = 2;
    secondTx.hashPrevTx = firstTx.GetHash();
    const uint512_t hashFirst = firstTx.GetHash();
    const uint512_t hashSecond = secondTx.GetHash();
    REQUIRE(LLD::Ledger->WriteTx(hashFirst, firstTx));
    REQUIRE(LLD::Ledger->WriteTx(hashSecond, secondTx));
    REQUIRE(LLD::Ledger->WriteLast(firstTx.hashGenesis, hashSecond));

    TAO::Ledger::BlockState tip;
    tip.nVersion = 4;
    tip.hashPrevBlock = hashGenesis;
    tip.nChannel = 2;
    tip.nHeight = genesis.nHeight + 1;
    tip.nBits = 1;
    tip.nNonce = 2203;
    tip.nChainTrust = genesis.nChainTrust + 1;
    tip.vtx = {{TAO::Ledger::TRANSACTION::TRITIUM, hashFirst},
               {TAO::Ledger::TRANSACTION::TRITIUM, hashSecond}};
    const uint1024_t hashTip = tip.GetHash();
    genesis.hashNextBlock = hashTip;
    REQUIRE(LLD::Ledger->WriteBlock(hashGenesis, genesis));
    REQUIRE(LLD::Ledger->WriteBlock(hashTip, tip));
    REQUIRE(LLD::Ledger->IndexBlock(hashFirst, hashTip));
    REQUIRE(LLD::Ledger->IndexBlock(hashSecond, hashTip));
    REQUIRE(LLD::Ledger->WriteBestChain(hashTip));
    TAO::Ledger::ChainState::tStateBest = tip;
    TAO::Ledger::ChainState::hashBestChain = hashTip;
    TAO::Ledger::ChainState::nBestHeight = tip.nHeight;
    TAO::Ledger::ChainState::nBestChainTrust = tip.nChainTrust;

    REQUIRE(genesis.SetBest());
    REQUIRE_FALSE(LLD::Ledger->HasBlock(hashTip));
    REQUIRE_FALSE(LLD::Ledger->HasTx(hashFirst));
    REQUIRE_FALSE(LLD::Ledger->HasTx(hashSecond));
    REQUIRE_FALSE(LLD::Ledger->HasIndex(hashFirst));
    REQUIRE_FALSE(LLD::Ledger->HasIndex(hashSecond));
    REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == hashGenesis);
    REQUIRE_FALSE(LLD::HasOpenTransaction());
    REQUIRE_FALSE(TAO::Ledger::ChainState::fChainReorg.load());
    LLD::Ledger->EraseLast(firstTx.hashGenesis);
}


TEST_CASE("Real SetBest(): checkpoint hardening runs after commit and gates best-tip publication",
          "[ledger][setbest_txn][checkpoint][real]")
{
    RealCodeLedgerGuard ledgerGuard;
    ChainStateGuard     chainGuard;
    BestChainDiskGuard  bestChainGuard;
    ShutdownGuard       shutdownGuard;

    config::fShutdown.store(false);

    TAO::Ledger::BlockState genesis =
        TAO::Ledger::ChainState::tStateGenesis;
    const uint1024_t hashGenesis = genesis.GetHash();
    REQUIRE(hashGenesis == TAO::Ledger::ChainState::Genesis());
    REQUIRE(LLD::Ledger->WriteBlock(hashGenesis, genesis));
    REQUIRE(LLD::Ledger->WriteBestChain(hashGenesis));

    TAO::Ledger::ChainState::tStateGenesis      = genesis;
    TAO::Ledger::ChainState::tStateBest         = genesis;
    TAO::Ledger::ChainState::hashBestChain      = hashGenesis;
    TAO::Ledger::ChainState::nBestHeight        = genesis.nHeight;
    TAO::Ledger::ChainState::nBestChainTrust    = genesis.nChainTrust;
    TAO::Ledger::ChainState::hashCheckpoint     = uint1024_t(0x1234);
    TAO::Ledger::ChainState::nCheckpointHeight  = 0;

    TAO::Ledger::BlockState candidate;
    candidate.nVersion      = 4;
    candidate.hashPrevBlock = hashGenesis;
    candidate.nChannel      = 2;
    candidate.nHeight       = genesis.nHeight + 1;
    candidate.nTime         = genesis.nTime + 1;
    candidate.nBits         = 1;
    candidate.nNonce        = 2001;
    candidate.nChainTrust   = genesis.nChainTrust + 1;

    const uint1024_t hashCandidate = candidate.GetHash();
    bool fHookCalledAfterCommit = false;
    bool fBestUnpublishedAtHook = false;

    SECTION("successful hardening publishes the checkpoint before the best tip")
    {
        TAO::Ledger::SetHardenCheckpointHook(
            [&](const TAO::Ledger::BlockState& state, bool* pfHardened)
            {
                uint1024_t hashBestOnDisk;
                fHookCalledAfterCommit =
                    !LLD::HasOpenTransaction(
                        TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::CONSENSUS)
                    && LLD::Ledger->ReadBestChain(hashBestOnDisk)
                    && hashBestOnDisk == hashCandidate;
                fBestUnpublishedAtHook =
                    TAO::Ledger::ChainState::hashBestChain.load() == hashGenesis;

                TAO::Ledger::ChainState::nCheckpointHeight = state.nHeight;
                TAO::Ledger::ChainState::hashCheckpoint = state.hashCheckpoint;
                *pfHardened = true;
                return true;
            });

        REQUIRE(candidate.SetBest());
        REQUIRE(fHookCalledAfterCommit);
        REQUIRE(fBestUnpublishedAtHook);
        REQUIRE(TAO::Ledger::ChainState::hashCheckpoint.load() == genesis.hashCheckpoint);
        REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == hashCandidate);
        REQUIRE_FALSE(config::fShutdown.load());
    }

    SECTION("hardening read failure requests shutdown without publishing the best tip")
    {
        const uint1024_t hashCheckpointBefore =
            TAO::Ledger::ChainState::hashCheckpoint.load();

        TAO::Ledger::SetHardenCheckpointHook(
            [&](const TAO::Ledger::BlockState&, bool*)
            {
                uint1024_t hashBestOnDisk;
                fHookCalledAfterCommit =
                    !LLD::HasOpenTransaction(
                        TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::CONSENSUS)
                    && LLD::Ledger->ReadBestChain(hashBestOnDisk)
                    && hashBestOnDisk == hashCandidate;
                fBestUnpublishedAtHook =
                    TAO::Ledger::ChainState::hashBestChain.load() == hashGenesis;
                return false;
            });

        REQUIRE_FALSE(candidate.SetBest());
        REQUIRE(fHookCalledAfterCommit);
        REQUIRE(fBestUnpublishedAtHook);
        REQUIRE(config::fShutdown.load());
        REQUIRE(TAO::Ledger::ChainState::hashCheckpoint.load() == hashCheckpointBefore);
        REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == hashGenesis);

        uint1024_t hashBestOnDisk;
        REQUIRE(LLD::Ledger->ReadBestChain(hashBestOnDisk));
        REQUIRE(hashBestOnDisk == hashCandidate);
    }

    LLD::Ledger->EraseBlock(hashCandidate);
}


TEST_CASE("Real SetBest(): multi-block reorg evaluates checkpoint candidates in order",
          "[ledger][setbest_txn][checkpoint][real]")
{
    RealCodeLedgerGuard ledgerGuard;
    ChainStateGuard     chainGuard;
    BestChainDiskGuard  bestChainGuard;
    ShutdownGuard       shutdownGuard;

    config::fShutdown.store(false);

    TAO::Ledger::BlockState genesis =
        TAO::Ledger::ChainState::tStateGenesis;
    const uint1024_t hashGenesis = genesis.GetHash();
    REQUIRE(LLD::Ledger->WriteBlock(hashGenesis, genesis));
    REQUIRE(LLD::Ledger->WriteBestChain(hashGenesis));

    TAO::Ledger::ChainState::tStateGenesis      = genesis;
    TAO::Ledger::ChainState::tStateBest         = genesis;
    TAO::Ledger::ChainState::hashBestChain      = hashGenesis;
    TAO::Ledger::ChainState::nBestHeight        = genesis.nHeight;
    TAO::Ledger::ChainState::nBestChainTrust    = genesis.nChainTrust;

    TAO::Ledger::BlockState first;
    first.nVersion      = 4;
    first.hashPrevBlock = hashGenesis;
    first.nChannel      = 2;
    first.nHeight       = genesis.nHeight + 1;
    first.nTime         = genesis.nTime + 1;
    first.nBits         = 1;
    first.nNonce        = 2101;
    first.nChainTrust   = genesis.nChainTrust + 1;
    const uint1024_t hashFirst = first.GetHash();
    REQUIRE(LLD::Ledger->WriteBlock(hashFirst, first));

    TAO::Ledger::BlockState second;
    second.nVersion      = 4;
    second.hashPrevBlock = hashFirst;
    second.nChannel      = 2;
    second.nHeight       = first.nHeight + 1;
    second.nTime         = first.nTime + 1;
    second.nBits         = 1;
    second.nNonce        = 2102;
    second.nChainTrust   = first.nChainTrust + 1;

    std::vector<uint1024_t> vCandidates;
    TAO::Ledger::SetHardenCheckpointHook(
        [&](const TAO::Ledger::BlockState& state, bool* pfHardened)
        {
            vCandidates.push_back(state.GetHash());
            *pfHardened = false;
            return true;
        });

    REQUIRE(second.SetBest());
    const std::vector<uint1024_t> vExpected{hashGenesis, hashFirst};
    REQUIRE(vCandidates == vExpected);
    REQUIRE_FALSE(config::fShutdown.load());

    LLD::Ledger->EraseBlock(second.GetHash());
    LLD::Ledger->EraseBlock(hashFirst);
}


/* ===========================================================================
 * TEST 9 — Case B: outer TxnBegin without SetBest() → HasOpenTransaction true
 *           → outer TxnCommit() is needed and succeeds
 * ===========================================================================
 * Verifies the Case B path from Accept(): block was accepted by Index() but
 * did NOT become the new best chain (IsHeavierThan was false, so SetBest was
 * never called).  The outer transaction is still open; the HasOpenTransaction()
 * guard correctly detects this and calls TxnCommit(), which succeeds.
 *
 * This must not regress: genuine commit failures in Case B (outer transaction
 * still open but TxnCommit fails) must still be surfaced as false.
 */
TEST_CASE("Accept() Case B: outer TxnBegin without SetBest, HasOpenTransaction true, TxnCommit needed",
          "[ledger][accept_txn][real]")
{
    RealCodeLedgerGuard ledgerGuard;

    /* ---- Open an outer transaction (simulating Accept() when block is not heavier) ---- */
    LLD::TxnBegin();

    /* Confirm transaction is open before any commit */
    REQUIRE(LLD::HasOpenTransaction(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::CONSENSUS));

    /* ---- The fix: guard fires, outer TxnCommit() is called because transaction is open ---- */
    const bool fNeedsCommit = LLD::HasOpenTransaction();
    REQUIRE(fNeedsCommit); /* guard would proceed to call TxnCommit() */

    /* Commit the open (empty) transaction — must succeed */
    const bool fCommitOk = LLD::TxnCommit();
    REQUIRE(fCommitOk); /* (a) outer TxnCommit succeeds — not a false-positive */

    /* After commit, no transaction should remain open */
    REQUIRE_FALSE(LLD::HasOpenTransaction(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::CONSENSUS));
}


TEST_CASE("Client SetBest commits or rolls back the block, links, and best pointer atomically",
          "[ledger][client][setbest_txn][real]")
{
    ClientModeGuard modeGuard;
    ClientGuard clientGuard;
    LogicalGuard logicalGuard;
    ChainStateGuard chainGuard;
    BestChainDiskGuard ledgerBestGuard;

    const TAO::Ledger::ClientBlock genesis(TAO::Ledger::TritiumGenesis());
    const uint1024_t hashGenesis = genesis.GetHash();
    REQUIRE(hashGenesis == TAO::Ledger::ChainState::Genesis());

    TAO::Ledger::ClientBlock savedGenesis;
    const bool hadGenesis = LLD::Client->ReadBlock(hashGenesis, savedGenesis);
    uint1024_t savedBest;
    const bool hadBest = LLD::Client->ReadBestChain(savedBest);

    REQUIRE(LLD::Client->WriteBlock(hashGenesis, genesis));
    REQUIRE(LLD::Client->WriteBestChain(hashGenesis));

    TAO::Ledger::ChainState::tStateGenesis = genesis;
    TAO::Ledger::ChainState::tStateBest = genesis;
    TAO::Ledger::ChainState::hashBestChain = hashGenesis;
    TAO::Ledger::ChainState::nBestHeight.store(genesis.nHeight);

    TAO::Ledger::ClientBlock candidate(genesis);
    candidate.hashPrevBlock = hashGenesis;
    candidate.hashNextBlock = 0;
    candidate.nHeight = genesis.nHeight + 1;
    candidate.nTime = genesis.nTime + 1;
    candidate.nNonce++;
    candidate.nChannelWeight[0]++;
    const uint1024_t hashCandidate = candidate.GetHash();

    uint1024_t hashLedgerBest = hashCandidate;
    ++hashLedgerBest;
    REQUIRE(LLD::Ledger->Write(std::string("hashbestchain"), hashLedgerBest));

    SECTION("success commits every client-chain record before publication")
    {
        REQUIRE(candidate.Index());

        TAO::Ledger::ClientBlock committedCandidate;
        REQUIRE(LLD::Client->ReadBlock(hashCandidate, committedCandidate));
        REQUIRE(committedCandidate == candidate);

        TAO::Ledger::ClientBlock committedGenesis;
        REQUIRE(LLD::Client->ReadBlock(hashGenesis, committedGenesis));
        REQUIRE(committedGenesis.hashNextBlock == hashCandidate);

        uint1024_t hashBest;
        REQUIRE(LLD::Client->ReadBestChain(hashBest));
        REQUIRE(hashBest == hashCandidate);
        REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == hashCandidate);
    }

    SECTION("startup restores the committed client tip")
    {
        REQUIRE(candidate.Index());

        TAO::Ledger::ChainState::tStateGenesis = TAO::Ledger::BlockState();
        TAO::Ledger::ChainState::tStateBest = TAO::Ledger::BlockState();
        TAO::Ledger::ChainState::hashBestChain = 0;
        TAO::Ledger::ChainState::nBestHeight.store(0);
        TAO::Ledger::ChainState::nBestChainTrust.store(0);
        TAO::Ledger::ChainState::hashCheckpoint = 0;
        TAO::Ledger::ChainState::nCheckpointHeight.store(0);

        REQUIRE(TAO::Ledger::ChainState::Initialize());
        REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == hashCandidate);
        REQUIRE(TAO::Ledger::ChainState::tStateBest.load().GetHash() == hashCandidate);
        REQUIRE(TAO::Ledger::ChainState::nBestHeight.load() == candidate.nHeight);

        uint1024_t hashLedgerOnDisk;
        REQUIRE(LLD::Ledger->Read(std::string("hashbestchain"), hashLedgerOnDisk));
        REQUIRE(hashLedgerOnDisk == hashLedgerBest);
    }

    SECTION("startup recovery selects the last linked client block")
    {
        REQUIRE(candidate.Index());

        uint1024_t hashMissing = hashCandidate;
        ++hashMissing;
        REQUIRE(LLD::Client->WriteBestChain(hashMissing));

        TAO::Ledger::ChainState::tStateGenesis = TAO::Ledger::BlockState();
        TAO::Ledger::ChainState::tStateBest = TAO::Ledger::BlockState();
        TAO::Ledger::ChainState::hashBestChain = 0;
        TAO::Ledger::ChainState::nBestHeight.store(0);
        TAO::Ledger::ChainState::nBestChainTrust.store(0);
        TAO::Ledger::ChainState::hashCheckpoint = 0;
        TAO::Ledger::ChainState::nCheckpointHeight.store(0);

        REQUIRE(TAO::Ledger::ChainState::Initialize());
        REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == hashCandidate);
        REQUIRE(TAO::Ledger::ChainState::tStateBest.load().GetHash() == hashCandidate);
        REQUIRE(TAO::Ledger::ChainState::nBestHeight.load() == candidate.nHeight);

        uint1024_t hashRecovered;
        REQUIRE(LLD::Client->ReadBestChain(hashRecovered));
        REQUIRE(hashRecovered == hashCandidate);

        uint1024_t hashLedgerOnDisk;
        REQUIRE(LLD::Ledger->Read(std::string("hashbestchain"), hashLedgerOnDisk));
        REQUIRE(hashLedgerOnDisk == hashLedgerBest);
    }

    SECTION("checkpoint failure rolls back every record and leaves ChainState unpublished")
    {
        REQUIRE(LLD::TxnBegin(TAO::Ledger::FLAGS::BLOCK, LLD::INSTANCES::MERKLE));
        REQUIRE(LLD::Client->WriteBlock(hashCandidate, candidate));

        /* Remove one recovery-group participant to force the real checkpoint
         * barrier to reject the transition before any staged record is applied. */
        REQUIRE(LLD::Logical->TxnRelease());
        REQUIRE_FALSE(candidate.SetBest());

        TAO::Ledger::ClientBlock missingCandidate;
        REQUIRE_FALSE(LLD::Client->ReadBlock(hashCandidate, missingCandidate));

        TAO::Ledger::ClientBlock unchangedGenesis;
        REQUIRE(LLD::Client->ReadBlock(hashGenesis, unchangedGenesis));
        REQUIRE(unchangedGenesis.hashNextBlock == 0);

        uint1024_t hashBest;
        REQUIRE(LLD::Client->ReadBestChain(hashBest));
        REQUIRE(hashBest == hashGenesis);
        REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == hashGenesis);
        REQUIRE(TAO::Ledger::ChainState::tStateBest.load().GetHash() == hashGenesis);
        REQUIRE(TAO::Ledger::ChainState::nBestHeight.load() == genesis.nHeight);
    }

    LLD::Client->EraseBlock(hashCandidate);
    if(hadGenesis)
        LLD::Client->WriteBlock(hashGenesis, savedGenesis);
    else
        LLD::Client->EraseBlock(hashGenesis);

    if(hadBest)
        LLD::Client->WriteBestChain(savedBest);
    else
        LLD::Client->Erase(std::string("hashbestchain"));
}


TEST_CASE("Checkpoint fixture teardown restores genesis and removes synthetic blocks",
          "[ledger][chainstate][checkpoint][startup][real]")
{
    RealCodeLedgerGuard ledgerGuard;
    ChainStateGuard chainGuard;
    BestChainDiskGuard bestChainGuard;
    const uint1024_t hashGenesis = TAO::Ledger::ChainState::Genesis();
    TAO::Ledger::BlockState savedGenesis;
    REQUIRE(LLD::Ledger->ReadBlock(hashGenesis, savedGenesis));
    std::vector<uint1024_t> hashes;

    {
        GenesisDiskGuard genesisGuard;
        CheckpointBlocksDiskGuard blocksGuard;
        const auto fixture = BuildCheckpointChainFixture(30000, blocksGuard);
        hashes = blocksGuard.hashes;
        REQUIRE(LLD::Ledger->EraseBlock(fixture.hashTwo));
    }

    for(const auto& hash : hashes)
        REQUIRE_FALSE(LLD::Ledger->HasBlock(hash));

    TAO::Ledger::BlockState restoredGenesis;
    REQUIRE(LLD::Ledger->ReadBlock(hashGenesis, restoredGenesis));
    REQUIRE(restoredGenesis.GetHash() == savedGenesis.GetHash());
    REQUIRE(restoredGenesis.hashNextBlock == savedGenesis.hashNextBlock);
}


TEST_CASE("ChainState hardcoded-checkpoint startup recovery is safe-by-default and preflighted",
          "[ledger][chainstate][checkpoint][startup][real]")
{
    RealCodeLedgerGuard ledgerGuard;
    ChainStateGuard     chainGuard;
    BestChainDiskGuard  bestChainGuard;
    GenesisDiskGuard    genesisGuard;
    CheckpointBlocksDiskGuard blocksGuard;
    ArgsMapGuard        argsGuard;
    FlagGuard           flagGuard;
    CheckpointRepairHookGuard repairHookGuard;

    config::fClient.store(false);
    config::fHybrid.store(false);
    config::fTestNet.store(true);

    SECTION("all applicable checkpoints present: startup recovery path is a no-op")
    {
        const auto fixture = BuildCheckpointChainFixture(31000, blocksGuard);
        uint32_t nSetBestCalls = 0;
        TAO::Ledger::ChainState::SetCheckpointRepairSetBestHook(
            [&nSetBestCalls](const TAO::Ledger::BlockState&)
            {
                ++nSetBestCalls;
                return true;
            });
        std::map<uint32_t, uint1024_t> checkpoints =
        {
            {1, fixture.hashOne},
            {2, fixture.hashTwo}
        };

        REQUIRE(TAO::Ledger::ChainState::RunHardcodedCheckpointRecoveryForTests(checkpoints, false));
        REQUIRE(nSetBestCalls == 0);
        REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == fixture.hashThree);
        REQUIRE(TAO::Ledger::ChainState::tStateBest.load().GetHash() == fixture.hashThree);
        REQUIRE_FALSE(LLD::HasOpenTransaction());

        uint1024_t hashBestDisk;
        REQUIRE(LLD::Ledger->ReadBestChain(hashBestDisk));
        REQUIRE(hashBestDisk == fixture.hashThree);
    }

    SECTION("newest applicable checkpoint missing: default startup is non-destructive")
    {
        const auto fixture = BuildCheckpointChainFixture(32000, blocksGuard);
        uint32_t nSetBestCalls = 0;
        TAO::Ledger::ChainState::SetCheckpointRepairSetBestHook(
            [&nSetBestCalls](const TAO::Ledger::BlockState&)
            {
                ++nSetBestCalls;
                return true;
            });
        const uint1024_t hashMissingCheckpoint(0x77770001);
        std::map<uint32_t, uint1024_t> checkpoints =
        {
            {1, fixture.hashOne},
            {2, hashMissingCheckpoint}
        };

        REQUIRE_FALSE(TAO::Ledger::ChainState::RunHardcodedCheckpointRecoveryForTests(checkpoints, false));
        REQUIRE(nSetBestCalls == 0);
        REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == fixture.hashThree);
        REQUIRE(TAO::Ledger::ChainState::tStateBest.load().GetHash() == fixture.hashThree);
        REQUIRE_FALSE(LLD::HasOpenTransaction());

        uint1024_t hashBestDisk;
        REQUIRE(LLD::Ledger->ReadBestChain(hashBestDisk));
        REQUIRE(hashBestDisk == fixture.hashThree);
    }

    SECTION("earliest applicable checkpoint missing: iterator boundary is safe")
    {
        const auto fixture = BuildCheckpointChainFixture(33000, blocksGuard);
        uint32_t nSetBestCalls = 0;
        TAO::Ledger::ChainState::SetCheckpointRepairSetBestHook(
            [&nSetBestCalls](const TAO::Ledger::BlockState&)
            {
                ++nSetBestCalls;
                return true;
            });
        const uint1024_t hashMissingEarliest(0x77770002);
        std::map<uint32_t, uint1024_t> checkpoints =
        {
            {1, hashMissingEarliest}
        };

        REQUIRE_FALSE(TAO::Ledger::ChainState::RunHardcodedCheckpointRecoveryForTests(checkpoints, false));
        REQUIRE(nSetBestCalls == 0);
        REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == fixture.hashThree);
        REQUIRE(TAO::Ledger::ChainState::tStateBest.load().GetHash() == fixture.hashThree);
        REQUIRE_FALSE(LLD::HasOpenTransaction());
    }

    SECTION("opt-in repair refuses rollback when ancestry walk is unreadable")
    {
        const auto fixture = BuildCheckpointChainFixture(34000, blocksGuard);
        uint32_t nSetBestCalls = 0;
        TAO::Ledger::ChainState::SetCheckpointRepairSetBestHook(
            [&nSetBestCalls](const TAO::Ledger::BlockState&)
            {
                ++nSetBestCalls;
                return true;
            });
        const uint1024_t hashMissingCheckpoint(0x77770003);
        std::map<uint32_t, uint1024_t> checkpoints =
        {
            {1, fixture.hashOne},
            {2, hashMissingCheckpoint}
        };

        REQUIRE(LLD::Ledger->EraseBlock(fixture.hashTwo));
        REQUIRE_FALSE(TAO::Ledger::ChainState::RunHardcodedCheckpointRecoveryForTests(checkpoints, true));
        REQUIRE(nSetBestCalls == 0);
        REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == fixture.hashThree);
        REQUIRE(TAO::Ledger::ChainState::tStateBest.load().GetHash() == fixture.hashThree);
        REQUIRE_FALSE(LLD::HasOpenTransaction());

        uint1024_t hashBestDisk;
        REQUIRE(LLD::Ledger->ReadBestChain(hashBestDisk));
        REQUIRE(hashBestDisk == fixture.hashThree);
    }

    SECTION("opt-in repair refuses an ancestor stored under the wrong hash")
    {
        const auto fixture = BuildCheckpointChainFixture(34100, blocksGuard);
        auto corrupt = fixture.two;
        ++corrupt.nNonce;
        REQUIRE(corrupt.GetHash() != fixture.hashTwo);
        REQUIRE(LLD::Ledger->WriteBlock(fixture.hashTwo, corrupt));
        auto ancestor = fixture.one;
        ancestor.hashNextBlock = corrupt.GetHash();
        REQUIRE(LLD::Ledger->WriteBlock(fixture.hashOne, ancestor));

        uint32_t nSetBestCalls = 0;
        TAO::Ledger::ChainState::SetCheckpointRepairSetBestHook(
            [&nSetBestCalls](const TAO::Ledger::BlockState&)
            {
                ++nSetBestCalls;
                return true;
            });
        const std::map<uint32_t, uint1024_t> checkpoints =
        {
            {1, fixture.hashOne},
            {2, uint1024_t(0x77770007)}
        };

        REQUIRE_FALSE(TAO::Ledger::ChainState::RunHardcodedCheckpointRecoveryForTests(checkpoints, true));
        REQUIRE(nSetBestCalls == 0);
        REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == fixture.hashThree);
        REQUIRE(TAO::Ledger::ChainState::tStateBest.load().GetHash() == fixture.hashThree);
        REQUIRE_FALSE(LLD::HasOpenTransaction());

        uint1024_t hashBestDisk;
        REQUIRE(LLD::Ledger->ReadBestChain(hashBestDisk));
        REQUIRE(hashBestDisk == fixture.hashThree);
    }

    SECTION("failed opt-in repair aborts staged changes without publishing a new tip")
    {
        TrustGuard trustGuard;
        const auto fixture = BuildCheckpointChainFixture(34200, blocksGuard);
        bool fFailCommit = false;
        SECTION("repair hook returns false") {}
        SECTION("outer transaction commit fails") { fFailCommit = true; }

        uint32_t nSetBestCalls = 0;
        TAO::Ledger::ChainState::SetCheckpointRepairSetBestHook(
            [&](const TAO::Ledger::BlockState& stateAncestor)
            {
                ++nSetBestCalls;
                REQUIRE(LLD::HasOpenTransaction());
                REQUIRE(LLD::Ledger->WriteBestChain(stateAncestor.GetHash()));
                REQUIRE(LLD::Ledger->EraseBlock(fixture.hashThree));
                if(fFailCommit)
                {
                    /* A missing participant forces commit to abort before durable apply. */
                    REQUIRE(LLD::Trust->TxnRelease());
                    return true;
                }

                return false;
            });
        const std::map<uint32_t, uint1024_t> checkpoints =
        {
            {1, fixture.hashOne},
            {2, uint1024_t(0x77770008)}
        };

        REQUIRE_FALSE(TAO::Ledger::ChainState::RunHardcodedCheckpointRecoveryForTests(checkpoints, true));
        REQUIRE(nSetBestCalls == 1);
        REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == fixture.hashThree);
        REQUIRE(TAO::Ledger::ChainState::tStateBest.load().GetHash() == fixture.hashThree);
        REQUIRE(TAO::Ledger::ChainState::nBestHeight.load() == fixture.three.nHeight);
        REQUIRE(TAO::Ledger::ChainState::nBestChainTrust.load() == fixture.three.nChainTrust);
        REQUIRE(TAO::Ledger::ChainState::hashCheckpoint.load() == 0);
        REQUIRE(TAO::Ledger::ChainState::nCheckpointHeight.load() == 0);
        REQUIRE_FALSE(LLD::HasOpenTransaction());

        uint1024_t hashBestDisk;
        REQUIRE(LLD::Ledger->ReadBestChain(hashBestDisk));
        REQUIRE(hashBestDisk == fixture.hashThree);
        TAO::Ledger::BlockState stateBestDisk;
        REQUIRE(LLD::Ledger->ReadBlock(fixture.hashThree, stateBestDisk));
        REQUIRE(stateBestDisk.GetHash() == fixture.hashThree);
    }

    SECTION("opt-in repair succeeds only after full rollback path preflight")
    {
        const auto fixture = BuildCheckpointChainFixture(35000, blocksGuard);
        uint32_t nSetBestCalls = 0;
        TAO::Ledger::ChainState::SetCheckpointRepairSetBestHook(
            [&nSetBestCalls](const TAO::Ledger::BlockState& stateAncestor)
            {
                ++nSetBestCalls;
                TAO::Ledger::ChainState::tStateBest = stateAncestor;
                TAO::Ledger::ChainState::hashBestChain = stateAncestor.GetHash();
                TAO::Ledger::ChainState::nBestHeight = stateAncestor.nHeight;
                TAO::Ledger::ChainState::nBestChainTrust = stateAncestor.nChainTrust;
                return LLD::Ledger->WriteBestChain(stateAncestor.GetHash());
            });
        const uint1024_t hashMissingCheckpoint(0x77770004);
        std::map<uint32_t, uint1024_t> checkpoints =
        {
            {1, fixture.hashOne},
            {2, hashMissingCheckpoint}
        };

        REQUIRE(TAO::Ledger::ChainState::RunHardcodedCheckpointRecoveryForTests(checkpoints, true));
        REQUIRE(nSetBestCalls == 1);
        REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == fixture.hashOne);
        REQUIRE(TAO::Ledger::ChainState::tStateBest.load().GetHash() == fixture.hashOne);
        REQUIRE(TAO::Ledger::ChainState::nBestHeight.load() == 1);
        REQUIRE_FALSE(LLD::HasOpenTransaction());

        uint1024_t hashBestDisk;
        REQUIRE(LLD::Ledger->ReadBestChain(hashBestDisk));
        REQUIRE(hashBestDisk == fixture.hashOne);
    }

    SECTION("opt-in repair can use the first older readable checkpoint after consecutive gaps")
    {
        const auto fixture = BuildCheckpointChainFixture(36000, blocksGuard);
        uint32_t nSetBestCalls = 0;
        TAO::Ledger::ChainState::SetCheckpointRepairSetBestHook(
            [&nSetBestCalls](const TAO::Ledger::BlockState& stateAncestor)
            {
                ++nSetBestCalls;
                TAO::Ledger::ChainState::tStateBest = stateAncestor;
                TAO::Ledger::ChainState::hashBestChain = stateAncestor.GetHash();
                TAO::Ledger::ChainState::nBestHeight = stateAncestor.nHeight;
                TAO::Ledger::ChainState::nBestChainTrust = stateAncestor.nChainTrust;
                return LLD::Ledger->WriteBestChain(stateAncestor.GetHash());
            });

        std::map<uint32_t, uint1024_t> checkpoints =
        {
            {1, fixture.hashOne},
            {2, uint1024_t(0x77770005)},
            {3, uint1024_t(0x77770006)}
        };

        REQUIRE(TAO::Ledger::ChainState::RunHardcodedCheckpointRecoveryForTests(checkpoints, true));
        REQUIRE(nSetBestCalls == 1);
        REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == fixture.hashOne);
        REQUIRE(TAO::Ledger::ChainState::tStateBest.load().GetHash() == fixture.hashOne);
        REQUIRE_FALSE(LLD::HasOpenTransaction());
    }

    SECTION("opt-in repair treats invalid checkpoint records like missing while searching older fallback")
    {
        const auto fixture = BuildCheckpointChainFixture(37000, blocksGuard);
        uint32_t nSetBestCalls = 0;
        TAO::Ledger::ChainState::SetCheckpointRepairSetBestHook(
            [&nSetBestCalls](const TAO::Ledger::BlockState& stateAncestor)
            {
                ++nSetBestCalls;
                TAO::Ledger::ChainState::tStateBest = stateAncestor;
                TAO::Ledger::ChainState::hashBestChain = stateAncestor.GetHash();
                TAO::Ledger::ChainState::nBestHeight = stateAncestor.nHeight;
                TAO::Ledger::ChainState::nBestChainTrust = stateAncestor.nChainTrust;
                return LLD::Ledger->WriteBestChain(stateAncestor.GetHash());
            });

        std::map<uint32_t, uint1024_t> checkpoints =
        {
            {1, fixture.hashOne},
            {2, fixture.hashThree}
        };

        REQUIRE(TAO::Ledger::ChainState::RunHardcodedCheckpointRecoveryForTests(checkpoints, true));
        REQUIRE(nSetBestCalls == 1);
        REQUIRE(TAO::Ledger::ChainState::hashBestChain.load() == fixture.hashOne);
        REQUIRE(TAO::Ledger::ChainState::tStateBest.load().GetHash() == fixture.hashOne);
        REQUIRE_FALSE(LLD::HasOpenTransaction());
    }
}


TEST_CASE("LLD::TxnRecovery retains complete journals after partial CONSENSUS apply failure",
          "[lld][txncommit][recovery]")
{
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;

    /* Apply earlier CONSENSUS participants successfully, then fail a later one
     * so recovery stops after a real partial apply and retains every journal. */
    const std::pair<std::string, uint32_t> contractKey =
        std::make_pair(std::string("recovery-contract"), 1);
    const std::pair<std::string, uint32_t> registerKey =
        std::make_pair(std::string("recovery-register"), 1);
    const std::pair<std::string, uint32_t> trustKey =
        std::make_pair(std::string("recovery-trust"), 1);

    LLD::Contract->Erase(contractKey);
    LLD::Register->Erase(registerKey);
    LLD::Trust->Erase(trustKey);

    REQUIRE(WriteRecoveryJournal("_CONTRACT", MakeWriteJournal(contractKey, 10)));
    REQUIRE(WriteRecoveryJournal("_REGISTER", MakeWriteJournal(registerKey, 11)));
    REQUIRE(WriteRecoveryJournal("_TRUST", MakeFailingIndexJournal()));
    REQUIRE(WriteRecoveryJournal("_LEGACY", MakeWriteJournal(
        std::make_pair(std::string("recovery-legacy"), 1), 13)));
    REQUIRE(WriteRecoveryJournal("_LEDGER", MakeWriteJournal(
        std::make_pair(std::string("recovery-ledger"), 1), 14)));

    const uint64_t nContractJournal = JournalSize("_CONTRACT");
    const uint64_t nRegisterJournal = JournalSize("_REGISTER");
    const uint64_t nTrustJournal = JournalSize("_TRUST");
    const uint64_t nLegacyJournal = JournalSize("_LEGACY");
    const uint64_t nLedgerJournal = JournalSize("_LEDGER");

    REQUIRE(nContractJournal > 0);
    REQUIRE(nRegisterJournal > 0);
    REQUIRE(nTrustJournal > 0);
    REQUIRE(nLegacyJournal > 0);
    REQUIRE(nLedgerJournal > 0);

    REQUIRE_FALSE(LLD::TxnRecovery());

    REQUIRE(LLD::Contract->Exists(contractKey));
    REQUIRE(LLD::Register->Exists(registerKey));
    REQUIRE_FALSE(LLD::Trust->Exists(trustKey));

    REQUIRE(JournalSize("_CONTRACT") == nContractJournal);
    REQUIRE(JournalSize("_REGISTER") == nRegisterJournal);
    REQUIRE(JournalSize("_TRUST") == nTrustJournal);
    REQUIRE(JournalSize("_LEGACY") == nLegacyJournal);
    REQUIRE(JournalSize("_LEDGER") == nLedgerJournal);

    LLD::ResetTxnRecoveryRequired();
    REQUIRE(LLD::Contract->TxnRelease());
    REQUIRE(LLD::Register->TxnRelease());
    REQUIRE(LLD::Trust->TxnRelease());
    REQUIRE(LLD::Legacy->TxnRelease());
    REQUIRE(LLD::Ledger->TxnRelease());

    LLD::Contract->Erase(contractKey);
    LLD::Register->Erase(registerKey);
}


TEST_CASE("LLD::TxnRecovery retains journals after a CONSENSUS parse failure",
          "[lld][txncommit][recovery]")
{
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;

    const std::pair<std::string, uint32_t> contractKey =
        std::make_pair(std::string("recovery-parse-contract"), 1);

    REQUIRE(WriteRecoveryJournal("_CONTRACT", MakeWriteJournal(contractKey, 30)));
    REQUIRE(WriteRecoveryJournal("_REGISTER", MakeInvalidRecoveryJournal()));

    const uint64_t nContractJournal = JournalSize("_CONTRACT");
    const uint64_t nRegisterJournal = JournalSize("_REGISTER");
    REQUIRE(nContractJournal > 0);
    REQUIRE(nRegisterJournal > 0);

    REQUIRE_FALSE(LLD::TxnRecovery());
    REQUIRE(JournalSize("_CONTRACT") == nContractJournal);
    REQUIRE(JournalSize("_REGISTER") == nRegisterJournal);

    LLD::ResetTxnRecoveryRequired();
    REQUIRE(LLD::Contract->TxnRelease());
    REQUIRE(LLD::Register->TxnRelease());
}


TEST_CASE("LLD::TxnRecovery retains journals after a CONSENSUS read failure",
          "[lld][txncommit][recovery]")
{
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;

    const std::pair<std::string, uint32_t> contractKey =
        std::make_pair(std::string("recovery-read-contract"), 1);
    const std::string strRegisterJournal =
        debug::safe_printstr(config::GetDataDir(), "_REGISTER/journal.dat");

    REQUIRE(WriteRecoveryJournal("_CONTRACT", MakeWriteJournal(contractKey, 31)));
    REQUIRE(filesystem::remove(strRegisterJournal));
    REQUIRE(filesystem::create_directories(strRegisterJournal + "/"));

    const uint64_t nContractJournal = JournalSize("_CONTRACT");
    REQUIRE(nContractJournal > 0);

    REQUIRE_FALSE(LLD::TxnRecovery());
    REQUIRE(JournalSize("_CONTRACT") == nContractJournal);
    REQUIRE(filesystem::is_directory(strRegisterJournal));

    LLD::ResetTxnRecoveryRequired();
    REQUIRE(filesystem::remove_directories(strRegisterJournal));
    REQUIRE(LLD::Contract->TxnRelease());
    REQUIRE(LLD::Register->TxnRelease());
}


TEST_CASE("LLD::TxnRecovery retains complete journals after partial MERKLE apply failure",
          "[lld][txncommit][recovery][merkle]")
{
    ClientModeGuard modeGuard;
    ClientGuard clientGuard;
    LogicalGuard logicalGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;

    /* Apply earlier MERKLE participants successfully, then fail a later one. */
    const std::pair<std::string, uint32_t> contractKey =
        std::make_pair(std::string("recovery-merkle-contract"), 1);
    const std::pair<std::string, uint32_t> registerKey =
        std::make_pair(std::string("recovery-merkle-register"), 1);
    const std::pair<std::string, uint32_t> logicalKey =
        std::make_pair(std::string("recovery-logical"), 1);

    LLD::Contract->Erase(contractKey);
    LLD::Register->Erase(registerKey);
    LLD::Logical->Erase(logicalKey);

    REQUIRE(WriteRecoveryJournal("_CONTRACT", MakeWriteJournal(contractKey, 20)));
    REQUIRE(WriteRecoveryJournal("_REGISTER", MakeWriteJournal(registerKey, 21)));
    REQUIRE(WriteRecoveryJournal("_API", MakeFailingIndexJournal()));
    REQUIRE(WriteRecoveryJournal("_CLIENT", MakeWriteJournal(
        std::make_pair(std::string("recovery-client"), 1), 23)));

    const uint64_t nContractJournal = JournalSize("_CONTRACT");
    const uint64_t nRegisterJournal = JournalSize("_REGISTER");
    const uint64_t nLogicalJournal = JournalSize("_API");
    const uint64_t nClientJournal = JournalSize("_CLIENT");

    REQUIRE(nContractJournal > 0);
    REQUIRE(nRegisterJournal > 0);
    REQUIRE(nLogicalJournal > 0);
    REQUIRE(nClientJournal > 0);

    REQUIRE_FALSE(LLD::TxnRecovery());

    REQUIRE(LLD::Contract->Exists(contractKey));
    REQUIRE(LLD::Register->Exists(registerKey));
    REQUIRE_FALSE(LLD::Logical->Exists(logicalKey));

    REQUIRE(JournalSize("_CONTRACT") == nContractJournal);
    REQUIRE(JournalSize("_REGISTER") == nRegisterJournal);
    REQUIRE(JournalSize("_API") == nLogicalJournal);
    REQUIRE(JournalSize("_CLIENT") == nClientJournal);

    LLD::ResetTxnRecoveryRequired();
    REQUIRE(LLD::Contract->TxnRelease());
    REQUIRE(LLD::Register->TxnRelease());
    REQUIRE(LLD::Logical->TxnRelease());
    REQUIRE(LLD::Client->TxnRelease());

    LLD::Contract->Erase(contractKey);
    LLD::Register->Erase(registerKey);
}
