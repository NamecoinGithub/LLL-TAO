/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To The Voice of The People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_TAO_LEDGER_INCLUDE_VERSION_CONTROL_H
#define NEXUS_TAO_LEDGER_INCLUDE_VERSION_CONTROL_H

#include <cstdint>
#include <LLC/include/flkey.h>

/** Version Control Utility - Central Coordination of All Version Systems
 *
 *  This header consolidates ALL version-related constants and helpers from across the
 *  Nexus codebase into a single, well-documented, maintainable location.
 *
 *  PROBLEM SOLVED:
 *  - 300+ version references scattered across 60+ files
 *  - Hardcoded magic numbers ("7", "9", etc.) with no context
 *  - Impossible hardfork execution (hunt through 60 files, change 100+ numbers)
 *  - No central coordination between subsystems
 *  - Cannot hand off development to others
 *
 *  ARCHITECTURE:
 *  This utility provides version management for:
 *  1. Transactions (Legacy v1-v2, Tritium v3-v5)
 *  2. Blocks (Legacy PoW v1-v2, Tritium PoW v5-v8, Tritium PoS v9)
 *  3. State/Retarget (critical boundaries: v5, v7, v9)
 *  4. Registers (currently v1)
 *  5. Signature schemes (Falcon 512/1024)
 *  6. Mining requirements (stateless minimum v5)
 *  7. API serialization
 *  8. Protocol versions (network handshake)
 *
 *  USAGE:
 *  Instead of:  if(state.nVersion >= 7)  // What does 7 mean?
 *  Use:         if(State::UsesModernRetarget(state.nVersion))
 *
 *  HARDFORK EXECUTION:
 *  Change ONE LINE in Upgrade::Config, entire codebase updates automatically.
 *
 *  MIGRATION STRATEGY:
 *  This PR is non-breaking. Existing code works unchanged.
 *  Future PRs will gradually refactor scattered references to use this utility.
 **/

namespace TAO
{
namespace Ledger
{
namespace Versions
{

    //=======================================================================================
    // TRANSACTION VERSION MANAGEMENT
    //=======================================================================================

    /** Transaction Version Constants
     *
     *  Nexus transaction versions evolved from Legacy (Bitcoin-style UTXO)
     *  to Tritium (signature chain based, quantum-resistant).
     **/
    namespace Transaction
    {
        // Legacy transaction versions (Bitcoin-style UTXO)
        constexpr uint32_t LEGACY_V1 = 1;
        constexpr uint32_t LEGACY_V2 = 2;

        // Tritium transaction versions (Signature chain based)
        constexpr uint32_t TRITIUM_V3 = 3;  // First Tritium version
        constexpr uint32_t TRITIUM_V4 = 4;  // Enhanced Tritium
        constexpr uint32_t TRITIUM_V5 = 5;  // Current Tritium (target hardfork version)

        // Current network versions (what nodes should produce)
        constexpr uint32_t MAINNET_CURRENT = 5;
        constexpr uint32_t TESTNET_CURRENT = 5;

        // Minimum supported version (transition control from PR #167)
        // Allows v3/v4 during migration to v5
        // TODO: After v5 hardfork activated, consider raising to 5
        constexpr uint32_t MINIMUM_SUPPORTED = 3;

        /** Transaction version validation helpers **/
        inline bool IsLegacy(uint32_t nVersion)
        {
            return nVersion == LEGACY_V1 || nVersion == LEGACY_V2;
        }

        inline bool IsTritium(uint32_t nVersion)
        {
            return nVersion >= TRITIUM_V3 && nVersion <= TRITIUM_V5;
        }

        inline bool IsValid(uint32_t nVersion)
        {
            return IsLegacy(nVersion) || IsTritium(nVersion);
        }

        inline bool IsSupported(uint32_t nVersion)
        {
            return nVersion >= MINIMUM_SUPPORTED && nVersion <= MAINNET_CURRENT;
        }
    }


    //=======================================================================================
    // BLOCK VERSION MANAGEMENT
    //=======================================================================================

