/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2023

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLC/include/random.h>

#include <LLP/include/global.h>
#include <LLP/include/network.h>

#include <TAO/API/include/global.h>

#include <TAO/API/types/authentication.h>
#include <TAO/API/types/indexing.h>

#include <TAO/Ledger/include/process.h>



namespace LLP
{
    /* Track our hostname so we don't have to call system every request. */
    std::string strHostname;

    /* Declare the Global LLP Instances. */
    Server<TritiumNode>* TRITIUM_SERVER;
    Server<LookupNode>*  LOOKUP_SERVER;
    Server<TimeNode>*    TIME_SERVER;
    Server<APINode>*     API_SERVER;
    Server<FileNode>*    FILE_SERVER;
    Server<RPCNode>*     RPC_SERVER;
    Server<Miner>*       MINING_SERVER;


    /* Current session identifier. */
    const uint64_t SESSION_ID = LLC::GetRand();


    /*  Initialize the LLP. */
    bool Initialize()
    {
        /* Initialize the underlying network resources such as sockets, etc */
        if(!NetworkInitialize())
            return debug::error(FUNCTION, "NetworkInitialize: Failed initializing network resources.");

        /* Get our current hostname. */
        char chHostname[128];
        gethostname(chHostname, sizeof(chHostname));
        strHostname = std::string(chHostname);

        /* Initialize API Pointers. */
        TAO::API::Initialize();


        /* TIME_SERVER instance */
        {
            /* Check if we need to enable listeners. */
            const bool fServer =
                (config::GetBoolArg(std::string("-unified"), false) && !config::fClient.load());

            /* Generate our config object and use correct settings. */
            LLP::Config CONFIG     = LLP::Config(GetTimePort());
            CONFIG.ENABLE_LISTEN   = fServer;
            CONFIG.ENABLE_METERS   = false;
            CONFIG.ENABLE_DDOS     = true;
            CONFIG.ENABLE_MANAGER  = true;
            CONFIG.ENABLE_SSL      = false;
            CONFIG.ENABLE_REMOTE   = fServer;
            CONFIG.REQUIRE_SSL     = false;
            CONFIG.PORT_SSL        = 0; //TODO: this is disabled until SSL code can be refactored
            CONFIG.MAX_INCOMING    = fServer ? static_cast<uint32_t>(config::GetArg(std::string("-maxincoming"), 84)) : 0;
            CONFIG.MAX_CONNECTIONS = fServer ? static_cast<uint32_t>(config::GetArg(std::string("-maxconnections"), 100)) : 8;
            CONFIG.MAX_THREADS     = fServer ? 8 : 1;
            CONFIG.DDOS_CSCORE     = 1;
            CONFIG.DDOS_RSCORE     = 10;
            CONFIG.DDOS_TIMESPAN   = 10;
            CONFIG.MANAGER_SLEEP   = 60000; //default: 60 second connection attempts
            CONFIG.SOCKET_TIMEOUT  = 10;

            /* Create the server instance. */
            TIME_SERVER = new Server<TimeNode>(CONFIG);
        }


        /* LOOKUP_SERVER instance */
        if(config::GetBoolArg(std::string("-lookup"), true))
        {
            /* Generate our config object and use correct settings. */
            LLP::Config CONFIG     = LLP::Config(GetLookupPort());
            CONFIG.ENABLE_LISTEN   = //we only listen if we have the valid indexes created
                (!config::fClient.load() && config::fIndexProofs.load() && config::fIndexRegister.load());

            CONFIG.ENABLE_METERS   = false;
            CONFIG.ENABLE_DDOS     = true;
            CONFIG.ENABLE_MANAGER  = false;
            CONFIG.ENABLE_SSL      = false;
            CONFIG.ENABLE_REMOTE   = true;
            CONFIG.REQUIRE_SSL     = false;
            CONFIG.PORT_SSL        = 0; //TODO: this is disabled until SSL code can be refactored
            CONFIG.MAX_INCOMING    = 128;
            CONFIG.MAX_CONNECTIONS = 128;
            CONFIG.MAX_THREADS     = config::GetArg(std::string("-lookupthreads"), 4);
            CONFIG.DDOS_CSCORE     = config::GetArg(std::string("-lookupcscore"), 1);
            CONFIG.DDOS_RSCORE     = config::GetArg(std::string("-lookuprscore"), 50);
            CONFIG.DDOS_TIMESPAN   = config::GetArg(std::string("-lookuptimespan"), 10);
            CONFIG.MANAGER_SLEEP   = 0; //this is disabled
            CONFIG.SOCKET_TIMEOUT  = config::GetArg(std::string("-lookuptimeout"), 30);

            /* Create the server instance. */
            LOOKUP_SERVER = new Server<LookupNode>(CONFIG);
        }


        /* TRITIUM_SERVER instance */
        {
            /* Generate our config object and use correct settings. */
            LLP::Config CONFIG     = LLP::Config(GetDefaultPort());
            CONFIG.ENABLE_LISTEN   = config::GetBoolArg(std::string("-listen"), (config::fClient.load() ? false : true));
            CONFIG.ENABLE_METERS   = config::GetBoolArg(std::string("-meters"), false);
            CONFIG.ENABLE_DDOS     = config::GetBoolArg(std::string("-ddos"), false);
            CONFIG.ENABLE_MANAGER  = config::GetBoolArg(std::string("-manager"), true);
            CONFIG.ENABLE_SSL      = config::GetBoolArg(std::string("-ssl"), false);
            CONFIG.ENABLE_REMOTE   = true;
            CONFIG.REQUIRE_SSL     = config::GetBoolArg(std::string("-sslrequired"), false);
            CONFIG.PORT_SSL        = 0; //TODO: this is disabled until SSL code can be refactored
            CONFIG.MAX_INCOMING    = config::GetArg(std::string("-maxincoming"), 84);
            CONFIG.MAX_CONNECTIONS = config::GetArg(std::string("-maxconnections"), 100);
            CONFIG.MAX_THREADS     = config::GetArg(std::string("-threads"), 8);
            CONFIG.DDOS_CSCORE     = config::GetArg(std::string("-cscore"), 1);
            CONFIG.DDOS_RSCORE     = config::GetArg(std::string("-rscore"), 2000);
            CONFIG.DDOS_TIMESPAN   = config::GetArg(std::string("-timespan"), 20);
            CONFIG.MANAGER_SLEEP   = 1000; //default: 1 second connection attempts
            CONFIG.SOCKET_TIMEOUT  = config::GetArg(std::string("-timeout"), 120);

            /* Create the server instance. */
            TRITIUM_SERVER = new Server<TritiumNode>(CONFIG);
        }


        /* FILE_SERVER instance */
        if(config::fFileServer.load())
        {
            /* Generate our config object and use correct settings. */
            LLP::Config CONFIG     = LLP::Config(80); //default http uses port 80
            CONFIG.ENABLE_LISTEN   = true;
            CONFIG.ENABLE_METERS   = config::GetBoolArg(std::string("-httpmeters"), false);
            CONFIG.ENABLE_DDOS     = true;
            CONFIG.ENABLE_MANAGER  = false;
            CONFIG.ENABLE_SSL      = config::GetBoolArg(std::string("-httpssl"));
            CONFIG.ENABLE_REMOTE   = config::GetBoolArg(std::string("-httpremote"), true);
            CONFIG.REQUIRE_SSL     = config::GetBoolArg(std::string("-httpsslrequired"), false);
            CONFIG.PORT_SSL        = 443; //default https uses port 443
            CONFIG.MAX_INCOMING    = 128;
            CONFIG.MAX_CONNECTIONS = 128;
            CONFIG.MAX_THREADS     = config::GetArg(std::string("-httpthreads"), 8);
            CONFIG.DDOS_CSCORE     = config::GetArg(std::string("-httpcscore"), 5);
            CONFIG.DDOS_RSCORE     = config::GetArg(std::string("-httprscore"), 5);
            CONFIG.DDOS_TIMESPAN   = config::GetArg(std::string("-httptimespan"), 60);
            CONFIG.MANAGER_SLEEP   = 0; //this is disabled
            CONFIG.SOCKET_TIMEOUT  = config::GetArg(std::string("-httptimeout"), 30);

            /* Create the server instance. */
            LLP::FILE_SERVER = new Server<FileNode>(CONFIG);

            /* We want to post a notice if this parameter is enabled. */
            debug::notice("HTTP SERVER ENABLED: you have set -fileroot=<directory> parameter, listening on port 80.");
        }


        /* API_SERVER instance */
        if((config::HasArg("-apiuser") && config::HasArg("-apipassword")) || !config::GetBoolArg("-apiauth", true))
        {
            /* Generate our config object and use correct settings. */
            LLP::Config CONFIG     = LLP::Config(GetAPIPort());
            CONFIG.ENABLE_LISTEN   = true;
            CONFIG.ENABLE_METERS   = config::GetBoolArg(std::string("-apimeters"), false);
            CONFIG.ENABLE_DDOS     = true;
            CONFIG.ENABLE_MANAGER  = false;
            CONFIG.ENABLE_SSL      = config::GetBoolArg(std::string("-apissl"));
            CONFIG.ENABLE_REMOTE   = config::GetBoolArg(std::string("-apiremote"), false);
            CONFIG.REQUIRE_SSL     = config::GetBoolArg(std::string("-apisslrequired"), false);
            CONFIG.PORT_SSL        = GetAPIPort(true); //switch API port based on boolean argument
            CONFIG.MAX_INCOMING    = 128;
            CONFIG.MAX_CONNECTIONS = 128;
            CONFIG.MAX_THREADS     = config::GetArg(std::string("-apithreads"), 8);
            CONFIG.DDOS_CSCORE     = config::GetArg(std::string("-apicscore"), 5);
            CONFIG.DDOS_RSCORE     = config::GetArg(std::string("-apirscore"), 5);
            CONFIG.DDOS_TIMESPAN   = config::GetArg(std::string("-apitimespan"), 60);
            CONFIG.MANAGER_SLEEP   = 0; //this is disabled
            CONFIG.SOCKET_TIMEOUT  = config::GetArg(std::string("-apitimeout"), 30);

            /* Create the server instance. */
            LLP::API_SERVER = new Server<APINode>(CONFIG);
        }
        else
        {
            /* Output our new warning message if the API was disabled. */
            debug::notice(ANSI_COLOR_BRIGHT_RED, "API SERVER DISABLED", ANSI_COLOR_RESET);
            debug::notice(ANSI_COLOR_BRIGHT_YELLOW, "You must set apiuser=<user> and apipassword=<password> configuration.", ANSI_COLOR_RESET);
            debug::notice(ANSI_COLOR_BRIGHT_YELLOW, "If you intend to run the API server without authenticating, set apiauth=0", ANSI_COLOR_RESET);
        }


        /* RPC_SERVER instance */
        #ifndef NO_WALLET
        if(config::HasArg("-rpcuser") && config::HasArg("-rpcpassword"))
        {
            /* Generate our config object and use correct settings. */
            LLP::Config CONFIG     = LLP::Config(GetRPCPort());
            CONFIG.ENABLE_LISTEN   = true;
            CONFIG.ENABLE_METERS   = config::GetBoolArg(std::string("-rpcmeters"), false);
            CONFIG.ENABLE_DDOS     = true;
            CONFIG.ENABLE_MANAGER  = false;
            CONFIG.ENABLE_SSL      = config::GetBoolArg(std::string("-rpcssl"));
            CONFIG.ENABLE_REMOTE   = config::GetBoolArg(std::string("-rpcremote"), false);
            CONFIG.REQUIRE_SSL     = config::GetBoolArg(std::string("-rpcsslrequired"), false);
            CONFIG.PORT_SSL        = 0; //TODO: this is disabled until SSL code can be refactored
            CONFIG.MAX_INCOMING    = 128;
            CONFIG.MAX_CONNECTIONS = 128;
            CONFIG.MAX_THREADS     = config::GetArg(std::string("-rpcthreads"), 4);
            CONFIG.DDOS_CSCORE     = config::GetArg(std::string("-rpccscore"), 5);
            CONFIG.DDOS_RSCORE     = config::GetArg(std::string("-rpcrscore"), 5);
            CONFIG.DDOS_TIMESPAN   = config::GetArg(std::string("-rpctimespan"), 60);
            CONFIG.MANAGER_SLEEP   = 0; //this is disabled
            CONFIG.SOCKET_TIMEOUT  = 30;

            /* Create the server instance. */
            RPC_SERVER = new Server<RPCNode>(CONFIG);
        }
        else
        {
            /* Output our new warning message if the API was disabled. */
            debug::log(0, ANSI_COLOR_BRIGHT_RED, "RPC SERVER DISABLED", ANSI_COLOR_RESET);
            debug::log(0, ANSI_COLOR_BRIGHT_YELLOW, "You must set rpcuser=<user> and rpcpassword=<password> configuration.", ANSI_COLOR_RESET);
        }
        #endif


        /* MINING_SERVER instance */
        if(config::GetBoolArg(std::string("-mining"), false) && !config::fClient.load())
        {
            /* Generate our config object and use correct settings. */
            LLP::Config CONFIG     = LLP::Config(GetMiningPort());
            CONFIG.ENABLE_LISTEN   = true;
            CONFIG.ENABLE_METERS   = false;
            CONFIG.ENABLE_DDOS     = config::GetBoolArg(std::string("-miningddos"), false);
            CONFIG.ENABLE_MANAGER  = false;
            CONFIG.ENABLE_SSL      = false;
            CONFIG.ENABLE_REMOTE   = true;
            CONFIG.REQUIRE_SSL     = false;
            CONFIG.PORT_SSL        = 0; //TODO: this is disabled until SSL code can be refactored
            CONFIG.MAX_INCOMING    = 128;
            CONFIG.MAX_CONNECTIONS = 128;
            CONFIG.MAX_THREADS     = config::GetArg(std::string("-miningthreads"), 4);
            CONFIG.DDOS_CSCORE     = config::GetArg(std::string("-miningcscore"), 1);
            CONFIG.DDOS_RSCORE     = config::GetArg(std::string("-miningrscore"), 50);
            CONFIG.DDOS_TIMESPAN   = config::GetArg(std::string("-miningtimespan"), 60);
            CONFIG.MANAGER_SLEEP   = 0; //this is disabled
            CONFIG.SOCKET_TIMEOUT  = config::GetArg(std::string("-miningtimeout"), 30);

            /* Create the server instance. */
            MINING_SERVER = new Server<Miner>(CONFIG);
        }

        return true;
    }


