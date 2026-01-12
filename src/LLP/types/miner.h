/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People
____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_TYPES_MINER_H
#define NEXUS_LLP_TYPES_MINER_H

#include <LLP/templates/connection.h>
#include <LLP/include/llp_opcodes.h>
#include <TAO/Ledger/types/block.h>
#include <Legacy/types/coinbase.h>
#include <atomic>

//forward declarations
namespace Legacy { class ReserveKey; }

namespace LLP
{

    /** Miner
     *
     *  Connection class that handles requests and responses from miners.
     *
     *  PROTOCOL DESIGN OVERVIEW:
     *  
     *  This LLP implements the Miner Protocol for NexusMiner communication with the Nexus Core node.
     *  
     *  Key Design Principles:
     *  1. Backward Compatibility: Supports both legacy (4-byte) and new (1-byte) SET_CHANNEL payloads
     *     to ensure smooth transition during miner upgrades without protocol breakage.
     *  
     *  2. Stateless Operation:
     *     - Uses Falcon signatures for authentication (no TAO API session required)
     *     - Supports both localhost and remote mining connections
     *     - Immutable MiningContext for state management in dedicated StatelessMinerConnection
     *  
     *  3. Forward Compatibility: SESSION_START and SESSION_KEEPALIVE packet types are defined
     *     and acknowledged but not fully implemented. This allows future miners to use these
     *     packets without breaking existing nodes.
     *  
     *  4. Mining Channels:
     *     - Channel 1: Prime (Fermat prime number discovery)
     *     - Channel 2: Hash (traditional SHA3 hashing)
     *     - Channel 0: Reserved for Proof of Stake (not valid for mining)
     *  
     *  5. Authentication Flow (Stateless Mode):
     *     MINER_AUTH_INIT -> MINER_AUTH_CHALLENGE -> MINER_AUTH_RESPONSE -> MINER_AUTH_RESULT
     *     Uses Falcon post-quantum signatures for security.
     *  
     *  This design coordinates with the unified mining stack implementation across:
     *  - LLL-TAO (this node implementation)
     *  - NexusMiner (miner client)
     *  - NexusInterface (GUI management)
     *
     **/
    class Miner : public Connection
    {
    public:
        /* Protocol message opcodes from centralized LLP Opcodes Registry.
         * For documentation on each opcode, see LLP/include/llp_opcodes.h
         */
        static constexpr auto BLOCK_DATA             = Opcodes::Legacy::BLOCK_DATA;
        static constexpr auto SUBMIT_BLOCK           = Opcodes::Legacy::SUBMIT_BLOCK;
        static constexpr auto BLOCK_HEIGHT           = Opcodes::Legacy::BLOCK_HEIGHT;
        static constexpr auto SET_CHANNEL            = Opcodes::Legacy::SET_CHANNEL;
        static constexpr auto BLOCK_REWARD           = Opcodes::Legacy::BLOCK_REWARD;
        static constexpr auto SET_COINBASE           = Opcodes::Legacy::SET_COINBASE;
        static constexpr auto GOOD_BLOCK             = Opcodes::Legacy::GOOD_BLOCK;
        static constexpr auto ORPHAN_BLOCK           = Opcodes::Legacy::ORPHAN_BLOCK;
        static constexpr auto CHECK_BLOCK            = Opcodes::Legacy::CHECK_BLOCK;
        static constexpr auto SUBSCRIBE              = Opcodes::Legacy::SUBSCRIBE;
        static constexpr auto GET_BLOCK              = Opcodes::Legacy::GET_BLOCK;
        static constexpr auto GET_HEIGHT             = Opcodes::Legacy::GET_HEIGHT;
        static constexpr auto GET_REWARD             = Opcodes::Legacy::GET_REWARD;
        static constexpr auto CLEAR_MAP              = Opcodes::Legacy::CLEAR_MAP;
        static constexpr auto GET_ROUND              = Opcodes::Legacy::GET_ROUND;
        static constexpr auto BLOCK_ACCEPTED         = Opcodes::Legacy::BLOCK_ACCEPTED;
        static constexpr auto BLOCK_REJECTED         = Opcodes::Legacy::BLOCK_REJECTED;
        static constexpr auto COINBASE_SET           = Opcodes::Legacy::COINBASE_SET;
        static constexpr auto COINBASE_FAIL          = Opcodes::Legacy::COINBASE_FAIL;
        static constexpr auto NEW_ROUND              = Opcodes::Legacy::NEW_ROUND;
        static constexpr auto OLD_ROUND              = Opcodes::Legacy::OLD_ROUND;
        static constexpr auto CHANNEL_ACK            = Opcodes::Legacy::CHANNEL_ACK;
        static constexpr auto MINER_AUTH_INIT        = Opcodes::Legacy::MINER_AUTH_INIT;
        static constexpr auto MINER_AUTH_CHALLENGE   = Opcodes::Legacy::MINER_AUTH_CHALLENGE;
        static constexpr auto MINER_AUTH_RESPONSE    = Opcodes::Legacy::MINER_AUTH_RESPONSE;
        static constexpr auto MINER_AUTH_RESULT      = Opcodes::Legacy::MINER_AUTH_RESULT;
        static constexpr auto SESSION_START          = Opcodes::Legacy::SESSION_START;
        static constexpr auto SESSION_KEEPALIVE      = Opcodes::Legacy::SESSION_KEEPALIVE;
        static constexpr auto MINER_SET_REWARD       = Opcodes::Legacy::MINER_SET_REWARD;
        static constexpr auto MINER_REWARD_RESULT    = Opcodes::Legacy::MINER_REWARD_RESULT;
        static constexpr auto MINER_READY            = Opcodes::Legacy::MINER_READY;
        static constexpr auto PRIME_BLOCK_AVAILABLE  = Opcodes::Legacy::PRIME_BLOCK_AVAILABLE;
        static constexpr auto HASH_BLOCK_AVAILABLE   = Opcodes::Legacy::HASH_BLOCK_AVAILABLE;
        static constexpr auto PING                   = Opcodes::Legacy::PING;
        static constexpr auto CLOSE                  = Opcodes::Legacy::CLOSE;