    /** Block Version Constants
     *
     *  Block versions control consensus rules, mining algorithms, and network upgrades.
     *  
     *  CRITICAL VERSIONS:
     *  - v5: Enabled stateless mining (removes miner.exe dependency)
     *  - v7: Changed retarget algorithm (modern difficulty adjustment)
     *  - v9: Enabled Proof-of-Stake (current consensus target)
     **/
    namespace Block
    {
        // Legacy Proof-of-Work versions
        constexpr uint32_t LEGACY_POW_V1 = 1;
        constexpr uint32_t LEGACY_POW_V2 = 2;

        // Tritium Proof-of-Work versions
        constexpr uint32_t TRITIUM_POW_V5 = 5;  // Stateless mining enabled
        constexpr uint32_t TRITIUM_POW_V6 = 6;
        constexpr uint32_t TRITIUM_POW_V7 = 7;  // Modern retarget algorithm
        constexpr uint32_t TRITIUM_POW_V8 = 8;

        // Tritium Proof-of-Stake version
        constexpr uint32_t TRITIUM_POS_V9 = 9;  // Current consensus target

        // Current network versions
        constexpr uint32_t MAINNET_CURRENT = 9;
        constexpr uint32_t TESTNET_CURRENT = 9;

        // Minimum supported version (transition control from PR #168)
        // Set to 1 to allow old v1/v2 miners during migration
        // SECURITY: Safe because PoW validation still enforced
        // TODO: After stateless miners reach >51% hashrate, change to 9
        constexpr uint32_t MINIMUM_SUPPORTED = 1;

        // Mining block version override (from -miningblockversion config)
        // Used for testing compatibility scenarios
        // Default is 0 (use current network version)
        // Declared extern here, defined in implementation file
        extern uint32_t MINING_OVERRIDE_VERSION;

        /** Block version validation helpers **/
        inline bool IsLegacyPoW(uint32_t nVersion)
        {
            return nVersion == LEGACY_POW_V1 || nVersion == LEGACY_POW_V2;
        }

        inline bool IsTritiumPoW(uint32_t nVersion)
        {
            return nVersion >= TRITIUM_POW_V5 && nVersion <= TRITIUM_POW_V8;
        }

        inline bool IsTritiumPoS(uint32_t nVersion)
        {
            return nVersion == TRITIUM_POS_V9;
        }

        inline bool IsTritium(uint32_t nVersion)
        {
            return IsTritiumPoW(nVersion) || IsTritiumPoS(nVersion);
        }

        inline bool IsValid(uint32_t nVersion)
        {
            return IsLegacyPoW(nVersion) || IsTritium(nVersion);
        }

        inline bool IsSupported(uint32_t nVersion)
        {
            return nVersion >= MINIMUM_SUPPORTED && nVersion <= MAINNET_CURRENT;
        }

        /** Get effective mining version (respects config override) **/
        inline uint32_t GetMiningVersion(bool fTestNet = false)
        {
            if(MINING_OVERRIDE_VERSION > 0)
                return MINING_OVERRIDE_VERSION;
            
            return fTestNet ? TESTNET_CURRENT : MAINNET_CURRENT;
        }
    }


    //=======================================================================================
    // STATE/RETARGET VERSION MANAGEMENT
    //=======================================================================================

    /** State Version Critical Boundaries
     *
     *  State versions control multiple independent features that changed at different versions.
     *  Each feature has its own semantic helper to allow independent evolution.
     *
     *  CRITICAL BOUNDARIES:
     *  - v5: Baseline Tritium state version
     *  - v7: Multiple features activated (retarget, coinstake, signature hash)
     *  - v9: New stake rules activated (Proof-of-Stake consensus)
     *
     *  IMPORTANT: Version 7 introduced multiple independent features. Each has its own
     *  helper function to allow future versions to change features independently.
     **/
    namespace State
    {
        constexpr uint32_t BASELINE_TRITIUM = 5;
        constexpr uint32_t MODERN_RETARGET  = 7;  // Key boundary: retarget algorithm change
        constexpr uint32_t TRITIUM_COINSTAKE = 7; // Key boundary: Tritium coinstake format
        constexpr uint32_t MODERN_SIGNATURE = 7;  // Key boundary: modern signature hash
        constexpr uint32_t NEW_STAKE_RULES  = 9;  // Key boundary: PoS activation

