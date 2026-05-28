# NSEQ Diagnostic: ValidateVtxSigchainConsistency — Mempool-Aware ReadLast Fix

## Summary

`ValidateVtxSigchainConsistency()` (introduced in PR #481) was using a **disk-only**
`ReadLast()` call to pre-check vtx sigchain consistency before `AcceptMinedBlock()`.
This caused **false rejections** when the miner's sigchain had a valid mempool
transaction that had not yet been flushed to disk.

The fix changes the `ReadLast()` call to use `FLAGS::MEMPOOL`, making the pre-check
consistent with how `CreateTransaction()` and `TritiumBlock::Check()` see sigchain
state when building and validating a template.

---

## The Original Bug

### Root Cause: Disk vs Mempool State Mismatch

When a miner submits a solved block containing vtx transactions, the node runs
`ValidateVtxSigchainConsistency()` before the irreversible `AcceptMinedBlock()` call.

The original implementation used:

```cpp
// BEFORE (disk-only — caused false rejections):
if(!LLD::Ledger->ReadLast(tx.hashGenesis, hashLast))
```

This reads the **on-disk** last transaction hash for the genesis.  However, the miner's
template was built using `CreateTransaction()`, which calls `ReadLast()` with
`FLAGS::MEMPOOL` — checking the mempool first, then falling back to disk.

If the producer's sigchain had a valid mempool transaction (nSequence=N+1) that had
not been committed to disk yet, the disk-only read returned the older hash (nSequence=N).
The pre-check then saw `tx.hashPrevTx != hashLast` and rejected the block even though
the template was perfectly valid.

### Failure Scenario

```
Sigchain state:
  disk:    genesis → tx(nSequence=0)  [genesis tx, on disk]
  mempool: genesis → tx(nSequence=1)  [follow-up tx, mempool only]

Template built by CreateTransaction():
  hashLast = mempool.ReadLast(genesis) = hash(tx_seq1)  ← mempool-aware
  new_tx.nSequence  = 2
  new_tx.hashPrevTx = hash(tx_seq1)                     ← points to mempool tx

Block submitted with vtx=[tx_seq2]:
  ValidateVtxSigchainConsistency() (old):
    ReadLast(genesis) [disk-only] = hash(tx_seq0)        ← stale disk state
    tx.hashPrevTx(hash(tx_seq1)) != hash(tx_seq0)
    → BLOCK_REJECTED ❌  (FALSE REJECTION)

ValidateVtxSigchainConsistency() (fixed):
    ReadLast(genesis, FLAGS::MEMPOOL) = hash(tx_seq1)    ← mempool-aware
    tx.hashPrevTx(hash(tx_seq1)) == hash(tx_seq1)
    → block passes pre-check ✅  → AcceptMinedBlock() succeeds
```

---

## How `ReadLast()` with `FLAGS::MEMPOOL` Works

From `src/LLD/ledger.cpp` (~line 746):

```cpp
bool LedgerDB::ReadLast(const uint256_t& hashGenesis, uint512_t& hashLast, const uint8_t nFlags)
{
    /* Check mempool first when FLAGS::MEMPOOL is requested. */
    if(nFlags == TAO::Ledger::FLAGS::MEMPOOL)
    {
        TAO::Ledger::Transaction tx;
        if(TAO::Ledger::mempool.Get(hashGenesis, tx))
        {
            hashLast = tx.GetHash();
            return true;
        }
    }
    /* Fall back to disk. */
    return Read(std::make_pair(std::string("last"), hashGenesis), hashLast);
}
```

`FLAGS::MEMPOOL` causes the function to check the mempool for the most recent
transaction for the given genesis, then fall back to the on-disk last hash.
This is the same resolution path used by `CreateTransaction()` and
`TritiumBlock::Check()`.

---

## The Fix

In `src/TAO/Ledger/stateless_block_utility.cpp`, the `ValidateVtxSigchainConsistency()`
function now uses:

```cpp
// AFTER (mempool-aware — matches CreateTransaction/TritiumBlock::Check):
if(!LLD::Ledger->ReadLast(tx.hashGenesis, hashLast, TAO::Ledger::FLAGS::MEMPOOL))
```

The comment explaining the choice:

```cpp
/* No prior in-block entry: read mempool-aware last hash.
 * FLAGS::MEMPOOL checks the mempool first then falls back to
 * disk, matching the same state that CreateTransaction() and
 * TritiumBlock::Check() use when building/validating the
 * sigchain. */
```

---

## Upstream Limitation: `BlockState::Connect()`

`BlockState::Connect()` in `src/TAO/Ledger/state.cpp` (lines 1266-1277) uses
disk-only `ReadLast()` **and** does not account for in-block vtx ordering.  This
means it has its own correctness limitation for blocks containing multiple
transactions from the same genesis.

This is an upstream issue for the Nexusoft/LLL-TAO project.  Our pre-check using
`FLAGS::MEMPOOL` is the correct approach: if mempool says the chain is consistent,
the block is genuinely valid and `Connect()` will succeed once the mempool
transactions are flushed to disk during block processing.

---

## Sequence Diagrams

### Before Fix (False Rejection)

```
Miner                   Node                    LLD
  |                       |                      |
  |--- GET_BLOCK -------->|                      |
  |<-- template ----------|  (producer: seq=2,   |
  |   (hashPrevTx=h1)     |   h1=mempool.last)   |
  |                       |                      |
  |--- SUBMIT_BLOCK ------>|                      |
  |                       |-- ReadLast(disk) ---->|
  |                       |<-- hash(seq0) --------|  ← stale!
  |                       |                      |
  |                       | tx.hashPrevTx(h1) ≠ hash(seq0)
  |<-- BLOCK_REJECTED ----|                      |
  |   (false rejection)   |                      |
```

### After Fix (Correct Behaviour)

```
Miner                   Node                    LLD / Mempool
  |                       |                      |
  |--- GET_BLOCK -------->|                      |
  |<-- template ----------|  (producer: seq=2,   |
  |   (hashPrevTx=h1)     |   h1=mempool.last)   |
  |                       |                      |
  |--- SUBMIT_BLOCK ------>|                      |
  |                       |-- ReadLast(MEMPOOL) ->|
  |                       |<-- hash(seq1) --------|  ← correct!
  |                       |                      |
  |                       | tx.hashPrevTx(h1) == hash(seq1)
  |                       |-- AcceptMinedBlock() ->|
  |<-- BLOCK_ACCEPTED ----|                      |
```

---

## Relationship to Other PRs

| PR | Description | Relevance |
|----|-------------|-----------|
| #479 | `CreateTransaction()` hashLast desync fix | Ensures hashPrevTx is set correctly from mempool state |
| #480 | `RefreshProducerIfStale()` producer freshness | Refreshes stale producer before vtx check |
| #481 | `ValidateVtxSigchainConsistency()` introduction | Introduced the pre-check; used disk-only ReadLast |
| this | Fix ReadLast to use FLAGS::MEMPOOL | Corrects false rejections due to mempool/disk mismatch |

---

## Architecture: Stateless Mining Pipeline

```
SUBMIT_BLOCK received
        │
        ▼
RefreshProducerIfStale()        ← refreshes stale producer (hashPrevBlock, sig)
        │
        ▼
ValidateVtxSigchainConsistency()  ← mempool-aware vtx sigchain pre-check (this fix)
        │
        ├── false → BLOCK_REJECTED (miner requests fresh template)
        │
        ▼
ValidateMinedBlock()            ← full block validation
        │
        ▼
AcceptMinedBlock()              ← irreversible acceptance + Connect()
        │
        ▼
BLOCK_ACCEPTED / BLOCK_REJECTED
```

The mempool-aware `ReadLast()` in `ValidateVtxSigchainConsistency()` ensures the
pre-check gate matches the state used during template creation, eliminating false
rejections for valid blocks containing mempool-only vtx transactions.

---

## Regression Update — PR #<this PR>

### Symptom

The `FLAGS::MEMPOOL` fix above introduced a **second-order false-rejection** affecting
the common case where a vtx transaction IS the latest mempool entry for its sigchain.

Production logs showed:

```
mempool.hashLast = 01950ecac4a2e034d3f9
tx               = 01950ecac4a2e034d3f9   ← IDENTICAL
tx.hashPrevTx    = 01711a8736...
→ tx.hashPrevTx != hashLast → BLOCK_REJECTED
```

The tx was being compared to itself. `mempool.Get(genesis)` returns the *latest*
mempool entry for the sigchain. When `AddTransactions()` pulls that latest entry
into the candidate block, `ValidateVtxSigchainConsistency` then asked mempool for
the same thing and got the tx back — comparing it to its own `hashPrevTx` is
guaranteed to fail.

**Impact:** every block containing a non-genesis user vtx was rejected.

### Fix — Composite C→B with hard invariant

The corrected validator uses three sequential checks per vtx entry:

1. **Hard invariant** — `tx.hashPrevTx == tx.GetHash()` is cryptographically
   impossible (predecessor cannot equal self). Reject as MALFORMED with a
   distinct error prefix so operators can grep MALFORMED separately from STALE.

2. **Option C — self-match detection** — if `ReadLast(genesis, FLAGS::MEMPOOL)`
   returns the tx being validated, mempool's "latest" is the tx itself.
   `hashMempoolLast` is the wrong anchor; fall through.

3. **Option B — disk-only fallback** — query `ReadLast(genesis)` (no flags) to
   get the actual on-disk predecessor. Compare against that. If disk also has
   no anchor, defer to `Connect()` (which has its own predecessor walk).

This restores correct behavior for the common case (tx IS mempool's latest)
while preserving the original `FLAGS::MEMPOOL` fix's intent (catch mempool-only
predecessor races). Genuine staleness — where mempool has advanced PAST the
tx in the block — is still detected via the standard mempool path.

### Test coverage

`tests/unit/TAO/Ledger/validate_vtx_consistency.cpp` extended with six invariant
tests pinning each branch of the composite logic.