    private:

        /* Externally set coinbase to be set on mined blocks */
        Legacy::Coinbase tCoinbaseTx;


        /** Mutex for thread safe data access **/
        std::mutex MUTEX;


        /** The map to hold the list of blocks that are being mined. */
        std::map<uint512_t, TAO::Ledger::Block *> mapBlocks;


        /** The current best block. **/
        std::atomic<uint32_t> nBestHeight;


        /* Subscribe to display how many blocks connection subscribed to */
        std::atomic<uint32_t> nSubscribed;


        /* The current channel mining for. */
        std::atomic<uint32_t> nChannel;


        /* The last txid on user's signature chain. Used to orphan blocks if another transaction is made while mining. */
        uint512_t nHashLast;


        /* The last height that the notifications processor was run at.  This is used to ensure that events are only processed once
           across all threads when the height changes */
        static std::atomic<uint32_t> nLastNotificationsHeight;


        /** Used as an ID iterator for generating unique hashes from same block transactions. **/
        static std::atomic<uint32_t> nBlockIterator;


        /* Authentication state for stateless miners using Falcon keys */
        std::vector<uint8_t> vMinerPubKey;       // Falcon public key received from miner
        std::string          strMinerId;         // Miner label or ID from payload
        std::vector<uint8_t> vAuthNonce;         // Random challenge nonce generated by node
        bool                 fMinerAuthenticated; // Whether auth succeeded
        uint256_t            hashGenesis;        // Miner's genesis (for Falcon auth)
        uint32_t             nSessionId = 0;     // Session ID derived from Falcon key hash

        /* ChaCha20 encryption state (established after Falcon auth) */
        std::vector<uint8_t> vChaChaKey;         // ChaCha20 session key
        bool                 fEncryptionReady;   // ChaCha20 encryption established

