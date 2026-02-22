/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_TAO_LEDGER_TYPES_TRITIUM_H
#define NEXUS_TAO_LEDGER_TYPES_TRITIUM_H

#include <LLC/include/flkey.h>

#include <LLP/include/falcon_constants.h>

#include <TAO/Register/types/stream.h>

#include <TAO/Ledger/types/block.h>
#include <TAO/Ledger/types/transaction.h>

#include <Util/templates/serialize.h>

#include <stdexcept>

/* Global TAO namespace. */
namespace TAO
{

    /* Ledger Layer namespace. */
    namespace Ledger
    {
        class BlockState;
        class SyncBlock;


        /** TritiumBlock
         *
         *  A tritium block contains references to the transactions in blocks.
         *  These are used to build the merkle tree for checking.
         *  Transactions are processed before block is recieved, and commit
         *  When a block is recieved to break up processing requirements.
         *
         **/
        class TritiumBlock : public Block
        {
        public:

            /** The Block's timestamp. This number is locked into the signature hash. **/
            uint64_t nTime;


            /** Producer Transaction.
             *
             *  Transaction responsible for the block producer (pre-version 9).
             *
             **/
            Transaction producer;


            /** System Script
             *
             *  The critical system level pre-states and post-states.
             *
             **/
            TAO::Register::Stream  ssSystem;


            /** The transaction history.
             *  uint8_t = TransactionType (per enum)
             *  uint512_t = Tx hash
             **/
            std::vector<std::pair<uint8_t, uint512_t> > vtx;


            /** vchPhysicalFalconSig
             *
             *  Optional Physical Falcon-1024 signature over this block.
             *  Present and parsed when nVersion >= FalconConstants::PHYSICAL_FALCON_BLOCK_VERSION.
             *  Consensus validation only enforced when FalconConstants::PHYSICAL_FALCON_ENFORCEMENT == true.
             *  The signature covers: hashPrevBlock + hashMerkleRoot + nTime + nNonce + nChannel + nHeight
             **/
            std::vector<uint8_t> vchPhysicalFalconSig;


            /** hashPhysicalFalconKeyID
             *
             *  SK256 hash of the Physical Falcon-1024 public key that produced vchPhysicalFalconSig.
             *  Used to look up the registered pubkey in the account/trust system.
             *  Present when nVersion >= FalconConstants::PHYSICAL_FALCON_BLOCK_VERSION and vchPhysicalFalconSig non-empty.
             **/
            uint256_t hashPhysicalFalconKeyID;


            /** GetSerializeSize **/
            uint64_t GetSerializeSize(uint32_t nSerType, uint32_t nSerVersion) const
            {
                uint64_t nSerSize = 0;
                nSerSize += ::GetSerializeSize(nVersion, nSerType, nSerVersion);
                nSerSize += ::GetSerializeSize(hashPrevBlock, nSerType, nSerVersion);
                nSerSize += ::GetSerializeSize(hashMerkleRoot, nSerType, nSerVersion);
                nSerSize += ::GetSerializeSize(nChannel, nSerType, nSerVersion);
                nSerSize += ::GetSerializeSize(nHeight, nSerType, nSerVersion);
                nSerSize += ::GetSerializeSize(nBits, nSerType, nSerVersion);
                nSerSize += ::GetSerializeSize(nNonce, nSerType, nSerVersion);
                nSerSize += ::GetSerializeSize(nTime, nSerType, nSerVersion);
                nSerSize += ::GetSerializeSize(vchBlockSig, nSerType, nSerVersion);
                nSerSize += ::GetSerializeSize(producer, nSerType, nSerVersion);
                nSerSize += ::GetSerializeSize(ssSystem, nSerType, nSerVersion);
                nSerSize += ::GetSerializeSize(vOffsets, nSerType, nSerVersion);
                nSerSize += ::GetSerializeSize(vtx, nSerType, nSerVersion);

                /* Physical Falcon signature field — version gated, idle until PHYSICAL_FALCON_ENFORCEMENT */
                if(nVersion >= LLP::FalconConstants::PHYSICAL_FALCON_BLOCK_VERSION)
                {
                    nSerSize += sizeof(uint16_t); // physiglen field (always present, 2 bytes)
                    if(!vchPhysicalFalconSig.empty())
                    {
                        nSerSize += ::GetSerializeSize(hashPhysicalFalconKeyID, nSerType, nSerVersion);
                        nSerSize += vchPhysicalFalconSig.size();
                    }
                }
                return nSerSize;
            }


            /** Serialize (write) **/
            template<typename Stream>
            void Serialize(Stream& s, uint32_t nSerType, uint32_t nSerVersion) const
            {
                ::Serialize(s, nVersion, nSerType, nSerVersion);
                ::Serialize(s, hashPrevBlock, nSerType, nSerVersion);
                ::Serialize(s, hashMerkleRoot, nSerType, nSerVersion);
                ::Serialize(s, nChannel, nSerType, nSerVersion);
                ::Serialize(s, nHeight, nSerType, nSerVersion);
                ::Serialize(s, nBits, nSerType, nSerVersion);
                ::Serialize(s, nNonce, nSerType, nSerVersion);
                ::Serialize(s, nTime, nSerType, nSerVersion);
                ::Serialize(s, vchBlockSig, nSerType, nSerVersion);
                ::Serialize(s, producer, nSerType, nSerVersion);
                ::Serialize(s, ssSystem, nSerType, nSerVersion);
                ::Serialize(s, vOffsets, nSerType, nSerVersion);
                ::Serialize(s, vtx, nSerType, nSerVersion);

                /* Physical Falcon signature field — version gated, idle until PHYSICAL_FALCON_ENFORCEMENT */
                if(nVersion >= LLP::FalconConstants::PHYSICAL_FALCON_BLOCK_VERSION)
                {
                    uint16_t nPhySigLen = static_cast<uint16_t>(vchPhysicalFalconSig.size());
                    WRITEDATA(s, nPhySigLen);
                    if(nPhySigLen > 0)
                    {
                        ::Serialize(s, hashPhysicalFalconKeyID, nSerType, nSerVersion);
                        s.write(reinterpret_cast<const char*>(vchPhysicalFalconSig.data()), nPhySigLen);
                    }
                }
            }


