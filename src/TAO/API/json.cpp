/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <TAO/API/include/json.h>
#include <TAO/API/include/get.h>

#include <Legacy/include/evaluate.h>
#include <Legacy/include/money.h>
#include <Legacy/types/address.h>
#include <Legacy/types/trustkey.h>

#include <TAO/API/include/constants.h>
#include <TAO/API/include/format.h>
#include <TAO/API/types/exception.h>
#include <TAO/API/types/commands.h>

#include <TAO/API/users/types/users.h>
#include <TAO/API/names/types/names.h>

#include <TAO/Ledger/include/constants.h>
#include <TAO/Ledger/include/chainstate.h>
#include <TAO/Ledger/include/difficulty.h>
#include <TAO/Ledger/include/timelocks.h>
#include <TAO/Ledger/types/tritium.h>
#include <TAO/Ledger/types/mempool.h>

#include <TAO/Operation/include/enum.h>
#include <TAO/Operation/include/create.h>
#include <TAO/Operation/types/contract.h>

#include <TAO/Register/include/unpack.h>

#include <Util/include/args.h>
#include <Util/include/convert.h>
#include <Util/include/debug.h>
#include <Util/include/hex.h>
#include <Util/include/json.h>
#include <Util/include/base64.h>
#include <Util/include/string.h>
#include <Util/include/math.h>

#include <LLD/include/global.h>

/* Global TAO namespace. */
namespace TAO::API
{
    /* Converts the block to formatted JSON */
    encoding::json BlockToJSON(const TAO::Ledger::BlockState& block, const uint32_t nVerbose,
                           const std::map<std::string, std::vector<Clause>>& vWhere)
    {
        /* Decalre the response object*/
        encoding::json result;

        /* Main block hash. */
        result["hash"] = block.GetHash().GetHex();

        /* The hash that was relevant for Proof of Stake or Proof of Work (depending on block version) */
        result["proofhash"]  =
                                block.nVersion < 5 ? block.GetHash().GetHex() :
                                ((block.nChannel == 0) ? block.StakeHash().GetHex() : block.ProofHash().GetHex());

        /* Body of the block with relevant data. */
        result["size"]       = (uint32_t)::GetSerializeSize(block, SER_NETWORK, LLP::PROTOCOL_VERSION);
        result["height"]     = (uint32_t)block.nHeight;
        result["channel"]    = (uint32_t)block.nChannel;
        result["version"]    = (uint32_t)block.nVersion;
        result["merkleroot"] = block.hashMerkleRoot.GetHex();
        result["time"]       = convert::DateTimeStrFormat(block.GetBlockTime());
        result["nonce"]      = (uint64_t)block.nNonce;
        result["bits"]       = HexBits(block.nBits);
        result["difficulty"] = TAO::Ledger::GetDifficulty(block.nBits, block.nChannel);
        result["mint"]       = Legacy::SatoshisToAmount(block.nMint);

        /* Add previous block if not null. */
        if(block.hashPrevBlock != 0)
            result["previousblockhash"] = block.hashPrevBlock.GetHex();

        /* Add next hash if not null. */
        if(block.hashNextBlock != 0)
            result["nextblockhash"] = block.hashNextBlock.GetHex();

        /* Add the transaction data if the caller has requested it*/
        if(nVerbose > 0)
        {
            encoding::json txinfo = encoding::json::array();

            /* Iterate through each transaction hash in the block vtx*/
            for(const auto& vtx : block.vtx)
            {
                if(vtx.first == TAO::Ledger::TRANSACTION::TRITIUM)
                {
                    /* Get the tritium transaction from the database*/
                    TAO::Ledger::Transaction tx;
                    if(LLD::Ledger->ReadTx(vtx.second, tx))
                    {
                        /* add the transaction JSON.  */
                        encoding::json jRet = TransactionToJSON(0, tx, block, nVerbose, 0, vWhere);

                        /* Only add the transaction if it has not been filtered out */
                        if(!jRet.empty())
                            txinfo.push_back(jRet);
                    }
                }
                else if(vtx.first == TAO::Ledger::TRANSACTION::LEGACY)
                {
                    /* Get the legacy transaction from the database. */
                    Legacy::Transaction tx;
                    if(LLD::Legacy->ReadTx(vtx.second, tx))
                    {
                        /* add the transaction JSON.  */
                        encoding::json jRet = TransactionToJSON(tx, block, nVerbose, vWhere);

                        /* Only add the transaction if it has not been filtered out */
                        if(!jRet.empty())
                            txinfo.push_back(jRet);
                    }
                }
            }

            /* Check to see if any transactions were returned.  If not then return an empty tx array */
            if(!txinfo.empty())
                result["tx"] = txinfo;
            else
                result = encoding::json();

        }

        return result;
    }

    /* Converts the transaction to formatted JSON */
    encoding::json TransactionToJSON(const uint256_t& hashCaller, const TAO::Ledger::Transaction& tx,
                                 const TAO::Ledger::BlockState& block, const uint32_t nVerbose, const uint256_t& hashCoinbase,
                                 const std::map<std::string, std::vector<Clause>>& vWhere)
    {
        /* Declare JSON object to return */
        encoding::json jRet;

        /* Always add the transaction hash */
        jRet["txid"] = tx.GetHash().GetHex();

        /* Basic TX info for level 2 and up */
        if(nVerbose >= 2)
        {
            /* Build base transaction data. */
            jRet["type"]      = tx.TypeString();
            jRet["version"]   = tx.nVersion;
            jRet["sequence"]  = tx.nSequence;
            jRet["timestamp"] = tx.nTimestamp;
            jRet["blockhash"] = block.IsNull() ? "" : block.GetHash().GetHex();
            jRet["confirmations"] = block.IsNull() ? 0 : TAO::Ledger::ChainState::nBestHeight.load() - block.nHeight + 1;

            /* Genesis and hashes are verbose 3 and up. */
            if(nVerbose >= 3)
            {
                /* More sigchain level details. */
                jRet["genesis"]   = tx.hashGenesis.ToString();
                jRet["nexthash"]  = tx.hashNext.ToString();
                jRet["prevhash"]  = tx.hashPrevTx.ToString();

                /* The cryptographic data. */
                jRet["pubkey"]    = HexStr(tx.vchPubKey.begin(), tx.vchPubKey.end());
                jRet["signature"] = HexStr(tx.vchSig.begin(),    tx.vchSig.end());
            }

            /* Check to see if any contracts were returned.  If not then return an empty transaction */
            encoding::json jContracts = ContractsToJSON(hashCaller, tx, nVerbose, hashCoinbase, vWhere);
            if(jContracts.empty())
                return encoding::json();

            /* Add contracts to return json object. */
            jRet["contracts"] = jContracts;
        }

        return jRet;
    }

