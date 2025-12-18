/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_TAO_LEDGER_INCLUDE_STATELESS_BLOCK_UTILITY_H
#define NEXUS_TAO_LEDGER_INCLUDE_STATELESS_BLOCK_UTILITY_H

#include <LLP/include/stateless_miner.h>
#include <TAO/Ledger/types/tritium.h>
#include <TAO/Ledger/types/credentials.h>

#include <Util/include/memory.h>

/* Global TAO namespace. */
namespace TAO::Ledger
{
    /** TritiumBlockUtility
     *
     *  Bridges stateless mining architecture to upstream Tritium block creation.
     *  Handles the authentication gap between Falcon-authenticated miners and
     *  Credentials-based block signing, while leveraging ALL of Colin's proven
     *  block creation logic (ambassador rewards, developer fund, client transactions, etc.)
     *
     *  INVESTIGATION FINDINGS:
     *
     *  Q1: How does Transaction::Sign() store signatures?
     *  A: Transaction::Sign() takes a uint512_t secret key, derives the public key
     *     based on nKeyType (FALCON or BRAINPOOL), and stores both vchPubKey and vchSig
     *     as member variables. The signature is over GetHash() of the transaction.
     *
     *  Q2: What signature scheme is used?
     *  A: Two schemes are supported: BRAINPOOL (ECC) and FALCON (post-quantum).
     *     The scheme is determined by the nKeyType field in the transaction.
     *     For mining, BRAINPOOL is typically used for producer transactions.
     *
     *  Q3: How is producer validated during block acceptance?
     *  A: Block validation checks that the producer transaction's hashGenesis matches
     *     an existing sigchain, the sequence is correct, the signature validates against
     *     the public key, and the public key hash matches the expected nextHash from
     *     the previous transaction in the sigchain.
     *
     *  Q4: Can Falcon signatures work for producers?
     *  A: Yes, producer transactions support both FALCON and BRAINPOOL signatures
     *     via the nKeyType field. However, the miner's Falcon key is for authentication
     *     only - the producer transaction needs to be signed by the node operator's
     *     sigchain (which can use either scheme).
     *
     *  Q5: What's the minimum Credentials object needed?
     *  A: Credentials needs:
     *     - strUsername (for Genesis() hash and Argon2 salt)
     *     - strPassword (for Argon2)
     *     - hashGenesis (computed from username)
     *     The Generate() method uses all three to derive signing keys via Argon2.
     *
     *  Q6: Can we create Credentials without username/password/PIN?
     *  A: No, not for full functionality. The Generate() method requires actual
     *     username/password/PIN for Argon2 key derivation. However, for stateless
     *     mining, the node operator MUST use -unlock=mining to provide their
     *     credentials, which are then used to sign blocks for remote miners.
     *
     *  APPROACH SELECTION:
     *
     *  Evaluated Options:
     *  - Option A (Miner-signed): Would require miner to have full sigchain, doesn't
     *                             work with stateless architecture where miner only
     *                             proves ownership via Falcon authentication
     *  - Option B (Ephemeral): Can't work - Generate() requires real credentials,
     *                          not possible to create ephemeral credentials that
     *                          produce valid signatures
     *  - Option C (Sig chain): Same issue - still needs real credentials
     *  - Option D (Falcon): Would require protocol changes for consensus
     *
     *  CHOSEN: Use Existing Session (Node Operator Signs)
     *
     *  RATIONALE:
     *  - Stateless mining already requires node operator to run with -unlock=mining
     *  - Node operator's credentials (in DEFAULT session) sign the producer transaction
     *  - Miner's Falcon-authenticated genesis is used for reward routing via hashDynamicGenesis
     *  - This is the "dual-identity" model: node operator signs, miner receives rewards
     *  - Already working in current new_block() - utility just formalizes and documents it
     *  - Maintains full compatibility with CreateBlock() without any credential workarounds
     *
     *  SECURITY CONSIDERATIONS:
     *  - Node operator controls block signing (as they should - it's their node)
     *  - Miner controls reward destination (proven via Falcon authentication)
     *  - No credential sharing between node and miner
     *  - Falcon authentication proves miner owns the genesis they claim
     *  - Network consensus validates all rewards go to valid addresses
     *
     *  SCALING CONSIDERATIONS:
     *  - Works with 500+ concurrent miners because:
     *    * Single set of node operator credentials reused for all blocks
     *    * No per-miner credential storage or derivation
     *    * Reward routing via hashDynamicGenesis parameter (already supported)
     *    * Minimal memory overhead per miner (just MiningContext)
     *
     **/
    class TritiumBlockUtility
    {
    public:
        /** CreateForStatelessMining
         *
         *  Creates a Tritium block for stateless mining using authenticated context.
         *  CALLS upstream CreateBlock() to get all complex logic for free.
         *
         *  This handles:
         *  - Ambassador reward distribution (automatic via CreateBlock)
         *  - Developer fund allocation (automatic via CreateBlock)
         *  - Client-mode transaction inclusion (automatic via CreateBlock)
         *  - Difficulty calculations (automatic via CreateBlock)
         *  - Money supply tracking (automatic via CreateBlock)
         *  - All consensus rules (automatic via CreateBlock)
         *
         *  Implementation: Uses node operator's credentials (from DEFAULT session)
         *  to sign the producer transaction, while routing rewards to the miner's
         *  Falcon-authenticated genesis hash (dual-identity mining model).
         *
         *  @param[in] context The Falcon-authenticated mining context
         *  @param[in] nChannel Mining channel (1=Prime, 2=Hash, 3=Private)
         *  @param[in] nExtraNonce Extra nonce for block iteration
         *
         *  @return Pointer to created Tritium block, or nullptr on error
         *
         **/
        static TAO::Ledger::TritiumBlock* CreateForStatelessMining(
            const LLP::MiningContext& context,
            const uint32_t nChannel,
            const uint64_t nExtraNonce
        );

    private:
        /** ValidateContext
         *
         *  Validates that the mining context has all required fields.
         *
         *  @param[in] context The mining context to validate
         *
         *  @return True if context is valid for block creation
         *
         **/
        static bool ValidateContext(const LLP::MiningContext& context);

        /** GetNodeCredentials
         *
         *  Retrieves the node operator's credentials from the DEFAULT session.
         *  These credentials are used to sign the producer transaction.
         *
         *  @return Pointer to credentials, or nullptr if not available
         *
         **/
        static const memory::encrypted_ptr<TAO::Ledger::Credentials>* GetNodeCredentials();

        /** UnlockNodePin
         *
         *  Unlocks the node operator's PIN for mining operations.
         *
         *  @param[out] strPIN The unlocked PIN
         *
         *  @return True if PIN was unlocked successfully
         *
         **/
        static bool UnlockNodePin(SecureString& strPIN);
    };
}

#endif
