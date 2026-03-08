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
#include <TAO/Ledger/include/supply.h>
#include <TAO/Ledger/include/retarget.h>
#include <TAO/Ledger/include/timelocks.h>
#include <TAO/Ledger/include/process.h>

#include <TAO/API/include/global.h>
#include <TAO/API/types/authentication.h>

#include <LLP/include/version.h>
#include <LLP/include/falcon_constants.h>
#include <LLP/include/disposable_falcon.h>

#include <Util/include/args.h>
#include <Util/include/convert.h>
#include <Util/include/debug.h>
#include <Util/include/runtime.h>
#include <sstream>

/* Global TAO namespace. */
namespace TAO::Ledger
{
    namespace
    {
        uint64_t bytes_to_uint64_le(const std::vector<uint8_t>& vData, const size_t nOffset)
        {
            uint64_t nValue = 0;
            for(size_t i = 0; i < LLP::FalconConstants::NONCE_SIZE; ++i)
                nValue |= static_cast<uint64_t>(vData[nOffset + i]) << (8 * i);

            return nValue;
        }


        bool TryParseFullBlockSubmission(const std::vector<uint8_t>& vData, ParseResult& result)
        {
            const size_t MIN_TRAILER_SIZE = LLP::FalconConstants::TIMESTAMP_SIZE
                                         + LLP::FalconConstants::LENGTH_FIELD_SIZE
                                         + LLP::FalconConstants::FALCON_SIG_MIN;

            if(vData.size() < LLP::FalconConstants::FULL_BLOCK_TRITIUM_MIN + MIN_TRAILER_SIZE)
                return false;

            static const uint16_t COMMON_SIG_SIZES[] = {
                LLP::FalconConstants::FALCON1024_SIG_COMMON_SIZE_1,
                LLP::FalconConstants::FALCON1024_SIG_COMMON_SIZE_2,
                LLP::FalconConstants::FALCON1024_SIG_COMMON_SIZE_3,
                LLP::FalconConstants::FALCON1024_SIG_COMMON_SIZE_4,
                LLP::FalconConstants::FALCON1024_SIG_COMMON_SIZE_5,
                LLP::FalconConstants::FALCON512_SIG_COMMON_SIZE_1,
                LLP::FalconConstants::FALCON512_SIG_COMMON_SIZE_2,
                LLP::FalconConstants::FALCON512_SIG_COMMON_SIZE_3,
                LLP::FalconConstants::FALCON512_SIG_COMMON_SIZE_4,
                LLP::FalconConstants::FALCON512_SIG_COMMON_SIZE_5
            };

            size_t nBlockBytes = 0;
            uint16_t nSigLen = 0;
            bool fFoundValidTrailer = false;

            const auto try_sig_len = [&](const uint16_t nCandidateSigLen) -> bool
            {
                if(vData.size() < LLP::FalconConstants::TIMESTAMP_SIZE
                                  + LLP::FalconConstants::LENGTH_FIELD_SIZE
                                  + nCandidateSigLen)
                    return false;

                const size_t nCandidateBlockBytes = vData.size()
                                                  - LLP::FalconConstants::TIMESTAMP_SIZE
                                                  - LLP::FalconConstants::LENGTH_FIELD_SIZE
                                                  - nCandidateSigLen;

                if(nCandidateBlockBytes < LLP::FalconConstants::FULL_BLOCK_TRITIUM_MIN
                || nCandidateBlockBytes > LLP::FalconConstants::FULL_BLOCK_TRITIUM_SIZE)
                    return false;

                const size_t nSigLenOffset = nCandidateBlockBytes + LLP::FalconConstants::TIMESTAMP_SIZE;
                const uint16_t nActualSigLen = static_cast<uint16_t>(vData[nSigLenOffset])
                    | (static_cast<uint16_t>(vData[nSigLenOffset + 1]) << 8);

                if(nActualSigLen != nCandidateSigLen
                || !LLP::FalconConstants::is_valid_sig_size(nActualSigLen))
                    return false;

                nBlockBytes = nCandidateBlockBytes;
                nSigLen = nActualSigLen;
                return true;
            };

            for(const uint16_t nCandidateSigLen : COMMON_SIG_SIZES)
            {
                if(try_sig_len(nCandidateSigLen))
                {
                    fFoundValidTrailer = true;
                    break;
                }
            }

            if(!fFoundValidTrailer)
            {
                for(size_t nCandidateSigLen = LLP::FalconConstants::FALCON_SIG_MIN;
                    nCandidateSigLen <= LLP::FalconConstants::FALCON_SIG_ABSOLUTE_MAX;
                    ++nCandidateSigLen)
                {
                    if(try_sig_len(static_cast<uint16_t>(nCandidateSigLen)))
                    {
                        fFoundValidTrailer = true;
                        break;
                    }
                }
            }

            if(!fFoundValidTrailer)
                return false;

            result.nBlockBytes = nBlockBytes;
            result.nSignatureLength = nSigLen;
            result.fFullBlockSubmission = true;
            result.timestamp = bytes_to_uint64_le(vData, nBlockBytes);
            result.vBlockBytes.assign(vData.begin(), vData.begin() + nBlockBytes);
            result.vSignature.assign(vData.begin() + nBlockBytes
                                   + LLP::FalconConstants::TIMESTAMP_SIZE
                                   + LLP::FalconConstants::LENGTH_FIELD_SIZE,
                                     vData.end());

            result.hashMerkle.SetBytes(std::vector<uint8_t>(
                vData.begin() + LLP::FalconConstants::FULL_BLOCK_MERKLE_OFFSET,
                vData.begin() + LLP::FalconConstants::FULL_BLOCK_MERKLE_OFFSET
                              + LLP::FalconConstants::MERKLE_ROOT_SIZE));

            result.nonce = bytes_to_uint64_le(vData, LLP::FalconConstants::FULL_BLOCK_TRITIUM_NONCE_OFFSET);

            if(nBlockBytes < 204)
            {
                result.reason = "full-block submission missing channel/height fields";
                return true;
            }

            result.nChannel = convert::bytes2uint(vData, 196);
            if(result.nChannel != 1 && result.nChannel != 2)
            {
                result.reason = "full-block submission missing valid mining channel";
                return true;
            }

            result.nUnifiedHeight = convert::bytes2uint(vData, 200);

            if(result.nChannel == 1)
            {
                if(nBlockBytes > LLP::FalconConstants::FULL_BLOCK_TRITIUM_MIN)
                {
                    result.vPrimeOffsets.assign(vData.begin() + LLP::FalconConstants::FULL_BLOCK_TRITIUM_MIN,
                                                vData.begin() + nBlockBytes);
                }
            }
            else if(nBlockBytes != LLP::FalconConstants::FULL_BLOCK_TRITIUM_MIN)
            {
                result.reason = "hash submission contains unexpected bytes before Falcon trailer";
                return true;
            }

            result.success = true;
            result.reason = "";
            return true;
        }
    }


