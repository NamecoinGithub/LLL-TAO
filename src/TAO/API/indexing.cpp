/*__________________________________________________________________________________________

			Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

			(c) Copyright The Nexus Developers 2014 - 2023

			Distributed under the MIT software license, see the accompanying
			file COPYING or http://www.opensource.org/licenses/mit-license.php.

			"ad vocem populi" - To The Voice of The People

____________________________________________________________________________________________*/

#include <Legacy/include/evaluate.h>

#include <LLD/include/global.h>

#include <LLP/include/global.h>
#include <LLP/types/tritium.h>

#include <TAO/API/types/authentication.h>
#include <TAO/API/types/commands.h>
#include <TAO/API/types/indexing.h>
#include <TAO/API/types/transaction.h>

#include <TAO/Operation/include/enum.h>

#include <TAO/Ledger/include/chainstate.h>
#include <TAO/Ledger/include/constants.h>
#include <TAO/Ledger/types/mempool.h>
#include <TAO/Ledger/types/transaction.h>

#include <Util/include/mutex.h>
#include <Util/include/debug.h>
#include <Util/include/runtime.h>
#include <Util/include/string.h>

#include <functional>

/* Global TAO namespace. */
namespace TAO::API
{
    /* Queue to handle dispatch requests. */
    util::atomic::lock_unique_ptr<std::queue<uint512_t>> Indexing::DISPATCH;


    /* Thread for running dispatch. */
    std::thread Indexing::EVENTS_THREAD;


    /* Condition variable to wake up the indexing thread. */
    std::condition_variable Indexing::CONDITION;


    /* Queue to handle dispatch requests. */
    util::atomic::lock_unique_ptr<std::queue<uint256_t>> Indexing::INITIALIZE;


    /* Thread for running dispatch. */
    std::thread Indexing::INITIALIZE_THREAD;


    /* Condition variable to wake up the indexing thread. */
    std::condition_variable Indexing::INITIALIZE_CONDITION;


    /* Set to track active indexing entries. */
    std::set<std::string> Indexing::REGISTERED;


    /* Mutex around registration. */
    std::mutex Indexing::REGISTERED_MUTEX;


    /* Initializes the current indexing systems. */
    void Indexing::Initialize()
    {
        /* Read our list of active login sessions. */

        /* Initialize our thread objects now. */
        Indexing::DISPATCH      = util::atomic::lock_unique_ptr<std::queue<uint512_t>>(new std::queue<uint512_t>());
        Indexing::EVENTS_THREAD = std::thread(&Indexing::Manager);


        /* Initialize our indexing thread now. */
        Indexing::INITIALIZE    = util::atomic::lock_unique_ptr<std::queue<uint256_t>>(new std::queue<uint256_t>());
        Indexing::INITIALIZE_THREAD = std::thread(&Indexing::InitializeThread);
    }


    /* Initialize a user's indexing entries. */
    void Indexing::Initialize(const uint256_t& hashSession)
    {
        /* Add our genesis to initialize ordering. */
        INITIALIZE->push(hashSession);
        INITIALIZE_CONDITION.notify_all();
    }


