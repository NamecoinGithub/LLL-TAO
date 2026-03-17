/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <LLP/include/channel_template_cache.h>
#include <LLP/include/mining_constants.h>
#include <TAO/Ledger/include/chainstate.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace LLP;


/** Helper: create a minimal non-null TritiumBlock shared_ptr for test purposes. */
static std::shared_ptr<TAO::Ledger::TritiumBlock> MakeFakeBlock(uint32_t nChannel = 1)
{
    auto pBlock = std::make_shared<TAO::Ledger::TritiumBlock>();
    pBlock->nChannel = nChannel;
    pBlock->nHeight  = 1234;
    return pBlock;
}


/** Helper: return the current hashBestChain (typically 0 in unit tests). */
static uint1024_t CurrentBestChainHash()
{
    return TAO::Ledger::ChainState::hashBestChain.load();
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Suite 1 — Constants
 * ─────────────────────────────────────────────────────────────────────────────*/
TEST_CASE("ChannelTemplateCache: CACHE_REBUILD_WAIT_MS constant", "[channel_cache][constants]")
{
    SECTION("CACHE_REBUILD_WAIT_MS equals 500 ms")
    {
        REQUIRE(MiningConstants::CACHE_REBUILD_WAIT_MS == 500u);
    }

    SECTION("CacheState enum values are distinct")
    {
        REQUIRE(static_cast<uint8_t>(CacheState::IDLE)     == 0u);
        REQUIRE(static_cast<uint8_t>(CacheState::BUILDING) == 1u);
        REQUIRE(static_cast<uint8_t>(CacheState::EMPTY)    == 2u);
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Suite 2 — EMPTY state
 * ─────────────────────────────────────────────────────────────────────────────*/
TEST_CASE("ChannelTemplateCache: EMPTY state returns nullptr immediately", "[channel_cache][empty]")
{
    ChannelTemplateCache cache(1);

    SECTION("Initial state is EMPTY")
    {
        REQUIRE(cache.GetState() == CacheState::EMPTY);
    }

    SECTION("GetCurrent() in EMPTY state returns nullptr without waiting")
    {
        const auto tStart = std::chrono::steady_clock::now();
        auto pResult = cache.GetCurrent(std::chrono::milliseconds(500));
        const auto tEnd   = std::chrono::steady_clock::now();

        const auto nElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            tEnd - tStart).count();

        REQUIRE(pResult == nullptr);

        /* Must return in well under 50 ms — no waiting in EMPTY state. */
        REQUIRE(nElapsedMs < 50);
    }

    SECTION("GetCurrent() in EMPTY state increments miss counter")
    {
        const uint64_t nBefore = g_channel_cache_misses_total.load();
        cache.GetCurrent(std::chrono::milliseconds(10));
        REQUIRE(g_channel_cache_misses_total.load() == nBefore + 1);
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Suite 3 — IDLE state
 * ─────────────────────────────────────────────────────────────────────────────*/
TEST_CASE("ChannelTemplateCache: IDLE state serves cached template", "[channel_cache][idle]")
{
    ChannelTemplateCache cache(1);
    cache.TEST_Reset();

    auto pFakeBlock = MakeFakeBlock(1);

    /* Force the cache into IDLE with a valid entry. */
    cache.TEST_ForceIdle(pFakeBlock, CurrentBestChainHash());

    SECTION("State transitions to IDLE after TEST_ForceIdle")
    {
        REQUIRE(cache.GetState() == CacheState::IDLE);
    }

    SECTION("GetCurrent() in IDLE returns non-null immediately")
    {
        const auto tStart = std::chrono::steady_clock::now();
        auto pResult = cache.GetCurrent(std::chrono::milliseconds(500));
        const auto tEnd   = std::chrono::steady_clock::now();

        const auto nElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            tEnd - tStart).count();

        /* Must return the cached block. */
        REQUIRE(pResult != nullptr);
        REQUIRE(pResult.get() == pFakeBlock.get());

        /* Fast path: must complete in well under 50 ms. */
        REQUIRE(nElapsedMs < 50);
    }

    SECTION("GetCurrent() in IDLE increments hit counter")
    {
        const uint64_t nBefore = g_channel_cache_hits_total.load();
        auto pResult = cache.GetCurrent(std::chrono::milliseconds(10));
        REQUIRE(pResult != nullptr);
        REQUIRE(g_channel_cache_hits_total.load() == nBefore + 1);
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Suite 4 — BUILDING state: wait and serve new template
 * ─────────────────────────────────────────────────────────────────────────────*/
TEST_CASE("ChannelTemplateCache: BUILDING state waits and serves new template", "[channel_cache][building][wait]")
{
    ChannelTemplateCache cache(2);
    cache.TEST_Reset();

    /* Drive cache into BUILDING state. */
    cache.TEST_ForceBuilding();
    REQUIRE(cache.GetState() == CacheState::BUILDING);

    auto pFakeBlock = MakeFakeBlock(2);

    SECTION("GET_BLOCK during BUILDING waits, then serves new template when rebuild completes")
    {
        const uint64_t nWaitsBefore     = g_channel_cache_rebuild_waits_total.load();
        const uint64_t nTimeoutsBefore  = g_channel_cache_rebuild_timeouts_total.load();

        /* Simulate the rebuild completing after 100 ms on a background thread. */
        std::thread rebuilder([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            cache.TEST_ForceIdle(pFakeBlock, CurrentBestChainHash());
        });

        /* GetCurrent() should block until the rebuild completes (well under 500 ms). */
        auto pResult = cache.GetCurrent(std::chrono::milliseconds(500));

        rebuilder.join();

        REQUIRE(pResult != nullptr);
        REQUIRE(pResult.get() == pFakeBlock.get());

        /* Exactly one wait satisfied, zero timeouts. */
        REQUIRE(g_channel_cache_rebuild_waits_total.load()   == nWaitsBefore + 1);
        REQUIRE(g_channel_cache_rebuild_timeouts_total.load() == nTimeoutsBefore);
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Suite 5 — BUILDING state: timeout fallback
 * ─────────────────────────────────────────────────────────────────────────────*/
TEST_CASE("ChannelTemplateCache: BUILDING state times out and returns nullptr", "[channel_cache][building][timeout]")
{
    ChannelTemplateCache cache(1);
    cache.TEST_Reset();
    cache.TEST_ForceBuilding();

    SECTION("GetCurrent() times out after nMaxWait and returns nullptr")
    {
        const uint64_t nTimeoutsBefore = g_channel_cache_rebuild_timeouts_total.load();
        const uint64_t nMissesBefore   = g_channel_cache_misses_total.load();

        /* Use a very short timeout (50 ms) with no builder completing. */
        const auto tStart = std::chrono::steady_clock::now();
        auto pResult = cache.GetCurrent(std::chrono::milliseconds(50));
        const auto tEnd   = std::chrono::steady_clock::now();

        const auto nElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            tEnd - tStart).count();

        REQUIRE(pResult == nullptr);

        /* Elapsed must be within [50 ms, 300 ms]. */
        REQUIRE(nElapsedMs >= 50);
        REQUIRE(nElapsedMs < 300);

        /* Timeout and miss counters must each increment exactly once. */
        REQUIRE(g_channel_cache_rebuild_timeouts_total.load() == nTimeoutsBefore + 1);
        REQUIRE(g_channel_cache_misses_total.load()           == nMissesBefore  + 1);
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Suite 6 — Concurrent readers all wake on notify_all()
 * ─────────────────────────────────────────────────────────────────────────────*/
TEST_CASE("ChannelTemplateCache: concurrent GET_BLOCK readers all wake on notify_all", "[channel_cache][concurrent][notify_all]")
{
    ChannelTemplateCache cache(1);
    cache.TEST_Reset();
    cache.TEST_ForceBuilding();

    constexpr int NUM_READERS = 8;

    auto pFakeBlock = MakeFakeBlock(1);

    std::atomic<int> nSuccesses{0};
    std::atomic<int> nFailures{0};

    /* Start N_READERS threads — all will block on GetCurrent() in BUILDING state. */
    std::vector<std::thread> readers;
    readers.reserve(NUM_READERS);
    for(int i = 0; i < NUM_READERS; ++i)
    {
        readers.emplace_back([&]() {
            auto pResult = cache.GetCurrent(std::chrono::milliseconds(500));
            if(pResult && pResult.get() == pFakeBlock.get())
                ++nSuccesses;
            else
                ++nFailures;
        });
    }

    /* Brief pause to let all readers enter their wait. */
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    /* Simulate rebuild complete — notify_all() will wake every waiting reader. */
    cache.TEST_ForceIdle(pFakeBlock, CurrentBestChainHash());

    for(auto& t : readers)
        t.join();

    SECTION("All N concurrent readers wake and serve the same template")
    {
        REQUIRE(nSuccesses.load() == NUM_READERS);
        REQUIRE(nFailures.load()  == 0);
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Suite 7 — GetChannelCache singletons
 * ─────────────────────────────────────────────────────────────────────────────*/
TEST_CASE("GetChannelCache returns correct singleton per channel", "[channel_cache][singleton]")
{
    SECTION("Channel 1 and channel 2 are independent instances")
    {
        ChannelTemplateCache& prime = GetChannelCache(1);
        ChannelTemplateCache& hash  = GetChannelCache(2);

        REQUIRE(&prime != &hash);
        REQUIRE(prime.GetChannel() == 1u);
        REQUIRE(hash.GetChannel()  == 2u);
    }

    SECTION("Same channel returns same singleton instance")
    {
        REQUIRE(&GetChannelCache(1) == &GetChannelCache(1));
        REQUIRE(&GetChannelCache(2) == &GetChannelCache(2));
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Suite 8 — BUILDING concurrent rebuild protection (idempotent)
 * ─────────────────────────────────────────────────────────────────────────────*/
TEST_CASE("ChannelTemplateCache: second ForceBuilding while already BUILDING", "[channel_cache][building][idempotent]")
{
    ChannelTemplateCache cache(2);
    cache.TEST_Reset();

    /* Drive into BUILDING state. */
    cache.TEST_ForceBuilding();
    REQUIRE(cache.GetState() == CacheState::BUILDING);

    /* Calling ForceBuilding again doesn't change to IDLE/EMPTY — stays BUILDING. */
    cache.TEST_ForceBuilding();
    REQUIRE(cache.GetState() == CacheState::BUILDING);

    /* Transition to IDLE to unblock any waiters. */
    auto pFakeBlock = MakeFakeBlock(2);
    cache.TEST_ForceIdle(pFakeBlock, CurrentBestChainHash());
    REQUIRE(cache.GetState() == CacheState::IDLE);
}
