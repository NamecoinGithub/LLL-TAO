/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_INCLUDE_MINING_CONSTANTS_H
#define NEXUS_LLP_INCLUDE_MINING_CONSTANTS_H

#include <cstdint>

namespace LLP
{
namespace MiningConstants
{
    //=========================================================================
    // RATE LIMITING CONSTANTS
    // Debug mode uses relaxed limits for development/testing
    // Production mode uses strict limits for network protection
    //=========================================================================
    
    #ifdef DEBUG
    /* Debug Mode - Relaxed limits for development/testing */
    constexpr uint32_t GET_BLOCK_MIN_INTERVAL_MS = 2000;          // 2 seconds in debug
    constexpr uint32_t GET_ROUND_MIN_INTERVAL_MS = 2000;          // 2 seconds in debug
    constexpr uint32_t GET_BLOCK_THROTTLE_INTERVAL_MS = 4000;     // 4 seconds when throttled
    constexpr uint32_t RATE_LIMIT_STRIKE_THRESHOLD = 20;          // 20 violations before ban
    constexpr uint32_t AUTOCOOLDOWN_DURATION_SECONDS = 60;        // 1 minute cooldown in debug
    constexpr bool DISABLE_LOCALHOST_RATE_LIMITING = true;        // Don't ban 127.0.0.1 in debug
    #else
    /* Production Mode - Strict limits for network protection */
    constexpr uint32_t GET_BLOCK_MIN_INTERVAL_MS = 6000;          // 6 seconds in production
    constexpr uint32_t GET_ROUND_MIN_INTERVAL_MS = 5000;          // 5 seconds in production
    constexpr uint32_t GET_BLOCK_THROTTLE_INTERVAL_MS = 10000;    // 10 seconds when throttled  
    constexpr uint32_t RATE_LIMIT_STRIKE_THRESHOLD = 10;          // 10 violations before ban
    constexpr uint32_t AUTOCOOLDOWN_DURATION_SECONDS = 300;       // 5 minutes cooldown
    constexpr bool DISABLE_LOCALHOST_RATE_LIMITING = false;       // Ban all IPs in production
    #endif
    
    //=========================================================================
    // PUSH NOTIFICATION INTERVALS
    //=========================================================================
    constexpr uint32_t NOTIFICATION_MIN_INTERVAL_MS = 1000;       // 1 second minimum between notifications
    constexpr uint32_t NOTIFICATION_TIMEOUT_MS = 5000;            // 5 seconds to respond to notification
    
    //=========================================================================
    // BLOCK REQUEST TIMEOUTS
    //=========================================================================
    constexpr uint32_t GET_ROUND_POLL_INTERVAL_MS = 2000;         // 2 seconds between GET_ROUND polls
    constexpr uint32_t BLOCK_TEMPLATE_CACHE_TTL_MS = 500;         // Cache block template for 500ms
    
    //=========================================================================
    // CHANNEL STATE CACHE
    //=========================================================================
    constexpr uint32_t CHANNEL_HEIGHT_CACHE_TTL_MS = 500;         // Cache channel height for 500ms
    constexpr uint32_t DIFFICULTY_CACHE_TTL_MS = 1000;            // Cache difficulty for 1 second
    
    //=========================================================================
    // SESSION MANAGEMENT
    //=========================================================================
    constexpr uint32_t SESSION_IDLE_TIMEOUT_SECONDS = 300;        // 5 minutes
    constexpr uint32_t SESSION_KEEPALIVE_INTERVAL_SECONDS = 60;   // 1 minute
    
    //=========================================================================
    // OPCODE VALIDATION RANGES
    //=========================================================================
    constexpr uint8_t MINING_OPCODE_MIN = 200;                    // Minimum valid mining opcode
    constexpr uint8_t MINING_OPCODE_MAX = 220;                    // Maximum valid mining opcode
    constexpr uint8_t AUTH_OPCODE_MIN = 207;                      // MINER_AUTH_INIT
    constexpr uint8_t AUTH_OPCODE_MAX = 212;                      // SESSION_KEEPALIVE
}
}

#endif