    /* Checks current events against transaction history to ensure we are up to date. */
    void Indexing::RefreshEvents()
    {
        /* Check to disable for -client mode. */
        if(config::fClient.load())
            return;

        /* Our list of transactions to read. */
        std::map<uint512_t, TAO::Ledger::Transaction> mapTransactions;

        /* Start a timer to track. */
        runtime::timer timer;
        timer.Start();

        /* Check our starting block to read from. */
        uint1024_t hashBlock;

        /* Track the last block processed. */
        TAO::Ledger::BlockState tStateLast;

        /* Handle first key if needed. */
        uint512_t hashIndex;
        if(!LLD::Logical->ReadLastIndex(hashIndex))
        {
            /* Set our internal values. */
            hashBlock = TAO::Ledger::hashTritium;

            /* Check for testnet mode. */
            if(config::fTestNet.load())
                hashBlock = TAO::Ledger::hashGenesisTestnet;

            /* Check for hybrid mode. */
            if(config::fHybrid.load())
                LLD::Ledger->ReadHybridGenesis(hashBlock);

            /* Read the first tritium block. */
            TAO::Ledger::BlockState tCurrent;
            if(!LLD::Ledger->ReadBlock(hashBlock, tCurrent))
            {
                debug::warning(FUNCTION, "No tritium blocks available to initialize ", hashBlock.SubString());
                return;
            }

            /* Set our last block as prev tritium block. */
            if(!tCurrent.Prev())
                tStateLast = tCurrent;
            else
            {
                hashBlock  = tCurrent.hashPrevBlock;
                tStateLast = tCurrent.Prev();
            }

            debug::log(0, FUNCTION, "Initializing indexing at tx ", hashBlock.SubString(), " and height ", tCurrent.nHeight);
        }
        else
        {
            /* Set our initial block hash. */
            TAO::Ledger::BlockState tCurrent;
            if(LLD::Ledger->ReadBlock(hashIndex, tCurrent))
            {
                /* Set our last block hash. */
                hashBlock = tCurrent.hashPrevBlock;

                /* Set our last block as prev tritium block. */
                tStateLast = tCurrent.Prev();
            }
        }

        /* Keep track of our total count. */
        uint32_t nScannedCount = 0;

        /* Start our scan. */
        debug::log(0, FUNCTION, "Scanning from block ", hashBlock.SubString());

        /* Build our loop based on the blocks we have read sequentially. */
        std::vector<TAO::Ledger::BlockState> vStates;
        while(!config::fShutdown.load() && LLD::Ledger->BatchRead(hashBlock, "block", vStates, 1000, true))
        {
            /* Loop through all available states. */
            for(auto& state : vStates)
            {
                /* Update start every iteration. */
                hashBlock = state.GetHash();

                /* Skip if not in main chain. */
                if(!state.IsInMainChain())
                    continue;

                /* Check for matching hashes. */
                if(state.hashPrevBlock != tStateLast.GetHash())
                {
                    debug::log(0, FUNCTION, "Correcting chain ", tStateLast.hashNextBlock.SubString());

                    /* Read the correct block from next index. */
                    if(!LLD::Ledger->ReadBlock(tStateLast.hashNextBlock, state))
                    {
                        debug::log(0, FUNCTION, "Terminated scanning ", nScannedCount, " tx in ", timer.Elapsed(), " seconds");
                        return;
                    }

                    /* Update hashBlock. */
                    hashBlock = state.GetHash();
                }

                /* Cache the block hash. */
                tStateLast = state;

                /* Track our checkpoint by first transaction in non-processed block. */
                LLD::Logical->WriteLastIndex(state.vtx[0].second);

                /* Handle our transactions now. */
                for(const auto& proof : state.vtx)
                {
                    /* Skip over legacy indexes. */
                    if(proof.first == TAO::Ledger::TRANSACTION::LEGACY)
                        continue;

                    /* Check our map contains transactions. */
                    if(!mapTransactions.count(proof.second))
                    {
                        /* Read the next batch of inventory. */
                        std::vector<TAO::Ledger::Transaction> vList;
                        if(LLD::Ledger->BatchRead(proof.second, "tx", vList, 1000, false))
                        {
                            /* Add all of our values to a map. */
                            for(const auto& tBatch : vList)
                                mapTransactions[tBatch.GetHash()] = tBatch;
                        }
                    }

                    /* Check that we found it in batch. */
                    if(!mapTransactions.count(proof.second))
                    {
                        /* Track this warning since this should not happen. */
                        debug::warning(FUNCTION, "batch read for ", proof.second.SubString(), " did not find results");

                        /* Make sure we have the transaction. */
                        TAO::Ledger::Transaction tMissing;
                        if(LLD::Ledger->ReadTx(proof.second, tMissing))
                            mapTransactions[proof.second] = tMissing;
                        else
                        {
                            debug::warning(FUNCTION, "single read for ", proof.second.SubString(), " is missing");
                            continue;
                        }
                    }

                    /* Get our transaction now. */
                    const TAO::Ledger::Transaction& rTX =
                        mapTransactions[proof.second];

                    /* Iterate the transaction contracts. */
                    for(uint32_t nContract = 0; nContract < rTX.Size(); ++nContract)
                    {
                        /* Grab contract reference. */
                        const TAO::Operation::Contract& rContract = rTX[nContract];

                        {
                            LOCK(REGISTERED_MUTEX);

                            /* Loop through registered commands. */
                            for(const auto& strCommands : REGISTERED)
                                Commands::Instance(strCommands)->Index(rContract, nContract);
                        }
                    }

                    /* Delete processed transaction from memory. */
                    mapTransactions.erase(proof.second);

                    /* Update the scanned count for meters. */
                    ++nScannedCount;

                    /* Meter for output. */
                    if(nScannedCount % 100000 == 0)
                    {
                        /* Get the time it took to rescan. */
                        const uint32_t nElapsedSeconds = timer.Elapsed();
                        debug::log(0, FUNCTION, "Processed ", nScannedCount, " in ", nElapsedSeconds, " seconds from height ", state.nHeight, " (",
                            std::fixed, (double)(nScannedCount / (nElapsedSeconds > 0 ? nElapsedSeconds : 1 )), " tx/s)");
                    }
                }

                /* Check if we are ready to terminate. */
                if(hashBlock == TAO::Ledger::ChainState::hashBestChain.load())
                    break;
            }
        }

        debug::log(0, FUNCTION, "Complated scanning ", nScannedCount, " tx in ", timer.Elapsed(), " seconds");
    }


    /*  Index a new block hash to relay thread.*/
    void Indexing::PushTransaction(const uint512_t& hashTx)
    {
        DISPATCH->push(hashTx);
        CONDITION.notify_all();

        debug::log(3, FUNCTION, "Pushing ", hashTx.SubString(), " To Indexing Queue.");
    }