    /* Create wallet-signed block for stateless mining */
    TritiumBlock* CreateBlockForStatelessMining(
        const uint32_t nChannel,
        const uint64_t nExtraNonce,
        const uint256_t& hashRewardAddress)
    {
        /* Early exit if shutdown is in progress */
        if(config::fShutdown.load())
        {
            debug::log(1, FUNCTION, "Shutdown in progress; skipping block creation");
            return nullptr;
        }
        
        /* Validate input nChannel parameter (defense in depth) */
        if(nChannel == 0)
        {
            debug::error(FUNCTION, "❌ Invalid input: nChannel is 0");
            debug::error(FUNCTION, "   Caller must provide valid channel (1=Prime, 2=Hash)");
            return nullptr;
        }
        
        if(nChannel != 1 && nChannel != 2)
        {
            debug::error(FUNCTION, "❌ Invalid input: nChannel = ", nChannel);
            debug::error(FUNCTION, "   Valid channels: 1 (Prime), 2 (Hash)");
            return nullptr;
        }
        
        /* All blocks MUST be wallet-signed per Nexus consensus */
        if (!TAO::API::Authentication::Unlocked(TAO::Ledger::PinUnlock::MINING))
        {
            debug::error(FUNCTION, "Mining not unlocked - use -unlock=mining or -autologin=username:password");
            debug::error(FUNCTION, "CRITICAL: Nexus consensus requires wallet-signed blocks");
            debug::error(FUNCTION, "Falcon authentication is for miner sessions, NOT block signing");
            return nullptr;
        }

        debug::log(1, FUNCTION, "Creating wallet-signed block (Nexus consensus requirement)");
        
        try {
            const uint256_t hashSession = uint256_t(TAO::API::Authentication::SESSION::DEFAULT);
            const auto& pCredentials = TAO::API::Authentication::Credentials(hashSession);
            
            SecureString strPIN;
            RECURSIVE(TAO::API::Authentication::Unlock(strPIN, TAO::Ledger::PinUnlock::MINING, hashSession));
            
            /* Get current chain state (SAME as normal node does) */
            const BlockState statePrev = ChainState::tStateBest.load();
            const uint32_t nChainHeight = ChainState::nBestHeight.load();
            
            /* Diagnostic logging */
            debug::log(2, FUNCTION, "=== CHAIN STATE DIAGNOSTIC ===");
            debug::log(2, FUNCTION, "  ChainState::nBestHeight: ", nChainHeight);
            debug::log(2, FUNCTION, "  statePrev.nHeight: ", statePrev.nHeight);
            debug::log(2, FUNCTION, "  statePrev.GetHash(): ", statePrev.GetHash().SubString());
            debug::log(2, FUNCTION, "  Synchronizing: ", ChainState::Synchronizing() ? "YES" : "NO");
            debug::log(2, FUNCTION, "  Template will be for height: ", statePrev.nHeight + 1);
            
            /* Verify chain state is valid before proceeding */
            if(!statePrev || statePrev.GetHash() == 0)
            {
                debug::error(FUNCTION, "Chain state not initialized - cannot create block template");
                debug::error(FUNCTION, "  Node may still be starting up or synchronizing");
                return nullptr;
            }
            
            /* Don't create blocks while synchronizing */
            if(ChainState::Synchronizing())
            {
                debug::error(FUNCTION, "Cannot create block templates while synchronizing");
                return nullptr;
            }
            
            TritiumBlock* pBlock = new TritiumBlock();
            
            // CreateBlock() handles wallet signing per consensus requirements
            bool success = CreateBlock(
                pCredentials,
                strPIN,
                nChannel,
                *pBlock,
                nExtraNonce,
                nullptr,           // No coinbase recipients
                hashRewardAddress  // Route rewards to miner's address
            );
            
            if (!success) {
                delete pBlock;
                debug::error(FUNCTION, "CreateBlock failed");
                return nullptr;
            }
            
            /* DO NOT call Check() here - the block hasn't been mined yet.
             * Check() validates PoW which requires a valid nonce from the miner.
             * Validation happens in validate_block() AFTER miner submits solution. */
            
            /* Basic sanity check only - verify CreateBlock() produced valid output */
            if(pBlock->hashMerkleRoot == 0)
            {
                debug::error(FUNCTION, "CreateBlock() produced invalid merkle root");
                delete pBlock;
                return nullptr;
            }
            
            /* Log block creation result */
            debug::log(2, FUNCTION, "CreateBlock: channel ", pBlock->nChannel, 
                       " unified height ", pBlock->nHeight);
            debug::log(2, FUNCTION, "  Note: PoW validation deferred until miner submits nonce");
            debug::log(2, FUNCTION, "  Reward address: ", hashRewardAddress.SubString());

            return pBlock;
        }
        catch (const std::exception& e) {
            debug::error(FUNCTION, "Block creation failed: ", e.what());
            return nullptr;
        }
    }


