/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <TAO/Ledger/include/stateless_block_utility.h>
#include <TAO/Ledger/include/create.h>
#include <TAO/Ledger/include/chainstate.h>
#include <TAO/Ledger/include/difficulty.h>
#include <TAO/Ledger/include/retarget.h>

#include <TAO/API/include/global.h>
#include <TAO/API/types/authentication.h>

#include <Util/include/debug.h>
#include <Util/include/runtime.h>
#include <Util/include/config.h>
#include <Util/include/mutex.h>

/* Global TAO namespace. */
namespace TAO::Ledger
{
    /* Helper to get default session ID */
    static const uint256_t GetDefaultSessionId()
    {
        return uint256_t(TAO::API::Authentication::SESSION::DEFAULT);
    }


    /* Detects which mining mode is available based on node state. */
    MiningMode DetectMiningMode()
    {
        /* Check if hybrid mining is enabled */
        if(!config::fHybrid.load())
        {
            debug::error(FUNCTION, "Hybrid mining not enabled");
            debug::error(FUNCTION, "  Start daemon with -hybrid flag");
            return MiningMode::UNAVAILABLE;
        }
        
        /* Hybrid mining is stateless - no credentials needed */
        debug::log(1, FUNCTION, "Mining Mode: HYBRID_STATELESS");
        debug::log(1, FUNCTION, "  - Hybrid block mining (nChannel = 3)");
        debug::log(1, FUNCTION, "  - Private mining enabled");
        debug::log(1, FUNCTION, "  - Simpler consensus rules");
        
        return MiningMode::HYBRID_STATELESS;
    }


    /* Creates a Hybrid block for learning/ALPHA branch. */
    static TritiumBlock* CreateHybridBlock(
        const uint32_t nChannel,
        const uint64_t nExtraNonce,
        const uint256_t& hashRewardAddress)
    {
        try
        {
            debug::log(2, FUNCTION, "Creating Hybrid block (ALPHA - learning)");
            debug::log(2, FUNCTION, "  Channel: ", nChannel);
            debug::log(2, FUNCTION, "  Extra nonce: ", nExtraNonce);
            debug::log(2, FUNCTION, "  Reward address: ", hashRewardAddress.SubString());
            
            /* Get the current best block */
            const TAO::Ledger::BlockState stateBest = 
                TAO::Ledger::ChainState::tStateBest.load();
            
            /* Create new block */
            TritiumBlock* pBlock = new TritiumBlock();
            
            /* Set block header fields */
            pBlock->nVersion = TAO::Ledger::CurrentBlockVersion();
            pBlock->hashPrevBlock = stateBest.GetHash();
            pBlock->nChannel = nChannel;
            pBlock->nHeight = stateBest.nHeight + 1;
            pBlock->nBits = GetNextTargetRequired(
                stateBest, nChannel, false);
            pBlock->nNonce = nExtraNonce;
            pBlock->nTime = runtime::unifiedtimestamp();
            
            /* ALPHA LEARNING: Create a simplified producer transaction
             * 
             * KNOWN ISSUES (learning from these):
             * 1. Missing hashPrevTx (requires sigchain lookup)
             * 2. nSequence should be looked up from last transaction
             * 3. Missing AUTHORIZE operation (hybrid blocks need this)
             * 4. Producer not properly signed
             * 
             * CORRECT APPROACH (for future):
             * - Use CreateTransaction() helper to get proper sequence
             * - Add OP::AUTHORIZE operation like create.cpp line 629
             * - Sign producer transaction with proper credentials
             * - Store producer in block.producer field, not vtx
             * 
             * CURRENT STATUS:
             * This simplified version will likely fail validation.
             * That's OK - we're learning what fields are required!
             */
            
            TAO::Ledger::Transaction producer;
            producer.nVersion = 1;
            producer.nSequence = 0;  // WRONG: Should lookup from sigchain
            producer.nTimestamp = pBlock->nTime;
            producer.hashGenesis = hashRewardAddress;
            producer.nKeyType = TAO::Ledger::SIGNATURE::BRAINPOOL;
            producer.nNextType = TAO::Ledger::SIGNATURE::BRAINPOOL;
            // MISSING: hashPrevTx
            // MISSING: OP::AUTHORIZE operation
            // MISSING: Proper signing
            
            /* LEARNING NOTE: This approach is wrong but educational
             * Real implementation should store producer directly:
             *   pBlock->producer = producer;
             * Not in vtx vector. We'll learn this from validation errors.
             */
            pBlock->vtx.push_back(
                std::make_pair(TAO::Ledger::TRANSACTION::TRITIUM, producer.GetHash())
            );
            
            /* Calculate merkle root */
            /* LEARNING NOTE: This may not be correct for hybrid blocks
             * Real merkle construction might need special handling.
             * We'll discover the right approach from errors.
             */
            pBlock->hashMerkleRoot = pBlock->BuildMerkleTree(pBlock->vtx);
            
            debug::log(2, FUNCTION, "Hybrid block created (INCOMPLETE - learning version)");
            debug::log(2, FUNCTION, "  Height: ", pBlock->nHeight);
            debug::log(2, FUNCTION, "  Version: ", pBlock->nVersion);
            debug::log(2, FUNCTION, "  Channel: ", pBlock->nChannel);
            debug::log(2, FUNCTION, "  Prev block: ", pBlock->hashPrevBlock.SubString());
            debug::log(2, FUNCTION, "");
            debug::log(2, FUNCTION, "ALPHA NOTE: This block is missing critical fields:");
            debug::log(2, FUNCTION, "  - Producer transaction not properly initialized");
            debug::log(2, FUNCTION, "  - Missing AUTHORIZE operation");
            debug::log(2, FUNCTION, "  - Not signed");
            debug::log(2, FUNCTION, "  - May fail validation (this is expected!)");
            debug::log(2, FUNCTION, "Goal: Learn from validation errors what's needed");
            
            return pBlock;
        }
        catch(const std::exception& e)
        {
            debug::error(FUNCTION, "Exception creating hybrid block: ", e.what());
            return nullptr;
        }
    }