    /* Handle relays of all events for LLP when processing block. */
    void Indexing::Manager()
    {
        /* Refresh our events. */
        RefreshEvents();

        /* Main loop controlled by condition variable. */
        std::mutex CONDITION_MUTEX;
        while(!config::fShutdown.load())
        {
            /* Wait for entries in the queue. */
            std::unique_lock<std::mutex> CONDITION_LOCK(CONDITION_MUTEX);
            CONDITION.wait(CONDITION_LOCK,
            []
            {
                /* Check for shutdown. */
                if(config::fShutdown.load())
                    return true;

                /* Check for suspended state. */
                if(config::fSuspended.load())
                    return false;

                return Indexing::DISPATCH->size() != 0;
            });

            /* Check for shutdown. */
            if(config::fShutdown.load())
                return;

            /* Grab the next entry in the queue. */
            const uint512_t hashTx = DISPATCH->front();
            DISPATCH->pop();

            /* Fire off indexing now. */
            IndexSigchain(hashTx);

            /* Write our last index now. */
            LLD::Logical->WriteLastIndex(hashTx);
        }
    }


    /* Index tritium transaction level events for logged in sessions. */
    void Indexing::IndexSigchain(const uint512_t& hashTx)
    {
        /* Check if handling legacy or tritium. */
        if(hashTx.GetType() == TAO::Ledger::TRITIUM)
        {
            /* Make sure the transaction is on disk. */
            TAO::Ledger::Transaction tx;
            if(!LLD::Ledger->ReadTx(hashTx, tx))
            {
                debug::warning(FUNCTION, "Indexing Failed: could not find ", hashTx.SubString(), " on disk");
                return;
            }

            /* Build our local sigchain events indexes. */
            index_transaction(hashTx, tx);

            /* Iterate the transaction contracts. */
            for(uint32_t nContract = 0; nContract < tx.Size(); nContract++)
            {
                /* Grab contract reference. */
                const TAO::Operation::Contract& rContract = tx[nContract];

                {
                    LOCK(REGISTERED_MUTEX);

                    /* Loop through registered commands. */
                    for(const auto& strCommands : REGISTERED)
                        Commands::Instance(strCommands)->Index(rContract, nContract);
                }
            }
        }

        /* Check for legacy transaction type. */
        if(hashTx.GetType() == TAO::Ledger::LEGACY)
        {
            /* Make sure the transaction is on disk. */
            Legacy::Transaction tx;
            if(!LLD::Legacy->ReadTx(hashTx, tx))
                return;

            /* Loop thgrough the available outputs. */
            for(uint32_t nContract = 0; nContract < tx.vout.size(); nContract++)
            {
                /* Grab a reference of our output. */
                const Legacy::TxOut& txout = tx.vout[nContract];

                /* Extract our register address. */
                uint256_t hashTo;
                if(Legacy::ExtractRegister(txout.scriptPubKey, hashTo))
                {
                    /* Read the owner of register. (check this for MEMPOOL, too) */
                    TAO::Register::State state;
                    if(!LLD::Register->ReadState(hashTo, state, TAO::Ledger::FLAGS::LOOKUP))
                        continue;

                    /* Check if owner is authenticated. */
                    if(Authentication::Active(state.hashOwner))
                    {
                        /* Write our events to database. */
                        if(!LLD::Logical->PushEvent(state.hashOwner, hashTx, nContract))
                            continue;

                        /* Increment our sequence. */
                        if(!LLD::Logical->IncrementLegacySequence(state.hashOwner))
                            continue;
                    }
                }
            }
        }
    }


    /* Broadcast our unconfirmed transactions if there are any. */
    void Indexing::BroadcastUnconfirmed(const uint256_t& hashGenesis)
    {
        /* Build list of transaction hashes. */
        std::vector<uint512_t> vHashes;

        /* Read all transactions from our last index. */
        uint512_t hash;
        if(!LLD::Logical->ReadLast(hashGenesis, hash))
            return;

        /* Loop until we reach confirmed transaction. */
        while(!config::fShutdown.load())
        {
            /* Read the transaction from the ledger database. */
            TAO::API::Transaction tx;
            if(!LLD::Logical->ReadTx(hash, tx))
            {
                debug::warning(FUNCTION, "read for ", hashGenesis.SubString(), " failed at tx ", hash.SubString());
                break;
            }

            /* Check we have index to break. */
            if(LLD::Ledger->HasIndex(hash))
                break;

            /* Push transaction to list. */
            vHashes.push_back(hash); //this will warm up the LLD cache if available, or remain low footprint if not

            /* Check for first. */
            if(tx.IsFirst())
                break;

            /* Set hash to previous hash. */
            hash = tx.hashPrevTx;
        }

        /* Reverse iterate our list of entries. */
        for(auto hash = vHashes.rbegin(); hash != vHashes.rend(); ++hash)
        {
            /* Read the transaction from the ledger database. */
            TAO::API::Transaction tx;
            if(!LLD::Logical->ReadTx(*hash, tx))
            {
                debug::warning(FUNCTION, "read for ", hashGenesis.SubString(), " failed at tx ", hash->SubString());
                break;
            }

            /* Broadcast our transaction if it is in the mempool already. */
            if(TAO::Ledger::mempool.Has(*hash))
                tx.Broadcast();

            /* Otherwise accept and execute this transaction. */
            else if(!TAO::Ledger::mempool.Accept(tx))
            {
                debug::warning(FUNCTION, "accept for ", hash->SubString(), " failed");
                continue;
            }
        }
    }


