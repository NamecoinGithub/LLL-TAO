/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2018

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#ifndef NEXUS_LLP_INCLUDE_ADDRESS_H
#define NEXUS_LLP_INCLUDE_ADDRESS_H

#include <LLP/include/service.h>
#include <Util/templates/serialize.h>
#include <cstdint>

namespace LLP
{
    /** Services flags */
    enum
    {
        NODE_NETWORK = (1 << 0),
    };


    /** A Service with information about it as peer */
    class Address : public Service
    {
    public:
        Address();
        Address(const Address &other);
        Address(Address &other);
        Address(const Service &ipIn, uint64_t nServicesIn = NODE_NETWORK);
        Address(Service &ipIn, uint64_t nServicesIn = NODE_NETWORK);

        virtual ~Address();

        Address &operator=(const Address &other);
        Address &operator=(Address &other);

        IMPLEMENT_SERIALIZE
        (
            Address* pthis = const_cast<Address*>(this);
            Service* pip = (Service*)pthis;

            if (fRead)
            {
                pthis->nServices = NODE_NETWORK;
                pthis->nTime = 100000000;
                pthis->nLastTry = 0;
            }


            if (nSerType & SER_DISK)
                READWRITE(nSerVersion);

            if ((nSerType & SER_DISK) || (!(nSerType & SER_GETHASH)))
                READWRITE(nTime);

            READWRITE(nServices);
            READWRITE(*pip);
        )

        void print() const;

    protected:
        uint64_t nServices;

        // disk and network only
        uint32_t nTime;

        // memory only
        int64_t nLastTry;
    };
}

#endif
