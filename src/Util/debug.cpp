/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <Util/include/debug.h>

#include <Util/include/args.h>
#include <Util/include/config.h>
#include <Util/include/convert.h>
#include <Util/include/filesystem.h>
#include <Util/include/mutex.h>
#include <Util/include/runtime.h>
#include <Util/include/version.h>

#include <cstdarg>
#include <cstdio>
#include <map>
#include <vector>

#include <iostream>
#include <iomanip>

#ifdef WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600    //targeting minimum Windows Vista version for winsock2, etc.
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1  //prevents windows.h from including winsock.h and messing up winsock2.h definitions we use
#endif

#ifndef NOMINMAX
#define NOMINMAX //prevents windows.h from including min/max and potentially interfering with std::min/std::max
#endif

#include <windows.h>

#endif


namespace debug
{
    /* Mutex to lock DEBUG and prevent race conditions. */
    std::mutex DEBUG_MUTEX;

    /* Main file object for debug logging. */
    std::ofstream ssFile;

    /* Determine the last error generated by debug::error. */
    thread_local std::string strLastError;

    /* The debug archive folder path. */
    std::string strLogFolder;

    /* The maximum number of log files to archive. */
    uint32_t nLogFiles;

    /* The maximum size threshold of each log file. */
    uint32_t nLogSizeMB;


    /* Write startup information into the log file */
    void Initialize()
    {
        strLogFolder = config::GetDataDir() + "log/";

        /* Create the debug archive folder if it doesn't exist. */
        if(!filesystem::exists(strLogFolder))
        {
            filesystem::create_directory(strLogFolder);
            printf("created debug folder directory\n");
        }


        /* Initialize the logging file stream. */
        ssFile.open(log_path(0), std::ios::app | std::ios::out);
        if(!ssFile.is_open())
        {
            printf("Unable to initalize system logging\n");
            return;
        }

        /* Get the debug logging configuration parameters (or default if none specified) */
        nLogFiles  = config::GetArg("-logfiles", 20);
        nLogSizeMB = config::GetArg("-logsizeMB", 5);
    }


    /*  Close the debug log file. */
    void Shutdown()
    {
        LOCK(DEBUG_MUTEX);

        if(ssFile.is_open())
            ssFile.close();
    }


    /*  Log startup information. */
    void LogStartup(int argc, char** argv)
    {
        log(0, "Startup time ", convert::DateTimeStrFormat(runtime::timestamp()));
        log(0, version::CLIENT_VERSION_BUILD_STRING);

        /* Log the Operating System. */
    #ifdef WIN32
        log(0, "Microsoft Windows Build (created ", version::CLIENT_DATE, ")");
    #else
        #ifdef MAC_OSX
        log(0, "Mac OSX Build (created ", version::CLIENT_DATE, ")");
        #else
        log(0, "Linux Build (created ", version::CLIENT_DATE, ")");
        #endif
    #endif


        /* Log the configuration file parameters. */
        std::string pathConfigFile = config::GetConfigFile();
        if(!filesystem::exists(pathConfigFile))
            log(0, "No configuration file");

        else
        {
            log(0, "Using configuration file: ", pathConfigFile);


            /* Log configuration file parameters. Need to read them into our own map copy first */
            std::map<std::string, std::string> mapBasicConfig;  //not used
            std::map<std::string, std::vector<std::string> > mapMultiConfig; //All values stored here whether multi or not, will use this

            config::ReadConfigFile(mapBasicConfig, mapMultiConfig);

            std::string confFileParams = "";

            for(const auto& argItem : mapMultiConfig)
            {
                for(int i = 0; i < argItem.second.size(); ++i)
                {
                    confFileParams += argItem.first;

                    /* Check for password/autologin parameters and hide them in the debug output. */
                    if(argItem.first.compare(0, 12, "-rpcpassword") == 0
                    || argItem.first.compare(0, 12, "-apipassword") == 0
                    || argItem.first.compare(0, 9, "-username") == 0
                    || argItem.first.compare(0, 9, "-password") == 0
                    || argItem.first.compare(0, 4, "-pin") == 0)
                        confFileParams += "=XXXXXXXX";
                    else if(!argItem.second[i].empty())
                        confFileParams += "=" + argItem.second[i];

                    confFileParams += " ";
                }
            }

            if(confFileParams == "")
                confFileParams = "(none)";

            log(0, "Configuration file parameters: ", confFileParams);
        }


        /* Log command line parameters (which can override conf file settings) */
        std::string cmdLineParms = "";
        for(int i = 1; i < argc; i++)
        {
            /* Check for password parameters and hide them in the debug output. */
            if(std::string(argv[i]).compare(0, 12, "-rpcpassword") == 0)
                cmdLineParms += "-rpcpassword=XXXXXXXX ";

            else if(std::string(argv[i]).compare(0, 12, "-apipassword") == 0)
                cmdLineParms += "-apipassword=XXXXXXXX ";

            else if(std::string(argv[i]).compare(0, 9, "-username") == 0)
                cmdLineParms += "-username=XXXXXXXX ";

            else if(std::string(argv[i]).compare(0, 9, "-password") == 0)
                cmdLineParms += "-password=XXXXXXXX ";

            else if(std::string(argv[i]).compare(0, 4, "-pin") == 0)
                cmdLineParms += "-pin=XXXXXXXX ";

            else
                cmdLineParms += std::string(argv[i]) + " ";
        }

        if(cmdLineParms == "")
            cmdLineParms = "(none)";

        log(0, "Command line parameters: ", cmdLineParms);
        log(0, "");
        log(0, "");
    }


