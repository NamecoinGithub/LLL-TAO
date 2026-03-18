/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <LLP/include/chacha20_evp_manager.h>
#include <LLC/include/chacha20_helpers.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace LLP;

/**
 * Chacha20EvpManager Unit Tests
 *
 * Tests for the node-side transport encryption gate that owns the EVP vs TLS
 * mode decision for both the legacy (8323) and stateless (9323) mining lanes.
 *
 * Pattern: same as test_simlink_session_rate_limiter.cpp
 */


TEST_CASE("Chacha20EvpManager: Default mode is EVP",
          "[chacha20_evp_manager][mode][default]")
{
    SECTION("GetMode returns EVP before Initialize is called")
    {
        /* The atomic default in the constructor is MiningTransportMode::EVP */
        auto& mgr = Chacha20EvpManager::Get();
        REQUIRE(mgr.GetMode() == MiningTransportMode::EVP);
    }

    SECTION("IsEvpActive returns true by default")
    {
        auto& mgr = Chacha20EvpManager::Get();
        REQUIRE(mgr.IsEvpActive());
    }

    SECTION("IsTlsActive returns false by default")
    {
        auto& mgr = Chacha20EvpManager::Get();
        REQUIRE_FALSE(mgr.IsTlsActive());
    }
}


TEST_CASE("Chacha20EvpManager: EVP and TLS mutual exclusion",
          "[chacha20_evp_manager][mode][mutual_exclusion]")
{
    /* The singleton is already initialized by previous tests (or by defaults).
     * We test the enum values and the invariant that EVP and TLS cannot both be true
     * by examining the manager API directly. */
    auto& mgr = Chacha20EvpManager::Get();

    SECTION("IsEvpActive and IsTlsActive are never both true")
    {
        /* At any point exactly one of EVP/TLS is active (or neither in NONE mode) */
        const bool fEvp = mgr.IsEvpActive();
        const bool fTls = mgr.IsTlsActive();
        /* Both active simultaneously would violate the mutual exclusion contract */
        const bool fBothActive = fEvp && fTls;
        REQUIRE_FALSE(fBothActive);
    }

    SECTION("MiningTransportMode enum values are distinct")
    {
        REQUIRE(static_cast<uint8_t>(MiningTransportMode::EVP)  == 0);
        REQUIRE(static_cast<uint8_t>(MiningTransportMode::TLS)  == 1);
        REQUIRE(static_cast<uint8_t>(MiningTransportMode::NONE) == 2);
    }
}


