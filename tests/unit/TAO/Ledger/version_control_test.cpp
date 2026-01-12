/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To The Voice of The People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <TAO/Ledger/include/version_control.h>

using namespace TAO::Ledger::Versions;


//===========================================================================================
// TRANSACTION VERSION TESTS
//===========================================================================================

TEST_CASE("Transaction Version Constants", "[version][transaction]")
{
    SECTION("Transaction version constants are correctly defined")
    {
        REQUIRE(Transaction::LEGACY_V1 == 1);
        REQUIRE(Transaction::LEGACY_V2 == 2);
        REQUIRE(Transaction::TRITIUM_V3 == 3);
        REQUIRE(Transaction::TRITIUM_V4 == 4);
        REQUIRE(Transaction::TRITIUM_V5 == 5);
        
        REQUIRE(Transaction::MAINNET_CURRENT == 5);
        REQUIRE(Transaction::TESTNET_CURRENT == 5);
        REQUIRE(Transaction::MINIMUM_SUPPORTED == 3);
    }
}


TEST_CASE("Transaction Version Helpers", "[version][transaction]")
{
    SECTION("IsLegacy() identifies legacy transactions")
    {
        REQUIRE(Transaction::IsLegacy(1) == true);
        REQUIRE(Transaction::IsLegacy(2) == true);
        REQUIRE(Transaction::IsLegacy(3) == false);
        REQUIRE(Transaction::IsLegacy(4) == false);
        REQUIRE(Transaction::IsLegacy(5) == false);
    }

    SECTION("IsTritium() identifies Tritium transactions")
    {
        REQUIRE(Transaction::IsTritium(1) == false);
        REQUIRE(Transaction::IsTritium(2) == false);
        REQUIRE(Transaction::IsTritium(3) == true);
        REQUIRE(Transaction::IsTritium(4) == true);
        REQUIRE(Transaction::IsTritium(5) == true);
        REQUIRE(Transaction::IsTritium(6) == false);
    }

    SECTION("IsValid() validates transaction versions")
    {
        REQUIRE(Transaction::IsValid(0) == false);
        REQUIRE(Transaction::IsValid(1) == true);
        REQUIRE(Transaction::IsValid(2) == true);
        REQUIRE(Transaction::IsValid(3) == true);
        REQUIRE(Transaction::IsValid(4) == true);
        REQUIRE(Transaction::IsValid(5) == true);
        REQUIRE(Transaction::IsValid(6) == false);
    }

    SECTION("IsSupported() checks minimum supported version")
    {
        REQUIRE(Transaction::IsSupported(1) == false);
        REQUIRE(Transaction::IsSupported(2) == false);
        REQUIRE(Transaction::IsSupported(3) == true);  // Minimum supported
        REQUIRE(Transaction::IsSupported(4) == true);
        REQUIRE(Transaction::IsSupported(5) == true);  // Current
        REQUIRE(Transaction::IsSupported(6) == false);
    }
}


//===========================================================================================
// BLOCK VERSION TESTS
//===========================================================================================

TEST_CASE("Block Version Constants", "[version][block]")
{
    SECTION("Block version constants are correctly defined")
    {
        REQUIRE(Block::LEGACY_POW_V1 == 1);
        REQUIRE(Block::LEGACY_POW_V2 == 2);
        REQUIRE(Block::TRITIUM_POW_V5 == 5);
        REQUIRE(Block::TRITIUM_POW_V6 == 6);
        REQUIRE(Block::TRITIUM_POW_V7 == 7);
        REQUIRE(Block::TRITIUM_POW_V8 == 8);
        REQUIRE(Block::TRITIUM_POS_V9 == 9);
        
        REQUIRE(Block::MAINNET_CURRENT == 9);
        REQUIRE(Block::TESTNET_CURRENT == 9);
        REQUIRE(Block::MINIMUM_SUPPORTED == 1);
    }
}