        /* Reward address binding (can be different from genesis!) */
        uint256_t            hashRewardAddress;  // Where to send mining rewards
        bool                 fRewardBound;       // True after successful MINER_SET_REWARD

    public:

        /** Default Constructor **/
        Miner();


        /** Constructor **/
        Miner(const Socket& SOCKET_IN, DDOS_Filter* DDOS_IN, bool fDDOSIn = false);


        /** Constructor **/
        Miner(DDOS_Filter* DDOS_IN, bool fDDOSIn = false);


        /** Default Destructor **/
        ~Miner();


        /** Name
         *
         *  Returns a string for the name of this type of Node.
         *
         **/
         static std::string Name()
         {
             return "Miner";
         }


        /** Event
         *
         *  Virtual Functions to Determine Behavior of Message LLP.
         *
         *  @param[in] EVENT The byte header of the event type.
         *  @param[in[ LENGTH The size of bytes read on packet read events.
         *
         */
        void Event(uint8_t EVENT, uint32_t LENGTH = 0) final;


        /** ProcessPacket
         *
         *  Main message handler once a packet is recieved.
         *
         *  @return True is no errors, false otherwise.
         *
         **/
        bool ProcessPacket() final;


        /** ProcessPacketStateless
         *
         *  Handles packets from stateless miners.
         *
         *  @param[in] PACKET The packet to process.
         *
         *  @return True if no errors, false otherwise.
         *
         **/
        bool ProcessPacketStateless(const Packet& PACKET);


    private:

        /** respond
         *
         *  Sends a packet response.
         *
         *  @param[in] header_response The header message to send.
         *  @param[in] data The packet data to send.
         *
         **/
        void respond(uint8_t nHeader, const std::vector<uint8_t>& vData = std::vector<uint8_t>());


        /** check_best_height
         *
         *  Checks the current height index and updates best height. Clears the block map if the height is outdated.
         *
         *  @return Returns true if best height was outdated, false otherwise.
         *
         **/
        bool check_best_height();


        /** check_round
         *
         *  For Tritium, this checks the mempool to make sure that there are no new transactions that would be orphaned by the
         *  the current round block.
         *
         *  @return Returns false if the current round is no longer valid.
         *
         **/
        bool check_round();


        /** clear_map
         *
         *  Clear the blocks map.
         *
         **/
        void clear_map();


        /** find_block
         *
         *  Determines if the block exists.
         *
         **/
        bool find_block(const uint512_t& hashMerkleRoot);


        /** new_block
         *
         *  Adds a new block to the map.
         *
         **/
        TAO::Ledger::Block *new_block();


        /** validate_block
         *
         *  validates the block for the derived miner class.
         *
         *  @param[in] hashMerkleRoot The root hash of the merkle tree.
         *
         *  @return Returns true if block is valid, false otherwise.
         *
         **/
        bool validate_block(const uint512_t& hashMerkleRoot);


        /** sign_block
         *
         *  signs the block to seal the proof of work.
         *
         *  @param[in] nNonce The nonce secret for the block proof.
         *  @param[in] hashMerkleRoot The root hash of the merkle tree.
         *
         *  @return Returns true if block is valid, false otherwise.
         *
         **/
        bool sign_block(uint64_t nNonce, const uint512_t& hashMerkleRoot);


        /** is_locked
         *
         *  Returns true if the mining wallet locked, false otherwise.
         *
         **/
        bool is_locked();


        /** is_prime_mod
         *
         *  Helper function used for prime channel modification rule in loop.
         *  Returns true if the condition is satisfied, false otherwise.
         *
         *  @param[in] nBitMask The bitMask for the highest order bits of a block hash to check for to satisfy rule.
         *  @param[in] pBlock The block to check.
         *
         **/
        bool is_prime_mod(uint32_t nBitMask, TAO::Ledger::Block *pBlock);

    };
}

#endif