TEST_CASE("Chacha20EvpManager: Encrypt/Decrypt round-trip via manager in EVP mode",
          "[chacha20_evp_manager][encrypt][decrypt][roundtrip]")
{
    auto& mgr = Chacha20EvpManager::Get();

    /* These tests only run when EVP is active (the default) */
    if(!mgr.IsEvpActive())
    {
        WARN("Skipping round-trip tests: EVP mode is not active");
        return;
    }

    SECTION("Encrypt produces non-empty output for valid key and plaintext")
    {
        std::vector<uint8_t> vPlain = {0x01, 0x02, 0x03, 0x04, 0x05};
        std::vector<uint8_t> vKey(32, 0xAB);
        std::vector<uint8_t> vOut;

        bool fOk = mgr.Encrypt(vPlain, vKey, vOut);
        REQUIRE(fOk);
        REQUIRE(!vOut.empty());
        /* format: nonce(12) + ciphertext + tag(16) */
        REQUIRE(vOut.size() == 12 + vPlain.size() + 16);
    }

    SECTION("Decrypt recovers original plaintext after Encrypt")
    {
        std::string strMessage = "nexus-mining-evp-roundtrip-test";
        std::vector<uint8_t> vPlain(strMessage.begin(), strMessage.end());
        std::vector<uint8_t> vKey(32, 0xCC);

        std::vector<uint8_t> vCipher;
        REQUIRE(mgr.Encrypt(vPlain, vKey, vCipher));
        REQUIRE(!vCipher.empty());

        std::vector<uint8_t> vDecrypted;
        REQUIRE(mgr.Decrypt(vCipher, vKey, vDecrypted));
        REQUIRE(vDecrypted == vPlain);
    }

    SECTION("Decrypt fails with wrong key")
    {
        std::vector<uint8_t> vPlain = {0xDE, 0xAD, 0xBE, 0xEF};
        std::vector<uint8_t> vKeyGood(32, 0x11);
        std::vector<uint8_t> vKeyBad(32, 0x22);

        std::vector<uint8_t> vCipher;
        REQUIRE(mgr.Encrypt(vPlain, vKeyGood, vCipher));

        std::vector<uint8_t> vDecrypted;
        REQUIRE_FALSE(mgr.Decrypt(vCipher, vKeyBad, vDecrypted));
    }

    SECTION("Encrypt returns false for empty plaintext")
    {
        std::vector<uint8_t> vEmpty;
        std::vector<uint8_t> vKey(32, 0xFF);
        std::vector<uint8_t> vOut;

        /* LLC::EncryptPayloadChaCha20 rejects empty plaintext */
        bool fOk = mgr.Encrypt(vEmpty, vKey, vOut);
        REQUIRE_FALSE(fOk);
        REQUIRE(vOut.empty());
    }

    SECTION("Encrypt returns false for wrong-length key")
    {
        std::vector<uint8_t> vPlain = {0x01, 0x02};
        std::vector<uint8_t> vShortKey(16, 0xAA); /* 16 bytes -- invalid, must be 32 */
        std::vector<uint8_t> vOut;

        bool fOk = mgr.Encrypt(vPlain, vShortKey, vOut);
        REQUIRE_FALSE(fOk);
        REQUIRE(vOut.empty());
    }
}


