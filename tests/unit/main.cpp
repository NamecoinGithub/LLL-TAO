/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2026

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#define CATCH_CONFIG_NO_POSIX_SIGNALS
#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#include <unit/catch2/catch.hpp>

#include <Legacy/wallet/wallet.h>

#include <LLD/include/global.h>

#include <TAO/API/include/global.h>

#include <TAO/Ledger/types/state.h>
#include <TAO/Ledger/include/chainstate.h>

#include <LLP/include/global.h>
#include <LLP/types/apinode.h>

#include <Util/include/filesystem.h>
#include <Util/include/args.h>

namespace
{
    /* Captured outcomes from the one-time global test environment setup performed by
     * GlobalTestListener::testRunStarting() below, asserted on by the "[args]" TEST_CASE.
     * These are snapshotted at setup time (rather than re-derived inside the TEST_CASE)
     * because the setup always runs before any test case -- regardless of the active
     * Catch2 tag filter -- so by the time (if ever) the "[args]" TEST_CASE body runs,
     * other tests may already have repopulated the data directory or mutated chain state. */
    bool fSetupComplete     = false;
    bool fRemovedOldDataDir = true;
    bool fWalletInitOk      = false;
    bool fWalletLoadOk      = false;
    bool fChainStateInitOk  = false;
    bool fBestStateSet      = false;
    bool fLLPInitOk         = false;


    /** SetupGlobalTestEnvironment
     *
     *  Performs the one-time process-global initialization (config, LLD databases,
     *  wallet, chain state, LLP/API subsystems) that the rest of the unit test suite
     *  depends on. This used to live directly inside TEST_CASE("Arguments Tests",
     *  "[args]"), which meant tag-filtered runs that exclude "[args]" (for example
     *  `./nexus "[ledger]"`) left globals such as LLD::Ledger/LLD::Logical/LLD::Sessions
     *  null, segfaulting the first test (or background thread, e.g. the API indexing
     *  manager) that touched them. Running this from testRunStarting() guarantees it
     *  always runs exactly once before any test case, independent of tag filtering.
     **/
    void SetupGlobalTestEnvironment()
    {
        if(fSetupComplete)
            return;
        fSetupComplete = true;

        config::fTestNet = true;
        config::mapArgs["-private"] = "1";
        config::mapArgs["-testnet"] = "92349234";
        config::mapArgs["-flushwallet"] = "false";
        config::mapArgs["-apiauth"]     = "0";
        config::mapArgs["-generate"]    = "password";

        /* To simplify the API testing we will always use multiuser mode */
        config::fMultiuser = true;
        config::mapArgs["-private"] = "1";
        config::mapArgs["-verbose"] = "3";
        config::fHybrid    = true;

        //get the data directory
        const std::string strPath = config::GetDataDir();

        //clear from previous unit test runs
        if(filesystem::exists(strPath))
            fRemovedOldDataDir = filesystem::remove_directories(strPath) && !filesystem::exists(strPath);

        //create LLD instances
        LLD::Contract = new LLD::ContractDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);
        LLD::Register = new LLD::RegisterDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);
        LLD::Local    = new LLD::LocalDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);
        LLD::Ledger   = new LLD::LedgerDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);
        LLD::Trust    = new LLD::TrustDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);
        LLD::Legacy   = new LLD::LegacyDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);
        LLD::Logical  = new LLD::LogicalDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);
        LLD::Sessions = new LLD::SessionDB(LLD::FLAGS::CREATE | LLD::FLAGS::FORCE);


        //load wallet
        bool fFirstRun;
        fWalletInitOk = Legacy::Wallet::Initialize(Legacy::WalletDB::DEFAULT_WALLET_DB);
        fWalletLoadOk = (Legacy::Wallet::LoadWallet(fFirstRun) == Legacy::DB_LOAD_OK);


        //initialize chain state
        fChainStateInitOk = TAO::Ledger::ChainState::Initialize();

        //create best chain.
        TAO::Ledger::BlockState state;
        state.nHeight = 200;
        state.nBits   = 555;

        //write best to disk
        LLD::Ledger->WriteBlock(state.GetHash(), state);

        //set best block
        TAO::Ledger::ChainState::tStateBest.store(state);
        TAO::Ledger::ChainState::nBestHeight.store(200);

        //check best
        fBestStateSet = !TAO::Ledger::ChainState::tStateBest.load().IsNull();


        /** Initialize network resources. (Need before RPC/API for WSAStartup call in Windows) **/
        fLLPInitOk = LLP::Initialize();

        /* Create the API instances. */
        TAO::API::Initialize();

        /* Create the Core API Server. */
        LLP::Config CONFIG    = LLP::Config(8080);
        CONFIG.PORT_SSL       = TESTNET_API_SSL_PORT;
        CONFIG.MAX_THREADS    = 10;
        CONFIG.SOCKET_TIMEOUT = 30;
        CONFIG.ENABLE_DDOS    = false;
        CONFIG.ENABLE_REMOTE  = true;
        CONFIG.ENABLE_LISTEN  = true;
        CONFIG.ENABLE_METERS  = false;
        CONFIG.ENABLE_MANAGER = false;
        CONFIG.ENABLE_SSL     = false;

        LLP::API_SERVER = new LLP::Server<LLP::APINode>(CONFIG);
    }


    /** GlobalTestListener
     *
     *  Catch2 test-run listener whose testRunStarting() fires exactly once per process,
     *  before any TEST_CASE executes -- regardless of tag filtering (unlike a TEST_CASE,
     *  which is skipped entirely when excluded by the active filter). Does not fire for
     *  `--list-tests`/`--help` since those return before the test run begins.
     **/
    struct GlobalTestListener : Catch::TestEventListenerBase
    {
        using TestEventListenerBase::TestEventListenerBase;

        void testRunStarting(Catch::TestRunInfo const&) override
        {
            SetupGlobalTestEnvironment();
        }
    };
}
CATCH_REGISTER_LISTENER(GlobalTestListener)


TEST_CASE("Arguments Tests", "[args]")
{
    /* Defensive no-op: GlobalTestListener::testRunStarting() has already performed this
     * setup before this (or any other) test case runs. */
    SetupGlobalTestEnvironment();

    REQUIRE(config::fTestNet.load() == true);
    REQUIRE(config::GetArg("-testnet", 0) == 92349234);
    REQUIRE(config::fMultiuser.load() == true);
    REQUIRE(config::fHybrid.load() == true);

    REQUIRE(fRemovedOldDataDir);

    REQUIRE(fWalletInitOk);
    REQUIRE(fWalletLoadOk);

    REQUIRE(fChainStateInitOk);
    REQUIRE(fBestStateSet);

    REQUIRE(fLLPInitOk);
}
