/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To The Voice of The People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_INCLUDE_CHANNEL_TEMPLATE_CACHE_H
#define NEXUS_LLP_INCLUDE_CHANNEL_TEMPLATE_CACHE_H

#include <TAO/Ledger/include/chainstate.h>
#include <LLC/types/uint1024.h>
#include <LLC/hash/SK.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <shared_mutex>

/* Forward declaration */
namespace TAO { namespace Ledger { class TritiumBlock; } }

namespace LLP
{

    /** CachedChannelTemplate
     *
     *  Holds a pre-built, wallet-signed mining template and the metadata needed
     *  to detect staleness and serve the 228-byte wire response.
     *
     *  Thread Safety:
     *  Protected by the owning ChannelTemplateCache's m_mutex.  Reads are
     *  gated behind a shared (read) lock; writes behind an exclusive lock.
     *
     **/
    struct CachedChannelTemplate
    {
        /** Pre-built block template (wallet-signed, ready to mine). */
        std::shared_ptr<TAO::Ledger::TritiumBlock> pBlock;

        /** Unified chain-tip hash captured at build time.
         *  Compared against ChainState::hashBestChain to detect staleness. */
        uint1024_t hashBestChainAtBuild;

        /** Reward address the template was built for.
         *  Used to determine whether a requesting miner can share this template. */
        uint256_t  hashRewardAddress;

        /** Mining channel (1 = Prime, 2 = Hash). */
        uint32_t   nChannel;

        /** Default constructor — leaves cache in empty/invalid state. */
        CachedChannelTemplate()
            : pBlock(nullptr)
            , hashBestChainAtBuild(0)
            , hashRewardAddress(0)
            , nChannel(0)
        {
        }

        /** IsValid
         *
         *  True when the cached block's hashPrevBlock still matches the live chain tip.
         *  This is the single authoritative staleness check at GET_BLOCK time.
         *
         *  No height re-validation is performed here — the only authoritative
         *  staleness gate is at SUBMIT_BLOCK: hashPrevBlock != hashBestChain → BLOCK_REJECTED.
         *
         **/
        bool IsValid() const
        {
            return pBlock != nullptr &&
                   hashBestChainAtBuild != uint1024_t(0) &&
                   hashBestChainAtBuild == TAO::Ledger::ChainState::hashBestChain.load();
        }
    };


    /** ChannelTemplateCache
     *
     *  Server-level, per-channel template store.  One singleton per PoW channel
     *  (Prime = 1, Hash = 2).
     *
     *  Design Goals:
     *  -------------
     *  - Reduce new_block() calls from (N_connections × tip_advances) to
     *    (1 × tip_advances) per channel.
     *  - GET_BLOCK hot path is near-zero-latency when the cache is warm.
     *  - std::shared_mutex gives writer-exclusive / reader-concurrent semantics:
     *      * GetCurrent()  acquires a shared lock — N simultaneous GET_BLOCKs can read.
     *      * Rebuild()     acquires an exclusive lock — one write at a time.
     *  - shared_ptr reference counting keeps the block alive for in-flight responses
     *    even if a new Rebuild() fires concurrently.
     *  - An atomic_flag try-lock prevents redundant concurrent Rebuild() calls: if
     *    a rebuild is already in progress for a channel, a second call returns
     *    immediately (the in-progress result will be correct for the current tip).
     *
     *  Reward Address Policy:
     *  ----------------------
     *  The cache stores the reward address of the miner who triggered the build.
     *  GET_BLOCK checks that the requesting miner's reward address matches the
     *  cached template's reward address before serving from cache.  If they differ,
     *  the connection falls through to the per-connection new_block() path (no
     *  regression for solo miners; pool miners — who share one reward address —
     *  benefit fully).
     *
     *  A node-level "last seen reward address" is also maintained per channel.
     *  Rebuild() uses this when called from DispatchPushEvent(), where no
     *  per-connection context is available.
     *
     **/
    class ChannelTemplateCache
    {
    public:

        /** Prime
         *
         *  Singleton accessor for the Prime (channel 1) cache.
         *
         **/
        static ChannelTemplateCache& Prime();


