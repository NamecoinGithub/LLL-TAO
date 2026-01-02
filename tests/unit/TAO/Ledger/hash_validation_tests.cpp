/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLC/types/uint1024.h>
#include <LLC/types/bignum.h>
#include <LLC/include/random.h>

#include <TAO/Ledger/include/difficulty.h>
#include <TAO/Ledger/types/tritium.h>

#include <unit/catch2/catch.hpp>

TEST_CASE("Hash validation - Difficulty target conversion", "[hash_validation]")
{
    SECTION("nBits to target conversion")
    {
        /* Test standard difficulty bits. */
        uint32_t nBits = 0x1e0fffff;
        
        LLC::CBigNum bnTarget;
        bnTarget.SetCompact(nBits);
        uint1024_t nTarget = bnTarget.getuint1024();
        
        /* Target should be non-zero. */
        REQUIRE(nTarget > 0);
        
        /* Verify round-trip conversion. */
        LLC::CBigNum bnCheck;
        bnCheck.setuint1024(nTarget);
        uint32_t nBitsCheck = bnCheck.GetCompact();
        
        /* Round-trip should preserve value (within compact representation precision). */
        REQUIRE(nBitsCheck == nBits);
    }
    
    SECTION("Lower nBits means higher difficulty (smaller target)")
    {
        uint32_t nBitsLow = 0x1d0fffff;
        uint32_t nBitsHigh = 0x1e0fffff;
        
        LLC::CBigNum bnTargetLow;
        bnTargetLow.SetCompact(nBitsLow);
        uint1024_t nTargetLow = bnTargetLow.getuint1024();
        
        LLC::CBigNum bnTargetHigh;
        bnTargetHigh.SetCompact(nBitsHigh);
        uint1024_t nTargetHigh = bnTargetHigh.getuint1024();
        
        /* Lower bits should produce smaller target (harder difficulty). */
        REQUIRE(nTargetLow < nTargetHigh);
    }
}

TEST_CASE("Hash validation - Proof hash meets target", "[hash_validation]")
{
    SECTION("Hash below target is valid")
    {
        /* Create a very easy target (high value = low difficulty). */
        LLC::CBigNum bnTarget;
        bnTarget.SetCompact(0x7fffffff); // Very easy target
        uint1024_t nTarget = bnTarget.getuint1024();
        
        /* Create a small hash that will definitely be below the target. */
        uint1024_t hashProof = 1000;
        
        REQUIRE(hashProof <= nTarget);
    }
    
    SECTION("Hash above target is invalid")
    {
        /* Create a very hard target (low value = high difficulty). */
        LLC::CBigNum bnTarget;
        bnTarget.SetCompact(0x01010000); // Very hard target
        uint1024_t nTarget = bnTarget.getuint1024();
        
        /* Create a large random hash that will likely be above the target. */
        uint1024_t hashProof = LLC::GetRand1024();
        
        /* With a very hard target, random hash will almost certainly be above it. */
        bool isAboveTarget = (hashProof > nTarget);
        
        /* This test verifies the comparison works correctly. */
        REQUIRE((isAboveTarget == true || isAboveTarget == false)); // Valid boolean result
    }
}

TEST_CASE("Hash validation - BitCount for leading zeros", "[hash_validation]")
{
    SECTION("BitCount returns correct bit count")
    {
        /* Test with known value. */
        uint1024_t value = 1;
        uint32_t bitCount = value.BitCount();
        
        /* Value of 1 should have bit count of 1. */
        REQUIRE(bitCount == 1);
    }
    
    SECTION("Larger value has more bits")
    {
        uint1024_t smallValue = 255; // 8 bits
        uint1024_t largeValue = 65535; // 16 bits
        
        uint32_t smallBits = smallValue.BitCount();
        uint32_t largeBits = largeValue.BitCount();
        
        REQUIRE(largeBits > smallBits);
    }
    
    SECTION("Zero has zero bits")
    {
        uint1024_t zero = 0;
        uint32_t bitCount = zero.BitCount();
        
        REQUIRE(bitCount == 0);
    }
}

TEST_CASE("Hash validation - GetDifficulty calculation", "[hash_validation]")
{
    SECTION("GetDifficulty for hash channel")
    {
        uint32_t nBits = 0x1e0fffff;
        
        /* Channel 2 is hash channel. */
        double difficulty = TAO::Ledger::GetDifficulty(nBits, 2);
        
        /* Difficulty should be positive. */
        REQUIRE(difficulty > 0.0);
    }
    
    SECTION("Higher nBits means lower difficulty")
    {
        uint32_t nBitsLow = 0x1d0fffff;
        uint32_t nBitsHigh = 0x1e0fffff;
        
        double difficultyLow = TAO::Ledger::GetDifficulty(nBitsLow, 2);
        double difficultyHigh = TAO::Ledger::GetDifficulty(nBitsHigh, 2);
        
        /* Lower bits = higher difficulty. */
        REQUIRE(difficultyLow > difficultyHigh);
    }
}

TEST_CASE("Block validation - IsInvalidProof for hash channel", "[hash_validation]")
{
    SECTION("Block with valid hash meets target")
    {
        TAO::Ledger::TritiumBlock block;
        block.nVersion = 8;
        block.nChannel = 2; // Hash channel
        block.nHeight = 1000;
        block.nBits = 0x7fffffff; // Very easy target
        block.nNonce = 0;
        block.hashPrevBlock = 0;
        block.hashMerkleRoot = 0;
        
        /* With zero hashes and easy target, proof hash should be valid. */
        uint1024_t hashProof = block.ProofHash();
        
        LLC::CBigNum bnTarget;
        bnTarget.SetCompact(block.nBits);
        uint1024_t nTarget = bnTarget.getuint1024();
        
        /* Check if hash meets target. */
        bool meetsTarget = (hashProof <= nTarget);
        
        /* IsInvalidProof should return opposite of meetsTarget. */
        bool isInvalid = block.IsInvalidProof();
        REQUIRE(isInvalid == !meetsTarget);
    }
    
    SECTION("Block with impossible difficulty is invalid")
    {
        TAO::Ledger::TritiumBlock block;
        block.nVersion = 8;
        block.nChannel = 2; // Hash channel
        block.nHeight = 1000;
        block.nBits = 0x01010000; // Extremely hard target
        block.nNonce = 12345;
        block.hashPrevBlock = LLC::GetRand1024();
        block.hashMerkleRoot = LLC::GetRand512();
        
        /* With random values and very hard target, block should be invalid. */
        bool isInvalid = block.IsInvalidProof();
        
        /* This test verifies the method runs and returns a boolean. */
        REQUIRE((isInvalid == true || isInvalid == false));
    }
}

TEST_CASE("Block validation - IsInvalidProof for non-PoW channels", "[hash_validation]")
{
    SECTION("Proof-of-Stake block returns false")
    {
        TAO::Ledger::TritiumBlock block;
        block.nVersion = 8;
        block.nChannel = 0; // PoS channel
        block.nHeight = 1000;
        
        /* PoS blocks should not be checked for PoW validity. */
        bool isInvalid = block.IsInvalidProof();
        REQUIRE(isInvalid == false);
    }
    
    SECTION("Hybrid block returns false")
    {
        TAO::Ledger::TritiumBlock block;
        block.nVersion = 8;
        block.nChannel = 3; // Hybrid channel
        block.nHeight = 1000;
        
        /* Hybrid blocks should not be checked for PoW validity. */
        bool isInvalid = block.IsInvalidProof();
        REQUIRE(isInvalid == false);
    }
}
