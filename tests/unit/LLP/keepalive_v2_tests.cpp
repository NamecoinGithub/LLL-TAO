/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <LLP/include/keepalive_v2.h>

#include <array>
#include <cstdint>
#include <vector>

/*
 * Unit tests for KeepaliveV2 utility.
 *
 * Covers:
 *   - ParsePayload: v1 (len==4) and v2 (len==8), edge cases
 *   - ParsePayload: v2 suffix returned as raw bytes (no endian conversion)
 *   - BuildBestCurrentResponse: correct size (28 bytes), field positions, endianness
 */

TEST_CASE("KeepaliveV2::ParsePayload", "[keepalive_v2][llp]")
{
    using namespace LLP::KeepaliveV2;

    SECTION("v1 payload (len==4) returns false and zero suffix bytes")
    {
        std::vector<uint8_t> data = { 0x01, 0x02, 0x03, 0x04 };

        uint32_t nSessionId = 0xFFFFFFFF;
        std::array<uint8_t, 4> suffixBytes = {0xFF, 0xFF, 0xFF, 0xFF};

        bool fIsV2 = ParsePayload(data, nSessionId, suffixBytes);

        REQUIRE(fIsV2 == false);
        /* session_id = 0x04030201 (little-endian) */
        REQUIRE(nSessionId == 0x04030201u);
        /* suffix bytes must all be zeroed for v1 */
        REQUIRE(suffixBytes[0] == 0u);
        REQUIRE(suffixBytes[1] == 0u);
        REQUIRE(suffixBytes[2] == 0u);
        REQUIRE(suffixBytes[3] == 0u);
    }

    SECTION("v2 payload (len==8) returns true and raw suffix bytes as-sent")
    {
        /* session_id  = 0xDEADBEEF (LE: EF BE AD DE)
         * suffix bytes as-sent: 78 56 34 12 */
        std::vector<uint8_t> data = {
            0xEF, 0xBE, 0xAD, 0xDE,   /* session_id LE */
            0x78, 0x56, 0x34, 0x12    /* prevblock_suffix raw bytes */
        };

        uint32_t nSessionId = 0;
        std::array<uint8_t, 4> suffixBytes = {};

        bool fIsV2 = ParsePayload(data, nSessionId, suffixBytes);

        REQUIRE(fIsV2 == true);
        REQUIRE(nSessionId == 0xDEADBEEFu);
        /* Bytes returned exactly as-sent (no endian conversion) */
        REQUIRE(suffixBytes[0] == 0x78u);
        REQUIRE(suffixBytes[1] == 0x56u);
        REQUIRE(suffixBytes[2] == 0x34u);
        REQUIRE(suffixBytes[3] == 0x12u);
    }

    SECTION("v2 payload with zero suffix (no template)")
    {
        std::vector<uint8_t> data = {
            0x01, 0x00, 0x00, 0x00,   /* session_id = 1 */
            0x00, 0x00, 0x00, 0x00    /* suffix = all zeros */
        };

        uint32_t nSessionId = 0;
        std::array<uint8_t, 4> suffixBytes = {0xFF, 0xFF, 0xFF, 0xFF};

        bool fIsV2 = ParsePayload(data, nSessionId, suffixBytes);

        REQUIRE(fIsV2 == true);
        REQUIRE(nSessionId == 1u);
        REQUIRE(suffixBytes[0] == 0u);
        REQUIRE(suffixBytes[1] == 0u);
        REQUIRE(suffixBytes[2] == 0u);
        REQUIRE(suffixBytes[3] == 0u);
    }

    SECTION("Payload shorter than 4 bytes returns false")
    {
        std::vector<uint8_t> data = { 0x01, 0x02, 0x03 };

        uint32_t nSessionId = 0xAA;
        std::array<uint8_t, 4> suffixBytes = {0xBB, 0xBB, 0xBB, 0xBB};

        bool fIsV2 = ParsePayload(data, nSessionId, suffixBytes);

        REQUIRE(fIsV2 == false);
        /* suffix bytes must be zeroed even on short input */
        REQUIRE(suffixBytes[0] == 0u);
        REQUIRE(suffixBytes[1] == 0u);
        REQUIRE(suffixBytes[2] == 0u);
        REQUIRE(suffixBytes[3] == 0u);
    }

    SECTION("Empty payload returns false")
    {
        std::vector<uint8_t> data;

        uint32_t nSessionId = 0xAA;
        std::array<uint8_t, 4> suffixBytes = {0xBB, 0xBB, 0xBB, 0xBB};

        bool fIsV2 = ParsePayload(data, nSessionId, suffixBytes);

        REQUIRE(fIsV2 == false);
        REQUIRE(suffixBytes[0] == 0u);
        REQUIRE(suffixBytes[1] == 0u);
        REQUIRE(suffixBytes[2] == 0u);
        REQUIRE(suffixBytes[3] == 0u);
    }

    SECTION("Payload exactly 5..7 bytes treated as v1 (no suffix)")
    {
        std::vector<uint8_t> data = { 0x04, 0x03, 0x02, 0x01, 0xFF, 0xEE, 0xDD };

        uint32_t nSessionId = 0;
        std::array<uint8_t, 4> suffixBytes = {0xFF, 0xFF, 0xFF, 0xFF};

        bool fIsV2 = ParsePayload(data, nSessionId, suffixBytes);

        REQUIRE(fIsV2 == false);
        REQUIRE(nSessionId == 0x01020304u);
        /* suffix bytes must be zero because we did not have 8 bytes */
        REQUIRE(suffixBytes[0] == 0u);
        REQUIRE(suffixBytes[1] == 0u);
        REQUIRE(suffixBytes[2] == 0u);
        REQUIRE(suffixBytes[3] == 0u);
    }

    SECTION("v2 suffix bytes preserve wire-order (no endian swap)")
    {
        /* Bytes [4..7] on the wire: AA BB CC DD
         * Expected: suffixBytes[0]=AA, [1]=BB, [2]=CC, [3]=DD */
        std::vector<uint8_t> data = {
            0x01, 0x00, 0x00, 0x00,  /* session_id = 1 */
            0xAA, 0xBB, 0xCC, 0xDD   /* suffix bytes in wire order */
        };

        uint32_t nSessionId = 0;
        std::array<uint8_t, 4> suffixBytes = {};

        bool fIsV2 = ParsePayload(data, nSessionId, suffixBytes);

        REQUIRE(fIsV2 == true);
        REQUIRE(suffixBytes[0] == 0xAAu);
        REQUIRE(suffixBytes[1] == 0xBBu);
        REQUIRE(suffixBytes[2] == 0xCCu);
        REQUIRE(suffixBytes[3] == 0xDDu);
    }
}


