/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <TAO/Ledger/include/stateless_block_utility.h>
#include <TAO/Ledger/include/create.h>

#include <TAO/API/include/global.h>
#include <TAO/API/types/authentication.h>

#include <Util/include/debug.h>

/* Global TAO namespace. */
namespace TAO::Ledger
{
    /* Creates a Tritium block for stateless mining using authenticated context. */
    TAO::Ledger::TritiumBlock* TritiumBlockUtility::CreateForStatelessMining(
        const LLP::MiningContext& context,
        const uint32_t nChannel,
        const uint64_t nExtraNonce)
    {
        /* Validate the mining context first */
        if(!ValidateContext(context))
        {
            debug::error(FUNCTION, "Invalid mining context - cannot create block");
            return nullptr;
        }

        /* Get node operator's credentials (required for signing producer) */
        const memory::encrypted_ptr<TAO::Ledger::Credentials>* pCredentialsPtr = 
            GetNodeCredentials();
        
        if(!pCredentialsPtr)
        {
            debug::error(FUNCTION, "Cannot create block - node credentials not available");
            debug::error(FUNCTION, "  Stateless mining requires: nexus -unlock=mining");
            debug::error(FUNCTION, "  This provides node operator credentials for signing blocks");
            return nullptr;
        }

        /* Dereference to get the actual encrypted_ptr (now safe after null check) */
        const memory::encrypted_ptr<TAO::Ledger::Credentials>& pCredentials = *pCredentialsPtr;

        /* Unlock the node operator's PIN for mining */
        SecureString strPIN;
        if(!UnlockNodePin(strPIN))
        {
            debug::error(FUNCTION, "Cannot create block - failed to unlock PIN");
            return nullptr;
        }

        /* Get the reward payout address from context
         * This is the Falcon-authenticated miner's genesis or bound reward address */
        const uint256_t hashRewardAddress = context.GetPayoutAddress();
        
        /* Verify reward address is set (required for stateless mining) */
        if(hashRewardAddress == 0)
        {
            debug::error(FUNCTION, "Cannot create block - reward address not set");
            debug::error(FUNCTION, "  Miner must send MINER_SET_REWARD before requesting blocks");
            return nullptr;
        }

        /* Log the dual-identity model clearly for transparency */
        debug::log(1, FUNCTION, "=== Dual-Identity Mining Block Creation ===");
        debug::log(1, FUNCTION, "Block producer signing: ", pCredentials->Genesis().SubString(), 
                   " (node operator)");
        debug::log(1, FUNCTION, "Reward routing to:      ", hashRewardAddress.SubString(), 
                   " (miner)");
        debug::log(1, FUNCTION, "Mining channel:         ", 
                   nChannel == 1 ? "Prime" : nChannel == 2 ? "Hash" : "Private");
        debug::log(1, FUNCTION, "Extra nonce:            ", nExtraNonce);

        /* Allocate the block */
        TAO::Ledger::TritiumBlock* pBlock = new TAO::Ledger::TritiumBlock();

        /* CALL COLIN'S PROVEN CODE
         * This gives us everything for free:
         * ✅ Producer transaction with proper sequence
         * ✅ Ambassador rewards distribution
         * ✅ Developer fund allocation
         * ✅ Client-mode transaction inclusion from mempool
         * ✅ Proper difficulty calculations
         * ✅ Money supply tracking
         * ✅ All timelock consensus rules
         * ✅ Correct transaction ordering
         * ✅ Fee prioritization
         * ✅ Conflict detection
         * ✅ Merkle tree construction
         * ✅ Block metadata population
         *
         * The hashRewardAddress (from Falcon-authenticated miner) is passed
         * as hashDynamicGenesis parameter, which routes all mining rewards
         * to the miner's address instead of the node operator's address.
         */
        const bool fSuccess = TAO::Ledger::CreateBlock(
            pCredentials,               // Node operator's credentials (for signing)
            strPIN,                     // Node operator's PIN
            nChannel,                   // Mining channel (1=Prime, 2=Hash, 3=Private)
            *pBlock,                    // Output block
            nExtraNonce,                // Extra nonce for iteration
            nullptr,                    // No legacy coinbase (Tritium-only)
            hashRewardAddress           // Dynamic reward routing (miner's address)
        );

        /* Check if block creation succeeded */
        if(!fSuccess)
        {
            debug::error(FUNCTION, "CreateBlock failed - see previous errors");
            delete pBlock;
            return nullptr;
        }

        /* Log success */
        debug::log(2, FUNCTION, "Successfully created Tritium block ", 
                   pBlock->ProofHash().SubString(), 
                   " version=", pBlock->nVersion,
                   " for stateless miner");

        /* Return the completed block
         * It contains:
         * - Signed producer transaction (node operator's sigchain)
         * - All coinbase outputs routed to miner's address
         * - Ambassador rewards (if applicable)
         * - Developer fund (if applicable)  
         * - Client transactions from mempool
         * - Valid merkle tree
         * - All required block metadata
         */
        return pBlock;
    }