    /* Converts the transaction to formatted JSON */
    encoding::json TransactionToJSON(const Legacy::Transaction& tx, const TAO::Ledger::BlockState& block, const uint32_t nVerbose,
                                 const std::map<std::string, std::vector<Clause>>& vWhere)
    {
        /* Declare JSON object to return */
        encoding::json jRet;

        /* Always add the hash */
        jRet["txid"] = tx.GetHash().GetHex();

        /* Basic TX info for level 1 and up */
        if(nVerbose > 0)
        {
            jRet["type"] = tx.TypeString();
            jRet["timestamp"] = tx.nTime;
            jRet["amount"] = Legacy::SatoshisToAmount(tx.GetValueOut());
            jRet["blockhash"] = block.IsNull() ? "" : block.GetHash().GetHex();
            jRet["confirmations"] = block.IsNull() ? 0 : TAO::Ledger::ChainState::nBestHeight.load() - block.nHeight + 1;

            /* Don't add inputs for coinbase or coinstake transactions */
            if(!tx.IsCoinBase())
            {
                /* Declare the inputs JSON array */
                encoding::json inputs = encoding::json::array();

                /* Iterate through each input */
                for (uint32_t i = (uint32_t)tx.IsCoinStake(); i < tx.vin.size(); ++i)
                {
                    const Legacy::TxIn& txin = tx.vin[i];

                    encoding::json input;
                    bool fFound = false;

                    if(tx.nVersion >= 2 && txin.prevout.hash.GetType() == TAO::Ledger::TRITIUM)
                    {
                        /* Previous output likely a Tritium send-to-legacy contract. Check for that first. It is possible for
                         * an older legacy tx to collide with the tritium hash type, so if not found still must check legacy.
                         */
                        TAO::Ledger::Transaction txPrev;
                        if(LLD::Ledger->ReadTx(txin.prevout.hash, txPrev))
                        {
                            fFound = true;

                            if(txPrev.Size() < txin.prevout.n)
                                throw APIException(-87, "Invalid or unknown transaction");

                            const uint256_t hashCaller = txPrev[txin.prevout.n].Caller();

                            input = ContractToJSON(hashCaller, txPrev[txin.prevout.n], txin.prevout.n, nVerbose);
                            inputs.push_back(input);
                        }
                    }

                    if(!fFound)
                    {
                        /* Read the previous legacy transaction in order to get its outputs */
                        Legacy::Transaction txPrev;

                        if(LLD::Legacy->ReadTx(txin.prevout.hash, txPrev))
                        {
                            fFound = true;

                            /* Extract the Nexus Address. */
                            Legacy::NexusAddress address;
                            TAO::Register::Address hashRegister;

                            if(Legacy::ExtractAddress(txPrev.vout[txin.prevout.n].scriptPubKey, address))
                                input["address"] = address.ToString();

                            else if(Legacy::ExtractRegister(txPrev.vout[txin.prevout.n].scriptPubKey, hashRegister))
                                input["address"] = hashRegister.ToString();

                            else
                                throw APIException(-8, "Unable to Extract Input Address");

                            input["amount"] = (double) txPrev.vout[txin.prevout.n].nValue / TAO::Ledger::NXS_COIN;
                            inputs.push_back(input);
                        }
                    }

                    if(!fFound)
                            throw APIException(-7, "Invalid transaction id");
                }

                jRet["inputs"] = inputs;
            }

            /* Declare the output JSON array */
            encoding::json outputs = encoding::json::array();

            /* Iterate through each output */
            for(const Legacy::TxOut& txout : tx.vout)
            {
                encoding::json output;
                /* Extract the Nexus Address. */
                Legacy::NexusAddress address;
                TAO::Register::Address hashRegister;

                if(Legacy::ExtractAddress(txout.scriptPubKey, address))
                    output["address"] = address.ToString();

                else if(Legacy::ExtractRegister(txout.scriptPubKey, hashRegister))
                    output["address"] = hashRegister.ToString();

                else if(block.nHeight == 112283) // Special case for malformed block 112283 with bad output address
                    output["address"] = "";
                else
                    throw APIException(-8, "Unable to Extract Output Address");

                output["amount"] = (double) txout.nValue / TAO::Ledger::NXS_COIN;

                outputs.push_back(output);

            }
            jRet["outputs"] = outputs;
        }

        return jRet;
    }


    /* Converts a transaction object into a formatted JSON list of contracts bound to the transaction. */
    encoding::json ContractsToJSON(const uint256_t& hashCaller, const TAO::Ledger::Transaction &tx, const uint32_t nVerbose,
                               const uint256_t& hashCoinbase, const std::map<std::string, std::vector<Clause>>& vWhere)
    {
        /* Declare the return JSON object*/
        encoding::json jRet = encoding::json::array();

        /* Add a contract to the list of contracts. */
        const uint32_t nContracts = tx.Size();
        for(uint32_t nContract = 0; nContract < nContracts; ++nContract)
        {
            /* Grab a reference of our contract. */
            const TAO::Operation::Contract& contract = tx[nContract];

            /* If the caller has requested to filter the coinbases then we only include those where the coinbase is meant for hashCoinbase  */
            if(hashCoinbase != 0)
            {
                /* Unpack to read coinbase operation data. */
                if(TAO::Register::Unpack(contract, TAO::Operation::OP::COINBASE))
                {
                    /* Unpack the owner from the contract */
                    uint256_t hashProof = 0;
                    TAO::Register::Unpack(contract, hashProof);

                    /* Skip this contract if the proof is not the hashCoinbase */
                    if(hashProof != hashCoinbase)
                        continue;
                }
            }

            /* Add the contract to the array */
            jRet.push_back
            (
                ContractToJSON(hashCaller, contract, nContract, nVerbose)
            );
        }

        return jRet;
    }