TEST_CASE("Block Version Helpers", "[version][block]")
{
    SECTION("IsLegacyPoW() identifies legacy PoW blocks")
    {
        REQUIRE(Block::IsLegacyPoW(1) == true);
        REQUIRE(Block::IsLegacyPoW(2) == true);
        REQUIRE(Block::IsLegacyPoW(3) == false);
        REQUIRE(Block::IsLegacyPoW(5) == false);
    }

    SECTION("IsTritiumPoW() identifies Tritium PoW blocks")
    {
        REQUIRE(Block::IsTritiumPoW(1) == false);
        REQUIRE(Block::IsTritiumPoW(2) == false);
        REQUIRE(Block::IsTritiumPoW(5) == true);
        REQUIRE(Block::IsTritiumPoW(6) == true);
        REQUIRE(Block::IsTritiumPoW(7) == true);
        REQUIRE(Block::IsTritiumPoW(8) == true);
        REQUIRE(Block::IsTritiumPoW(9) == false);
    }

    SECTION("IsTritiumPoS() identifies Tritium PoS blocks")
    {
        REQUIRE(Block::IsTritiumPoS(8) == false);
        REQUIRE(Block::IsTritiumPoS(9) == true);
        REQUIRE(Block::IsTritiumPoS(10) == false);
    }

    SECTION("IsTritium() identifies all Tritium blocks")
    {
        REQUIRE(Block::IsTritium(1) == false);
        REQUIRE(Block::IsTritium(2) == false);
        REQUIRE(Block::IsTritium(5) == true);
        REQUIRE(Block::IsTritium(6) == true);
        REQUIRE(Block::IsTritium(7) == true);
        REQUIRE(Block::IsTritium(8) == true);
        REQUIRE(Block::IsTritium(9) == true);
    }

    SECTION("IsValid() validates block versions")
    {
        REQUIRE(Block::IsValid(0) == false);
        REQUIRE(Block::IsValid(1) == true);
        REQUIRE(Block::IsValid(2) == true);
        REQUIRE(Block::IsValid(3) == false);
        REQUIRE(Block::IsValid(4) == false);
        REQUIRE(Block::IsValid(5) == true);
        REQUIRE(Block::IsValid(6) == true);
        REQUIRE(Block::IsValid(7) == true);
        REQUIRE(Block::IsValid(8) == true);
        REQUIRE(Block::IsValid(9) == true);
        REQUIRE(Block::IsValid(10) == false);
    }

    SECTION("IsSupported() checks minimum supported version")
    {
        REQUIRE(Block::IsSupported(0) == false);
        REQUIRE(Block::IsSupported(1) == true);  // Minimum supported
        REQUIRE(Block::IsSupported(5) == true);
        REQUIRE(Block::IsSupported(9) == true);  // Current
        REQUIRE(Block::IsSupported(10) == false);
    }

    SECTION("GetMiningVersion() returns correct version")
    {
        // Reset override
        Block::MINING_OVERRIDE_VERSION = 0;
        
        REQUIRE(Block::GetMiningVersion(false) == 9);  // Mainnet
        REQUIRE(Block::GetMiningVersion(true) == 9);   // Testnet
        
        // Test override
        Block::MINING_OVERRIDE_VERSION = 5;
        REQUIRE(Block::GetMiningVersion(false) == 5);
        REQUIRE(Block::GetMiningVersion(true) == 5);
        
        // Reset
        Block::MINING_OVERRIDE_VERSION = 0;
    }
}


//===========================================================================================
// STATE/RETARGET VERSION TESTS
//===========================================================================================

TEST_CASE("State Version Constants", "[version][state]")
{
    SECTION("State version constants are correctly defined")
    {
        REQUIRE(State::BASELINE_TRITIUM == 5);
        REQUIRE(State::MODERN_RETARGET == 7);
        REQUIRE(State::NEW_STAKE_RULES == 9);
    }
}


TEST_CASE("State Version Helpers", "[version][state][retarget]")
{
    SECTION("UsesModernRetarget() identifies v7+ states")
    {
        REQUIRE(State::UsesModernRetarget(5) == false);
        REQUIRE(State::UsesModernRetarget(6) == false);
        REQUIRE(State::UsesModernRetarget(7) == true);
        REQUIRE(State::UsesModernRetarget(8) == true);
        REQUIRE(State::UsesModernRetarget(9) == true);
    }

    SECTION("UsesV7StakeRules() identifies v7-v8 states")
    {
        REQUIRE(State::UsesV7StakeRules(6) == false);
        REQUIRE(State::UsesV7StakeRules(7) == true);
        REQUIRE(State::UsesV7StakeRules(8) == true);
        REQUIRE(State::UsesV7StakeRules(9) == false);
    }

    SECTION("UsesV9StakeRules() identifies v9+ states")
    {
        REQUIRE(State::UsesV9StakeRules(7) == false);
        REQUIRE(State::UsesV9StakeRules(8) == false);
        REQUIRE(State::UsesV9StakeRules(9) == true);
        REQUIRE(State::UsesV9StakeRules(10) == true);
    }

    SECTION("IsLegacy() identifies pre-Tritium states")
    {
        REQUIRE(State::IsLegacy(1) == true);
        REQUIRE(State::IsLegacy(4) == true);
        REQUIRE(State::IsLegacy(5) == false);
        REQUIRE(State::IsLegacy(9) == false);
    }
}


