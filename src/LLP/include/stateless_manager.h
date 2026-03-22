/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_INCLUDE_STATELESS_MANAGER_H
#define NEXUS_LLP_INCLUDE_STATELESS_MANAGER_H

#include <LLP/include/stateless_miner.h>
#include <LLP/include/get_block_policy.h>
#include <Util/include/convert.h>
#include <Util/templates/concurrent_hashmap.h>

#include <LLC/types/uint1024.h>

#include <string>
#include <vector>
#include <optional>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

/* Forward declaration to avoid including block.h in manager header */
namespace TAO { namespace Ledger { class Block; } }

namespace LLP
{
    /** MinerInfo
     *
     *  Simplified miner information for pool statistics.
     *  Contains essential metrics for pool dashboard display.
     *
     **/
    struct MinerInfo
    {
        uint256_t hashGenesis;           // Miner's genesis hash (authentication)
        uint256_t hashRewardAddress;     // Reward payout address
        bool fAuthenticated;             // Whether Falcon auth succeeded
        bool fRewardBound;               // Whether reward address has been set
        uint32_t nSessionId;             // Session identifier
        std::string strAddress;          // Miner's network address

        /** Default Constructor **/
        MinerInfo()
        : hashGenesis(0)
        , hashRewardAddress(0)
        , fAuthenticated(false)
        , fRewardBound(false)
        , nSessionId(0)
        , strAddress("")
        {
        }
    };


    /** StatelessMinerManager
     *
     *  Manages active stateless miner connections.
     *  Keeps track of miner contexts and provides RPC query interface.
     *
     *  Scalability Features:
     *  - Thread-safe concurrent hash map for parallel miner access
     *  - Support for multiple Falcon sessions with secure isolation
     *  - Session indexing by both address and keyID for efficient lookups
     *  - Atomic counters for lock-free statistics
     *
     *  Concurrency Refinements:
     *  - Atomic session counter for real-time monitoring
     *  - Lock-free read operations where possible
     *  - Fine-grained updates without global locks
     *
     **/
    class StatelessMinerManager
    {
    public:
        /** GET_HEIGHT per-session rolling-window request budget (shared across both mining lanes).
         *
         *  Miner-side polling is expected to occur every 30 seconds, so a 4/min
         *  budget allows one legacy-lane poll + one stateless-lane poll every
         *  30 seconds for the same authenticated session while still throttling
         *  bursty abuse.  Enforcement uses a rolling 60-second window, not a
         *  wall-clock calendar minute boundary.
         */
        static constexpr std::size_t GET_HEIGHT_ROLLING_LIMIT_PER_MINUTE = 4;
        static constexpr std::chrono::seconds GET_HEIGHT_ROLLING_WINDOW = std::chrono::seconds(60);

        /** Canonical GET_HEIGHT snapshot cache TTL.
         *
         *  Keep this short so miners get a fresh view promptly after tip advance,
         *  while still collapsing duplicate requests across connections.
         */
        static constexpr std::chrono::milliseconds GET_HEIGHT_CACHE_TTL = std::chrono::milliseconds(1000);

        /** Canonical full-height snapshot served by GET_HEIGHT. */
        struct GetHeightSnapshot
        {
            uint32_t nUnifiedHeight{0};
            uint32_t nPrimeHeight{0};
            uint32_t nHashHeight{0};
            uint32_t nStakeHeight{0};
            uint32_t nHashTipLo32{0};
        };

        /** Result of serving a GET_HEIGHT request through the shared manager. */
        struct GetHeightResult
        {
            GetHeightSnapshot snapshot{};
            uint32_t nFreshnessMs{0};
            uint32_t nLatencyMs{0};
            uint32_t nRetryAfterMs{0};
            std::size_t nRequestsInWindow{0};
            bool fSessionCacheHit{false};
            bool fGlobalCacheHit{false};
            bool fRateLimitBudgetExceeded{false};
        };

        /** Cached GET_HEIGHT snapshot entry. */
        struct GetHeightCacheEntry
        {
            GetHeightSnapshot snapshot{};
            std::chrono::steady_clock::time_point tCaptured{};
            bool fInitialized{false};
        };

        /** Get
         *
         *  Get the global manager instance.
         *
         *  @return Reference to the singleton manager
         *
         **/
        static StatelessMinerManager& Get();

