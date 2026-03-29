# Stateless Mining nSequence Root Cause Analysis

## Overview

This document explains the complete failure chain behind the `"prev transaction incorrect sequence"` / `"last hash mismatch"` errors observed in production when a miner submits a valid PoW solution that is subsequently rejected during `BlockState::Connect()`.

---

## 1. Background: Sigchain Sequence Model

Every Nexus account (identified by `hashGenesis`) has a **sigchain** — an ordered linked list of transactions where each transaction references the hash of its predecessor via `hashPrevTx` and carries a monotonically increasing `nSequence` counter.

```
Genesis tx (nSequence=0, hashPrevTx=0)
  └─► tx-1 (nSequence=1, hashPrevTx=hash(genesis-tx))
        └─► tx-2 (nSequence=2, hashPrevTx=hash(tx-1))
              └─► ...
```

`LLD::Ledger->WriteLast(hashGenesis, hashTx)` / `ReadLast(hashGenesis)` is the disk index that tracks the **tip** of each sigchain.

---

## 2. The `CreateTransaction` Three-Source Resolution

`src/TAO/Ledger/create.cpp` builds the producer transaction (coinbase reward claim) during `GET_BLOCK`. It resolves the current sigchain tip from three sources in priority order:

```
1. Sessions DB  (in-memory, per-session state)
2. Mempool       (pending but unconfirmed transactions)
3. Ledger DB     (on-disk confirmed state — lowest priority)
```

The producer is built with:
```
producer.nSequence  = resolved_tip.nSequence + 1
producer.hashPrevTx = hash(resolved_tip)
```

### The hashLast Desync Bug (already fixed)

Before the fix, the mempool branch updated `txPrev` but **not** `hashLast`:

```cpp
// BEFORE (buggy)
if(pMempoolTx->nSequence > txPrev.nSequence)
{
    txPrev = *pMempoolTx;
    // hashLast was NOT updated here!
}
// ...
tx.hashPrevTx = hashLast;  // ← used the stale Sessions or zero hash
```

The fix adds:
```cpp
hashLast = txMem.GetHash();   // ← added one-liner fix
```

This fix is present at `src/TAO/Ledger/create.cpp` in this repository.

**Upstream reference:** The same bug exists in `Nexusoft/LLL-TAO` at commit `ff33dea5fc4e9dc3bf325c059278d6938934bfb9`, lines 84-99 of `src/TAO/Ledger/create.cpp`.

---

## 3. The Race Condition: Template Creation vs. Block Submission

Even with the hashLast fix in place, there is a **time-of-check vs. time-of-use (TOCTOU)** race condition between template creation (`GET_BLOCK`) and block submission (`SUBMIT_BLOCK`).

