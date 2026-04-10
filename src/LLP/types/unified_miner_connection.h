/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_TYPES_UNIFIED_MINER_CONNECTION_H
#define NEXUS_LLP_TYPES_UNIFIED_MINER_CONNECTION_H

#include <LLP/templates/base_connection.h>
#include <LLP/packets/stateless_packet.h>
#include <LLP/include/graceful_shutdown.h>
#include <LLP/include/stateless_miner.h>
#include <LLP/include/auto_cooldown.h>
#include <LLP/include/get_block_policy.h>
#include <LLP/include/mining_constants.h>
#include <TAO/Ledger/types/block.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <map>
#include <memory>
#include <vector>

namespace LLP
{
    /** UnifiedMinerConnection
     *
     *  Unified mining connection that handles BOTH stateless (16-bit, port 9323)
     *  and legacy (8-bit, port 8323) mining protocols from a single Server instance.
     *
     *  DESIGN:
     *  =======
     *  - Inherits from BaseConnection<StatelessPacket> (16-bit header superset)
     *  - Protocol lane (STATELESS vs LEGACY) is determined at EVENTS::CONNECT based
     *    on which listen port accepted the connection
     *  - ReadPacket() reads 1-byte headers for LEGACY, 2-byte for STATELESS
     *  - DrainOutgoingQueue() writes 5-byte frames for LEGACY, 6-byte for STATELESS
     *  - ProcessPacket() dispatches by opcode range:
     *      Legacy: 0x0000-0x00FF (zero-extended 8-bit)
     *      Stateless: 0xD000-0xD0FF
     *
     *  GATING:
     *  =======
     *  This class is only instantiated when -unifiedmining=1 (off by default).
     *  The legacy Miner and StatelessMinerConnection classes remain functional
     *  when -unifiedmining is not set.
     *
     **/
    class UnifiedMinerConnection : public BaseConnection<StatelessPacket>
    {
    public:

        /** ProtocolLane
         *
         *  Immutable after EVENTS::CONNECT. Determines packet framing and opcode range.
         *
         **/
        enum class ProtocolLane
        {
            STATELESS,  // 16-bit framing, port 9323/9325
            LEGACY      // 8-bit framing, port 8323/8325
        };


        /** Default Constructor **/
        UnifiedMinerConnection();


        /** Constructor
         *
         *  @param[in] SOCKET_IN  The socket for the connection.
         *  @param[in] DDOS_IN    The DDOS filter for the connection.
         *  @param[in] fDDOSIn    Whether DDOS is enabled.
         *  @param[in] nListenPort The port that accepted this connection (for lane detection).
         *
         **/
        UnifiedMinerConnection(const Socket& SOCKET_IN, DDOS_Filter* DDOS_IN, bool fDDOSIn = false,
                               uint16_t nListenPort = 0);


        /** Constructor **/
        UnifiedMinerConnection(DDOS_Filter* DDOS_IN, bool fDDOSIn = false);


        /** Default Destructor **/
        ~UnifiedMinerConnection();


        /** Name
         *
         *  Returns a string for the name of this type of Node.
         *
         **/
        static std::string Name()
        {
            return "UnifiedMiner";
        }


        /** Event
         *
         *  Virtual Functions to Determine Behavior of Message LLP.
         *
         *  @param[in] EVENT The byte header of the event type.
         *  @param[in] LENGTH The size of bytes read on packet read events.
         *
         **/
        void Event(uint8_t EVENT, uint32_t LENGTH = 0) final;


        /** ProcessPacket
         *
         *  Main message handler once a packet is received.
         *
         *  @return True if no errors, false otherwise.
         *
         **/
        bool ProcessPacket() final;


        /** ReadPacket
         *
         *  Dual-mode framing: reads 1-byte header for LEGACY, 2-byte for STATELESS.
         *  LENGTH and DATA reading are identical for both lanes.
         *
         **/
        void ReadPacket() final;


