/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People
____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_TYPES_TIME_H
#define NEXUS_LLP_TYPES_TIME_H

#include <LLP/templates/connection.h>
#include <LLP/include/llp_opcodes.h>

#include <Util/templates/containers.h>

namespace LLP
{

    class TimeNode : public Connection
    {
    public:
        /* Protocol message opcodes from centralized LLP Opcodes Registry.
         * For documentation on each opcode, see LLP/include/llp_opcodes.h
         */
        static constexpr auto TIME_DATA     = Opcodes::TimeSynchronization::TIME_DATA;
        static constexpr auto ADDRESS_DATA  = Opcodes::TimeSynchronization::ADDRESS_DATA;
        static constexpr auto TIME_OFFSET   = Opcodes::TimeSynchronization::TIME_OFFSET;
        static constexpr auto GET_OFFSET    = Opcodes::TimeSynchronization::GET_OFFSET;
        static constexpr auto GET_TIME      = Opcodes::TimeSynchronization::GET_TIME;
        static constexpr auto GET_ADDRESS   = Opcodes::TimeSynchronization::GET_ADDRESS;
        static constexpr auto PING          = Opcodes::TimeSynchronization::PING;
        static constexpr auto CLOSE         = Opcodes::TimeSynchronization::CLOSE;

    private:


        /** Store the samples in a majority object. */
        majority<int32_t> nSamples;


        /** Keep track of our sent requests for time data. This gives us protection against unsolicted TIME_DATA messages. **/
        std::atomic<int32_t> nRequests;

    public:


        /** Name
         *
         *  Returns a string for the name of this type of Node.
         *
         **/
        static std::string Name() { return "Time"; }


        /** Constructor **/
        TimeNode();


        /** Constructor **/
        TimeNode(Socket SOCKET_IN, DDOS_Filter* DDOS_IN, bool fDDOSIn = false);


        /** Constructor **/
        TimeNode(DDOS_Filter* DDOS_IN, bool fDDOSIn = false);


        /* Virtual destructor. */
        virtual ~TimeNode();


        /** Event
         *
         *  Virtual Functions to Determine Behavior of Message LLP.
         *
         *  @param[in] EVENT The byte header of the event type.
         *  @param[in[ LENGTH The size of bytes read on packet read events.
         *
         */
        void Event(uint8_t EVENT, uint32_t LENGTH = 0);


        /** ProcessPacket
         *
         *  Main message handler once a packet is recieved.
         *
         *  @return True is no errors, false otherwise.
         *
         **/
        bool ProcessPacket();


        /** GetSample
         *
         *  Get a time sample from the time server.
         *
         **/
        void GetSample();


        /** GetOffset
         *
         *  Get the current time offset from the unified majority.
         *
         **/
        static int32_t GetOffset();
    };
}

#endif