    /* Converts a serialized operation stream to formattted JSON */
    encoding::json ContractToJSON(const uint256_t& hashCaller, const TAO::Operation::Contract& contract, uint32_t nContract, uint32_t nVerbose)
    {
        /* Declare the return JSON object*/
        encoding::json jRet;

        /* Add the id */
        jRet["id"] = nContract;

        /* Reset all streams */
        contract.Reset();

        /* Seek the contract operation stream to the position of the primitive. */
        contract.SeekToPrimitive();

        /* Make sure no exceptions are thrown. */
        try
        {

            /* Get the contract operations. */
            uint8_t OPERATION = 0;
            contract >> OPERATION;

            /* Check the current opcode. */
            switch(OPERATION)
            {
                /* Record a new state to the register. */
                case TAO::Operation::OP::WRITE:
                {
                    /* Get the Address of the Register. */
                    TAO::Register::Address hashAddress;
                    contract >> hashAddress;

                    /* Deserialize the register from contract. */
                    std::vector<uint8_t> vchData;
                    contract >> vchData;

                    /* Output the json information. */
                    jRet["OP"]      = "WRITE";
                    jRet["address"] = hashAddress.ToString();
                    jRet["data"]    = HexStr(vchData.begin(), vchData.end());

                    break;
                }


                /* Append a new state to the register. */
                case TAO::Operation::OP::APPEND:
                {
                    /* Get the Address of the Register. */
                    TAO::Register::Address hashAddress;
                    contract >> hashAddress;

                    /* Deserialize the register from contract. */
                    std::vector<uint8_t> vchData;
                    contract >> vchData;

                    /* Output the json information. */
                    jRet["OP"]      = "APPEND";
                    jRet["address"] = hashAddress.ToString();
                    jRet["data"]    = HexStr(vchData.begin(), vchData.end());

                    break;
                }


                /* Create a new register. */
                case TAO::Operation::OP::CREATE:
                {
                    /* Extract the address from the contract. */
                    TAO::Register::Address hashAddress;
                    contract >> hashAddress;

                    /* Extract the register type from contract. */
                    uint8_t nType = 0;
                    contract >> nType;

                    /* Extract the register data from the contract. */
                    std::vector<uint8_t> vchData;
                    contract >> vchData;

                    /* Output the json information. */
                    jRet["OP"]      = "CREATE";
                    jRet["address"] = hashAddress.ToString();
                    jRet["type"]    = GetRegisterType(nType);

                    /* If this is a register object then decode the object type */
                    if(nType == TAO::Register::REGISTER::OBJECT)
                    {
                        /* Create the register object. */
                        TAO::Register::Object state;
                        state.nVersion   = 1;
                        state.nType      = nType;
                        state.hashOwner  = contract.Caller();

                        /* Calculate the new operation. */
                        if(!TAO::Operation::Create::Execute(state, vchData, contract.Timestamp()))
                            throw APIException(-110, "Contract execution failed");

                        /* parse object so that the data fields can be accessed */
                        if(!state.Parse())
                            throw APIException(-36, "Failed to parse object register");

                        jRet["object_type"] = GetObjectType(state.Standard());
                    }

                    jRet["data"]    = HexStr(vchData.begin(), vchData.end());

                    break;
                }


                /* Transfer ownership of a register to another signature chain. */
                case TAO::Operation::OP::TRANSFER:
                {
                    /* Extract the address from the contract. */
                    TAO::Register::Address hashAddress;
                    contract >> hashAddress;

                    /* Read the register transfer recipient. */
                    TAO::Register::Address hashTransfer;
                    contract >> hashTransfer;

                    /* Get the flag byte. */
                    uint8_t nType = 0;
                    contract >> nType;

                    /* Output the json information. */
                    jRet["OP"]       = "TRANSFER";
                    jRet["address"]  = hashAddress.ToString();
                    jRet["destination"] = hashTransfer.ToString();

                    /* If this is a register object then decode the object type */
                    if(nType == TAO::Register::REGISTER::OBJECT)
                    {
                        TAO::Register::Object object;
                        if(LLD::Register->ReadState(hashAddress, object, TAO::Ledger::FLAGS::MEMPOOL))
                        {
                            /* parse object so that the data fields can be accessed */
                            if(!object.Parse())
                                throw APIException(-36, "Failed to parse object register");

                            jRet["object_type"] = GetObjectType(object.Standard());
                        }
                    }

                    jRet["force"]    = nType == TAO::Operation::TRANSFER::FORCE;

                    break;
                }


                /* Claim ownership of a register to another signature chain. */
                case TAO::Operation::OP::CLAIM:
                {
                    /* Extract the transaction from contract. */
                    uint512_t hashTx = 0;
                    contract >> hashTx;

                    /* Extract the contract-id. */
                    uint32_t nContract = 0;
                    contract >> nContract;

                    /* Extract the address from the contract. */
                    TAO::Register::Address hashAddress;
                    contract >> hashAddress;

                    /* Output the json information. */
                    jRet["OP"]         = "CLAIM";
                    jRet["txid"]       = hashTx.ToString();
                    jRet["contract"]   = nContract;
                    jRet["address"]    = hashAddress.ToString();


                    break;
                }


                /* Coinbase operation. Creates an account if none exists. */
                case TAO::Operation::OP::COINBASE:
                {
                    /* Get the genesis. */
                    uint256_t hashGenesis = 0;
                    contract >> hashGenesis;

                    /* The total to be credited. */
                    uint64_t nCredit = 0;
                    contract >> nCredit;

                    /* The extra nNonce available in script. */
                    uint64_t nExtraNonce = 0;
                    contract >> nExtraNonce;

                    /* Output the json information. */
                    jRet["OP"]      = "COINBASE";
                    jRet["genesis"] = hashGenesis.ToString();
                    jRet["nonce"]   = nExtraNonce;
                    jRet["amount"]  = (double) nCredit / TAO::Ledger::NXS_COIN;;

                    if(nVerbose > 0)
                    {
                        uint32_t nConfirms = 0;
                        LLD::Ledger->ReadConfirmations(contract.Hash(), nConfirms);
                        jRet["confirms"] = nConfirms;
                    }

                    break;
                }


                /* Trust operation. Builds trust and generates reward. */
                case TAO::Operation::OP::TRUST:
                {
                    /* Get the last stake tx hash. */
                    uint512_t hashLastTrust = 0;
                    contract >> hashLastTrust;

                    /* The total trust score. */
                    uint64_t nScore = 0;
                    contract >> nScore;

                    /* Change to stake amount. */
                    int64_t nStakeChange = 0;
                    contract >> nStakeChange;

                    /* The trust reward. */
                    uint64_t nReward = 0;
                    contract >> nReward;

                    TAO::Register::Address address("trust", contract.Caller(), TAO::Register::Address::TRUST);

                    /* Output the json information. */
                    jRet["OP"]     = "TRUST";
                    jRet["address"] = address.ToString();
                    jRet["last"]   = hashLastTrust.ToString();
                    jRet["score"]  = nScore;
                    jRet["amount"] = (double) nReward / TAO::Ledger::NXS_COIN;

                    if(nStakeChange > 0)
                        jRet["add_stake"] = (double) nStakeChange / TAO::Ledger::NXS_COIN;

                    else if (nStakeChange < 0)
                        jRet["unstake"] = (double) (0 - nStakeChange) / TAO::Ledger::NXS_COIN;

                    break;
                }


                /* Genesis operation. Begins trust and stakes. */
                case TAO::Operation::OP::GENESIS:
                {
                    /* The genesis reward. */
                    uint64_t nReward = 0;
                    contract >> nReward;

                    TAO::Register::Address address("trust", contract.Caller(), TAO::Register::Address::TRUST);

                    /* Output the json information. */
                    jRet["OP"]        = "GENESIS";
                    jRet["address"]   = address.ToString();
                    jRet["amount"]    = (double) nReward / TAO::Ledger::NXS_COIN;;

                    break;
                }


                /* Debit tokens from an account you own. */
                case TAO::Operation::OP::DEBIT:
                {
                    /* Get the register address. */
                    TAO::Register::Address hashFrom;
                    contract >> hashFrom;

                    /* Get the transfer address. */
                    TAO::Register::Address hashTo;
                    contract >> hashTo;

                    /* Get the transfer amount. */
                    uint64_t  nAmount = 0;
                    contract >> nAmount;

                    /* Get the reference */
                    uint64_t nReference = 0;
                    contract >> nReference;

                    /* Output the json information. */
                    jRet["OP"]       = "DEBIT";
                    jRet["from"]     = hashFrom.ToString();

                    /* Resolve the name of the token/account that the debit is from */
                    std::string strFrom = Names::ResolveName(hashCaller, hashFrom);
                    if(!strFrom.empty())
                        jRet["from_name"] = strFrom;

                    jRet["to"]       = hashTo.ToString();

                    /* Resolve the name of the token/account/register that the debit is to */
                    std::string strTo = Names::ResolveName(hashCaller, hashTo);
                    if(!strTo.empty())
                        jRet["to_name"] = strTo;

                    /* Get the token/account we are debiting from so that we can output the token address / name. */
                    TAO::Register::Object object;
                    if(!LLD::Register->ReadState(hashFrom, object))
                        throw APIException(-13, "Object not found");

                    /* Parse the object register. */
                    if(!object.Parse())
                        throw APIException(-14, "Object failed to parse");

                    /* Add the amount to the response */
                    jRet["amount"]  = (double) nAmount / pow(10, GetDecimals(object));

                    /* Add the reference to the response */
                    jRet["reference"] = nReference;

                    /* Get the object standard. */
                    uint8_t nStandard = object.Standard();

                    /* Check the object standard. */
                    if(nStandard != TAO::Register::OBJECTS::ACCOUNT
                    && nStandard != TAO::Register::OBJECTS::TRUST
                    && nStandard != TAO::Register::OBJECTS::TOKEN)
                        throw APIException(-15, "Object is not an account or token");

                    /* Get the token address */
                    TAO::Register::Address hashToken = object.get<uint256_t>("token");

                    /* Add the token address to the response */
                    jRet["token"]   = hashToken.ToString();

                    /* Resolve the name of the token name */
                    std::string strToken = hashToken != 0 ? Names::ResolveName(hashCaller, hashToken) : "NXS";
                    if(!strToken.empty())
                        jRet["token_name"] = strToken;


                    break;
                }


                /* Credit tokens to an account you own. */
                case TAO::Operation::OP::CREDIT:
                {
                    /* Extract the transaction from contract. */
                    uint512_t hashTx = 0;
                    contract >> hashTx;

                    /* Extract the contract-id. */
                    uint32_t nID = 0;
                    contract >> nID;

                    /* Get the account address. */
                    TAO::Register::Address hashAddress;
                    contract >> hashAddress;

                    /* Get the proof address. */
                    TAO::Register::Address hashProof;
                    contract >> hashProof;

                    /* Get the transfer amount. */
                    uint64_t  nCredit = 0;
                    contract >> nCredit;

                    /* Output the json information. */
                    jRet["OP"]      = "CREDIT";

                    /* Determine the transaction type that this credit is made for */
                    std::string strInput;
                    if(hashTx.GetType() == TAO::Ledger::LEGACY)
                        strInput = "LEGACY";
                    else if(hashProof.IsAccount() || hashProof.IsToken() || hashProof.IsTrust())
                        strInput = "DEBIT";
                    else
                        strInput = "COINBASE";

                    jRet["for"]      = strInput;

                    jRet["txid"]     = hashTx.ToString();
                    jRet["contract"] = nID;
                    jRet["proof"]    = hashProof.ToString();
                    jRet["to"]       = hashAddress.ToString();

                    /* Resolve the name of the account that the credit is to */
                    std::string strAccount = Names::ResolveName(hashCaller, hashAddress);
                    if(!strAccount.empty())
                        jRet["to_name"] = strAccount;

                    /* Get the token/account we are crediting to so that we can output the token address / name. */
                    TAO::Register::Object account;
                    if(!LLD::Register->ReadState(hashAddress, account))
                        throw APIException(-13, "Object not found");

                    /* Parse the object register. */
                    if(!account.Parse())
                        throw APIException(-14, "Object failed to parse");

                    /* Add the amount to the response */
                    jRet["amount"]  = (double) nCredit / pow(10, GetDecimals(account));

                    /* Get the object standard. */
                    uint8_t nStandard = account.Standard();

                    /* Check the object standard. */
                    if(nStandard != TAO::Register::OBJECTS::ACCOUNT
                    && nStandard != TAO::Register::OBJECTS::TRUST
                    && nStandard != TAO::Register::OBJECTS::TOKEN)
                        throw APIException(-15, "Object is not an account or token");

                    /* Get the token address */
                    TAO::Register::Address hashToken = account.get<uint256_t>("token");

                    /* Add the token address to the response */
                    jRet["token"]   = hashToken.ToString();

                    /* Resolve the name of the token name */
                    std::string strToken = hashToken != 0 ? Names::ResolveName(hashCaller, hashToken) : "NXS";
                    if(!strToken.empty())
                        jRet["token_name"] = strToken;

                    /* Check type transaction that was credited */
                    if(hashTx.GetType() == TAO::Ledger::TRITIUM)
                    {
                        /* The debit transaction being credited */
                        TAO::Ledger::Transaction txDebit;

                        /* Read the corresponding debit/coinbase transaction */
                        if(LLD::Ledger->ReadTx(hashTx, txDebit))
                        {
                            /* Get the contract. */
                            const TAO::Operation::Contract& debitContract = txDebit[nID];

                            /* Only add reference if the credit is for a debit (rather than a coinbase) */
                            if(TAO::Register::Unpack(debitContract, TAO::Operation::OP::DEBIT))
                            {
                                /* Get the address the debit came from */
                                TAO::Register::Address hashFrom;
                                TAO::Register::Unpack(debitContract, hashFrom);

                                jRet["from"]     = hashFrom.ToString();

                                /* Resolve the name of the token/account that the debit is from */
                                std::string strFrom = Names::ResolveName(hashCaller, hashFrom);
                                if(!strFrom.empty())
                                    jRet["from_name"] = strFrom;

                                /* Reset the operation stream position in case it was loaded from mempool and therefore still in previous state */
                                debitContract.SeekToPrimitive();

                                /* Seek to reference. */
                                debitContract.Seek(73);

                                /* The reference */
                                uint64_t nReference = 0;
                                debitContract >> nReference;

                                jRet["reference"] = nReference;
                            }
                        }

                    }

                    break;
                }


                /* Migrate a trust key to a trust account register. */
                case TAO::Operation::OP::MIGRATE:
                {
                    /* Extract the transaction from contract. */
                    uint512_t hashTx;
                    contract >> hashTx;

                    /* Get the trust register address. (hash to) */
                    TAO::Register::Address hashAccount;
                    contract >> hashAccount;

                    /* Get thekey for the  Legacy trust key */
                    uint576_t hashTrust;
                    contract >> hashTrust;

                    /* Get the amount to migrate. */
                    uint64_t nAmount;
                    contract >> nAmount;

                    /* Get the trust score to migrate. */
                    uint32_t nScore;
                    contract >> nScore;

                    /* Get the hash last stake. */
                    uint512_t hashLast;
                    contract >> hashLast;

                    /* Output the json information. */
                    jRet["OP"]      = "MIGRATE";
                    jRet["txid"]    = hashTx.ToString();
                    jRet["account"]  = hashAccount.ToString();

                    /* Resolve the name of the account that the credit is to */
                    std::string strAccount = Names::ResolveName(hashCaller, hashAccount);
                    if(!strAccount.empty())
                        jRet["account_name"] = strAccount;

                    Legacy::TrustKey trustKey;
                    if(LLD::Trust->ReadTrustKey(hashTrust, trustKey))
                    {
                        Legacy::NexusAddress address;
                        address.SetPubKey(trustKey.vchPubKey);
                        jRet["trustkey"] = address.ToString();
                    }

                    jRet["amount"] = (double) nAmount / TAO::Ledger::NXS_COIN;
                    jRet["score"] = nScore;
                    jRet["hashLast"] = hashLast.ToString();

                    break;
                }


                /* Authorize a transaction to happen with a temporal proof. */
                case TAO::Operation::OP::AUTHORIZE:
                {
                    /* The transaction that you are authorizing. */
                    uint512_t hashTx = 0;
                    contract >> hashTx;

                    /* The proof you are using that you have rights. */
                    uint256_t hashProof = 0;
                    contract >> hashProof;

                    /* Output the json information. */
                    jRet["OP"]    = "AUTHORIZE";
                    jRet["txid"]  = hashTx.ToString();
                    jRet["proof"] = hashProof.ToString();

                    break;
                }

                case TAO::Operation::OP::FEE:
                {
                    /* Get the address of the account the fee came from. */
                    TAO::Register::Address hashAccount;
                    contract >> hashAccount;

                    /* The fee amount. */
                    uint64_t nFee = 0;
                    contract >> nFee;

                    /* Output the json information. */
                    jRet["OP"]      = "FEE";
                    jRet["from"] = hashAccount.ToString();

                    /* Resolve the name of the account that the credit is to */
                    std::string strAccount = Names::ResolveName(hashCaller, hashAccount);
                    if(!strAccount.empty())
                        jRet["from_name"] = strAccount;

                    jRet["amount"]  = (double) nFee / TAO::Ledger::NXS_COIN;

                    break;
                }

                /* Debit NXS to legacy address. */
                case TAO::Operation::OP::LEGACY:
                {
                    /* Get the register address. */
                    TAO::Register::Address hashFrom;
                    contract >> hashFrom;

                    /* Get the transfer amount. */
                    uint64_t  nAmount = 0;
                    contract >> nAmount;

                    /* Get the output script. */
                    Legacy::Script script;
                    contract >> script;

                    /* The receiving legacy address */
                    Legacy::NexusAddress legacyAddress;

                    /* Extract the receiving legacy address */
                    Legacy::ExtractAddress(script, legacyAddress);

                    /* Output the json information. */
                    jRet["OP"]       = "LEGACY";
                    jRet["from"]     = hashFrom.ToString();

                    /* Resolve the name of the token/account that the debit is from */
                    std::string strFrom = Names::ResolveName(hashCaller, hashFrom);
                    if(!strFrom.empty())
                        jRet["from_name"] = strFrom;

                    jRet["to"]       = legacyAddress.ToString();

                    /* Get the token/account we are debiting from so that we can output the token address / name. */
                    TAO::Register::Object object;
                    if(!LLD::Register->ReadState(hashFrom, object))
                        throw APIException(-13, "Object not found");

                    /* Parse the object register. */
                    if(!object.Parse())
                        throw APIException(-14, "Object failed to parse");

                    /* Add the amount to the response */
                    jRet["amount"]  = (double) nAmount / pow(10, GetDecimals(object));

                    /* Get the object standard. */
                    uint8_t nStandard = object.Standard();

                    /* Check the object standard. */
                    if(nStandard != TAO::Register::OBJECTS::ACCOUNT
                    && nStandard != TAO::Register::OBJECTS::TRUST
                    && nStandard != TAO::Register::OBJECTS::TOKEN)
                        throw APIException(-15, "Object is not an account or token");

                    /* Get the token address */
                    TAO::Register::Address hashToken = object.get<uint256_t>("token");

                    /* Add the token address to the response */
                    jRet["token"]   = hashToken.ToString();
                    jRet["token_name"] = "NXS";


                    break;
                }
            }
        }
        catch(const std::exception& e)
        {
            debug::error(FUNCTION, e.what());
        }

        return jRet;
    }


