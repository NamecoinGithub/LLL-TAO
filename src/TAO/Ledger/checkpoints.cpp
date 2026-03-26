/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To The Voice of The People

____________________________________________________________________________________________*/

#include <LLD/include/global.h>

#include <LLP/include/global.h>

#include <TAO/Ledger/types/state.h>

#include <TAO/Ledger/include/chainstate.h>
#include <TAO/Ledger/include/checkpoints.h>

#include <cmath>

/* Global TAO namespace. */
namespace TAO
{

    /* Ledger Layer namespace. */
    namespace Ledger
    {

        /** Checkpoint timespan. **/
        uint32_t CHECKPOINT_TIMESPAN = 30;


        /* Check if the new block triggers a new Checkpoint timespan.*/
        bool IsNewTimespan(const BlockState& state)
        {
            /* Catch if checkpoint is not established. */
            if(ChainState::hashCheckpoint == 0)
                return true;

            /* Get previous block state. */
            BlockState statePrev;
            if(!LLD::Ledger->ReadBlock(state.hashPrevBlock, statePrev))
                return true;

            /* Get checkpoint state. */
            BlockState stateCheck;
            if(!LLD::Ledger->ReadBlock(state.hashCheckpoint, stateCheck))
                return debug::error(FUNCTION, "failed to read checkpoint");

            /* Calculate the time differences. */
            uint32_t nFirstMinutes = static_cast<uint32_t>((state.GetBlockTime() - stateCheck.GetBlockTime()) / 60);
            uint32_t nLastMinutes =  static_cast<uint32_t>((statePrev.GetBlockTime() - stateCheck.GetBlockTime()) / 60);

            return (nFirstMinutes != nLastMinutes && nFirstMinutes >= CHECKPOINT_TIMESPAN);
        }


        /* Check that the checkpoint is a Descendant of previous Checkpoint.*/
        bool IsDescendant(const BlockState& state)
        {
            /* Check if we should force our descendant checks. */
            if(config::GetBoolArg("-forcesync", false))
                return true;

            /* If no checkpoint defined, return true. */
            if(ChainState::hashCheckpoint == 0)
                return true;

            /* Check hard coded checkpoints when syncing. */
            if(ChainState::Synchronizing())
            {
                /* Check that height isn't exceeded. */
                if(config::fTestNet || state.nHeight > CHECKPOINT_HEIGHT)
                    return true;

                /* Check map checkpoints. */
                auto it = mapCheckpoints.find(state.nHeight);
                if(it == mapCheckpoints.end())
                    return true;

                /* Verbose logging for hardcoded checkpoints. */
                debug::log(0, "===== HARDCODED Checkpoint ", it->first, " Hash ", it->second.SubString());

                /* Block must match checkpoints map. */
                return it->second == state.hashCheckpoint;
            }

            /* Check The Block Hash */
            BlockState check = state;
            uint32_t nWalkDepth = 0;
            while(!check.IsNull())
            {
                /* Check that checkpoint exists in the map. */
                if(ChainState::hashCheckpoint.load() == check.hashCheckpoint)
                    return true;

                /* Break when the walking pointer drops below the checkpoint height. */
                if(check.nHeight < ChainState::nCheckpointHeight.load())
                {
                    debug::log(2, FUNCTION, "IsDescendant FAILED: block height=", state.nHeight,
                        " walked to height=", check.nHeight,
                        " below checkpointHeight=", ChainState::nCheckpointHeight.load(),
                        " checkpoint=", ChainState::hashCheckpoint.load().SubString(),
                        " walkDepth=", nWalkDepth);
                    return false;
                }

                /* Iterate backwards. */
                check = check.Prev();
                ++nWalkDepth;
            }

            debug::log(2, FUNCTION, "IsDescendant FAILED: block height=", state.nHeight,
                " backward walk exhausted (null block) after ", nWalkDepth, " steps",
                " checkpoint=", ChainState::hashCheckpoint.load().SubString(),
                " checkpointHeight=", ChainState::nCheckpointHeight.load());
            return false;
        }


