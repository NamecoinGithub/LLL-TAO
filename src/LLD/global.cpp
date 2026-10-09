/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2026

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLD/include/global.h>
#include <LLD/durable.h>

#include <TAO/Ledger/include/enum.h> //for internal flags

#include <Util/include/args.h>
#include <Util/include/debug.h>
#include <Util/include/signals.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
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
        bool ShutdownGroupBarrier();
        bool MayDiscardPendingSequence(const void* pDatabase, uint64_t nSequence) const;

    private:
        using PhysicalDB = SectorDatabase<BinaryHashMap, BinaryLRU>;

        void ReleaseMemoryTransactions(uint8_t nFlags, uint16_t nInstances);
        bool ReleasePhysicalTransactions(uint16_t nInstances);
        void ReleaseOwnership();
        bool ParkGroup(uint16_t nInstances, uint64_t nSequence);
        bool DiscardSequences(uint16_t nInstances, const std::vector<uint64_t>& vSequences);
        bool PublishRetirement(uint16_t nInstances, uint64_t nSequence);
        bool ClearRetirement(uint16_t nInstances, uint64_t nSequence);
        bool FinishInterruptedRetirements();
        bool RecoverGroup(uint16_t nInstances,
                          const std::vector<PhysicalDB*>& vParticipants,
                          const std::vector<PhysicalDB*>* pvSequenceSources = nullptr);
        bool SyncGroup(uint16_t nInstances);
        bool RetireParkedJournals();
        bool LoadPending(const std::vector<PhysicalDB*>& vDatabases,
                         std::map<PhysicalDB*, std::set<uint64_t>>& mapPending);
        bool RecoverOrdered();
        bool QuiesceGroup(uint16_t nInstances);
        void FailRecovery(const std::string& strError);
        void RequireRecovery();
        void AllowPendingJournalDiscard();

        std::mutex cMutex;
        std::atomic<bool> fRecoveryRequired{false};
        std::atomic<bool> fDiscardPendingJournals{false};
        /* CONSENSUS and MERKLE share Contract and Register, but each group has
         * its own DeferredBatch so one group's commits do not count toward the
         * other's threshold. A data barrier is newer than every parked journal,
         * so it syncs both groups and discards every parked sequence in global
         * order. Leaving an earlier other-group journal would replay it over
         * the barrier. A newer journal cannot exist yet: the coordinator lock
         * admits only one commit at a time. */
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


    namespace
    {
        const char RETIREMENT_MARK[] = "retire";


        std::filesystem::path RetirementDirectory()
        {
            return std::filesystem::path(config::GetDataDir()) / "txn-retire";
        }


        /* CONSENSUS and MERKLE are the only groups that park a sequence. A subset
         * mask must not be recorded as one of those groups. */
        const char* RetirementGroup(const uint16_t nInstances)
        {
            if(nInstances == INSTANCES::CONSENSUS)
                return "consensus";
            if(nInstances == INSTANCES::MERKLE)
                return "merkle";

            return nullptr;
        }


        uint16_t RetirementInstances(const std::string& strGroup)
        {
            if(strGroup == "consensus")
                return INSTANCES::CONSENSUS;
            if(strGroup == "merkle")
                return INSTANCES::MERKLE;

            return 0;
        }


        std::string FormatParkSequence(const uint64_t nSequence)
        {
            return debug::safe_printstr(std::setfill('0'), std::setw(8), nSequence);
        }


        /* journal.<sequence>.consensus or .merkle, optional .prepare suffix.
         * The sequence must round-trip to PendingJournalPath()'s width. */
        bool ParseRetirementName(const std::string& strFile, uint64_t& nSequence,
                                 uint16_t& nInstances, bool& fPrepare)
        {
            nSequence = 0;
            nInstances = 0;
            fPrepare = false;

            static const std::string strPrefix = "journal.";
            if(strFile.compare(0, strPrefix.size(), strPrefix) != 0)
                return false;

            std::string strRest = strFile.substr(strPrefix.size());
            static const std::string strPrepare = ".prepare";
            if(strRest.size() > strPrepare.size()
            && strRest.compare(strRest.size() - strPrepare.size(), strPrepare.size(), strPrepare) == 0)
            {
                fPrepare = true;
                strRest.erase(strRest.size() - strPrepare.size());
            }

            const std::size_t nDot = strRest.rfind('.');
            if(nDot == std::string::npos || nDot == 0)
                return false;

            const std::string strSequence = strRest.substr(0, nDot);
            const std::string strGroup = strRest.substr(nDot + 1);
            nInstances = RetirementInstances(strGroup);
            if(nInstances == 0)
                return false;

            if(strSequence.empty()
            || strSequence.find_first_not_of("0123456789") != std::string::npos)
                return false;

            errno = 0;
            char* pEnd = nullptr;
            const unsigned long long nParsed = std::strtoull(strSequence.c_str(), &pEnd, 10);
            if(errno == ERANGE || !pEnd || *pEnd != '\0' || nParsed == 0
            || nParsed > std::numeric_limits<uint64_t>::max())
                return false;

            nSequence = static_cast<uint64_t>(nParsed);
            const std::string strCanonical = debug::safe_printstr(
                "journal.", FormatParkSequence(nSequence), ".", strGroup,
                fPrepare ? strPrepare : "");
            return strCanonical == strFile;
        }


        bool RetirementMarkMatches(const std::filesystem::path& pathIntent)
        {
            std::vector<uint8_t> vData;
            if(!DurableIO::Current().Read(pathIntent.string(), vData))
                return false;

            return vData.size() == sizeof(RETIREMENT_MARK) - 1
                && std::memcmp(vData.data(), RETIREMENT_MARK, vData.size()) == 0;
        }
    }


    /* Record that this sequence's data sync already succeeded, before any
     * participant copy is deleted. Startup finishes a durable intent instead of
     * treating the remaining files as an incomplete park. */
    bool DurableCommitCoordinator::PublishRetirement(const uint16_t nInstances,
                                                     const uint64_t nSequence)
    {
        const char* szGroup = RetirementGroup(nInstances);
        if(!szGroup)
            return debug::error(FUNCTION, "retirement group is not CONSENSUS or MERKLE");

        const std::filesystem::path pathDirectory = RetirementDirectory();
        std::error_code ecCreate;
        std::filesystem::create_directories(pathDirectory, ecCreate);
        if(ecCreate)
            return debug::error(FUNCTION, "failed to create retirement directory");

        DurableIO& cIO = DurableIO::Current();
        if(!cIO.SyncDirectoryChain(pathDirectory.string()))
            return debug::error(FUNCTION, "failed to sync retirement directory");

        const std::filesystem::path pathIntent = pathDirectory / debug::safe_printstr(
            "journal.", FormatParkSequence(nSequence), ".", szGroup);
        const std::filesystem::path pathPrepare = pathDirectory / debug::safe_printstr(
            "journal.", FormatParkSequence(nSequence), ".", szGroup, ".prepare");

        std::error_code ecIntent;
        const std::filesystem::file_status nIntentStatus =
            std::filesystem::symlink_status(pathIntent, ecIntent);
        if(ecIntent != std::errc::no_such_file_or_directory
        && (ecIntent || nIntentStatus.type() != std::filesystem::file_type::not_found))
        {
            if(ecIntent || nIntentStatus.type() != std::filesystem::file_type::regular
            || !RetirementMarkMatches(pathIntent))
                return debug::error(FUNCTION, "retirement intent is not a complete record");

            /* Already published. Sync again so a crash after rename but before
             * the previous directory sync still cannot delete journals first. */
            return cIO.SyncDirectoryChain(pathDirectory.string())
                || debug::error(FUNCTION, "failed to sync retirement directory");
        }

        std::error_code ecPrepare;
        const std::filesystem::file_status nPrepareStatus =
            std::filesystem::symlink_status(pathPrepare, ecPrepare);
        if(ecPrepare != std::errc::no_such_file_or_directory
        && (ecPrepare || nPrepareStatus.type() != std::filesystem::file_type::not_found))
        {
            if(ecPrepare || nPrepareStatus.type() != std::filesystem::file_type::regular)
                return debug::error(FUNCTION, "incomplete retirement record is not a regular file");

            std::error_code ecRemove;
            if(!std::filesystem::remove(pathPrepare, ecRemove) || ecRemove)
                return debug::error(FUNCTION, "failed to remove incomplete retirement record");
            if(!cIO.SyncDirectoryChain(pathDirectory.string()))
                return debug::error(FUNCTION, "failed to sync retirement directory");
        }

        FILE* pFile = cIO.Open(pathPrepare.string(), "wb");
        if(!pFile)
            return debug::error(FUNCTION, "failed to create retirement intent");

        const bool fWrote = cIO.Write(pFile, RETIREMENT_MARK, sizeof(RETIREMENT_MARK) - 1)
                == sizeof(RETIREMENT_MARK) - 1
            && cIO.Flush(pFile)
            && cIO.SyncFile(pFile);
        const bool fClosed = cIO.Close(pFile);
        if(!fWrote || !fClosed)
            return debug::error(FUNCTION, "failed to sync retirement intent");

        std::error_code ecRename;
        std::filesystem::rename(pathPrepare, pathIntent, ecRename);
        if(ecRename)
            return debug::error(FUNCTION, "failed to publish retirement intent");

        if(!cIO.SyncDirectoryChain(pathDirectory.string()))
            return debug::error(FUNCTION, "failed to sync retirement directory");

        return true;
    }


    bool DurableCommitCoordinator::ClearRetirement(const uint16_t nInstances,
                                                   const uint64_t nSequence)
    {
        const char* szGroup = RetirementGroup(nInstances);
        if(!szGroup)
            return debug::error(FUNCTION, "retirement group is not CONSENSUS or MERKLE");

        const std::filesystem::path pathDirectory = RetirementDirectory();
        const std::filesystem::path pathIntent = pathDirectory / debug::safe_printstr(
            "journal.", FormatParkSequence(nSequence), ".", szGroup);

        std::error_code ec;
        const std::filesystem::file_status nStatus = std::filesystem::symlink_status(pathIntent, ec);
        if(nStatus.type() == std::filesystem::file_type::not_found
        || ec == std::errc::no_such_file_or_directory)
            return true;
        if(ec || nStatus.type() != std::filesystem::file_type::regular)
            return debug::error(FUNCTION, "retirement intent is not a regular file");

        std::error_code ecRemove;
        if(!std::filesystem::remove(pathIntent, ecRemove) || ecRemove)
            return debug::error(FUNCTION, "failed to remove retirement intent");

        if(!DurableIO::Current().SyncDirectoryChain(pathDirectory.string()))
            return debug::error(FUNCTION, "failed to sync retirement directory");

        return true;
    }


    /* A committed intent means the data barrier already succeeded. Delete the
     * copies that a crash left behind, then drop the intent. An incomplete
     * prepare file is not an intent and must not delete journals. */
    bool DurableCommitCoordinator::FinishInterruptedRetirements()
    {
        const std::filesystem::path pathDirectory = RetirementDirectory();
        std::error_code ec;
        const std::filesystem::file_status nStatus = std::filesystem::symlink_status(pathDirectory, ec);
        if(ec == std::errc::no_such_file_or_directory
        || (!ec && nStatus.type() == std::filesystem::file_type::not_found))
            return true;
        if(ec || nStatus.type() != std::filesystem::file_type::directory)
            return debug::error(FUNCTION, "retirement directory is not a directory");

        std::filesystem::directory_iterator itEntries(pathDirectory, ec);
        if(ec)
            return debug::error(FUNCTION, "failed to list retirement intents");

        struct Intent
        {
            uint64_t nSequence;
            uint16_t nGroup;
        };

        std::vector<Intent> vIntents;
        std::vector<std::filesystem::path> vPrepare;
        const std::filesystem::directory_iterator itEnd;
        while(itEntries != itEnd)
        {
            const std::filesystem::directory_entry cEntry = *itEntries;
            itEntries.increment(ec);
            if(ec)
                return debug::error(FUNCTION, "failed to list retirement intents");

            std::error_code ecStatus;
            const std::filesystem::file_status cStatus = cEntry.symlink_status(ecStatus);
            if(ecStatus || cStatus.type() != std::filesystem::file_type::regular)
                return debug::error(FUNCTION, "retirement record is not a regular file");

            uint64_t nSequence = 0;
            uint16_t nGroup = 0;
            bool fPrepare = false;
            if(!ParseRetirementName(cEntry.path().filename().string(), nSequence, nGroup, fPrepare))
                return debug::error(FUNCTION, "retirement record name is not canonical");

            if(fPrepare)
            {
                vPrepare.push_back(cEntry.path());
                continue;
            }

            if(!RetirementMarkMatches(cEntry.path()))
                return debug::error(FUNCTION, "retirement intent is not a complete record");

            vIntents.push_back(Intent{nSequence, nGroup});
        }

        bool fRemovedPrepare = false;
        for(const std::filesystem::path& pathPrepare : vPrepare)
        {
            std::error_code ecRemove;
            if(!std::filesystem::remove(pathPrepare, ecRemove) || ecRemove)
                return debug::error(FUNCTION, "failed to remove incomplete retirement record");

            fRemovedPrepare = true;
        }

        if(fRemovedPrepare && !DurableIO::Current().SyncDirectoryChain(pathDirectory.string()))
            return debug::error(FUNCTION, "failed to sync retirement directory");

        std::sort(vIntents.begin(), vIntents.end(),
            [](const Intent& a, const Intent& b)
            {
                return a.nSequence < b.nSequence;
            });

        for(const Intent& cIntent : vIntents)
        {
            if(!DiscardSequences(cIntent.nGroup, std::vector<uint64_t>{cIntent.nSequence}))
                return debug::error(FUNCTION, "failed to finish interrupted journal retirement");
        }

        return true;
    }


    bool DurableCommitCoordinator::DiscardSequences(const uint16_t nInstances,
                                                    const std::vector<uint64_t>& vSequences)
    {
        if(vSequences.empty())
            return true;

        /* One sequence at a time. The intent for that sequence is durable before
         * any of its copies are deleted, and it is removed only after every
         * remaining copy is gone. A later sequence is not touched until this
         * one is fully retired. */
        std::vector<uint64_t> vOrdered = vSequences;
        std::sort(vOrdered.begin(), vOrdered.end());
        vOrdered.erase(std::unique(vOrdered.begin(), vOrdered.end()), vOrdered.end());

        for(const uint64_t nSequence : vOrdered)
        {
            if(!PublishRetirement(nInstances, nSequence))
                return false;

            const std::vector<uint64_t> vOne{nSequence};
            bool fDiscarded = true;

            if(Logical && (nInstances & INSTANCES::LOGICAL))
                fDiscarded = Logical->TxnDiscardPendingSequences(vOne) && fDiscarded;
            if(Contract && (nInstances & INSTANCES::CONTRACT))
                fDiscarded = Contract->TxnDiscardPendingSequences(vOne) && fDiscarded;
            if(Register && (nInstances & INSTANCES::REGISTER))
                fDiscarded = Register->TxnDiscardPendingSequences(vOne) && fDiscarded;
            if(Ledger && (nInstances & INSTANCES::LEDGER))
                fDiscarded = Ledger->TxnDiscardPendingSequences(vOne) && fDiscarded;
            if(Client && (nInstances & INSTANCES::CLIENT))
                fDiscarded = Client->TxnDiscardPendingSequences(vOne) && fDiscarded;
            if(Trust && (nInstances & INSTANCES::TRUST))
                fDiscarded = Trust->TxnDiscardPendingSequences(vOne) && fDiscarded;
            if(Legacy && (nInstances & INSTANCES::LEGACY))
                fDiscarded = Legacy->TxnDiscardPendingSequences(vOne) && fDiscarded;

            if(!fDiscarded)
                return false;

            if(!ClearRetirement(nInstances, nSequence))
                return false;
        }

        return true;
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


    bool DurableCommitCoordinator::SyncGroup(const uint16_t nInstances)
    {
        bool fSynced = true;

        const auto Sync = [&fSynced, nInstances](PhysicalDB* pDatabase, const uint16_t nBit)
        {
            if(pDatabase && (nInstances & nBit))
                fSynced = pDatabase->TxnSyncDeferred() && fSynced;
        };

        Sync(Logical, INSTANCES::LOGICAL);
        Sync(Contract, INSTANCES::CONTRACT);
        Sync(Register, INSTANCES::REGISTER);
        Sync(Ledger, INSTANCES::LEDGER);
        Sync(Client, INSTANCES::CLIENT);
        Sync(Trust, INSTANCES::TRUST);
        Sync(Legacy, INSTANCES::LEGACY);
        return fSynced;
    }


    /* Every parked sequence is older than the commit that reached a data
     * barrier. Sync the groups that still have journals, then delete the
     * lowest sequence first so a crash cannot replay an earlier journal over
     * an already-durable later one. Each sequence publishes a retirement intent
     * before any participant copy is removed. */
    bool DurableCommitCoordinator::RetireParkedJournals()
    {
        if(!cDeferredConsensus.vSequences.empty() && !SyncGroup(INSTANCES::CONSENSUS))
            return false;

        if(!cDeferredMerkle.vSequences.empty() && !SyncGroup(INSTANCES::MERKLE))
            return false;

        std::vector<std::pair<uint64_t, uint16_t>> vOrdered;
        vOrdered.reserve(cDeferredConsensus.vSequences.size() + cDeferredMerkle.vSequences.size());
        for(const uint64_t nSequence : cDeferredConsensus.vSequences)
            vOrdered.emplace_back(nSequence, static_cast<uint16_t>(INSTANCES::CONSENSUS));
        for(const uint64_t nSequence : cDeferredMerkle.vSequences)
            vOrdered.emplace_back(nSequence, static_cast<uint16_t>(INSTANCES::MERKLE));

        std::sort(vOrdered.begin(), vOrdered.end());
        for(const auto& cItem : vOrdered)
        {
            if(!DiscardSequences(cItem.second, std::vector<uint64_t>{cItem.first}))
                return false;
        }

        cDeferredConsensus = {};
        cDeferredMerkle = {};
        return true;
    }


    bool DurableCommitCoordinator::LoadPending(
        const std::vector<PhysicalDB*>& vDatabases,
        std::map<PhysicalDB*, std::set<uint64_t>>& mapPending)
    {
        for(PhysicalDB* pDatabase : vDatabases)
        {
            if(!pDatabase || mapPending.count(pDatabase))
                continue;

            std::vector<uint64_t> vPending;
            if(!pDatabase->TxnPendingSequences(vPending))
            {
                FailRecovery("failed to list parked transaction journals");
                return false;
            }

            mapPending.emplace(pDatabase, std::set<uint64_t>(vPending.begin(), vPending.end()));
        }

        return true;
    }


    /* Full-node recovery. Sequence numbers are global, so a MERKLE journal can
     * be older or newer than a CONSENSUS journal. Replaying one group first can
     * restore an older shared Contract/Register value over a newer one. */
    bool DurableCommitCoordinator::RecoverOrdered()
    {
        const std::vector<PhysicalDB*> vMerkle = {Logical, Contract, Register, Client};
        const std::vector<PhysicalDB*> vConsensus = {Contract, Register, Trust, Legacy, Ledger};

        std::map<PhysicalDB*, std::set<uint64_t>> mapPending;
        if(!LoadPending(vMerkle, mapPending) || !LoadPending(vConsensus, mapPending))
            return false;

        const auto HasSequence = [&mapPending](PhysicalDB* pDatabase, const uint64_t nSequence)
        {
            const auto itDatabase = mapPending.find(pDatabase);
            return pDatabase && itDatabase != mapPending.end()
                && itDatabase->second.count(nSequence) != 0;
        };

        std::set<uint64_t> setSequences;
        for(const auto& cPending : mapPending)
            setSequences.insert(cPending.second.begin(), cPending.second.end());

        const bool fMerkleJournal =
            (Logical && Logical->TxnHasRecoverableJournal())
            || (Client && Client->TxnHasRecoverableJournal());
        const bool fConsensusJournal =
            (Ledger && Ledger->TxnHasRecoverableJournal())
            || (Trust && Trust->TxnHasRecoverableJournal())
            || (Legacy && Legacy->TxnHasRecoverableJournal());

        std::map<uint64_t, uint16_t> mapOwner;
        for(const uint64_t nSequence : setSequences)
        {
            const bool fMerkleCopy = HasSequence(Logical, nSequence) || HasSequence(Client, nSequence);
            const bool fConsensusCopy = HasSequence(Ledger, nSequence)
                || HasSequence(Trust, nSequence) || HasSequence(Legacy, nSequence);

            if(fMerkleCopy && fConsensusCopy)
            {
                FailRecovery("parked sequence belongs to both recovery groups");
                return false;
            }

            if(fMerkleCopy)
                mapOwner.emplace(nSequence, static_cast<uint16_t>(INSTANCES::MERKLE));
            else if(fConsensusCopy)
                mapOwner.emplace(nSequence, static_cast<uint16_t>(INSTANCES::CONSENSUS));
            else if(HasSequence(Contract, nSequence) || HasSequence(Register, nSequence))
            {
                /* ParkGroup() can rename Contract/Register and then fail before
                 * Logical is renamed, or the reverse when Logical is renamed
                 * after those copies. A Logical/Client journal.dat identifies
                 * that partial group. Guessing CONSENSUS would truncate the
                 * unparked MERKLE journal and replay the shared files as a
                 * different group. */
                if(fMerkleJournal == fConsensusJournal)
                    continue;

                mapOwner.emplace(nSequence, static_cast<uint16_t>(
                    fMerkleJournal ? INSTANCES::MERKLE : INSTANCES::CONSENSUS));
            }
        }

        uint64_t nMerkleMax = 0;
        uint64_t nConsensusMax = 0;
        for(const auto& cOwner : mapOwner)
        {
            if(cOwner.second == INSTANCES::MERKLE)
                nMerkleMax = std::max(nMerkleMax, cOwner.first);
            else
                nConsensusMax = std::max(nConsensusMax, cOwner.first);
        }

        /* An unowned or partial sequence blocks this sequence and every higher
         * one. Lower complete sequences may already have been synced. */
        bool fBlocked = false;
        for(const uint64_t nSequence : setSequences)
        {
            if(fBlocked || !mapOwner.count(nSequence))
            {
                fBlocked = true;
                break;
            }

            const uint16_t nGroup = mapOwner[nSequence];
            const std::vector<PhysicalDB*>& vParticipants =
                nGroup == INSTANCES::MERKLE ? vMerkle : vConsensus;
            const uint64_t nGroupMax = nGroup == INSTANCES::MERKLE ? nMerkleMax : nConsensusMax;

            std::vector<PhysicalDB*> vMissing;
            bool fForeignPending = false;
            for(PhysicalDB* pDatabase : vParticipants)
            {
                if(!pDatabase)
                    continue;

                if(!HasSequence(pDatabase, nSequence))
                    vMissing.push_back(pDatabase);
                /* TxnJournalBytes() is zero for a directory or symlink. That
                 * entry is still a crash record and must block partial replay. */
                else if(pDatabase->TxnHasRecoverableJournal())
                    fForeignPending = true;
            }

            const bool fComplete = vMissing.empty();
            /* journal.dat on a missing participant is this sequence only when the
             * other group has no unparked journal. Otherwise that file is the
             * other group's newer commit, and this incomplete sequence stays. */
            const bool fOtherJournal = nGroup == INSTANCES::MERKLE
                ? fConsensusJournal : fMerkleJournal;
            bool fJournalUnit = !fComplete && !fForeignPending && !fOtherJournal
                && nSequence == nGroupMax;
            if(fJournalUnit)
            {
                for(PhysicalDB* pDatabase : vMissing)
                {
                    const auto itPending = mapPending.find(pDatabase);
                    if(itPending != mapPending.end() && !itPending->second.empty()
                    && *itPending->second.rbegin() > nSequence)
                    {
                        fJournalUnit = false;
                        break;
                    }

                    const RECOVERY nRecovery = pDatabase->TxnRecovery();
                    if(nRecovery == RECOVERY::FAILED)
                    {
                        FailRecovery("failed to recover transaction journal");
                        return false;
                    }

                    if(nRecovery != RECOVERY::COMPLETE)
                    {
                        fJournalUnit = false;
                        break;
                    }
                }
            }

            if(!fComplete && !fJournalUnit)
            {
                fBlocked = true;
                break;
            }

            for(PhysicalDB* pDatabase : vParticipants)
            {
                if(!pDatabase)
                    continue;

                const bool fMissing = std::find(vMissing.begin(), vMissing.end(), pDatabase) != vMissing.end();
                if(fMissing)
                {
                    if(pDatabase->TxnRecovery() != RECOVERY::COMPLETE || !pDatabase->TxnCommit(false))
                    {
                        FailRecovery("failed to recover unparked transaction journal");
                        return false;
                    }
                }
                else if(!pDatabase->TxnReplayPending(nSequence))
                {
                    FailRecovery("failed to replay parked transaction journal");
                    return false;
                }
            }

            if(!SyncGroup(nGroup))
            {
                FailRecovery("failed to sync replayed transaction data; parked journals retained");
                return false;
            }

            /* journal.dat on a missing participant was applied as this sequence.
             * Release it after the data sync, before a later sequence or
             * RecoverGroup's complete-journal pass can apply it again. */
            if(!fComplete)
            {
                for(PhysicalDB* pDatabase : vMissing)
                {
                    if(!pDatabase->TxnRelease())
                    {
                        FailRecovery("failed to release consumed transaction journal");
                        return false;
                    }
                }
            }

            if(!DiscardSequences(nGroup, std::vector<uint64_t>{nSequence}))
            {
                FailRecovery("failed to discard parked journals after data sync");
                return false;
            }
        }

        if(fBlocked)
        {
            FailRecovery("incomplete parked sequence retained; partial group was not discarded");
            return false;
        }

        /* journal.dat that was not consumed as a partial park is the newest
         * commit. Apply that group only after every parked sequence. Both
         * exclusive sides having a journal cannot be ordered, so keep them.
         * With no MERKLE journal, CONSENSUS keeps the historical apply order,
         * including a partial apply that must retain every journal. */
        if(fMerkleJournal && fConsensusJournal)
        {
            FailRecovery("both recovery groups have an unparked journal");
            return false;
        }

        if(fMerkleJournal)
            return RecoverGroup(INSTANCES::MERKLE, vMerkle);

        return RecoverGroup(INSTANCES::CONSENSUS, vConsensus);
    }


    bool DurableCommitCoordinator::RecoverGroup(
        const uint16_t nInstances,
        const std::vector<PhysicalDB*>& vParticipants,
        const std::vector<PhysicalDB*>* pvSequenceSources)
    {
        /* Sequence sources limit replay and discard to journals this group
         * owns. Shared Contract/Register copies of the other group's sequences
         * stay on disk when a full node recovers MERKLE before CONSENSUS. */
        const std::vector<PhysicalDB*>& vSources =
            pvSequenceSources ? *pvSequenceSources : vParticipants;

        std::set<uint64_t> setSequences;
        for(PhysicalDB* pDatabase : vSources)
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

        /* A parked sequence is one commit. ParkGroup() can rename only a subset
         * before a crash, so replaying whichever copies exist and then deleting
         * them drops the participants that never got the file. Replay and discard
         * a sequence only when every participant has it, or when the missing
         * copies are still the unparked journal.dat of that same newest sequence. */
        const uint64_t nLatestSequence = setSequences.empty() ? 0 : *setSequences.rbegin();
        std::set<uint64_t> setRecovered;
        std::set<PhysicalDB*> setConsumedJournals;
        bool fIncompleteSequence = false;

        for(const uint64_t nSequence : setSequences)
        {
            std::vector<PhysicalDB*> vMissing;
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

                if(std::find(vPending.begin(), vPending.end(), nSequence) == vPending.end())
                    vMissing.push_back(pDatabase);
            }

            if(vMissing.empty())
            {
                for(PhysicalDB* pDatabase : vParticipants)
                {
                    if(pDatabase && !pDatabase->TxnReplayPending(nSequence))
                    {
                        FailRecovery("failed to replay parked transaction journal");
                        return false;
                    }
                }

                setRecovered.insert(nSequence);
                continue;
            }

            /* journal.dat can only be this sequence when it is the newest parked
             * commit and the missing participant has no newer pending file. A
             * parked participant must not also still have journal.dat, or the
             * two records may be different commits. */
            bool fJournalUnit = nSequence == nLatestSequence;
            for(PhysicalDB* pDatabase : vParticipants)
            {
                if(!pDatabase || !fJournalUnit)
                    continue;

                const bool fMissing = std::find(vMissing.begin(), vMissing.end(), pDatabase) != vMissing.end();
                std::vector<uint64_t> vPending;
                if(!pDatabase->TxnPendingSequences(vPending))
                {
                    FailRecovery("failed to list parked transaction journals");
                    return false;
                }

                if(!fMissing)
                {
                    /* A non-regular journal.dat is not an empty file. It must
                     * not be treated as the missing participant's journal. */
                    if(pDatabase->TxnHasRecoverableJournal())
                        fJournalUnit = false;
                    continue;
                }

                if(!vPending.empty() && *std::max_element(vPending.begin(), vPending.end()) > nSequence)
                    fJournalUnit = false;

                const RECOVERY nRecovery = pDatabase->TxnRecovery();
                if(nRecovery == RECOVERY::FAILED)
                {
                    FailRecovery("failed to recover transaction journal");
                    return false;
                }

                if(nRecovery != RECOVERY::COMPLETE)
                    fJournalUnit = false;
            }

            if(!fJournalUnit)
            {
                fIncompleteSequence = true;
                break;
            }

            for(PhysicalDB* pDatabase : vParticipants)
            {
                if(!pDatabase)
                    continue;

                const bool fMissing = std::find(vMissing.begin(), vMissing.end(), pDatabase) != vMissing.end();
                if(fMissing)
                {
                    if(pDatabase->TxnRecovery() != RECOVERY::COMPLETE || !pDatabase->TxnCommit(false))
                    {
                        FailRecovery("failed to recover unparked transaction journal");
                        return false;
                    }

                    setConsumedJournals.insert(pDatabase);
                }
                else if(!pDatabase->TxnReplayPending(nSequence))
                {
                    FailRecovery("failed to replay parked transaction journal");
                    return false;
                }
            }

            setRecovered.insert(nSequence);
        }

        if(!setRecovered.empty())
        {
            for(PhysicalDB* pDatabase : vParticipants)
            {
                if(pDatabase && !pDatabase->TxnSyncDeferred())
                {
                    FailRecovery("failed to sync replayed transaction data; parked journals retained");
                    return false;
                }
            }

            /* These journal.dat files were the partial park. Release them before
             * the complete-journal pass below, which would apply them again. */
            for(PhysicalDB* pDatabase : setConsumedJournals)
            {
                if(!pDatabase->TxnRelease())
                {
                    FailRecovery("failed to release consumed transaction journal");
                    return false;
                }
            }

            const std::vector<uint64_t> vRecovered(setRecovered.begin(), setRecovered.end());
            if(!DiscardSequences(nInstances, vRecovered))
            {
                FailRecovery("failed to discard parked journals after data sync");
                return false;
            }
        }

        if(fIncompleteSequence)
        {
            FailRecovery("incomplete parked sequence retained; partial group was not discarded");
            return false;
        }

        bool fAllSatisfied = true;
        std::vector<PhysicalDB*> vComplete;
        std::vector<PhysicalDB*> vIncomplete;
        vComplete.reserve(vParticipants.size());
        vIncomplete.reserve(vParticipants.size());

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
            else if(nRecovery == RECOVERY::INCOMPLETE && nJournalBytes > 0)
                vIncomplete.push_back(pDatabase);

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

        /* Absent journals are the normal startup path. Truncate a journal only
         * after it was applied, or when it is an incomplete record that never
         * reached commit and no partial parked sequence remains. A complete
         * journal that this pass did not apply must stay: releasing the group
         * would drop a transaction that crashed between participant checkpoints.
         * Recovered sequences were already discarded above; do not discard a
         * sequence this group does not own. */
        if(fAllSatisfied)
        {
            if(!vComplete.empty() || !setRecovered.empty())
            {
                if(!ReleasePhysicalTransactions(nInstances))
                {
                    FailRecovery("failed to durably release transaction journals");
                    return false;
                }
            }

            return true;
        }

        for(PhysicalDB* pDatabase : vIncomplete)
        {
            if(!pDatabase->TxnRelease())
            {
                FailRecovery("failed to durably release transaction journals");
                return false;
            }
        }

        if(!vComplete.empty())
        {
            FailRecovery("incomplete recovery retained unapplied complete journals");
            return false;
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

        /* Quiesce every participant before any destructor runs. Contract and
         * Register are destroyed first and hold both groups' parked journals;
         * discarding there would drop a MERKLE record before Logical/Client
         * have synced. */
        cTxnCoordinator.ShutdownGroupBarrier();

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
        /* An interrupted retirement already synced its data. Finish deleting
         * those copies before a partial sequence can fail closed. */
        if(!FinishInterruptedRetirements())
        {
            FailRecovery("failed to finish interrupted journal retirement");
            return false;
        }

        std::vector<PhysicalDB*> vParticipants;
        uint16_t nInstances = INSTANCES::CONSENSUS;

        if(config::fClient.load())
        {
            nInstances = INSTANCES::MERKLE;
            vParticipants = {Contract, Register, Logical, Client};
            if(!RecoverGroup(nInstances, vParticipants))
                return false;
        }
        else if(!RecoverOrdered())
            return false;

        /* Recovery synced and discarded parked journals. Drop both batches so
         * the next commit does not inherit a stale barrier. A full node may
         * have recovered CONSENSUS and MERKLE together. */
        DeferredFor(INSTANCES::MERKLE) = {};
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
            /* This commit is newer than every parked sequence. Sync and discard
             * both groups, lowest sequence first, so recovery cannot replay an
             * earlier journal over this barrier. A discard failure keeps the
             * journals that are still on disk. RetireParkedJournals() clears
             * the batch, so remember the count for the barrier log. */
            const uint32_t nBarrierCommits = cBatch.nCommits;
            if(!RetireParkedJournals())
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

            if(nBarrierCommits > 0)
                debug::log(0, FUNCTION, "data durability barrier after ",
                    nBarrierCommits + 1, " commits");
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


    bool MayDiscardPendingSequence(const void* pDatabase, const uint64_t nSequence)
    {
        return cTxnCoordinator.MayDiscardPendingSequence(pDatabase, nSequence);
    }


    bool DurableCommitCoordinator::QuiesceGroup(const uint16_t nInstances)
    {
        bool fSynced = true;

        const auto Quiesce = [&fSynced](PhysicalDB* pDatabase, const uint16_t nBit, const uint16_t nMask)
        {
            if(pDatabase && (nMask & nBit))
                fSynced = pDatabase->QuiesceAndSync() && fSynced;
        };

        Quiesce(Logical, INSTANCES::LOGICAL, nInstances);
        Quiesce(Contract, INSTANCES::CONTRACT, nInstances);
        Quiesce(Register, INSTANCES::REGISTER, nInstances);
        Quiesce(Ledger, INSTANCES::LEDGER, nInstances);
        Quiesce(Client, INSTANCES::CLIENT, nInstances);
        Quiesce(Trust, INSTANCES::TRUST, nInstances);
        Quiesce(Legacy, INSTANCES::LEGACY, nInstances);
        return fSynced;
    }


    bool DurableCommitCoordinator::ShutdownGroupBarrier()
    {
        /* Recovery failure must keep every parked journal, including ones this
         * process did not create. Sync both groups before deleting any sequence,
         * and delete the lowest sequence first. Discarding a later group first
         * would let an earlier journal replay over data that shutdown already
         * synced. */
        if(!MayDiscardPendingJournals())
            return false;

        /* A failed barrier must clear the discard latch before destructors run.
         * Otherwise an exclusive participant can delete an untracked sequence
         * after only its local sync, while the group has not synced. */
        if(!QuiesceGroup(INSTANCES::MERKLE) || !QuiesceGroup(INSTANCES::CONSENSUS))
        {
            RetainPendingJournals();
            return false;
        }

        if(!RetireParkedJournals())
        {
            RetainPendingJournals();
            return false;
        }

        return true;
    }


    bool DurableCommitCoordinator::MayDiscardPendingSequence(const void* pDatabase,
                                                             const uint64_t nSequence) const
    {
        const auto Contains = [nSequence](const std::vector<uint64_t>& vSequences)
        {
            return std::find(vSequences.begin(), vSequences.end(), nSequence) != vSequences.end();
        };

        /* Tracked sequences are deleted by ShutdownGroupBarrier only after
         * every participant in that group has quiesced. A per-database
         * destructor must not remove them. */
        if(Contains(cDeferredConsensus.vSequences) || Contains(cDeferredMerkle.vSequences))
            return false;

        /* Contract and Register can hold either group's crash record. An
         * untracked file there is not proof that the other participants have
         * synced, so keep it for the coordinator barrier or the next startup. */
        if(pDatabase == static_cast<const void*>(Contract)
        || pDatabase == static_cast<const void*>(Register))
            return false;

        return true;
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


    bool TxnShutdownGroupBarrier()
    {
        return cTxnCoordinator.ShutdownGroupBarrier();
    }
    #endif
}