    /** ObjectToJSON
     *
     *  Converts an Object Register to formattted JSON
     *
     *  @param[in] object The Object Register to convert
     *
     *  @return the formatted JSON object
     *
     **/
    encoding::json ObjectToJSON(const TAO::Register::Object& object)
    {
        /* Add the register owner */
        encoding::json jRet;
        jRet["owner"]    = TAO::Register::Address(object.hashOwner).ToString();
        jRet["created"]  = object.nCreated;
        jRet["modified"] = object.nModified;

        /* Declare type and data variables for unpacking the Object fields */
        const std::vector<std::string> vFieldNames = object.ListFields();
        for(const auto& strName : vFieldNames)
        {
            /* First get the type */
            uint8_t nType = 0;
            object.Type(strName, nType);

            /* Switch based on type. */
            switch(nType)
            {
                /* Check for uint8_t type. */
                case TAO::Register::TYPES::UINT8_T:
                {
                    /* Set the return value from object register data. */
                    jRet[strName] = object.get<uint8_t>(strName);

                    break;
                }

                /* Check for uint16_t type. */
                case TAO::Register::TYPES::UINT16_T:
                {
                    /* Set the return value from object register data. */
                    jRet[strName] = object.get<uint16_t>(strName);

                    break;
                }

                /* Check for uint32_t type. */
                case TAO::Register::TYPES::UINT32_T:
                {
                    /* Set the return value from object register data. */
                    jRet[strName] = object.get<uint32_t>(strName);

                    break;
                }

                /* Check for uint64_t type. */
                case TAO::Register::TYPES::UINT64_T:
                {
                    /* Grab a copy of our object register value. */
                    const uint64_t nValue = object.get<uint64_t>(strName);

                    /* Specific rule for balance. */
                    if(strName == "balance" || strName == "stake" || strName == "supply")
                    {
                        /* Handle decimals conversion. */
                        const uint8_t nDecimals = GetDecimals(object);

                        /* Special rule for supply. */
                        if(strName == "supply")
                        {
                            /* Find our current supply. */
                            const uint64_t nCurrent = (object.get<uint64_t>("supply") - object.get<uint64_t>("balance"));

                            /* Populate json values. */
                            jRet["current_supply"] = FormatBalance(nCurrent, nDecimals);
                            jRet["max_supply"]     = FormatBalance(object.get<uint64_t>("supply"), nDecimals);
                        }
                        else
                            jRet[strName] = FormatBalance(nValue, nDecimals);
                    }
                    else if(strName == "trust")
                    {
                        /* Get our total counters. */
                        uint32_t nDays = 0, nHours = 0, nMinutes = 0;
                        convert::i64todays(nValue / 60, nDays, nHours, nMinutes);

                        /* Add string to trust. */
                        jRet["trust"]  = nValue;
                        jRet["times"]   = debug::safe_printstr(nDays, " days, ", nHours, " hours, ", nMinutes, " minutes");
                    }

                    /* Set the return value from object register data. */
                    else
                        jRet[strName] = nValue;

                    break;
                }

                /* Check for uint256_t type. */
                case TAO::Register::TYPES::UINT256_T:
                {
                    /* Grab a copy of our hash to check. */
                    const uint256_t hash = object.get<uint256_t>(strName);

                    /* Specific rule for token ticker. */
                    if(strName == "token")
                    {
                        /* Encode token in Base58 Encoding. */
                        jRet["token"] = TAO::Register::Address(hash).ToString();

                        /* Handle for NXS hardcoded token name. */
                        if(hash == TOKEN::NXS)
                            jRet["token_name"] = "NXS";
                    }

                    /* Specific rule for register address. */
                    else if(strName == "address")
                        jRet["register"] = TAO::Register::Address(hash).ToString();

                    /* Set the return value from object register data. */
                    else
                        jRet[strName] = hash.GetHex();

                    break;
                }

                /* Check for uint512_t type. */
                case TAO::Register::TYPES::UINT512_T:
                {
                    /* Set the return value from object register data. */
                    jRet[strName] = object.get<uint512_t>(strName).GetHex();

                    break;
                }


                /* Check for uint1024_t type. */
                case TAO::Register::TYPES::UINT1024_T:
                {
                    /* Set the return value from object register data. */
                    jRet[strName] = object.get<uint1024_t>(strName).GetHex();

                    break;
                }

                /* Check for string type. */
                case TAO::Register::TYPES::STRING:
                {
                    /* Remove trailing nulls from the data, which are padding to maxlength on mutable fields */
                    const std::string strRet = object.get<std::string>(strName);

                    /* Set the return value from object register data. */
                    jRet[strName] = strRet.substr(0, strRet.find_last_not_of('\0') + 1);

                    break;
                }

                /* Check for bytes type. */
                case TAO::Register::TYPES::BYTES:
                {
                    /* Set the return value from object register data. */
                    std::vector<uint8_t> vRet = object.get<std::vector<uint8_t>>(strName);

                    /* Remove trailing nulls from the data, which are padding to maxlength on mutable fields */
                    vRet.erase(std::find(vRet.begin(), vRet.end(), '\0'), vRet.end());

                    /* Add to return value in base64 encoding. */
                    jRet[strName] = encoding::EncodeBase64(&vRet[0], vRet.size()) ;

                    break;
                }
            }
        }

        /* Check some standard types to see if we want to add addresses. */
        const uint8_t nStandard = object.Standard();
        switch(nStandard)
        {
            /* Special handle for crypto object register. */
            case TAO::Register::OBJECTS::CRYPTO:
            {
                /* The register address */
                const TAO::Register::Address hashAddress =
                    TAO::Register::Address("crypto", object.hashOwner, TAO::Register::Address::TRUST);

                /* Populate stake related information. */
                jRet["address"] = hashAddress.ToString();

                break;
            }

            /* Special handle for trust object register. */
            case TAO::Register::OBJECTS::TRUST:
            {
                /* The register address */
                const TAO::Register::Address hashAddress =
                    TAO::Register::Address("trust", object.hashOwner, TAO::Register::Address::TRUST);

                /* Populate stake related information. */
                jRet["address"] = hashAddress.ToString();

                break;
            }

            /* Special handle for names and namespaces. */
            case TAO::Register::OBJECTS::NAME:
            case TAO::Register::OBJECTS::NAMESPACE:
            {
                /* Start with default namespace generated by user-id. */
                uint256_t hashNamespace = object.hashOwner;

                /* Check if object contains a namespace. */
                const std::string strNamespace = object.get<std::string>("namespace");
                if(!strNamespace.empty())
                {
                    /* Add our global flag to tell if it's global name. */
                    jRet["global"]    = (strNamespace == TAO::Register::NAMESPACE::GLOBAL);

                    /* Add our namespace hash if not global or local. */
                    if(!jRet["global"])
                        hashNamespace = TAO::Register::Address(strNamespace, TAO::Register::Address::NAMESPACE);
                }

                /* Generate namespace object's address. */
                if(nStandard == TAO::Register::OBJECTS::NAMESPACE)
                    jRet["address"] = TAO::Register::Address(hashNamespace).ToString();

                /* Generate name object's address. */
                if(nStandard == TAO::Register::OBJECTS::NAME)
                    jRet["address"] = TAO::Register::Address(object.get<std::string>("name"), hashNamespace, TAO::Register::Address::NAME).ToString();

                break;
            }
        }

        return jRet;
    }