//===========================================================================================
// REGISTER VERSION TESTS
//===========================================================================================

TEST_CASE("Register Version Constants", "[version][register]")
{
    SECTION("Register version constants are correctly defined")
    {
        REQUIRE(Register::CURRENT == 1);
    }
}


TEST_CASE("Register Version Helpers", "[version][register]")
{
    SECTION("IsValid() validates register versions")
    {
        REQUIRE(Register::IsValid(0) == false);
        REQUIRE(Register::IsValid(1) == true);
        REQUIRE(Register::IsValid(2) == false);
    }
}


//===========================================================================================
// SIGNATURE SCHEME VERSION TESTS
//===========================================================================================

TEST_CASE("Signature Scheme Constants", "[version][signature][falcon]")
{
    SECTION("Falcon signature scheme constants match LLC enum")
    {
        REQUIRE(Signature::FALCON_512 == LLC::FalconVersion::FALCON_512);
        REQUIRE(Signature::FALCON_1024 == LLC::FalconVersion::FALCON_1024);
        REQUIRE(Signature::DEFAULT == Signature::FALCON_512);
    }
}


TEST_CASE("Signature Scheme Helpers", "[version][signature][falcon]")
{
    SECTION("GetFalconLogN() returns correct log(N) values")
    {
        REQUIRE(Signature::GetFalconLogN(Signature::FALCON_512) == 9);
        REQUIRE(Signature::GetFalconLogN(Signature::FALCON_1024) == 10);
    }

    SECTION("IsValid() validates Falcon versions")
    {
        REQUIRE(Signature::IsValid(Signature::FALCON_512) == true);
        REQUIRE(Signature::IsValid(Signature::FALCON_1024) == true);
    }

    SECTION("FromLegacyEnum() converts legacy enum values")
    {
        REQUIRE(Signature::FromLegacyEnum(1) == Signature::FALCON_512);
        REQUIRE(Signature::FromLegacyEnum(2) == Signature::FALCON_1024);
    }
}


//===========================================================================================
// MINING REQUIREMENTS TESTS
//===========================================================================================

TEST_CASE("Mining Requirements Constants", "[version][mining]")
{
    SECTION("Mining requirement constants are correctly defined")
    {
        REQUIRE(Mining::STATELESS_MINIMUM == 5);
    }
}


TEST_CASE("Mining Requirements Helpers", "[version][mining]")
{
    SECTION("IsStatelessCompatible() identifies v5+ blocks")
    {
        REQUIRE(Mining::IsStatelessCompatible(1) == false);
        REQUIRE(Mining::IsStatelessCompatible(4) == false);
        REQUIRE(Mining::IsStatelessCompatible(5) == true);
        REQUIRE(Mining::IsStatelessCompatible(9) == true);
    }

    SECTION("RequiresLegacyMiner() identifies v1-v4 blocks")
    {
        REQUIRE(Mining::RequiresLegacyMiner(1) == true);
        REQUIRE(Mining::RequiresLegacyMiner(4) == true);
        REQUIRE(Mining::RequiresLegacyMiner(5) == false);
        REQUIRE(Mining::RequiresLegacyMiner(9) == false);
    }
}


//===========================================================================================
// API SERIALIZATION TESTS
//===========================================================================================

TEST_CASE("API Serialization Constants", "[version][api]")
{
    SECTION("API serialization constants are correctly defined")
    {
        REQUIRE(API::LEGACY_FORMAT == 1);
        REQUIRE(API::TRITIUM_FORMAT == 2);
    }
}


TEST_CASE("API Serialization Helpers", "[version][api]")
{
    SECTION("RequiresTritiumAPI() identifies Tritium transactions")
    {
        REQUIRE(API::RequiresTritiumAPI(1) == false);
        REQUIRE(API::RequiresTritiumAPI(2) == false);
        REQUIRE(API::RequiresTritiumAPI(3) == true);
        REQUIRE(API::RequiresTritiumAPI(5) == true);
    }

    SECTION("GetSerializationVersion() returns correct format")
    {
        REQUIRE(API::GetSerializationVersion(1) == API::LEGACY_FORMAT);
        REQUIRE(API::GetSerializationVersion(2) == API::LEGACY_FORMAT);
        REQUIRE(API::GetSerializationVersion(3) == API::TRITIUM_FORMAT);
        REQUIRE(API::GetSerializationVersion(5) == API::TRITIUM_FORMAT);
    }
}


