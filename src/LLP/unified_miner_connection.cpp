/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLP/types/unified_miner_connection.h>
#include <LLP/templates/ddos.h>
#include <LLP/templates/events.h>
#include <LLP/packets/stateless_packet.h>
#include <LLP/include/genesis_constants.h>
#include <LLP/include/stateless_miner.h>
#include <LLP/include/stateless_manager.h>
#include <LLP/include/opcode_utility.h>
#include <LLP/include/falcon_auth.h>
#include <LLP/include/push_notification.h>
#include <LLP/include/mining_constants.h>
#include <LLP/include/round_state_utility.h>
#include <LLP/include/auto_cooldown_manager.h>
#include <LLP/include/port.h>

#include <TAO/Ledger/include/chainstate.h>
#include <TAO/Ledger/include/retarget.h>

#include <Util/include/config.h>
#include <Util/include/args.h>
#include <Util/include/debug.h>
#include <Util/include/runtime.h>
#include <Util/include/hex.h>

#include <chrono>
#include <limits>


namespace LLP
{
    /* Static member definitions */
    std::atomic<uint32_t> UnifiedMinerConnection::nBlockIterator{0};
    std::atomic<uint64_t> UnifiedMinerConnection::nDiffCacheTime{0};
    UnifiedMinerConnection::PaddedDifficultyCache UnifiedMinerConnection::nDiffCacheValue[3];


    /** Default Constructor **/
    UnifiedMinerConnection::UnifiedMinerConnection()
    : BaseConnection<StatelessPacket>()
    , m_lane(ProtocolLane::STATELESS)
    , m_nAcceptPort(0)
    , context()
    , MUTEX()
    , mapBlocks()
    , mapSessionKeys()
    , SESSION_MUTEX()
    {
    }


    /** Constructor **/
    UnifiedMinerConnection::UnifiedMinerConnection(const Socket& SOCKET_IN, DDOS_Filter* DDOS_IN,
                                                   bool fDDOSIn, uint16_t nListenPort)
    : BaseConnection<StatelessPacket>(SOCKET_IN, DDOS_IN, fDDOSIn, false /* fOutgoing — miners connect to us */)
    , m_lane(IsStatelessPort(nListenPort) ? ProtocolLane::STATELESS : ProtocolLane::LEGACY)
    , m_nAcceptPort(nListenPort)
    , context()
    , MUTEX()
    , mapBlocks()
    , mapSessionKeys()
    , SESSION_MUTEX()
    {
    }


    /** Constructor **/
    UnifiedMinerConnection::UnifiedMinerConnection(DDOS_Filter* DDOS_IN, bool fDDOSIn)
    : BaseConnection<StatelessPacket>(DDOS_IN, fDDOSIn, false)
    , m_lane(ProtocolLane::STATELESS)
    , m_nAcceptPort(0)
    , context()
    , MUTEX()
    , mapBlocks()
    , mapSessionKeys()
    , SESSION_MUTEX()
    {
    }


    /** Default Destructor **/
    UnifiedMinerConnection::~UnifiedMinerConnection()
    {
        /* Clear session keys */
        {
            std::lock_guard<std::mutex> lock(SESSION_MUTEX);
            mapSessionKeys.clear();
        }

        /* Clean up block map */
        LOCK(MUTEX);
        clear_map();
    }


    /** IsStatelessPort
     *
     *  Determines if a port corresponds to the stateless mining protocol.
     *  The stateless ports are the main mining port (9323) and its SSL variant (9325).
     *  The legacy ports are 8323 and 8325.
     *
     **/
    bool UnifiedMinerConnection::IsStatelessPort(uint16_t nPort)
    {
        const uint16_t nStatelessPlain = GetMiningPort();
        const uint16_t nStatelessSSL   = GetMiningSSLPort();

        return (nPort == nStatelessPlain || nPort == nStatelessSSL);
    }