    /* Converts an Object Register to formattted JSON */
    encoding::json ObjectToJSON(const encoding::json& params,
                            const TAO::Register::Object& object,
                            const TAO::Register::Address& hashRegister,
                            bool fLookupName /*= true*/)
    {
        /* Declare the return JSON object */
        encoding::json jRet;

        /* Get callers hashGenesis . */
        uint256_t hashGenesis = Commands::Get<Users>()->GetCallersGenesis(params);

        /* The resolved name for this object */
        std::string strName = "";

        /* If the caller has specified to look up the name */
        if(fLookupName)
        {
            /* Look up the object name based on the Name records in the caller's sig chain */
            strName = Names::ResolveName(hashGenesis, hashRegister);

            /* Add the name to the response if one is found. */
            if(!strName.empty())
                jRet["name"] = strName;
        }

        /* Now build the response based on the register type */
        if(object.nType == TAO::Register::REGISTER::APPEND
        || object.nType == TAO::Register::REGISTER::RAW
        || object.nType == TAO::Register::REGISTER::READONLY)
        {
            /* Raw state assets only have one data member containing the raw hex-encoded data*/
            jRet["address"]    = hashRegister.ToString();
            jRet["created"]    = object.nCreated;
            jRet["modified"]   = object.nModified;
            jRet["owner"]      = TAO::Register::Address(object.hashOwner).ToString();

            /* If this is an append register we need to grab the data from the end of the stream which will be the most recent data */
            while(!object.end())
            {
                /* Deserialize the leading byte of the state data to check the data type */
                uint16_t type;
                object >> type;

                /* If the data type is string. */
                std::string data;
                object >> data;

                //jRet["checksum"] = state.hashChecksum;
                jRet["data"] = data;
            }
        }
        else if(object.nType == TAO::Register::REGISTER::OBJECT)
        {
            /* Get the object standard. */
            uint8_t nStandard = object.Standard();
            switch(nStandard)
            {
                /* Handle for a regular account. */
                case TAO::Register::OBJECTS::ACCOUNT:
                case TAO::Register::OBJECTS::TRUST:
                {
                    jRet["address"]    = hashRegister.ToString();

                    /* Get the token names. */ //XXX: this is disabled as it is another O(n^2) algorithm for every object
                    //std::string strTokenName = Names::ResolveAccountTokenName(params, object);
                    //if(!strTokenName.empty())
                    //    jRet["token_name"] = strTokenName;

                    /* Handle digit conversion. */
                    const uint8_t  nDecimals = GetDecimals(object);

                    /* Get disk state so we can find unconfirmed. */
                    TAO::Register::Object objDisk;
                    if(LLD::Register->ReadObject(hashRegister, objDisk, TAO::Ledger::FLAGS::BLOCK))
                    {
                        /* Get our balances to check against for unconfirmed balance. */
                        const uint64_t nMemBalance  = object.get<uint64_t>("balance");
                        const uint64_t nDiskBalance = objDisk.get<uint64_t>("balance");

                        /* Check if we have unconfired coins. */
                        if(nMemBalance > nDiskBalance)
                            jRet["unconfirmed"] = double(nMemBalance - nDiskBalance) / math::pow(10, nDecimals);

                        /* Set balance from disk. */
                        jRet["balance"]     = (double)nDiskBalance / math::pow(10, nDecimals);
                    }
                    else
                    {
                        /* Add our balance in case we don't have a disk state. */
                        jRet["balance"]         = (double)0;
                        jRet["unconfirmed"]     = (double)object.get<uint64_t>("balance") / math::pow(10, nDecimals);
                    }

                    /* Set the value to the token contract address. */
                    jRet["token"] = TAO::Register::Address(object.get<uint256_t>("token")).ToString();

                    /* If the register has extra data included then output that in the JSON */
                    if(object.Check("data"))
                        jRet["data"] = object.get<std::string>("data");

                    /* Add Trust specific fields */
                    if(object.Check("stake"))
                        jRet["stake"]    = (double)object.get<uint64_t>("stake") / math::pow(10, nDecimals);

                    break;
                }


                /* Handle for a token contract. */
                case TAO::Register::OBJECTS::TOKEN:
                {
                    /* Handle decimals conversion. */
                    const uint8_t nDecimals = GetDecimals(object);

                    /* Poplate primary token related data. */
                    jRet["balance"]          = (double) object.get<uint64_t>("balance") / math::pow(10, nDecimals);
                    jRet["maxsupply"]        = (double) object.get<uint64_t>("supply") / math::pow(10, nDecimals);
                    jRet["currentsupply"]    = (double) (object.get<uint64_t>("supply")
                                            - object.get<uint64_t>("balance")) / math::pow(10, nDecimals);

                    /* Populate secondary token related data. */
                    jRet["decimals"]         = nDecimals;
                    jRet["address"]          = hashRegister.ToString();

                    break;
                }


                /* Handle for a Name Object. */
                case TAO::Register::OBJECTS::NAME:
                {
                    /* Poplate initial name record values. */
                    jRet["address"]   = hashRegister.ToString();
                    jRet["name"]      = object.get<std::string>("name");

                    /* Flag if it is global to populate namespace field. */
                    jRet["global"]    = (jRet["namespace"] == TAO::Register::NAMESPACE::GLOBAL);
                    if(jRet["global"])
                        jRet["namespace"] = object.get<std::string>("namespace");;

                    //XXX: are we doing _ or not for multiple words? inconsistent formatting, needs to be consistent
                    jRet["register_address"] = TAO::Register::Address(object.get<uint256_t>("address")).ToString();

                    break;
                }


                /* Handle for a Name Object. */
                case TAO::Register::OBJECTS::NAMESPACE:
                {
                    jRet["address"]          = hashRegister.ToString();
                    jRet["name"]             = object.get<std::string>("namespace");

                    break;
                }

                /* Handle for all nonstandard object registers. */
                default:
                {
                    /* Get the owner of the object */
                    const TAO::Register::Address hashOwner = TAO::Register::Address(object.hashOwner);

                    /* Add the register address */
                    jRet["address"]  = hashRegister.ToString();
                    jRet["owner"]    = hashOwner.ToString();

                    /* If this is a tokenized asset then work out what % the caller owns */
                    if(hashOwner.IsToken())
                        jRet["ownership"] = GetTokenOwnership(hashOwner, hashGenesis);

                    /* Declare type and data variables for unpacking the Object fields */
                    std::vector<std::string> vFieldNames = object.ListFields();
                    for(const auto& strName : vFieldNames)
                    {
                        /* First get the type */
                        uint8_t nType = 0;
                        object.Type(strName, nType);

                        /* Switch based on type. */
                        switch(nType)
                        {
                            /* Check for uint8_t type. */
                            case TAO::Register::TYPES::UINT8_T:
                            {
                                /* Set the return value from object register data. */
                                jRet[strName] = object.get<uint8_t>(strName);

                                break;
                            }

                            /* Check for uint16_t type. */
                            case TAO::Register::TYPES::UINT16_T:
                            {
                                /* Set the return value from object register data. */
                                jRet[strName] = object.get<uint16_t>(strName);

                                break;
                            }

                            /* Check for uint32_t type. */
                            case TAO::Register::TYPES::UINT32_T:
                            {
                                /* Set the return value from object register data. */
                                jRet[strName] = object.get<uint32_t>(strName);

                                break;
                            }

                            /* Check for uint64_t type. */
                            case TAO::Register::TYPES::UINT64_T:
                            {
                                /* Set the return value from object register data. */
                                jRet[strName] = object.get<uint64_t>(strName);

                                break;
                            }

                            /* Check for uint256_t type. */
                            case TAO::Register::TYPES::UINT256_T:
                            {
                                /* Set the return value from object register data. */
                                jRet[strName] = object.get<uint256_t>(strName).GetHex();

                                break;
                            }

                            /* Check for uint512_t type. */
                            case TAO::Register::TYPES::UINT512_T:
                            {
                                /* Set the return value from object register data. */
                                jRet[strName] = object.get<uint512_t>(strName).GetHex();

                                break;
                            }

                            /* Check for uint1024_t type. */
                            case TAO::Register::TYPES::UINT1024_T:
                            {
                                /* Set the return value from object register data. */
                                jRet[strName] = object.get<uint1024_t>(strName).GetHex();

                                break;
                            }

                            /* Check for string type. */
                            case TAO::Register::TYPES::STRING:
                            {
                                /* Set the return value from object register data. */
                                std::string strRet = object.get<std::string>(strName);

                                /* Remove trailing nulls from the data, which are padding to maxlength on mutable fields */
                                jRet[strName] = strRet.substr(0, strRet.find_last_not_of('\0') + 1);

                                break;
                            }

                            /* Check for bytes type. */
                            case TAO::Register::TYPES::BYTES:
                            {
                                /* Set the return value from object register data. */
                                std::vector<uint8_t> vRet = object.get<std::vector<uint8_t>>(strName);

                                /* Remove trailing nulls from the data, which are padding to maxlength on mutable fields */
                                vRet.erase(std::find(vRet.begin(), vRet.end(), '\0'), vRet.end());

                                /* Add to return value in base64 encoding. */
                                jRet[strName] = encoding::EncodeBase64(&vRet[0], vRet.size()) ;

                                break;
                            }
                        }
                    }

                    break;
                }
            }
        }

        return jRet;
    }