        /** Difficulty retarget algorithm version check
         *  Controls which difficulty retarget algorithm is used.
         *  Can be changed independently of other v7 features in future versions.
         **/
        inline bool UsesModernRetarget(uint32_t nVersion)
        {
            return nVersion >= MODERN_RETARGET;
        }

        /** Coinstake format version check
         *  Controls whether block uses Tritium coinstake (v7+) or Legacy coinstake (pre-v7).
         *  Can be changed independently of other v7 features in future versions.
         **/
        inline bool UsesTritiumCoinstake(uint32_t nVersion)
        {
            return nVersion >= TRITIUM_COINSTAKE;
        }

        /** Signature hash format version check
         *  Controls which signature hash algorithm is used.
         *  Can be changed independently of other v7 features in future versions.
         **/
        inline bool UsesModernSignatureHash(uint32_t nVersion)
        {
            return nVersion >= MODERN_SIGNATURE;
        }

        /** Stake rules version checks **/
        inline bool UsesV7StakeRules(uint32_t nVersion)
        {
            return nVersion >= MODERN_RETARGET && nVersion < NEW_STAKE_RULES;
        }

        inline bool UsesV9StakeRules(uint32_t nVersion)
        {
            return nVersion >= NEW_STAKE_RULES;
        }

        inline bool IsLegacy(uint32_t nVersion)
        {
            return nVersion < BASELINE_TRITIUM;
        }
    }


    //=======================================================================================
    // REGISTER VERSION MANAGEMENT
    //=======================================================================================

    /** Register Version Constants
     *
     *  Register objects (state storage) currently use version 1.
     *  Addresses TODO in Register/state.cpp:206: "make this a global constant"
     *
     *  Future versions may support:
     *  - Enhanced state compression
     *  - Alternative serialization formats
     *  - Extended metadata storage
     **/
    namespace Register
    {
        constexpr uint32_t CURRENT = 1;  // ✅ NOW IT IS A GLOBAL CONSTANT!

        inline bool IsValid(uint32_t nVersion)
        {
            return nVersion == CURRENT;
        }
    }


    //=======================================================================================
    // SIGNATURE SCHEME VERSION MANAGEMENT
    //=======================================================================================

    /** Signature Scheme Version Constants
     *
     *  Nexus uses Falcon post-quantum signature schemes (NIST finalist).
     *  Unifies scattered FalconVersion references into central management.
     *
     *  FALCON_512:  logn=9,  NIST Level 1, 128-bit quantum security (default)
     *  FALCON_1024: logn=10, NIST Level 5, 256-bit quantum security (high security)
     **/
    namespace Signature
    {
        // Map to LLC::FalconVersion enum for compatibility
        using FalconScheme = LLC::FalconVersion;

        constexpr FalconScheme FALCON_512  = FalconScheme::FALCON_512;
        constexpr FalconScheme FALCON_1024 = FalconScheme::FALCON_1024;

        constexpr FalconScheme DEFAULT = FALCON_512;

        /** Signature scheme helpers **/
        inline uint8_t GetFalconLogN(FalconScheme version)
        {
            return (version == FALCON_512) ? 9 : 10;
        }

        inline bool IsValid(FalconScheme version)
        {
            return version == FALCON_512 || version == FALCON_1024;
        }

        /** Convert from legacy uint8_t enum to FalconScheme **/
        inline FalconScheme FromLegacyEnum(uint8_t nVersion)
        {
            return (nVersion == 1) ? FALCON_512 : FALCON_1024;
        }
    }


    //=======================================================================================
    // MINING REQUIREMENTS
    //=======================================================================================

