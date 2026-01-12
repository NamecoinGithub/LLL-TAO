/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_INCLUDE_LLP_OPCODES_H
#define NEXUS_LLP_INCLUDE_LLP_OPCODES_H

#include <cstdint>
#include <string>

/** LLP Opcodes Registry
 *
 *  Central registry for all Lower Level Protocol (LLP) opcodes used across the Nexus network.
 *  This file serves as the single source of truth for protocol message types, preventing
 *  opcode conflicts and making the protocol architecture self-documenting.
 *
 *  ARCHITECTURE:
 *  =============
 *  
 *  16-Bit Opcode Space:
 *  -------------------
 *  Nexus uses 16-bit opcodes (0x0000 - 0xFFFF = 65,536 possible codes) divided into
 *  protocol-specific ranges to prevent conflicts. Each protocol gets 4096 codes.
 *  
 *  Allocation Strategy:
 *  -------------------
 *  0x0000 - 0x0FFF:  Legacy protocols (pool mining, etc.)
 *  0x1000 - 0x1FFF:  Tritium node protocol (blockchain sync, etc.)
 *  0x2000 - 0x2FFF:  Time synchronization protocol
 *  0xD000 - 0xDFFF:  Stateless mining protocol (NEW in this PR)
 *  0xE000 - 0xEFFF:  Pool mining protocol (future expansion)
 *  0xF000 - 0xFFFF:  Reserved for future protocols
 *  
 *  Why This Matters:
 *  -----------------
 *  - Prevents accidental opcode reuse across protocols
 *  - Makes protocol boundaries clear at a glance
 *  - Self-documents the entire LLP architecture
 *  - Easy to add new protocols without conflicts
 *  - Simplifies debugging (opcode tells you which protocol)
 *  
 *  USAGE:
 *  ======
 *  
 *  Creating Packets:
 *  ----------------
 *  Packet packet(Opcodes::StatelessMining::MINER_AUTH);
 *  WritePacket(packet);
 *  
 *  Processing Packets:
 *  ------------------
 *  switch(PACKET.HEADER) {
 *      case Opcodes::StatelessMining::MINER_AUTH:
 *          return ProcessAuth(packet);
 *      case Opcodes::StatelessMining::GET_BLOCK:
 *          return ProcessGetBlock(packet);
 *  }
 *  
 *  Debugging:
 *  ---------
 *  debug::log(0, "Received ", GetOpcodeName(PACKET.HEADER));
 *  debug::log(0, "Protocol: ", GetProtocolName(PACKET.HEADER));
 *  
 **/

namespace LLP
{
    namespace Opcodes
    {
        /** Legacy Mining Protocol (0x0000 - 0x0FFF)
         *  
         *  Original pool mining protocol used before Tritium upgrade.
         *  Still supported for backward compatibility with older miners.
         **/
        namespace Legacy
        {
            /* Block management */
            constexpr uint16_t SUBMIT_BLOCK        = 0x0001;  // Submit mined block
            constexpr uint16_t BLOCK_DATA          = 0x0000;  // Block template data
            constexpr uint16_t BLOCK_HEIGHT        = 0x0002;  // Current block height
            constexpr uint16_t BLOCK_REWARD        = 0x0004;  // Mining reward amount
            constexpr uint16_t BLOCK_ACCEPTED      = 0x00C8;  // Block accepted (200)
            constexpr uint16_t BLOCK_REJECTED      = 0x00C9;  // Block rejected (201)
            
            /* Template requests */
            constexpr uint16_t GET_BLOCK           = 0x0081;  // Request block template (129)
            constexpr uint16_t GET_HEIGHT          = 0x0082;  // Request blockchain height (130)
            constexpr uint16_t GET_REWARD          = 0x0083;  // Request mining reward (131)
            constexpr uint16_t GET_ROUND           = 0x0085;  // Request mining round info (133)
            
