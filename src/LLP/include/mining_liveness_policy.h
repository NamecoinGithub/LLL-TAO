#pragma once
#ifndef NEXUS_LLP_INCLUDE_MINING_LIVENESS_POLICY_H
#define NEXUS_LLP_INCLUDE_MINING_LIVENESS_POLICY_H

#include <LLP/include/mining_constants.h>
#include <LLP/include/mining_timers.h>
#include <LLP/include/node_cache.h>

#include <Util/include/config.h>

#include <cstdint>
#include <limits>
#include <string>

namespace LLP
{
namespace MiningLivenessPolicy
{
    /* 30-minute transport floor:
     * Prime miners can legitimately spend 30+ minutes exploring deep chains
     * before they have anything to submit, so a shorter socket read-idle
     * timeout disconnects healthy miners.  This floor also remains safely above
     * the keepalive grace and health-probe windows used by node-side liveness. */
    constexpr uint32_t WORKLOAD_READ_TIMEOUT_FLOOR_MS = 1800000;

    constexpr uint32_t HEALTH_PROBE_WINDOW_MS =
        static_cast<uint32_t>(MiningTimers::HEALTH_PROBE_INTERVAL_SEC * 2 * 1000);

    constexpr uint32_t KEEPALIVE_GRACE_PERIOD_MS =
        static_cast<uint32_t>(MiningTimers::KEEPALIVE_GRACE_PERIOD_SEC * 1000);

    constexpr uint32_t Max3(
        uint32_t a,
        uint32_t b,
        uint32_t c)
    {
        const uint32_t ab = (a >= b) ? a : b;
        return (ab >= c) ? ab : c;
    }

    constexpr uint32_t READ_TIMEOUT_FLOOR_MS =
        Max3(WORKLOAD_READ_TIMEOUT_FLOOR_MS, HEALTH_PROBE_WINDOW_MS, KEEPALIVE_GRACE_PERIOD_MS);

    static_assert(MiningConstants::DEFAULT_MINING_READ_TIMEOUT_MS >= READ_TIMEOUT_FLOOR_MS,
        "DEFAULT_MINING_READ_TIMEOUT_MS must satisfy the shared mining read-timeout floor.");

    inline uint64_t GetSessionLivenessTimeoutSec(const std::string& strAddress = std::string())
    {
        return NodeCache::GetSessionLivenessTimeout(strAddress);
    }

    inline uint32_t GetConfiguredReadTimeoutMs()
    {
        const int64_t nConfigured = config::GetArg(
            "-miningreadtimeout",
            static_cast<int64_t>(MiningConstants::DEFAULT_MINING_READ_TIMEOUT_MS));

        const int64_t nClamped =
            (nConfigured < static_cast<int64_t>(READ_TIMEOUT_FLOOR_MS))
                ? static_cast<int64_t>(READ_TIMEOUT_FLOOR_MS)
                : nConfigured;

        if(nClamped >= static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
            return std::numeric_limits<uint32_t>::max();

        return static_cast<uint32_t>(nClamped);
    }
} // namespace MiningLivenessPolicy
} // namespace LLP

#endif
