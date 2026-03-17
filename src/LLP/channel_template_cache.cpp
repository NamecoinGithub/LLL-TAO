/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To The Voice of The People

____________________________________________________________________________________________*/

#include <LLP/include/channel_template_cache.h>

#include <TAO/Ledger/include/stateless_block_utility.h>
#include <TAO/Ledger/include/chainstate.h>
#include <TAO/Ledger/types/tritium.h>
#include <TAO/API/types/authentication.h>
#include <TAO/Ledger/types/pinunlock.h>

#include <Util/include/debug.h>
#include <Util/include/config.h>
#include <Util/include/runtime.h>

#include <atomic>
#include <memory>
#include <shared_mutex>

namespace LLP
{

    /* ──────────────────────────── Metrics ─────────────────────────────────── */

    std::atomic<uint64_t> g_channel_cache_hits_total{0};
    std::atomic<uint64_t> g_channel_cache_misses_total{0};


    /* ─────────────────────── Singleton accessors ───────────────────────────── */

    ChannelTemplateCache& ChannelTemplateCache::Prime()
    {
        static ChannelTemplateCache s_prime;
        return s_prime;
    }


    ChannelTemplateCache& ChannelTemplateCache::Hash()
    {
        static ChannelTemplateCache s_hash;
        return s_hash;
    }


    ChannelTemplateCache& ChannelTemplateCache::ForChannel(uint32_t nChannel)
    {
        return (nChannel == 2) ? Hash() : Prime();
    }


    /* ────────────────────────── StoreRewardAddress ─────────────────────────── */

    void ChannelTemplateCache::StoreRewardAddress(const uint256_t& hashRewardAddress)
    {
        if(hashRewardAddress == uint256_t(0))
            return;

        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_lastRewardAddress = hashRewardAddress;
    }


    /* ────────────────────────── GetLastRewardAddress ────────────────────────── */

    uint256_t ChannelTemplateCache::GetLastRewardAddress() const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_lastRewardAddress;
    }


    /* ─────────────────────────────── Rebuild ──────────────────────────────── */

    void ChannelTemplateCache::Rebuild(uint32_t nChannel, const uint256_t& hashRewardAddress)
    {
        /* Validate channel */
        if(nChannel != 1 && nChannel != 2)
        {
            debug::error(FUNCTION, "ChannelTemplateCache::Rebuild called with invalid channel ", nChannel);
            return;
        }

        /* Validate reward address */
        if(hashRewardAddress == uint256_t(0))
        {
            debug::log(2, FUNCTION, "[", (nChannel == 1 ? "Prime" : "Hash"),
                       "] Skipping rebuild: zero reward address");
            return;
        }

        /* Atomic try-lock: if a rebuild is already in progress for this channel, bail out.
         * The in-progress rebuild will produce a valid result for the current tip. */
        if(m_rebuildInProgress.test_and_set(std::memory_order_acquire))
        {
            debug::log(2, FUNCTION, "[", (nChannel == 1 ? "Prime" : "Hash"),
                       "] Rebuild already in progress — skipping duplicate");
            return;
        }

        /* RAII: clear the in-progress flag on exit (normal or exceptional). */
        struct FlagGuard
        {
            std::atomic_flag& flag;
            ~FlagGuard() { flag.clear(std::memory_order_release); }
        } guard{m_rebuildInProgress};

        /* Exit early if shutting down. */
        if(config::fShutdown.load())
        {
            debug::log(1, FUNCTION, "[", (nChannel == 1 ? "Prime" : "Hash"),
                       "] Shutdown in progress — skipping rebuild");
            return;
        }

        const char* strCh = (nChannel == 1) ? "Prime" : "Hash";
        debug::log(1, FUNCTION, "[", strCh, "] Rebuilding server-level template"
                   " (hashRewardAddress=", hashRewardAddress.SubString(8), ")");

        /* Ensure the wallet is unlocked for mining (mirrors stateless_miner_connection new_block() logic). */
        if(!TAO::API::Authentication::Unlocked(TAO::Ledger::PinUnlock::MINING))
        {
            try
            {
                SecureString strPIN;
                RECURSIVE(TAO::API::Authentication::Unlock(strPIN, TAO::Ledger::PinUnlock::MINING));
                debug::log(1, FUNCTION, "[", strCh, "] Wallet auto-unlocked for mining");
            }
            catch(const std::exception& e)
            {
                debug::error(FUNCTION, "[", strCh, "] Mining unlock failed: ", e.what());
                return;
            }
        }

        /* Grab a monotonically increasing extra-nonce so each template has a unique merkle root. */
        static std::atomic<uint64_t> s_nExtraNonce{0};
        const uint64_t nExtraNonce = ++s_nExtraNonce;

        /* Build the block template. */
        TAO::Ledger::TritiumBlock* pRaw =
            TAO::Ledger::CreateBlockForStatelessMining(nChannel, nExtraNonce, hashRewardAddress);

        if(!pRaw)
        {
            debug::error(FUNCTION, "[", strCh, "] CreateBlockForStatelessMining returned nullptr — cache NOT updated");
            return;
        }

        /* Wrap in a shared_ptr so concurrent readers that already hold a reference
         * to the old block keep it alive until they finish. */
        std::shared_ptr<TAO::Ledger::TritiumBlock> pNew(pRaw);

        /* Snapshot the chain tip AFTER block creation so we don't race a block
         * accepted between CreateBlock and our cache write. */
        const uint1024_t hashTip = TAO::Ledger::ChainState::hashBestChain.load();

        /* Validate: the new block's hashPrevBlock must match the tip we snapshotted. */
        if(pNew->hashPrevBlock != hashTip)
        {
            debug::log(1, FUNCTION, "[", strCh,
                       "] Chain advanced during rebuild — discarding template"
                       " (hashPrevBlock=", pNew->hashPrevBlock.SubString(),
                       " vs tip=", hashTip.SubString(), ")");
            return;
        }

        /* Commit atomically under exclusive lock. */
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_cache.pBlock               = pNew;
            m_cache.hashBestChainAtBuild = hashTip;
            m_cache.hashRewardAddress    = hashRewardAddress;
            m_cache.nChannel             = nChannel;
            m_lastRewardAddress          = hashRewardAddress;
        }

        debug::log(1, FUNCTION, "[", strCh, "] Cache updated"
                   " unified=", pNew->nHeight,
                   " merkle=", pNew->hashMerkleRoot.SubString(),
                   " tip=", hashTip.SubString());
    }


    /* ─────────────────────────── GetCurrent ──────────────────────────────── */

    std::shared_ptr<TAO::Ledger::TritiumBlock> ChannelTemplateCache::GetCurrent() const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if(!m_cache.IsValid())
            return nullptr;
        return m_cache.pBlock;
    }


    /* ─────────────────────── GetCurrentForReward ─────────────────────────── */

    std::shared_ptr<TAO::Ledger::TritiumBlock>
    ChannelTemplateCache::GetCurrentForReward(const uint256_t& hashRewardAddress) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        if(!m_cache.IsValid())
            return nullptr;

        /* Reward-address gate: only serve the cached template to miners whose
         * reward address matches.  Pool miners all share one address → full cache
         * benefit.  Solo miners with unique addresses fall through to new_block(). */
        if(m_cache.hashRewardAddress != hashRewardAddress)
            return nullptr;

        return m_cache.pBlock;
    }


    /* ─────────────────────────── Invalidate ──────────────────────────────── */

    void ChannelTemplateCache::Invalidate()
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache = CachedChannelTemplate();
        debug::log(2, FUNCTION, "ChannelTemplateCache invalidated");
    }

} // namespace LLP