    /** ReadPacket
     *
     *  Dual-mode framing: reads 1-byte header for LEGACY, 2-byte for STATELESS.
     *  LENGTH (4-byte big-endian) and DATA reading are identical for both.
     *
     **/
    void UnifiedMinerConnection::ReadPacket()
    {
        if(m_lane == ProtocolLane::LEGACY)
        {
            /* Read 1-byte header, zero-extend to uint16_t for StatelessPacket compatibility. */
            if(Available() >= 1 && INCOMING.IsNull())
            {
                std::vector<uint8_t> HEADER(1, 0);
                if(Read(HEADER, 1) == 1)
                    INCOMING.HEADER = HEADER[0];  /* 0x00-0xFE, zero-extended */
            }
        }
        else
        {
            /* Read 2-byte header (big-endian) */
            if(Available() >= 2 && INCOMING.IsNull())
            {
                std::vector<uint8_t> HEADER(2, 0);
                if(Read(HEADER, 2) == 2)
                    INCOMING.HEADER = (static_cast<uint16_t>(HEADER[0]) << 8) | HEADER[1];
            }
        }

        /* Read the packet length (4-byte big-endian) — identical for both lanes. */
        if(Available() >= 4 && !INCOMING.IsNull() && INCOMING.LENGTH == 0)
        {
            std::vector<uint8_t> BYTES(4, 0);
            if(Read(BYTES, 4) == 4)
            {
                INCOMING.SetLength(BYTES);
                Event(EVENTS::HEADER);
            }
        }

        /* Handle Reading Packet Data. */
        uint32_t nAvailable = Available();
        if(INCOMING.Header() && nAvailable > 0 && !INCOMING.IsNull() && INCOMING.DATA.size() < INCOMING.LENGTH)
        {
            uint32_t nMaxRead = static_cast<uint32_t>(INCOMING.LENGTH - INCOMING.DATA.size());
            std::vector<uint8_t> DATA(std::min(nAvailable, nMaxRead), 0);

            int32_t nRead = Read(DATA, DATA.size());
            if(nRead > 0)
                INCOMING.DATA.insert(INCOMING.DATA.end(), DATA.begin(), DATA.begin() + nRead);

            if(INCOMING.Complete())
                Event(EVENTS::PACKET, static_cast<uint32_t>(DATA.size()));
        }
    }


    /** Event
     *
     *  Handle connection events.
     *
     **/
    void UnifiedMinerConnection::Event(uint8_t EVENT, uint32_t LENGTH)
    {
        switch(EVENT)
        {
            case EVENTS::HEADER:
            {
                /* Log packet header received */
                if(Incoming())
                {
                    const StatelessPacket& PACKET = this->INCOMING;
                    debug::log(1, FUNCTION, "UnifiedMinerLLP: HEADER from ", GetAddress().ToStringIP(),
                               " header=0x", std::hex, uint32_t(PACKET.HEADER), std::dec,
                               " length=", PACKET.LENGTH,
                               " lane=", (m_lane == ProtocolLane::LEGACY ? "LEGACY" : "STATELESS"));
                }

                /* DDOS: check oversized packets */
                if(fDDOS.load() && Incoming())
                {
                    const StatelessPacket& PACKET = this->INCOMING;
                    if(PACKET.LENGTH > MiningConstants::MAX_MINING_PACKET_SIZE)
                    {
                        debug::error(FUNCTION, "Packet length too large: ", PACKET.LENGTH);
                        if(DDOS)
                            DDOS->Ban();
                        Disconnect();
                        return;
                    }
                }

                break;
            }

            case EVENTS::PACKET:
            {
                break;
            }

            case EVENTS::CONNECT:
            {
                /* Check auto-expiring cooldown FIRST before accepting connection */
                if(AutoCooldownManager::Get().IsInCooldown(GetAddress()))
                {
                    debug::log(0, FUNCTION, "Connection rejected - IP in cooldown: ",
                               GetAddress().ToStringIP());
                    Disconnect();
                    return;
                }

                /* Lane was already set in constructor from m_nAcceptPort.
                 * Log connection details with lane info. */
                debug::log(0, FUNCTION, "UnifiedMinerLLP: New ",
                           (m_lane == ProtocolLane::LEGACY ? "legacy" : "stateless"),
                           " connection from ", GetAddress().ToStringIP(),
                           ":", GetAddress().GetPort(),
                           " (accepted on port ", m_nAcceptPort, ")");

                /* Initialize context with connection info */
                LOCK(MUTEX);

                std::string strAddr = GetAddress().ToStringIP();
                fAuthenticatedAtomic.store(false, std::memory_order_relaxed);
                fHandshakeInProgressAtomic.store(false, std::memory_order_relaxed);

                context = MiningContext(
                    0,  /* nChannel - not set yet */
                    TAO::Ledger::ChainState::nBestHeight.load(),
                    runtime::unifiedtimestamp(),
                    strAddr,
                    0,  /* nProtocolVersion */
                    false,  /* fAuthenticated */
                    0,  /* nSessionId */
                    uint256_t(0),  /* hashKeyID */
                    uint256_t(0)   /* hashGenesis */
                );

                /* Set protocol lane in context */
                if(m_lane == ProtocolLane::STATELESS)
                    context = context.WithProtocolLane(ProtocolLane::STATELESS);
                else
                    context = context.WithProtocolLane(ProtocolLane::LEGACY);

                break;
            }

            case EVENTS::DISCONNECT:
            {
                debug::log(0, FUNCTION, "UnifiedMinerLLP: Disconnecting ",
                           (m_lane == ProtocolLane::LEGACY ? "legacy" : "stateless"),
                           " connection from ", GetAddress().ToStringIP(),
                           " reason=", LENGTH);

                /* Unregister from StatelessMinerManager if registered */
                {
                    LOCK(MUTEX);
                    if(context.fAuthenticated)
                    {
                        StatelessMinerManager::Get().RemoveMinerByKeyID(context.hashKeyID);
                    }
                }

                break;
            }

            case EVENTS::PROCESSED:
            {
                break;
            }

            case EVENTS::GENERIC:
            {
                break;
            }
        }
    }