TEST_CASE("KeepaliveV2::BuildBestCurrentResponse", "[keepalive_v2][llp]")
{
    using namespace LLP::KeepaliveV2;

    SECTION("Response is exactly 28 bytes")
    {
        uint1024_t hash;
        std::vector<uint8_t> v = BuildBestCurrentResponse(1, 2, 3, 4, 5, 6, hash);
        REQUIRE(v.size() == 28u);
    }

    SECTION("session_id encoded little-endian at bytes [0..3]")
    {
        /* session_id = 0xDEADBEEF
         * Expected LE bytes: EF BE AD DE */
        uint32_t nSessionId = 0xDEADBEEFu;
        uint1024_t hash;
        std::vector<uint8_t> v = BuildBestCurrentResponse(nSessionId, 0, 0, 0, 0, 0, hash);

        REQUIRE(v[0] == 0xEFu);
        REQUIRE(v[1] == 0xBEu);
        REQUIRE(v[2] == 0xADu);
        REQUIRE(v[3] == 0xDEu);
    }

    SECTION("unified_height encoded big-endian at bytes [4..7]")
    {
        /* unified_height = 0x01020304 → BE bytes: 01 02 03 04 */
        uint32_t nUnifiedHeight = 0x01020304u;
        uint1024_t hash;
        std::vector<uint8_t> v = BuildBestCurrentResponse(0, nUnifiedHeight, 0, 0, 0, 0, hash);

        REQUIRE(v[4] == 0x01u);
        REQUIRE(v[5] == 0x02u);
        REQUIRE(v[6] == 0x03u);
        REQUIRE(v[7] == 0x04u);
    }

    SECTION("prime_height encoded big-endian at bytes [8..11]")
    {
        uint32_t nPrimeHeight = 0xAABBCCDDu;
        uint1024_t hash;
        std::vector<uint8_t> v = BuildBestCurrentResponse(0, 0, nPrimeHeight, 0, 0, 0, hash);

        REQUIRE(v[8]  == 0xAAu);
        REQUIRE(v[9]  == 0xBBu);
        REQUIRE(v[10] == 0xCCu);
        REQUIRE(v[11] == 0xDDu);
    }

    SECTION("hash_height encoded big-endian at bytes [12..15]")
    {
        uint32_t nHashHeight = 0x11223344u;
        uint1024_t hash;
        std::vector<uint8_t> v = BuildBestCurrentResponse(0, 0, 0, nHashHeight, 0, 0, hash);

        REQUIRE(v[12] == 0x11u);
        REQUIRE(v[13] == 0x22u);
        REQUIRE(v[14] == 0x33u);
        REQUIRE(v[15] == 0x44u);
    }

    SECTION("stake_height encoded big-endian at bytes [16..19]")
    {
        uint32_t nStakeHeight = 0xFEDCBA98u;
        uint1024_t hash;
        std::vector<uint8_t> v = BuildBestCurrentResponse(0, 0, 0, 0, nStakeHeight, 0, hash);

        REQUIRE(v[16] == 0xFEu);
        REQUIRE(v[17] == 0xDCu);
        REQUIRE(v[18] == 0xBAu);
        REQUIRE(v[19] == 0x98u);
    }

    SECTION("nBits encoded big-endian at bytes [20..23]")
    {
        uint32_t nBits = 0x1D00FFFFu;
        uint1024_t hash;
        std::vector<uint8_t> v = BuildBestCurrentResponse(0, 0, 0, 0, 0, nBits, hash);

        REQUIRE(v[20] == 0x1Du);
        REQUIRE(v[21] == 0x00u);
        REQUIRE(v[22] == 0xFFu);
        REQUIRE(v[23] == 0xFFu);
    }

    SECTION("hashBestChain_prefix occupies bytes [24..27] (first 4 bytes of GetBytes())")
    {
        /* Construct a uint1024_t where the first 4 bytes of GetBytes() are known.
         * GetBytes() returns the internal limb storage in little-endian word order.
         * The simplest way is to set the lowest-order limb to a known value. */
        uint1024_t hash(0u);

        /* Set to a known 32-bit value so the first limb has predictable bytes */
        hash.SetHex("AABBCCDD");  /* lowest limb = 0x0000...AABBCCDD */

        std::vector<uint8_t> v = BuildBestCurrentResponse(0, 0, 0, 0, 0, 0, hash);

        /* GetBytes() returns LE bytes of the internal representation.
         * For a value 0xAABBCCDD the first 4 bytes should be DD CC BB AA (LE). */
        std::vector<uint8_t> vHashBytes = hash.GetBytes();
        REQUIRE(vHashBytes.size() >= 4u);

        REQUIRE(v[24] == vHashBytes[0]);
        REQUIRE(v[25] == vHashBytes[1]);
        REQUIRE(v[26] == vHashBytes[2]);
        REQUIRE(v[27] == vHashBytes[3]);
    }

    SECTION("All fields together - full round-trip sanity check")
    {
        uint32_t nSessionId     = 0x00000042u;
        uint32_t nUnifiedHeight = 100u;
        uint32_t nPrimeHeight   = 200u;
        uint32_t nHashHeight    = 300u;
        uint32_t nStakeHeight   = 400u;
        uint32_t nBits          = 0x1D00FFFFu;
        uint1024_t hash(0u);

        std::vector<uint8_t> v = BuildBestCurrentResponse(
            nSessionId, nUnifiedHeight, nPrimeHeight,
            nHashHeight, nStakeHeight, nBits, hash);

        REQUIRE(v.size() == 28u);

        /* session_id LE */
        REQUIRE(v[0] == 0x42u);
        REQUIRE(v[1] == 0x00u);
        REQUIRE(v[2] == 0x00u);
        REQUIRE(v[3] == 0x00u);

        /* unified_height BE */
        REQUIRE(v[4] == 0x00u);
        REQUIRE(v[5] == 0x00u);
        REQUIRE(v[6] == 0x00u);
        REQUIRE(v[7] == 100u);

        /* prime_height BE */
        REQUIRE(v[8]  == 0x00u);
        REQUIRE(v[9]  == 0x00u);
        REQUIRE(v[10] == 0x00u);
        REQUIRE(v[11] == 200u);

        /* hash_height BE */
        REQUIRE(v[12] == 0x00u);
        REQUIRE(v[13] == 0x00u);
        REQUIRE(v[14] == 0x01u);   /* 300 = 0x0000012C */
        REQUIRE(v[15] == 0x2Cu);

        /* stake_height BE */
        REQUIRE(v[16] == 0x00u);
        REQUIRE(v[17] == 0x00u);
        REQUIRE(v[18] == 0x01u);   /* 400 = 0x00000190 */
        REQUIRE(v[19] == 0x90u);

        /* nBits BE */
        REQUIRE(v[20] == 0x1Du);
        REQUIRE(v[21] == 0x00u);
        REQUIRE(v[22] == 0xFFu);
        REQUIRE(v[23] == 0xFFu);
    }
}