        /** UpdateMiner
         *
         *  Update or add a miner context.
         *
         *  @param[in] strAddress Miner address
         *  @param[in] context Updated context
         *  @param[in] nLane Mining lane (0=Legacy, 1=Stateless)
         *
         **/
        void UpdateMiner(const std::string& strAddress, const MiningContext& context, uint8_t nLane = 0);

        /** GetMinerLane
         *
         *  Retrieve the last known mining lane for an address.
         *
         *  @param[in] strAddress Miner address
         *
         *  @return Lane if tracked, nullopt otherwise
         *
         **/
        std::optional<uint8_t> GetMinerLane(const std::string& strAddress) const;

        /** HasSwitchedLanes
         *
         *  Check if miner has switched lanes from a previous session.
         *
         *  @param[in] strAddress Miner address
         *  @param[in] nNewLane New lane to compare
         *
         *  @return true if tracked lane differs from new lane
         *
         **/
        bool HasSwitchedLanes(const std::string& strAddress, uint8_t nNewLane) const;

        /** RemoveMiner
         *
         *  Remove a miner from tracking.
         *
         *  @param[in] strAddress Miner address
         *
         *  @return true if miner was found and removed
         *
         **/
        bool RemoveMiner(const std::string& strAddress);

        /** RemoveMinerByKeyID
         *
         *  Remove a miner from tracking by Falcon key ID.
         *
         *  @param[in] hashKeyID Falcon key identifier
         *
         *  @return true if miner was found and removed
         *
         **/
        bool RemoveMinerByKeyID(const uint256_t& hashKeyID);

        /** GetMinerContext
         *
         *  Retrieve context for a specific miner.
         *
         *  @param[in] strAddress Miner address
         *
         *  @return Context if found, nullopt otherwise
         *
         **/
        std::optional<MiningContext> GetMinerContext(const std::string& strAddress) const;

        /** GetMinerContextByKeyID
         *
         *  Retrieve context by Falcon key ID.
         *
         *  @param[in] hashKeyID Falcon key identifier
         *
         *  @return Context if found, nullopt otherwise
         *
         **/
        std::optional<MiningContext> GetMinerContextByKeyID(const uint256_t& hashKeyID) const;

        /** GetMinerContextByIP
         *
         *  Look up a miner context using only the IP address (ignoring port).
         *  Used as fallback when IP:port lookup misses due to ephemeral port changes.
         *  If multiple miners share the same IP (e.g. behind NAT), returns the most
         *  recently active one.
         *
         *  @param[in] strIP   IP address string (no port)
         *
         *  @return Optional MiningContext, empty if not found
         *
         **/
        std::optional<MiningContext> GetMinerContextByIP(const std::string& strIP) const;

        /** GetMinerContextBySessionID
         *
         *  Retrieve context by session ID.
         *
         *  @param[in] nSessionId Session identifier
         *
         *  @return Context if found, nullopt otherwise
         *
         **/
        std::optional<MiningContext> GetMinerContextBySessionID(uint32_t nSessionId) const;

        /** GetMinerContextByGenesis
         *
         *  Retrieve context by genesis hash (payout address).
         *  Useful for GenesisHash reward mapping lookups.
         *
         *  @param[in] hashGenesis Tritium genesis hash
         *
         *  @return Context if found, nullopt otherwise
         *
         **/
        std::optional<MiningContext> GetMinerContextByGenesis(const uint256_t& hashGenesis) const;

        /** ListMiners
         *
         *  List all active miners.
         *
         *  @return Vector of all miner contexts
         *
         **/
        std::vector<MiningContext> ListMiners() const;

        /** ListMinersByGenesis
         *
         *  List all miners mining for a specific genesis hash.
         *  Supports GenesisHash reward mapping queries.
         *
         *  @param[in] hashGenesis Tritium genesis hash
         *
         *  @return Vector of matching miner contexts
         *
         **/
        std::vector<MiningContext> ListMinersByGenesis(const uint256_t& hashGenesis) const;

        /** GetMinerStatus
         *
         *  Get status for a specific miner in JSON format.
         *
         *  @param[in] strAddress Miner address
         *
         *  @return JSON string with miner status, or error
         *
         **/
        std::string GetMinerStatus(const std::string& strAddress) const;

        /** GetAllMinersStatus
         *
         *  Get status for all miners in JSON format.
         *
         *  @return JSON string array with all miner statuses
         *
         **/
        std::string GetAllMinersStatus() const;