TEST_CASE("Chacha20EvpManager: Encrypt/Decrypt round-trip with AAD",
          "[chacha20_evp_manager][encrypt][decrypt][aad]")
{
    auto& mgr = Chacha20EvpManager::Get();

    if(!mgr.IsEvpActive())
    {
        WARN("Skipping AAD tests: EVP mode is not active");
        return;
    }

    SECTION("Round-trip with SESSION_KEEPALIVE AAD succeeds")
    {
        /* AAD = opcode bytes in LE for SESSION_KEEPALIVE (0xD0D4) */
        std::vector<uint8_t> vAAD = OpcodeAAD(0xD0D4);
        REQUIRE(vAAD.size() == 2);
        REQUIRE(vAAD[0] == 0xD4);
        REQUIRE(vAAD[1] == 0xD0);

        std::string strMsg = "keepalive-aad-roundtrip";
        std::vector<uint8_t> vPlain(strMsg.begin(), strMsg.end());
        std::vector<uint8_t> vKey(32, 0xAB);

        std::vector<uint8_t> vCipher;
        REQUIRE(mgr.Encrypt(vPlain, vKey, vCipher, vAAD));
        REQUIRE(!vCipher.empty());

        std::vector<uint8_t> vDecrypted;
        REQUIRE(mgr.Decrypt(vCipher, vKey, vDecrypted, vAAD));
        REQUIRE(vDecrypted == vPlain);
    }

    SECTION("Round-trip with SESSION_STATUS_ACK AAD succeeds")
    {
        /* AAD = opcode bytes in LE for SESSION_STATUS_ACK (0xD0DC) */
        std::vector<uint8_t> vAAD = OpcodeAAD(0xD0DC);
        REQUIRE(vAAD[0] == 0xDC);
        REQUIRE(vAAD[1] == 0xD0);

        std::vector<uint8_t> vPlain = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        std::vector<uint8_t> vKey(32, 0xCC);

        std::vector<uint8_t> vCipher;
        REQUIRE(mgr.Encrypt(vPlain, vKey, vCipher, vAAD));

        std::vector<uint8_t> vDecrypted;
        REQUIRE(mgr.Decrypt(vCipher, vKey, vDecrypted, vAAD));
        REQUIRE(vDecrypted == vPlain);
    }

    SECTION("Decrypt fails with wrong AAD (domain separation)")
    {
        /* Encrypt with SESSION_STATUS AAD */
        std::vector<uint8_t> vAAD_Status = OpcodeAAD(0xD0DB);
        /* Try to decrypt with SESSION_KEEPALIVE AAD */
        std::vector<uint8_t> vAAD_Keepalive = OpcodeAAD(0xD0D4);

        std::vector<uint8_t> vPlain = {0xDE, 0xAD, 0xBE, 0xEF};
        std::vector<uint8_t> vKey(32, 0xDD);

        std::vector<uint8_t> vCipher;
        REQUIRE(mgr.Encrypt(vPlain, vKey, vCipher, vAAD_Status));

        /* Decrypt with mismatched AAD must fail */
        std::vector<uint8_t> vDecrypted;
        REQUIRE_FALSE(mgr.Decrypt(vCipher, vKey, vDecrypted, vAAD_Keepalive));
    }

    SECTION("Decrypt fails with empty AAD when encrypted with AAD")
    {
        std::vector<uint8_t> vAAD = OpcodeAAD(0xD0D4);
        std::vector<uint8_t> vPlain = {0x01, 0x02, 0x03};
        std::vector<uint8_t> vKey(32, 0xEE);

        std::vector<uint8_t> vCipher;
        REQUIRE(mgr.Encrypt(vPlain, vKey, vCipher, vAAD));

        /* Decrypt without AAD must fail */
        std::vector<uint8_t> vDecrypted;
        REQUIRE_FALSE(mgr.Decrypt(vCipher, vKey, vDecrypted));
    }

    SECTION("Decrypt fails with AAD when encrypted without AAD")
    {
        std::vector<uint8_t> vPlain = {0x04, 0x05, 0x06};
        std::vector<uint8_t> vKey(32, 0xFF);

        /* Encrypt without AAD */
        std::vector<uint8_t> vCipher;
        REQUIRE(mgr.Encrypt(vPlain, vKey, vCipher));

        /* Decrypt with AAD must fail */
        std::vector<uint8_t> vAAD = OpcodeAAD(0xD0DC);
        std::vector<uint8_t> vDecrypted;
        REQUIRE_FALSE(mgr.Decrypt(vCipher, vKey, vDecrypted, vAAD));
    }

    SECTION("KEEPALIVE_V2_ACK AAD is correctly formed")
    {
        /* KEEPALIVE_V2_ACK = 0xD101, LE = {0x01, 0xD1} */
        std::vector<uint8_t> vAAD = OpcodeAAD(0xD101);
        REQUIRE(vAAD.size() == 2);
        REQUIRE(vAAD[0] == 0x01);
        REQUIRE(vAAD[1] == 0xD1);

        /* Verify round-trip with this AAD */
        std::vector<uint8_t> vPlain = {0xAA, 0xBB, 0xCC, 0xDD};
        std::vector<uint8_t> vKey(32, 0x77);

        std::vector<uint8_t> vCipher;
        REQUIRE(mgr.Encrypt(vPlain, vKey, vCipher, vAAD));

        std::vector<uint8_t> vDecrypted;
        REQUIRE(mgr.Decrypt(vCipher, vKey, vDecrypted, vAAD));
        REQUIRE(vDecrypted == vPlain);
    }
}


TEST_CASE("Chacha20EvpManager: AllowPlaintext returns true for 127.0.0.1 only when encryption not required",
          "[chacha20_evp_manager][allow_plaintext][localhost]")
{
    auto& mgr = Chacha20EvpManager::Get();

    SECTION("AllowPlaintext returns false for 127.0.0.1 when EVP is active (encryption required)")
    {
        /* In EVP mode, IsEncryptionRequired() == true, so even localhost must encrypt */
        if(mgr.IsEvpActive())
        {
            REQUIRE_FALSE(mgr.AllowPlaintext("127.0.0.1"));
        }
    }

    SECTION("AllowPlaintext returns false for remote IP regardless of mode")
    {
        /* Remote IPs are never allowed to send plaintext */
        REQUIRE_FALSE(mgr.AllowPlaintext("192.168.1.100"));
        REQUIRE_FALSE(mgr.AllowPlaintext("10.0.0.1"));
        REQUIRE_FALSE(mgr.AllowPlaintext("8.8.8.8"));
    }

    SECTION("AllowPlaintext returns false for empty string")
    {
        REQUIRE_FALSE(mgr.AllowPlaintext(""));
    }
}


