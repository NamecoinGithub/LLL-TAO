/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLC_INCLUDE_QTV_ENGINE_H
#define NEXUS_LLC_INCLUDE_QTV_ENGINE_H

#include <LLC/include/qtv_julia_bridge.h>
#include <include/qtv/QTVEngineFacade.hpp>

namespace LLC
{
namespace QTV
{
    using QTVBackendKind = qtv::QTVBackendKind;
    using QTVCapabilities = qtv::QTVCapabilities;
    using IQTVEngine = qtv::IQTVEngine;
    using NullQTVEngine = qtv::NullQTVEngine;
    using JuliaQTVEngine = qtv::JuliaQTVEngine;
    using CppQTVEngine = qtv::CppQTVEngine;
    using QTVEngineFacade = qtv::QTVEngineFacade;

    inline QTVBackendKind SelectBackend(const QTVBackendKind preferred, const QTVCapabilities& capabilities) noexcept
    {
        return qtv::SelectBackend(preferred, capabilities);
    }
} // namespace QTV
} // namespace LLC

#endif
