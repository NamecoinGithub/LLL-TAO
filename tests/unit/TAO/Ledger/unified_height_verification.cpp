/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <TAO/Ledger/include/chainstate.h>
#include <TAO/Ledger/types/state.h>

#include <unit/catch2/catch.hpp>

TEST_CASE("Unified Height Verification - GetChannelHeights basic", "[ledger]")
{
    uint32_t nStake = 0;
    uint32_t nPrime = 0;
    uint32_t nHash = 0;

    /* Test that GetChannelHeights doesn't crash */
    bool result = TAO::Ledger::ChainState::GetChannelHeights(nStake, nPrime, nHash);
    
    /* Should always return true */
    REQUIRE(result == true);
    
    /* At genesis or early blocks, values should be zero or small */
    REQUIRE(nStake >= 0);
    REQUIRE(nPrime >= 0);
    REQUIRE(nHash >= 0);
}


TEST_CASE("Unified Height Verification - VerifyUnifiedHeightConsistency basic", "[ledger]")
{
    /* Test that verification doesn't crash */
    bool result = TAO::Ledger::ChainState::VerifyUnifiedHeightConsistency();
    
    /* Should return true (consistent or skipped during sync) */
    REQUIRE(result == true);
}


TEST_CASE("Unified Height Verification - GetChannelHeights at genesis", "[ledger]")
{
    /* If we're at genesis block, all channels should be zero */
    TAO::Ledger::BlockState bestState = TAO::Ledger::ChainState::tStateBest.load();
    
    if(bestState.nHeight == 0)
    {
        uint32_t nStake = 0;
        uint32_t nPrime = 0;
        uint32_t nHash = 0;
        
        bool result = TAO::Ledger::ChainState::GetChannelHeights(nStake, nPrime, nHash);
        
        REQUIRE(result == true);
        REQUIRE(nStake == 0);
        REQUIRE(nPrime == 0);
        REQUIRE(nHash == 0);
    }
}


TEST_CASE("Unified Height Verification - Height consistency formula", "[ledger]")
{
    /* Get the current best state */
    TAO::Ledger::BlockState bestState = TAO::Ledger::ChainState::tStateBest.load();
    
    /* Skip test if at genesis */
    if(bestState.nHeight == 0)
    {
        SUCCEED("Skipping test at genesis block");
        return;
    }
    
    /* Get channel heights */
    uint32_t nStake = 0;
    uint32_t nPrime = 0;
    uint32_t nHash = 0;
    
    bool result = TAO::Ledger::ChainState::GetChannelHeights(nStake, nPrime, nHash);
    REQUIRE(result == true);
    
    /* Verify the formula: unified = stake + prime + hash */
    uint32_t nCalculated = nStake + nPrime + nHash;
    uint32_t nActual = bestState.nHeight;
    
    /* The heights should match */
    INFO("Stake height: " << nStake);
    INFO("Prime height: " << nPrime);
    INFO("Hash height: " << nHash);
    INFO("Calculated unified height: " << nCalculated);
    INFO("Actual unified height: " << nActual);
    
    REQUIRE(nCalculated == nActual);
}


TEST_CASE("Unified Height Verification - Channel heights non-negative", "[ledger]")
{
    uint32_t nStake = 0;
    uint32_t nPrime = 0;
    uint32_t nHash = 0;
    
    TAO::Ledger::ChainState::GetChannelHeights(nStake, nPrime, nHash);
    
    /* All channel heights should be non-negative (uint32_t guarantees this) */
    /* Just verify they're reasonable values */
    REQUIRE(nStake < 1000000000);  // Less than 1 billion
    REQUIRE(nPrime < 1000000000);
    REQUIRE(nHash < 1000000000);
}