    /*  Refresh our events and transactions for a given sigchain. */
    void Indexing::DownloadSigchain(const uint256_t& hashGenesis)
    {
        /* Check for client mode. */
        if(!config::fClient.load())
            return;

        /* Check for genesis. */
        if(LLP::TRITIUM_SERVER)
        {
            /* Find an active connection to sync from. */
            std::shared_ptr<LLP::TritiumNode> pNode = LLP::TRITIUM_SERVER->GetConnection();
            if(pNode != nullptr)
            {
                debug::log(0, FUNCTION, "CLIENT MODE: Synchronizing Sigchain");

                /* Get the last txid in sigchain. */
                uint512_t hashLast;
                LLD::Logical->ReadLastConfirmed(hashGenesis, hashLast);

                do
                {
                    /* Request the sig chain. */
                    debug::log(0, FUNCTION, "CLIENT MODE: Requesting LIST::SIGCHAIN for ", hashGenesis.SubString());
                    LLP::TritiumNode::BlockingMessage
                    (
                        30000,
                        pNode.get(), LLP::TritiumNode::ACTION::LIST,
                        uint8_t(LLP::TritiumNode::TYPES::SIGCHAIN), hashGenesis, hashLast
                    );
                    debug::log(0, FUNCTION, "CLIENT MODE: LIST::SIGCHAIN received for ", hashGenesis.SubString());

                    /* Check for shutdown. */
                    if(config::fShutdown.load())
                        break;

                    uint512_t hashCurrent;
                    LLD::Logical->ReadLastConfirmed(hashGenesis, hashCurrent);

                    if(hashCurrent == hashLast)
                    {
                        debug::log(0, FUNCTION, "CLIENT MODE: LIST::SIGCHAIN completed for ", hashGenesis.SubString());
                        break;
                    }
                }
                while(LLD::Logical->ReadLastConfirmed(hashGenesis, hashLast));
            }
            else
                debug::error(FUNCTION, "no connections available...");
        }
    }


    /* Refresh our notifications for a given sigchain. */
    void Indexing::DownloadNotifications(const uint256_t& hashGenesis)
    {
        /* Check for client mode. */
        if(!config::fClient.load())
            return;

        /* Check for genesis. */
        if(LLP::TRITIUM_SERVER)
        {
            /* Find an active connection to sync from. */
            std::shared_ptr<LLP::TritiumNode> pNode = LLP::TRITIUM_SERVER->GetConnection();
            if(pNode != nullptr)
            {
                debug::log(0, FUNCTION, "CLIENT MODE: Synchronizing Notifications");

                /* Get our current tritium events sequence now. */
                uint32_t nTritiumSequence = 0;
                LLD::Logical->ReadTritiumSequence(hashGenesis, nTritiumSequence);

                /* Loop until we have received all of our events. */
                do
                {
                    /* Request the sig chain. */
                    debug::log(0, FUNCTION, "CLIENT MODE: Requesting LIST::NOTIFICATION from ", nTritiumSequence, " for ", hashGenesis.SubString());
                    LLP::TritiumNode::BlockingMessage
                    (
                        30000,
                        pNode.get(), LLP::TritiumNode::ACTION::LIST,
                        uint8_t(LLP::TritiumNode::TYPES::NOTIFICATION), hashGenesis, nTritiumSequence
                    );
                    debug::log(0, FUNCTION, "CLIENT MODE: LIST::NOTIFICATION received for ", hashGenesis.SubString());

                    /* Check for shutdown. */
                    if(config::fShutdown.load())
                        break;

                    /* Cache our current sequence to see if we got any new events while waiting. */
                    uint32_t nCurrentSequence = 0;
                    LLD::Logical->ReadTritiumSequence(hashGenesis, nCurrentSequence);

                    /* Check that are starting and current sequences match. */
                    if(nCurrentSequence == nTritiumSequence)
                    {
                        debug::log(0, FUNCTION, "CLIENT MODE: LIST::NOTIFICATION completed for ", hashGenesis.SubString());
                        break;
                    }
                }
                while(LLD::Logical->ReadTritiumSequence(hashGenesis, nTritiumSequence));

                /* Get our current legacy events sequence now. */
                uint32_t nLegacySequence = 0;
                LLD::Logical->ReadLegacySequence(hashGenesis, nLegacySequence);

                /* Loop until we have received all of our events. */
                do
                {
                    /* Request the sig chain. */
                    debug::log(0, FUNCTION, "CLIENT MODE: Requesting LIST::LEGACY::NOTIFICATION from ", nLegacySequence, " for ", hashGenesis.SubString());
                    LLP::TritiumNode::BlockingMessage
                    (
                        30000,
                        pNode.get(), LLP::TritiumNode::ACTION::LIST,
                        uint8_t(LLP::TritiumNode::SPECIFIER::LEGACY), uint8_t(LLP::TritiumNode::TYPES::NOTIFICATION),
                        hashGenesis, nLegacySequence
                    );
                    debug::log(0, FUNCTION, "CLIENT MODE: LIST::LEGACY::NOTIFICATION received for ", hashGenesis.SubString());

                    /* Check for shutdown. */
                    if(config::fShutdown.load())
                        break;

                    /* Cache our current sequence to see if we got any new events while waiting. */
                    uint32_t nCurrentSequence = 0;
                    LLD::Logical->ReadLegacySequence(hashGenesis, nCurrentSequence);

                    /* Check that are starting and current sequences match. */
                    if(nCurrentSequence == nLegacySequence)
                    {
                        debug::log(0, FUNCTION, "CLIENT MODE: LIST::LEGACY::NOTIFICATION completed for ", hashGenesis.SubString());
                        break;
                    }
                }
                while(LLD::Logical->ReadLegacySequence(hashGenesis, nLegacySequence));
            }
            else
                debug::error(FUNCTION, "no connections available...");
        }
    }