    /** ProcessPacket
     *
     *  Opcode-range dispatch: routes to legacy or stateless handler based on lane.
     *
     **/
    bool UnifiedMinerConnection::ProcessPacket()
    {
        const uint16_t nOpcode = INCOMING.HEADER;

        if(m_lane == ProtocolLane::LEGACY && nOpcode < 0x0100)
        {
            /* Route to legacy handler */
            return ProcessLegacyPacket(nOpcode);
        }
        else if(m_lane == ProtocolLane::STATELESS && (nOpcode & 0xFF00) == 0xD000)
        {
            /* Route to stateless handler */
            return ProcessStatelessPacket(nOpcode);
        }
        else
        {
            /* Wrong protocol on wrong lane — reject */
            debug::error(FUNCTION, "Invalid opcode 0x", std::hex, nOpcode, std::dec,
                         " on ", (m_lane == ProtocolLane::LEGACY ? "LEGACY" : "STATELESS"),
                         " lane from ", GetAddress().ToStringIP());
            Disconnect();
            return false;
        }
    }


    /** ProcessLegacyPacket
     *
     *  Handle legacy 8-bit opcodes (zero-extended to 16-bit).
     *
     *  TODO: Port handler logic from Miner::ProcessPacket() in miner.cpp.
     *  This is a transitional stub that logs and disconnects.
     *
     **/
    bool UnifiedMinerConnection::ProcessLegacyPacket(uint16_t nOpcode)
    {
        debug::log(2, FUNCTION, "Legacy opcode 0x", std::hex, nOpcode, std::dec,
                   " from ", GetAddress().ToStringIP(), " — handler stub (TODO: port from Miner)");

        /* TODO: Dispatch to shared mining handlers extracted from Miner::ProcessPacket */

        return true;
    }


    /** ProcessStatelessPacket
     *
     *  Handle stateless 16-bit opcodes (0xD0xx range).
     *
     *  TODO: Port handler logic from StatelessMinerConnection::ProcessPacket().
     *  This is a transitional stub that logs and disconnects.
     *
     **/
    bool UnifiedMinerConnection::ProcessStatelessPacket(uint16_t nOpcode)
    {
        debug::log(2, FUNCTION, "Stateless opcode 0x", std::hex, nOpcode, std::dec,
                   " from ", GetAddress().ToStringIP(), " — handler stub (TODO: port from StatelessMinerConnection)");

        /* TODO: Dispatch to shared mining handlers extracted from StatelessMinerConnection::ProcessPacket */

        return true;
    }


    /** DrainOutgoingQueue
     *
     *  Lane-aware drain: writes packets using the correct framing for this lane.
     *  Name-hides the base class version.
     *
     **/
    uint32_t UnifiedMinerConnection::DrainOutgoingQueue()
    {
        uint32_t nDrained = 0;

        /* Grab all packets under lock, then release before writing. */
        std::queue<StatelessPacket> qLocal;
        {
            LOCK(OUTGOING_MUTEX);
            qLocal = std::move(OUTGOING_QUEUE);
            OUTGOING_QUEUE = std::queue<StatelessPacket>();
        }

        fHasOutgoing.store(false, std::memory_order_release);

        while(!qLocal.empty())
        {
            if(m_lane == ProtocolLane::LEGACY)
                WriteLegacyFrame(qLocal.front());
            else
                WritePacket(qLocal.front());

            qLocal.pop();
            ++nDrained;
        }

        return nDrained;
    }