TEST_CASE("KeepaliveV2 - backward compatibility", "[keepalive_v2][llp]")
{
    using namespace LLP::KeepaliveV2;

    SECTION("v1 keepalive zeros all suffix bytes (does not write into miner prevblock slot)")
    {
        std::vector<uint8_t> data = { 0xAA, 0xBB, 0xCC, 0xDD };

        uint32_t nSessionId = 0;
        std::array<uint8_t, 4> suffixBytes = {0x12, 0x34, 0x56, 0x78};  /* pre-loaded with garbage */

        bool fIsV2 = ParsePayload(data, nSessionId, suffixBytes);

        REQUIRE(fIsV2 == false);
        REQUIRE(suffixBytes[0] == 0u);  /* MUST be zeroed */
        REQUIRE(suffixBytes[1] == 0u);
        REQUIRE(suffixBytes[2] == 0u);
        REQUIRE(suffixBytes[3] == 0u);
    }

    SECTION("v2 keepalive with zero suffix is valid (miner has no template yet)")
    {
        std::vector<uint8_t> data = {
            0x01, 0x00, 0x00, 0x00,  /* session_id = 1 */
            0x00, 0x00, 0x00, 0x00   /* suffix = 0 */
        };

        uint32_t nSessionId = 0;
        std::array<uint8_t, 4> suffixBytes = {0xDE, 0xAD, 0x00, 0x00};

        bool fIsV2 = ParsePayload(data, nSessionId, suffixBytes);

        REQUIRE(fIsV2 == true);
        REQUIRE(suffixBytes[0] == 0u);
        REQUIRE(suffixBytes[1] == 0u);
        REQUIRE(suffixBytes[2] == 0u);
        REQUIRE(suffixBytes[3] == 0u);
    }
}


