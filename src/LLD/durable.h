/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2026

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLD_DURABLE_H
#define NEXUS_LLD_DURABLE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <set>
#include <string>
#include <vector>

namespace LLD
{
    /** Injectable operating-system operations used by durable LLD writes. */
    class DurableIO
    {
    public:
        virtual ~DurableIO() = default;

        virtual FILE* Open(const std::string& strPath, const char* szMode);
        virtual std::size_t Write(FILE* pStream, const void* pData, std::size_t nSize);
        virtual std::size_t Write(std::ostream& cStream, const void* pData, std::size_t nSize);
        virtual bool Flush(FILE* pStream);
        virtual bool Flush(std::ostream& cStream);
        virtual bool SyncFile(FILE* pStream);
        virtual bool Close(FILE* pStream);
        virtual bool SyncDirectoryChain(const std::string& strDirectory);
        virtual bool Truncate(const std::string& strPath);
        virtual bool Read(const std::string& strPath, std::vector<uint8_t>& vData);

        virtual bool SyncFile(const std::string& strPath);

        static DurableIO& Current();

        #ifdef UNIT_TESTS
        static void SetForTesting(DurableIO* pIO);
        #endif
    };


    /** Tracks file and directory metadata that has not yet been synced. */
    class DurabilityTracker
    {
    public:
        void MarkDirty(const std::string& strPath);
        void MarkCreated(const std::string& strPath);
        void MarkDirectoryCreated(const std::string& strPath);

        /** Fsync dirty files and their directory chains. Clears every obligation that succeeds. */
        bool Sync(DurableIO& cIO);

        /** Fsync newly created files and their directories only.
         *  Update-dirty files stay tracked for a later Sync(). */
        bool SyncCreated(DurableIO& cIO);

    private:
        void TrackParentDirectories(const std::string& strPath);
        void RememberCreatedDirectory(const std::string& strPath);
        bool SyncDirectoryLeaves(DurableIO& cIO, std::set<std::string>& setDirectories, bool fClearUnsynced);

        std::set<std::string> setDirtyFiles;
        std::set<std::string> setNewEntries;
        std::set<std::string> setUnsyncedDirectories;
        std::set<std::string> setCreatedDirectories;
    };
}

#endif
