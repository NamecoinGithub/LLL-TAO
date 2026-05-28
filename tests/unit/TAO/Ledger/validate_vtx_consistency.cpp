/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

/*
 * Regression tests for ValidateVtxSigchainConsistency() composite C→B anchor fix.
 *
 * These tests use an inline simulation of the validator logic so they do not
 * require a running node, LLD database, or mempool instance.
 */

#include <LLC/include/random.h>

#include <TAO/Ledger/types/transaction.h>
#include <TAO/Ledger/types/genesis.h>

#include <unit/catch2/catch.hpp>

namespace
{
    TAO::Ledger::Transaction MakeTx(const uint256_t& hashGenesis, const uint32_t nSeq,
                                     const uint512_t hashPrevTx = 0)
    {
        TAO::Ledger::Transaction tx;
        tx.hashGenesis = hashGenesis;
        tx.nSequence   = nSeq;
        tx.hashPrevTx  = hashPrevTx;
        tx.nTimestamp  = 1700000000u + nSeq;
        tx.nKeyType    = TAO::Ledger::SIGNATURE::BRAINPOOL;
        tx.nNextType   = TAO::Ledger::SIGNATURE::BRAINPOOL;
        return tx;
    }

    bool SimulateConsistencyCheck(
        const std::vector<std::pair<uint512_t, TAO::Ledger::Transaction>>& vtxPairs,
        const std::map<uint256_t, uint512_t>& mapMempoolLast,
        const std::map<uint256_t, uint512_t>& mapDiskLast)
    {
        std::map<uint256_t, uint512_t> mapLast;

        for(const auto& entry : vtxPairs)
        {
            const uint512_t& txHash = entry.first;
            const TAO::Ledger::Transaction& tx = entry.second;

            if(tx.IsFirst())
            {
                mapLast[tx.hashGenesis] = txHash;
                continue;
            }

            if(tx.hashPrevTx == txHash)
                return false;

            uint512_t hashLast = 0;
            bool fAnchorFound = false;

            if(mapLast.count(tx.hashGenesis))
            {
                hashLast = mapLast.at(tx.hashGenesis);
                fAnchorFound = true;
            }
            else
            {
                uint512_t hashMempoolLast = 0;
                const auto itMempool = mapMempoolLast.find(tx.hashGenesis);
                const bool fMempoolReadOk = (itMempool != mapMempoolLast.end());
                if(fMempoolReadOk)
                    hashMempoolLast = itMempool->second;

                if(fMempoolReadOk && hashMempoolLast != txHash)
                {
                    hashLast = hashMempoolLast;
                    fAnchorFound = true;
                }
                else
                {
                    const auto itDisk = mapDiskLast.find(tx.hashGenesis);
                    if(itDisk != mapDiskLast.end())
                    {
                        hashLast = itDisk->second;
                        fAnchorFound = true;
                    }
                    else
                    {
                        mapLast[tx.hashGenesis] = txHash;
                        continue;
                    }
                }
            }

            if(fAnchorFound && tx.hashPrevTx != hashLast)
                return false;

            mapLast[tx.hashGenesis] = txHash;
        }

        return true;
    }
}


TEST_CASE("ValidateVtxSigchainConsistency: MALFORMED self-as-predecessor rejected", "[validate_vtx_consistency][ledger]")
{
    const uint256_t genesis = TAO::Ledger::Genesis(LLC::GetRand256(), true);

    TAO::Ledger::Transaction txSeq1 = MakeTx(genesis, 1);
    const uint512_t hashSeq1 = txSeq1.GetHash();

    /* Explicitly construct malformed state: predecessor equals self hash. */
    txSeq1.hashPrevTx = hashSeq1;

    std::vector<std::pair<uint512_t, TAO::Ledger::Transaction>> vtx = {{hashSeq1, txSeq1}};

    REQUIRE(SimulateConsistencyCheck(vtx, {}, {}) == false);
}


TEST_CASE("ValidateVtxSigchainConsistency: mempool self-match falls through to disk anchor", "[validate_vtx_consistency][ledger]")
{
    const uint256_t genesis = TAO::Ledger::Genesis(LLC::GetRand256(), true);

    TAO::Ledger::Transaction txSeq0 = MakeTx(genesis, 0);
    const uint512_t hashSeq0 = txSeq0.GetHash();

    TAO::Ledger::Transaction txSeq1 = MakeTx(genesis, 1, hashSeq0);
    const uint512_t hashSeq1 = txSeq1.GetHash();

    std::vector<std::pair<uint512_t, TAO::Ledger::Transaction>> vtx = {{hashSeq1, txSeq1}};

    const std::map<uint256_t, uint512_t> mempoolLast = {{genesis, hashSeq1}}; /* self-match */
    const std::map<uint256_t, uint512_t> diskLast = {{genesis, hashSeq0}};

    REQUIRE(SimulateConsistencyCheck(vtx, mempoolLast, diskLast) == true);
}


