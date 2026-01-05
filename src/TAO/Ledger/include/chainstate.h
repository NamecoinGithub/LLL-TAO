/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To The Voice of The People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_TAO_LEDGER_INCLUDE_CHAINSTATE_H
#define NEXUS_TAO_LEDGER_INCLUDE_CHAINSTATE_H

#include <LLC/types/uint1024.h>

#include <TAO/Ledger/types/state.h>

#include <Util/include/memory.h>

/* Global TAO namespace. */
namespace TAO
{

    /* Ledger Layer namespace. */
    namespace Ledger
    {

        /** ChainState
         *
         *
         *
         **/
        namespace ChainState
        {

            /** The best block height in the chain. **/
            extern std::atomic<uint32_t> nBestHeight;


            /** The best trust in the chain. **/
            extern std::atomic<uint64_t> nBestChainTrust;


            /** The current checkpoint height. **/
            extern std::atomic<uint64_t> nCheckpointHeight;


            /** Track if the chain is currently undergoing a re-org. */
            extern std::atomic<bool> fChainReorg;


            /** The best hash in the chain. */
            extern memory::atomic<uint1024_t> hashBestChain;


            /** Hardened Checkpoint. **/
            extern memory::atomic<uint1024_t> hashCheckpoint;


            /** Synchronizing
             *
             *  Flag to tell if initial blocks are downloading.
             *
             **/
            bool Synchronizing();


            /** PercentSynchronized
             *
             *  Real value of the total synchronization percent completion.
             *
             **/
            double PercentSynchronized();


            /** SyncProgress
             *
             *  Percentage of blocks synchronized since the node started.
             *
             **/
            double SyncProgress();


            /** Initialize
             *
             *  Initialize the Chain State.
             *
             **/
            bool Initialize();


            /** Genesis
             *
             *  Get the hash of the genesis block.
             *
             **/
            uint1024_t Genesis();


            /** The best block in the chain. **/
            extern memory::atomic<BlockState> tStateBest;


            /** The best block in the chain. **/
            extern BlockState tStateGenesis;


            /** ForkDetector
             *
             *  Monitors consecutive validation failures and triggers rollback.
             *
             **/
            class ForkDetector
            {
            private:
                static uint32_t nConsecutiveFailures;
                static uint32_t nLastGoodHeight;
                static uint512_t hashLastGoodBlock;

            public:
                /** RecordSuccess
                 *
                 *  Reset failure counter after successful validation.
                 *
                 *  @param[in] nHeight The height of the successful block
                 *  @param[in] hash The hash of the successful block
                 *
                 **/
                static void RecordSuccess(uint32_t nHeight, const uint512_t& hash);

                /** RecordFailure
                 *
                 *  Increment failure counter.
                 *
                 **/
                static void RecordFailure();

                /** CheckForFork
                 *
                 *  Detects fork conditions based on consecutive failures.
                 *
                 *  @return True if fork threshold exceeded
                 *
                 **/
                static bool CheckForFork();

                /** GetRollbackHeight
                 *
                 *  Determines safe rollback height.
                 *
                 *  @return Height to rollback to
                 *
                 **/
                static uint32_t GetRollbackHeight();

                /** TriggerRollback
                 *
                 *  Initiates blockchain rollback to last known-good state.
                 *
                 *  @return True if rollback succeeded
                 *
                 **/
                static bool TriggerRollback();

                /** ResurrectTransactions
                 *
                 *  Re-inserts transactions from rolled-back blocks into mempool.
                 *
                 *  @param[in] nFromHeight Starting height
                 *  @param[in] nToHeight Ending height
                 *
                 *  @return True if transactions resurrected successfully
                 *
                 **/
                static bool ResurrectTransactions(uint32_t nFromHeight, uint32_t nToHeight);
            };

        }
    }
}


#endif