// ============================================================================
// Tests for the new KEEPALIVE_V2 / KEEPALIVE_V2_ACK opcode constants and
// frame structs introduced to fix the ACK payload size (8 → 28 bytes).
// ============================================================================

TEST_CASE("KeepaliveV2Opcodes - constants", "[keepalive_v2][llp]")
{
    using namespace LLP::KeepaliveV2Opcodes;

    SECTION("KEEPALIVE_V2 opcode is 0xD0E2")
    {
        REQUIRE(KEEPALIVE_V2 == static_cast<uint16_t>(0xD0E2u));
    }

    SECTION("KEEPALIVE_V2_ACK opcode is 0xD0E3")
    {
        REQUIRE(KEEPALIVE_V2_ACK == static_cast<uint16_t>(0xD0E3u));
    }

    SECTION("Request payload size is 8 bytes (Miner → Node)")
    {
        REQUIRE(KEEPALIVE_V2_REQUEST_PAYLOAD_SIZE == 8u);
    }

    SECTION("ACK payload size is 28 bytes (Node → Miner) — the corrected value")
    {
        REQUIRE(KEEPALIVE_V2_ACK_PAYLOAD_SIZE == 28u);
    }

    SECTION("Request and ACK sizes are asymmetric")
    {
        REQUIRE(KEEPALIVE_V2_REQUEST_PAYLOAD_SIZE != KEEPALIVE_V2_ACK_PAYLOAD_SIZE);
    }
}