    /* Creates a Tritium block using node credentials (Mode 2). */
    /* ALPHA BRANCH: Not used - stubbed for reference */
    static TritiumBlock* CreateWithNodeCredentials(
        const uint32_t nChannel,
        const uint64_t nExtraNonce,
        const uint256_t& hashRewardAddress)
    {
        debug::error(FUNCTION, "Mode 2 (INTERFACE_SESSION) not used in ALPHA branch");
        debug::error(FUNCTION, "  ALPHA focuses on Hybrid blocks only (nChannel = 3)");
        debug::error(FUNCTION, "  This function kept for reference");
        return nullptr;
    }


    /* Creates a Tritium block with miner-signed producer (Mode 1). */
    /* ALPHA BRANCH: Not used - stubbed for reference */
    static TritiumBlock* CreateWithMinerProducer(
        const uint32_t nChannel,
        const uint64_t nExtraNonce,
        const uint256_t& hashRewardAddress,
        const Transaction& preSignedProducer)
    {
        debug::error(FUNCTION, "Mode 1 (DAEMON_STATELESS) not used in ALPHA branch");
        debug::error(FUNCTION, "  ALPHA focuses on Hybrid blocks only (nChannel = 3)");
        debug::error(FUNCTION, "  This function kept for reference");
        return nullptr;
    }


    /* Unified interface for stateless mining block creation. */
    TritiumBlock* CreateBlockForStatelessMining(
        const uint32_t nChannel,
        const uint64_t nExtraNonce,
        const uint256_t& hashRewardAddress,
        const Transaction* pPreSignedProducer)
    {
        /* Only support hybrid blocks */
        if(nChannel != 3)
        {
            debug::error(FUNCTION, "Only hybrid blocks (nChannel = 3) supported in ALPHA");
            debug::error(FUNCTION, "  Requested channel: ", nChannel);
            return nullptr;
        }
        
        /* Detect mining mode */
        MiningMode mode = DetectMiningMode();
        if(mode != MiningMode::HYBRID_STATELESS)
        {
            debug::error(FUNCTION, "Hybrid mining not available");
            return nullptr;
        }
        
        /* Create hybrid block */
        return CreateHybridBlock(nChannel, nExtraNonce, hashRewardAddress);
    }


    /* Validates a miner-signed producer transaction. */
    bool ValidateMinerProducer(
        const Transaction& producer,
        const uint256_t& hashExpectedGenesis,
        const uint256_t& hashBoundRewardAddress)
    {
        /* This would validate:
         * 1. Producer signature matches Falcon-authenticated genesis
         * 2. Producer structure is correct
         * 3. Sequence number is valid
         * 4. Timestamp is recent
         * 5. Not a replay attack
         * 6. Reward address matches bound address
         * 
         * NOT YET IMPLEMENTED - Part of Mode 1 enhancement
         */

        debug::error(FUNCTION, "ValidateMinerProducer not yet implemented");
        return false;
    }


    /* Creates template data for miner-side producer creation. */
    bool CreateProducerTemplate(
        const uint32_t nChannel,
        uint1024_t& hashPrevBlock,
        uint32_t& nHeight,
        uint32_t& nVersion,
        uint64_t& nTimestamp)
    {
        /* This would provide:
         * - Previous block hash
         * - Current block height  
         * - Block version
         * - Suggested timestamp
         * 
         * Miner would use this to create producer locally.
         * 
         * NOT YET IMPLEMENTED - Part of Mode 1 enhancement
         */

        try
        {
            /* Get current best state */
            const TAO::Ledger::BlockState tStateBest =
                TAO::Ledger::ChainState::tStateBest.load();

            /* Fill in template data */
            hashPrevBlock = tStateBest.GetHash();
            nHeight = tStateBest.nHeight + 1;
            nTimestamp = runtime::unifiedtimestamp();

            /* Get block version */
            uint32_t nCurrent = CurrentBlockVersion();
            if(BlockVersionActive(nTimestamp, nCurrent))
                nVersion = nCurrent;
            else
                nVersion = nCurrent - 1;

            debug::log(2, FUNCTION, "Created producer template:");
            debug::log(2, FUNCTION, "  Height: ", nHeight);
            debug::log(2, FUNCTION, "  Version: ", nVersion);
            debug::log(2, FUNCTION, "  Prev Block: ", hashPrevBlock.SubString());
            debug::log(2, FUNCTION, "  Timestamp: ", nTimestamp);

            return true;
        }
        catch(const std::exception& e)
        {
            debug::error(FUNCTION, "Exception: ", e.what());
            return false;
        }
    }

}
