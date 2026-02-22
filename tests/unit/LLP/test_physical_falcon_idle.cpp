/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <LLP/include/falcon_constants.h>

#include <TAO/Ledger/types/block.h>
#include <TAO/Ledger/types/tritium.h>

#include <Util/templates/datastream.h>
#include <LLP/include/version.h>

#include <vector>
#include <stdexcept>


/* ─────────────────────────────────────────────────────────────────────────────
 * Helpers
 * ───────────────────────────────────────────────────────────────────────────*/

/** Build a minimal block header (nVersion gated by caller). **/
static TAO::Ledger::TritiumBlock MakeBlock(uint32_t nVersion)
{
    TAO::Ledger::Block base(nVersion, uint1024_t(0), 1 /* channel */, 1 /* height */);
    TAO::Ledger::TritiumBlock blk(base);
    blk.nTime = 1000000000u;
    return blk;
}

/** Return the serialized bytes of a TritiumBlock. **/
static std::vector<uint8_t> Serialize(const TAO::Ledger::TritiumBlock& blk)
{
    DataStream ss(SER_NETWORK, LLP::PROTOCOL_VERSION);
    ss << blk;
    return std::vector<uint8_t>(ss.begin(), ss.end());
}

/** Deserialize bytes back into a TritiumBlock. **/
static TAO::Ledger::TritiumBlock Deserialize(const std::vector<uint8_t>& vBytes)
{
    DataStream ss(vBytes, SER_NETWORK, LLP::PROTOCOL_VERSION);
    TAO::Ledger::TritiumBlock blk;
    ss >> blk;
    return blk;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * GROUP 0 — Constants sanity checks
 * ───────────────────────────────────────────────────────────────────────────*/
TEST_CASE("Physical Falcon constants are correct", "[physical_falcon][constants]")
{
    using namespace LLP::FalconConstants;

    SECTION("PHYSICAL_FALCON_BLOCK_VERSION is one above current production maximum (8)")
    {
        /* Current production maximum is NETWORK_BLOCK_CURRENT_VERSION = 8. */
        REQUIRE(PHYSICAL_FALCON_BLOCK_VERSION == 9u);
    }

    SECTION("PHYSICAL_FALCON_ENFORCEMENT is false (idle/dormant)")
    {
        REQUIRE(PHYSICAL_FALCON_ENFORCEMENT == false);
    }

    SECTION("FALCON_CT_SIG_SIZE_512 < FALCON_CT_SIG_SIZE_1024")
    {
        REQUIRE(FALCON_CT_SIG_SIZE_512 < FALCON_CT_SIG_SIZE_1024);
    }

    SECTION("FALCON_CT_SIG_SIZE_512 is 897 (Falcon-512 CT exact)")
    {
        REQUIRE(FALCON_CT_SIG_SIZE_512 == 897u);
    }

    SECTION("FALCON_CT_SIG_SIZE_1024 is 1577 (Falcon-1024 CT exact)")
    {
        REQUIRE(FALCON_CT_SIG_SIZE_1024 == 1577u);
    }

    SECTION("FALCON_CT_SIG_SIZE_1024 fits in uint16_t")
    {
        REQUIRE(FALCON_CT_SIG_SIZE_1024 <= 65535u);
    }

    SECTION("PHYSICAL_FALCON1024_PUBKEY_SIZE is 1793 (standard Falcon-1024 pubkey)")
    {
        REQUIRE(PHYSICAL_FALCON1024_PUBKEY_SIZE == 1793u);
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * GROUP 1 (T01-T03) — Pre-threshold blocks: no physical sig field in stream
 * ───────────────────────────────────────────────────────────────────────────*/
TEST_CASE("T01-T03: Pre-threshold block serialization unchanged", "[physical_falcon][serialize][pre_threshold]")
{
    using namespace LLP::FalconConstants;

    SECTION("T01: Fields default to empty / zero on a pre-threshold block")
    {
        TAO::Ledger::TritiumBlock blk = MakeBlock(1);
        REQUIRE(blk.vchPhysicalFalconSig.empty());
        REQUIRE(blk.hashPhysicalFalconKeyID == uint256_t(0));
    }

    SECTION("T02: GetSerializeSize for version 7 does not include physical sig overhead")
    {
        TAO::Ledger::TritiumBlock blk7 = MakeBlock(7);
        TAO::Ledger::TritiumBlock blk9 = MakeBlock(PHYSICAL_FALCON_BLOCK_VERSION);

        uint64_t size7 = blk7.GetSerializeSize(SER_NETWORK, LLP::PROTOCOL_VERSION);
        uint64_t size9 = blk9.GetSerializeSize(SER_NETWORK, LLP::PROTOCOL_VERSION);

        /* version 9 block has physiglen (2 bytes) overhead even with no sig */
        REQUIRE(size9 == size7 + sizeof(uint16_t));
    }

    SECTION("T03: Round-trip for version 1 block leaves physical sig fields empty")
    {
        TAO::Ledger::TritiumBlock orig = MakeBlock(1);
        std::vector<uint8_t> vBytes   = Serialize(orig);
        TAO::Ledger::TritiumBlock recv = Deserialize(vBytes);

        REQUIRE(recv.nVersion == 1u);
        REQUIRE(recv.vchPhysicalFalconSig.empty());
        REQUIRE(recv.hashPhysicalFalconKeyID == uint256_t(0));
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * GROUP 2 (T04-T06) — Post-threshold, no physical sig (physiglen == 0)
 * ───────────────────────────────────────────────────────────────────────────*/
TEST_CASE("T04-T06: Post-threshold block with physiglen=0 serialization", "[physical_falcon][serialize][no_sig]")
{
    using namespace LLP::FalconConstants;

    SECTION("T04: GetSerializeSize with empty sig adds exactly 2 bytes vs pre-threshold")
    {
        TAO::Ledger::TritiumBlock blkPre  = MakeBlock(PHYSICAL_FALCON_BLOCK_VERSION - 1);
        TAO::Ledger::TritiumBlock blkPost = MakeBlock(PHYSICAL_FALCON_BLOCK_VERSION);

        uint64_t sizePre  = blkPre.GetSerializeSize(SER_NETWORK, LLP::PROTOCOL_VERSION);
        uint64_t sizePost = blkPost.GetSerializeSize(SER_NETWORK, LLP::PROTOCOL_VERSION);

        REQUIRE(sizePost == sizePre + sizeof(uint16_t));
    }

    SECTION("T05: Serialized bytes for empty sig end with two zero bytes (physiglen == 0)")
    {
        TAO::Ledger::TritiumBlock blk = MakeBlock(PHYSICAL_FALCON_BLOCK_VERSION);
        REQUIRE(blk.vchPhysicalFalconSig.empty());

        std::vector<uint8_t> vBytes = Serialize(blk);
        REQUIRE(vBytes.size() >= 2u);

        /* Last 2 bytes must be 0x00 0x00 (uint16_t zero, little-endian) */
        REQUIRE(vBytes[vBytes.size() - 2] == 0x00);
        REQUIRE(vBytes[vBytes.size() - 1] == 0x00);
    }

    SECTION("T06: Round-trip preserves empty physical sig on post-threshold block")
    {
        TAO::Ledger::TritiumBlock orig = MakeBlock(PHYSICAL_FALCON_BLOCK_VERSION);
        std::vector<uint8_t> vBytes   = Serialize(orig);
        TAO::Ledger::TritiumBlock recv = Deserialize(vBytes);

        REQUIRE(recv.nVersion == PHYSICAL_FALCON_BLOCK_VERSION);
        REQUIRE(recv.vchPhysicalFalconSig.empty());
        REQUIRE(recv.hashPhysicalFalconKeyID == uint256_t(0));
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * GROUP 3 (T07-T09) — Post-threshold block with a dummy physical sig
 * ───────────────────────────────────────────────────────────────────────────*/
TEST_CASE("T07-T09: Post-threshold block with dummy physical sig round-trip", "[physical_falcon][serialize][with_sig]")
{
    using namespace LLP::FalconConstants;

    /* Build a dummy sig of valid size (Falcon-512 CT exact size) */
    static const size_t DUMMY_SIG_SIZE = FALCON_CT_SIG_SIZE_512;  // 897 bytes
    std::vector<uint8_t> vDummySig(DUMMY_SIG_SIZE, 0xAB);
    uint256_t dummyKeyID(0x1234567890ABCDEF);

    SECTION("T07: GetSerializeSize with sig includes keyID + sig bytes")
    {
        TAO::Ledger::TritiumBlock blkNoSig  = MakeBlock(PHYSICAL_FALCON_BLOCK_VERSION);
        TAO::Ledger::TritiumBlock blkWithSig = MakeBlock(PHYSICAL_FALCON_BLOCK_VERSION);
        blkWithSig.vchPhysicalFalconSig    = vDummySig;
        blkWithSig.hashPhysicalFalconKeyID = dummyKeyID;

        uint64_t sizeNoSig   = blkNoSig.GetSerializeSize(SER_NETWORK, LLP::PROTOCOL_VERSION);
        uint64_t sizeWithSig = blkWithSig.GetSerializeSize(SER_NETWORK, LLP::PROTOCOL_VERSION);

        /* Expected extra: hashKeyID (32 bytes) + sig bytes */
        uint64_t expected = sizeNoSig + sizeof(uint256_t) + DUMMY_SIG_SIZE;
        REQUIRE(sizeWithSig == expected);
    }

    SECTION("T08: Serialized physiglen field encodes actual sig size")
    {
        TAO::Ledger::TritiumBlock blk = MakeBlock(PHYSICAL_FALCON_BLOCK_VERSION);
        blk.vchPhysicalFalconSig    = vDummySig;
        blk.hashPhysicalFalconKeyID = dummyKeyID;

        std::vector<uint8_t> vBytes = Serialize(blk);

        /* physiglen is the first 2 bytes AFTER the pre-threshold block payload.
         * Pre-threshold size is obtained from a block at nVersion-1. */
        TAO::Ledger::TritiumBlock blkRef = MakeBlock(PHYSICAL_FALCON_BLOCK_VERSION - 1);
        uint64_t refSize = blkRef.GetSerializeSize(SER_NETWORK, LLP::PROTOCOL_VERSION);

        REQUIRE(vBytes.size() > refSize + 1);
        uint16_t physiglen = static_cast<uint16_t>(vBytes[refSize]) |
                             (static_cast<uint16_t>(vBytes[refSize + 1]) << 8);
        REQUIRE(physiglen == static_cast<uint16_t>(DUMMY_SIG_SIZE));
    }

    SECTION("T09: Round-trip preserves physical sig and keyID byte-for-byte")
    {
        TAO::Ledger::TritiumBlock orig = MakeBlock(PHYSICAL_FALCON_BLOCK_VERSION);
        orig.vchPhysicalFalconSig    = vDummySig;
        orig.hashPhysicalFalconKeyID = dummyKeyID;

        std::vector<uint8_t> vBytes   = Serialize(orig);
        TAO::Ledger::TritiumBlock recv = Deserialize(vBytes);

        REQUIRE(recv.nVersion == PHYSICAL_FALCON_BLOCK_VERSION);
        REQUIRE(recv.vchPhysicalFalconSig.size() == DUMMY_SIG_SIZE);
        REQUIRE(recv.vchPhysicalFalconSig == vDummySig);
        REQUIRE(recv.hashPhysicalFalconKeyID == dummyKeyID);
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * GROUP 4 (T10-T12) — Idle mode: PHYSICAL_FALCON_ENFORCEMENT == false
 * ───────────────────────────────────────────────────────────────────────────*/
TEST_CASE("T10-T12: PHYSICAL_FALCON_ENFORCEMENT is false in idle mode", "[physical_falcon][enforcement]")
{
    using namespace LLP::FalconConstants;

    SECTION("T10: Enforcement flag is statically false")
    {
        static_assert(PHYSICAL_FALCON_ENFORCEMENT == false,
                      "PHYSICAL_FALCON_ENFORCEMENT must be false until stealth activation");
        REQUIRE(PHYSICAL_FALCON_ENFORCEMENT == false);
    }

    SECTION("T11: Block with empty physical sig and post-threshold version still stores fields")
    {
        TAO::Ledger::TritiumBlock blk = MakeBlock(PHYSICAL_FALCON_BLOCK_VERSION);
        /* sig is empty — idle mode should not reject */
        REQUIRE(blk.vchPhysicalFalconSig.empty());
        REQUIRE(!PHYSICAL_FALCON_ENFORCEMENT);  // no enforcement → no rejection
    }

    SECTION("T12: SetNull clears physical sig fields")
    {
        TAO::Ledger::TritiumBlock blk = MakeBlock(PHYSICAL_FALCON_BLOCK_VERSION);
        blk.vchPhysicalFalconSig    = std::vector<uint8_t>(100, 0xFF);
        blk.hashPhysicalFalconKeyID = uint256_t(0xDEADBEEF);

        blk.SetNull();

        REQUIRE(blk.vchPhysicalFalconSig.empty());
        REQUIRE(blk.hashPhysicalFalconKeyID == uint256_t(0));
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * GROUP 5 (T13-T15) — Size bounds enforcement on deserialization
 * ───────────────────────────────────────────────────────────────────────────*/
TEST_CASE("T13-T15: Physical Falcon sig size bounds on deserialization", "[physical_falcon][serialize][bounds]")
{
    using namespace LLP::FalconConstants;

    /** Build bytes for a post-threshold block with a custom physiglen value
     *  but only <actual_data_bytes> bytes of actual sig data (may be truncated). **/
    auto MakeBytesWithPhysiglen = [](uint16_t physiglen, size_t actual_data_bytes)
        -> std::vector<uint8_t>
    {
        /* Serialize a post-threshold block with empty sig to get the header bytes,
         * then strip the trailing 2-byte physiglen=0 field and replace it with
         * a custom physiglen value and sig data. */
        TAO::Ledger::TritiumBlock blkBase = MakeBlock(PHYSICAL_FALCON_BLOCK_VERSION);
        DataStream ssBase(SER_NETWORK, LLP::PROTOCOL_VERSION);
        ssBase << blkBase;  // serializes with physiglen=0 (2 zero bytes at end)
        std::vector<uint8_t> vBase(ssBase.begin(), ssBase.end());

        /* Strip the trailing 2-byte physiglen=0 from the end */
        vBase.resize(vBase.size() - 2);

        /* Append the custom physiglen (little-endian) */
        vBase.push_back(static_cast<uint8_t>(physiglen & 0xFF));
        vBase.push_back(static_cast<uint8_t>(physiglen >> 8));

        /* Append the dummy keyID (32 zero bytes) if physiglen > 0 */
        if(physiglen > 0 && actual_data_bytes > 0)
        {
            for(size_t i = 0; i < sizeof(uint256_t); ++i)
                vBase.push_back(0x00);
        }

        /* Append actual sig bytes */
        for(size_t i = 0; i < actual_data_bytes; ++i)
            vBase.push_back(0xCC);

        return vBase;
    };

    SECTION("T13: physiglen below FALCON_CT_SIG_SIZE_512 throws on deserialization")
    {
        const uint16_t tooSmall = static_cast<uint16_t>(FALCON_CT_SIG_SIZE_512 - 1);
        std::vector<uint8_t> vBytes = MakeBytesWithPhysiglen(tooSmall, static_cast<size_t>(tooSmall));

        REQUIRE_THROWS_AS(Deserialize(vBytes), std::runtime_error);
    }

    SECTION("T14: physiglen above FALCON_CT_SIG_SIZE_1024 throws on deserialization")
    {
        const uint16_t tooBig = static_cast<uint16_t>(FALCON_CT_SIG_SIZE_1024 + 1);
        std::vector<uint8_t> vBytes = MakeBytesWithPhysiglen(tooBig, static_cast<size_t>(tooBig));

        REQUIRE_THROWS_AS(Deserialize(vBytes), std::runtime_error);
    }

    SECTION("T15: physiglen at FALCON_CT_SIG_SIZE_512 (897) deserializes successfully")
    {
        const uint16_t validMin = static_cast<uint16_t>(FALCON_CT_SIG_SIZE_512);
        std::vector<uint8_t> vBytes = MakeBytesWithPhysiglen(validMin, static_cast<size_t>(validMin));

        TAO::Ledger::TritiumBlock blk;
        REQUIRE_NOTHROW(blk = Deserialize(vBytes));
        REQUIRE(blk.vchPhysicalFalconSig.size() == static_cast<size_t>(validMin));
    }
}