### Sequence Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                  STATELESS MINING PIPELINE                          │
│                                                                     │
│  TIME ──────────────────────────────────────────────────────►       │
│                                                                     │
│  T0: GET_BLOCK                                                      │
│  ├─ CreateBlock() → CreateProducer() → CreateTransaction()          │
│  ├─ ReadLast(genesis) → hashLast_A  (disk or mempool tip)           │
│  ├─ producer.nSequence = txPrev.nSequence + 1 = N                   │
│  ├─ producer.hashPrevTx = hashLast_A                                │
│  └─ Template cached, sent to miner                                  │
│                                                                     │
│  T1: ⚡ ANOTHER BLOCK CONNECTED BY NETWORK                          │
│  ├─ BlockState::Connect() writes a new tx for the same genesis      │
│  ├─ WriteLast(genesis) → hashLast_B   (disk advanced!)              │
│  └─ Disk tip: nSequence = N  (what was supposed to be ours)         │
│                                                                     │
│  T2: SUBMIT_BLOCK (miner found solution)                            │
│  ├─ RefreshProducerIfStale()                                        │
│  │   └─ ✅ Refreshes producer's nSequence based on vtx / disk last  │
│  ├─ Producer disk-state cross-check                                 │
│  │   ├─ ReadLast(genesis) → hashLast_B                              │
│  │   ├─ producer.hashPrevTx = hashLast_A ≠ hashLast_B              │
│  │   └─ ✅ CATCHES MISMATCH — returns BLOCK_REJECTED early          │
│  └─ Miner requests fresh template with correct hashPrevTx           │
│                                                                     │
│  WITHOUT THE CROSS-CHECK (old behaviour):                           │
│  ├─ AcceptMinedBlock() → Process() → Accept() → Connect()           │
│  │   ├─ ReadLast(genesis) → hashLast_B  (disk-only!)                │
│  │   ├─ producer.hashPrevTx = hashLast_A ≠ hashLast_B              │
│  │   └─ ❌ "prev transaction incorrect sequence"                    │
│  └─ Block REJECTED — but miner was already told ACCEPTED (old bug)  │
└─────────────────────────────────────────────────────────────────────┘
```

### Why `RefreshProducerIfStale()` alone is insufficient

`RefreshProducerIfStale()` in `src/TAO/Ledger/stateless_block_utility.cpp` performs a producer refresh when the vtx chain advances the producer's sigchain. However, it resolves the "true last" by:

1. Walking `block.vtx` to find the highest-sequence tx for the producer's genesis
2. If no vtx entries exist for that genesis, falling back to `LLD::Ledger->ReadLast()`

**The gap:** When another network block commits a transaction from the producer's sigchain to disk between `GET_BLOCK` and `SUBMIT_BLOCK`, `RefreshProducerIfStale()` will attempt a re-signing of the producer with the new `hashPrevTx`. If credential unlocking fails (wrong PIN, session expired, etc.) or the refresh itself fails for any reason, the block would still reach `AcceptMinedBlock()` with a stale producer.

The **producer disk-state cross-check** added in `SUBMIT_BLOCK` provides a final safety net: it verifies `producer.hashPrevTx == disk.ReadLast(producer.hashGenesis)` **after** refresh, and rejects early if there's still a mismatch.

---

## 4. The PR #481 Checkpoint Regression

PR #481 added `ValidateVtxSigchainConsistency()` as a pre-connect guard for all vtx transactions. While conceptually sound, this function used `ReadLast()` (disk-only) to validate vtx entries. Under specific checkpoint advancement scenarios — where another block had just been connected and hardened a new checkpoint — the `IsDescendant()` check in `TritiumBlock::Accept()` would fail because the in-memory checkpoint state had advanced beyond what the mined block's ancestor chain contained.

### Fix

The `ValidateVtxSigchainConsistency()` calls have been **removed** from both `SUBMIT_BLOCK` paths:

- `src/LLP/stateless_miner_connection.cpp`
- `src/LLP/miner.cpp`

The function **declaration and implementation** remain in `stateless_block_utility.h` / `stateless_block_utility.cpp` for future use, once the checkpoint interaction is fully resolved.

---

## 5. Implemented Fixes

### 5.1 Remove `ValidateVtxSigchainConsistency` calls (revert PR #481 regression)

**Files modified:**
- `src/LLP/stateless_miner_connection.cpp` — removed the `ValidateVtxSigchainConsistency` block
- `src/LLP/miner.cpp` — removed the `ValidateVtxSigchainConsistency` block

### 5.2 Add producer disk-state cross-check in SUBMIT_BLOCK

Added the following check in **both** `SUBMIT_BLOCK` handlers, after `RefreshProducerIfStale()` and before `ValidateMinedBlock()`:

```cpp
/* Cross-check producer's hashPrevTx against current disk last.
 * If the disk state has advanced since template creation (e.g., another
 * channel's block committed the producer's sigchain), the producer is
 * stale and AcceptMinedBlock() will fail with "prev transaction incorrect
 * sequence" or "last hash mismatch".  Reject early so the miner gets
 * BLOCK_REJECTED and can request a fresh template. */
{
    uint512_t hashDiskLast = 0;
    if(!pTritium->producer.IsFirst() &&
        LLD::Ledger->ReadLast(pTritium->producer.hashGenesis, hashDiskLast))
    {
        if(pTritium->producer.hashPrevTx != hashDiskLast)
        {
            debug::error(FUNCTION,
                "SUBMIT_BLOCK: producer sigchain stale — "
                "producer.hashPrevTx=", pTritium->producer.hashPrevTx.SubString(),
                " disk.hashLast=", hashDiskLast.SubString(),
                " — rejecting, miner should request fresh template");
            // respond(BLOCK_REJECTED) or respond(STATELESS_BLOCK_REJECTED)
            return true;
        }
    }
}
```

**Why this is simpler and safer than `ValidateVtxSigchainConsistency()`:**
- Checks only the **producer** (the transaction that actually fails in `Connect()`)
- Does not walk all vtx entries (avoiding the in-flight mapLast complexity)
- Does not interact with checkpoint state
- Producer is the coinbase reward claim — if it's stale, the whole block is invalid regardless of vtx

---

## 6. Related Files

| File | Role |
|------|------|
| `src/TAO/Ledger/create.cpp` | `CreateTransaction()` — builds producer; hashLast desync fixed here |
| `src/TAO/Ledger/state.cpp` | `BlockState::Connect()` — validates `hashPrevTx` against `ReadLast()` |
| `src/TAO/Ledger/stateless_block_utility.cpp` | `RefreshProducerIfStale()`, `ValidateVtxSigchainConsistency()` |
| `src/LLP/stateless_miner_connection.cpp` | Stateless SUBMIT_BLOCK handler (port 9323) |
| `src/LLP/miner.cpp` | Legacy SUBMIT_BLOCK handler (port 8323) |
| `tests/unit/TAO/Ledger/create_transaction.cpp` | Unit tests for hashLast desync fix |
| `tests/unit/TAO/Ledger/producer_staleness.cpp` | Unit tests for producer disk-state cross-check |
| `docs/NSEQ_DIAG_MEMPOOL_HASHLAST_BUG.md` | Detailed analysis of the hashLast desync bug |

---

## 7. Error Messages Reference

| Error | Location | Cause |
|-------|----------|-------|
| `"prev transaction incorrect sequence"` | `state.cpp Connect()` | `producer.hashPrevTx` doesn't match `ReadLast()` |
| `"last hash mismatch"` | `state.cpp Connect()` | Same root cause, different code path |
| `"failed to connect transaction"` | `state.cpp Connect()` | Cascaded from above |
| `"failed to set best chain"` | `state.cpp SetBest()` | Cascaded from Connect() failure |
| `"SUBMIT_BLOCK: producer sigchain stale"` | `miner.cpp` / `stateless_miner_connection.cpp` | New early-rejection check |

---

*Last updated: 2026-03-29*