        /*Harden a checkpoint into the checkpoint chain.*/
        bool HardenCheckpoint(const BlockState& state)
        {
            /* Only Harden New Checkpoint if it Fits new timestamp. */
            if(!IsNewTimespan(state))
                return false;

            /* Notify nodes of the checkpoint. */
            if(LLP::TRITIUM_SERVER && !ChainState::Synchronizing())
            {
                LLP::TRITIUM_SERVER->Relay
                (
                    LLP::TritiumNode::ACTION::NOTIFY,
                    uint8_t(LLP::TritiumNode::TYPES::CHECKPOINT),
                    state.hashCheckpoint
                );
            }

            /* Update the Checkpoints into Memory. */
            ChainState::hashCheckpoint    = state.hashCheckpoint;

            /* Get checkpoint state. */
            BlockState stateCheckpoint;
            if(!LLD::Ledger->ReadBlock(state.hashCheckpoint, stateCheckpoint))
                return debug::error(FUNCTION, "failed to read checkpoint");

            /* Set the correct height for the checkpoint. */
            ChainState::nCheckpointHeight = stateCheckpoint.nHeight;

            /* Dump the Checkpoint if not Initializing. */
            if(config::nVerbose >= ChainState::Synchronizing() ? 1 : 0)
                debug::log(ChainState::Synchronizing() ? 1 : 0, "===== Hardened Checkpoint ", ChainState::hashCheckpoint.load().SubString(), " Height ", ChainState::nCheckpointHeight.load());

            return true;
        }


        /* Repair in-memory checkpoint state if it is stale relative to the best chain state. */
        bool RepairCheckpointIfStale()
        {
            /* In-memory fast path — compare best chain state's checkpoint against the
             * global in-memory checkpoint without any disk I/O. */
            const BlockState stateBestMem = ChainState::tStateBest.load();
            const uint1024_t hashCheckpointMem = ChainState::hashCheckpoint.load();

            /* No staleness detected — nothing to repair. */
            if(stateBestMem.hashCheckpoint == hashCheckpointMem)
                return false;

            debug::error(FUNCTION, "CHECKPOINT STALE: in-memory=",
                hashCheckpointMem.SubString(),
                " best-state=", stateBestMem.hashCheckpoint.SubString(),
                " — repairing from best chain state");

            /* Repair: re-derive checkpoint from the best chain state. */
            const uint1024_t hashCheckpointOld = hashCheckpointMem;
            ChainState::hashCheckpoint = stateBestMem.hashCheckpoint;

            BlockState stateCheckpoint;
            if(!LLD::Ledger->ReadBlock(ChainState::hashCheckpoint.load(), stateCheckpoint))
            {
                /* Rollback on failure. */
                ChainState::hashCheckpoint = hashCheckpointOld;
                debug::error(FUNCTION, "Repair failed: could not read checkpoint block");
                return false;
            }
            ChainState::nCheckpointHeight = stateCheckpoint.nHeight;

            debug::log(0, FUNCTION, "Checkpoint repaired: ", hashCheckpointOld.SubString(),
                " -> ", ChainState::hashCheckpoint.load().SubString(),
                " height=", ChainState::nCheckpointHeight.load());
            return true;
        }


        /* Check descendant status, allowing for blocks that predate a recently-hardened checkpoint. */
        bool IsDescendantOrPredatesCheckpoint(const BlockState& state)
        {
            /* Standard descendant check. */
            if(IsDescendant(state))
                return true;

            /* If the block's height is at or above the checkpoint it truly fails. */
            if(state.nHeight >= ChainState::nCheckpointHeight.load())
                return false;

            /* The block predates the current checkpoint.  This can happen when
             * HardenCheckpoint() advances during SetBest() while this block was
             * still in the Accept() pipeline.
             *
             * Verify the block descends from a PREVIOUS checkpoint by reading the
             * current checkpoint block and checking whether the incoming block's
             * hashCheckpoint appears in the checkpoint block's ancestor chain. */
            BlockState stateCheckpoint;
            if(!LLD::Ledger->ReadBlock(ChainState::hashCheckpoint.load(), stateCheckpoint))
                return false;

            /* Bounded walk: search at most (checkpointHeight - blockHeight + a small margin). */
            const uint32_t nCheckpointHeight = ChainState::nCheckpointHeight.load();
            const uint32_t nMaxWalk = (nCheckpointHeight > state.nHeight)
                ? (nCheckpointHeight - state.nHeight + 10)
                : 10u;

            BlockState walk = stateCheckpoint;
            uint32_t nWalk = 0;
            while(!walk.IsNull() && nWalk < nMaxWalk)
            {
                if(walk.nHeight <= state.nHeight)
                    break;

                /* Check whether the walked block IS the checkpoint that the incoming block references. */
                if(walk.GetHash() == state.hashCheckpoint)
                {
                    debug::log(0, FUNCTION, "Block at height ", state.nHeight,
                        " predates checkpoint at height ", nCheckpointHeight,
                        " but shares ancestor checkpoint — ALLOWING");
                    return true;
                }

                walk = walk.Prev();
                ++nWalk;
            }

            return false;
        }
    }
}