    /* Special Specification for HTTP Protocol. */
    std::string rfc1123Time()
    {
        LOCK(DEBUG_MUTEX); //gmtime and localtime are not thread safe together

        char buffer[64];
        time_t now;
        time(&now);

        struct tm* now_gmt = gmtime(&now);
        std::string locale(setlocale(LC_TIME, nullptr));
        setlocale(LC_TIME, "C"); // we want posix (aka "C") weekday/month strings
        strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S +0000", now_gmt);
        setlocale(LC_TIME, locale.c_str());
        return std::string(buffer);
    }


    /*  Writes log output to console and debug file with timestamps.
     *  Encapsulated log for improved compile time. Not thread safe. */
    void log_(time_t &timestamp, std::string &debug_str)
    {
        #ifndef UNIT_TESTS

        /* Build the timestamp */
        std::string time_str = safe_printstr(
            "[",
            std::put_time(std::localtime(&timestamp), "%H:%M:%S"),
            ".",
            std::setfill('0'),
            std::setw(3),
            (runtime::timestamp(true) % 1000),
            "] ");

        /* Get the final timestamped debug string. */
        std::string final_str = time_str + debug_str;

        /* Dump it to the console. */
        std::cout << final_str << std::endl;

        /* Write it to the debug file. */
        ssFile << final_str << std::endl;

        /* Check if the current file should be archived and take action. */
        check_log_archive(ssFile);

        #endif
    }


    /* Gets the last error string logged via debug::error and clears the last error */
    std::string GetLastError()
    {
        std::string strRet = strLastError;
        strLastError = "";
        return strRet;
    }


    /*  Checks if the current debug log should be closed and archived. */
    void check_log_archive(std::ofstream &outFile)
    {
        /* If the file is not open don't bother with archive. */
        if(!outFile.is_open())
            return;

        /* Get the current position to determine number of bytes. */
        uint32_t nBytes = outFile.tellp();

        /* Get the max log size in bytes. */
        uint32_t nMaxLogSizeBytes = nLogSizeMB << 20;

        /* Check if the log size is exceeded. */
        if(nBytes > nMaxLogSizeBytes)
        {
            /* Close the current debug.log file. */
            outFile.close();

            /* Get the number of debug files. */
            uint32_t nDebugFiles = debug_filecount();

            /* Shift the archived debug file name indices by 1. */
            for(int32_t i = nDebugFiles-1; i >= 0; --i)
            {
                /* If the oldest file will exceed the max amount of files, delete it. */
                if(i + 1 >= nLogFiles)
                    filesystem::remove(log_path(i));
                /* Otherwise, rename the files in reverse order. */
                else
                    filesystem::rename(log_path(i), log_path(i+1));
            }

            /* Open the new debug file. */
            outFile.open(log_path(0), std::ios::app | std::ios::out);
            if(!outFile.is_open())
            {
                printf("Unable to start a new debug file\n");
                return;
            }
        }
    }


    /*  Returns the number of debug files present in the debug directory. */
    uint32_t debug_filecount()
    {
        uint32_t nCount = 0;

        /* Loop through the max file count and check if the file exists. */
        for(uint32_t i = 0; i < nLogFiles; ++i)
        {
            if(filesystem::exists(log_path(i)))
                ++nCount;
        }

        return nCount;
    }


    /*  Builds an indexed debug log path for a file. */
    std::string log_path(uint32_t nIndex)
    {
        return strLogFolder + std::to_string(nIndex) + ".log";
    }

}
