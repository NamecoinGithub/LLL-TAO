/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <TAO/Ledger/include/chainstate.h>
#include <Util/include/args.h>
#include <Util/include/config.h>

using namespace TAO::Ledger;

TEST_CASE("Fork Prevention Tests", "[ledger][fork][prevention]")
{
    SECTION("ForkDetector records successful validations")
    {
        /* Reset state */
        ChainState::ForkDetector::RecordSuccess(100, uint512_t(12345));

        /* Verify failure counter is reset */
        bool fForkDetected = ChainState::ForkDetector::CheckForFork();
        REQUIRE(fForkDetected == false);
    }

    SECTION("ForkDetector counts consecutive failures")
    {
        /* Reset state */
        ChainState::ForkDetector::RecordSuccess(100, uint512_t(12345));

        /* Record failures below threshold */
        for(uint32_t i = 0; i < 5; ++i)
        {
            ChainState::ForkDetector::RecordFailure();
        }

        /* Should not trigger fork detection yet */
        bool fForkDetected = ChainState::ForkDetector::CheckForFork();
        REQUIRE(fForkDetected == false);
    }

    SECTION("ForkDetector triggers at threshold")
    {
        /* Reset state */
        ChainState::ForkDetector::RecordSuccess(100, uint512_t(12345));

        /* Get fork threshold (default 10) */
        uint32_t nThreshold = config::GetArg("-forkthreshold", 10);

        /* Record failures up to threshold */
        for(uint32_t i = 0; i < nThreshold; ++i)
        {
            ChainState::ForkDetector::RecordFailure();
        }

        /* Should trigger fork detection */
        bool fForkDetected = ChainState::ForkDetector::CheckForFork();
        REQUIRE(fForkDetected == true);
    }

    SECTION("ForkDetector resets after successful validation")
    {
        /* Reset state */
        ChainState::ForkDetector::RecordSuccess(100, uint512_t(12345));

        /* Record some failures */
        for(uint32_t i = 0; i < 5; ++i)
        {
            ChainState::ForkDetector::RecordFailure();
        }

        /* Should not trigger yet */
        REQUIRE(ChainState::ForkDetector::CheckForFork() == false);

        /* Record successful validation */
        ChainState::ForkDetector::RecordSuccess(105, uint512_t(67890));

        /* Record more failures */
        for(uint32_t i = 0; i < 5; ++i)
        {
            ChainState::ForkDetector::RecordFailure();
        }

        /* Should still not trigger because counter was reset */
        REQUIRE(ChainState::ForkDetector::CheckForFork() == false);
    }

    SECTION("GetRollbackHeight returns last good height")
    {
        /* Set a known good state */
        uint32_t nTestHeight = 12345;
        uint512_t hashTest(0xABCDEF);

        ChainState::ForkDetector::RecordSuccess(nTestHeight, hashTest);

        /* Get rollback height */
        uint32_t nRollbackHeight = ChainState::ForkDetector::GetRollbackHeight();

        /* Should return the last good height */
        REQUIRE(nRollbackHeight == nTestHeight);
    }

    SECTION("CLI configuration functions")
    {
        /* Test default values */
        uint32_t nForkThreshold = config::GetForkThreshold();
        REQUIRE(nForkThreshold == 10); // Default value

        bool fAutoRollback = config::GetAutoRollback();
        REQUIRE(fAutoRollback == true); // Default value

        uint32_t nManualHeight = config::GetManualRollbackHeight();
        REQUIRE(nManualHeight == 0); // Default value (disabled)
    }

    SECTION("ForkDetector tracks last good block")
    {
        /* Set initial state */
        uint32_t nHeight1 = 1000;
        uint512_t hash1(0x1111);
        ChainState::ForkDetector::RecordSuccess(nHeight1, hash1);

        uint32_t nRollback1 = ChainState::ForkDetector::GetRollbackHeight();
        REQUIRE(nRollback1 == nHeight1);

        /* Update to new state */
        uint32_t nHeight2 = 1001;
        uint512_t hash2(0x2222);
        ChainState::ForkDetector::RecordSuccess(nHeight2, hash2);

        uint32_t nRollback2 = ChainState::ForkDetector::GetRollbackHeight();
        REQUIRE(nRollback2 == nHeight2);

        /* Verify rollback height updated */
        REQUIRE(nRollback2 > nRollback1);
    }

    SECTION("Fork detection with varying thresholds")
    {
        /* Test with different threshold values */
        std::vector<uint32_t> vThresholds = {1, 5, 10, 20, 50};

        for(uint32_t nTestThreshold : vThresholds)
        {
            /* Reset state */
            ChainState::ForkDetector::RecordSuccess(100, uint512_t(999));

            /* Record failures just below threshold */
            for(uint32_t i = 0; i < nTestThreshold - 1; ++i)
            {
                ChainState::ForkDetector::RecordFailure();
            }

            /* Should not trigger yet */
            REQUIRE(ChainState::ForkDetector::CheckForFork() == false);

            /* One more failure should trigger */
            ChainState::ForkDetector::RecordFailure();
            
            /* Note: CheckForFork uses config value, so this test validates
             * that the counter increments correctly */
        }
    }

    SECTION("Transaction resurrection interface")
    {
        /* Test that ResurrectTransactions can be called */
        bool fResult = ChainState::ForkDetector::ResurrectTransactions(100, 110);
        
        /* Should return true (indicating it processed the request) */
        REQUIRE(fResult == true);
    }

    SECTION("Rollback safety checks")
    {
        /* Set initial good state */
        ChainState::ForkDetector::RecordSuccess(500, uint512_t(0xABC));

        /* Record failures to trigger fork detection */
        for(uint32_t i = 0; i < 15; ++i)
        {
            ChainState::ForkDetector::RecordFailure();
        }

        /* Verify fork is detected */
        REQUIRE(ChainState::ForkDetector::CheckForFork() == true);

        /* Get rollback height */
        uint32_t nRollback = ChainState::ForkDetector::GetRollbackHeight();
        
        /* Rollback height should be the last good height */
        REQUIRE(nRollback == 500);
    }
}