    /* Recursive helper function for QueryToJSON to recursively generate JSON statements for use with filters. */
    encoding::json StatementToJSON(std::vector<std::string> &vWhere, uint32_t &nIndex, encoding::json &jStatement)
    {
        /* Check if we have consumed all of our clauses. */
        if(nIndex >= vWhere.size())
            return jStatement;

        /* Grab a reference of our working string. */
        std::string& strClause = vWhere[nIndex];

        /* Check if we are recursing up a level. */
        const auto nLeft = strClause.find("(");
        if(nLeft == 0)
        {
            /* Parse out substring removing paranthesis. */
            strClause = strClause.substr(nLeft + 1);

            /* Create a new group to recurse up a level. */
            encoding::json jGroup
            {
                { "logical", "NONE" },
                { "statement", encoding::json::array() },
            };

            /* We want to push this new group recursive field to current level. */
            jStatement["statement"].push_back(StatementToJSON(vWhere, nIndex, jGroup));

            /* Now we continue consuming our clauses to complete the statement. */
            return StatementToJSON(vWhere, ++nIndex, jStatement);
        }

        /* Check if we are recursing back down a level. */
        const auto nRight = strClause.rfind(")");
        if(nRight == strClause.length() - 1)
        {
            /* Parse out substring removing paranthesis. */
            strClause = strClause.substr(0, nRight);

            /* Check if we need to recurse another level still. */
            if(strClause.rfind(")") == strClause.length() - 1)
                return StatementToJSON(vWhere, nIndex, jStatement);

            /* Add our clause to end of statement. */
            jStatement["statement"].push_back(ClauseToJSON(strClause));

            return jStatement;
        }

        /* Check for logical statement. */
        if(strClause == "AND" || strClause == "OR")
        {
            /* Check for incorrect mixing of AND/OR. */
            if(jStatement.find("logical") == jStatement.end())
                throw APIException(-122, "Query Syntax Error: missing logical operator for group");

            /* Grab a copy of our current logical statement. */
            const std::string strLogical = jStatement["logical"].get<std::string>();
            if(strLogical != "NONE" && strLogical != strClause)
                throw APIException(-121, "Query Syntax Error, must use '(' and ')' to mix AND/OR statements");

            jStatement["logical"] = strClause;
            return StatementToJSON(vWhere, ++nIndex, jStatement);
        }

        /* Regular statement adding clause. */
        jStatement["statement"].push_back(ClauseToJSON(strClause));

        /* Regular recursion to move to next statement. */
        return StatementToJSON(vWhere, ++nIndex, jStatement);
    }