        /** IsTimeoutExempt
         *
         *  Authenticated mining connections bypass aggressive POLL_EMPTY and
         *  TIMEOUT_WRITE checks.
         *
         *  @return true if miner is authenticated or in auth handshake.
         *
         **/
        bool IsTimeoutExempt() const final;


        /** GetReadTimeout
         *
         *  Authenticated miners use a long but finite read-idle timeout.
         *
         *  @return read-idle timeout in milliseconds, or 0 for default.
         *
         **/
        uint32_t GetReadTimeout() const final;


        /** GetWriteTimeout
         *
         *  Authenticated miners use a longer write-stall timeout.
         *
         *  @return write-stall timeout in milliseconds.
         *
         **/
        uint32_t GetWriteTimeout() const final;


        /** GetMaxSendBuffer
         *
         *  Authenticated miners use a larger send buffer.
         *
         *  @return maximum send buffer size in bytes for this connection.
         *
         **/
        uint64_t GetMaxSendBuffer() const final;


        /** GetContext
         *
         *  Get the current mining context (for server-level operations like notifications).
         *
         *  @return Copy of the current mining context.
         *
         **/
        MiningContext GetContext();


        /** SendChannelNotification
         *
         *  Send a channel-specific push notification to this miner.
         *  Uses the correct framing (8-bit or 16-bit) based on the protocol lane.
         *
         **/
        void SendChannelNotification();


        /** SendNodeShutdown
         *
         *  Send a NODE_SHUTDOWN packet to notify the miner of graceful shutdown.
         *
         *  @param[in] nReasonCode  Shutdown reason: 1=GRACEFUL, 2=MAINTENANCE
         *
         **/
        void SendNodeShutdown(uint32_t nReasonCode = GracefulShutdown::REASON_GRACEFUL);


        /** Check whether NODE_SHUTDOWN was already attempted on this connection. **/
        bool NodeShutdownSent() const
        {
            return m_nodeShutdownNotification.Sent();
        }


        /** GetLane
         *
         *  Returns the protocol lane for this connection.
         *
         *  @return The protocol lane (STATELESS or LEGACY).
         *
         **/
        ProtocolLane GetLane() const { return m_lane; }


        /** DrainOutgoingQueue
         *
         *  Lane-aware packet drain — writes 5-byte frames for LEGACY lane,
         *  6-byte frames for STATELESS lane.  Name-hides the base class
         *  version so DataThread<UnifiedMinerConnection>'s FLUSH_THREAD
         *  calls this version via C++ name resolution.
         *
         *  @return the number of packets drained.
         *
         **/
        uint32_t DrainOutgoingQueue();


        /* Difficulty cache (shared across all connections, per channel) */
        static std::atomic<uint64_t> nDiffCacheTime;
        struct alignas(64) PaddedDifficultyCache {
            std::atomic<uint32_t> nDifficulty{0};
        };
        static PaddedDifficultyCache nDiffCacheValue[3];

        /** GetCachedDifficulty
         *
         *  Get difficulty with 1-second TTL cache.
         *
         *  @param[in] nChannel Mining channel (0=PoS, 1=Prime, 2=Hash)
         *  @return Target difficulty bits for the channel.
         *
         **/
        static uint32_t GetCachedDifficulty(uint32_t nChannel);


    private:

        /** Protocol lane — set once at EVENTS::CONNECT, immutable thereafter. **/
        ProtocolLane m_lane;


        /** The port that accepted this connection (for lane detection). **/
        uint16_t m_nAcceptPort;


        /** The current mining context (immutable snapshot). **/
        MiningContext context;


        /** Mutex for thread-safe context updates. **/
        std::mutex MUTEX;


        /** Atomic mirror of context.fAuthenticated for lock-free reads. **/
        std::atomic<bool> fAuthenticatedAtomic{false};


        /** Tracks a Falcon handshake in progress. **/
        std::atomic<bool> fHandshakeInProgressAtomic{false};


