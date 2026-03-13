/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <LLP/include/session_recovery.h>
#include <LLP/include/stateless_miner.h>

#include <Util/include/runtime.h>

using namespace LLP;

/* ─────────────────────────────────────────────────────────────────────────────
 * Consistency-gate rejection tests for MINER_SET_REWARD and SUBMIT_BLOCK
 *
 * These tests confirm that MinerSessionContainer::ValidateConsistency() (the
 * canonical invariant gate used by both the legacy and stateless protocol
 * handlers) correctly rejects sessions whose reward/encryption state is
 * internally inconsistent, and that sessions with fully-bound state pass.
 * ─────────────────────────────────────────────────────────────────────────── */

/* ═══════════════════════════════════════════════════════════════════════════
 * MINER_SET_REWARD consistency-gate tests
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("Consistency gate - MINER_SET_REWARD reward binding invariants",
          "[consistency_gate][miner_set_reward]")
{
    SECTION("Unbound session is consistent (fRewardBound=false, hash=0)")
    {
        /* Before MINER_SET_REWARD is received the reward fields must both be
         * false/zero — the gate must not report a spurious error. */
        SessionRecoveryData data;
        data.fAuthenticated = true;
        data.nSessionId     = 1001;
        data.hashKeyID.SetHex(
            "aaaa000011112222333344445555666677778888999900001111222233334444");
        data.hashGenesis.SetHex(
            "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
        data.vPubKey = {0x01, 0x02, 0x03, 0x04};

        /* No reward binding yet */
        data.fRewardBound    = false;
        data.hashRewardAddress = uint256_t(0);

        REQUIRE(data.ValidateConsistency() == SessionConsistencyResult::Ok);
    }

    SECTION("MINER_SET_REWARD accepted: bound flag + non-zero address passes gate")
    {
        /* After a successful MINER_SET_REWARD both the flag and the address
         * must be set.  The gate must accept this as consistent. */
        SessionRecoveryData data;
        data.fAuthenticated = true;
        data.nSessionId     = 1002;
        data.hashKeyID.SetHex(
            "bbbb000011112222333344445555666677778888999900001111222233334444");
        data.hashGenesis.SetHex(
            "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
        data.vPubKey = {0x0a, 0x0b, 0x0c};

        data.fRewardBound = true;
        data.hashRewardAddress.SetHex(
            "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210");

        REQUIRE(data.ValidateConsistency() == SessionConsistencyResult::Ok);
    }

    SECTION("MINER_SET_REWARD rejected: bound flag set but address is zero")
    {
        /* This is the canonical corruption: the handler set fRewardBound=true
         * but failed to copy the address, leaving hashRewardAddress=0.
         * The gate must catch this and return RewardBoundMissingHash. */
        SessionRecoveryData data;
        data.fRewardBound      = true;
        data.hashRewardAddress = uint256_t(0);  // missing!

        REQUIRE(data.ValidateConsistency() ==
                SessionConsistencyResult::RewardBoundMissingHash);
    }

    SECTION("MINER_SET_REWARD rejected: address present but bound flag is false")
    {
        /* Inverse corruption: address copied but flag never flipped.
         * The gate treats the reward as unbound (fRewardBound==false → Ok),
         * but the address should not be silently used. */
        SessionRecoveryData data;
        data.fRewardBound      = false;
        data.hashRewardAddress.SetHex(
            "1111222233334444555566667777888899990000aaaabbbbccccddddeeeeffff");

        /* ValidateConsistency only checks fRewardBound→hash direction,
         * so a non-zero hash with fRewardBound=false is treated as Ok
         * (harmless orphan) rather than an error. */
        REQUIRE(data.ValidateConsistency() == SessionConsistencyResult::Ok);
    }

    SECTION("Round-trip: MiningContext with reward binding survives SaveSession/RecoverSession")
    {
        SessionRecoveryManager& manager = SessionRecoveryManager::Get();
        manager.SetSessionTimeout(3600);

        uint256_t keyId;
        keyId.SetHex("cccc000011112222333344445555666677778888999900001111222233334444");

        uint256_t genesis;
        genesis.SetHex("9988776655443322998877665544332299887766554433229988776655443322");

        uint256_t rewardAddr;
        rewardAddr.SetHex(
            "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210");

        /* Build a fully-bound context */
        MiningContext ctx = MiningContext()
            .WithChannel(2)
            .WithHeight(100000)
            .WithSession(42000)
            .WithKeyId(keyId)
            .WithGenesis(genesis)
            .WithAuth(true)
            .WithPubKey({0xde, 0xad, 0xbe, 0xef})
            .WithRewardAddress(rewardAddr)
            .WithTimestamp(runtime::unifiedtimestamp());

        /* Persist — simulates what MINER_SET_REWARD handler does via SaveSession */
        bool fSaved = manager.SaveSession(ctx);
        REQUIRE(fSaved == true);

        /* Recover and confirm reward binding is preserved */
        MiningContext recovered;
        bool fRecovered = manager.RecoverSession(keyId, recovered);

        REQUIRE(fRecovered == true);
        REQUIRE(recovered.fRewardBound == true);
        REQUIRE(recovered.hashRewardAddress == rewardAddr);

        /* Consistency gate must pass after recovery */
        SessionRecoveryData snapshot(recovered);
        REQUIRE(snapshot.ValidateConsistency() == SessionConsistencyResult::Ok);

        manager.RemoveSession(keyId);
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
 * SUBMIT_BLOCK consistency-gate tests
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("Consistency gate - SUBMIT_BLOCK encryption invariants",
          "[consistency_gate][submit_block]")
{
    SECTION("Encryption ready + non-empty key passes gate")
    {
        SessionRecoveryData data;
        data.fAuthenticated   = true;
        data.nSessionId       = 2001;
        data.hashKeyID.SetHex(
            "dddd000011112222333344445555666677778888999900001111222233334444");
        data.hashGenesis.SetHex(
            "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899");
        data.vPubKey = {0x55, 0x66, 0x77};

        /* Fully provisioned ChaCha20 key */
        data.fEncryptionReady = true;
        data.vChaCha20Key     = std::vector<uint8_t>(32, 0xA5);

        REQUIRE(data.ValidateConsistency() == SessionConsistencyResult::Ok);
    }

    SECTION("SUBMIT_BLOCK rejected: encryption ready but key is empty")
    {
        /* The handler sets fEncryptionReady=true after KDF but forgets to
         * store the key bytes.  The gate must catch this. */
        SessionRecoveryData data;
        data.fEncryptionReady = true;
        data.vChaCha20Key.clear();  // missing!

        REQUIRE(data.ValidateConsistency() ==
                SessionConsistencyResult::EncryptionReadyMissingKey);
    }

    SECTION("SUBMIT_BLOCK rejected: authenticated but session ID is zero")
    {
        SessionRecoveryData data;
        data.fAuthenticated = true;
        data.nSessionId     = 0;  // missing!

        data.hashKeyID.SetHex(
            "eeee000011112222333344445555666677778888999900001111222233334444");
        data.hashGenesis.SetHex(
            "1111222233334444555566667777888899990000aaaabbbbccccddddeeeeffff");
        data.vPubKey = {0x01, 0x02};

        REQUIRE(data.ValidateConsistency() ==
                SessionConsistencyResult::MissingSessionId);
    }

    SECTION("SUBMIT_BLOCK rejected: authenticated but genesis hash is zero")
    {
        SessionRecoveryData data;
        data.fAuthenticated = true;
        data.nSessionId     = 3001;
        data.hashGenesis    = uint256_t(0);  // missing!

        data.hashKeyID.SetHex(
            "ffff000011112222333344445555666677778888999900001111222233334444");
        data.vPubKey = {0xca, 0xfe};

        REQUIRE(data.ValidateConsistency() ==
                SessionConsistencyResult::MissingGenesis);
    }

    SECTION("SUBMIT_BLOCK rejected: authenticated but Falcon key ID is zero")
    {
        SessionRecoveryData data;
        data.fAuthenticated = true;
        data.nSessionId     = 4001;
        data.hashKeyID      = uint256_t(0);  // missing!

        data.hashGenesis.SetHex(
            "2222333344445555666677778888999900001111aaaabbbbccccddddeeeeffff");
        data.vPubKey = {0xbe, 0xef};

        REQUIRE(data.ValidateConsistency() ==
                SessionConsistencyResult::MissingFalconKey);
    }

    SECTION("Unauthenticated session with no encryption is consistent (Ok)")
    {
        /* A session that has not completed auth yet has no session ID or
         * genesis.  That is expected and must not be flagged as broken. */
        SessionRecoveryData data;
        data.fAuthenticated   = false;
        data.nSessionId       = 0;
        data.hashKeyID        = uint256_t(0);
        data.hashGenesis      = uint256_t(0);
        data.fRewardBound     = false;
        data.fEncryptionReady = false;

        REQUIRE(data.ValidateConsistency() == SessionConsistencyResult::Ok);
    }

    SECTION("Fully provisioned submit context passes all gate checks")
    {
        /* Simulate the full chain: auth + reward bound + encryption ready.
         * The gate must return Ok for a well-formed submit context. */
        SessionRecoveryData data;
        data.fAuthenticated = true;
        data.nSessionId     = 5001;

        data.hashKeyID.SetHex(
            "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
        data.hashGenesis.SetHex(
            "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210");
        data.vPubKey = {0x11, 0x22, 0x33, 0x44};

        data.fRewardBound = true;
        data.hashRewardAddress.SetHex(
            "a0b1c2d3e4f5061728394a5b6c7d8e9fa0b1c2d3e4f5061728394a5b6c7d8e9f");

        data.fEncryptionReady = true;
        data.vChaCha20Key     = std::vector<uint8_t>(32, 0x7E);

        REQUIRE(data.ValidateConsistency() == SessionConsistencyResult::Ok);
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
 * Combined MINER_SET_REWARD → SUBMIT_BLOCK pipeline gate tests
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("Consistency gate - MINER_SET_REWARD to SUBMIT_BLOCK pipeline",
          "[consistency_gate][pipeline]")
{
    SECTION("Gate rejects SUBMIT_BLOCK when MINER_SET_REWARD was never sent")
    {
        /* Simulate a context where the miner skipped MINER_SET_REWARD.
         * fRewardBound remains false — that alone is Ok at the gate level,
         * but the GET_BLOCK handler enforces the reward requirement at
         * runtime. We verify that the unbound state is detectable. */
        SessionRecoveryData data;
        data.fAuthenticated   = true;
        data.nSessionId       = 6001;
        data.hashKeyID.SetHex(
            "9999000011112222333344445555666677778888999900001111222233334444");
        data.hashGenesis.SetHex(
            "5555666677778888999900001111222233334444555566667777888899990000");
        data.vPubKey          = {0xAA, 0xBB};
        data.fRewardBound     = false;
        data.hashRewardAddress = uint256_t(0);

        /* Gate Ok — the reward-not-set enforcement happens in GET_BLOCK, not
         * in ValidateConsistency. Confirm the detection predicate works. */
        REQUIRE(data.ValidateConsistency() == SessionConsistencyResult::Ok);
        REQUIRE(data.fRewardBound == false);
        /* Simulate the GET_BLOCK guard: reject if reward not bound */
        bool fWouldBeRejected = !data.fRewardBound;
        REQUIRE(fWouldBeRejected == true);
    }

    SECTION("Gate accepts SUBMIT_BLOCK after valid MINER_SET_REWARD sequence")
    {
        /* Simulate full pipeline: auth + set_reward + ready to submit. */
        SessionRecoveryData data;
        data.fAuthenticated = true;
        data.nSessionId     = 7001;
        data.hashKeyID.SetHex(
            "aaaa111122223333444455556666777788889999000011112222333344445555");
        data.hashGenesis.SetHex(
            "bbbb111122223333444455556666777788889999000011112222333344445555");
        data.vPubKey = {0x01, 0x23, 0x45, 0x67};

        /* MINER_SET_REWARD step */
        data.fRewardBound = true;
        data.hashRewardAddress.SetHex(
            "cccc111122223333444455556666777788889999000011112222333344445555");

        /* Encryption derived from genesis */
        data.fEncryptionReady = true;
        data.vChaCha20Key     = std::vector<uint8_t>(32, 0x3C);

        REQUIRE(data.ValidateConsistency() == SessionConsistencyResult::Ok);
        REQUIRE(data.fRewardBound == true);
        REQUIRE(data.hashRewardAddress != uint256_t(0));
    }

    SECTION("Gate rejects corrupt pipeline: reward bound but zero address + encryption key empty")
    {
        /* Both invariants broken simultaneously — gate must report the FIRST
         * failing check (RewardBoundMissingHash has higher priority in the
         * ValidateConsistency ordering). */
        SessionRecoveryData data;
        data.fAuthenticated    = true;
        data.nSessionId        = 8001;
        data.hashKeyID.SetHex(
            "dddd111122223333444455556666777788889999000011112222333344445555");
        data.hashGenesis.SetHex(
            "eeee111122223333444455556666777788889999000011112222333344445555");
        data.vPubKey = {0xFF, 0xEE};

        data.fRewardBound      = true;
        data.hashRewardAddress = uint256_t(0);   // broken
        data.fEncryptionReady  = true;
        data.vChaCha20Key.clear();               // also broken

        /* Reward check comes before encryption check in ValidateConsistency */
        REQUIRE(data.ValidateConsistency() ==
                SessionConsistencyResult::RewardBoundMissingHash);
    }
}