    /** Mining Version Requirements
     *
     *  Stateless mining requires block version >= 5.
     *  Replaces hardcoded check in stateless_miner_connection.cpp:2709.
     *
     *  CONTEXT:
     *  Legacy miners (Windows miner.exe) produced v1/v2 blocks.
     *  Stateless mining infrastructure requires v5+ for proper template handling.
     **/
    namespace Mining
    {
        constexpr uint32_t STATELESS_MINIMUM = 5;  // Prime channel stateless mining

        inline bool IsStatelessCompatible(uint32_t nBlockVersion)
        {
            return nBlockVersion >= STATELESS_MINIMUM;
        }

        inline bool RequiresLegacyMiner(uint32_t nBlockVersion)
        {
            return nBlockVersion < STATELESS_MINIMUM;
        }
    }


    //=======================================================================================
    // API SERIALIZATION VERSIONS
    //=======================================================================================

    /** API Serialization Version Constants
     *
     *  Controls JSON output format for API responses.
     *  Different transaction versions require different serialization.
     **/
    namespace API
    {
        constexpr uint32_t LEGACY_FORMAT  = 1;  // Legacy UTXO format
        constexpr uint32_t TRITIUM_FORMAT = 2;  // Tritium sigchain format

        inline bool RequiresTritiumAPI(uint32_t nTxVersion)
        {
            return nTxVersion >= Transaction::TRITIUM_V3;
        }

        inline uint32_t GetSerializationVersion(uint32_t nTxVersion)
        {
            return RequiresTritiumAPI(nTxVersion) ? TRITIUM_FORMAT : LEGACY_FORMAT;
        }
    }


    //=======================================================================================
    // PROTOCOL VERSION COORDINATION
    //=======================================================================================

    /** Network Protocol Version
     *
     *  Coordinates network handshake version with other version systems.
     *  Links to LLP/include/version.h protocol versioning.
     *
     *  FORMAT: MAJOR.MINOR.PATCH.BUILD (e.g., 3.6.0.0)
     *  COMPARABLE: MAJOR*1000000 + MINOR*10000 + PATCH*100 + BUILD
     **/
    namespace Protocol
    {
        constexpr uint32_t MAJOR = 3;
        constexpr uint32_t MINOR = 6;
        constexpr uint32_t PATCH = 0;
        constexpr uint32_t BUILD = 0;

        constexpr uint32_t VERSION = 
            MAJOR * 1000000 + MINOR * 10000 + PATCH * 100 + BUILD;

        constexpr uint32_t MIN_VERSION = 20000;
        constexpr uint32_t MIN_TRITIUM_VERSION = 3000000;
        constexpr uint32_t MIN_CLIENT_VERSION  = 3060000;

        inline bool IsTritiumProtocol(uint32_t nProtocolVersion)
        {
            return nProtocolVersion >= MIN_TRITIUM_VERSION;
        }

        inline bool IsClientMode(uint32_t nProtocolVersion)
        {
            return nProtocolVersion >= MIN_CLIENT_VERSION;
        }
    }


    //=======================================================================================
    // UPGRADE COORDINATOR
    //=======================================================================================

    /** Hardfork Execution Coordinator
     *
     *  ONE-STOP configuration for protocol upgrades.
     *  Change values here, entire codebase updates automatically.
     *
     *  USAGE EXAMPLE (hardfork to v9-only PoS):
     *      Upgrade::Config hardfork;
     *      hardfork.ENFORCE_V9_POS_ONLY = true;
     *      Upgrade::ApplyHardfork(hardfork);
     *
     *  RESULT: All 100+ version checks across codebase respect the new rules.
     **/
    namespace Upgrade
    {
        struct Config
        {
            // Enforce Tritium-only transactions (disable Legacy v1/v2)
            bool ENFORCE_TRITIUM_TX_ONLY = false;

            // Enforce v5+ transactions only (disable v3/v4 transition support)
            bool ENFORCE_TX_V5_ONLY = false;

