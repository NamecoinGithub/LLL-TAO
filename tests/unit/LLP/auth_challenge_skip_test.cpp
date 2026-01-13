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

#include <Util/include/runtime.h>
#include <Util/include/args.h>

using namespace LLP;

/* Packet type definitions for testing */
const Packet::message_t MINER_AUTH_INIT = 207;
const Packet::message_t MINER_AUTH_CHALLENGE = 208;


TEST_CASE("Authentication Challenge Skip Workaround", "[falcon][authentication][workaround]")
{
    /* Initialize Falcon auth for testing */
    FalconAuth::Initialize();

    /* Save original config state */
    auto original_config = config::mapArgs.find("-minerauthrequirechallenge");
    bool had_original = (original_config != config::mapArgs.end());
    std::string original_value = had_original ? original_config->second : "";
    
    /* RAII cleanup helper */
    struct ConfigCleanup {
        bool had_original;
        std::string original_value;
        ~ConfigCleanup() {
            config::mapArgs.erase("-minerauthrequirechallenge");
            if (had_original) {
                config::mapArgs["-minerauthrequirechallenge"] = original_value;
            }
        }
    } cleanup{had_original, original_value};

    SECTION("Default behavior: Skip challenge-response (backward compatibility)")
    {
        /* Ensure config flag is not set (default false) */
        config::mapArgs.erase("-minerauthrequirechallenge");
        
        FalconAuth::IFalconAuth* pAuth = FalconAuth::Get();
        REQUIRE(pAuth != nullptr);

        /* Generate a Falcon-512 key for testing */
        FalconAuth::KeyMetadata meta = pAuth->GenerateKey(
            FalconAuth::Profile::FALCON_512,
            "test_skip_challenge"
        );

        REQUIRE(meta.pubkey.size() == LLC::FalconSizes::FALCON512_PUBLIC_KEY_SIZE);

        /* Create test genesis hash */
        uint256_t testGenesis;
        testGenesis.SetHex("a174011c93ca1c80bca5388382b167cacd33d3154395ea8f45ac99a8308cd122");

        /* Derive ChaCha20 key for encryption */
        std::vector<uint8_t> vSessionKey = LLC::MiningSessionKeys::DeriveChaCha20Key(testGenesis);

        /* Create MINER_AUTH_INIT packet with genesis FIRST */
        MiningContext ctx;
        Packet initPacket(MINER_AUTH_INIT);

        /* Add genesis FIRST (32 bytes) */
        std::vector<uint8_t> vGenesis = testGenesis.GetBytes();
        initPacket.DATA.insert(initPacket.DATA.end(), vGenesis.begin(), vGenesis.end());

        /* Add pubkey length (2 bytes, big-endian) */
        uint16_t nPubKeyLen = static_cast<uint16_t>(meta.pubkey.size());
        initPacket.DATA.push_back(static_cast<uint8_t>(nPubKeyLen >> 8));
        initPacket.DATA.push_back(static_cast<uint8_t>(nPubKeyLen & 0xFF));
        
        /* Add pubkey */
        initPacket.DATA.insert(initPacket.DATA.end(), meta.pubkey.begin(), meta.pubkey.end());

        /* Add miner ID length (2 bytes, big-endian) - set to 0 */
        initPacket.DATA.push_back(0x00);
        initPacket.DATA.push_back(0x00);

        initPacket.LENGTH = static_cast<uint32_t>(initPacket.DATA.size());

        /* Process INIT packet */
        ProcessResult initResult = StatelessMiner::ProcessPacket(ctx, initPacket);

        /* Should succeed */
        REQUIRE(initResult.fSuccess == true);
        
        /* CRITICAL: Should be authenticated immediately (no challenge sent) */
        REQUIRE(initResult.context.fAuthenticated == true);
        
        /* Response should be empty (no challenge packet) */
        REQUIRE(initResult.response.HEADER == 0);
        REQUIRE(initResult.response.DATA.empty());
        
        /* Context should still have the pubkey and genesis */
        REQUIRE(!initResult.context.vMinerPubKey.empty());
        REQUIRE(initResult.context.hashGenesis == testGenesis);
    }

    SECTION("Config flag enabled: Require challenge-response (full authentication)")
    {
        /* Set config flag to require challenge */
        config::mapArgs["-minerauthrequirechallenge"] = "1";
        
        FalconAuth::IFalconAuth* pAuth = FalconAuth::Get();
        REQUIRE(pAuth != nullptr);

        /* Generate a Falcon-512 key for testing */
        FalconAuth::KeyMetadata meta = pAuth->GenerateKey(
            FalconAuth::Profile::FALCON_512,
            "test_require_challenge"
        );

        REQUIRE(meta.pubkey.size() == LLC::FalconSizes::FALCON512_PUBLIC_KEY_SIZE);

        /* Create test genesis hash */
        uint256_t testGenesis;
        testGenesis.SetHex("b285012c94da2d91cdb6499493c278dbde44e4265506fb9f56bd00b9419de233");

        /* Create MINER_AUTH_INIT packet with genesis FIRST */
        MiningContext ctx;
        Packet initPacket(MINER_AUTH_INIT);

        /* Add genesis FIRST (32 bytes) */
        std::vector<uint8_t> vGenesis = testGenesis.GetBytes();
        initPacket.DATA.insert(initPacket.DATA.end(), vGenesis.begin(), vGenesis.end());

        /* Add pubkey length (2 bytes, big-endian) */
        uint16_t nPubKeyLen = static_cast<uint16_t>(meta.pubkey.size());
        initPacket.DATA.push_back(static_cast<uint8_t>(nPubKeyLen >> 8));
        initPacket.DATA.push_back(static_cast<uint8_t>(nPubKeyLen & 0xFF));
        
        /* Add pubkey */
        initPacket.DATA.insert(initPacket.DATA.end(), meta.pubkey.begin(), meta.pubkey.end());

        /* Add miner ID length (2 bytes, big-endian) - set to 0 */
        initPacket.DATA.push_back(0x00);
        initPacket.DATA.push_back(0x00);

        initPacket.LENGTH = static_cast<uint32_t>(initPacket.DATA.size());

        /* Process INIT packet */
        ProcessResult initResult = StatelessMiner::ProcessPacket(ctx, initPacket);

        /* Should succeed */
        REQUIRE(initResult.fSuccess == true);
        
        /* Should NOT be authenticated yet (challenge required) */
        REQUIRE(initResult.context.fAuthenticated == false);
        
        /* Should send challenge packet */
        REQUIRE(initResult.response.HEADER == MINER_AUTH_CHALLENGE);
        REQUIRE(!initResult.response.DATA.empty());
        
        /* Should have nonce stored for verification */
        REQUIRE(!initResult.context.vAuthNonce.empty());
        REQUIRE(initResult.context.hashGenesis == testGenesis);
    }

    SECTION("Falcon-1024 works with challenge skip")
    {
        /* Ensure config flag is not set (default false) */
        config::mapArgs.erase("-minerauthrequirechallenge");
        
        FalconAuth::IFalconAuth* pAuth = FalconAuth::Get();
        REQUIRE(pAuth != nullptr);

        /* Generate a Falcon-1024 key for testing */
        FalconAuth::KeyMetadata meta = pAuth->GenerateKey(
            FalconAuth::Profile::FALCON_1024,
            "test_falcon1024_skip"
        );

        REQUIRE(meta.pubkey.size() == LLC::FalconSizes::FALCON1024_PUBLIC_KEY_SIZE);

        /* Create test genesis hash */
        uint256_t testGenesis;
        testGenesis.SetHex("c396123d05eb3e02dcf7510604d389ecef55f5376617ac0a67ce11c0520ef344");

        /* Create MINER_AUTH_INIT packet with genesis FIRST */
        MiningContext ctx;
        Packet initPacket(MINER_AUTH_INIT);

        /* Add genesis FIRST (32 bytes) */
        std::vector<uint8_t> vGenesis = testGenesis.GetBytes();
        initPacket.DATA.insert(initPacket.DATA.end(), vGenesis.begin(), vGenesis.end());

        /* Add pubkey length (2 bytes, big-endian) */
        uint16_t nPubKeyLen = static_cast<uint16_t>(meta.pubkey.size());
        initPacket.DATA.push_back(static_cast<uint8_t>(nPubKeyLen >> 8));
        initPacket.DATA.push_back(static_cast<uint8_t>(nPubKeyLen & 0xFF));
        
        /* Add pubkey */
        initPacket.DATA.insert(initPacket.DATA.end(), meta.pubkey.begin(), meta.pubkey.end());

        /* Add miner ID length (2 bytes, big-endian) - set to 0 */
        initPacket.DATA.push_back(0x00);
        initPacket.DATA.push_back(0x00);

        initPacket.LENGTH = static_cast<uint32_t>(initPacket.DATA.size());

        /* Process INIT packet */
        ProcessResult initResult = StatelessMiner::ProcessPacket(ctx, initPacket);

        /* Should succeed */
        REQUIRE(initResult.fSuccess == true);
        
        /* Should be authenticated immediately */
        REQUIRE(initResult.context.fAuthenticated == true);
        
        /* Verify Falcon-1024 was detected */
        REQUIRE(initResult.context.fFalconVersionDetected == true);
        REQUIRE(initResult.context.nFalconVersion == LLC::FalconVersion::FALCON_1024);
    }
}