    /* Default destructor. */
    void Indexing::InitializeThread()
    {
        /* Track our current genesis that we are initializing. */
        uint256_t hashSession = TAO::API::Authentication::SESSION::INVALID;

        /* Main loop controlled by condition variable. */
        std::mutex CONDITION_MUTEX;
        while(!config::fShutdown.load())
        {
            try
            {
                /* Cleanup our previous indexing session by setting our status. */
                if(hashSession != TAO::API::Authentication::SESSION::INVALID)
                {
                    /* Get our current genesis-id to start initialization. */
                    const uint256_t hashGenesis =
                        Authentication::Caller(hashSession);

                    /* Track our ledger database sequence. */
                    uint32_t nLegacySequence = 0;
                    LLD::Logical->ReadLegacySequence(hashGenesis, nLegacySequence);

                    /* Track our logical database sequence. */
                    uint32_t nLogicalSequence = 0;
                    LLD::Logical->ReadTritiumSequence(hashGenesis, nLogicalSequence);

                    //TODO: check why we are getting an extra transaction on FLAGS::MEMPOOL
                    uint512_t hashLedgerLast = 0;
                    LLD::Ledger->ReadLast(hashGenesis, hashLedgerLast, TAO::Ledger::FLAGS::MEMPOOL);

                    TAO::Ledger::Transaction txLedgerLast;
                    LLD::Ledger->ReadTx(hashLedgerLast, txLedgerLast, TAO::Ledger::FLAGS::MEMPOOL);

                    uint512_t hashLogicalLast = 0;
                    LLD::Logical->ReadLast(hashGenesis, hashLogicalLast);

                    TAO::API::Transaction txLogicalLast;
                    LLD::Logical->ReadTx(hashLogicalLast, txLogicalLast);

                    uint32_t nLogicalHeight = txLogicalLast.nSequence;
                    uint32_t nLedgerHeight  = txLedgerLast.nSequence;

                    /* Set our indexing status to ready now. */
                    Authentication::SetReady(hashSession);

                    /* Debug output to track our sequences. */
                    debug::log(0, FUNCTION, "Completed building indexes at ", VARIABLE(nLegacySequence), " | ", VARIABLE(nLogicalSequence), " | ", VARIABLE(nLedgerHeight), " | ", VARIABLE(nLogicalHeight), " for genesis=", hashGenesis.SubString());

                    /* Reset the genesis-id now. */
                    hashSession = TAO::API::Authentication::SESSION::INVALID;

                    continue;
                }

                /* Wait for entries in the queue. */
                std::unique_lock<std::mutex> CONDITION_LOCK(CONDITION_MUTEX);
                INITIALIZE_CONDITION.wait(CONDITION_LOCK,
                [&]
                {
                    /* Check for shutdown. */
                    if(config::fShutdown.load())
                        return true;

                    /* Check for suspended state. */
                    if(config::fSuspended.load())
                        return false;

                    /* Check for a session that needs to be wiped. */
                    if(hashSession != TAO::API::Authentication::SESSION::INVALID)
                        return true;

                    return Indexing::INITIALIZE->size() != 0;
                });

                /* Check for shutdown. */
                if(config::fShutdown.load())
                    return;

                /* Check that we have items in the queue. */
                if(Indexing::INITIALIZE->empty())
                    continue;

                /* Get the current genesis-id to initialize for. */
                hashSession = INITIALIZE->front();
                INITIALIZE->pop();

                /* Get our current genesis-id to start initialization. */
                const uint256_t hashGenesis =
                    Authentication::Caller(hashSession);

                /* Sync the sigchain if an active client before building our indexes. */
                if(config::fClient.load())
                {
                    /* Broadcast our unconfirmed transactions first. */
                    BroadcastUnconfirmed(hashGenesis);

                    /* Process our sigchain events now. */
                    DownloadNotifications(hashGenesis);
                    DownloadSigchain(hashGenesis);

                    /* Exit out of this thread if we are shutting down. */
                    if(config::fShutdown.load())
                        return;
                }

                /* EVENTS INDEXING DISABLED for -client mode since we build them when downloading. */
                else
                {
                    /* Read our last sequence. */
                    uint32_t nTritiumSequence = 0;
                    LLD::Logical->ReadTritiumSequence(hashGenesis, nTritiumSequence);

                    /* Read our last sequence. */
                    uint32_t nLegacySequence = 0;
                    LLD::Logical->ReadLegacySequence(hashGenesis, nLegacySequence);

                    /* Debug output so w4e can track our events indexes. */
                    debug::log(2, FUNCTION, "Building events indexes from ", VARIABLE(nTritiumSequence), " | ", VARIABLE(nLegacySequence), " for genesis=", hashGenesis.SubString());

                    /* Loop through our ledger level events. */
                    TAO::Ledger::Transaction tTritium;
                    while(LLD::Ledger->ReadEvent(hashGenesis, nTritiumSequence++, tTritium))
                    {
                        /* Check for shutdown. */
                        if(config::fShutdown.load())
                            return;

                        /* Cache our current event's txid. */
                        const uint512_t hashEvent =
                            tTritium.GetHash(true); //true to override cache

                        /* Index our dependant transaction. */
                        IndexDependant(hashEvent, tTritium);
                    }

                    /* Loop through our ledger level events. */
                    Legacy::Transaction tLegacy;
                    while(LLD::Legacy->ReadEvent(hashGenesis, nLegacySequence++, tLegacy))
                    {
                        /* Check for shutdown. */
                        if(config::fShutdown.load())
                            return;

                        /* Cache our current event's txid. */
                        const uint512_t hashEvent =
                            tLegacy.GetHash();

                        /* Index our dependant transaction. */
                        IndexDependant(hashEvent, tLegacy);
                    }

                    /* Check that our ledger indexes are up-to-date with our logical indexes. */
                    uint512_t hashLedger = 0;
                    if(LLD::Ledger->ReadLast(hashGenesis, hashLedger, TAO::Ledger::FLAGS::MEMPOOL))
                    {
                        /* Build list of transaction hashes. */
                        std::vector<uint512_t> vBuild;

                        /* Read all transactions from our last index. */
                        uint512_t hashTx = hashLedger;
                        while(!config::fShutdown.load())
                        {
                            /* Read the transaction from the ledger database. */
                            TAO::Ledger::Transaction tx;
                            if(!LLD::Ledger->ReadTx(hashTx, tx, TAO::Ledger::FLAGS::MEMPOOL))
                            {
                                debug::warning(FUNCTION, "pre-build read failed at ", hashTx.SubString());
                                break;
                            }

                            /* Check for valid logical indexes. */
                            if(!LLD::Logical->HasTx(hashTx))
                                vBuild.push_back(hashTx);
                            else
                                break;

                            /* Break on first after we have checked indexes. */
                            if(tx.IsFirst())
                                break;

                            /* Set hash to previous hash. */
                            hashTx = tx.hashPrevTx;
                        }

                        /* Only output our data when we have indexes to build. */
                        if(!vBuild.empty())
                            debug::log(1, FUNCTION, "Building ", vBuild.size(), " indexes for genesis=", hashGenesis.SubString());

                        /* Reverse iterate our list of entries and index. */
                        for(auto hashTx = vBuild.rbegin(); hashTx != vBuild.rend(); ++hashTx)
                        {
                            /* Read the transaction from the ledger database. */
                            TAO::Ledger::Transaction tx;
                            if(!LLD::Ledger->ReadTx(*hashTx, tx, TAO::Ledger::FLAGS::MEMPOOL))
                            {
                                debug::warning(FUNCTION, "build read failed at ", hashTx->SubString());
                                break;
                            }

                            /* Build an API transaction. */
                            TAO::API::Transaction tIndex =
                                TAO::API::Transaction(tx);

                            /* Index the transaction to the database. */
                            if(!tIndex.Index(*hashTx))
                                debug::warning(FUNCTION, "failed to build index ", hashTx->SubString());

                            /* Log that tx was rebroadcast. */
                            debug::log(1, FUNCTION, "Built Indexes for ", hashTx->SubString(), " to logical db");
                        }
                    }


                    /* Check that our last indexing entries match. */
                    uint512_t hashLogical = 0;
                    if(LLD::Logical->ReadLast(hashGenesis, hashLogical))
                    {
                        /* Build list of transaction hashes. */
                        std::vector<uint512_t> vIndex;

                        /* Read all transactions from our last index. */
                        uint512_t hashTx = hashLogical;
                        while(!config::fShutdown.load())
                        {
                            /* Read the transaction from the ledger database. */
                            TAO::API::Transaction tx;
                            if(!LLD::Logical->ReadTx(hashTx, tx))
                            {
                                debug::warning(FUNCTION, "pre-update read failed at ", hashTx.SubString());
                                break;
                            }

                            /* Break on our first confirmed tx. */
                            if(tx.Confirmed())
                                break;

                            /* Check that we have an index here. */
                            if(LLD::Ledger->HasIndex(hashTx) && !tx.Confirmed())
                                vIndex.push_back(hashTx); //this will warm up the LLD cache if available, or remain low footprint if not

                            /* Break on first after we have checked indexes. */
                            if(tx.IsFirst())
                                break;

                            /* Set hash to previous hash. */
                            hashTx = tx.hashPrevTx;
                        }

                        /* Only output our data when we have indexes to build. */
                        if(!vIndex.empty())
                            debug::log(1, FUNCTION, "Updating ", vIndex.size(), " indexes for genesis=", hashGenesis.SubString());

                        /* Reverse iterate our list of entries and index. */
                        for(auto hashTx = vIndex.rbegin(); hashTx != vIndex.rend(); ++hashTx)
                        {
                            /* Read the transaction from the ledger database. */
                            TAO::API::Transaction tx;
                            if(!LLD::Logical->ReadTx(*hashTx, tx))
                            {
                                debug::warning(FUNCTION, "update read failed at ", hashTx->SubString());
                                break;
                            }

                            /* Index the transaction to the database. */
                            if(!tx.Index(*hashTx))
                                debug::warning(FUNCTION, "failed to update index ", hashTx->SubString());

                            /* Log that tx was rebroadcast. */
                            debug::log(1, FUNCTION, "Updated Indexes for ", hashTx->SubString(), " to logical db");
                        }

                        /* Check if we need to re-broadcast anything. */
                        BroadcastUnconfirmed(hashGenesis);
                    }
                }
            }
            catch(const Exception& e)
            {
                debug::warning(e.what());
            }
        }
    }