    /* Validates that the mining context has all required fields. */
    bool TritiumBlockUtility::ValidateContext(const LLP::MiningContext& context)
    {
        /* Check authentication status */
        if(!context.fAuthenticated)
        {
            debug::error(FUNCTION, "Mining context not authenticated");
            debug::error(FUNCTION, "  Miner must complete Falcon authentication before mining");
            return false;
        }

        /* Check that genesis hash is set (proves miner identity) */
        if(context.hashGenesis == 0)
        {
            debug::error(FUNCTION, "Mining context missing genesis hash");
            debug::error(FUNCTION, "  Genesis hash required for reward routing");
            return false;
        }

        /* Check that channel is valid (from context) */
        if(context.nChannel != 1 && context.nChannel != 2 && context.nChannel != 3)
        {
            debug::error(FUNCTION, "Invalid mining channel: ", context.nChannel);
            debug::error(FUNCTION, "  Valid channels: 1=Prime, 2=Hash, 3=Private");
            return false;
        }

        /* Verify payout address is configured */
        if(!context.HasValidPayout())
        {
            debug::error(FUNCTION, "Mining context has no valid payout configuration");
            debug::error(FUNCTION, "  Miner must authenticate and optionally set reward address");
            return false;
        }

        /* All validations passed */
        return true;
    }


    /* Retrieves the node operator's credentials from the DEFAULT session. */
    const memory::encrypted_ptr<TAO::Ledger::Credentials>* 
        TritiumBlockUtility::GetNodeCredentials()
    {
        try
        {
            /* Attempt to get credentials from DEFAULT session
             * This session is created when node is started with -unlock=mining */
            const memory::encrypted_ptr<TAO::Ledger::Credentials>& pCredentials = 
                TAO::API::Authentication::Credentials(
                    uint256_t(TAO::API::Authentication::SESSION::DEFAULT)
                );
            
            return &pCredentials;
        }
        catch(const std::exception& e)
        {
            /* Session doesn't exist - node not started with -unlock=mining */
            debug::log(2, FUNCTION, "DEFAULT session not found: ", e.what());
            return nullptr;
        }
    }


    /* Unlocks the node operator's PIN for mining operations. */
    bool TritiumBlockUtility::UnlockNodePin(SecureString& strPIN)
    {
        try
        {
            /* Unlock the PIN for mining operations
             * This uses the MINING PinUnlock type which allows mining operations */
            RECURSIVE(TAO::API::Authentication::Unlock(strPIN, TAO::Ledger::PinUnlock::MINING));
            return true;
        }
        catch(const std::exception& e)
        {
            debug::error(FUNCTION, "Failed to unlock PIN: ", e.what());
            return false;
        }
    }

} // namespace TAO::Ledger
