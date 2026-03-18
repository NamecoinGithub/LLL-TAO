/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once

#include <vector>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <string>

#include <Util/templates/datastream.h>

namespace LLP
{

    /** StatelessPacket
     *
     *  Packet type for stateless mining protocol with 16-bit opcodes.
     *
     *  Components of a Stateless Mining Packet:
     *   BYTE 0 - 1   : Header (16-bit opcode, big-endian)
     *   BYTE 2 - 5   : Length (32-bit, big-endian)
     *   BYTE 6 - End : Data
     *
     *  This is the proper framing for NexusMiner stateless opcodes (0xD007, 0xD008, etc.)
     *  and is incompatible with legacy 8-bit LLP framing.
     *
     **/
    class StatelessPacket
    {
    public:
        /* Message typedef for compatibility with BaseConnection template. */
        typedef uint16_t message_t;


        /** The packet header (16-bit opcode, big-endian). **/
        uint16_t                HEADER;


        /** The length of the packet data. **/
        uint32_t                LENGTH;


        /** The packet payload. **/
        std::vector<uint8_t>    DATA;


        /** Default Constructor **/
        StatelessPacket()
        : HEADER (0xFFFF)
        , LENGTH (0)
        , DATA   ( )
        {
        }


        /** Copy Constructor **/
        StatelessPacket(const StatelessPacket& packet)
        : HEADER (packet.HEADER)
        , LENGTH (packet.LENGTH)
        , DATA   (packet.DATA)
        {
        }


        /** Move Constructor **/
        StatelessPacket(StatelessPacket&& packet) noexcept
        : HEADER (std::move(packet.HEADER))
        , LENGTH (std::move(packet.LENGTH))
        , DATA   (std::move(packet.DATA))
        {
        }


        /** Copy Assignment Operator **/
        StatelessPacket& operator=(const StatelessPacket& packet)
        {
            HEADER = packet.HEADER;
            LENGTH = packet.LENGTH;
            DATA   = packet.DATA;

            return *this;
        }


        /** Move Assignment Operator **/
        StatelessPacket& operator=(StatelessPacket&& packet) noexcept
        {
            HEADER = std::move(packet.HEADER);
            LENGTH = std::move(packet.LENGTH);
            DATA   = std::move(packet.DATA);

            return *this;
        }


        /** Destructor. **/
        ~StatelessPacket()
        {
            std::vector<uint8_t>().swap(DATA);
        }


        /** Constructor with 16-bit opcode **/
        StatelessPacket(const message_t nMessage)
        {
            SetNull();
            HEADER = nMessage;
        }


        /** SetNull
         *
         *  Set the Packet Null Flags.
         *
         **/
        void SetNull()
        {
            HEADER   = 0xFFFF;
            LENGTH   = 0;
            DATA.clear();
        }


        /** IsNull
         *
         *  Returns packet null flag.
         *
         **/
        bool IsNull() const
        {
            return (HEADER == 0xFFFF);
        }


        /** GetOpcode
         *
         *  Get the 16-bit opcode value.
         *
         *  @return The 16-bit opcode
         *
         **/
        uint16_t GetOpcode() const
        {
            return HEADER;
        }


        /** Complete
         *
         *  Determines if a packet is fully read.
         *  Packet is complete when we have header, length, and all data bytes.
         *
         **/
        bool Complete() const
        {
            return (Header() && static_cast<uint32_t>(DATA.size()) == LENGTH);
        }


        /** Header
         *
         *  Determines if header and length are fully read.
         *
         **/
        bool Header() const
        {
            if(IsNull())
                return false;

            /* For stateless packets, header is complete when LENGTH is set */
            return LENGTH > 0 || (HEADER != 0xFFFF && LENGTH == 0);
        }


        /** SetData
         *
         *  Set the Packet Data.
         *
         *  @param[in] ssData The datastream with the data to set.
         *
         **/
        void SetData(const DataStream& ssData)
        {
            LENGTH = static_cast<uint32_t>(ssData.size());
            DATA   = ssData.Bytes();
        }


        /** SetLength
         *
         *  Sets the size of the packet from the byte vector (big-endian).
         *
         *  @param[in] BYTES The vector of bytes to set length from.
         *
         **/
        void SetLength(const std::vector<uint8_t> &BYTES)
        {
            LENGTH = (BYTES[0] << 24) + (BYTES[1] << 16) + (BYTES[2] << 8) + (BYTES[3]);
        }