            /* Round management */
            constexpr uint16_t NEW_ROUND           = 0x00CC;  // New mining round started (204)
            constexpr uint16_t OLD_ROUND           = 0x00CD;  // Mining round expired (205)
            
            /* Push notifications */
            constexpr uint16_t MINER_READY         = 0x00D8;  // Miner ready for updates (216)
            constexpr uint16_t PRIME_BLOCK_AVAILABLE = 0x00D9; // Prime block available (217)
            constexpr uint16_t HASH_BLOCK_AVAILABLE  = 0x00DA; // Hash block available (218)
        }

        /** Tritium Node Protocol (0x1000 - 0x1FFF)
         *  
         *  Blockchain synchronization and peer-to-peer communication.
         *  Used for block propagation, transaction relay, and chain consensus.
         **/
        namespace Tritium
        {
            /* Blockchain sync */
            constexpr uint16_t GET_BLOCKS          = 0x1000;  // Request block range
            constexpr uint16_t BLOCK               = 0x1001;  // Block data
            constexpr uint16_t GET_HEADERS         = 0x1002;  // Request block headers
            constexpr uint16_t HEADERS             = 0x1003;  // Block headers
            
            /* Transaction relay */
            constexpr uint16_t TX                  = 0x1010;  // Transaction broadcast
            constexpr uint16_t GET_TX              = 0x1011;  // Request transaction
            constexpr uint16_t INV                 = 0x1012;  // Inventory announcement
            constexpr uint16_t GET_DATA            = 0x1013;  // Request inventory data
            
            /* Peer management */
            constexpr uint16_t PING                = 0x1020;  // Keepalive ping
            constexpr uint16_t PONG                = 0x1021;  // Keepalive pong
            constexpr uint16_t VERSION             = 0x1022;  // Peer version info
            constexpr uint16_t VERACK              = 0x1023;  // Version acknowledged
            constexpr uint16_t GET_ADDR            = 0x1024;  // Request peer addresses
            constexpr uint16_t ADDR                = 0x1025;  // Peer addresses
        }

        /** Time Synchronization Protocol (0x2000 - 0x2FFF)
         *  
         *  Network time protocol for unified timestamp consensus.
         *  Ensures all nodes agree on current time for block validation.
         **/
        namespace TimeSynchronization
        {
            constexpr uint16_t TIME_REQUEST        = 0x2000;  // Request time from peers
            constexpr uint16_t TIME_RESPONSE       = 0x2001;  // Time sync response
            constexpr uint16_t TIME_OFFSET         = 0x2002;  // Time offset adjustment
        }