        /** GetMinerCount
         *
         *  Get the number of active miners.
         *  Uses atomic counter for lock-free access.
         *
         *  @return Number of miners
         *
         **/
        size_t GetMinerCount() const;

        /** GetAuthenticatedCount
         *
         *  Get the number of authenticated miners.
         *  Uses atomic counter for lock-free access.
         *
         *  @return Number of authenticated miners
         *
         **/
        size_t GetAuthenticatedCount() const;

        /** GetActiveSessionCount
         *
         *  Get the number of active sessions (started and not expired).
         *
         *  @return Number of active sessions
         *
         **/
        size_t GetActiveSessionCount() const;

        /** GetTotalKeepalives
         *
         *  Get total number of keepalives processed across all sessions.
         *  Uses atomic counter for lock-free access.
         *
         *  @return Total keepalive count
         *
         **/
        uint64_t GetTotalKeepalives() const;

        /** GetPeakSessionCount
         *
         *  Get the peak concurrent session count (high water mark).
         *
         *  @return Peak session count
         *
         **/
        size_t GetPeakSessionCount() const;

        /** CleanupInactive
         *
         *  Remove miners that have been inactive for too long.
         *
         *  @param[in] nTimeoutSec Inactivity timeout in seconds
         *
         *  @return Number of miners removed
         *
         **/
        uint32_t CleanupInactive(uint64_t nTimeoutSec = 3600);

        /** CleanupExpiredSessions
         *
         *  Remove miners with expired sessions based on their session timeout.
         *
         *  @return Number of miners removed
         *
         **/
        uint32_t CleanupExpiredSessions();

        /** CleanupSessionScopedMaps
         *
         *  Remove rate limiters and session block maps for sessions that no longer
         *  exist in mapMiners.  Called from CleanupExpiredSessions() and directly
         *  from tests.
         *
         *  @return Number of entries removed (limiters + block maps combined)
         *
         **/
        uint32_t CleanupSessionScopedMaps();

        /** PurgeInactiveMiners
         *
         *  Purge miners that haven't sent keepalive within configured timeout.
         *  Uses different timeouts for localhost vs remote miners.
         *  This is the primary cache maintenance routine for DDOS protection.
         *
         *  @return Number of miners purged
         *
         **/
        uint32_t PurgeInactiveMiners();

        /** EnforceCacheLimit
         *
         *  Enforce the maximum cache size limit for DDOS protection.
         *  Removes oldest miners when cache exceeds limit.
         *
         *  @param[in] nMaxSize Maximum cache size (default: 500)
         *
         *  @return Number of miners removed
         *
         **/
        uint32_t EnforceCacheLimit(size_t nMaxSize = 500);

        /** CheckKeepaliveRequired
         *
         *  Check if a miner needs to send a keepalive.
         *
         *  @param[in] strAddress Miner address
         *  @param[in] nInterval Required keepalive interval in seconds
         *
         *  @return true if keepalive is required
         *
         **/
        bool CheckKeepaliveRequired(const std::string& strAddress, uint64_t nInterval = 86400) const;

        /** NotifyNewRound
         *
         *  Notify all tracked miners that a new mining round has started.
         *  Called when block height changes or chain state is updated.
         *  Updates internal tracking of chain state for template generation.
         *
         *  Mining Template Integration:
         *  - Tracks when templates become stale due to height changes
         *  - Provides efficient notification mechanism for connected miners
         *  - Supports both SOLO and Pool miners using NexusMiner
         *
         *  @param[in] nNewHeight The new block height
         *
         *  @return Number of miners notified
         *
         **/
        uint32_t NotifyNewRound(uint32_t nNewHeight);

        /** GetCurrentHeight
         *
         *  Get the current tracked block height.
         *
         *  @return Current block height being mined on
         *
         **/
        uint32_t GetCurrentHeight() const;

        /** SetCurrentHeight
         *
         *  Set the current tracked block height.
         *  Called when chain state updates.
         *
         *  @param[in] nHeight New block height
         *
         **/
        void SetCurrentHeight(uint32_t nHeight);

        /** IsNewRound
         *
         *  Check if a new round has started since last check.
         *  Used by miners to detect when templates are stale.
         *
         *  @param[in] nLastHeight The height the miner last worked on
         *
         *  @return true if a new round has started
         *
         **/
        bool IsNewRound(uint32_t nLastHeight) const;