    /* Canonical validation entrypoint for mined Tritium blocks. */
    BlockValidationResult ValidateMinedBlock(const TAO::Ledger::TritiumBlock& block)
    {
        BlockValidationResult result;
        result.nChannel = block.nChannel;
        result.nHeight = block.nHeight;
        result.nUnifiedHeight = block.nHeight;  // block.nHeight is unified height (NexusMiner #169)
        result.hashBlock = block.hashMerkleRoot;

        debug::log(2, FUNCTION, "Centralized validation for block ", block.hashMerkleRoot.SubString(),
                   " channel=", block.nChannel, " unified_height=", block.nHeight);

        if(config::fShutdown.load())
        {
            result.reason = "shutdown in progress";
            return result;
        }

        if(block.IsNull())
        {
            result.reason = "block is null";
            return result;
        }

        if(block.hashMerkleRoot == 0)
        {
            result.reason = "block merkle root is null";
            return result;
        }

        if(block.nChannel != 1 && block.nChannel != 2)
        {
            result.reason = "invalid block channel";
            return result;
        }

        if(block.nHeight == 0)
        {
            result.reason = "invalid block height";
            return result;
        }

        if(!block.Check())
        {
            result.reason = "block Check() failed";
            return result;
        }

        /* Stale detection uses unified chain tip shared across channels. */
        if(block.hashPrevBlock != TAO::Ledger::ChainState::hashBestChain.load())
        {
            result.reason = "submitted block is stale";
            return result;
        }

        result.valid = true;
        result.reason = "valid";
        return result;
    }
    /* Canonical acceptance entrypoint for mined Tritium blocks. */
    BlockAcceptanceResult AcceptMinedBlock(TAO::Ledger::TritiumBlock& block)
    {
        BlockAcceptanceResult result;
        result.nChannel = block.nChannel;
        result.nHeight = block.nHeight;
        result.nUnifiedHeight = block.nHeight;  // block.nHeight is unified height (NexusMiner #169)
        result.hashBlock = block.hashMerkleRoot;

        debug::log(2, FUNCTION, "Centralized acceptance for block ", block.hashMerkleRoot.SubString(),
                   " channel=", block.nChannel, " unified_height=", block.nHeight);

        /* Unlock sigchain to process mined block. */
        try
        {
            SecureString strPIN; // empty PIN expected; Authentication::Unlock fetches mining PIN for unlocked session
            RECURSIVE(TAO::API::Authentication::Unlock(strPIN, TAO::Ledger::PinUnlock::MINING));
        }
        catch(const std::exception& e)
        {
            result.reason = e.what();
            return result;
        }

        uint8_t nStatus = 0;
        TAO::Ledger::Process(block, nStatus);
        result.status = nStatus;
        result.accepted = (nStatus & TAO::Ledger::PROCESS::ACCEPTED);

        if(!result.accepted)
        {
            if(nStatus & TAO::Ledger::PROCESS::ORPHAN)
                result.reason = "block is orphan";
            else if(nStatus & TAO::Ledger::PROCESS::DUPLICATE)
                result.reason = "duplicate block";
            else if(nStatus & TAO::Ledger::PROCESS::INCOMPLETE)
                result.reason = "block incomplete";
            else if(nStatus & TAO::Ledger::PROCESS::REJECTED)
                result.reason = "block rejected";
            else if(nStatus & TAO::Ledger::PROCESS::IGNORED)
                result.reason = "block ignored";
            else
                result.reason = "block not accepted";
            return result;
        }

        result.reason = "accepted";
        return result;
    }