    /* Closes the listening sockets on all running servers. */
    void CloseListening()
    {
        /* Release any triggers we have waiting. */
        Release();

        /* Set global system into suspended state. */
        config::fSuspended.store(true);

        debug::log(0, FUNCTION, "Closing LLP Listeners");

        /* Set our protocol into suspended state. */
        config::fSuspendProtocol.store(true);

        /* Close sockets for the lookup server and its subsystems. */
        CloseSockets<LookupNode>(LOOKUP_SERVER);

        /* Close sockets for the tritium server and its subsystems. */
        CloseSockets<TritiumNode>(TRITIUM_SERVER);

        /* Close sockets for the time server and its subsystems. */
        CloseSockets<TimeNode>(TIME_SERVER);

        /* Close sockets for the core API server and its subsystems. */
        CloseSockets<APINode>(API_SERVER);

        /* Close sockets for the RPC server and its subsystems. */
        CloseSockets<RPCNode>(RPC_SERVER);

        /* Close sockets for the mining server and its subsystems. */
        CloseSockets<Miner>(MINING_SERVER);

    }


    /* Restarts the listening sockets on all running servers. */
    void OpenListening()
    {
        /* Initialize the logging file stream. */
        if(!debug::ssFile.is_open())
            debug::ssFile.open(debug::log_path(0), std::ios::app | std::ios::out);

        /* Log that we are opening our listeners back up. */
        debug::log(0, FUNCTION, "Opening LLP Listeners");

        /* Open sockets for the core API server and its subsystems. */
        OpenListening<APINode>(API_SERVER);

        /* Open sockets for the lookup server and its subsystems. */
        OpenListening<LookupNode>(LOOKUP_SERVER);

        /* Open sockets for the tritium server and its subsystems. */
        OpenListening  <TritiumNode> (TRITIUM_SERVER);

        /* Open sockets for the time server and its subsystems. */
        OpenListening<TimeNode>(TIME_SERVER);

        /* Open sockets for the RPC server and its subsystems. */
        OpenListening<RPCNode>(RPC_SERVER);

        /* Open sockets for the mining server and its subsystems. */
        OpenListening<Miner>(MINING_SERVER);

        /* Remove our protocol from suspended state once established. */
        config::fSuspendProtocol.store(false);

        /* Add our connections from commandline. */
        MakeConnections<LLP::TritiumNode>(TRITIUM_SERVER);

        /* Set global system out of suspended state. */
        config::fSuspended.store(false);
    }