TEST_CASE("KeepAliveV2Frame - Miner→Node 8-byte request", "[keepalive_v2][llp]")
{
    using LLP::KeepAliveV2Frame;

    SECTION("PAYLOAD_SIZE constant is 8")
    {
        REQUIRE(KeepAliveV2Frame::PAYLOAD_SIZE == 8u);
    }

    SECTION("Serialize produces exactly 8 bytes")
    {
        KeepAliveV2Frame f;
        f.sequence           = 1;
        f.hashPrevBlock_lo32 = 2;
        auto v = f.Serialize();
        REQUIRE(v.size() == 8u);
    }

    SECTION("Serialize encodes sequence big-endian at bytes [0..3]")
    {
        KeepAliveV2Frame f;
        f.sequence           = 0x01020304u;
        f.hashPrevBlock_lo32 = 0u;
        auto v = f.Serialize();

        REQUIRE(v[0] == 0x01u);
        REQUIRE(v[1] == 0x02u);
        REQUIRE(v[2] == 0x03u);
        REQUIRE(v[3] == 0x04u);
    }

    SECTION("Serialize encodes hashPrevBlock_lo32 big-endian at bytes [4..7]")
    {
        KeepAliveV2Frame f;
        f.sequence           = 0u;
        f.hashPrevBlock_lo32 = 0xDEADBEEFu;
        auto v = f.Serialize();

        REQUIRE(v[4] == 0xDEu);
        REQUIRE(v[5] == 0xADu);
        REQUIRE(v[6] == 0xBEu);
        REQUIRE(v[7] == 0xEFu);
    }

    SECTION("Parse rejects fewer than 8 bytes")
    {
        KeepAliveV2Frame f;
        std::vector<uint8_t> data(7, 0xFF);
        REQUIRE(f.Parse(data) == false);
    }

    SECTION("Parse rejects empty data")
    {
        KeepAliveV2Frame f;
        std::vector<uint8_t> data;
        REQUIRE(f.Parse(data) == false);
    }

    SECTION("Parse decodes sequence big-endian from bytes [0..3]")
    {
        KeepAliveV2Frame f;
        std::vector<uint8_t> data = {
            0x01, 0x02, 0x03, 0x04,  /* sequence = 0x01020304 */
            0x00, 0x00, 0x00, 0x00   /* hashPrevBlock_lo32 = 0 */
        };
        REQUIRE(f.Parse(data) == true);
        REQUIRE(f.sequence == 0x01020304u);
    }

    SECTION("Parse decodes hashPrevBlock_lo32 big-endian from bytes [4..7]")
    {
        KeepAliveV2Frame f;
        std::vector<uint8_t> data = {
            0x00, 0x00, 0x00, 0x00,  /* sequence = 0 */
            0xDE, 0xAD, 0xBE, 0xEF  /* hashPrevBlock_lo32 = 0xDEADBEEF */
        };
        REQUIRE(f.Parse(data) == true);
        REQUIRE(f.hashPrevBlock_lo32 == 0xDEADBEEFu);
    }

    SECTION("Serialize/Parse round-trip")
    {
        KeepAliveV2Frame orig;
        orig.sequence           = 0xCAFEBABEu;
        orig.hashPrevBlock_lo32 = 0x12345678u;

        auto wire = orig.Serialize();

        KeepAliveV2Frame parsed;
        REQUIRE(parsed.Parse(wire) == true);
        REQUIRE(parsed.sequence           == orig.sequence);
        REQUIRE(parsed.hashPrevBlock_lo32 == orig.hashPrevBlock_lo32);
    }
}


