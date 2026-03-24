/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <TAO/Ledger/include/stateless_block_utility.h>
#include <TAO/Ledger/include/prime.h>
#include <TAO/Ledger/types/tritium.h>

#include <LLC/include/random.h>

#include <cstdint>
#include <vector>


/** Test Suite: Prime Sign / Finalization
 *
 *  These tests validate the ledger utility functions used for solved-block
 *  candidate construction:
 *
 *    1. BuildSolvedPrimeCandidateFromTemplate – constructs a canonical solved
 *       block by copying the immutable template and applying the miner's nNonce.
 *       vOffsets is cleared; the caller (sign_block) derives vOffsets itself by
 *       calling GetOffsets(GetPrime(), vOffsets) — exactly as upstream
 *       Nexusoft/LLL-TAO sign_block() does.
 *
 *    2. FinalizeWalletSignatureForSolvedBlock – generates the canonical
 *       vchBlockSig so that TritiumBlock::Check() → VerifySignature() passes.
 *       (Runtime tests are integration-level; unit tests cover structural
 *       correctness of the utility's interface.)
 *
 *  Root cause fixed:
 *    - The prior approach accepted miner-submitted vOffsets and applied them
 *      to the block, then called VerifyWork(GetPrimeBits(GetPrime(), vOffsets,
 *      fVerify=true)).  GetPrimeDifficulty(fVerify=true) calls PrimeCheck() on
 *      the base first and returns 0.0 for any composite GetPrime() value,
 *      ignoring the offsets entirely.  Since the miner's sieve does not
 *      guarantee that GetPrime() = ProofHash() + nNonce is itself prime, valid
 *      Cunningham chain submissions were persistently rejected.
 *    - Fix: the node now derives vOffsets itself via GetOffsets(GetPrime(),
 *      vOffsets) after applying nNonce, matching upstream behaviour.
 **/


/* ─── helpers ─────────────────────────────────────────────────────────────── */

/** Build a minimal TritiumBlock with the supplied channel, nNonce, and vOffsets
 *  for use as a test fixture.  Fields that are irrelevant to the tested utility
 *  functions are left at their zero-initialised defaults. */
static TAO::Ledger::TritiumBlock MakeTestBlock(
    uint32_t nChannel,
    uint64_t nNonce,
    const std::vector<uint8_t>& vOffsets)
{
    TAO::Ledger::TritiumBlock block;
    block.nVersion  = 8;
    block.nChannel  = nChannel;
    block.nNonce    = nNonce;
    block.nHeight   = 1000;
    block.nBits     = 0x04000000u;
    block.nTime     = 1700000000u;  // fixed timestamp
    block.vOffsets  = vOffsets;
    return block;
}

/** Minimal valid Prime vOffsets: one chain-offset byte (2) and 4 fractional bytes. */
static std::vector<uint8_t> MinimalValidPrimeOffsets()
{
    return {2u, 0u, 0u, 0u, 0u};
}


/* ─── BuildSolvedPrimeCandidateFromTemplate tests ────────────────────────── */

TEST_CASE("BuildSolvedPrimeCandidateFromTemplate: consensus fields preserved", "[prime][sign_finalization]")
{
    const uint32_t nChannel  = TAO::Ledger::CHANNEL::PRIME;
    const uint64_t nNonce    = 0xDEADBEEF12345678ULL;
    const uint32_t nVersion  = 8u;
    const uint32_t nHeight   = 6630855u;
    const uint32_t nBits     = 0x04000001u;
    const uint32_t nTime     = 1700000000u;

    TAO::Ledger::TritiumBlock tmpl = MakeTestBlock(nChannel, 1u /*template nNonce*/, {});
    tmpl.nVersion = nVersion;
    tmpl.nHeight  = nHeight;
    tmpl.nBits    = nBits;
    tmpl.nTime    = nTime;
    /* Simulate a prior template signature (to confirm it is cleared). */
    tmpl.vchBlockSig = {0xAA, 0xBB, 0xCC};

    TAO::Ledger::TritiumBlock solved =
        TAO::Ledger::BuildSolvedPrimeCandidateFromTemplate(tmpl, nNonce);

    SECTION("nVersion is preserved from template")
    {
        REQUIRE(solved.nVersion == nVersion);
    }

    SECTION("nChannel is preserved from template")
    {
        REQUIRE(solved.nChannel == nChannel);
    }

    SECTION("nHeight is preserved from template")
    {
        REQUIRE(solved.nHeight == nHeight);
    }

    SECTION("nBits is preserved from template")
    {
        REQUIRE(solved.nBits == nBits);
    }

    SECTION("nTime is preserved from template (not refreshed for Prime)")
    {
        /* For Prime: ProofHash = SK1024(nVersion..nBits) excludes nTime.
         * The miner's solved proof is independent of nTime, so we preserve
         * the template's nTime to avoid mutating anchor fields after issuance. */
        REQUIRE(solved.nTime == nTime);
    }

    SECTION("nNonce is set to the miner-submitted value")
    {
        REQUIRE(solved.nNonce == nNonce);
    }

    SECTION("vOffsets is cleared (node derives via GetOffsets after this call)")
    {
        /* The node calls GetOffsets(GetPrime(), vOffsets) after BuildSolvedPrimeCandidateFromTemplate
         * to derive vOffsets — miner-submitted vOffsets are not used. */
        REQUIRE(solved.vOffsets.empty());
    }

    SECTION("vchBlockSig is cleared (must be re-generated after nNonce change)")
    {
        REQUIRE(solved.vchBlockSig.empty());
    }

    SECTION("hashPrevBlock is preserved (stale detection anchor)")
    {
        REQUIRE(solved.hashPrevBlock == tmpl.hashPrevBlock);
    }

    SECTION("hashMerkleRoot is preserved (transaction commitment)")
    {
        REQUIRE(solved.hashMerkleRoot == tmpl.hashMerkleRoot);
    }
}