        /** GetMinersForChannel
         *
         *  Get all miners mining on a specific channel.
         *  Useful for targeted template distribution.
         *
         *  @param[in] nChannel Mining channel (1=Prime, 2=Hash)
         *
         *  @return Vector of miner contexts for that channel
         *
         **/
        std::vector<MiningContext> GetMinersForChannel(uint32_t nChannel) const;

        /** GetTotalTemplatesServed
         *
         *  Get total number of templates served across all sessions.
         *  Uses atomic counter for lock-free access.
         *
         *  @return Total templates served count
         *
         **/
        uint64_t GetTotalTemplatesServed() const;

        /** IncrementTemplatesServed
         *
         *  Increment the templates served counter.
         *  Called each time a template is provided to a miner.
         *
         **/
        void IncrementTemplatesServed();

        /** GetTotalBlocksSubmitted
         *
         *  Get total number of blocks submitted across all sessions.
         *
         *  @return Total blocks submitted count
         *
         **/
        uint64_t GetTotalBlocksSubmitted() const;

        /** IncrementBlocksSubmitted
         *
         *  Increment the blocks submitted counter.
         *  Called each time a block is submitted by a miner.
         *
         **/
        void IncrementBlocksSubmitted();

        /** GetTotalBlocksAccepted
         *
         *  Get total number of blocks accepted across all sessions.
         *
         *  @return Total blocks accepted count
         *
         **/
        uint64_t GetTotalBlocksAccepted() const;

        /** IncrementBlocksAccepted
         *
         *  Increment the blocks accepted counter.
         *  Called each time a submitted block is accepted.
         *
         **/
        void IncrementBlocksAccepted();

        /** GetRewardAddress
         *
         *  Get the reward address (genesis hash) for a specific miner.
         *  Returns the hashGenesis if set, otherwise returns 0.
         *
         *  @param[in] strAddress Miner address
         *
         *  @return Genesis hash for rewards, or 0 if not set
         *
         **/
        uint256_t GetRewardAddress(const std::string& strAddress) const;

        /** GetRewardBoundCount
         *
         *  Get the number of miners with reward address explicitly bound.
         *  (Miners who have sent MINER_SET_REWARD)
         *
         *  @return Number of miners with reward address bound
         *
         **/
        size_t GetRewardBoundCount() const;

        /** GetMinerList
         *
         *  Get a list of all active miners with simplified information.
         *  Used for pool statistics and dashboard display.
         *
         *  @return Vector of MinerInfo structures
         *
         **/
        std::vector<MinerInfo> GetMinerList() const;

        /** ValidateMinerGenesis
         *
         *  Validate a miner's genesis hash and resolve default account.
         *  Used during authentication to validate genesis for auto-credit.
         *
         *  @param[in] strAddress Miner address
         *  @param[out] hashDefault Resolved default account address
         *
         *  @return ValidationResult from GenesisConstants
         *
         **/
        uint8_t ValidateMinerGenesis(const std::string& strAddress,
                                     TAO::Register::Address& hashDefault) const;


        /* ═══════════════════════════════════════════════════════════════════════
         * SIM-LINK SESSION-SCOPED SERVICES
         *
         * These APIs support the dual-lane architecture where a single authenticated
         * miner session is simultaneously active on both the legacy (8323) and
         * stateless (9323) lanes, sharing a single rate-limit budget and block
         * template store across both connection threads.
         * ═══════════════════════════════════════════════════════════════════════ */

        /** GetSessionRateLimiter
         *
         *  Get (or create) the session-scoped rolling rate limiter for a session.
         *  The returned shared_ptr is thread-safe; multiple connection threads may
         *  call Allow() concurrently — GetBlockRollingLimiter is internally locked.
         *
         *  This replaces the per-connection m_getBlockRollingLimiter for GET_BLOCK
         *  so that a miner with both a legacy and a stateless connection shares one
         *  20/60s budget across both lanes (SIM-LINK combined budget).
         *
         *  Key format used by callers: "session=N|combined"
         *
         *  @param[in] nSessionId Session identifier
         *
         *  @return Shared pointer to the session-scoped rate limiter (never null)
         *
         **/
        std::shared_ptr<GetBlockRollingLimiter> GetSessionRateLimiter(uint32_t nSessionId);