    /* Turns a where query string in url encoding into a formatted JSON object. */
    encoding::json QueryToJSON(const std::string& strWhere)
    {
        /* Split up our query by spaces. */
        std::vector<std::string> vWhere;
        ParseString(strWhere, ' ', vWhere);

        /* Build our return object. */
        encoding::json jRet
        {
            { "logical", "NONE" },
            { "statement", encoding::json::array() },
        };

        /* Recursively process the query. */
        uint32_t n = 0;
        jRet = StatementToJSON(vWhere, n, jRet);

        /* Check for logical operator. */
        //if(jRet["logical"].get<std::string>() == "NONE" && jRet["statement"].size() > 1)
        //    throw APIException(-120, "Query Syntax Error: missing logical operator for base group, too many '()' maybe?");

        return jRet;
    }


    /* Turns a where clause string in url encoding into a formatted JSON object. */
    encoding::json ClauseToJSON(const std::string& strClause)
    {
        /* Check for a set to compare. */
        const auto nBegin = strClause.find_first_of("!=<>");
        if(nBegin == strClause.npos)
            throw APIException(-120, "Query Syntax Error: must use <key>=<value> with no extra characters. ", strClause);

        /* Grab our current key. */
        const std::string strKey = strClause.substr(0, nBegin);

        /* Check for our incoming parameter. */
        const std::string::size_type nDot = strKey.find('.');
        if(nDot == strKey.npos)
            throw APIException(-60, "Query Syntax Error: malformed where clause at ", strKey);

        /* Build our current json value. */
        encoding::json jClause =
        {
            { "class", strKey.substr(0, nDot)  },
            { "field", strKey.substr(nDot + 1) },
        };

        /* Check if its < or =. */
        const auto nEnd = strClause.find_first_of("!=<>", nBegin + 1);
        if(nEnd != strClause.npos)
        {
            jClause["operator"] = strClause[nBegin] + std::string("") + strClause[nEnd];
            jClause["value"]    = strClause.substr(nBegin + 2);
        }

        /* It's just < if we reach here. */
        else
        {
            jClause["operator"] = strClause[nBegin] + std::string("");
            jClause["value"]    = strClause.substr(nBegin + 1);
        }

        /* Check for valid values and parameters. */
        if(jClause["value"].get<std::string>().empty())
            throw APIException(-120, "Query Syntax Error: must use <key>=<value> with no extra characters.");

        return jClause;
    }