        /** Stateless Mining Protocol (0xD000 - 0xDFFF)
         *  
         *  Phase 2 stateless mining with Falcon-1024 authentication.
         *  Miners authenticate with quantum-resistant Falcon signatures and receive
         *  wallet-signed block templates. No blockchain required on miner side.
         *  
         *  PROTOCOL FLOW:
         *  =============
         *  
         *  Phase 1: Authentication
         *  -----------------------
         *  Miner -> Node:   MINER_AUTH (0xD000)          [Falcon public key + signature]
         *  Node -> Miner:   MINER_AUTH_RESPONSE (0xD001) [Session ID + ChaCha20 key]
         *  
         *  Phase 2: Configuration
         *  ----------------------
         *  Miner -> Node:   MINER_SET_REWARD (0xD003)    [Reward address]
         *  Node -> Miner:   MINER_REWARD_RESULT (0xD004) [Success/failure]
         *  Miner -> Node:   SET_CHANNEL (0xD005)         [Prime=1 or Hash=2]
         *  Node -> Miner:   CHANNEL_ACK (0xD006)         [Success/failure]
         *  
         *  Phase 3: Subscription (216-byte templates)
         *  ---------------------
         *  Miner -> Node:   MINER_READY (0xD007)         [Subscribe to templates]
         *  Node -> Miner:   MINER_READY (0xD007)         [1-byte acknowledgment]
         *  Node -> Miner:   GET_BLOCK (0xD008)           [Initial 216-byte template] ← FIX!
         *  
         *  Phase 4: Mining (216-byte templates)
         *  ---------------
         *  [Miner works on template...]
         *  [Network event: New block found]
         *  Node -> Miner:   NEW_BLOCK (0xD009)           [Updated 216-byte template] ← FIX!
         *  [Miner abandons old work, starts new template]
         *  
         *  Phase 5: Solution Submission
         *  ----------------------------
         *  [Miner finds valid nonce]
         *  Miner -> Node:   SUBMIT_BLOCK (0xD00A)        [Signed block solution]
         *  Node -> Miner:   BLOCK_ACCEPTED (0xD00B)      [Success]
         *    OR
         *  Node -> Miner:   BLOCK_REJECTED (0xD00C)      [Failure + reason]
         *  Node -> All:     NEW_BLOCK (0xD009)           [Next template to all miners]
         *  
         *  TEMPLATE FORMAT (216 bytes):
         *  ===========================
         *  Serialized TritiumBlock (wallet-signed, ready to mine)
         *  
         *  Note: Earlier documentation mentioned 228 bytes (12-byte metadata + 216-byte block).
         *  The actual implementation uses 216 bytes only. Metadata (height, difficulty) can
         *  be derived from the block itself or sent separately via notifications.
         *  
         *  SECURITY:
         *  =========
         *  - Falcon-1024 post-quantum authentication
         *  - ChaCha20-Poly1305 transport encryption
         *  - Session-based key binding
         *  - Replay attack prevention via nonces
         *  
         *  COMPATIBILITY:
         *  ==============
         *  - Backward compatible with legacy GET_BLOCK (0x0081)
         *  - Miners detect protocol version from auth response
         *  - Nodes support both legacy and stateless miners
         *  
         **/
        namespace StatelessMining
        {
            /* Authentication */
            constexpr uint16_t MINER_AUTH          = 0xD000;  // Falcon authentication
            constexpr uint16_t MINER_AUTH_RESPONSE = 0xD001;  // Auth result + session key
            constexpr uint16_t MINER_AUTH_INIT     = 0xD002;  // Auth init (legacy alias)
            
            /* Configuration */
            constexpr uint16_t MINER_SET_REWARD    = 0xD003;  // Set reward address
            constexpr uint16_t MINER_REWARD_RESULT = 0xD004;  // Reward confirmed
            constexpr uint16_t SET_CHANNEL         = 0xD005;  // Select Prime/Hash
            constexpr uint16_t CHANNEL_ACK         = 0xD006;  // Channel confirmed
            
            /* Template delivery (CRITICAL FIX in this PR) */
            constexpr uint16_t MINER_READY         = 0xD007;  // Subscribe to templates
            constexpr uint16_t GET_BLOCK           = 0xD008;  // Initial template (216 bytes) ← NEW!
            constexpr uint16_t NEW_BLOCK           = 0xD009;  // Updated template (216 bytes) ← NEW!
            
            /* Solution submission */
            constexpr uint16_t SUBMIT_BLOCK        = 0xD00A;  // Submit solution (216 bytes + sig)
            constexpr uint16_t BLOCK_ACCEPTED      = 0xD00B;  // Block accepted
            constexpr uint16_t BLOCK_REJECTED      = 0xD00C;  // Block rejected + reason
            
            /* Status queries */
            constexpr uint16_t GET_STATS           = 0xD010;  // Request mining statistics
            constexpr uint16_t STATS_RESPONSE      = 0xD011;  // Statistics data
            constexpr uint16_t GET_ROUND           = 0xD012;  // Request round info
            constexpr uint16_t ROUND_INFO          = 0xD013;  // Round metadata
        }

