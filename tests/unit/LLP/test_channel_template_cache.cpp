/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <LLP/include/channel_template_cache.h>
#include <TAO/Ledger/include/chainstate.h>
#include <TAO/Ledger/types/tritium.h>

#include <LLC/types/uint1024.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using namespace LLP;


/* ─────────────────────────────────── Helpers ─────────────────────────────── */

/** Forge a fake best-chain hash into ChainState for unit testing. */
static void set_fake_chain_tip(const uint1024_t& hash)
{
    TAO::Ledger::ChainState::hashBestChain.store(hash);
}


/* ─────────────────────────── Test Suite ──────────────────────────────────── */

TEST_CASE("ChannelTemplateCache - singleton accessors", "[channel_template_cache]")
{
    SECTION("Prime() and Hash() return distinct singletons")
    {
        REQUIRE(&ChannelTemplateCache::Prime() != &ChannelTemplateCache::Hash());
    }

    SECTION("ForChannel(1) returns Prime singleton")
    {
        REQUIRE(&ChannelTemplateCache::ForChannel(1) == &ChannelTemplateCache::Prime());
    }

    SECTION("ForChannel(2) returns Hash singleton")
    {
        REQUIRE(&ChannelTemplateCache::ForChannel(2) == &ChannelTemplateCache::Hash());
    }

    SECTION("ForChannel(0) falls back to Prime")
    {
        REQUIRE(&ChannelTemplateCache::ForChannel(0) == &ChannelTemplateCache::Prime());
    }
}


TEST_CASE("CachedChannelTemplate IsValid validity checks", "[channel_template_cache]")
{
    SECTION("Empty cache entry is invalid")
    {
        CachedChannelTemplate entry;
        REQUIRE_FALSE(entry.IsValid());
    }

    SECTION("Entry with null block is invalid")
    {
        CachedChannelTemplate entry;
        entry.hashBestChainAtBuild = uint1024_t(1);
        entry.pBlock               = nullptr;
        REQUIRE_FALSE(entry.IsValid());
    }

    SECTION("Entry with zero hashBestChainAtBuild is invalid")
    {
        CachedChannelTemplate entry;
        entry.hashBestChainAtBuild = uint1024_t(0);
        entry.pBlock               = std::make_shared<TAO::Ledger::TritiumBlock>();
        REQUIRE_FALSE(entry.IsValid());
    }

    SECTION("Entry is valid when hashBestChainAtBuild matches live hashBestChain")
    {
        const uint1024_t hashTip = uint1024_t(0xABCD1234u);
        set_fake_chain_tip(hashTip);

        CachedChannelTemplate entry;
        entry.pBlock               = std::make_shared<TAO::Ledger::TritiumBlock>();
        entry.hashBestChainAtBuild = hashTip;

        REQUIRE(entry.IsValid());

        set_fake_chain_tip(uint1024_t(0));
    }

    SECTION("Entry is stale when chain tip advances")
    {
        const uint1024_t hashOld = uint1024_t(0xAABBCCDDu);
        const uint1024_t hashNew = uint1024_t(0x11223344u);

        set_fake_chain_tip(hashOld);

        CachedChannelTemplate entry;
        entry.pBlock               = std::make_shared<TAO::Ledger::TritiumBlock>();
        entry.hashBestChainAtBuild = hashOld;

        REQUIRE(entry.IsValid());

        set_fake_chain_tip(hashNew);
        REQUIRE_FALSE(entry.IsValid());

        set_fake_chain_tip(uint1024_t(0));
    }
}


TEST_CASE("ChannelTemplateCache StoreRewardAddress and GetLastRewardAddress",
          "[channel_template_cache]")
{
    ChannelTemplateCache cache;

    SECTION("Initial last reward address is zero")
    {
        REQUIRE(cache.GetLastRewardAddress() == uint256_t(0));
    }

    SECTION("StoreRewardAddress persists the address")
    {
        const uint256_t hashReward = uint256_t(0xDEADBEEFu);
        cache.StoreRewardAddress(hashReward);
        REQUIRE(cache.GetLastRewardAddress() == hashReward);
    }

    SECTION("StoreRewardAddress with zero is ignored")
    {
        const uint256_t hashReward = uint256_t(0xCAFEBABEu);
        cache.StoreRewardAddress(hashReward);
        cache.StoreRewardAddress(uint256_t(0));
        REQUIRE(cache.GetLastRewardAddress() == hashReward);
    }

    SECTION("Second StoreRewardAddress replaces the first")
    {
        const uint256_t hashFirst  = uint256_t(0x00000001u);
        const uint256_t hashSecond = uint256_t(0x00000002u);
        cache.StoreRewardAddress(hashFirst);
        cache.StoreRewardAddress(hashSecond);
        REQUIRE(cache.GetLastRewardAddress() == hashSecond);
    }
}