        /** HasDataPayload
         *
         *  Check whether this stateless opcode carries a data payload (LENGTH + DATA fields).
         *  Mirrors OpcodeUtility::HasDataPayload16() logic without requiring that header.
         *
         *  Un-mirrored stateless-only data-bearing opcodes (0xD100, 0xD101, 0xD0E0, 0xD0E1)
         *  always have payloads.  For mirrored opcodes (0xD0xx), the high byte is 0xD0
         *  and the low byte is the legacy 8-bit opcode whose payload rules apply:
         *   - 0-127: traditional data packets
         *   - 204-205: mining round response (NEW_ROUND / OLD_ROUND)
         *   - 206: CHANNEL_ACK
         *   - 207-212: Falcon auth/session packets
         *   - 213-214: reward-binding packets
         *   - 217-218: push-notification packets
         *   - 219-220: SESSION_STATUS / SESSION_STATUS_ACK
         *
         *  @return true if the opcode carries a payload
         *
         **/
        bool HasDataPayload() const
        {
            /* Un-mirrored stateless-only opcodes */
            if(HEADER == 0xD100 || HEADER == 0xD101 ||  // KEEPALIVE_V2 / KEEPALIVE_V2_ACK
               HEADER == 0xD0E0 || HEADER == 0xD0E1)    // PING_DIAG / PONG_DIAG
                return true;

            /* Mirrored opcodes: extract 8-bit base opcode from low byte */
            if((HEADER & 0xFF00) == 0xD000)
            {
                const uint8_t op = static_cast<uint8_t>(HEADER & 0xFF);
                if(op < 128)                return true;  // traditional data
                if(op >= 204 && op <= 206)  return true;  // NEW_ROUND/OLD_ROUND/CHANNEL_ACK
                if(op >= 207 && op <= 212)  return true;  // Falcon auth + keepalive
                if(op >= 213 && op <= 214)  return true;  // reward-binding
                if(op == 217 || op == 218)  return true;  // push notifications
                if(op == 219 || op == 220)  return true;  // SESSION_STATUS / ACK
            }
            return false;
        }


        /** GetBytes
         *
         *  Serializes class into a byte vector. Used to write packet to sockets.
         *
         *  Format: [HEADER (2, big-endian)] [LENGTH (4, big-endian)] [DATA (variable)]
         *
         **/
        std::vector<uint8_t> GetBytes() const
        {
            std::vector<uint8_t> BYTES;
            BYTES.reserve(6 + DATA.size());

            /* Encode 16-bit header (big-endian) */
            BYTES.push_back(static_cast<uint8_t>(HEADER >> 8));   // High byte
            BYTES.push_back(static_cast<uint8_t>(HEADER & 0xFF)); // Low byte

            /* Encode 32-bit length (big-endian) */
            BYTES.push_back(static_cast<uint8_t>(LENGTH >> 24));
            BYTES.push_back(static_cast<uint8_t>(LENGTH >> 16));
            BYTES.push_back(static_cast<uint8_t>(LENGTH >> 8));
            BYTES.push_back(static_cast<uint8_t>(LENGTH));

            /* Append data payload */
            BYTES.insert(BYTES.end(), DATA.begin(), DATA.end());

            return BYTES;
        }


        /** DebugString
         *
         *  Returns a debug string representation of the packet for logging.
         *
         *  @param[in] nMaxDataBytes Maximum number of data bytes to include (0 = all)
         *
         *  @return Human-readable string representation of packet
         *
         **/
        std::string DebugString(size_t nMaxDataBytes = 64) const
        {
            std::ostringstream oss;
            oss << "StatelessPacket{header=0x" << std::hex << std::setw(4) << std::setfill('0')
                << static_cast<uint32_t>(HEADER) << std::dec
                << ", length=" << LENGTH
                << ", data_size=" << DATA.size();

            if(!DATA.empty() && nMaxDataBytes > 0)
            {
                oss << ", data_preview=";
                size_t nShow = std::min(nMaxDataBytes, DATA.size());
                for(size_t i = 0; i < nShow; ++i)
                {
                    oss << std::hex << std::setw(2) << std::setfill('0')
                        << static_cast<uint32_t>(DATA[i]);
                }
                if(DATA.size() > nMaxDataBytes)
                    oss << "...";
            }

            oss << "}";
            return oss.str();
        }
    };
}