        /** Pool Mining Protocol (0xE000 - 0xEFFF)
         *  
         *  Reserved for future pool mining enhancements.
         *  Will support stratumv2-style work distribution with Falcon auth.
         **/
        namespace PoolMining
        {
            /* Placeholder - to be defined when pool protocol is implemented */
            constexpr uint16_t RESERVED            = 0xE000;
        }

    } // namespace Opcodes

    /** GetOpcodeName
     *  
     *  Convert opcode to human-readable name for debugging.
     *  
     *  @param nOpcode The 16-bit opcode value
     *  @return String description of the opcode
     *  
     *  Example:
     *    debug::log(0, "Received ", GetOpcodeName(0xD000));
     *    // Output: "Received MINER_AUTH"
     **/
    inline std::string GetOpcodeName(uint16_t nOpcode)
    {
        /* Legacy protocol (0x0000 - 0x0FFF) */
        if (nOpcode <= 0x0FFF) {
            switch(nOpcode) {
                case Opcodes::Legacy::SUBMIT_BLOCK:        return "SUBMIT_BLOCK";
                case Opcodes::Legacy::BLOCK_DATA:          return "BLOCK_DATA";
                case Opcodes::Legacy::BLOCK_HEIGHT:        return "BLOCK_HEIGHT";
                case Opcodes::Legacy::BLOCK_REWARD:        return "BLOCK_REWARD";
                case Opcodes::Legacy::GET_BLOCK:           return "GET_BLOCK";
                case Opcodes::Legacy::GET_HEIGHT:          return "GET_HEIGHT";
                case Opcodes::Legacy::GET_REWARD:          return "GET_REWARD";
                case Opcodes::Legacy::GET_ROUND:           return "GET_ROUND";
                case Opcodes::Legacy::BLOCK_ACCEPTED:      return "BLOCK_ACCEPTED";
                case Opcodes::Legacy::BLOCK_REJECTED:      return "BLOCK_REJECTED";
                case Opcodes::Legacy::NEW_ROUND:           return "NEW_ROUND";
                case Opcodes::Legacy::OLD_ROUND:           return "OLD_ROUND";
                case Opcodes::Legacy::MINER_READY:         return "MINER_READY";
                case Opcodes::Legacy::PRIME_BLOCK_AVAILABLE: return "PRIME_BLOCK_AVAILABLE";
                case Opcodes::Legacy::HASH_BLOCK_AVAILABLE:  return "HASH_BLOCK_AVAILABLE";
                default: return "LEGACY_UNKNOWN";
            }
        }
        
        /* Tritium protocol (0x1000 - 0x1FFF) */
        if (nOpcode >= 0x1000 && nOpcode <= 0x1FFF) {
            switch(nOpcode) {
                case Opcodes::Tritium::GET_BLOCKS:         return "GET_BLOCKS";
                case Opcodes::Tritium::BLOCK:              return "BLOCK";
                case Opcodes::Tritium::GET_HEADERS:        return "GET_HEADERS";
                case Opcodes::Tritium::HEADERS:            return "HEADERS";
                case Opcodes::Tritium::TX:                 return "TX";
                case Opcodes::Tritium::GET_TX:             return "GET_TX";
                case Opcodes::Tritium::INV:                return "INV";
                case Opcodes::Tritium::GET_DATA:           return "GET_DATA";
                case Opcodes::Tritium::PING:               return "PING";
                case Opcodes::Tritium::PONG:               return "PONG";
                case Opcodes::Tritium::VERSION:            return "VERSION";
                case Opcodes::Tritium::VERACK:             return "VERACK";
                case Opcodes::Tritium::GET_ADDR:           return "GET_ADDR";
                case Opcodes::Tritium::ADDR:               return "ADDR";
                default: return "TRITIUM_UNKNOWN";
            }
        }
        
        /* Time sync protocol (0x2000 - 0x2FFF) */
        if (nOpcode >= 0x2000 && nOpcode <= 0x2FFF) {
            switch(nOpcode) {
                case Opcodes::TimeSynchronization::TIME_REQUEST:  return "TIME_REQUEST";
                case Opcodes::TimeSynchronization::TIME_RESPONSE: return "TIME_RESPONSE";
                case Opcodes::TimeSynchronization::TIME_OFFSET:   return "TIME_OFFSET";
                default: return "TIMESYNC_UNKNOWN";
            }
        }
        
        /* Stateless mining protocol (0xD000 - 0xDFFF) */
        if (nOpcode >= 0xD000 && nOpcode <= 0xDFFF) {
            switch(nOpcode) {
                case Opcodes::StatelessMining::MINER_AUTH:          return "MINER_AUTH";
                case Opcodes::StatelessMining::MINER_AUTH_RESPONSE: return "MINER_AUTH_RESPONSE";
                case Opcodes::StatelessMining::MINER_AUTH_INIT:     return "MINER_AUTH_INIT";
                case Opcodes::StatelessMining::MINER_SET_REWARD:    return "MINER_SET_REWARD";
                case Opcodes::StatelessMining::MINER_REWARD_RESULT: return "MINER_REWARD_RESULT";
                case Opcodes::StatelessMining::SET_CHANNEL:         return "SET_CHANNEL";
                case Opcodes::StatelessMining::CHANNEL_ACK:         return "CHANNEL_ACK";
                case Opcodes::StatelessMining::MINER_READY:         return "MINER_READY";
                case Opcodes::StatelessMining::GET_BLOCK:           return "GET_BLOCK";
                case Opcodes::StatelessMining::NEW_BLOCK:           return "NEW_BLOCK";
                case Opcodes::StatelessMining::SUBMIT_BLOCK:        return "SUBMIT_BLOCK";
                case Opcodes::StatelessMining::BLOCK_ACCEPTED:      return "BLOCK_ACCEPTED";
                case Opcodes::StatelessMining::BLOCK_REJECTED:      return "BLOCK_REJECTED";
                case Opcodes::StatelessMining::GET_STATS:           return "GET_STATS";
                case Opcodes::StatelessMining::STATS_RESPONSE:      return "STATS_RESPONSE";
                case Opcodes::StatelessMining::GET_ROUND:           return "GET_ROUND";
                case Opcodes::StatelessMining::ROUND_INFO:          return "ROUND_INFO";
                default: return "STATELESS_UNKNOWN";
            }
        }
        
        /* Pool mining protocol (0xE000 - 0xEFFF) */
        if (nOpcode >= 0xE000 && nOpcode <= 0xEFFF) {
            return "POOL_RESERVED";
        }
        
        /* Reserved/unknown */
        return "UNKNOWN";
    }

    /** GetProtocolName
     *  
     *  Determine which protocol an opcode belongs to.
     *  
     *  @param nOpcode The 16-bit opcode value
     *  @return Protocol name string
     *  
     *  Example:
     *    debug::log(0, "Protocol: ", GetProtocolName(0xD000));
     *    // Output: "Protocol: StatelessMining"
     **/
    inline std::string GetProtocolName(uint16_t nOpcode)
    {
        if (nOpcode <= 0x0FFF)
            return "Legacy";
        if (nOpcode >= 0x1000 && nOpcode <= 0x1FFF)
            return "Tritium";
        if (nOpcode >= 0x2000 && nOpcode <= 0x2FFF)
            return "TimeSynchronization";
        if (nOpcode >= 0xD000 && nOpcode <= 0xDFFF)
            return "StatelessMining";
        if (nOpcode >= 0xE000 && nOpcode <= 0xEFFF)
            return "PoolMining";
        if (nOpcode >= 0xF000)
            return "Reserved";
        
        return "Unknown";
    }

} // namespace LLP

#endif // NEXUS_LLP_INCLUDE_LLP_OPCODES_H