TEST_CASE("ChannelTemplateCache GetCurrent returns nullptr when empty",
          "[channel_template_cache]")
{
    ChannelTemplateCache cache;
    cache.Invalidate();

    const uint1024_t hashTip = uint1024_t(0x55667788u);
    set_fake_chain_tip(hashTip);
    REQUIRE(cache.GetCurrent() == nullptr);

    set_fake_chain_tip(uint1024_t(0));
}


TEST_CASE("ChannelTemplateCache GetCurrentForReward returns nullptr on empty cache",
          "[channel_template_cache]")
{
    ChannelTemplateCache cache;
    cache.Invalidate();

    const uint1024_t hashTip    = uint1024_t(0xF0F0F0F0u);
    const uint256_t  hashReward = uint256_t(0x12345678u);
    set_fake_chain_tip(hashTip);

    REQUIRE(cache.GetCurrentForReward(hashReward) == nullptr);

    set_fake_chain_tip(uint1024_t(0));
}


TEST_CASE("ChannelTemplateCache Invalidate clears cache", "[channel_template_cache]")
{
    ChannelTemplateCache cache;
    REQUIRE_NOTHROW(cache.Invalidate());
    REQUIRE(cache.GetCurrent() == nullptr);
}


TEST_CASE("ChannelTemplateCache metrics counters are accessible",
          "[channel_template_cache]")
{
    const uint64_t hits   = g_channel_cache_hits_total.load();
    const uint64_t misses = g_channel_cache_misses_total.load();

    g_channel_cache_hits_total.fetch_add(1);
    REQUIRE(g_channel_cache_hits_total.load() >= hits + 1);

    g_channel_cache_misses_total.fetch_add(1);
    REQUIRE(g_channel_cache_misses_total.load() >= misses + 1);
}


TEST_CASE("ChannelTemplateCache concurrent reads are safe",
          "[channel_template_cache]")
{
    ChannelTemplateCache cache;
    cache.Invalidate();

    const uint1024_t hashTip = uint1024_t(0xABABABABu);
    set_fake_chain_tip(hashTip);

    constexpr int N_THREADS = 16;
    constexpr int N_READS   = 100;

    std::vector<std::thread> vThreads;
    vThreads.reserve(N_THREADS);

    std::atomic<int> nSuccessful{0};

    for(int i = 0; i < N_THREADS; ++i)
    {
        vThreads.emplace_back([&cache, &nSuccessful]()
        {
            for(int j = 0; j < N_READS; ++j)
            {
                std::shared_ptr<TAO::Ledger::TritiumBlock> p = cache.GetCurrent();
                (void)p;
                ++nSuccessful;
            }
        });
    }

    for(auto& t : vThreads)
        t.join();

    REQUIRE(nSuccessful.load() == N_THREADS * N_READS);

    set_fake_chain_tip(uint1024_t(0));
}


TEST_CASE("ChannelTemplateCache Rebuild with zero reward address is no-op",
          "[channel_template_cache]")
{
    ChannelTemplateCache cache;
    cache.Invalidate();

    const uint1024_t hashTip = uint1024_t(0xC0FFEE00u);
    set_fake_chain_tip(hashTip);

    cache.Rebuild(1, uint256_t(0));
    REQUIRE(cache.GetCurrent() == nullptr);

    set_fake_chain_tip(uint1024_t(0));
}


TEST_CASE("ChannelTemplateCache Rebuild with invalid channel is no-op",
          "[channel_template_cache]")
{
    ChannelTemplateCache cache;
    cache.Invalidate();

    const uint256_t hashReward = uint256_t(0xBAADF00Du);

    REQUIRE_NOTHROW(cache.Rebuild(0, hashReward));
    REQUIRE(cache.GetCurrent() == nullptr);

    REQUIRE_NOTHROW(cache.Rebuild(3, hashReward));
    REQUIRE(cache.GetCurrent() == nullptr);
}