        /** Hash
         *
         *  Singleton accessor for the Hash (channel 2) cache.
         *
         **/
        static ChannelTemplateCache& Hash();


        /** ForChannel
         *
         *  Return the singleton for a given channel number (1 or 2).
         *  Returns the Prime cache for any other value.
         *
         **/
        static ChannelTemplateCache& ForChannel(uint32_t nChannel);


        /** Rebuild
         *
         *  Build a fresh template for this channel using the supplied reward address
         *  and store it atomically.  Called from the push-notification path after a
         *  tip advance so that subsequent GET_BLOCK requests are served instantly.
         *
         *  If a rebuild is already in progress (atomic_flag busy), the call returns
         *  immediately — the in-progress rebuild will produce a valid result.
         *
         *  @param[in] nChannel         Mining channel (1 = Prime, 2 = Hash).
         *  @param[in] hashRewardAddress Reward address to embed in the coinbase.
         *
         **/
        void Rebuild(uint32_t nChannel, const uint256_t& hashRewardAddress);


        /** GetCurrent
         *
         *  Return the currently cached block template if it is still valid
         *  (hashPrevBlock == hashBestChain), or nullptr on cache miss.
         *
         *  Acquires a shared lock — safe to call from N concurrent GET_BLOCK handlers.
         *
         *  @return shared_ptr to the cached TritiumBlock, or nullptr if stale/empty.
         *
         **/
        std::shared_ptr<TAO::Ledger::TritiumBlock> GetCurrent() const;


        /** GetCurrentForReward
         *
         *  Return the cached block if valid AND the template's reward address
         *  matches the requesting miner's reward address.
         *
         *  Returns nullptr if the cache is empty, stale, or the reward address
         *  does not match (caller should fall through to per-connection new_block()).
         *
         *  @param[in]  hashRewardAddress Requesting miner's bound reward address.
         *  @return shared_ptr to the cached TritiumBlock, or nullptr on miss/mismatch.
         *
         **/
        std::shared_ptr<TAO::Ledger::TritiumBlock> GetCurrentForReward(
            const uint256_t& hashRewardAddress) const;


        /** StoreRewardAddress
         *
         *  Record the most-recently seen reward address for this channel.
         *  Called when a miner authenticates and binds a reward address so that
         *  future Rebuild() calls triggered from DispatchPushEvent() (which has no
         *  per-connection context) can use a meaningful address.
         *
         *  @param[in] hashRewardAddress The miner's bound reward address.
         *
         **/
        void StoreRewardAddress(const uint256_t& hashRewardAddress);


        /** GetLastRewardAddress
         *
         *  Retrieve the most-recently stored reward address for this channel.
         *  Returns uint256_t(0) if no miner has bound a reward address yet.
         *
         **/
        uint256_t GetLastRewardAddress() const;


        /** Invalidate
         *
         *  Clear the cache immediately (e.g., on node shutdown or test reset).
         *
         **/
        void Invalidate();

    private:

        /** Cached template (pBlock, hashBestChainAtBuild, hashRewardAddress, nChannel). */
        mutable std::shared_mutex m_mutex;
        CachedChannelTemplate     m_cache;

        /** Atomic flag used as a try-lock to prevent concurrent Rebuild() calls.
         *  test_and_set() returns true when a rebuild is already in progress. */
        std::atomic_flag m_rebuildInProgress = ATOMIC_FLAG_INIT;

        /** Most-recently seen reward address for this channel.
         *  Written under m_mutex exclusive lock; read under m_mutex shared lock. */
        uint256_t m_lastRewardAddress;
    };


    /* ──────────────────────── Observability metrics ───────────────────────── */

    /** g_channel_cache_hits_total
     *
     *  Incremented on every GET_BLOCK request served from the server-level cache.
     *
     **/
    extern std::atomic<uint64_t> g_channel_cache_hits_total;


    /** g_channel_cache_misses_total
     *
     *  Incremented on every GET_BLOCK request that falls through to per-connection
     *  new_block() (cache empty, stale, or reward-address mismatch).
     *
     **/
    extern std::atomic<uint64_t> g_channel_cache_misses_total;

} // namespace LLP

#endif // NEXUS_LLP_INCLUDE_CHANNEL_TEMPLATE_CACHE_H
