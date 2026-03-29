/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

/*
 * Unit tests for the producer disk-state cross-check added to the SUBMIT_BLOCK
 * path in miner.cpp and stateless_miner_connection.cpp.
 *
 * The producer disk-state cross-check guards against the TOCTOU race where a
 * miner's block template is created with producer.hashPrevTx = hashLast_A, but
 * between template creation and SUBMIT_BLOCK another network block connects the
 * same sigchain and advances disk to hashLast_B.  If the mismatch is not caught
 * early, BlockState::Connect() will reject the block with
 * "prev transaction incorrect sequence".
 *
 * These tests verify the staleness-detection logic WITHOUT needing a running
 * node, LLD, or LLP.  They simulate the disk-state cross-check inline using the
 * same conditional expression used in production code.
 *
 * See docs/STATELESS_MINING_NSEQUENCE_ROOT_CAUSE.md for the full analysis.
 */

#include <LLC/include/random.h>

#include <TAO/Ledger/types/transaction.h>
#include <TAO/Ledger/types/genesis.h>

#include <unit/catch2/catch.hpp>

/* ---------------------------------------------------------------------------
 * Inline helpers mirroring the producer disk-state cross-check in production.
 *
 * In production the check is:
 *
 *   uint512_t hashDiskLast = 0;
 *   if(!pTritium->producer.IsFirst() &&
 *       LLD::Ledger->ReadLast(pTritium->producer.hashGenesis, hashDiskLast))
 *   {
 *       if(pTritium->producer.hashPrevTx != hashDiskLast)
 *           // → reject
 *   }
 *
 * Here we simulate ReadLast() by passing hashDiskLast directly.
 * --------------------------------------------------------------------------- */
namespace
{
    /* Result of the producer staleness check. */
    enum class StalenessResult
    {
        Fresh,          /* producer.hashPrevTx matches disk last → accept */
        Stale,          /* mismatch → reject (race condition detected)    */
        FirstTx,        /* producer.IsFirst() → no predecessor to check  */
        NoDiskRecord,   /* ReadLast() returned false (genesis not on disk) */
    };

    /* Build a minimal transaction for a given genesis and sequence number. */
    TAO::Ledger::Transaction MakeTx(const uint256_t& hashGenesis, const uint32_t nSeq)
    {
        TAO::Ledger::Transaction tx;
        tx.hashGenesis = hashGenesis;
        tx.nSequence   = nSeq;
        tx.nTimestamp  = 1700000000u + nSeq;
        tx.nKeyType    = TAO::Ledger::SIGNATURE::BRAINPOOL;
        tx.nNextType   = TAO::Ledger::SIGNATURE::BRAINPOOL;
        return tx;
    }

    /* Simulate the producer disk-state cross-check.
     *
     *  producer       – the producer transaction from the block template
     *  hashDiskLast   – what LLD::Ledger->ReadLast() would return for
     *                   producer.hashGenesis (zero if not on disk)
     *  fDiskRecordExists – whether ReadLast() succeeded (true if genesis on disk)
     */
    StalenessResult CheckProducerStaleness(
        const TAO::Ledger::Transaction& producer,
        const uint512_t&                hashDiskLast,
        const bool                      fDiskRecordExists)
    {
        /* Mirror of the IsFirst() guard. */
        if(producer.IsFirst())
            return StalenessResult::FirstTx;

        /* Mirror of the ReadLast() return-value guard. */
        if(!fDiskRecordExists)
            return StalenessResult::NoDiskRecord;

        /* Core check. */
        if(producer.hashPrevTx != hashDiskLast)
            return StalenessResult::Stale;

        return StalenessResult::Fresh;
    }
}


/* ===========================================================================
 * Test 1 — Fresh producer: hashPrevTx matches disk last → accept
 *
 * Normal happy-path: the producer was created from the current disk state
 * and no other block has advanced the sigchain between template creation
 * and SUBMIT_BLOCK.
 * =========================================================================== */
TEST_CASE( "Producer disk-state check: fresh producer is accepted", "[ledger]" )
{
    const uint256_t hashGenesis = TAO::Ledger::Genesis(LLC::GetRand256(), true);

    /* Disk last points at seq-0. */
    TAO::Ledger::Transaction txSeq0 = MakeTx(hashGenesis, 0);
    const uint512_t hashSeq0 = txSeq0.GetHash();

    /* Producer is seq-1, predecessor = seq-0 — exactly what disk says. */
    TAO::Ledger::Transaction producer = MakeTx(hashGenesis, 1);
    producer.hashPrevTx = hashSeq0;

    const StalenessResult result =
        CheckProducerStaleness(producer, hashSeq0, /*fDiskRecordExists=*/true);

    REQUIRE(result == StalenessResult::Fresh);
}


/* ===========================================================================
 * Test 2 — Stale producer: disk advanced, mismatch detected → reject
 *
 * Race-condition scenario:
 *   T0: template created — disk last = hashSeq0, producer.hashPrevTx = hashSeq0
 *   T1: another block connects, writes seq-1 to disk → disk last = hashSeq1
 *   T2: SUBMIT_BLOCK arrives — producer.hashPrevTx = hashSeq0 ≠ hashSeq1
 * =========================================================================== */
