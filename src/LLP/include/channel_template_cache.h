/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_INCLUDE_CHANNEL_TEMPLATE_CACHE_H
#define NEXUS_LLP_INCLUDE_CHANNEL_TEMPLATE_CACHE_H

#include <TAO/Ledger/types/tritium.h>
#include <LLC/types/uint1024.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>


namespace LLP
{
    /** CacheState
     *
     *  Three-state machine for ChannelTemplateCache:
     *
     *    EMPTY    — No template yet (node startup or first build pending).
     *               GetCurrent() returns nullptr immediately; caller falls back
     *               to per-connection new_block().
     *
     *    BUILDING — Rebuild in progress.  GetCurrent() waits up to nMaxWait
     *               for the rebuild to complete before falling back to new_block().
     *               All concurrent waiters share a single condition_variable so
     *               exactly one notify_all() wakes every blocked GET_BLOCK handler.
     *
     *    IDLE     — Cache valid and serving normally.  GetCurrent() acquires a
     *               shared lock, validates IsValid(), and returns the cached block
     *               in the common case without blocking writers.
     *
     **/
    enum class CacheState : uint8_t
    {
        IDLE     = 0,   // Cache valid, serving normally
        BUILDING = 1,   // Rebuild in progress — new GET_BLOCK requests should wait
        EMPTY    = 2,   // No template yet (node startup)
    };


    /** ChannelTemplateCacheEntry
     *
     *  Inner data structure for the cached block template.
     *  Owned exclusively by ChannelTemplateCache (under m_mutex).
     *
     **/
    struct ChannelTemplateCacheEntry
    {
        /** The cached block template (shared ownership so GetCurrent() callers can hold it). */
        std::shared_ptr<TAO::Ledger::TritiumBlock> pBlock;

        /** hashBestChain at the time this template was built.
         *  IsValid() returns false if the live hashBestChain has advanced. */
        uint1024_t hashBestChainAtBuild{0};

        /** Unified timestamp (seconds) when this entry was built. */
        uint64_t nCreationTime{0};

        /** Returns true when the template is still anchored to the current chain tip. */
        bool IsValid() const;
    };


    /** ChannelTemplateCache
     *
     *  Server-level, per-channel block template cache with a rebuild-in-progress guard.
     *
     *  Purpose
     *  -------
     *  When SetBest() fires (any new block on any channel), the unified chain tip
     *  advances and every existing template becomes stale (hashPrevBlock anchor).
     *  Without a shared cache every concurrent GET_BLOCK from N miners spawns N
     *  independent CreateBlockForStatelessMining() calls — expensive wallet-signing
     *  work replicated N-fold.
     *
     *  ChannelTemplateCache reduces this to one CreateBlockForStatelessMining() call
     *  per channel per tip advance.  Concurrent GET_BLOCK requests that arrive while
     *  Rebuild() is running WAIT (up to CACHE_REBUILD_WAIT_MS) on m_rebuild_cv, then
     *  all wake simultaneously via notify_all() and share the single new template.
     *
     *  State Machine
     *  -------------
     *    EMPTY  ──(first Rebuild())──▶  BUILDING ──(complete)──▶  IDLE
     *                                       ▲                        │
     *                                       └────(next SetBest())────┘
     *
     *  Usage
     *  -----
     *  Call GetChannelCache(nChannel) to obtain the singleton for a given channel.
     *  Call Rebuild(hashRewardAddress) from SetBest() or the first GET_BLOCK handler.
     *  Call GetCurrent() from every GET_BLOCK handler before falling back to new_block().
     *
     *  Thread safety
     *  -------------
     *  All public methods are thread-safe.  Rebuild() and GetCurrent() may be called
     *  concurrently from any number of threads.
     *
     **/
    class ChannelTemplateCache
    {
    public:

        /** Constructor.
         *
         *  @param[in] nChannel Mining channel this cache serves (1=Prime, 2=Hash).
         **/
        explicit ChannelTemplateCache(uint32_t nChannel);


        /** Destructor. */
        ~ChannelTemplateCache() = default;


        /* Non-copyable and non-movable — singletons by channel. */
        ChannelTemplateCache(const ChannelTemplateCache&)            = delete;
        ChannelTemplateCache& operator=(const ChannelTemplateCache&) = delete;
        ChannelTemplateCache(ChannelTemplateCache&&)                  = delete;
        ChannelTemplateCache& operator=(ChannelTemplateCache&&)       = delete;


        /** Rebuild
         *
         *  Build a new block template for this channel and atomically swap it
         *  into the cache.
         *
         *  State transitions:
         *    IDLE/EMPTY → BUILDING  (compare_exchange; returns immediately if already BUILDING)
         *    BUILDING   → IDLE      (on success, after storing the new template)
         *    BUILDING   → EMPTY     (on CreateBlockForStatelessMining failure)
         *
         *  Notifies all threads blocked in GetCurrent() via notify_all() when done.
         *
         *  @param[in] hashRewardAddress  Reward address to embed in the new template.
         **/
        void Rebuild(const uint256_t& hashRewardAddress);