            /** Unserialize (read) **/
            template<typename Stream>
            void Unserialize(Stream& s, uint32_t nSerType, uint32_t nSerVersion)
            {
                ::Unserialize(s, nVersion, nSerType, nSerVersion);
                ::Unserialize(s, hashPrevBlock, nSerType, nSerVersion);
                ::Unserialize(s, hashMerkleRoot, nSerType, nSerVersion);
                ::Unserialize(s, nChannel, nSerType, nSerVersion);
                ::Unserialize(s, nHeight, nSerType, nSerVersion);
                ::Unserialize(s, nBits, nSerType, nSerVersion);
                ::Unserialize(s, nNonce, nSerType, nSerVersion);
                ::Unserialize(s, nTime, nSerType, nSerVersion);
                ::Unserialize(s, vchBlockSig, nSerType, nSerVersion);
                ::Unserialize(s, producer, nSerType, nSerVersion);
                ::Unserialize(s, ssSystem, nSerType, nSerVersion);
                ::Unserialize(s, vOffsets, nSerType, nSerVersion);
                ::Unserialize(s, vtx, nSerType, nSerVersion);

                /* Physical Falcon signature field — version gated */
                if(nVersion >= LLP::FalconConstants::PHYSICAL_FALCON_BLOCK_VERSION)
                {
                    uint16_t nPhySigLen = 0;
                    READDATA(s, nPhySigLen);
                    if(nPhySigLen > 0)
                    {
                        if(nPhySigLen < LLP::FalconConstants::FALCON_CT_SIG_SIZE_512 ||
                           nPhySigLen > LLP::FalconConstants::FALCON_CT_SIG_SIZE_1024)
                        {
                            throw std::runtime_error("Physical Falcon sig size out of range");
                        }
                        ::Unserialize(s, hashPhysicalFalconKeyID, nSerType, nSerVersion);
                        vchPhysicalFalconSig.resize(nPhySigLen);
                        s.read(reinterpret_cast<char*>(vchPhysicalFalconSig.data()), nPhySigLen);
                    }
                    else
                    {
                        vchPhysicalFalconSig.clear();
                        hashPhysicalFalconKeyID = uint256_t(0);
                    }
                }
            }


            /** The default constructor. **/
            TritiumBlock();


            /** Copy constructor. **/
            TritiumBlock(const TritiumBlock& block);


            /** Move constructor. **/
            TritiumBlock(TritiumBlock&& block) noexcept;


            /** Copy assignment. **/
            TritiumBlock& operator=(const TritiumBlock& block);


            /** Move assignment. **/
            TritiumBlock& operator=(TritiumBlock&& block) noexcept;


            /** Default Destructor **/
            virtual ~TritiumBlock();


            /** Copy Constructor. **/
            TritiumBlock(const Block& block);


            /** Copy Constructor. **/
            TritiumBlock(const BlockState& state);


            /** Copy Constructor. **/
            TritiumBlock(const SyncBlock& block);



            /** Clone
             *
             *  Allows polymorphic copying of blocks
             *  Overridden to return an instance of the TritiumBlock class.
             *  Return-type covariance allows us to return the more derived type whilst
             *  still overriding the virtual base-class method
             *
             *  @return A pointer to a copy of this TritiumBlock.
             *
             **/
            virtual TritiumBlock* Clone() const override;


            /** SetNull
             *
             *  Set the block to Null state.
             *
             **/
            void SetNull() override;


            /** UpdateTime
             *
             *  Update the blocks timestamp.
             *
             **/
            void UpdateTime();


            /** GetBlockTime
             *
             *  Returns the current UNIX timestamp of the block.
             *
             *  @return 64-bit integer of timestamp.
             *
             **/
            uint64_t GetBlockTime() const;


            /** Check
             *
             *  Check a tritium block for consistency.
             *
             **/
            bool Check() const override;


            /** Accept
             *
             *  Accept a tritium block with chain state parameters.
             *
             **/
            bool Accept() const override;


            /** CheckStake
             *
             *  Check the proof of stake calculations.
             *
             **/
            bool CheckStake() const;


            /** VerifyWork
             *
             *  Verify the work was completed by miners as advertised.
             *
             *  @return True if work is valid, false otherwise.
             *
             **/
            bool VerifyWork() const override;


            /** SignatureHash
             *
             *  Get the Signature Hash of the block. Used to verify work claims.
             *
             *  @return Returns a 1024-bit signature hash.
             *
             **/
            uint1024_t SignatureHash() const override;


            /** StakeHash
             *
             *  Prove that you staked a number of seconds based on weight
             *
             *  @return 1024-bit stake hash
             *
             **/
            uint1024_t StakeHash() const;


            /** ToString
             *
             *  For debugging Purposes seeing block state data dump
             *
             **/
            std::string ToString() const override;


        };
    }
}

#endif