    /** WriteLegacyFrame
     *
     *  Writes a StatelessPacket using legacy 5-byte framing:
     *  [uint8_t header][uint32_t big-endian length][data]
     *
     *  The HEADER field is truncated from uint16_t to uint8_t.
     *
     **/
    void UnifiedMinerConnection::WriteLegacyFrame(const StatelessPacket& packet)
    {
        /* Build the legacy wire-format bytes. */
        std::vector<uint8_t> vBytes;

        /* 1-byte header (truncate 16-bit to 8-bit) */
        vBytes.push_back(static_cast<uint8_t>(packet.HEADER & 0xFF));

        /* 4-byte length (big-endian) */
        uint32_t nLen = static_cast<uint32_t>(packet.DATA.size());
        vBytes.push_back(static_cast<uint8_t>((nLen >> 24) & 0xFF));
        vBytes.push_back(static_cast<uint8_t>((nLen >> 16) & 0xFF));
        vBytes.push_back(static_cast<uint8_t>((nLen >>  8) & 0xFF));
        vBytes.push_back(static_cast<uint8_t>((nLen      ) & 0xFF));

        /* Append data payload. */
        if(!packet.DATA.empty())
            vBytes.insert(vBytes.end(), packet.DATA.begin(), packet.DATA.end());

        /* Write to socket. */
        Write(vBytes, vBytes.size());
    }


    /** respond
     *
     *  Send a packet response. Enqueues via QueuePacket for deferred FLUSH_THREAD sending.
     *
     **/
    void UnifiedMinerConnection::respond(const StatelessPacket& packet)
    {
        QueuePacket(packet);
    }


    /** IsTimeoutExempt **/
    bool UnifiedMinerConnection::IsTimeoutExempt() const
    {
        return fAuthenticatedAtomic.load(std::memory_order_relaxed)
            || fHandshakeInProgressAtomic.load(std::memory_order_relaxed);
    }


    /** GetReadTimeout **/
    uint32_t UnifiedMinerConnection::GetReadTimeout() const
    {
        if(fAuthenticatedAtomic.load(std::memory_order_relaxed))
            return config::GetArg("-miningreadtimeout", MiningConstants::DEFAULT_MINING_READ_TIMEOUT_MS);

        return 0;
    }


    /** GetWriteTimeout **/
    uint32_t UnifiedMinerConnection::GetWriteTimeout() const
    {
        if(fAuthenticatedAtomic.load(std::memory_order_relaxed))
            return config::GetArg("-miningwritetimeout", 30000);

        return config::GetArg("-writetimeout", 5000);
    }


    /** GetMaxSendBuffer **/
    uint64_t UnifiedMinerConnection::GetMaxSendBuffer() const
    {
        if(fAuthenticatedAtomic.load(std::memory_order_relaxed))
            return config::GetArg("-miningmaxsendbuffer", MiningConstants::MINING_MAX_SEND_BUFFER);

        return config::GetArg("-maxsendbuffer", MAX_SEND_BUFFER);
    }


    /** GetContext **/
    MiningContext UnifiedMinerConnection::GetContext()
    {
        LOCK(MUTEX);
        return context;
    }


    /** SendChannelNotification
     *
     *  Send a channel-specific push notification using lane-appropriate framing.
     *
     *  TODO: Fully port notification logic from Miner::SendChannelNotification()
     *  and StatelessMinerConnection::SendChannelNotification().
     *
     **/
    void UnifiedMinerConnection::SendChannelNotification()
    {
        /* Take snapshot of context under lock */
        MiningContext ctxSnap;
        {
            LOCK(MUTEX);
            ctxSnap = context;
        }

        /* Must be authenticated and subscribed — check BEFORE throttle. */
        if(!ctxSnap.fAuthenticated || !ctxSnap.fSubscribedToNotifications)
            return;

        /* Check push throttle — skip if too recent (unless forced) */
        {
            LOCK(MUTEX);
            if(!m_force_next_push)
            {
                auto tNow = std::chrono::steady_clock::now();
                auto tElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    tNow - m_last_template_push_time).count();

                if(tElapsed < MiningConstants::TEMPLATE_PUSH_MIN_INTERVAL_MS)
                {
                    debug::log(3, FUNCTION, "Push throttled for ",
                               GetAddress().ToStringIP(), " (", tElapsed, "ms since last)");
                    return;
                }
            }

            m_force_next_push = false;
            m_last_template_push_time = std::chrono::steady_clock::now();
        }