        /** GetCurrent
         *
         *  Return the cached block template, waiting if a rebuild is in progress.
         *
         *  Fast path (IDLE + IsValid()):
         *    Acquires shared lock, validates, returns shared_ptr immediately.
         *
         *  BUILDING path:
         *    Parks on m_rebuild_cv for up to nMaxWait.  On wake:
         *      - If rebuild completed and cache is now valid → returns the new template.
         *      - If timeout expired → returns nullptr (caller falls back to new_block()).
         *
         *  EMPTY path:
         *    Returns nullptr immediately (node startup — caller uses new_block()).
         *
         *  @param[in] nMaxWait  Maximum time to wait for an in-progress rebuild.
         *                       Defaults to 500 ms (CACHE_REBUILD_WAIT_MS).
         *
         *  @return Shared pointer to the cached TritiumBlock, or nullptr on miss/timeout.
         **/
        std::shared_ptr<TAO::Ledger::TritiumBlock> GetCurrent(
            std::chrono::milliseconds nMaxWait = std::chrono::milliseconds(500)) const;


        /** GetChannel
         *
         *  @return The mining channel (1=Prime, 2=Hash) served by this cache instance.
         **/
        uint32_t GetChannel() const { return m_nChannel; }


        /** GetState
         *
         *  Returns the current cache state for diagnostics.
         **/
        CacheState GetState() const { return m_state.load(); }


#ifdef UNIT_TESTS
        /* ──────────────────────────────────────────────────────────────────────
         * Test-only helpers — compiled only when UNIT_TESTS is defined.
         * These bypass Rebuild() to directly manipulate internal state so that
         * unit tests can exercise the state-machine without a live blockchain.
         * ─────────────────────────────────────────────────────────────────────*/

        /** TEST_ForceBuilding — Atomically set state to BUILDING. */
        void TEST_ForceBuilding()
        {
            m_state.store(CacheState::BUILDING, std::memory_order_release);
        }

        /** TEST_ForceIdle — Store a synthetic entry and transition to IDLE, then
         *  wake all waiters (simulates a successful Rebuild() completion). */
        void TEST_ForceIdle(std::shared_ptr<TAO::Ledger::TritiumBlock> pBlock,
                            const uint1024_t& hashBestChain)
        {
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_cache.pBlock              = pBlock;
                m_cache.hashBestChainAtBuild= hashBestChain;
                m_cache.nCreationTime       = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
            }
            m_state.store(CacheState::IDLE, std::memory_order_release);
            m_rebuild_cv.notify_all();
        }

        /** TEST_NotifyAll — Wake all GetCurrent() waiters without changing state.
         *  Useful to test wakeup-with-still-invalid-cache paths. */
        void TEST_NotifyAll()
        {
            m_rebuild_cv.notify_all();
        }

        /** TEST_Reset — Return cache to EMPTY state and clear the entry. */
        void TEST_Reset()
        {
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_cache.pBlock.reset();
                m_cache.hashBestChainAtBuild = uint1024_t(0);
                m_cache.nCreationTime        = 0;
            }
            m_state.store(CacheState::EMPTY, std::memory_order_release);
        }
#endif /* UNIT_TESTS */


    private:

        /** Mining channel (1=Prime, 2=Hash). */
        uint32_t m_nChannel;

        /** Cached template entry.  Protected by m_mutex (shared for reads, exclusive for writes). */
        ChannelTemplateCacheEntry m_cache;

        /** Shared mutex — allows many concurrent readers in GetCurrent() IDLE fast path
         *  while Rebuild() holds an exclusive lock to swap in the new template. */
        mutable std::shared_mutex m_mutex;

        /** Mutex paired with m_rebuild_cv.
         *  Must be std::mutex (not shared_mutex) for std::condition_variable. */
        mutable std::mutex m_wait_mutex;

        /** Condition variable signalled (notify_all) by Rebuild() on BUILDING → IDLE/EMPTY
         *  transition.  GetCurrent() blocks here when state == BUILDING. */
        mutable std::condition_variable m_rebuild_cv;

        /** Current cache state.  Written atomically by Rebuild(); read atomically by GetCurrent(). */
        std::atomic<CacheState> m_state{CacheState::EMPTY};
    };


    /** GetChannelCache
     *
     *  Returns the singleton ChannelTemplateCache for the given mining channel.
     *  Thread-safe: C++11 guarantees static local initialization is done exactly once.
     *
     *  @param[in] nChannel  Mining channel (1=Prime, 2=Hash).
     *
     *  @return Reference to the per-channel singleton.
     **/
    ChannelTemplateCache& GetChannelCache(uint32_t nChannel);


    /* ───────────────────────────────────────────────────────────────────────────
     * Observability metrics (defined in channel_template_cache.cpp)
     *
     *  g_channel_cache_hits_total            — GetCurrent() served IDLE valid template
     *  g_channel_cache_misses_total          — GetCurrent() returned nullptr (EMPTY / timeout)
     *  g_channel_cache_rebuild_waits_total   — GetCurrent() waited on BUILDING and got result
     *  g_channel_cache_rebuild_timeouts_total — GetCurrent() waited on BUILDING and timed out
     * ─────────────────────────────────────────────────────────────────────────*/
    extern std::atomic<uint64_t> g_channel_cache_hits_total;
    extern std::atomic<uint64_t> g_channel_cache_misses_total;
    extern std::atomic<uint64_t> g_channel_cache_rebuild_waits_total;
    extern std::atomic<uint64_t> g_channel_cache_rebuild_timeouts_total;

} // namespace LLP

#endif