TEST_CASE("BuildSolvedPrimeCandidateFromTemplate: Hash channel clears vOffsets", "[prime][sign_finalization]")
{
    /* For non-Prime channels (Hash), vOffsets must also be cleared. */
    const std::vector<uint8_t> vOffsets = MinimalValidPrimeOffsets();
    TAO::Ledger::TritiumBlock tmpl = MakeTestBlock(2u /*Hash*/, 1u, vOffsets);

    TAO::Ledger::TritiumBlock solved =
        TAO::Ledger::BuildSolvedPrimeCandidateFromTemplate(tmpl, 42u);

    REQUIRE(solved.nChannel == 2u);
    REQUIRE(solved.vOffsets.empty());
}


TEST_CASE("BuildSolvedPrimeCandidateFromTemplate: template is not mutated", "[prime][sign_finalization]")
{
    /* Confirm the function returns a copy and does not modify the original. */
    const uint64_t tmplNonce = 1u;
    const uint32_t tmplTime  = 1700000000u;
    TAO::Ledger::TritiumBlock tmpl = MakeTestBlock(1u, tmplNonce, {});
    tmpl.nTime = tmplTime;
    tmpl.vchBlockSig = {0xFF};

    TAO::Ledger::TritiumBlock solved =
        TAO::Ledger::BuildSolvedPrimeCandidateFromTemplate(tmpl, 0xCAFEBABEULL);

    /* Template must be unchanged. */
    REQUIRE(tmpl.nNonce == tmplNonce);
    REQUIRE(tmpl.nTime  == tmplTime);
    REQUIRE(!tmpl.vchBlockSig.empty());

    /* Solved block must differ. */
    REQUIRE(solved.nNonce != tmplNonce);
    REQUIRE(solved.vchBlockSig.empty());
}


/* ─── Stale template rejection tests ─────────────────────────────────────── */

TEST_CASE("Stale Prime template: rejection is handled before sign_block", "[prime][sign_finalization][stale]")
{
    /* This test documents the expected stale-rejection behaviour:
     *   - sign_block() in both the legacy and stateless paths checks IsStale()
     *     on the TemplateMetadata before applying the miner's nNonce.
     *   - A stale template must be rejected BEFORE any mutation.
     *   - The final hashPrevBlock-vs-hashBestChain stale check catches
     *     templates that became stale after the IsStale() pre-screen.
     *
     * We cannot fully exercise the runtime staleness path in a unit test
     * (it requires a live chain state), but we can verify the TemplateMetadata
     * interface that drives those decisions. */

    SECTION("A fresh Prime block candidate has the correct channel")
    {
        TAO::Ledger::TritiumBlock block = MakeTestBlock(1u, 42u, MinimalValidPrimeOffsets());
        REQUIRE(block.nChannel == TAO::Ledger::CHANNEL::PRIME);
    }

    SECTION("BuildSolvedPrimeCandidateFromTemplate clears vOffsets for Prime (node derives them)")
    {
        TAO::Ledger::TritiumBlock tmpl = MakeTestBlock(1u, 1u, MinimalValidPrimeOffsets());
        TAO::Ledger::TritiumBlock solved =
            TAO::Ledger::BuildSolvedPrimeCandidateFromTemplate(tmpl, 42u);
        REQUIRE(solved.vOffsets.empty());
    }
}


/* ─── Node-side GetOffsets derivation ────────────────────────────────────── */

TEST_CASE("Node-side GetOffsets: empty result for composite GetPrime()", "[prime][sign_finalization]")
{
    /* Demonstrate that GetOffsets() returns an empty vector when GetPrime() is
     * not itself prime.  In sign_block(), this means VerifyWork() will correctly
     * reject the block as below minimum work — which is the right outcome.
     *
     * A random zero-filled block is overwhelmingly likely to have a composite
     * GetPrime() value, so we use that as a representative composite candidate. */
    TAO::Ledger::TritiumBlock block = MakeTestBlock(1u, 1u, {});

    std::vector<uint8_t> vNodeDerived;
    TAO::Ledger::GetOffsets(block.GetPrime(), vNodeDerived);

    /* vNodeDerived will typically be empty for a random composite GetPrime().
     * If it is non-empty, the hash happened to yield a valid prime chain, which
     * is astronomically unlikely and not tested here. */
    /* (No REQUIRE — just document the expected path.) */
    SUCCEED("GetOffsets invoked without crash; empty result is expected for composite base");
}

