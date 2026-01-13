/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <LLP/types/stateless_miner_connection.h>
#include <LLP/templates/ddos.h>

using namespace LLP;

TEST_CASE("Connection Lifecycle State Machine Tests", "[connection_lifecycle]")
{
    SECTION("Initial state is CONNECTING")
    {
        /* Create a connection (no socket needed for state testing) */
        DDOS_Filter ddos(60);
        StatelessMinerConnection conn(&ddos, false);
        
        /* Verify initial state */
        REQUIRE(conn.GetState() == StatelessMinerConnection::ConnectionState::CONNECTING);
        REQUIRE_FALSE(conn.IsCleanedUp());
    }
    
    SECTION("IsCleanedUp returns false initially and true after disconnect")
    {
        DDOS_Filter ddos(60);
        StatelessMinerConnection conn(&ddos, false);
        
        /* Initially not cleaned up */
        REQUIRE_FALSE(conn.IsCleanedUp());
        
        /* After disconnect, should be cleaned up */
        conn.Disconnect("Test disconnect");
        REQUIRE(conn.IsCleanedUp());
        REQUIRE(conn.GetState() == StatelessMinerConnection::ConnectionState::CLOSED);
    }
    
    SECTION("Disconnect is idempotent - multiple calls are safe")
    {
        DDOS_Filter ddos(60);
        StatelessMinerConnection conn(&ddos, false);
        
        /* First disconnect */
        conn.Disconnect("First disconnect");
        REQUIRE(conn.IsCleanedUp());
        REQUIRE(conn.GetState() == StatelessMinerConnection::ConnectionState::CLOSED);
        
        /* Second disconnect should be no-op */
        conn.Disconnect("Second disconnect");
        REQUIRE(conn.IsCleanedUp());
        REQUIRE(conn.GetState() == StatelessMinerConnection::ConnectionState::CLOSED);
    }
}

TEST_CASE("Connection State Transitions", "[connection_lifecycle]")
{
    SECTION("State transitions follow expected flow")
    {
        DDOS_Filter ddos(60);
        StatelessMinerConnection conn(&ddos, false);
        
        /* Start in CONNECTING */
        REQUIRE(conn.GetState() == StatelessMinerConnection::ConnectionState::CONNECTING);
        
        /* Note: Testing state transitions requires packet processing which needs
         * a full connection setup. The important test is that Disconnect works
         * from any state and prevents further operations. */
    }
}