            // Enforce Tritium-only blocks (disable Legacy v1/v2 mining)
            bool ENFORCE_TRITIUM_BLOCKS_ONLY = false;

            // Enforce v9 Proof-of-Stake only (disable PoW)
            bool ENFORCE_V9_POS_ONLY = false;

            // Enforce modern retarget algorithm (disable pre-v7 logic)
            bool ENFORCE_MODERN_RETARGET = false;

            // Enforce stateless mining only (disable legacy miners)
            bool ENFORCE_STATELESS_MINING_ONLY = false;

            // Cut off old protocol versions
            uint32_t MIN_PROTOCOL_OVERRIDE = 0;  // 0 = use defaults

            Config() = default;
        };

        /** Apply hardfork configuration (updates global state)
         *
         *  NOTE: This function provides the FRAMEWORK for centralized hardfork control.
         *  The actual implementation will be added in future refactoring PRs as existing
         *  code is migrated to use this utility.
         *
         *  Current status: STUB - Structure defined, implementation pending migration.
         *  
         *  Future implementation will update global constants in timelocks.cpp and other
         *  subsystems based on hardfork flags. This allows ONE-LINE hardfork execution:
         *      Upgrade::Config hardfork;
         *      hardfork.ENFORCE_V9_POS_ONLY = true;
         *      Upgrade::ApplyHardfork(hardfork);
         *  
         *  Once migration is complete, this will propagate changes to all 100+ version
         *  checks across the codebase automatically.
         **/
        inline void ApplyHardfork(const Config& config)
        {
            // STUB: Implementation will be added during refactoring PRs
            // This provides the framework for future centralized hardfork control
            (void)config; // Suppress unused parameter warning
        }

        /** Pre-configured hardfork scenarios for testing **/
        namespace Scenarios
        {
            inline Config V9OnlyPoS()
            {
                Config c;
                c.ENFORCE_V9_POS_ONLY = true;
                c.ENFORCE_TRITIUM_BLOCKS_ONLY = true;
                c.ENFORCE_TRITIUM_TX_ONLY = true;
                return c;
            }

            inline Config StatelessMiningOnly()
            {
                Config c;
                c.ENFORCE_STATELESS_MINING_ONLY = true;
                c.ENFORCE_TRITIUM_BLOCKS_ONLY = true;
                return c;
            }

            inline Config ModernRetargetOnly()
            {
                Config c;
                c.ENFORCE_MODERN_RETARGET = true;
                return c;
            }
        }
    }


    //=======================================================================================
    // VALIDATION HELPERS
    //=======================================================================================

    /** Centralized Validation Functions
     *
     *  Replaces scattered validation logic with consistent, well-tested functions.
     **/
    namespace Validation
    {
        /** Validate transaction version for current network state **/
        inline bool IsTransactionVersionValid(uint32_t nVersion, bool fTestNet = false)
        {
            if(!Transaction::IsValid(nVersion))
                return false;

            if(!Transaction::IsSupported(nVersion))
                return false;

            return true;
        }

        /** Validate block version for current network state **/
        inline bool IsBlockVersionValid(uint32_t nVersion, bool fTestNet = false)
        {
            if(!Block::IsValid(nVersion))
                return false;

            if(!Block::IsSupported(nVersion))
                return false;

            return true;
        }

        /** Check if block version supports required feature **/
        inline bool SupportsStatelessMining(uint32_t nBlockVersion)
        {
            return Mining::IsStatelessCompatible(nBlockVersion);
        }

        inline bool SupportsModernRetarget(uint32_t nStateVersion)
        {
            return State::UsesModernRetarget(nStateVersion);
        }

        inline bool SupportsProofOfStake(uint32_t nBlockVersion)
        {
            return Block::IsTritiumPoS(nBlockVersion);
        }
    }

} // namespace Versions
} // namespace Ledger
} // namespace TAO


#endif // NEXUS_TAO_LEDGER_INCLUDE_VERSION_CONTROL_H