    /* Notify the LLP. */
    void Release()
    {
        debug::log(0, FUNCTION, "Releasing LLP Triggers");

        /* Release the lookup server and its subsystems. */
        Release<LookupNode>(LOOKUP_SERVER);

        /* Release the tritium server and its subsystems. */
        Release<TritiumNode>(TRITIUM_SERVER);

        /* Release the time server and its subsystems. */
        Release<TimeNode>(TIME_SERVER);

        /* Release the core API server and its subsystems. */
        Release<APINode>(API_SERVER);

        /* Release the RPC server and its subsystems. */
        Release<RPCNode>(RPC_SERVER);

        /* Release the mining server and its subsystems. */
        Release<Miner>(MINING_SERVER);
    }


    /*  Shutdown the LLP. */
    void Shutdown()
    {
        debug::log(0, FUNCTION, "Shutting down LLP");

        /* Shutdown the time server and its subsystems. */
        Shutdown<TimeNode>(TIME_SERVER);

        /* Shutdown the mining server and its subsystems. */
        Shutdown<Miner>(MINING_SERVER);

        /* Shutdown the core API server and its subsystems. */
        Shutdown<APINode>(API_SERVER);

        /* Shutdown the RPC server and its subsystems. */
        Shutdown<RPCNode>(RPC_SERVER);

        /* Shutdown the tritium server and its subsystems. */
        Shutdown<TritiumNode>(TRITIUM_SERVER);

        /* Shutdown the lookup server and its subsystems. */
        Shutdown<LookupNode>(LOOKUP_SERVER);

        /* After all servers shut down, clean up underlying network resources. */
        NetworkShutdown();
    }
}