        /** GetSessionHeightSnapshot
         *
         *  Serve GET_HEIGHT from a session-aware cache/limiter pipeline.
         *
         *  - Uses one canonical full-height snapshot built from a single chain-state read
         *  - Rate-limits refresh pressure per authenticated session
         *  - Reuses a short global cache to avoid duplicate chain-state reads across lanes
         *    and across sessions
         *  - Maintains a short per-session cache so an over-budget session still receives
         *    the last canonical snapshot rather than triggering more chain-state reads
         *
         *  @param[in] nSessionId Session identifier
         *
         *  @return Snapshot, cache-hit metadata, latency, and freshness information
         *
         **/
        GetHeightResult GetSessionHeightSnapshot(uint32_t nSessionId);

        /** BuildBlockHeightPayload
         *
         *  Serialize a canonical GET_HEIGHT snapshot into the BLOCK_HEIGHT payload.
         *
         *  Wire format (16 bytes, big-endian uint32 fields):
         *    [0-3]   unified_height
         *    [4-7]   prime_height
         *    [8-11]  hash_height
         *    [12-15] stake_height
         *
         *  @param[in] snapshot Canonical height snapshot to serialize
         *
         *  @return 16-byte BLOCK_HEIGHT payload
         *
         **/
        static std::vector<uint8_t> BuildBlockHeightPayload(const GetHeightSnapshot& snapshot)
        {
            std::vector<uint8_t> vPayload;
            vPayload.reserve(16);

            const auto append = [&vPayload](uint32_t nHeight)
            {
                const auto vBytes = convert::uint2bytes(nHeight);
                vPayload.insert(vPayload.end(), vBytes.begin(), vBytes.end());
            };

            append(snapshot.nUnifiedHeight);
            append(snapshot.nPrimeHeight);
            append(snapshot.nHashHeight);
            append(snapshot.nStakeHeight);

            return vPayload;
        }

        /** ParseBlockHeightPayload
         *
         *  Decode the fixed-width BLOCK_HEIGHT payload back into a height snapshot.
         *
         *  @param[in] vPayload BLOCK_HEIGHT payload bytes
         *  @param[out] snapshot Decoded snapshot on success
         *
         *  @return true if vPayload is exactly 16 bytes and was decoded successfully
         *
         **/
        static bool ParseBlockHeightPayload(const std::vector<uint8_t>& vPayload, GetHeightSnapshot& snapshot)
        {
            if(vPayload.size() != 16)
                return false;

            snapshot = GetHeightSnapshot{};
            snapshot.nUnifiedHeight = convert::bytes2uint(vPayload, 0);
            snapshot.nPrimeHeight   = convert::bytes2uint(vPayload, 4);
            snapshot.nHashHeight    = convert::bytes2uint(vPayload, 8);
            snapshot.nStakeHeight   = convert::bytes2uint(vPayload, 12);
            return true;
        }

        /** StoreSessionBlock
         *
         *  Store a block template in the session-scoped cross-lane block map.
         *  Called by SharedGetBlockHandler after new_block() succeeds on either lane.
         *
         *  The session map uses shared_ptr ownership so both the legacy and stateless
         *  connection threads can safely access the same template, and the template
         *  survives for cross-lane SUBMIT_BLOCK resolution (a solution submitted on
         *  port 8323 can resolve a template issued on port 9323).
         *
         *  Thread-safe: protected by m_sessionBlockMutex.
         *
         *  @param[in] nSessionId      Session identifier
         *  @param[in] hashMerkleRoot  Block template merkle root (lookup key)
         *  @param[in] spBlock         Shared ownership of the block template
         *
         **/
        void StoreSessionBlock(uint32_t nSessionId, const uint512_t& hashMerkleRoot,
                               std::shared_ptr<TAO::Ledger::Block> spBlock);

        /** FindSessionBlock
         *
         *  Look up a block template in the session-scoped cross-lane block map.
         *  Used by SUBMIT_BLOCK handlers on either lane to resolve templates issued
         *  on the other lane (cross-lane resolution).
         *
         *  Thread-safe: protected by m_sessionBlockMutex.
         *
         *  @param[in] nSessionId      Session identifier
         *  @param[in] hashMerkleRoot  Block template merkle root to search for
         *
         *  @return Shared pointer to the block template, or nullptr if not found
         *
         **/
        std::shared_ptr<TAO::Ledger::Block> FindSessionBlock(uint32_t nSessionId,
                                                              const uint512_t& hashMerkleRoot);

