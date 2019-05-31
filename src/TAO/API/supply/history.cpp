/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <TAO/API/include/users.h>
#include <TAO/API/include/supply.h>
#include <TAO/API/include/utils.h>

#include <TAO/Operation/include/enum.h>
#include <TAO/Operation/include/execute.h>

#include <TAO/Register/include/verify.h>
#include <TAO/Register/include/enum.h>

#include <TAO/Ledger/include/create.h>
#include <TAO/Ledger/types/mempool.h>

#include <LLC/include/random.h>
#include <LLD/include/global.h>

/* Global TAO namespace. */
namespace TAO
{

    /* API Layer namespace. */
    namespace API
    {

        /* Gets the history of an item. */
        json::json Supply::History(const json::json& params, bool fHelp)
        {
            json::json ret = json::json::array();

            /* Get the register address. */
            uint256_t hashRegister = 0;

            /* Check whether the caller has provided the asset name parameter. */
            if(params.find("name") != params.end())
                /* If name is provided then use this to deduce the register address */
                hashRegister = RegisterAddressFromName(params, "item", params["name"].get<std::string>());
            /* Otherwise try to find the raw hex encoded address. */
            else if(params.find("address") != params.end())
                hashRegister.SetHex(params["address"]);
            /* Fail if no required parameters supplied. */
            else
                throw APIException(-23, "Missing memory address");

            /* Get the register. */
            TAO::Register::State state;
            if(!LLD::regDB->ReadState(hashRegister, state))
                return ret; // no history so return empty array

            /* Generate return object. */
            json::json first;
            first["owner"]      = state.hashOwner.ToString();
            first["timestamp"]  = state.nTimestamp;

            /* Reset read position. */
            state.nReadPos = 0;

            /* Grab the last state. */
            while(!state.end())
            {
                /* If the data type is string. */
                std::string data;
                state >> data;

                first["checksum"] = state.hashChecksum;
                first["data"]    = data;
            }

            /* Push to return array. */
            ret.push_back(first);

            /* Read the last hash of owner. */
            uint512_t hashLast = 0;
            if(!LLD::legDB->ReadLast(state.hashOwner, hashLast))
                throw APIException(-24, "No last hash found");

            /* Iterate through sigchain for register updates. */
            while(hashLast != 0)
            {
                /* Get the transaction from disk. */
                TAO::Ledger::Transaction tx;
                if(!LLD::legDB->ReadTx(hashLast, tx))
                    throw APIException(-28, "Failed to read transaction");

                /* Set the next last. */
                hashLast = tx.hashPrevTx;

                /* Loop through all transactions. */
                for(uint32_t nContract = 0; nContract < tx.Size(); ++nContract)
                {
                    /* Get the operation byte. */
                    uint8_t nType = 0;
                    tx[nContract] >> nType;

                    /* Check for key operations. */
                    TAO::Register::State state;
                    switch(nType)
                    {
                        /* Break when at the register declaration. */
                        case TAO::Operation::OP::CREATE:
                        {
                            /* Set hash last to zero to break. */
                            hashLast = 0;

                            break;
                        }

                        case TAO::Operation::OP::APPEND:
                        {
                            /* Seek past pre-state. */
                            uint8_t nState = 0;
                            tx[nContract] >>= nState;

                            /* De-Serialize state register. */
                            tx[nContract] >>= state;

                            /* Generate return object. */
                            json::json obj;
                            obj["OP"]         = "APPEND";
                            obj["owner"]      = state.hashOwner.ToString();
                            obj["updated"]  = state.nTimestamp;

                            /* Reset read position. */
                            state.nReadPos = 0;

                            /* Grab the last state. */
                            while(!state.end())
                            {
                                /* If the data type is string. */
                                std::string data;
                                state >> data;

                                obj["checksum"] = state.hashChecksum;
                                obj["data"]    = data;
                            }

                            /* Push to return array. */
                            ret.push_back(obj);

                            break;
                        }

                        case TAO::Operation::OP::WRITE:
                        {
                            /* Seek past pre-state. */
                            uint8_t nState = 0;
                            tx[nContract] >>= nState;

                            /* De-Serialize state register. */
                            tx[nContract] >>= state;

                            /* Generate return object. */
                            json::json obj;
                            obj["OP"]         = "WRITE";
                            obj["owner"]      = state.hashOwner.ToString();
                            obj["updated"]  = state.nTimestamp;

                            /* Reset read position. */
                            state.nReadPos = 0;

                            /* Grab the last state. */
                            while(!state.end())
                            {
                                /* If the data type is string. */
                                std::string data;
                                state >> data;

                                obj["checksum"] = state.hashChecksum;
                                obj["data"]    = data;
                            }

                            /* Push to return array. */
                            ret.push_back(obj);

                            break;
                        }

                        case TAO::Operation::OP::CLAIM:
                        {
                            /* Jump to proper sigchain. */
                            tx[nContract] >> hashLast;

                            break;
                        }

                        /* Get old owner from transfer. */
                        case TAO::Operation::OP::TRANSFER:
                        {
                             /* Generate return object. */
                            json::json obj;
                            obj["OP"]         = "TRANSFER";
                            obj["owner"]      = tx.hashGenesis.ToString();
                            obj["updated"]  = state.nTimestamp;

                            /* Reset read position. */
                            state.nReadPos = 0;

                            /* Grab the last state. */
                            while(!state.end())
                            {
                                /* If the data type is string. */
                                std::string data;
                                state >> data;

                                obj["checksum"] = state.hashChecksum;
                                obj["data"]    = data;
                            }

                            /* Push to return array. */
                            ret.push_back(obj);

                            break;
                        }

                        default:
                            continue;
                    }
                }
            }

            return ret;
        }
    }
}
