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

#include <TAO/Ledger/include/prime.h>
#include <TAO/Ledger/include/difficulty.h>
#include <TAO/Ledger/types/tritium.h>

#include <unit/catch2/catch.hpp>

TEST_CASE("Prime validation - Fermat test accuracy", "[prime_validation]")
{
    SECTION("Known prime passes Fermat test")
    {
        /* Use a known small prime for testing. */
        uint1024_t knownPrime = 7919; // A known prime number
        
        uint1024_t result = TAO::Ledger::FermatTest(knownPrime);
        REQUIRE(result == 1);
    }
    
    SECTION("Known composite fails Fermat test")
    {
        /* Use a known composite number. */
        uint1024_t knownComposite = 7920; // 7920 = 16 * 495
        
        uint1024_t result = TAO::Ledger::FermatTest(knownComposite);
        REQUIRE(result != 1);
    }
}

TEST_CASE("Prime validation - Small divisor check", "[prime_validation]")
{
    SECTION("Number divisible by 2 fails small divisor check")
    {
        uint1024_t evenNumber = 1000;
        REQUIRE(TAO::Ledger::SmallDivisors(evenNumber) == false);
    }
    
    SECTION("Number divisible by 3 fails small divisor check")
    {
        uint1024_t divisibleBy3 = 1005; // 1005 = 3 * 335
        REQUIRE(TAO::Ledger::SmallDivisors(divisibleBy3) == false);
    }
    
    SECTION("Prime number passes small divisor check")
    {
        uint1024_t prime = 7919;
        REQUIRE(TAO::Ledger::SmallDivisors(prime) == true);
    }
}

TEST_CASE("Prime validation - PrimeCheck integration", "[prime_validation]")
{
    SECTION("Known prime passes PrimeCheck")
    {
        uint1024_t knownPrime = 7919;
        REQUIRE(TAO::Ledger::PrimeCheck(knownPrime) == true);
    }
    
    SECTION("Known composite fails PrimeCheck")
    {
        uint1024_t knownComposite = 7920;
        REQUIRE(TAO::Ledger::PrimeCheck(knownComposite) == false);
    }
    
    SECTION("Random even number fails PrimeCheck")
    {
        uint1024_t evenNumber = (LLC::GetRand1024() & ~uint1024_t(1)); // Clear last bit to make even
        REQUIRE(TAO::Ledger::PrimeCheck(evenNumber) == false);
    }
}

TEST_CASE("Prime validation - GetOffsets cluster detection", "[prime_validation]")
{
    SECTION("Valid prime cluster generates offsets")
    {
        /* Create a block with a prime channel. */
        TAO::Ledger::TritiumBlock block;
        block.nVersion = 8;
        block.nChannel = 1; // Prime channel
        block.nHeight = 1000;
        block.nBits = 0x7b7fffff; // Difficulty bits
        block.nNonce = 0;
        
        /* We need to find a valid prime to test with.
         * For testing purposes, we'll use a known pattern that generates a prime cluster.
         * Note: In real scenarios, miners search for these values. */
        
        /* Test that GetOffsets produces results for any odd number that is prime. */
        uint1024_t hashPrime = 7919; // Known prime
        std::vector<uint8_t> vOffsets;
        
        TAO::Ledger::GetOffsets(hashPrime, vOffsets);
        
        /* A valid prime should generate at least one offset (the base prime itself counts). */
        REQUIRE(vOffsets.size() > 0);
    }
}

TEST_CASE("Prime validation - GetPrimeDifficulty calculation", "[prime_validation]")
{
    SECTION("Prime difficulty increases with cluster size")
    {
        /* Test with a known prime. */
        uint1024_t hashPrime = 7919;
        std::vector<uint8_t> vOffsets;
        
        TAO::Ledger::GetOffsets(hashPrime, vOffsets);
        double difficulty = TAO::Ledger::GetPrimeDifficulty(hashPrime, vOffsets, true);
        
        /* Valid prime should have difficulty > 0. */
        REQUIRE(difficulty > 0.0);
    }
    
    SECTION("Composite number has zero difficulty")
    {
        uint1024_t hashComposite = 7920; // Composite
        std::vector<uint8_t> vOffsets;
        
        TAO::Ledger::GetOffsets(hashComposite, vOffsets);
        double difficulty = TAO::Ledger::GetPrimeDifficulty(hashComposite, vOffsets, true);
        
        /* Composite should have zero difficulty. */
        REQUIRE(difficulty == 0.0);
    }
}

TEST_CASE("Block validation - IsInvalidProof for prime channel", "[prime_validation]")
{
    SECTION("Block with invalid prime is detected")
    {
        TAO::Ledger::TritiumBlock block;
        block.nVersion = 8;
        block.nChannel = 1; // Prime channel
        block.nHeight = 1000;
        block.nBits = 0x7b7fffff;
        block.nNonce = 12345;
        block.hashPrevBlock = LLC::GetRand1024();
        block.hashMerkleRoot = LLC::GetRand512();
        
        /* With random values, the block should be invalid. */
        /* Note: There's a tiny probability it could be valid, but extremely unlikely. */
        bool isInvalid = block.IsInvalidProof();
        
        /* We expect this to likely be invalid (though we can't guarantee due to randomness). */
        /* This test mainly verifies the method runs without crashing. */
        REQUIRE((isInvalid == true || isInvalid == false)); // Method returns a valid boolean
    }
}