    /* Index transaction level events for logged in sessions. */
    void Indexing::IndexDependant(const uint512_t& hashTx, const Legacy::Transaction& tx)
    {
        /* Loop thgrough the available outputs. */
        for(uint32_t nContract = 0; nContract < tx.vout.size(); nContract++)
        {
            /* Grab a reference of our output. */
            const Legacy::TxOut& txout = tx.vout[nContract];

            /* Extract our register address. */
            uint256_t hashTo;
            if(Legacy::ExtractRegister(txout.scriptPubKey, hashTo))
            {
                /* Read the owner of register. (check this for MEMPOOL, too) */
                TAO::Register::State state;
                if(!LLD::Register->ReadState(hashTo, state, TAO::Ledger::FLAGS::LOOKUP))
                    continue;

                /* Check if owner is authenticated. */
                if(Authentication::Active(state.hashOwner))
                {
                    /* Write our events to database. */
                    if(!LLD::Logical->PushEvent(state.hashOwner, hashTx, nContract))
                        continue;

                    /* Increment our sequence. */
                    if(!LLD::Logical->IncrementLegacySequence(state.hashOwner))
                        continue;

                    debug::log(2, FUNCTION, "LEGACY: ",
                        "for genesis ", state.hashOwner.SubString(), " | ", VARIABLE(hashTx.SubString()), ", ", VARIABLE(nContract));
                }
            }
        }
    }