TEST_CASE("ValidateVtxSigchainConsistency: non-self mempool newer tx is stale", "[validate_vtx_consistency][ledger]")
{
    const uint256_t genesis = TAO::Ledger::Genesis(LLC::GetRand256(), true);

    TAO::Ledger::Transaction txSeq0 = MakeTx(genesis, 0);
    const uint512_t hashSeq0 = txSeq0.GetHash();

    TAO::Ledger::Transaction txSeq1 = MakeTx(genesis, 1, hashSeq0);
    const uint512_t hashSeq1 = txSeq1.GetHash();

    TAO::Ledger::Transaction txSeq2 = MakeTx(genesis, 2, hashSeq1);
    const uint512_t hashSeq2 = txSeq2.GetHash();

    TAO::Ledger::Transaction txSeq3 = MakeTx(genesis, 3, hashSeq2);
    const uint512_t hashSeq3 = txSeq3.GetHash();

    std::vector<std::pair<uint512_t, TAO::Ledger::Transaction>> vtx = {{hashSeq2, txSeq2}};

    const std::map<uint256_t, uint512_t> mempoolLast = {{genesis, hashSeq3}};
    const std::map<uint256_t, uint512_t> diskLast = {{genesis, hashSeq1}};

    REQUIRE(SimulateConsistencyCheck(vtx, mempoolLast, diskLast) == false);
}


TEST_CASE("ValidateVtxSigchainConsistency: real predecessor in mempool passes", "[validate_vtx_consistency][ledger]")
{
    const uint256_t genesis = TAO::Ledger::Genesis(LLC::GetRand256(), true);

    TAO::Ledger::Transaction txSeq0 = MakeTx(genesis, 0);
    const uint512_t hashSeq0 = txSeq0.GetHash();

    TAO::Ledger::Transaction txSeq1 = MakeTx(genesis, 1, hashSeq0);
    const uint512_t hashSeq1 = txSeq1.GetHash();

    TAO::Ledger::Transaction txSeq2 = MakeTx(genesis, 2, hashSeq1);
    const uint512_t hashSeq2 = txSeq2.GetHash();

    std::vector<std::pair<uint512_t, TAO::Ledger::Transaction>> vtx = {{hashSeq2, txSeq2}};

    const std::map<uint256_t, uint512_t> mempoolLast = {{genesis, hashSeq1}};
    const std::map<uint256_t, uint512_t> diskLast = {{genesis, hashSeq0}};

    REQUIRE(SimulateConsistencyCheck(vtx, mempoolLast, diskLast) == true);
}


TEST_CASE("ValidateVtxSigchainConsistency: no anchor anywhere defers", "[validate_vtx_consistency][ledger]")
{
    const uint256_t genesis = TAO::Ledger::Genesis(LLC::GetRand256(), true);

    TAO::Ledger::Transaction txSeq0 = MakeTx(genesis, 0);
    const uint512_t hashSeq0 = txSeq0.GetHash();

    TAO::Ledger::Transaction txSeq1 = MakeTx(genesis, 1, hashSeq0);
    const uint512_t hashSeq1 = txSeq1.GetHash();

    std::vector<std::pair<uint512_t, TAO::Ledger::Transaction>> vtx = {{hashSeq1, txSeq1}};

    const std::map<uint256_t, uint512_t> mempoolLast = {{genesis, hashSeq1}}; /* self-match */
    const std::map<uint256_t, uint512_t> diskLast; /* no anchor */

    REQUIRE(SimulateConsistencyCheck(vtx, mempoolLast, diskLast) == true);
}


TEST_CASE("ValidateVtxSigchainConsistency: in-block same-genesis mapLast remains correct", "[validate_vtx_consistency][ledger]")
{
    const uint256_t genesis = TAO::Ledger::Genesis(LLC::GetRand256(), true);

    TAO::Ledger::Transaction txSeq0 = MakeTx(genesis, 0);
    const uint512_t hashSeq0 = txSeq0.GetHash();

    TAO::Ledger::Transaction txSeq1 = MakeTx(genesis, 1, hashSeq0);
    const uint512_t hashSeq1 = txSeq1.GetHash();

    TAO::Ledger::Transaction txSeq2 = MakeTx(genesis, 2, hashSeq1);
    const uint512_t hashSeq2 = txSeq2.GetHash();

    std::vector<std::pair<uint512_t, TAO::Ledger::Transaction>> vtx = {
        {hashSeq1, txSeq1},
        {hashSeq2, txSeq2}
    };

    const std::map<uint256_t, uint512_t> mempoolLast = {{genesis, hashSeq2}}; /* latest is tx2 */
    const std::map<uint256_t, uint512_t> diskLast = {{genesis, hashSeq0}};

    REQUIRE(SimulateConsistencyCheck(vtx, mempoolLast, diskLast) == true);
}
