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
#include <LLD/durable.h>
#include <LLD/cache/binary_lru.h>
#include <LLD/keychain/hashmap.h>
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
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
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


    /* Write one canonical parked journal for a recovery sequence. */
    bool WritePendingJournal(const std::string& strName,
                             const uint64_t nSequence,
                             const DataStream& ssJournal)
    {
        const std::string strPath = debug::safe_printstr(
            config::GetDataDir(), strName, "/journal.",
            std::setfill('0'), std::setw(8), nSequence, ".pending");

        FILE* stream = std::fopen(strPath.c_str(), "wb");
        if(!stream)
            return false;

        const std::vector<uint8_t>& vBytes = ssJournal.Bytes();
        const bool fWrote =
            std::fwrite(vBytes.data(), 1, vBytes.size(), stream) == vBytes.size()
            && std::fflush(stream) == 0;
        return std::fclose(stream) == 0 && fWrote;
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


    /* Build a parsed journal that never reached the commit marker. */
    DataStream MakeUncommittedJournal(const std::pair<std::string, uint32_t>& key,
                                      const uint32_t nValue)
    {
        DataStream ssKey(SER_LLD, LLD::DATABASE_VERSION);
        ssKey << key;

        DataStream ssData(SER_LLD, LLD::DATABASE_VERSION);
        ssData << std::string("NONE");
        ssData << nValue;

        DataStream ssJournal(SER_LLD, LLD::DATABASE_VERSION);
        ssJournal << std::string("write") << ssKey.Bytes() << ssData.Bytes();
        return ssJournal;
    }

    DataStream MakeCommitJournal()
    {
        DataStream ssJournal(SER_LLD, LLD::DATABASE_VERSION);
        ssJournal << std::string("commit");
        return ssJournal;
    }


    class FaultInjectingDurableIO : public LLD::DurableIO
    {
    public:
        bool fShortNextWrite{false};
        bool fFailNextStreamWrite{false};
        bool fFailNextIndexWrite{false};
        bool fFailNextFlush{false};
        bool fFailNextTruncate{false};
        bool fFailNextRead{false};
        bool fFailNextSectorSync{false};
        bool fSkipDirectorySync{false};
        uint32_t nFileSyncFailures{0};
        uint32_t nDirectorySyncFailures{0};
        uint32_t nFileSyncCalls{0};
        uint32_t nDirectorySyncCalls{0};
        std::vector<std::string> vSyncedFiles;
        std::vector<std::string> vTruncatedFiles;
        std::function<void()> onSectorSync;

        std::size_t Write(FILE* pStream, const void* pData, const std::size_t nSize) override
        {
            if(fShortNextWrite)
            {
                fShortNextWrite = false;
                const std::size_t nShortSize = nSize > 0 ? nSize - 1 : 0;
                return DurableIO::Write(pStream, pData, nShortSize);
            }

            return DurableIO::Write(pStream, pData, nSize);
        }

        std::size_t Write(std::ostream& cStream, const void* pData, const std::size_t nSize) override
        {
            if(fFailNextStreamWrite || (fFailNextIndexWrite && nSize == sizeof(uint16_t)))
            {
                fFailNextStreamWrite = false;
                fFailNextIndexWrite = false;
                cStream.setstate(std::ios::badbit);
                return 0;
            }

            if(fShortNextWrite)
            {
                fShortNextWrite = false;
                const std::size_t nShortSize = nSize > 0 ? nSize - 1 : 0;
                return DurableIO::Write(cStream, pData, nShortSize);
            }

            return DurableIO::Write(cStream, pData, nSize);
        }

        bool Flush(FILE* pStream) override
        {
            if(fFailNextFlush)
            {
                fFailNextFlush = false;
                return false;
            }

            return DurableIO::Flush(pStream);
        }

        bool Flush(std::ostream& cStream) override
        {
            if(fFailNextFlush)
            {
                fFailNextFlush = false;
                return false;
            }

            return DurableIO::Flush(cStream);
        }

        bool SyncFile(FILE* pStream) override
        {
            ++nFileSyncCalls;
            if(nFileSyncFailures > 0)
            {
                --nFileSyncFailures;
                return false;
            }

            return DurableIO::SyncFile(pStream);
        }

        bool SyncFile(const std::string& strPath) override
        {
            vSyncedFiles.push_back(strPath);
            if(onSectorSync && strPath.find("_block.") != std::string::npos)
                onSectorSync();

            if(fFailNextSectorSync && strPath.find("_block.") != std::string::npos)
            {
                fFailNextSectorSync = false;
                return false;
            }

            return DurableIO::SyncFile(strPath);
        }

        bool SyncDirectoryChain(const std::string& strDirectory) override
        {
            ++nDirectorySyncCalls;
            if(nDirectorySyncFailures > 0)
            {
                --nDirectorySyncFailures;
                return false;
            }

            return fSkipDirectorySync || DurableIO::SyncDirectoryChain(strDirectory);
        }

        bool Truncate(const std::string& strPath) override
        {
            vTruncatedFiles.push_back(strPath);
            if(fFailNextTruncate)
            {
                fFailNextTruncate = false;
                return false;
            }

            return DurableIO::Truncate(strPath);
        }

        bool Read(const std::string& strPath, std::vector<uint8_t>& vData) override
        {
            if(fFailNextRead)
            {
                fFailNextRead = false;
                return false;
            }

            return DurableIO::Read(strPath, vData);
        }
    };


    class DurabilityTestDatabase
        : public LLD::SectorDatabase<LLD::BinaryHashMap, LLD::BinaryLRU>
    {
    public:
        using SectorDatabase::SectorDatabase;

        void RequireRollover()
        {
            WRITE_LOCK(SECTOR_MUTEX);
            nCurrentFileSize = LLD::MAX_SECTOR_FILE_SIZE + 1;
        }

        bool SectorReadersAvailable()
        {
            bool fAvailable = false;
            std::thread probe([&]
            {
                fAvailable = SECTOR_MUTEX.try_lock_shared();
                if(fAvailable)
                    SECTOR_MUTEX.unlock_shared();
            });
            probe.join();
            return fAvailable;
        }

        bool SectorWritersBlocked()
        {
            bool fBlocked = false;
            std::thread probe([&]
            {
                fBlocked = !SECTOR_DURABILITY_MUTEX.try_lock_shared();
                if(!fBlocked)
                    SECTOR_DURABILITY_MUTEX.unlock_shared();
            });
            probe.join();
            return fBlocked;
        }
    };


    struct DurableIOGuard
    {
        explicit DurableIOGuard(LLD::DurableIO& cIO)
        {
            LLD::DurableIO::SetForTesting(&cIO);
        }

        ~DurableIOGuard()
        {
            Reset();
        }

        void Reset()
        {
            LLD::DurableIO::SetForTesting(nullptr);
        }
    };


    bool HasPendingJournal(const std::string& strName)
    {
        const std::string strDirectory =
            debug::safe_printstr(config::GetDataDir(), strName);
        std::error_code ec;
        if(!std::filesystem::exists(strDirectory, ec) || ec)
            return false;

        for(const std::filesystem::directory_entry& cEntry :
            std::filesystem::directory_iterator(strDirectory, ec))
        {
            if(ec)
                return false;

            const std::string strFile = cEntry.path().filename().string();
            if(strFile.rfind("journal.", 0) == 0 && strFile.size() >= 17
            && strFile.compare(strFile.size() - 8, 8, ".pending") == 0)
                return true;
        }

        return false;
    }


    uint32_t PendingJournalCount(const std::string& strName)
    {
        const std::string strDirectory =
            debug::safe_printstr(config::GetDataDir(), strName);
        std::error_code ec;
        if(!std::filesystem::exists(strDirectory, ec) || ec)
            return 0;

        uint32_t nCount = 0;
        for(const std::filesystem::directory_entry& cEntry :
            std::filesystem::directory_iterator(strDirectory, ec))
        {
            if(ec)
                return nCount;

            const std::string strFile = cEntry.path().filename().string();
            if(strFile.rfind("journal.", 0) == 0 && strFile.size() >= 17
            && strFile.compare(strFile.size() - 8, 8, ".pending") == 0)
                ++nCount;
        }

        return nCount;
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


TEST_CASE("LedgerDB hash-keyed block reads reject mismatched record identity",
          "[lld][ledger][integrity]")
{
    LedgerGuard guard;

    TAO::Ledger::BlockState stored;
    stored.nVersion = 4;
    stored.nChannel = 2;
    stored.nHeight = 7;
    stored.nBits = 1;
    stored.nNonce = std::chrono::steady_clock::now().time_since_epoch().count();

    const uint1024_t hashExpected = stored.GetHash();
    uint1024_t hashWrongKey = hashExpected;
    ++hashWrongKey;
    REQUIRE_FALSE(LLD::Ledger->HasBlock(hashWrongKey));

    struct BlockRecordDiskGuard
    {
        uint1024_t hash;

        ~BlockRecordDiskGuard()
        {
            LLD::Ledger->EraseBlock(hash);
        }
    } diskGuard{hashWrongKey};

    REQUIRE(LLD::Ledger->WriteBlock(hashWrongKey, stored));

    TAO::Ledger::BlockState result;
    REQUIRE_FALSE(LLD::Ledger->ReadBlock(hashWrongKey, result));

    TAO::Ledger::BlockState atomicInitial = stored;
    memory::atomic<TAO::Ledger::BlockState> atomicResult;
    atomicResult.store(atomicInitial);
    REQUIRE_FALSE(LLD::Ledger->ReadBlock(hashWrongKey, atomicResult));
    REQUIRE(atomicResult.load().GetHash() == hashExpected);
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


TEST_CASE("LLD::TxnCommit rejects a memory mode that does not own the transaction",
          "[lld][txncommit][concurrency]")
{
    LedgerGuard guard;

    std::mutex mutex;
    std::condition_variable condition;
    bool fContenderWaiting = false;
    std::atomic<bool> fContenderAcquired{false};

    REQUIRE(LLD::TxnBegin(TAO::Ledger::FLAGS::MINER, LLD::INSTANCES::LEDGER));
    REQUIRE_FALSE(LLD::TxnCommit(TAO::Ledger::FLAGS::MEMPOOL, LLD::INSTANCES::LEDGER));

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
    const bool fAborted = LLD::TxnAbort(TAO::Ledger::FLAGS::MINER, LLD::INSTANCES::LEDGER);

    if(contender.joinable())
        contender.join();
    LLD::SetTxnCoordinatorWaitHook({});

    REQUIRE(fAborted);
    REQUIRE(fSawContenderWaiting);
    REQUIRE_FALSE(fAcquiredBeforeRelease);
    REQUIRE(fContenderAcquired.load());
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

    /* Cleanup */
    LLD::Ledger->EraseBlock(hashCandidate);
    LLD::Ledger->EraseBlock(hashGenesis);
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


TEST_CASE("LLD::TxnRecovery retains unapplied complete journals when the group is not satisfied",
          "[lld][txncommit][recovery]")
{
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;
    LogicalGuard logicalGuard;

    const std::pair<std::string, uint32_t> contractKey =
        std::make_pair(std::string("recovery-partial-checkpoint-contract"), 1);
    const std::pair<std::string, uint32_t> legacyKey =
        std::make_pair(std::string("recovery-partial-checkpoint-legacy"), 1);

    struct Cleanup
    {
        std::pair<std::string, uint32_t> contractKey;
        std::pair<std::string, uint32_t> legacyKey;

        ~Cleanup()
        {
            LLD::ResetTxnRecoveryRequired();
            if(LLD::Contract)
            {
                LLD::Contract->TxnRelease();
                LLD::Contract->Erase(contractKey);
            }
            if(LLD::Register)
                LLD::Register->TxnRelease();
            if(LLD::Ledger)
                LLD::Ledger->TxnRelease();
            if(LLD::Trust)
                LLD::Trust->TxnRelease();
            if(LLD::Legacy)
            {
                LLD::Legacy->TxnRelease();
                LLD::Legacy->Erase(legacyKey);
            }
            if(LLD::Logical)
                LLD::Logical->TxnRelease();
        }
    } cleanup{contractKey, legacyKey};

    LLD::ResetTxnRecoveryRequired();
    REQUIRE(LLD::Contract->TxnRelease());
    REQUIRE(LLD::Register->TxnRelease());
    REQUIRE(LLD::Ledger->TxnRelease());
    REQUIRE(LLD::Trust->TxnRelease());
    REQUIRE(LLD::Legacy->TxnRelease());
    REQUIRE(LLD::Logical->TxnRelease());
    LLD::Contract->Erase(contractKey);
    LLD::Legacy->Erase(legacyKey);

    SECTION("complete journals stay when another participant never committed")
    {
        /* Crash between participant checkpoints: some journals are complete, and
         * one never reached commit. The complete records must not be truncated. */
        REQUIRE(WriteRecoveryJournal("_CONTRACT", MakeWriteJournal(contractKey, 41)));
        REQUIRE(WriteRecoveryJournal("_REGISTER", MakeWriteJournal(
            std::make_pair(std::string("recovery-partial-checkpoint-register"), 1), 42)));
        REQUIRE(WriteRecoveryJournal("_LEGACY", MakeUncommittedJournal(legacyKey, 43)));

        const uint64_t nContractJournal = JournalSize("_CONTRACT");
        const uint64_t nRegisterJournal = JournalSize("_REGISTER");
        const uint64_t nLegacyJournal = JournalSize("_LEGACY");
        REQUIRE(nContractJournal > 0);
        REQUIRE(nRegisterJournal > 0);
        REQUIRE(nLegacyJournal > 0);

        REQUIRE_FALSE(LLD::TxnRecovery());
        REQUIRE(LLD::LastTxnOutcome() == LLD::TXN_OUTCOME::RECOVERY_REQUIRED);
        /* TxnRecovery stages a complete journal in memory. Disk must stay
         * unchanged, including the unapplied payload. */
        REQUIRE(JournalSize("_CONTRACT") == nContractJournal);
        REQUIRE(JournalSize("_REGISTER") == nRegisterJournal);
        REQUIRE(JournalSize("_LEGACY") == 0);
    }

    SECTION("an uncommitted journal is discarded when no complete journal remains")
    {
        REQUIRE(WriteRecoveryJournal("_LEGACY", MakeUncommittedJournal(legacyKey, 44)));
        const uint64_t nLegacyJournal = JournalSize("_LEGACY");
        REQUIRE(nLegacyJournal > 0);

        REQUIRE(LLD::TxnRecovery());
        REQUIRE(LLD::LastTxnOutcome() == LLD::TXN_OUTCOME::RECOVERED);
        REQUIRE_FALSE(LLD::Legacy->Exists(legacyKey));
        REQUIRE(JournalSize("_LEGACY") == 0);
    }
}


TEST_CASE("LLD::TxnParkJournal retains a dangling pending symlink",
          "[lld][txncommit][recovery]")
{
    LedgerGuard ledgerGuard;

    const std::string strPending = debug::safe_printstr(
        config::GetDataDir(), "_LEDGER/journal.",
        std::setfill('0'), std::setw(8), uint64_t(1), ".pending");

    struct Cleanup
    {
        std::string strPending;

        ~Cleanup()
        {
            std::error_code ecRemove;
            std::filesystem::remove(strPending, ecRemove);
            if(LLD::Ledger)
                LLD::Ledger->TxnRelease();
        }
    } cleanup{strPending};

    std::error_code ecRemove;
    std::filesystem::remove(strPending, ecRemove);
    REQUIRE(WriteRecoveryJournal("_LEDGER", MakeCommitJournal()));

    std::error_code ecLink;
    std::filesystem::create_symlink("missing-journal-target", strPending, ecLink);
    REQUIRE_FALSE(ecLink);
    /* stat follows the link, so the old exists() check would miss this record. */
    REQUIRE_FALSE(filesystem::exists(strPending));
    REQUIRE(std::filesystem::is_symlink(std::filesystem::symlink_status(strPending)));

    REQUIRE_FALSE(LLD::Ledger->TxnParkJournal(1));
    REQUIRE(std::filesystem::is_symlink(std::filesystem::symlink_status(strPending)));
    REQUIRE(JournalSize("_LEDGER") > 0);
    REQUIRE_FALSE(std::filesystem::is_regular_file(strPending));
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


TEST_CASE("DurableIO injects short writes and filesystem operation failures",
          "[lld][durable]")
{
    FaultInjectingDurableIO cIO;
    FILE* pStream = std::tmpfile();
    REQUIRE(pStream != nullptr);

    const std::vector<uint8_t> vData{0x01, 0x02, 0x03, 0x04};
    cIO.fShortNextWrite = true;
    REQUIRE(cIO.Write(pStream, vData.data(), vData.size()) == vData.size() - 1);

    cIO.fFailNextFlush = true;
    REQUIRE_FALSE(cIO.Flush(pStream));
    std::fclose(pStream);

    cIO.fFailNextTruncate = true;
    REQUIRE_FALSE(cIO.Truncate("unused-durable-test-path"));

    cIO.fFailNextRead = true;
    std::vector<uint8_t> vRead;
    REQUIRE_FALSE(cIO.Read("unused-durable-test-path", vRead));

    cIO.nDirectorySyncFailures = 1;
    REQUIRE_FALSE(cIO.SyncDirectoryChain("unused-durable-test-directory"));
}


TEST_CASE("DurabilityTracker retains failed sync obligations for retry",
          "[lld][durable]")
{
    const std::string strPath =
        debug::safe_printstr(config::GetDataDir(), "_durable_tracker_failure_test");
    {
        std::ofstream stream(strPath, std::ios::binary | std::ios::trunc);
        REQUIRE(stream.is_open());
        stream << "durability";
    }

    LLD::DurabilityTracker cTracker;
    cTracker.MarkCreated(strPath);

    FaultInjectingDurableIO cIO;
    cIO.nFileSyncFailures = 1;
    cIO.nDirectorySyncFailures = 1;
    cIO.fSkipDirectorySync = true;

    REQUIRE_FALSE(cTracker.Sync(cIO));
    REQUIRE_FALSE(cTracker.Sync(cIO));
    REQUIRE(cTracker.Sync(cIO));
    REQUIRE(cIO.nFileSyncCalls == 2);
    REQUIRE(cIO.nDirectorySyncCalls == 2);

    REQUIRE(std::filesystem::remove(strPath));
}


TEST_CASE("DurabilityTracker syncs each directory chain once",
          "[lld][durable]")
{
    const std::string strDirectory =
        debug::safe_printstr(config::GetDataDir(), "_durable_tracker_chain_test");
    const std::string strPath = strDirectory + "/db/datachain/dirty";
    std::filesystem::create_directories(std::filesystem::path(strPath).parent_path());
    {
        std::ofstream stream(strPath, std::ios::binary | std::ios::trunc);
        REQUIRE(stream.is_open());
        stream << "durability";
    }

    LLD::DurabilityTracker cTracker;
    cTracker.MarkCreated(strPath);

    FaultInjectingDurableIO cIO;
    cIO.fSkipDirectorySync = true;
    REQUIRE(cTracker.Sync(cIO));
    REQUIRE(cIO.nDirectorySyncCalls == 1);

    REQUIRE(std::filesystem::remove_all(strDirectory) > 0);
}


TEST_CASE("LLD makes startup storage durable without a transaction",
          "[lld][durable]")
{
    const std::string strName = "_durable_startup_test";
    const std::string strPath = config::GetDataDir() + strName;
    std::filesystem::remove_all(strPath);
    FaultInjectingDurableIO cIO;
    DurableIOGuard ioGuard(cIO);
    {
        DurabilityTestDatabase db(strName, LLD::FLAGS::CREATE | LLD::FLAGS::FORCE, 8);
        REQUIRE(cIO.vSyncedFiles == std::vector<std::string>{
            strPath + "/keychain/_hashmap.00000",
            strPath + "/keychain/_hashmap.index",
            strPath + "/datachain/_block.00000"});
        REQUIRE(cIO.nDirectorySyncCalls > 0);
    }
    REQUIRE(std::filesystem::remove_all(strPath) > 0);
}


TEST_CASE("LLD syncs standalone sector and keychain writes on teardown",
          "[lld][durable]")
{
    const std::string strName = "_durable_teardown_test";
    const std::string strPath = config::GetDataDir() + strName;
    std::filesystem::remove_all(strPath);
    FaultInjectingDurableIO cIO;
    DurableIOGuard ioGuard(cIO);
    bool fExpectKeychainSync = true;
    {
        DurabilityTestDatabase db(strName, LLD::FLAGS::CREATE | LLD::FLAGS::FORCE, 8);
        REQUIRE(db.Write(std::string("key"), uint32_t(1)));
        cIO.vSyncedFiles.clear();
        SECTION("new record") {}
        SECTION("in-place update")
        {
            db.TxnBegin();
            REQUIRE(db.TxnCommit());
            cIO.vSyncedFiles.clear();
            REQUIRE(db.Write(std::string("key"), uint32_t(2)));
            fExpectKeychainSync = false;
        }
        SECTION("erased record")
        {
            db.TxnBegin();
            REQUIRE(db.TxnCommit());
            cIO.vSyncedFiles.clear();
            REQUIRE(db.Erase(std::string("key")));
        }
    }
    REQUIRE_FALSE(cIO.vSyncedFiles.empty());
    REQUIRE(cIO.vSyncedFiles.front() == strPath + "/datachain/_block.00000");
    if(fExpectKeychainSync)
        REQUIRE(std::find(cIO.vSyncedFiles.begin(), cIO.vSyncedFiles.end(),
            strPath + "/keychain/_hashmap.00000") != cIO.vSyncedFiles.end());
    REQUIRE(std::filesystem::remove_all(strPath) > 0);
}


TEST_CASE("BinaryHashMap syncs standalone collision files on teardown",
          "[lld][durable]")
{
    const std::string strPath = config::GetDataDir() + "_durable_hashmap_test";
    std::filesystem::remove_all(strPath);
    FaultInjectingDurableIO cIO;
    DurableIOGuard ioGuard(cIO);
    {
        LLD::BinaryHashMap keys(strPath + "/", LLD::FLAGS::CREATE | LLD::FLAGS::APPEND, 1);
        cIO.vSyncedFiles.clear();
        cIO.nDirectorySyncCalls = 0;
        REQUIRE(keys.Put(LLD::SectorKey(LLD::STATE::READY, {1}, 0, 0, 0)));
        REQUIRE(keys.Put(LLD::SectorKey(LLD::STATE::READY, {2}, 0, 0, 0)));
    }
    REQUIRE(cIO.vSyncedFiles == std::vector<std::string>{
        strPath + "/_hashmap.00000", strPath + "/_hashmap.00001", strPath + "/_hashmap.index"});
    REQUIRE(cIO.nDirectorySyncCalls > 0);
    REQUIRE(std::filesystem::remove_all(strPath) > 0);
}


TEST_CASE("LLD retries buffered I/O without losing records or advancing past a failed file",
          "[lld][durable]")
{
    const std::string strName = "_durable_rollover_test";
    const std::string strPath = config::GetDataDir() + strName;
    std::filesystem::remove_all(strPath);
    FaultInjectingDurableIO cIO;
    DurableIOGuard ioGuard(cIO);
    ShutdownGuard shutdownGuard;
    bool fWriteSecond = true;
    std::size_t nExpectedTruncates = 1;
    {
        DurabilityTestDatabase db(strName, LLD::FLAGS::CREATE | LLD::FLAGS::WRITE, 8);
        db.RequireRollover();
        cIO.vTruncatedFiles.clear();
        SECTION("failed rollover")
        {
            cIO.fFailNextTruncate = true;
            nExpectedTruncates = 2;
        }
        SECTION("failed stream write")
        {
            cIO.fFailNextStreamWrite = true;
        }
        SECTION("failed hashmap index write")
        {
            cIO.fFailNextIndexWrite = true;
            fWriteSecond = false;
        }
        REQUIRE(db.Write(std::string("first"), uint32_t(1)));
        if(fWriteSecond)
            REQUIRE(db.Write(std::string("second"), uint32_t(2)));
        config::fShutdown.store(true);
    }
    REQUIRE(cIO.vTruncatedFiles.size() == nExpectedTruncates);
    for(const auto& strFile : cIO.vTruncatedFiles)
        REQUIRE(strFile == strPath + "/datachain/_block.00001");
    config::fShutdown.store(shutdownGuard.savedShutdown);
    {
        DurabilityTestDatabase db(strName, LLD::FLAGS::FORCE, 8);
        uint32_t nValue = 0;
        REQUIRE(db.Read(std::string("first"), nValue));
        REQUIRE(nValue == 1);
        if(fWriteSecond)
        {
            REQUIRE(db.Read(std::string("second"), nValue));
            REQUIRE(nValue == 2);
        }
    }
    REQUIRE(std::filesystem::remove_all(strPath) > 0);
}


TEST_CASE("LLD keeps sector writers out of sync without blocking readers",
          "[lld][durable]")
{
    const std::string strName = "_durable_sync_lock_test";
    const std::string strPath = config::GetDataDir() + strName;
    std::filesystem::remove_all(strPath);
    FaultInjectingDurableIO cIO;
    DurableIOGuard ioGuard(cIO);
    {
        DurabilityTestDatabase db(strName, LLD::FLAGS::CREATE | LLD::FLAGS::WRITE, 8);
        bool fReadersAvailable = false;
        bool fWritersBlocked = false;
        cIO.onSectorSync = [&]
        {
            fReadersAvailable = db.SectorReadersAvailable();
            fWritersBlocked = db.SectorWritersBlocked();
        };
        db.TxnBegin();
        REQUIRE(db.Write(std::string("key"), uint32_t(1)));
        REQUIRE(db.TxnCommit());
        cIO.onSectorSync = nullptr;
        REQUIRE(fReadersAvailable);
        REQUIRE(fWritersBlocked);
    }
    REQUIRE(std::filesystem::remove_all(strPath) > 0);
}


TEST_CASE("LLD aborts staged changes when a participant checkpoint short-writes",
          "[lld][durable][txncommit]")
{
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;

    const std::pair<std::string, uint32_t> key =
        std::make_pair(std::string("durable-short-write-abort"), 1);

    REQUIRE(LLD::TxnBegin(0, LLD::INSTANCES::CONSENSUS));
    REQUIRE(LLD::Ledger->Write(key, uint32_t(88)));

    FaultInjectingDurableIO cIO;
    cIO.fShortNextWrite = true;
    DurableIOGuard ioGuard(cIO);
    const bool fCommitted = LLD::TxnCommit(0, LLD::INSTANCES::CONSENSUS);
    ioGuard.Reset();

    REQUIRE_FALSE(fCommitted);
    REQUIRE(LLD::LastTxnOutcome() == LLD::TXN_OUTCOME::ABORTED);
    REQUIRE_FALSE(LLD::HasOpenTransaction(0, LLD::INSTANCES::CONSENSUS));
    REQUIRE_FALSE(LLD::Ledger->Exists(key));
}


TEST_CASE("LLD replay is idempotent after a participant apply sync failure",
          "[lld][durable][recovery]")
{
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;

    const std::pair<std::string, uint32_t> key =
        std::make_pair(std::string("durable-replay-idempotent"), 1);
    LLD::Contract->Erase(key);

    REQUIRE(WriteRecoveryJournal("_CONTRACT", MakeWriteJournal(key, 91)));
    REQUIRE(WriteRecoveryJournal("_REGISTER", MakeCommitJournal()));
    REQUIRE(WriteRecoveryJournal("_LEDGER", MakeCommitJournal()));
    REQUIRE(WriteRecoveryJournal("_TRUST", MakeCommitJournal()));
    REQUIRE(WriteRecoveryJournal("_LEGACY", MakeCommitJournal()));

    FaultInjectingDurableIO cIO;
    cIO.fFailNextSectorSync = true;
    DurableIOGuard ioGuard(cIO);
    REQUIRE_FALSE(LLD::TxnRecovery());
    REQUIRE(LLD::LastTxnOutcome() == LLD::TXN_OUTCOME::RECOVERY_REQUIRED);
    REQUIRE(JournalSize("_CONTRACT") > 0);
    REQUIRE(JournalSize("_REGISTER") > 0);

    ioGuard.Reset();
    LLD::ResetTxnRecoveryRequired();
    REQUIRE(LLD::TxnRecovery());
    REQUIRE(LLD::LastTxnOutcome() == LLD::TXN_OUTCOME::RECOVERED);

    uint32_t nValue = 0;
    REQUIRE(LLD::Contract->Read(key, nValue));
    REQUIRE(nValue == 91);
    REQUIRE(LLD::Contract->Erase(key));
}


TEST_CASE("LLD reports recovery-required rather than abort after partial journal cleanup",
          "[lld][durable][txncommit][recovery]")
{
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;
    ShutdownGuard shutdownGuard;

    const std::pair<std::string, uint32_t> key =
        std::make_pair(std::string("durable-partial-cleanup"), 1);
    LLD::Ledger->Erase(key);

    REQUIRE(LLD::TxnBegin(0, LLD::INSTANCES::CONSENSUS));
    REQUIRE(LLD::Ledger->Write(key, uint32_t(92)));

    FaultInjectingDurableIO cIO;
    cIO.fFailNextTruncate = true;
    DurableIOGuard ioGuard(cIO);
    const bool fCommitted = LLD::TxnCommit(0, LLD::INSTANCES::CONSENSUS);
    ioGuard.Reset();

    REQUIRE_FALSE(fCommitted);
    REQUIRE(LLD::LastTxnOutcome() == LLD::TXN_OUTCOME::RECOVERY_REQUIRED);
    REQUIRE(LLD::Ledger->Exists(key));
    REQUIRE(JournalSize("_CONTRACT") > 0);
    REQUIRE_FALSE(LLD::HasOpenTransaction(0, LLD::INSTANCES::CONSENSUS));

    /* The apply already landed, but release truncated only some journals.
     * The remaining complete journal is indistinguishable from a crash between
     * checkpoints, so recovery must retain it instead of deleting the record. */
    LLD::ResetTxnRecoveryRequired();
    const uint64_t nContractJournal = JournalSize("_CONTRACT");
    REQUIRE(nContractJournal > 0);
    REQUIRE_FALSE(LLD::TxnRecovery());
    REQUIRE(LLD::LastTxnOutcome() == LLD::TXN_OUTCOME::RECOVERY_REQUIRED);
    REQUIRE(JournalSize("_CONTRACT") == nContractJournal);
    REQUIRE(LLD::Ledger->Exists(key));

    LLD::ResetTxnRecoveryRequired();
    REQUIRE(LLD::Contract->TxnRelease());
    REQUIRE(LLD::Ledger->Erase(key));
}


TEST_CASE("DurabilityTracker syncs created files without flushing dirty updates",
          "[lld][durable]")
{
    const std::string strDirectory =
        debug::safe_printstr(config::GetDataDir(), "_durable_created_only_test");
    const std::string strCreated = strDirectory + "/created.dat";
    const std::string strDirty = strDirectory + "/dirty.dat";
    std::filesystem::create_directories(strDirectory);
    {
        std::ofstream created(strCreated, std::ios::binary | std::ios::trunc);
        std::ofstream dirty(strDirty, std::ios::binary | std::ios::trunc);
        REQUIRE(created.is_open());
        REQUIRE(dirty.is_open());
        created << "new";
        dirty << "update";
    }

    LLD::DurabilityTracker cTracker;
    cTracker.MarkCreated(strCreated);
    cTracker.MarkDirty(strDirty);

    FaultInjectingDurableIO cIO;
    cIO.fSkipDirectorySync = true;
    REQUIRE(cTracker.SyncCreated(cIO));
    REQUIRE(cIO.vSyncedFiles == std::vector<std::string>{strCreated});

    cIO.vSyncedFiles.clear();
    REQUIRE(cTracker.Sync(cIO));
    REQUIRE(cIO.vSyncedFiles == std::vector<std::string>{strDirty});

    REQUIRE(std::filesystem::remove_all(strDirectory) > 0);
}


TEST_CASE("LLD parks journals until the sync-commit barrier",
          "[lld][txncommit][durable]")
{
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;
    ShutdownGuard shutdownGuard;
    config::fShutdown.store(false);
    LLD::ResetTxnRecoveryRequired();

    const std::pair<std::string, uint32_t> key =
        std::make_pair(std::string("sync-commit-parked-journal"), 1);
    LLD::Ledger->Erase(key);

    struct PendingRecoveryGuard
    {
        std::pair<std::string, uint32_t> key;

        ~PendingRecoveryGuard()
        {
            const bool fPending =
                HasPendingJournal("_CONTRACT") || HasPendingJournal("_REGISTER")
                || HasPendingJournal("_LEDGER") || HasPendingJournal("_TRUST")
                || HasPendingJournal("_LEGACY");
            if(fPending || JournalSize("_LEDGER") > 0 || JournalSize("_CONTRACT") > 0)
            {
                LLD::ResetTxnRecoveryRequired();
                LLD::TxnRecovery();
            }
            if(LLD::Ledger)
                LLD::Ledger->Erase(key);
        }
    } cleanup{key};

    REQUIRE(LLD::TxnBegin(0, LLD::INSTANCES::CONSENSUS));
    REQUIRE(LLD::Ledger->Write(key, uint32_t(77)));
    REQUIRE(LLD::TxnCommit(0, LLD::INSTANCES::CONSENSUS, LLD::SYNC_COMMIT_BLOCKS));
    REQUIRE(LLD::LastTxnOutcome() == LLD::TXN_OUTCOME::COMMITTED);

    uint32_t nValue = 0;
    REQUIRE(LLD::Ledger->Read(key, nValue));
    REQUIRE(nValue == 77);
    REQUIRE(JournalSize("_LEDGER") == 0);
    REQUIRE(JournalSize("_CONTRACT") == 0);
    REQUIRE(HasPendingJournal("_LEDGER"));
    REQUIRE(HasPendingJournal("_CONTRACT"));
    REQUIRE(HasPendingJournal("_REGISTER"));
    REQUIRE(HasPendingJournal("_TRUST"));
    REQUIRE(HasPendingJournal("_LEGACY"));

    /* Drop the applied record. Crash recovery must restore it from the parked
     * journal, which is the durable decision until the data barrier. */
    REQUIRE(LLD::Ledger->Erase(key));
    REQUIRE_FALSE(LLD::Ledger->Exists(key));

    REQUIRE(LLD::TxnRecovery());
    REQUIRE(LLD::LastTxnOutcome() == LLD::TXN_OUTCOME::RECOVERED);
    REQUIRE(LLD::Ledger->Read(key, nValue));
    REQUIRE(nValue == 77);
    REQUIRE_FALSE(HasPendingJournal("_LEDGER"));
    REQUIRE_FALSE(HasPendingJournal("_CONTRACT"));
    REQUIRE_FALSE(HasPendingJournal("_REGISTER"));
    REQUIRE_FALSE(HasPendingJournal("_TRUST"));
    REQUIRE_FALSE(HasPendingJournal("_LEGACY"));
    REQUIRE(JournalSize("_LEDGER") == 0);
}


TEST_CASE("LLD shutdown retains parked journals when recovery fails",
          "[lld][durable][recovery]")
{
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;

    const std::string strContractPending =
        debug::safe_printstr(config::GetDataDir(), "_CONTRACT/journal.00000042.pending");
    const std::string strName = "_recovery_shutdown_journal";
    const std::string strPath = config::GetDataDir() + strName;
    const std::string strPending = strPath + "/journal.00000042.pending";

    struct Cleanup
    {
        const std::string& strContractPending;
        const std::string& strPath;

        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove(strContractPending, ec);
            std::filesystem::remove_all(strPath, ec);
            /* Replay any unrelated parked journals this failed recovery stopped
             * before discarding, then clear the latch for later tests. */
            LLD::ResetTxnRecoveryRequired();
            LLD::TxnRecovery();
        }
    } cleanup{strContractPending, strPath};

    /* A successful startup grants shutdown permission to drop parked journals.
     * The same destructor must refuse after failed replay. */
    LLD::ResetTxnRecoveryRequired();
    REQUIRE(LLD::MayDiscardPendingJournals());

    std::filesystem::remove_all(strPath);
    {
        DurabilityTestDatabase db(strName, LLD::FLAGS::CREATE | LLD::FLAGS::WRITE, 8);
        {
            std::ofstream cPending(strPending, std::ios::binary | std::ios::trunc);
            REQUIRE(cPending.is_open());
            cPending << "not-a-journal";
            REQUIRE(cPending.good());
        }
    }
    REQUIRE_FALSE(std::filesystem::exists(strPending));

    {
        std::ofstream cPending(strContractPending, std::ios::binary | std::ios::trunc);
        REQUIRE(cPending.is_open());
        cPending << "not-a-journal";
        REQUIRE(cPending.good());
    }

    REQUIRE_FALSE(LLD::TxnRecovery());
    REQUIRE(LLD::LastTxnOutcome() == LLD::TXN_OUTCOME::RECOVERY_REQUIRED);
    REQUIRE_FALSE(LLD::MayDiscardPendingJournals());
    REQUIRE(std::filesystem::exists(strContractPending));
    REQUIRE_FALSE(LLD::Contract->TxnDiscardPendingJournals());
    REQUIRE(std::filesystem::exists(strContractPending));

    {
        DurabilityTestDatabase db(strName, LLD::FLAGS::CREATE | LLD::FLAGS::WRITE, 8);
        {
            std::ofstream cPending(strPending, std::ios::binary | std::ios::trunc);
            REQUIRE(cPending.is_open());
            cPending << "not-a-journal";
            REQUIRE(cPending.good());
        }

        REQUIRE_FALSE(db.TxnDiscardPendingJournals());
        REQUIRE(std::filesystem::exists(strPending));
    }
    REQUIRE(std::filesystem::exists(strPending));
}


TEST_CASE("deferred commit syncs files created during apply",
          "[lld][durable][synccommit]")
{
    const std::string strName = "_deferred_created_sync";
    const std::string strPath = config::GetDataDir() + strName;
    const std::string strNewSector = strPath + "/datachain/_block.00001";
    const std::string strHashMap = strPath + "/keychain/_hashmap.00000";
    std::filesystem::remove_all(strPath);
    FaultInjectingDurableIO cIO;
    DurableIOGuard ioGuard(cIO);
    {
        DurabilityTestDatabase db(strName, LLD::FLAGS::CREATE | LLD::FLAGS::FORCE, 8);
        db.RequireRollover();
        cIO.vSyncedFiles.clear();
        db.TxnBegin();
        REQUIRE(db.Write(std::string("rollover-key"), uint32_t(1)));
        REQUIRE(db.TxnCommit(false));
        REQUIRE(std::find(cIO.vSyncedFiles.begin(), cIO.vSyncedFiles.end(), strNewSector)
            != cIO.vSyncedFiles.end());
        REQUIRE(std::find(cIO.vSyncedFiles.begin(), cIO.vSyncedFiles.end(), strHashMap)
            == cIO.vSyncedFiles.end());
    }
    REQUIRE(std::filesystem::remove_all(strPath) > 0);
}


TEST_CASE("parked journal names must be canonical unsigned sequences",
          "[lld][durable][recovery]")
{
    const std::string strName = "_pending_sequence_names";
    const std::string strPath = config::GetDataDir() + strName;
    std::filesystem::remove_all(strPath);
    {
        DurabilityTestDatabase db(strName, LLD::FLAGS::CREATE | LLD::FLAGS::WRITE, 8);
        const auto WritePending = [&strPath](const std::string& strFile)
        {
            std::ofstream cPending(strPath + "/" + strFile, std::ios::binary | std::ios::trunc);
            REQUIRE(cPending.is_open());
            cPending << "payload";
            REQUIRE(cPending.good());
        };

        WritePending("journal.-1.pending");
        WritePending("journal.+1.pending");
        WritePending("journal.18446744073709551616.pending");
        WritePending("journal.00000007.pending");

        std::vector<uint64_t> vSequences;
        REQUIRE_FALSE(db.TxnPendingSequences(vSequences));
        REQUIRE(vSequences.empty());
        REQUIRE_FALSE(db.TxnDiscardPendingJournals(true));
        REQUIRE(std::filesystem::exists(strPath + "/journal.-1.pending"));
        REQUIRE(std::filesystem::exists(strPath + "/journal.+1.pending"));
        REQUIRE(std::filesystem::exists(strPath + "/journal.18446744073709551616.pending"));
        REQUIRE(std::filesystem::exists(strPath + "/journal.00000007.pending"));

        REQUIRE(std::filesystem::remove(strPath + "/journal.-1.pending"));
        REQUIRE(std::filesystem::remove(strPath + "/journal.+1.pending"));
        REQUIRE(std::filesystem::remove(strPath + "/journal.18446744073709551616.pending"));

        REQUIRE(db.TxnPendingSequences(vSequences));
        REQUIRE(vSequences == std::vector<uint64_t>{7});
        REQUIRE(db.TxnDiscardPendingJournals(true));
        REQUIRE_FALSE(std::filesystem::exists(strPath + "/journal.00000007.pending"));
    }
    REQUIRE(std::filesystem::remove_all(strPath) > 0);
}


TEST_CASE("empty pending journal sequence fails closed",
          "[lld][durable][recovery]")
{
    const std::string strName = "_pending_empty_sequence";
    const std::string strPath = config::GetDataDir() + strName;
    const std::string strPending = strPath + "/journal..pending";
    std::filesystem::remove_all(strPath);
    {
        DurabilityTestDatabase db(strName, LLD::FLAGS::CREATE | LLD::FLAGS::WRITE, 8);
        {
            std::ofstream cPending(strPending, std::ios::binary | std::ios::trunc);
            REQUIRE(cPending.is_open());
            cPending << "payload";
            REQUIRE(cPending.good());
        }

        /* journal..pending matches journal.*.pending with an empty sequence.
         * Listing and discard must reject it rather than treat it as absent. */
        std::vector<uint64_t> vSequences;
        REQUIRE_FALSE(db.TxnPendingSequences(vSequences));
        REQUIRE(vSequences.empty());
        REQUIRE_FALSE(db.TxnDiscardPendingJournals(true));
        REQUIRE(std::filesystem::exists(strPending));
        REQUIRE(std::filesystem::remove(strPending));
    }
    REQUIRE(std::filesystem::remove_all(strPath) > 0);
}


TEST_CASE("deferred commit barriers are tracked per recovery group",
          "[lld][txncommit][durable]")
{
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;
    LogicalGuard logicalGuard;
    ShutdownGuard shutdownGuard;
    config::fShutdown.store(false);
    LLD::ResetTxnRecoveryRequired();
    /* Drop batches left by earlier tests so this threshold starts at zero. */
    REQUIRE(LLD::TxnRecovery());
    if(LLD::TxnBegin(0, LLD::INSTANCES::MERKLE))
        REQUIRE(LLD::TxnCommit(0, LLD::INSTANCES::MERKLE, 1));

    const std::pair<std::string, uint32_t> consensusKey =
        std::make_pair(std::string("sync-commit-consensus-group"), 1);
    const std::pair<std::string, uint32_t> merkleKey =
        std::make_pair(std::string("sync-commit-merkle-group"), 1);
    LLD::Ledger->Erase(consensusKey);
    LLD::Logical->Erase(merkleKey);

    struct PendingRecoveryGuard
    {
        std::pair<std::string, uint32_t> consensusKey;
        std::pair<std::string, uint32_t> merkleKey;

        ~PendingRecoveryGuard()
        {
            /* Replay whichever group this test parked, then clear the latch.
             * A later MERKLE barrier drops journals recovery does not own. */
            LLD::ResetTxnRecoveryRequired();
            LLD::TxnRecovery();
            if(LLD::Logical && LLD::TxnBegin(0, LLD::INSTANCES::MERKLE))
                LLD::TxnCommit(0, LLD::INSTANCES::MERKLE, 1);

            if(LLD::Ledger)
                LLD::Ledger->Erase(consensusKey);
            if(LLD::Logical)
                LLD::Logical->Erase(merkleKey);
        }
    } cleanup{consensusKey, merkleKey};

    const uint32_t nBarrier = 2;

    REQUIRE(LLD::TxnBegin(0, LLD::INSTANCES::CONSENSUS));
    REQUIRE(LLD::Ledger->Write(consensusKey, uint32_t(1)));
    REQUIRE(LLD::TxnCommit(0, LLD::INSTANCES::CONSENSUS, nBarrier));
    REQUIRE(HasPendingJournal("_LEDGER"));
    REQUIRE(HasPendingJournal("_CONTRACT"));
    REQUIRE(PendingJournalCount("_CONTRACT") == 1);

    /* A deferred MERKLE commit must not consume the CONSENSUS 2-commit barrier
     * before either group flushes. Both journals stay until a barrier. */
    REQUIRE(LLD::TxnBegin(0, LLD::INSTANCES::MERKLE));
    REQUIRE(LLD::Logical->Write(merkleKey, uint32_t(2)));
    REQUIRE(LLD::TxnCommit(0, LLD::INSTANCES::MERKLE, nBarrier));
    REQUIRE(HasPendingJournal("_LEDGER"));
    REQUIRE(HasPendingJournal("_TRUST"));
    REQUIRE(HasPendingJournal("_LEGACY"));
    REQUIRE(HasPendingJournal("_API"));
    REQUIRE(PendingJournalCount("_CONTRACT") == 2);
    REQUIRE(PendingJournalCount("_REGISTER") == 2);

    /* This MERKLE barrier is newer than the parked CONSENSUS sequence. It must
     * sync that older group and drop its journals; leaving them would let
     * recovery replay the older Register/Contract values over this barrier. */
    REQUIRE(LLD::TxnBegin(0, LLD::INSTANCES::MERKLE));
    REQUIRE(LLD::Logical->Write(merkleKey, uint32_t(3)));
    REQUIRE(LLD::TxnCommit(0, LLD::INSTANCES::MERKLE, 1));
    REQUIRE_FALSE(HasPendingJournal("_API"));
    REQUIRE_FALSE(HasPendingJournal("_LEDGER"));
    REQUIRE_FALSE(HasPendingJournal("_TRUST"));
    REQUIRE_FALSE(HasPendingJournal("_LEGACY"));
    REQUIRE_FALSE(HasPendingJournal("_CONTRACT"));
    REQUIRE_FALSE(HasPendingJournal("_REGISTER"));

    uint32_t nValue = 0;
    REQUIRE(LLD::Ledger->Read(consensusKey, nValue));
    REQUIRE(nValue == 1);
    REQUIRE(LLD::Logical->Read(merkleKey, nValue));
    REQUIRE(nValue == 3);
    REQUIRE(LLD::TxnRecovery());
    REQUIRE(LLD::Ledger->Read(consensusKey, nValue));
    REQUIRE(nValue == 1);

    /* The retired CONSENSUS batch no longer counts, so one deferred commit parks
     * again and the next CONSENSUS commit is the barrier. */
    REQUIRE(LLD::TxnBegin(0, LLD::INSTANCES::CONSENSUS));
    REQUIRE(LLD::Ledger->Write(consensusKey, uint32_t(4)));
    REQUIRE(LLD::TxnCommit(0, LLD::INSTANCES::CONSENSUS, nBarrier));
    REQUIRE(HasPendingJournal("_LEDGER"));
    REQUIRE_FALSE(HasPendingJournal("_API"));

    REQUIRE(LLD::TxnBegin(0, LLD::INSTANCES::CONSENSUS));
    REQUIRE(LLD::Ledger->Write(consensusKey, uint32_t(5)));
    REQUIRE(LLD::TxnCommit(0, LLD::INSTANCES::CONSENSUS, nBarrier));
    REQUIRE_FALSE(HasPendingJournal("_LEDGER"));
    REQUIRE_FALSE(HasPendingJournal("_CONTRACT"));
    REQUIRE(LLD::Ledger->Read(consensusKey, nValue));
    REQUIRE(nValue == 5);
}


TEST_CASE("LLD::TxnRecovery applies a full-node MERKLE journal.dat before CONSENSUS",
          "[lld][txncommit][recovery][merkle]")
{
    LogicalGuard logicalGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;

    REQUIRE_FALSE(config::fClient.load());
    LLD::ResetTxnRecoveryRequired();

    const std::pair<std::string, uint32_t> contractKey =
        std::make_pair(std::string("merkle-journal-contract"), 1);
    const std::pair<std::string, uint32_t> registerKey =
        std::make_pair(std::string("merkle-journal-register"), 1);
    const std::pair<std::string, uint32_t> logicalKey =
        std::make_pair(std::string("merkle-journal-logical"), 1);
    const std::pair<std::string, uint32_t> consensusKey =
        std::make_pair(std::string("merkle-journal-consensus"), 1);

    struct Cleanup
    {
        std::pair<std::string, uint32_t> contractKey;
        std::pair<std::string, uint32_t> registerKey;
        std::pair<std::string, uint32_t> logicalKey;
        std::pair<std::string, uint32_t> consensusKey;

        ~Cleanup()
        {
            LLD::ResetTxnRecoveryRequired();
            LLD::TxnRecovery();
            if(LLD::Contract)
                LLD::Contract->Erase(contractKey);
            if(LLD::Register)
                LLD::Register->Erase(registerKey);
            if(LLD::Logical)
                LLD::Logical->Erase(logicalKey);
            if(LLD::Ledger)
                LLD::Ledger->Erase(consensusKey);
        }
    } cleanup{contractKey, registerKey, logicalKey, consensusKey};

    REQUIRE(LLD::TxnRecovery());
    LLD::Contract->Erase(contractKey);
    LLD::Register->Erase(registerKey);
    LLD::Logical->Erase(logicalKey);
    LLD::Ledger->Erase(consensusKey);

    /* Crash after the MERKLE checkpoint synced and before any journal was
     * parked. CONSENSUS still has an older parked commit on the shared
     * databases. Recovery must apply the MERKLE journal before CONSENSUS
     * release, and must not drop the CONSENSUS payload. */
    const DataStream ssConsensus = MakeWriteJournal(consensusKey, 51);
    const auto WritePending = [](const std::string& strName, const DataStream& ssJournal)
    {
        const std::string strPath =
            debug::safe_printstr(config::GetDataDir(), strName, "/journal.00000011.pending");
        FILE* stream = std::fopen(strPath.c_str(), "wb");
        if(!stream)
            return false;

        const std::vector<uint8_t>& vBytes = ssJournal.Bytes();
        const bool fWrote =
            std::fwrite(vBytes.data(), 1, vBytes.size(), stream) == vBytes.size()
            && std::fflush(stream) == 0;
        return std::fclose(stream) == 0 && fWrote;
    };

    REQUIRE(WritePending("_LEDGER", ssConsensus));
    REQUIRE(WritePending("_TRUST", ssConsensus));
    REQUIRE(WritePending("_LEGACY", ssConsensus));
    REQUIRE(WritePending("_CONTRACT", ssConsensus));
    REQUIRE(WritePending("_REGISTER", ssConsensus));
    REQUIRE(WriteRecoveryJournal("_CONTRACT", MakeWriteJournal(contractKey, 61)));
    REQUIRE(WriteRecoveryJournal("_REGISTER", MakeWriteJournal(registerKey, 62)));
    REQUIRE(WriteRecoveryJournal("_API", MakeWriteJournal(logicalKey, 63)));
    REQUIRE_FALSE(HasPendingJournal("_API"));

    REQUIRE(LLD::TxnRecovery());
    REQUIRE(LLD::LastTxnOutcome() == LLD::TXN_OUTCOME::RECOVERED);
    REQUIRE(LLD::Contract->Exists(contractKey));
    REQUIRE(LLD::Register->Exists(registerKey));
    REQUIRE(LLD::Logical->Exists(logicalKey));
    REQUIRE(LLD::Ledger->Exists(consensusKey));
    REQUIRE(JournalSize("_API") == 0);
    REQUIRE(JournalSize("_CONTRACT") == 0);
    REQUIRE(JournalSize("_REGISTER") == 0);
    REQUIRE_FALSE(HasPendingJournal("_LEDGER"));
    REQUIRE_FALSE(HasPendingJournal("_CONTRACT"));
}


TEST_CASE("LLD shutdown discards parked journals only after the recovery group syncs",
          "[lld][durable][recovery]")
{
    LogicalGuard logicalGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;
    ShutdownGuard shutdownGuard;
    config::fShutdown.store(false);
    LLD::ResetTxnRecoveryRequired();
    REQUIRE(LLD::TxnRecovery());
    if(LLD::TxnBegin(0, LLD::INSTANCES::MERKLE))
        REQUIRE(LLD::TxnCommit(0, LLD::INSTANCES::MERKLE, 1));
    if(LLD::TxnBegin(0, LLD::INSTANCES::CONSENSUS))
        REQUIRE(LLD::TxnCommit(0, LLD::INSTANCES::CONSENSUS, 1));

    const std::pair<std::string, uint32_t> consensusKey =
        std::make_pair(std::string("shutdown-group-consensus"), 1);
    const std::pair<std::string, uint32_t> merkleKey =
        std::make_pair(std::string("shutdown-group-merkle"), 1);

    struct Cleanup
    {
        std::pair<std::string, uint32_t> consensusKey;
        std::pair<std::string, uint32_t> merkleKey;

        ~Cleanup()
        {
            LLD::ResetTxnRecoveryRequired();
            LLD::TxnRecovery();
            LLD::TxnShutdownGroupBarrier();
            if(LLD::Ledger)
                LLD::Ledger->Erase(consensusKey);
            if(LLD::Logical)
                LLD::Logical->Erase(merkleKey);
        }
    } cleanup{consensusKey, merkleKey};

    LLD::Ledger->Erase(consensusKey);
    LLD::Logical->Erase(merkleKey);

    REQUIRE(LLD::TxnBegin(0, LLD::INSTANCES::CONSENSUS));
    REQUIRE(LLD::Ledger->Write(consensusKey, uint32_t(1)));
    REQUIRE(LLD::TxnCommit(0, LLD::INSTANCES::CONSENSUS, LLD::SYNC_COMMIT_BLOCKS));
    REQUIRE(LLD::TxnBegin(0, LLD::INSTANCES::MERKLE));
    REQUIRE(LLD::Logical->Write(merkleKey, uint32_t(2)));
    REQUIRE(LLD::TxnCommit(0, LLD::INSTANCES::MERKLE, LLD::SYNC_COMMIT_BLOCKS));
    REQUIRE(HasPendingJournal("_API"));
    REQUIRE(HasPendingJournal("_LEDGER"));
    REQUIRE(PendingJournalCount("_CONTRACT") == 2);

    /* Contract is destroyed before Logical. Its own sync must not delete the
     * MERKLE sequence that Logical still needs. */
    REQUIRE(LLD::MayDiscardPendingJournals());
    REQUIRE(LLD::Contract->TxnDiscardPendingJournals());
    REQUIRE(HasPendingJournal("_API"));
    REQUIRE(HasPendingJournal("_LEDGER"));
    REQUIRE(PendingJournalCount("_CONTRACT") == 2);

    REQUIRE(LLD::TxnShutdownGroupBarrier());
    REQUIRE_FALSE(HasPendingJournal("_API"));
    REQUIRE_FALSE(HasPendingJournal("_LEDGER"));
    REQUIRE_FALSE(HasPendingJournal("_CONTRACT"));
    REQUIRE_FALSE(HasPendingJournal("_REGISTER"));

    uint32_t nValue = 0;
    REQUIRE(LLD::Ledger->Read(consensusKey, nValue));
    REQUIRE(nValue == 1);
    REQUIRE(LLD::Logical->Read(merkleKey, nValue));
    REQUIRE(nValue == 2);
}


TEST_CASE("LLD::TxnRecovery keeps an incomplete parked sequence as a unit",
          "[lld][txncommit][recovery]")
{
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;

    REQUIRE_FALSE(config::fClient.load());
    LLD::ResetTxnRecoveryRequired();

    const std::pair<std::string, uint32_t> key =
        std::make_pair(std::string("partial-park-ledger"), 1);
    const DataStream ssJournal = MakeWriteJournal(key, 41);
    const std::string strLedgerPending =
        debug::safe_printstr(config::GetDataDir(), "_LEDGER/journal.00000005.pending");
    const std::string strContractPending =
        debug::safe_printstr(config::GetDataDir(), "_CONTRACT/journal.00000005.pending");

    struct Cleanup
    {
        std::pair<std::string, uint32_t> key;
        std::string strLedgerPending;
        std::string strContractPending;

        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove(strLedgerPending, ec);
            std::filesystem::remove(strContractPending, ec);
            LLD::ResetTxnRecoveryRequired();
            LLD::TxnRecovery();
            if(LLD::Ledger)
                LLD::Ledger->Erase(key);
        }
    } cleanup{key, strLedgerPending, strContractPending};

    LLD::Ledger->Erase(key);
    REQUIRE(WritePendingJournal("_LEDGER", 5, ssJournal));
    REQUIRE(WritePendingJournal("_CONTRACT", 5, ssJournal));
    REQUIRE_FALSE(HasPendingJournal("_REGISTER"));

    REQUIRE_FALSE(LLD::TxnRecovery());
    REQUIRE(LLD::LastTxnOutcome() == LLD::TXN_OUTCOME::RECOVERY_REQUIRED);
    REQUIRE(std::filesystem::exists(strLedgerPending));
    REQUIRE(std::filesystem::exists(strContractPending));
    REQUIRE_FALSE(LLD::Ledger->Exists(key));
}


TEST_CASE("LLD::TxnRecovery applies a partially parked MERKLE group before CONSENSUS",
          "[lld][txncommit][recovery][merkle]")
{
    LogicalGuard logicalGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;

    REQUIRE_FALSE(config::fClient.load());
    LLD::ResetTxnRecoveryRequired();

    const std::pair<std::string, uint32_t> logicalKey =
        std::make_pair(std::string("partial-merkle-logical"), 1);
    const std::pair<std::string, uint32_t> registerKey =
        std::make_pair(std::string("partial-merkle-register"), 1);
    const std::pair<std::string, uint32_t> contractKey =
        std::make_pair(std::string("partial-merkle-contract"), 1);

    struct Cleanup
    {
        std::pair<std::string, uint32_t> logicalKey;
        std::pair<std::string, uint32_t> registerKey;
        std::pair<std::string, uint32_t> contractKey;

        ~Cleanup()
        {
            LLD::ResetTxnRecoveryRequired();
            LLD::TxnRecovery();
            if(LLD::Logical)
                LLD::Logical->Erase(logicalKey);
            if(LLD::Register)
                LLD::Register->Erase(registerKey);
            if(LLD::Contract)
                LLD::Contract->Erase(contractKey);
        }
    } cleanup{logicalKey, registerKey, contractKey};

    LLD::Logical->Erase(logicalKey);
    LLD::Register->Erase(registerKey);
    LLD::Contract->Erase(contractKey);

    /* Logical's rename failed. Contract and Register were renamed afterward.
     * The shared sequence must stay MERKLE and Logical's journal must be applied,
     * not truncated by the CONSENSUS release. */
    REQUIRE(WriteRecoveryJournal("_API", MakeWriteJournal(logicalKey, 63)));
    REQUIRE(WritePendingJournal("_CONTRACT", 4, MakeWriteJournal(contractKey, 61)));
    REQUIRE(WritePendingJournal("_REGISTER", 4, MakeWriteJournal(registerKey, 62)));
    REQUIRE_FALSE(HasPendingJournal("_API"));
    REQUIRE_FALSE(HasPendingJournal("_LEDGER"));

    REQUIRE(LLD::TxnRecovery());
    REQUIRE(LLD::LastTxnOutcome() == LLD::TXN_OUTCOME::RECOVERED);
    REQUIRE(LLD::Logical->Exists(logicalKey));
    REQUIRE(LLD::Contract->Exists(contractKey));
    REQUIRE(LLD::Register->Exists(registerKey));
    REQUIRE(JournalSize("_API") == 0);
    REQUIRE_FALSE(HasPendingJournal("_CONTRACT"));
    REQUIRE_FALSE(HasPendingJournal("_REGISTER"));
}


TEST_CASE("LLD::TxnRecovery replays CONSENSUS and MERKLE journals in global sequence order",
          "[lld][txncommit][recovery]")
{
    LogicalGuard logicalGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;

    REQUIRE_FALSE(config::fClient.load());
    LLD::ResetTxnRecoveryRequired();

    const std::pair<std::string, uint32_t> key =
        std::make_pair(std::string("cross-group-sequence-order"), 1);

    const auto WriteGroup = [](const uint64_t nSequence, const bool fMerkle, const uint32_t nValue)
    {
        const DataStream ssJournal = MakeWriteJournal(
            std::make_pair(std::string("cross-group-sequence-order"), 1), nValue);
        if(fMerkle)
        {
            return WritePendingJournal("_API", nSequence, ssJournal)
                && WritePendingJournal("_CONTRACT", nSequence, ssJournal)
                && WritePendingJournal("_REGISTER", nSequence, ssJournal);
        }

        return WritePendingJournal("_CONTRACT", nSequence, ssJournal)
            && WritePendingJournal("_REGISTER", nSequence, ssJournal)
            && WritePendingJournal("_LEDGER", nSequence, ssJournal)
            && WritePendingJournal("_TRUST", nSequence, ssJournal)
            && WritePendingJournal("_LEGACY", nSequence, ssJournal);
    };

    struct Cleanup
    {
        std::pair<std::string, uint32_t> key;

        ~Cleanup()
        {
            LLD::ResetTxnRecoveryRequired();
            LLD::TxnRecovery();
            if(LLD::Register)
                LLD::Register->Erase(key);
        }
    } cleanup{key};

    LLD::Register->Erase(key);

    SECTION("later CONSENSUS sequence wins over an earlier MERKLE sequence")
    {
        REQUIRE(WriteGroup(2, true, 2));
        REQUIRE(WriteGroup(3, false, 3));
        REQUIRE(LLD::TxnRecovery());
        uint32_t nValue = 0;
        REQUIRE(LLD::Register->Read(key, nValue));
        REQUIRE(nValue == 3);
        REQUIRE_FALSE(HasPendingJournal("_REGISTER"));
        REQUIRE_FALSE(HasPendingJournal("_API"));
        REQUIRE_FALSE(HasPendingJournal("_LEDGER"));
    }

    SECTION("later MERKLE sequence wins over an earlier CONSENSUS sequence")
    {
        REQUIRE(WriteGroup(2, false, 2));
        REQUIRE(WriteGroup(3, true, 3));
        REQUIRE(LLD::TxnRecovery());
        uint32_t nValue = 0;
        REQUIRE(LLD::Register->Read(key, nValue));
        REQUIRE(nValue == 3);
        REQUIRE_FALSE(HasPendingJournal("_REGISTER"));
        REQUIRE_FALSE(HasPendingJournal("_API"));
        REQUIRE_FALSE(HasPendingJournal("_LEDGER"));
    }
}


TEST_CASE("parked journal removal syncs earlier deletions before returning an error",
          "[lld][durable][recovery]")
{
    const std::string strName = "_discard_removal_sync";
    const std::string strPath = config::GetDataDir() + strName;
    std::filesystem::remove_all(strPath);

    struct FaultGuard
    {
        ~FaultGuard()
        {
            LLD::SetJournalRemovalFault(0, 0);
        }
    } faultGuard;

    {
        DurabilityTestDatabase db(strName, LLD::FLAGS::CREATE | LLD::FLAGS::WRITE, 8);
        const auto WritePending = [&strPath](const uint64_t nSequence)
        {
            const std::string strFile = debug::safe_printstr(
                strPath, "/journal.", std::setfill('0'), std::setw(8), nSequence, ".pending");
            std::ofstream cPending(strFile, std::ios::binary | std::ios::trunc);
            REQUIRE(cPending.is_open());
            cPending << "payload";
            REQUIRE(cPending.good());
            return strFile;
        };

        const std::string strFirst = WritePending(1);
        const std::string strSecond = WritePending(2);
        FaultInjectingDurableIO cIO;
        DurableIOGuard ioGuard(cIO);
        LLD::SetJournalRemovalFault(1, 1);
        REQUIRE_FALSE(db.TxnDiscardPendingSequences(std::vector<uint64_t>{1, 2}));
        REQUIRE(cIO.nDirectorySyncCalls >= 1);
        REQUIRE_FALSE(std::filesystem::exists(strFirst));
        REQUIRE(std::filesystem::exists(strSecond));

        /* Directory iteration order is unspecified, so either remaining file may
         * be the one whose removal fails. One earlier deletion must still sync. */
        cIO.nDirectorySyncCalls = 0;
        LLD::SetJournalRemovalFault(1, 1);
        const std::string strThird = WritePending(3);
        REQUIRE_FALSE(db.TxnDiscardPendingJournals(true));
        REQUIRE(cIO.nDirectorySyncCalls >= 1);
        const bool fSecond = std::filesystem::exists(strSecond);
        const bool fThird = std::filesystem::exists(strThird);
        REQUIRE(fSecond != fThird);
    }

    REQUIRE(std::filesystem::remove_all(strPath) > 0);
}


TEST_CASE("LLD finishes an interrupted multi-participant journal retirement",
          "[lld][durable][recovery]")
{
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;
    ShutdownGuard shutdownGuard;
    config::fShutdown.store(false);
    LLD::ResetTxnRecoveryRequired();

    const uint64_t nSequence = 77;
    const std::filesystem::path pathRetire =
        std::filesystem::path(config::GetDataDir()) / "txn-retire";
    const std::string strIntent = debug::safe_printstr(
        pathRetire.string(), "/journal.", std::setfill('0'), std::setw(8), nSequence, ".consensus");
    const std::string strPrepare = strIntent + ".prepare";

    struct Cleanup
    {
        std::filesystem::path pathRetire;

        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(pathRetire, ec);
            LLD::SetJournalRemovalFault(0, 0);
            config::fShutdown.store(false);
            LLD::ResetTxnRecoveryRequired();
            LLD::TxnRecovery();
        }
    } cleanup{pathRetire};

    const auto Pending = [nSequence](const char* szName)
    {
        return debug::safe_printstr(config::GetDataDir(), szName, "/journal.",
            std::setfill('0'), std::setw(8), nSequence, ".pending");
    };
    const std::string strContract = Pending("_CONTRACT");
    const std::string strRegister = Pending("_REGISTER");

    const auto WriteRaw = [](const std::string& strPath)
    {
        std::filesystem::create_directories(std::filesystem::path(strPath).parent_path());
        std::ofstream cPending(strPath, std::ios::binary | std::ios::trunc);
        REQUIRE(cPending.is_open());
        cPending << "not-a-journal";
        REQUIRE(cPending.good());
    };
    const auto RemovePlanted = [&]()
    {
        std::error_code ec;
        std::filesystem::remove(strContract, ec);
        std::filesystem::remove(strRegister, ec);
        std::filesystem::remove_all(pathRetire, ec);
        LLD::ResetTxnRecoveryRequired();
    };

    SECTION("partial sequence without an intent fails closed")
    {
        WriteRaw(strContract);
        WriteRaw(strRegister);
        REQUIRE_FALSE(LLD::TxnRecovery());
        REQUIRE(std::filesystem::exists(strContract));
        REQUIRE(std::filesystem::exists(strRegister));
        RemovePlanted();
    }

    SECTION("an unpublished prepare file does not delete journals")
    {
        WriteRaw(strContract);
        WriteRaw(strRegister);
        std::filesystem::create_directories(pathRetire);
        {
            std::ofstream cPrepare(strPrepare, std::ios::binary | std::ios::trunc);
            REQUIRE(cPrepare.is_open());
            cPrepare << "ret";
            REQUIRE(cPrepare.good());
        }

        REQUIRE_FALSE(LLD::TxnRecovery());
        REQUIRE(std::filesystem::exists(strContract));
        REQUIRE(std::filesystem::exists(strRegister));
        REQUIRE_FALSE(std::filesystem::exists(strPrepare));
        RemovePlanted();
    }

    SECTION("a durable intent deletes the remaining copies without replay")
    {
        WriteRaw(strContract);
        WriteRaw(strRegister);
        std::filesystem::create_directories(pathRetire);
        {
            std::ofstream cIntent(strIntent, std::ios::binary | std::ios::trunc);
            REQUIRE(cIntent.is_open());
            cIntent << "retire";
            REQUIRE(cIntent.good());
        }

        REQUIRE(LLD::TxnRecovery());
        REQUIRE(LLD::LastTxnOutcome() == LLD::TXN_OUTCOME::RECOVERED);
        REQUIRE_FALSE(std::filesystem::exists(strContract));
        REQUIRE_FALSE(std::filesystem::exists(strRegister));
        REQUIRE_FALSE(std::filesystem::exists(strIntent));
    }
}


TEST_CASE("LLD retirement intent lets startup finish a crashed barrier discard",
          "[lld][durable][recovery]")
{
    LedgerGuard ledgerGuard;
    TrustGuard trustGuard;
    LegacyGuard legacyGuard;
    ContractGuard contractGuard;
    RegisterGuard registerGuard;
    ShutdownGuard shutdownGuard;
    config::fShutdown.store(false);
    LLD::ResetTxnRecoveryRequired();

    const std::filesystem::path pathRetire =
        std::filesystem::path(config::GetDataDir()) / "txn-retire";
    const std::pair<std::string, uint32_t> key =
        std::make_pair(std::string("retirement-intent-barrier"), 1);

    struct Cleanup
    {
        std::filesystem::path pathRetire;
        std::pair<std::string, uint32_t> key;
        bool savedShutdown;

        ~Cleanup()
        {
            LLD::SetJournalRemovalFault(0, 0);
            config::fShutdown.store(savedShutdown);
            std::error_code ec;
            std::filesystem::remove_all(pathRetire, ec);
            LLD::ResetTxnRecoveryRequired();
            LLD::TxnRecovery();
            if(LLD::Ledger)
                LLD::Ledger->Erase(key);
        }
    } cleanup{pathRetire, key, config::fShutdown.load()};

    REQUIRE(LLD::TxnRecovery());
    LLD::Ledger->Erase(key);

    REQUIRE(LLD::TxnBegin(0, LLD::INSTANCES::CONSENSUS));
    REQUIRE(LLD::Ledger->Write(key, uint32_t(77)));
    REQUIRE(LLD::TxnCommit(0, LLD::INSTANCES::CONSENSUS, LLD::SYNC_COMMIT_BLOCKS));
    REQUIRE(HasPendingJournal("_CONTRACT"));
    REQUIRE(HasPendingJournal("_LEDGER"));

    LLD::SetJournalRemovalFault(1, 1);
    REQUIRE(LLD::TxnBegin(0, LLD::INSTANCES::CONSENSUS));
    REQUIRE(LLD::Ledger->Write(key, uint32_t(78)));
    REQUIRE_FALSE(LLD::TxnCommit(0, LLD::INSTANCES::CONSENSUS, 1));
    REQUIRE(config::fShutdown.load());
    REQUIRE(LLD::LastTxnOutcome() == LLD::TXN_OUTCOME::RECOVERY_REQUIRED);

    bool fIntent = false;
    std::error_code ec;
    REQUIRE(std::filesystem::exists(pathRetire, ec));
    for(const std::filesystem::directory_entry& cEntry :
        std::filesystem::directory_iterator(pathRetire))
    {
        const std::string strFile = cEntry.path().filename().string();
        if(strFile.rfind("journal.", 0) == 0
        && strFile.size() > std::string(".consensus").size()
        && strFile.compare(strFile.size() - 10, 10, ".consensus") == 0
        && strFile.compare(strFile.size() - 8, 8, ".prepare") != 0)
            fIntent = true;
    }
    REQUIRE(fIntent);

    const uint32_t nRemaining =
        (HasPendingJournal("_CONTRACT") ? 1u : 0u)
        + (HasPendingJournal("_REGISTER") ? 1u : 0u)
        + (HasPendingJournal("_LEDGER") ? 1u : 0u)
        + (HasPendingJournal("_TRUST") ? 1u : 0u)
        + (HasPendingJournal("_LEGACY") ? 1u : 0u);
    REQUIRE(nRemaining > 0);
    REQUIRE(nRemaining < 5);

    LLD::SetJournalRemovalFault(0, 0);
    config::fShutdown.store(false);
    LLD::ResetTxnRecoveryRequired();
    REQUIRE(LLD::TxnRecovery());
    REQUIRE_FALSE(HasPendingJournal("_CONTRACT"));
    REQUIRE_FALSE(HasPendingJournal("_REGISTER"));
    REQUIRE_FALSE(HasPendingJournal("_LEDGER"));
    REQUIRE_FALSE(HasPendingJournal("_TRUST"));
    REQUIRE_FALSE(HasPendingJournal("_LEGACY"));
    if(std::filesystem::exists(pathRetire, ec))
    {
        for(const std::filesystem::directory_entry& cEntry :
            std::filesystem::directory_iterator(pathRetire))
            REQUIRE(cEntry.path().filename().string().rfind("journal.", 0) != 0);
    }

    uint32_t nValue = 0;
    REQUIRE(LLD::Ledger->Read(key, nValue));
    REQUIRE(nValue == 78);
}