TEST_CASE("KeepAliveV2AckFrame - Node→Miner 28-byte response", "[keepalive_v2][llp]")
{
    using LLP::KeepAliveV2AckFrame;

    SECTION("PAYLOAD_SIZE constant is 28 — the corrected value")
    {
        REQUIRE(KeepAliveV2AckFrame::PAYLOAD_SIZE == 28u);
    }

    SECTION("Serialize produces exactly 28 bytes")
    {
        KeepAliveV2AckFrame f;
        auto v = f.Serialize();
        REQUIRE(v.size() == 28u);
    }

    SECTION("sequence echoed big-endian at bytes [0..3]")
    {
        KeepAliveV2AckFrame f;
        f.sequence = 0x0A0B0C0Du;
        auto v = f.Serialize();
        REQUIRE(v[0] == 0x0Au);
        REQUIRE(v[1] == 0x0Bu);
        REQUIRE(v[2] == 0x0Cu);
        REQUIRE(v[3] == 0x0Du);
    }

    SECTION("hashPrevBlock_lo32 at bytes [4..7]")
    {
        KeepAliveV2AckFrame f;
        f.hashPrevBlock_lo32 = 0xAABBCCDDu;
        auto v = f.Serialize();
        REQUIRE(v[4] == 0xAAu);
        REQUIRE(v[5] == 0xBBu);
        REQUIRE(v[6] == 0xCCu);
        REQUIRE(v[7] == 0xDDu);
    }

    SECTION("unified_height at bytes [8..11]")
    {
        KeepAliveV2AckFrame f;
        f.unified_height = 0x00123456u;
        auto v = f.Serialize();
        REQUIRE(v[8]  == 0x00u);
        REQUIRE(v[9]  == 0x12u);
        REQUIRE(v[10] == 0x34u);
        REQUIRE(v[11] == 0x56u);
    }

    SECTION("hash_tip_lo32 at bytes [12..15]")
    {
        KeepAliveV2AckFrame f;
        f.hash_tip_lo32 = 0x11223344u;
        auto v = f.Serialize();
        REQUIRE(v[12] == 0x11u);
        REQUIRE(v[13] == 0x22u);
        REQUIRE(v[14] == 0x33u);
        REQUIRE(v[15] == 0x44u);
    }

    SECTION("prime_height at bytes [16..19]")
    {
        KeepAliveV2AckFrame f;
        f.prime_height = 0xFEDCBA98u;
        auto v = f.Serialize();
        REQUIRE(v[16] == 0xFEu);
        REQUIRE(v[17] == 0xDCu);
        REQUIRE(v[18] == 0xBAu);
        REQUIRE(v[19] == 0x98u);
    }

    SECTION("hash_height at bytes [20..23]")
    {
        KeepAliveV2AckFrame f;
        f.hash_height = 0x0000007Bu;
        auto v = f.Serialize();
        REQUIRE(v[20] == 0x00u);
        REQUIRE(v[21] == 0x00u);
        REQUIRE(v[22] == 0x00u);
        REQUIRE(v[23] == 0x7Bu);
    }

    SECTION("fork_score at bytes [24..27]")
    {
        KeepAliveV2AckFrame f;
        f.fork_score = 0u;  /* 0 = healthy */
        auto v = f.Serialize();
        REQUIRE(v[24] == 0x00u);
        REQUIRE(v[25] == 0x00u);
        REQUIRE(v[26] == 0x00u);
        REQUIRE(v[27] == 0x00u);
    }

    SECTION("Parse rejects fewer than 28 bytes")
    {
        KeepAliveV2AckFrame f;
        std::vector<uint8_t> data(27, 0xFF);
        REQUIRE(f.Parse(data) == false);
    }

    SECTION("Parse rejects empty data")
    {
        KeepAliveV2AckFrame f;
        std::vector<uint8_t> data;
        REQUIRE(f.Parse(data) == false);
    }

    SECTION("Serialize/Parse round-trip - all fields")
    {
        KeepAliveV2AckFrame orig;
        orig.sequence           = 0xCAFEBABEu;
        orig.hashPrevBlock_lo32 = 0x12345678u;
        orig.unified_height     = 1000000u;
        orig.hash_tip_lo32      = 0xDEADBEEFu;
        orig.prime_height       = 500u;
        orig.hash_height        = 750u;
        orig.fork_score         = 0u;

        auto wire = orig.Serialize();
        REQUIRE(wire.size() == 28u);

        KeepAliveV2AckFrame parsed;
        REQUIRE(parsed.Parse(wire) == true);
        REQUIRE(parsed.sequence           == orig.sequence);
        REQUIRE(parsed.hashPrevBlock_lo32 == orig.hashPrevBlock_lo32);
        REQUIRE(parsed.unified_height     == orig.unified_height);
        REQUIRE(parsed.hash_tip_lo32      == orig.hash_tip_lo32);
        REQUIRE(parsed.prime_height       == orig.prime_height);
        REQUIRE(parsed.hash_height        == orig.hash_height);
        REQUIRE(parsed.fork_score         == orig.fork_score);
    }
}