    /* Index transaction level events for logged in sessions. */
    void Indexing::IndexDependant(const uint512_t& hashTx, const TAO::Ledger::Transaction& tx)
    {
        /* Check all the tx contracts. */
        for(uint32_t nContract = 0; nContract < tx.Size(); nContract++)
        {
            /* Grab reference of our contract. */
            const TAO::Operation::Contract& rContract = tx[nContract];

            /* Skip to our primitive. */
            rContract.SeekToPrimitive();

            /* Check the contract's primitive. */
            uint8_t nOP = 0;
            rContract >> nOP;
            switch(nOP)
            {
                case TAO::Operation::OP::TRANSFER:
                case TAO::Operation::OP::DEBIT:
                {
                    /* Get the register address. */
                    TAO::Register::Address hashAddress;
                    rContract >> hashAddress;

                    /* Deserialize recipient from contract. */
                    TAO::Register::Address hashRecipient;
                    rContract >> hashRecipient;

                    /* Special check when handling a DEBIT. */
                    if(nOP == TAO::Operation::OP::DEBIT)
                    {
                        /* Skip over partials as this is handled seperate. */
                        if(hashRecipient.IsObject())
                            continue;

                        /* Read the owner of register. (check this for MEMPOOL, too) */
                        TAO::Register::State oRegister;
                        if(!LLD::Register->ReadState(hashRecipient, oRegister, TAO::Ledger::FLAGS::LOOKUP))
                            continue;

                        /* Set our hash to based on owner. */
                        hashRecipient = oRegister.hashOwner;
                    }

                    /* Check if we need to build index for this contract. */
                    if(Authentication::Active(hashRecipient))
                    {
                        /* Push to unclaimed indexes if processing incoming transfer. */
                        if(nOP == TAO::Operation::OP::TRANSFER && !LLD::Logical->PushUnclaimed(hashRecipient, hashAddress))
                            continue;

                        /* Write our events to database. */
                        if(!LLD::Logical->PushEvent(hashRecipient, hashTx, nContract))
                        {
                            debug::warning(FUNCTION, "failed to push event for ", hashTx.SubString(), " contract ", nContract);
                            continue;
                        }

                        /* Increment our sequence. */
                        if(!LLD::Logical->IncrementTritiumSequence(hashRecipient))
                        {
                            debug::warning(FUNCTION, "failed to increment sequence for ", hashTx.SubString(), " contract ", nContract);
                            continue;
                        }
                    }

                    debug::log(2, FUNCTION, (nOP == TAO::Operation::OP::TRANSFER ? "TRANSFER: " : "DEBIT: "),
                        "for genesis ", hashRecipient.SubString(), " | ", VARIABLE(hashTx.SubString()), ", ", VARIABLE(nContract));

                    break;
                }

                case TAO::Operation::OP::COINBASE:
                {
                    /* Get the genesis. */
                    uint256_t hashRecipient;
                    rContract >> hashRecipient;

                    /* Check if we need to build index for this contract. */
                    if(Authentication::Active(hashRecipient))
                    {
                        /* Write our events to database. */
                        if(!LLD::Logical->PushEvent(hashRecipient, hashTx, nContract))
                            continue;

                        /* We don't increment our events index for miner coinbase contract. */
                        if(hashRecipient == tx.hashGenesis)
                            continue;

                        /* Increment our sequence. */
                        if(!LLD::Logical->IncrementTritiumSequence(hashRecipient))
                            continue;

                        debug::log(2, FUNCTION, "COINBASE: for genesis ", hashRecipient.SubString(), " | ", VARIABLE(hashTx.SubString()), ", ", VARIABLE(nContract));
                    }

                    break;
                }
            }
        }
    }


