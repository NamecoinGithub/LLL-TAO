/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2026

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLD/durable.h>

#include <Util/include/config.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <limits>

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace LLD
{
    namespace
    {
        #ifdef UNIT_TESTS
        std::atomic<DurableIO*> pTestIO{nullptr};
        #endif
    }


    FILE* DurableIO::Open(const std::string& strPath, const char* szMode)
    {
        return std::fopen(strPath.c_str(), szMode);
    }


    std::size_t DurableIO::Write(FILE* pStream, const void* pData, const std::size_t nSize)
    {
        return std::fwrite(pData, 1, nSize, pStream);
    }


    std::size_t DurableIO::Write(std::ostream& cStream, const void* pData, const std::size_t nSize)
    {
        cStream.write(static_cast<const char*>(pData), static_cast<std::streamsize>(nSize));
        return cStream ? nSize : 0;
    }


    bool DurableIO::Flush(FILE* pStream)
    {
        return std::fflush(pStream) == 0;
    }


    bool DurableIO::Flush(std::ostream& cStream)
    {
        cStream.flush();
        return static_cast<bool>(cStream);
    }


    bool DurableIO::SyncFile(FILE* pStream)
    {
        #ifdef WIN32
        return _commit(_fileno(pStream)) == 0;
        #else
        return fsync(fileno(pStream)) == 0;
        #endif
    }


    bool DurableIO::Close(FILE* pStream)
    {
        return std::fclose(pStream) == 0;
    }


    bool DurableIO::SyncDirectoryChain(const std::string& strDirectory)
    {
        return config::SyncDataDirectoryChain(strDirectory);
    }


    bool DurableIO::Truncate(const std::string& strPath)
    {
        FILE* pStream = Open(strPath, "wb");
        return pStream && Close(pStream);
    }


    bool DurableIO::Read(const std::string& strPath, std::vector<uint8_t>& vData)
    {
        FILE* pStream = Open(strPath, "rb");
        if(!pStream)
            return false;

        bool fRead = std::fseek(pStream, 0, SEEK_END) == 0;
        const long nSize = fRead ? std::ftell(pStream) : -1;
        fRead = fRead && nSize >= 0
            && static_cast<unsigned long long>(nSize) <= std::numeric_limits<uint32_t>::max()
            && std::fseek(pStream, 0, SEEK_SET) == 0;

        if(fRead)
        {
            vData.resize(static_cast<std::size_t>(nSize));
            fRead = vData.empty()
                || std::fread(vData.data(), 1, vData.size(), pStream) == vData.size();
        }

        return Close(pStream) && fRead;
    }


    bool DurableIO::SyncFile(const std::string& strPath)
    {
        FILE* pStream = Open(strPath, "rb+");
        if(!pStream)
            return false;

        const bool fSynced = SyncFile(pStream);
        const bool fClosed = Close(pStream);
        return fSynced && fClosed;
    }


    DurableIO& DurableIO::Current()
    {
        #ifdef UNIT_TESTS
        if(DurableIO* pIO = pTestIO.load())
            return *pIO;
        #endif

        static DurableIO cSystemIO;
        return cSystemIO;
    }


    #ifdef UNIT_TESTS
    void DurableIO::SetForTesting(DurableIO* pIO)
    {
        pTestIO.store(pIO);
    }
    #endif


    void DurabilityTracker::MarkDirty(const std::string& strPath)
    {
        if(!strPath.empty())
        {
            setDirtyFiles.insert(strPath);
            TrackParentDirectories(strPath);
        }
    }


    void DurabilityTracker::MarkCreated(const std::string& strPath)
    {
        if(strPath.empty())
            return;

        setNewEntries.insert(strPath);
        MarkDirty(strPath);
        RememberCreatedDirectory(strPath);
    }


    void DurabilityTracker::MarkDirectoryCreated(const std::string& strPath)
    {
        if(strPath.empty())
            return;

        setNewEntries.insert(strPath);
        TrackParentDirectories(strPath);
        RememberCreatedDirectory(strPath);
    }


    void DurabilityTracker::TrackParentDirectories(const std::string& strPath)
    {
        std::filesystem::path cDataDirectory =
            std::filesystem::path(config::GetDataDir()).lexically_normal();
        if(cDataDirectory.has_relative_path() && cDataDirectory.filename().empty())
            cDataDirectory = cDataDirectory.parent_path();

        std::filesystem::path cDirectory =
            std::filesystem::path(strPath).parent_path().lexically_normal();

        while(!cDirectory.empty())
        {
            setUnsyncedDirectories.insert(cDirectory.string());

            if(cDirectory == cDataDirectory)
                break;

            const std::filesystem::path cParent = cDirectory.parent_path();
            if(cParent == cDirectory)
                break;

            cDirectory = cParent;
        }
    }


    void DurabilityTracker::RememberCreatedDirectory(const std::string& strPath)
    {
        if(strPath.empty())
            return;

        std::filesystem::path cDataDirectory =
            std::filesystem::path(config::GetDataDir()).lexically_normal();
        if(cDataDirectory.has_relative_path() && cDataDirectory.filename().empty())
            cDataDirectory = cDataDirectory.parent_path();

        std::filesystem::path cDirectory =
            std::filesystem::path(strPath).lexically_normal();
        std::error_code ec;
        if(!std::filesystem::is_directory(cDirectory, ec))
            cDirectory = cDirectory.parent_path();

        while(!cDirectory.empty())
        {
            setCreatedDirectories.insert(cDirectory.string());

            if(cDirectory == cDataDirectory)
                break;

            const std::filesystem::path cParent = cDirectory.parent_path();
            if(cParent == cDirectory)
                break;

            cDirectory = cParent;
        }
    }


    namespace
    {
        bool IsAncestorOrSame(const std::string& strAncestor, const std::string& strDescendant)
        {
            const std::filesystem::path cAncestor =
                std::filesystem::path(strAncestor).lexically_normal();
            const std::filesystem::path cDescendant =
                std::filesystem::path(strDescendant).lexically_normal();

            auto itAncestor = cAncestor.begin();
            auto itDescendant = cDescendant.begin();
            for(; itAncestor != cAncestor.end() && itDescendant != cDescendant.end();
                ++itAncestor, ++itDescendant)
            {
                if(*itAncestor != *itDescendant)
                    return false;
            }

            return itAncestor == cAncestor.end();
        }
    }


    bool DurabilityTracker::SyncDirectoryLeaves(DurableIO& cIO,
                                                std::set<std::string>& setDirectories,
                                                const bool fClearUnsynced)
    {
        std::vector<std::string> vLeafDirectories;
        for(const std::string& strDirectory : setDirectories)
        {
            const bool fHasDescendant = std::any_of(
                setDirectories.begin(), setDirectories.end(),
                [&strDirectory](const std::string& strCandidate)
                {
                    return strCandidate != strDirectory
                        && IsAncestorOrSame(strDirectory, strCandidate);
                });

            if(!fHasDescendant)
                vLeafDirectories.push_back(strDirectory);
        }

        for(const std::string& strLeaf : vLeafDirectories)
        {
            if(!cIO.SyncDirectoryChain(strLeaf))
                return false;

            for(auto it = setDirectories.begin(); it != setDirectories.end();)
            {
                if(IsAncestorOrSame(*it, strLeaf))
                    it = setDirectories.erase(it);
                else
                    ++it;
            }

            if(!fClearUnsynced)
                continue;

            for(auto it = setUnsyncedDirectories.begin(); it != setUnsyncedDirectories.end();)
            {
                if(IsAncestorOrSame(*it, strLeaf))
                    it = setUnsyncedDirectories.erase(it);
                else
                    ++it;
            }
        }

        return true;
    }


    bool DurabilityTracker::SyncCreated(DurableIO& cIO)
    {
        std::vector<std::string> vFiles;
        std::vector<std::string> vMissing;
        for(const std::string& strEntry : setNewEntries)
        {
            std::error_code ec;
            const std::filesystem::file_status nStatus =
                std::filesystem::symlink_status(strEntry, ec);
            if(ec == std::errc::no_such_file_or_directory)
            {
                vMissing.push_back(strEntry);
                continue;
            }

            if(ec)
                return false;

            if(std::filesystem::is_regular_file(nStatus))
                vFiles.push_back(strEntry);
            else if(!std::filesystem::is_directory(nStatus))
                return false;
        }

        for(const std::string& strMissing : vMissing)
        {
            setNewEntries.erase(strMissing);
            setDirtyFiles.erase(strMissing);
            setCreatedDirectories.erase(strMissing);
        }

        /* Created files are durable after this fsync. Leave every other dirty
         * update tracked so a deferred commit does not write back the keychain. */
        for(const std::string& strFile : vFiles)
        {
            if(!cIO.SyncFile(strFile))
                return false;

            setDirtyFiles.erase(strFile);
            setNewEntries.erase(strFile);
        }

        if(!SyncDirectoryLeaves(cIO, setCreatedDirectories, true))
            return false;

        for(auto it = setNewEntries.begin(); it != setNewEntries.end();)
        {
            std::error_code ec;
            if(std::filesystem::is_directory(*it, ec))
                it = setNewEntries.erase(it);
            else
                ++it;
        }

        return true;
    }


    bool DurabilityTracker::Sync(DurableIO& cIO)
    {
        for(auto it = setDirtyFiles.begin(); it != setDirtyFiles.end();)
        {
            if(!cIO.SyncFile(*it))
                return false;

            it = setDirtyFiles.erase(it);
        }

        const auto IsAncestorOrSame = [](const std::string& strAncestor, const std::string& strDescendant)
        {
            const std::filesystem::path cAncestor =
                std::filesystem::path(strAncestor).lexically_normal();
            const std::filesystem::path cDescendant =
                std::filesystem::path(strDescendant).lexically_normal();

            auto itAncestor = cAncestor.begin();
            auto itDescendant = cDescendant.begin();
            for(; itAncestor != cAncestor.end() && itDescendant != cDescendant.end();
                ++itAncestor, ++itDescendant)
            {
                if(*itAncestor != *itDescendant)
                    return false;
            }

            return itAncestor == cAncestor.end();
        };

        std::vector<std::string> vLeafDirectories;
        for(const std::string& strDirectory : setUnsyncedDirectories)
        {
            const bool fHasDescendant = std::any_of(
                setUnsyncedDirectories.begin(), setUnsyncedDirectories.end(),
                [&IsAncestorOrSame, &strDirectory](const std::string& strCandidate)
                {
                    return strCandidate != strDirectory
                        && IsAncestorOrSame(strDirectory, strCandidate);
                });

            if(!fHasDescendant)
                vLeafDirectories.push_back(strDirectory);
        }

        for(const std::string& strLeaf : vLeafDirectories)
        {
            if(!cIO.SyncDirectoryChain(strLeaf))
                return false;

            for(auto it = setUnsyncedDirectories.begin(); it != setUnsyncedDirectories.end();)
            {
                if(IsAncestorOrSame(*it, strLeaf))
                    it = setUnsyncedDirectories.erase(it);
                else
                    ++it;
            }
        }

        setNewEntries.clear();
        setCreatedDirectories.clear();
        return true;
    }
}
