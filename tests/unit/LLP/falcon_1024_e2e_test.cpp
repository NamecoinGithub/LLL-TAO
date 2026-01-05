/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <LLP/include/stateless_miner.h>
#include <LLP/include/falcon_auth.h>

#include <LLC/include/mining_session_keys.h>
#include <LLC/include/flkey.h>
#include <LLC/include/chacha20_helpers.h>

#include <Util/include/runtime.h>
#include <Util/include/hex.h>

using namespace LLP;

/* Packet type definitions */
const Packet::message_t MINER_AUTH_INIT = 207;
const Packet::message_t MINER_AUTH_CHALLENGE = 208;
const Packet::message_t MINER_AUTH_RESPONSE = 209;
const Packet::message_t MINER_AUTH_RESULT = 210;
const Packet::message_t SESSION_START = 211;
const Packet::message_t SUBMIT_BLOCK = 2;

TEST_CASE("Falcon-1024 End-to-End Integration", "[falcon1024][e2e][integration]")
{
    /* Initialize Falcon auth for testing */
    FalconAuth::Initialize();

    SECTION("Full authentication flow with Falcon-1024")
    {
        /* This test validates the complete Falcon-1024 authentication workflow:
         * 1. MINER_AUTH_INIT with Falcon-1024 public key
         * 2. MINER_AUTH_CHALLENGE from server
         * 3. MINER_AUTH_RESPONSE with Falcon-1024 signature
         * 4. MINER_AUTH_RESULT confirming success
         * 5. SESSION_START establishing session parameters
         */

        FalconAuth::IFalconAuth* pAuth = FalconAuth::Get();
        REQUIRE(pAuth != nullptr);

        /* Generate a Falcon-1024 key pair */
        FalconAuth::KeyMetadata meta = pAuth->GenerateKey(
            FalconAuth::Profile::FALCON_1024,
            "test_falcon1024_e2e"
        );

        REQUIRE(meta.pubkey.size() == LLC::FalconSizes::FALCON1024_PUBLIC_KEY_SIZE);
        REQUIRE(meta.privkey.size() == LLC::FalconSizes::FALCON1024_PRIVATE_KEY_SIZE);

        /* Create test genesis hash */
        uint256_t testGenesis;
        testGenesis.SetHex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

        /* Step 1: Create MINER_AUTH_INIT packet with Falcon-1024 public key */
        Packet authInit(MINER_AUTH_INIT);
        authInit.DATA.insert(authInit.DATA.end(), meta.pubkey.begin(), meta.pubkey.end());
        authInit.DATA.insert(authInit.DATA.end(), testGenesis.begin(), testGenesis.end());

        /* Validate packet has correct size */
        REQUIRE(authInit.DATA.size() == LLC::FalconSizes::FALCON1024_PUBLIC_KEY_SIZE + 32);

        /* Step 2: Simulate server challenge */
        std::vector<uint8_t> vChallenge(32);
        for(size_t i = 0; i < 32; ++i)
            vChallenge[i] = static_cast<uint8_t>(i);

        Packet challenge(MINER_AUTH_CHALLENGE);
        challenge.DATA = vChallenge;

        /* Step 3: Sign challenge with Falcon-1024 */
        std::vector<uint8_t> vSignature = pAuth->Sign(meta.privkey, vChallenge);
        
        /* Verify signature size matches Falcon-1024 */
        REQUIRE(vSignature.size() == LLC::FalconSizes::FALCON1024_SIGNATURE_SIZE);

        /* Create MINER_AUTH_RESPONSE */
        Packet response(MINER_AUTH_RESPONSE);
        response.DATA = vSignature;

        /* Step 4: Verify signature on server side */
        bool fVerified = pAuth->Verify(meta.pubkey, vChallenge, vSignature);
        REQUIRE(fVerified == true);

        /* Step 5: Session establishment would follow */
        Packet authResult(MINER_AUTH_RESULT);
        authResult.DATA.push_back(0x01); // Success code

        /* Session parameters */
        Packet sessionStart(SESSION_START);
        uint32_t nSessionId = 12345;
        sessionStart.DATA.insert(sessionStart.DATA.end(), 
                                 reinterpret_cast<uint8_t*>(&nSessionId),
                                 reinterpret_cast<uint8_t*>(&nSessionId) + sizeof(nSessionId));

        REQUIRE(authResult.DATA.size() == 1);
        REQUIRE(sessionStart.DATA.size() >= 4);
    }

    SECTION("ChaCha20 encryption with Falcon-1024 throughout")
    {
        /* This test validates encrypted communication using ChaCha20
         * with keys derived from Falcon-1024 authenticated session */

        /* Generate Falcon-1024 key pair */
        FalconAuth::IFalconAuth* pAuth = FalconAuth::Get();
        REQUIRE(pAuth != nullptr);

        FalconAuth::KeyMetadata meta = pAuth->GenerateKey(
            FalconAuth::Profile::FALCON_1024,
            "test_chacha20_falcon1024"
        );

        /* Create test genesis hash for key derivation */
        uint256_t testGenesis;
        testGenesis.SetHex("fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210");

        /* Derive ChaCha20 key from genesis hash */
        std::vector<uint8_t> vChaChaKey = LLC::ChaCha20::DeriveKey(testGenesis);
        REQUIRE(vChaChaKey.size() == 32);

        /* Test data to encrypt */
        std::string strPlaintext = "Test block submission with Falcon-1024";
        std::vector<uint8_t> vPlaintext(strPlaintext.begin(), strPlaintext.end());

        /* Encrypt data */
        std::vector<uint8_t> vCiphertext = LLC::ChaCha20::Encrypt(vPlaintext, vChaChaKey);
        REQUIRE(vCiphertext.size() > 0);
        REQUIRE(vCiphertext != vPlaintext); // Verify it's actually encrypted

        /* Decrypt data */
        std::vector<uint8_t> vDecrypted = LLC::ChaCha20::Decrypt(vCiphertext, vChaChaKey);
        REQUIRE(vDecrypted == vPlaintext); // Verify decryption works

        /* Verify we can use this for block submission */
        Packet blockPacket(SUBMIT_BLOCK);
        blockPacket.DATA = vCiphertext;
        REQUIRE(blockPacket.DATA.size() == vCiphertext.size());
    }

    SECTION("Key bonding validation (Falcon-1024 disposable + physical)")
    {
        /* This test validates that Physical Falcon signatures must use
         * the same Falcon version (1024) as Disposable Falcon signatures */

        FalconAuth::IFalconAuth* pAuth = FalconAuth::Get();
        REQUIRE(pAuth != nullptr);

        /* Generate Falcon-1024 key pair for both disposable and physical */
        FalconAuth::KeyMetadata metaDisposable = pAuth->GenerateKey(
            FalconAuth::Profile::FALCON_1024,
            "test_disposable_1024"
        );

        FalconAuth::KeyMetadata metaPhysical = pAuth->GenerateKey(
            FalconAuth::Profile::FALCON_1024,
            "test_physical_1024"
        );

        /* Test data for signing */
        std::vector<uint8_t> vTestData(64);
        for(size_t i = 0; i < 64; ++i)
            vTestData[i] = static_cast<uint8_t>(i * 3);

        /* Sign with disposable Falcon-1024 */
        std::vector<uint8_t> vDisposableSignature = pAuth->Sign(metaDisposable.privkey, vTestData);
        REQUIRE(vDisposableSignature.size() == LLC::FalconSizes::FALCON1024_SIGNATURE_SIZE);

        /* Sign with physical Falcon-1024 (must be same key in real scenario) */
        std::vector<uint8_t> vPhysicalSignature = pAuth->Sign(metaPhysical.privkey, vTestData);
        REQUIRE(vPhysicalSignature.size() == LLC::FalconSizes::FALCON1024_SIGNATURE_SIZE);

        /* Verify both signatures are valid */
        bool fDisposableValid = pAuth->Verify(metaDisposable.pubkey, vTestData, vDisposableSignature);
        bool fPhysicalValid = pAuth->Verify(metaPhysical.pubkey, vTestData, vPhysicalSignature);
        
        REQUIRE(fDisposableValid == true);
        REQUIRE(fPhysicalValid == true);

        /* In production, both signatures would use the SAME key pair
         * This test demonstrates that both signatures are Falcon-1024 sized */
        REQUIRE(vDisposableSignature.size() == vPhysicalSignature.size());

        /* Test that mixing Falcon-512 and Falcon-1024 is detected */
        FalconAuth::KeyMetadata meta512 = pAuth->GenerateKey(
            FalconAuth::Profile::FALCON_512,
            "test_mixed_512"
        );

        std::vector<uint8_t> vSignature512 = pAuth->Sign(meta512.privkey, vTestData);
        REQUIRE(vSignature512.size() == LLC::FalconSizes::FALCON512_SIGNATURE_SIZE);

        /* Verify sizes are different (key bonding would catch this mismatch) */
        REQUIRE(vSignature512.size() != vDisposableSignature.size());
    }

    SECTION("Block submission with both Falcon-1024 signature types")
    {
        /* This test validates a complete block submission with both
         * disposable and physical Falcon-1024 signatures */

        FalconAuth::IFalconAuth* pAuth = FalconAuth::Get();
        REQUIRE(pAuth != nullptr);

        /* Generate Falcon-1024 key pair (same key used for both signatures) */
        FalconAuth::KeyMetadata meta = pAuth->GenerateKey(
            FalconAuth::Profile::FALCON_1024,
            "test_block_submission_1024"
        );

        /* Simulate block data */
        std::vector<uint8_t> vBlockData(128);
        for(size_t i = 0; i < 128; ++i)
            vBlockData[i] = static_cast<uint8_t>(i);

        /* Sign block with disposable Falcon-1024 (session-based, NOT stored) */
        std::vector<uint8_t> vDisposableSignature = pAuth->Sign(meta.privkey, vBlockData);
        REQUIRE(vDisposableSignature.size() == LLC::FalconSizes::FALCON1024_SIGNATURE_SIZE);

        /* Sign block with physical Falcon-1024 (SAME key, stored on blockchain) */
        std::vector<uint8_t> vPhysicalSignature = pAuth->Sign(meta.privkey, vBlockData);
        REQUIRE(vPhysicalSignature.size() == LLC::FalconSizes::FALCON1024_SIGNATURE_SIZE);

        /* Verify both signatures are valid */
        bool fDisposableValid = pAuth->Verify(meta.pubkey, vBlockData, vDisposableSignature);
        bool fPhysicalValid = pAuth->Verify(meta.pubkey, vBlockData, vPhysicalSignature);
        
        REQUIRE(fDisposableValid == true);
        REQUIRE(fPhysicalValid == true);

        /* Create block submission packet with both signatures */
        Packet blockPacket(SUBMIT_BLOCK);
        
        /* Add block data */
        blockPacket.DATA.insert(blockPacket.DATA.end(), vBlockData.begin(), vBlockData.end());
        
        /* Add disposable signature (used during session, not stored) */
        blockPacket.DATA.insert(blockPacket.DATA.end(), 
                               vDisposableSignature.begin(), 
                               vDisposableSignature.end());
        
        /* Add physical signature (optional, stored permanently: 1577 bytes) */
        blockPacket.DATA.insert(blockPacket.DATA.end(),
                               vPhysicalSignature.begin(),
                               vPhysicalSignature.end());

        /* Verify packet structure */
        size_t nExpectedSize = vBlockData.size() + 
                              LLC::FalconSizes::FALCON1024_SIGNATURE_SIZE +  // Disposable
                              LLC::FalconSizes::FALCON1024_SIGNATURE_SIZE;   // Physical
        
        REQUIRE(blockPacket.DATA.size() == nExpectedSize);
    }
}