    /* Default destructor. */
    void Indexing::Shutdown()
    {
        /* Cleanup our initialization thread. */
        INITIALIZE_CONDITION.notify_all();
        if(INITIALIZE_THREAD.joinable())
            INITIALIZE_THREAD.join();

        /* Cleanup our dispatch thread. */
        CONDITION.notify_all();
        if(EVENTS_THREAD.joinable())
            EVENTS_THREAD.join();

        /* Clear open registrations. */
        {
            LOCK(REGISTERED_MUTEX);
            REGISTERED.clear();
        }
    }


    /* Index list of user level indexing entries. */
    void Indexing::index_transaction(const uint512_t& hash, const TAO::Ledger::Transaction& tx)
    {
        /* Check if we need to index the main sigchain. */
        if(Authentication::Active(tx.hashGenesis))
        {
            /* Build an API transaction. */
            TAO::API::Transaction tIndex =
                TAO::API::Transaction(tx);

            /* Index the transaction to the database. */
            if(!tIndex.Index(hash))
                return;
        }

        /* Check all the tx contracts. */
        for(uint32_t nContract = 0; nContract < tx.Size(); nContract++)
        {
            /* Grab reference of our contract. */
            const TAO::Operation::Contract& rContract = tx[nContract];

            /* Skip to our primitive. */
            rContract.SeekToPrimitive();

            /* Check the contract's primitive. */
            uint8_t nOP = 0;
            rContract >> nOP;
            switch(nOP)
            {
                case TAO::Operation::OP::TRANSFER:
                case TAO::Operation::OP::DEBIT:
                {
                    /* Get the register address. */
                    TAO::Register::Address hashAddress;
                    rContract >> hashAddress;

                    /* Deserialize recipient from contract. */
                    TAO::Register::Address hashRecipient;
                    rContract >> hashRecipient;

                    /* Special check when handling a DEBIT. */
                    if(nOP == TAO::Operation::OP::DEBIT)
                    {
                        /* Skip over partials as this is handled seperate. */
                        if(hashRecipient.IsObject())
                            continue;

                        /* Read the owner of register. (check this for MEMPOOL, too) */
                        TAO::Register::State oRegister;
                        if(!LLD::Register->ReadState(hashRecipient, oRegister, TAO::Ledger::FLAGS::LOOKUP))
                            continue;

                        /* Set our hash to based on owner. */
                        hashRecipient = oRegister.hashOwner;

                        /* Check for active debit from with contract. */
                        if(Authentication::Active(tx.hashGenesis))
                        {
                            /* Write our events to database. */
                            if(!LLD::Logical->PushContract(tx.hashGenesis, hash, nContract))
                                continue;
                        }
                    }

                    /* Check if we need to build index for this contract. */
                    if(Authentication::Active(hashRecipient))
                    {
                        /* Push to unclaimed indexes if processing incoming transfer. */
                        if(nOP == TAO::Operation::OP::TRANSFER && !LLD::Logical->PushUnclaimed(hashRecipient, hashAddress))
                            continue;

                        /* Write our events to database. */
                        if(!LLD::Logical->PushEvent(hashRecipient, hash, nContract))
                            continue;

                        /* Increment our sequence. */
                        if(!LLD::Logical->IncrementTritiumSequence(hashRecipient))
                            continue;
                    }

                    debug::log(2, FUNCTION, (nOP == TAO::Operation::OP::TRANSFER ? "TRANSFER: " : "DEBIT: "),
                        "for genesis ", hashRecipient.SubString(), " | ", VARIABLE(hash.SubString()), ", ", VARIABLE(nContract));

                    break;
                }

                case TAO::Operation::OP::COINBASE:
                {
                    /* Get the genesis. */
                    uint256_t hashRecipient;
                    rContract >> hashRecipient;

                    /* Check if we need to build index for this contract. */
                    if(Authentication::Active(hashRecipient))
                    {
                        /* Write our events to database. */
                        if(!LLD::Logical->PushEvent(hashRecipient, hash, nContract))
                            continue;

                        /* We don't increment our events index for miner coinbase contract. */
                        if(hashRecipient == tx.hashGenesis)
                            continue;

                        /* Increment our sequence. */
                        if(!LLD::Logical->IncrementTritiumSequence(hashRecipient))
                            continue;

                        debug::log(2, FUNCTION, "COINBASE: for genesis ", hashRecipient.SubString(), " | ", VARIABLE(hash.SubString()), ", ", VARIABLE(nContract));
                    }

                    break;
                }
            }
        }
    }
}