        /** The map to hold the list of blocks being mined. **/
        std::map<uint512_t, TemplateMetadata> mapBlocks;


        /** Used as an ID iterator for generating unique hashes. **/
        static std::atomic<uint32_t> nBlockIterator;


        /** Map of session ID -> Falcon public key. **/
        std::map<uint32_t, std::vector<uint8_t>> mapSessionKeys;


        /** Mutex for thread-safe session key access. **/
        mutable std::mutex SESSION_MUTEX;


        /** Per-session template creation coordination. **/
        std::mutex TEMPLATE_CREATE_MUTEX;
        std::condition_variable TEMPLATE_CREATE_CV;
        bool m_template_create_in_flight{false};
        TAO::Ledger::Block* m_last_created_template{nullptr};


        /** Push throttle timestamp. **/
        std::chrono::steady_clock::time_point m_last_template_push_time;


        /** Force immediate push on next notification. **/
        bool m_force_next_push{false};


        /** GET_BLOCK fallback polling cooldown. **/
        AutoCoolDown m_get_block_cooldown{std::chrono::seconds(MiningConstants::GET_BLOCK_COOLDOWN_SECONDS)};


        /** Per-connection GET_BLOCK rolling rate limiter. **/
        GetBlockRollingLimiter m_getBlockRateLimiter;


        /** Track whether NODE_SHUTDOWN was sent. **/
        GracefulShutdown::NotificationState m_nodeShutdownNotification;


        /** IsStatelessPort
         *
         *  Helper to determine if a port corresponds to the stateless mining protocol.
         *
         *  @param[in] nPort The port to check.
         *  @return true if stateless protocol port, false if legacy.
         *
         **/
        static bool IsStatelessPort(uint16_t nPort);


        /** respond
         *
         *  Send a packet response using the correct framing for this lane.
         *
         *  @param[in] packet The stateless packet to send.
         *
         **/
        void respond(const StatelessPacket& packet);


        /** WriteLegacyFrame
         *
         *  Write a packet using legacy 5-byte framing: [uint8_t header][uint32_t length][data]
         *
         *  @param[in] packet The stateless packet to write in legacy framing.
         *
         **/
        void WriteLegacyFrame(const StatelessPacket& packet);


        /** new_block
         *
         *  Creates a new block template and adds it to mapBlocks.
         *
         *  @return Pointer to newly created block, or nullptr on failure.
         *
         **/
        TAO::Ledger::Block* new_block();


        /** find_block
         *
         *  Determines if the block exists.
         *
         *  @param[in] hashMerkleRoot The merkle root to search for.
         *  @return True if block exists, false otherwise.
         *
         **/
        bool find_block(const uint512_t& hashMerkleRoot);


        /** sign_block
         *
         *  Signs the block to seal the proof of work.
         *
         *  @return True if block is valid, false otherwise.
         *
         **/
        bool sign_block(uint64_t nNonce, const uint512_t& hashMerkleRoot, const std::vector<uint8_t>& vOffsets,
                        TAO::Ledger::Block* pBlock, uint64_t nTemplateCreationTime,
                        uint32_t nTemplateChannel, uint32_t nTemplateChannelHeight);


        /** clear_map
         *
         *  Clear the blocks map.
         *
         **/
        void clear_map();


        /** ProcessLegacyPacket
         *
         *  Handle legacy 8-bit opcodes (zero-extended to 16-bit).
         *
         *  @param[in] nOpcode The opcode to process.
         *  @return True if no errors, false otherwise.
         *
         **/
        bool ProcessLegacyPacket(uint16_t nOpcode);


        /** ProcessStatelessPacket
         *
         *  Handle stateless 16-bit opcodes (0xD0xx range).
         *
         *  @param[in] nOpcode The opcode to process.
         *  @return True if no errors, false otherwise.
         *
         **/
        bool ProcessStatelessPacket(uint16_t nOpcode);
    };

} // namespace LLP

#endif // NEXUS_LLP_TYPES_UNIFIED_MINER_CONNECTION_H