    /* Turns a query string in url encoding into a formatted JSON object. */
    encoding::json ParamsToJSON(const std::vector<std::string>& vParams)
    {
        /* Track our where clause. */
        std::string strWhere;

        /* Get the parameters. */
        encoding::json jRet;
        for(uint32_t n = 0; n < vParams.size(); ++n)
        {
            /* Set et as reference of our parameter. */
            const std::string& strParam = vParams[n];

            /* Find the key/value delimiter. */
            const auto nPos = strParam.find("=");
            if(nPos != strParam.npos || strParam == "WHERE")
            {
                /* Check for key/value pairs. */
                std::string strKey, strValue;
                if(nPos != strParam.npos)
                {
                    /* Grab the key and value substrings. */
                    strKey   = strParam.substr(0, nPos);
                    strValue = strParam.substr(nPos + 1);

                    /* Check for empty value, due to ' ' or bad input. */
                    if(strValue.empty())
                        throw APIException(-120, "Query Syntax Error: must use <key>=<value> with no extra characters.");
                }

                /* Check for where clauses. */
                if(strParam == "WHERE" || strKey == "where")
                {
                    /* Check if we have operated before. */
                    if(!strWhere.empty()) //check for double WHERE
                        throw APIException(-60, "Query Syntax Error: malformed where clause at ", strValue);

                    /* If where as key/value, append the value we parsed out. */
                    if(strKey == "where")
                    {
                        /* Check that our where clause is a proper conditional statement. */
                        if(strValue.find_first_of("!=<>") == strValue.npos)
                            throw APIException(-60, "Query Syntax Error: malformed where clause at ", strValue);

                        strWhere += std::string(strValue);
                    }

                    /* Check if we have empty WHERE. */
                    if(n + 1 >= vParams.size() && strParam == "WHERE")
                        throw APIException(7, "Query Syntax Error: empty WHERE clause at [", strValue, "]");

                    /* Build a single where string for parsing. */
                    for(uint32_t i = n + 1; i < vParams.size(); ++i)
                    {
                        /* Check if we have operated before. */
                        if(vParams[i] == "WHERE" && strKey == "where") //check for double WHERE
                            throw APIException(-60, "Query Syntax Error: malformed where clause at ", strValue);

                        /* Append the string with remaining arguments. */
                        strWhere += vParams[i];

                        /* Add a space as delimiter. */
                        if(i < vParams.size() - 1)
                            strWhere += std::string(" ");
                    }

                    break;
                }

                /* Add to our return json. */
                else
                {
                    /* Parse if the parameter is a json object or array. */
                    if(strValue.find_first_of("{}") != strValue.npos || strValue.find_first_of("[]") != strValue.npos)
                    {
                        jRet[strKey] = encoding::json::parse(strValue);
                        continue;
                    }

                    /* Set our return value */
                    jRet[strKey] = strValue;
                }
            }
            else
                throw APIException(-120, "Query Syntax Error: must use <key>=<value> with no extra characters.");
        }

        /* Build our query string now. */
        if(!strWhere.empty())
            jRet["where"] = TAO::API::QueryToJSON(strWhere);

        return jRet;
    }
}