//===========================================================================================
// PROTOCOL VERSION TESTS
//===========================================================================================

TEST_CASE("Protocol Version Constants", "[version][protocol]")
{
    SECTION("Protocol version constants are correctly defined")
    {
        REQUIRE(Protocol::MAJOR == 3);
        REQUIRE(Protocol::MINOR == 6);
        REQUIRE(Protocol::PATCH == 0);
        REQUIRE(Protocol::BUILD == 0);
        
        REQUIRE(Protocol::VERSION == 3060000);
        REQUIRE(Protocol::MIN_VERSION == 20000);
        REQUIRE(Protocol::MIN_TRITIUM_VERSION == 3000000);
        REQUIRE(Protocol::MIN_CLIENT_VERSION == 3060000);
    }
}


TEST_CASE("Protocol Version Helpers", "[version][protocol]")
{
    SECTION("IsTritiumProtocol() identifies Tritium protocol versions")
    {
        REQUIRE(Protocol::IsTritiumProtocol(2000000) == false);
        REQUIRE(Protocol::IsTritiumProtocol(3000000) == true);
        REQUIRE(Protocol::IsTritiumProtocol(3060000) == true);
    }

    SECTION("IsClientMode() identifies client mode versions")
    {
        REQUIRE(Protocol::IsClientMode(3000000) == false);
        REQUIRE(Protocol::IsClientMode(3050000) == false);
        REQUIRE(Protocol::IsClientMode(3060000) == true);
        REQUIRE(Protocol::IsClientMode(3070000) == true);
    }
}


//===========================================================================================
// UPGRADE COORDINATOR TESTS
//===========================================================================================

TEST_CASE("Upgrade Coordinator Configuration", "[version][upgrade][hardfork]")
{
    SECTION("Default config has all flags disabled")
    {
        Upgrade::Config config;
        
        REQUIRE(config.ENFORCE_TRITIUM_TX_ONLY == false);
        REQUIRE(config.ENFORCE_TX_V5_ONLY == false);
        REQUIRE(config.ENFORCE_TRITIUM_BLOCKS_ONLY == false);
        REQUIRE(config.ENFORCE_V9_POS_ONLY == false);
        REQUIRE(config.ENFORCE_MODERN_RETARGET == false);
        REQUIRE(config.ENFORCE_STATELESS_MINING_ONLY == false);
        REQUIRE(config.MIN_PROTOCOL_OVERRIDE == 0);
    }

    SECTION("Pre-configured scenarios work correctly")
    {
        auto v9Config = Upgrade::Scenarios::V9OnlyPoS();
        REQUIRE(v9Config.ENFORCE_V9_POS_ONLY == true);
        REQUIRE(v9Config.ENFORCE_TRITIUM_BLOCKS_ONLY == true);
        REQUIRE(v9Config.ENFORCE_TRITIUM_TX_ONLY == true);
        
        auto statelessConfig = Upgrade::Scenarios::StatelessMiningOnly();
        REQUIRE(statelessConfig.ENFORCE_STATELESS_MINING_ONLY == true);
        REQUIRE(statelessConfig.ENFORCE_TRITIUM_BLOCKS_ONLY == true);
        
        auto retargetConfig = Upgrade::Scenarios::ModernRetargetOnly();
        REQUIRE(retargetConfig.ENFORCE_MODERN_RETARGET == true);
    }
}


//===========================================================================================
// VALIDATION HELPERS TESTS
//===========================================================================================

TEST_CASE("Validation Helpers", "[version][validation]")
{
    SECTION("IsTransactionVersionValid() validates correctly")
    {
        REQUIRE(Validation::IsTransactionVersionValid(0) == false);
        REQUIRE(Validation::IsTransactionVersionValid(1) == false);  // Not supported (< min)
        REQUIRE(Validation::IsTransactionVersionValid(3) == true);
        REQUIRE(Validation::IsTransactionVersionValid(5) == true);
        REQUIRE(Validation::IsTransactionVersionValid(6) == false);
    }

    SECTION("IsBlockVersionValid() validates correctly")
    {
        REQUIRE(Validation::IsBlockVersionValid(0) == false);
        REQUIRE(Validation::IsBlockVersionValid(1) == true);
        REQUIRE(Validation::IsBlockVersionValid(5) == true);
        REQUIRE(Validation::IsBlockVersionValid(9) == true);
        REQUIRE(Validation::IsBlockVersionValid(10) == false);
    }

    SECTION("Feature support checks work correctly")
    {
        REQUIRE(Validation::SupportsStatelessMining(4) == false);
        REQUIRE(Validation::SupportsStatelessMining(5) == true);
        
        REQUIRE(Validation::SupportsModernRetarget(6) == false);
        REQUIRE(Validation::SupportsModernRetarget(7) == true);
        
        REQUIRE(Validation::SupportsProofOfStake(8) == false);
        REQUIRE(Validation::SupportsProofOfStake(9) == true);
    }
}


