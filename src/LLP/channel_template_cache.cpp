/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLP/include/channel_template_cache.h>
#include <LLP/include/mining_constants.h>

#include <TAO/Ledger/include/stateless_block_utility.h>
#include <TAO/Ledger/include/chainstate.h>

#include <Util/include/debug.h>
#include <Util/include/runtime.h>

#include <atomic>


namespace LLP
{
    /* ─────────────────────────────────────────────────────────────────────────
     * Module-level observability metrics
     * ─────────────────────────────────────────────────────────────────────────*/

    std::atomic<uint64_t> g_channel_cache_hits_total{0};
    std::atomic<uint64_t> g_channel_cache_misses_total{0};
    std::atomic<uint64_t> g_channel_cache_rebuild_waits_total{0};
    std::atomic<uint64_t> g_channel_cache_rebuild_timeouts_total{0};


    /* Monotonically-increasing extra-nonce counter shared across all Rebuild() calls. */
    namespace
    {
        std::atomic<uint64_t> g_rebuild_extra_nonce{0};
    }


    /* ─────────────────────────────────────────────────────────────────────────
     * ChannelTemplateCacheEntry
     * ─────────────────────────────────────────────────────────────────────────*/

    /** IsValid — Returns true when pBlock is non-null and the cache is still anchored
     *            to the current best chain tip. */
    bool ChannelTemplateCacheEntry::IsValid() const
    {
        if(!pBlock)
            return false;

        if(hashBestChainAtBuild == uint1024_t(0))
            return false;

        return hashBestChainAtBuild == TAO::Ledger::ChainState::hashBestChain.load();
    }


    /* ─────────────────────────────────────────────────────────────────────────
     * ChannelTemplateCache
     * ─────────────────────────────────────────────────────────────────────────*/

    ChannelTemplateCache::ChannelTemplateCache(uint32_t nChannel)
        : m_nChannel  (nChannel)
        , m_cache     ()
        , m_mutex     ()
        , m_wait_mutex()
        , m_rebuild_cv()
        , m_state     {CacheState::EMPTY}
    {
    }


    /** Rebuild — State machine:
     *
     *    IDLE/EMPTY → BUILDING  (atomic compare_exchange; abort if already BUILDING)
     *    BUILDING   → IDLE      (success: new template stored)
     *    BUILDING   → EMPTY     (failure: CreateBlockForStatelessMining returned nullptr)
     *
     *  Notifies all threads waiting in GetCurrent() via notify_all() on completion.
     **/
    void ChannelTemplateCache::Rebuild(const uint256_t& hashRewardAddress)
    {
        /* ── Step 1: Transition IDLE → BUILDING (or EMPTY → BUILDING). ─────────
         * If another thread is already BUILDING, return immediately — one rebuild
         * is sufficient for all concurrent waiters. */
        CacheState expected = CacheState::IDLE;
        if(!m_state.compare_exchange_strong(expected, CacheState::BUILDING,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire))
        {
            expected = CacheState::EMPTY;
            if(!m_state.compare_exchange_strong(expected, CacheState::BUILDING,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire))
            {
                /* State is BUILDING — another Rebuild() is already in progress. */
                debug::log(2, FUNCTION, "ChannelTemplateCache ch=", m_nChannel,
                           " already BUILDING — skipping duplicate rebuild");
                return;
            }
        }

        debug::log(1, FUNCTION, "ChannelTemplateCache ch=", m_nChannel,
                   " BUILDING — calling CreateBlockForStatelessMining");

        /* ── Step 2: Build the new template (expensive: wallet sign + chain I/O). ──*/
        const uint64_t nExtraNonce = g_rebuild_extra_nonce.fetch_add(1, std::memory_order_relaxed) + 1;

        TAO::Ledger::TritiumBlock* pNew =
            TAO::Ledger::CreateBlockForStatelessMining(m_nChannel, nExtraNonce, hashRewardAddress);

        /* ── Step 3: Atomically swap the new template in (exclusive lock). ─────────*/
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if(pNew)
            {
                m_cache.pBlock              = std::shared_ptr<TAO::Ledger::TritiumBlock>(pNew);
                m_cache.hashBestChainAtBuild= TAO::Ledger::ChainState::hashBestChain.load();
                m_cache.nCreationTime       = static_cast<uint64_t>(runtime::unifiedtimestamp());
                debug::log(1, FUNCTION, "ChannelTemplateCache ch=", m_nChannel,
                           " stored new template height=", pNew->nHeight,
                           " hashPrevBlock=", pNew->hashPrevBlock.SubString());
            }
            else
            {
                /* Build failed — clear the stale entry so EMPTY is the honest state. */
                m_cache.pBlock.reset();
                m_cache.hashBestChainAtBuild = uint1024_t(0);
                m_cache.nCreationTime        = 0;
                debug::error(FUNCTION, "ChannelTemplateCache ch=", m_nChannel,
                             " CreateBlockForStatelessMining returned nullptr — cache EMPTY");
            }
        }