        /** PruneSessionBlocks
         *
         *  Remove all stale block templates from the session-scoped block map for
         *  a given session.  Called when the chain tip advances (SendChannelNotification
         *  fires) to prevent unbounded growth of the session-scoped template store.
         *
         *  Thread-safe: protected by m_sessionBlockMutex.
         *
         *  @param[in] nSessionId  Session identifier whose templates to prune
         *
         **/
        void PruneSessionBlocks(uint32_t nSessionId);



    private:
        /** Private constructor for singleton **/
        StatelessMinerManager() = default;

        /** Thread-safe concurrent hash map of miner address to context **/
        util::ConcurrentHashMap<std::string, MiningContext> mapMiners;

        /** Index by Falcon key ID for efficient lookups **/
        util::ConcurrentHashMap<uint256_t, std::string> mapKeyToAddress;

        /** Index by session ID for efficient lookups **/
        util::ConcurrentHashMap<uint32_t, std::string> mapSessionToAddress;

        /** Index by genesis hash for GenesisHash reward mapping **/
        util::ConcurrentHashMap<uint256_t, std::string> mapGenesisToAddress;

        /** Track last known lane per miner address **/
        util::ConcurrentHashMap<std::string, uint8_t> mapAddressToLane;

        /** Atomic counter for total miners (lock-free stats) **/
        mutable std::atomic<size_t> nTotalMiners{0};

        /** Atomic counter for authenticated miners **/
        mutable std::atomic<size_t> nAuthenticatedMiners{0};

        /** Atomic counter for total keepalives processed **/
        mutable std::atomic<uint64_t> nTotalKeepalives{0};

        /** Atomic peak session count (high water mark) **/
        mutable std::atomic<size_t> nPeakSessions{0};

        /** Current tracked block height for template generation **/
        mutable std::atomic<uint32_t> nCurrentHeight{0};

        /** Atomic counter for total templates served **/
        mutable std::atomic<uint64_t> nTotalTemplatesServed{0};

        /** Atomic counter for total blocks submitted **/
        mutable std::atomic<uint64_t> nTotalBlocksSubmitted{0};

        /** Atomic counter for total blocks accepted **/
        mutable std::atomic<uint64_t> nTotalBlocksAccepted{0};

        /* ═══════════════════════════════════════════════════════════════════════
         * SIM-LINK SESSION-SCOPED STORAGE (private)
         * ═══════════════════════════════════════════════════════════════════════ */

        /** Mutex protecting session rate limiter map **/
        mutable std::mutex m_sessionLimiterMutex;

        /** Session-scoped rate limiters keyed by session ID.
         *  Each limiter enforces 20/60s GET_BLOCK budget shared across both lanes.
         *  Using shared_ptr so callers can hold a reference past the lock window. **/
        std::unordered_map<uint32_t, std::shared_ptr<GetBlockRollingLimiter>> m_mapSessionLimiters;

        /** Session-scoped GET_HEIGHT limiters keyed by session ID.
         *  Each limiter enforces a 4/60s GET_HEIGHT budget shared across both lanes. **/
        std::unordered_map<uint32_t, std::shared_ptr<GetBlockRollingLimiter>> m_mapSessionHeightLimiters;

        /** Mutex protecting session block template map **/
        mutable std::mutex m_sessionBlockMutex;

        /** Session-scoped block template storage for cross-lane SUBMIT_BLOCK resolution.
         *  Outer key: session ID.  Inner key: hashMerkleRoot.
         *  shared_ptr ownership allows both lane threads to safely access the template. **/
        std::unordered_map<uint32_t, std::map<uint512_t, std::shared_ptr<TAO::Ledger::Block>>> m_mapSessionBlocks;

        /** Mutex protecting GET_HEIGHT cache state. */
        mutable std::mutex m_sessionHeightCacheMutex;

        /** Global cached GET_HEIGHT snapshot shared across all sessions. */
        GetHeightCacheEntry m_globalHeightCache;

        /** Per-session cached GET_HEIGHT snapshots for over-budget fallback. */
        std::unordered_map<uint32_t, GetHeightCacheEntry> m_mapSessionHeightCache;

        /** Shared GET_HEIGHT instrumentation counters. */
        mutable std::atomic<uint64_t> nGetHeightRequests{0};
        mutable std::atomic<uint64_t> nGetHeightCacheHits{0};
        mutable std::atomic<uint64_t> nGetHeightRateLimited{0};
    };

} // namespace LLP

#endif