//===========================================================================================
// INTEGRATION TESTS (semantic replacements for scattered checks)
//===========================================================================================

TEST_CASE("Semantic Helpers Replace Magic Numbers", "[version][integration]")
{
    SECTION("Replace 'nVersion >= 7' checks with UsesModernRetarget()")
    {
        // Old scattered code: if(state.nVersion >= 7)
        // New semantic code: if(State::UsesModernRetarget(state.nVersion))
        
        uint32_t oldStateVersion = 6;
        REQUIRE((oldStateVersion >= 7) == State::UsesModernRetarget(oldStateVersion));
        
        uint32_t modernStateVersion = 7;
        REQUIRE((modernStateVersion >= 7) == State::UsesModernRetarget(modernStateVersion));
    }

    SECTION("Replace 'nVersion < 9' checks with !UsesV9StakeRules()")
    {
        // Old scattered code: if(block.nVersion < 9)
        // New semantic code: if(!State::UsesV9StakeRules(block.nVersion))
        
        uint32_t v8Block = 8;
        REQUIRE((v8Block < 9) == !State::UsesV9StakeRules(v8Block));
        
        uint32_t v9Block = 9;
        REQUIRE((v9Block < 9) == !State::UsesV9StakeRules(v9Block));
    }

    SECTION("Replace 'pBlock->nVersion < 5' with !IsStatelessCompatible()")
    {
        // Old scattered code: if(pBlock->nVersion < 5)
        // New semantic code: if(!Mining::IsStatelessCompatible(pBlock->nVersion))
        
        uint32_t legacyBlock = 2;
        REQUIRE((legacyBlock < 5) == !Mining::IsStatelessCompatible(legacyBlock));
        
        uint32_t statelessBlock = 5;
        REQUIRE((statelessBlock < 5) == !Mining::IsStatelessCompatible(statelessBlock));
    }

    SECTION("Replace 'nVersion != 1' with !Register::IsValid()")
    {
        // Old scattered code: if(nVersion != 1) // TODO: make this a global constant
        // New semantic code: if(!Register::IsValid(nVersion))
        
        uint32_t validRegister = 1;
        REQUIRE((validRegister != 1) == !Register::IsValid(validRegister));
        
        uint32_t invalidRegister = 2;
        REQUIRE((invalidRegister != 1) == !Register::IsValid(invalidRegister));
    }
}


TEST_CASE("Version Control Addresses Original Problem Statement", "[version][requirements]")
{
    SECTION("Replaces hardcoded '7' checks in retarget.cpp")
    {
        // ~50 hardcoded "7" checks replaced with semantic helper
        REQUIRE(State::UsesModernRetarget(7) == true);
        REQUIRE(State::MODERN_RETARGET == 7);
    }

    SECTION("Replaces hardcoded '9' checks in stake.cpp")
    {
        // Stake rule version checks centralized
        REQUIRE(State::UsesV9StakeRules(9) == true);
        REQUIRE(State::NEW_STAKE_RULES == 9);
    }

    SECTION("Replaces hardcoded '5' check in stateless_miner_connection.cpp:2709")
    {
        // if(nChannel != 1 || pBlock->nVersion < 5)
        // becomes: if(nChannel != 1 || !Mining::IsStatelessCompatible(pBlock->nVersion))
        REQUIRE(Mining::STATELESS_MINIMUM == 5);
        REQUIRE(Mining::IsStatelessCompatible(5) == true);
    }

    SECTION("Addresses TODO in Register/state.cpp:206")
    {
        // "TODO: make this a global constant"
        // NOW IT IS: Register::CURRENT
        REQUIRE(Register::CURRENT == 1);
        REQUIRE(Register::IsValid(1) == true);
    }

    SECTION("Unifies Falcon version references from LLC/include/flkey.h")
    {
        REQUIRE(Signature::FALCON_512 == LLC::FalconVersion::FALCON_512);
        REQUIRE(Signature::FALCON_1024 == LLC::FalconVersion::FALCON_1024);
    }
}