    /* Canonical acceptance entrypoint for mined Tritium blocks. */
    SubmitResult SubmitMinedBlockForStatelessMining(TAO::Ledger::TritiumBlock& block)
    {
        SubmitResult result;
        result.nChannel = block.nChannel;
        result.nHeight = block.nHeight;
        result.nUnifiedHeight = block.nHeight;  // block.nHeight is unified height (NexusMiner #169)
        result.hashBlock = block.hashMerkleRoot;

        debug::log(0, FUNCTION, "[BLOCK SUBMIT] nHeight=", block.nHeight, " (unified)",
                   " channel=", block.nChannel,
                   " hashPrevBlock=", block.hashPrevBlock.SubString());

        const BlockValidationResult validationResult = ValidateMinedBlock(block);
        if(!validationResult.valid)
        {
            result.reason = validationResult.reason;
            return result;
        }

        const BlockAcceptanceResult acceptanceResult = AcceptMinedBlock(block);
        if(!acceptanceResult.accepted)
        {
            result.reason = acceptanceResult.reason;
            return result;
        }

        result.accepted = true;
        result.reason = acceptanceResult.reason;
        return result;
    }


    /* Parse stateless miner work submission payloads. */
    ParseResult ParseStatelessWorkSubmission(const std::vector<uint8_t>& vData)
    {
        ParseResult result;

        if(vData.size() < LLP::FalconConstants::MERKLE_ROOT_SIZE + LLP::FalconConstants::NONCE_SIZE)
        {
            result.reason = "submission payload too small";
            return result;
        }

        if(TryParseFullBlockSubmission(vData, result))
            return result;

        if(vData.size() >= LLP::FalconConstants::SUBMIT_BLOCK_WRAPPER_MIN)
        {
            LLP::DisposableFalcon::SignedWorkSubmission submission;
            if(submission.Deserialize(vData) && submission.IsValid())
            {
                result.hashMerkle = submission.hashMerkleRoot;
                result.nonce = submission.nNonce;
                result.timestamp = submission.nTimestamp;
                result.nSignatureLength = static_cast<uint16_t>(submission.vSignature.size());
                result.vSignature = submission.vSignature;
                result.success = true;

                /* Opportunistically extract nUnifiedHeight from the block body when the
                 * payload is large enough to contain a full Tritium block header (>= 204 bytes
                 * covers offsets [0-203]).  We validate nChannel (must be 1 or 2) at offset
                 * 196 to discriminate full-block-body payloads from compact-format submissions
                 * that happen to be >= 204 bytes.  Channel 0 (Proof-of-Stake) is intentionally
                 * excluded because stateless mining only supports Prime (1) and Hash (2).
                 * nHeight lives at offset 200 (big-endian uint32_t). */
                if(vData.size() >= 204)
                {
                    const uint32_t nCh = convert::bytes2uint(vData, 196);
                    if(nCh == 1 || nCh == 2)
                    {
                        result.nUnifiedHeight = convert::bytes2uint(vData, 200);
                    }
                }

                return result;
            }
        }

        result.hashMerkle.SetBytes(std::vector<uint8_t>(
            vData.begin(),
            vData.begin() + LLP::FalconConstants::MERKLE_ROOT_SIZE));

        /* Nonce is little-endian per Falcon stateless protocol. */
        uint64_t nonce = 0;
        for(size_t i = 0; i < LLP::FalconConstants::NONCE_SIZE; ++i)
        {
            nonce |= static_cast<uint64_t>(vData[LLP::FalconConstants::MERKLE_ROOT_SIZE + i]) << (8 * i);
        }
        result.nonce = nonce;

        result.success = true;
        return result;
    }

}