TEST_CASE("Chacha20EvpManager: AllowPlaintext returns false for remote IP",
          "[chacha20_evp_manager][allow_plaintext][remote]")
{
    auto& mgr = Chacha20EvpManager::Get();

    SECTION("Remote IPv4 addresses are always rejected for plaintext")
    {
        REQUIRE_FALSE(mgr.AllowPlaintext("172.16.0.1"));
        REQUIRE_FALSE(mgr.AllowPlaintext("203.0.113.1"));
    }

    SECTION("Loopback variants that are not the exact string 127.0.0.1 are rejected")
    {
        /* AllowPlaintext checks exact string equality with "127.0.0.1".
         * Other loopback representations (IPv6 ::1, 127.0.0.2, etc.) must fail. */
        REQUIRE_FALSE(mgr.AllowPlaintext("::1"));
        REQUIRE_FALSE(mgr.AllowPlaintext("127.0.0.2"));
        REQUIRE_FALSE(mgr.AllowPlaintext("localhost"));
    }
}


TEST_CASE("Chacha20EvpManager: ModeString returns expected strings",
          "[chacha20_evp_manager][mode_string]")
{
    auto& mgr = Chacha20EvpManager::Get();

    SECTION("ModeString is non-empty")
    {
        REQUIRE_FALSE(mgr.ModeString().empty());
    }

    SECTION("ModeString returns EVP string when mode is EVP")
    {
        if(mgr.IsEvpActive())
        {
            REQUIRE(mgr.ModeString() == "EVP (ChaCha20-Poly1305)");
        }
    }

    SECTION("ModeString returns TLS string when mode is TLS")
    {
        if(mgr.IsTlsActive())
        {
            REQUIRE(mgr.ModeString() == "TLS");
        }
    }
}


TEST_CASE("Chacha20EvpManager: IsEncryptionRequired true in EVP mode",
          "[chacha20_evp_manager][encryption_required]")
{
    auto& mgr = Chacha20EvpManager::Get();

    SECTION("IsEncryptionRequired returns true when EVP is active")
    {
        if(mgr.IsEvpActive())
        {
            REQUIRE(mgr.IsEncryptionRequired());
        }
    }

    SECTION("IsEncryptionRequired and IsEvpActive are consistent")
    {
        /* If EVP is active, encryption is required; otherwise it may not be */
        if(mgr.IsEvpActive())
        {
            REQUIRE(mgr.IsEncryptionRequired());
        }
        else if(mgr.IsTlsActive())
        {
            /* TLS handles its own encryption at the transport layer */
            /* IsEncryptionRequired() covers only the EVP payload-level gate */
            REQUIRE_FALSE(mgr.IsEncryptionRequired());
        }
    }
}


TEST_CASE("Chacha20EvpManager: Get() returns the same singleton instance",
          "[chacha20_evp_manager][singleton]")
{
    SECTION("Multiple Get() calls return the same object")
    {
        Chacha20EvpManager& ref1 = Chacha20EvpManager::Get();
        Chacha20EvpManager& ref2 = Chacha20EvpManager::Get();
        REQUIRE(&ref1 == &ref2);
    }
}


TEST_CASE("Chacha20EvpManager: Initialize is idempotent",
          "[chacha20_evp_manager][initialize][idempotent]")
{
    SECTION("Calling Initialize() multiple times does not crash or change mode")
    {
        auto& mgr = Chacha20EvpManager::Get();

        /* Capture mode before repeated Initialize() calls */
        const MiningTransportMode modeBefore = mgr.GetMode();

        /* Call Initialize() again — must be a no-op */
        REQUIRE_NOTHROW(mgr.Initialize());
        REQUIRE_NOTHROW(mgr.Initialize());

        /* Mode must not have changed */
        REQUIRE(mgr.GetMode() == modeBefore);
    }
}
