/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <Util/include/signals.h>
#include <signal.h>
#include <Util/include/args.h>
#include <Util/include/debug.h>

std::condition_variable SHUTDOWN;

/** Shutdown the system and all its subsystems. **/
void Shutdown()
{
    config::fShutdown.store(true);
    SHUTDOWN.notify_all();
}


/** Catch Signal Handler function **/
void HandleSIGTERM(int signum)
{
    /* Static counter to track repeated signals (for emergency exit) */
    static std::atomic<int> signal_count{0};
    
#ifndef WIN32
    if(signum == SIGPIPE)
        return;  // Ignore SIGPIPE
    
    /* Increment signal count */
    int count = signal_count.fetch_add(1);
    
    if(count == 0)
    {
        /* First signal - normal graceful shutdown */
        debug::log(0, "Shutdown signal received - shutting down gracefully...");
        Shutdown();
    }
    else
    {
        /* Second or subsequent signal - force immediate exit */
        debug::log(0, "Emergency shutdown signal received - forcing immediate exit");
        debug::log(0, "WARNING: This may leave resources in inconsistent state");
        std::_Exit(1);  // Immediate exit without cleanup (no debug::Shutdown to avoid conflicts)
    }
#else
    if(signum == SIGINT)
    {
        /* Increment signal count */
        int count = signal_count.fetch_add(1);
        
        if(count == 0)
        {
            /* First signal - normal graceful shutdown */
            debug::log(0, "Shutdown signal received - shutting down gracefully...");
            Shutdown();
        }
        else
        {
            /* Second or subsequent signal - force immediate exit */
            debug::log(0, "Emergency shutdown signal received - forcing immediate exit");
            debug::log(0, "WARNING: This may leave resources in inconsistent state");
            std::_Exit(1);  // Immediate exit without cleanup (no debug::Shutdown to avoid conflicts)
        }
        return;  // Don't call Shutdown again on Windows for SIGINT
    }
    
    /* For other signals, proceed with normal shutdown */
    Shutdown();
#endif
}


/** Setup the signal handlers.**/
void SetupSignals()
{
    /* Handle all the signals with signal handler method. */
    #ifndef WIN32
        // Clean shutdown on SIGTERM
        struct sigaction sa;
        sa.sa_handler = HandleSIGTERM;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        //catch all signals to flag fShutdown for all threads
        sigaction(SIGABRT, &sa, nullptr);
        sigaction(SIGILL, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGPIPE, &sa, nullptr);

    #else
        //catch all signals to flag fShutdown for all threads
        signal(SIGABRT, HandleSIGTERM);
        signal(SIGILL, HandleSIGTERM);
        signal(SIGINT, HandleSIGTERM);
        signal(SIGTERM, HandleSIGTERM);

    #ifdef SIGBREAK
        signal(SIGBREAK, HandleSIGTERM);
    #endif
    #endif
}
