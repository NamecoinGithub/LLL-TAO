/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLC_INCLUDE_QTV_JULIA_BRIDGE_H
#define NEXUS_LLC_INCLUDE_QTV_JULIA_BRIDGE_H

#include <include/qtv/QTVJuliaBridge.hpp>

namespace LLC
{
namespace QTV
{
    constexpr int HOOK_STATUS_OK          = qtv::HOOK_STATUS_OK;
    constexpr int HOOK_STATUS_UNAVAILABLE = qtv::HOOK_STATUS_UNAVAILABLE;

    using HookFunction = qtv::HookFunction;
    using QTVJuliaBridge = qtv::QTVJuliaBridge;
} // namespace QTV
} // namespace LLC

#endif