        debug::log(2, FUNCTION, "Push notification sent to ",
                   (m_lane == ProtocolLane::LEGACY ? "legacy" : "stateless"),
                   " miner ", GetAddress().ToStringIP(),
                   " on channel ", ctxSnap.nChannel);

        /* TODO: Build and send the actual notification packet using
         * PushNotificationBuilder::BuildChannelNotification() with proper args
         * (stateBest, stateChannel, etc.). The full implementation will be
         * ported from the existing Miner/StatelessMinerConnection classes. */
    }


    /** SendNodeShutdown **/
    void UnifiedMinerConnection::SendNodeShutdown(uint32_t nReasonCode)
    {
        if(m_nodeShutdownNotification.Sent())
            return;

        m_nodeShutdownNotification.MarkSent();

        /* Build NODE_SHUTDOWN packet with reason code */
        StatelessPacket PACKET;
        PACKET.HEADER = OpcodeUtility::Stateless::NODE_SHUTDOWN;

        /* For legacy lane, use the low byte of the shutdown opcode (0xFF) */
        if(m_lane == ProtocolLane::LEGACY)
            PACKET.HEADER = OpcodeUtility::Stateless::NODE_SHUTDOWN & 0xFF;

        /* Encode reason as 4-byte big-endian payload */
        PACKET.DATA.resize(4);
        PACKET.DATA[0] = static_cast<uint8_t>((nReasonCode >> 24) & 0xFF);
        PACKET.DATA[1] = static_cast<uint8_t>((nReasonCode >> 16) & 0xFF);
        PACKET.DATA[2] = static_cast<uint8_t>((nReasonCode >>  8) & 0xFF);
        PACKET.DATA[3] = static_cast<uint8_t>((nReasonCode      ) & 0xFF);
        PACKET.LENGTH = static_cast<uint32_t>(PACKET.DATA.size());

        QueuePacket(PACKET);

        debug::log(0, FUNCTION, "NODE_SHUTDOWN sent to ",
                   (m_lane == ProtocolLane::LEGACY ? "legacy" : "stateless"),
                   " miner ", GetAddress().ToStringIP(),
                   " reason=", nReasonCode);
    }


    /** GetCachedDifficulty **/
    uint32_t UnifiedMinerConnection::GetCachedDifficulty(uint32_t nChannel)
    {
        if(nChannel > 2)
        {
            debug::error(FUNCTION, "Invalid channel ", nChannel, ", defaulting to Prime (1)");
            nChannel = 1;
        }

        uint64_t nNow = runtime::unifiedtimestamp();
        uint64_t nCacheTime = nDiffCacheTime.load(std::memory_order_acquire);

        if(nCacheTime > 0 && nNow >= nCacheTime &&
           (nNow - nCacheTime) < MiningConstants::DIFFICULTY_CACHE_TTL_SECONDS)
        {
            uint32_t nCached = nDiffCacheValue[nChannel].nDifficulty.load(std::memory_order_acquire);
            if(nCached > 0)
                return nCached;
        }

        /* Cache miss — compute difficulty */
        uint32_t nBits = TAO::Ledger::GetNextTargetRequired(
            TAO::Ledger::ChainState::tStateBest.load(), nChannel, false);

        /* Update cache atomically */
        nDiffCacheValue[nChannel].nDifficulty.store(nBits, std::memory_order_release);
        nDiffCacheTime.store(nNow, std::memory_order_release);

        return nBits;
    }


    /** new_block **/
    TAO::Ledger::Block* UnifiedMinerConnection::new_block()
    {
        /* TODO: Port from StatelessMinerConnection::new_block() */
        return nullptr;
    }


    /** find_block **/
    bool UnifiedMinerConnection::find_block(const uint512_t& hashMerkleRoot)
    {
        return mapBlocks.count(hashMerkleRoot) > 0;
    }


    /** sign_block **/
    bool UnifiedMinerConnection::sign_block(uint64_t nNonce, const uint512_t& hashMerkleRoot,
                                             const std::vector<uint8_t>& vOffsets,
                                             TAO::Ledger::Block* pBlock,
                                             uint64_t nTemplateCreationTime,
                                             uint32_t nTemplateChannel,
                                             uint32_t nTemplateChannelHeight)
    {
        /* TODO: Port from StatelessMinerConnection::sign_block() */
        return false;
    }


    /** clear_map **/
    void UnifiedMinerConnection::clear_map()
    {
        mapBlocks.clear();
    }

} // namespace LLP