        /* ── Step 4: Transition BUILDING → IDLE or EMPTY, then wake all waiters. ──*/
        m_state.store(pNew ? CacheState::IDLE : CacheState::EMPTY,
                      std::memory_order_release);
        m_rebuild_cv.notify_all();

        debug::log(1, FUNCTION, "ChannelTemplateCache ch=", m_nChannel,
                   " transition BUILDING → ", (pNew ? "IDLE" : "EMPTY"),
                   " — notified all waiters");
    }


    /** GetCurrent — Serve the cached template with wait-on-rebuild guard.
     *
     *  Fast path (IDLE + valid):
     *    Shared lock, immediate return.
     *
     *  BUILDING path:
     *    Wait up to nMaxWait on m_rebuild_cv.  If rebuild finishes in time and
     *    the new template is valid, return it (zero extra new_block() calls).
     *    If the wait times out, return nullptr (caller falls back to new_block()).
     *
     *  EMPTY path:
     *    Return nullptr immediately (node startup — caller uses new_block()).
     **/
    std::shared_ptr<TAO::Ledger::TritiumBlock> ChannelTemplateCache::GetCurrent(
        std::chrono::milliseconds nMaxWait) const
    {
        /* ── Fast path: IDLE and valid ──────────────────────────────────────────*/
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if(m_state.load(std::memory_order_acquire) == CacheState::IDLE &&
               m_cache.IsValid())
            {
                ++g_channel_cache_hits_total;
                return m_cache.pBlock;
            }
        }

        /* ── BUILDING path: wait for the rebuild to complete ────────────────────*/
        if(m_state.load(std::memory_order_acquire) == CacheState::BUILDING)
        {
            std::unique_lock<std::mutex> waitLock(m_wait_mutex);
            const bool fReady = m_rebuild_cv.wait_for(waitLock, nMaxWait, [this]() {
                return m_state.load(std::memory_order_acquire) != CacheState::BUILDING;
            });

            if(fReady)
            {
                /* Rebuild completed — check if the new template is usable. */
                std::shared_lock<std::shared_mutex> lock(m_mutex);
                if(m_cache.IsValid())
                {
                    ++g_channel_cache_rebuild_waits_total;
                    debug::log(1, FUNCTION, "ChannelTemplateCache ch=", m_nChannel,
                               " wait satisfied after rebuild — serving cached template");
                    return m_cache.pBlock;
                }
                /* Rebuild completed but cache is not valid (build failed). */
            }
            else
            {
                /* Timeout expired — fall through to per-connection new_block(). */
                ++g_channel_cache_rebuild_timeouts_total;
                debug::log(1, FUNCTION, "ChannelTemplateCache ch=", m_nChannel,
                           " rebuild wait timed out after ", nMaxWait.count(),
                           "ms — falling through to per-connection new_block()");
            }
        }

        /* ── EMPTY or timeout: caller falls back to per-connection new_block(). ─*/
        ++g_channel_cache_misses_total;
        return nullptr;
    }


    /* ─────────────────────────────────────────────────────────────────────────
     * GetChannelCache — Per-channel singletons
     * ─────────────────────────────────────────────────────────────────────────*/

    /** GetChannelCache — Returns the singleton ChannelTemplateCache for nChannel.
     *  C++11 static-local initialization is thread-safe and guaranteed once. */
    ChannelTemplateCache& GetChannelCache(uint32_t nChannel)
    {
        /* Channel 1 = Prime */
        if(nChannel == 1)
        {
            static ChannelTemplateCache sPrimeCache(1);
            return sPrimeCache;
        }

        /* Channel 2 = Hash (default for any other value including 2) */
        static ChannelTemplateCache sHashCache(2);
        return sHashCache;
    }

} // namespace LLP
