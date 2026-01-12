/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To The Voice of The People

____________________________________________________________________________________________*/

#include <TAO/Ledger/include/version_control.h>

/** Version Control Utility Implementation
 *
 *  This file provides the implementation for global variables declared in version_control.h.
 *  Most of the version control utility is header-only (constexpr and inline functions),
 *  but this file defines non-const global variables that need external linkage.
 **/

namespace TAO
{
namespace Ledger
{
namespace Versions
{
namespace Block
{
    /** Mining block version override
     *
     *  Can be set via -miningblockversion config parameter for testing scenarios.
     *  Default is 0, which means use the current network version (mainnet or testnet).
     *
     *  Example usage:
     *      ./nexus -miningblockversion=2   // Test v2 block mining
     *      ./nexus -miningblockversion=5   // Test v5 stateless mining
     *
     *  This allows testing compatibility scenarios without modifying code.
     **/
    uint32_t MINING_OVERRIDE_VERSION = 0;

} // namespace Block
} // namespace Versions
} // namespace Ledger
} // namespace TAO