TEST_CASE( "Producer disk-state check: stale producer is rejected", "[ledger]" )
{
    const uint256_t hashGenesis = TAO::Ledger::Genesis(LLC::GetRand256(), true);

    TAO::Ledger::Transaction txSeq0 = MakeTx(hashGenesis, 0);
    TAO::Ledger::Transaction txSeq1 = MakeTx(hashGenesis, 1);

    const uint512_t hashSeq0 = txSeq0.GetHash();
    const uint512_t hashSeq1 = txSeq1.GetHash();

    /* Template was created when disk last was seq-0. */
    TAO::Ledger::Transaction producer = MakeTx(hashGenesis, 1);
    producer.hashPrevTx = hashSeq0;

    /* Between template creation and SUBMIT_BLOCK, disk advanced to seq-1. */
    const StalenessResult result =
        CheckProducerStaleness(producer, hashSeq1, /*fDiskRecordExists=*/true);

    REQUIRE(result == StalenessResult::Stale);

    /* Verify the hashes are distinct (sanity check for the test itself). */
    REQUIRE(hashSeq0 != hashSeq1);
}


/* ===========================================================================
 * Test 3 — First (genesis) transaction: no predecessor to check → accept
 *
 * If producer.IsFirst() is true (nSequence == 0, hashPrevTx == 0), there is
 * no predecessor to validate.  The check must return early without rejection.
 * =========================================================================== */
TEST_CASE( "Producer disk-state check: first transaction bypasses check", "[ledger]" )
{
    const uint256_t hashGenesis = TAO::Ledger::Genesis(LLC::GetRand256(), true);

    /* Genesis tx: nSequence=0, hashPrevTx=0 → IsFirst() returns true. */
    TAO::Ledger::Transaction producer = MakeTx(hashGenesis, 0);
    producer.hashPrevTx = uint512_t(0);

    /* Even with a non-zero disk last, IsFirst() should short-circuit. */
    const uint512_t hashArbitrary = LLC::GetRand512();
    const StalenessResult result =
        CheckProducerStaleness(producer, hashArbitrary, /*fDiskRecordExists=*/true);

    REQUIRE(result == StalenessResult::FirstTx);
}


/* ===========================================================================
 * Test 4 — Genesis not on disk: ReadLast() fails → accept (new sigchain)
 *
 * When the producer's genesis has no on-disk record (brand new sigchain that
 * hasn't been committed yet), ReadLast() returns false.  The check must not
 * falsely reject the block.
 * =========================================================================== */
TEST_CASE( "Producer disk-state check: no disk record accepts block", "[ledger]" )
{
    const uint256_t hashGenesis = TAO::Ledger::Genesis(LLC::GetRand256(), true);

    /* Producer at seq-1, pointing at seq-0 which hasn't been committed yet. */
    TAO::Ledger::Transaction txSeq0 = MakeTx(hashGenesis, 0);
    const uint512_t hashSeq0 = txSeq0.GetHash();

    TAO::Ledger::Transaction producer = MakeTx(hashGenesis, 1);
    producer.hashPrevTx = hashSeq0;

    /* ReadLast() fails → fDiskRecordExists = false. */
    const StalenessResult result =
        CheckProducerStaleness(producer, uint512_t(0), /*fDiskRecordExists=*/false);

    REQUIRE(result == StalenessResult::NoDiskRecord);
}


/* ===========================================================================
 * Test 5 — Multiple sequence advances: most-recent mismatch caught
 *
 * Simulate a sigchain that has advanced several times between template
 * creation and SUBMIT_BLOCK.  The check should still detect the mismatch
 * regardless of the magnitude of the sequence gap.
 * =========================================================================== */
TEST_CASE( "Producer disk-state check: multi-step sigchain advance detected", "[ledger]" )
{
    const uint256_t hashGenesis = TAO::Ledger::Genesis(LLC::GetRand256(), true);

    /* Build a chain seq-0 → seq-1 → seq-2 → seq-3 → seq-4 */
    TAO::Ledger::Transaction txSeq0 = MakeTx(hashGenesis, 0);
    TAO::Ledger::Transaction txSeq4 = MakeTx(hashGenesis, 4);

    const uint512_t hashSeq0 = txSeq0.GetHash();
    const uint512_t hashSeq4 = txSeq4.GetHash();

    /* Template was created when disk was at seq-0. */
    TAO::Ledger::Transaction producer = MakeTx(hashGenesis, 1);
    producer.hashPrevTx = hashSeq0;

    /* Disk has since advanced to seq-4 (multiple blocks committed). */
    const StalenessResult result =
        CheckProducerStaleness(producer, hashSeq4, /*fDiskRecordExists=*/true);

    REQUIRE(result == StalenessResult::Stale);
    REQUIRE(hashSeq0 != hashSeq4);
}
