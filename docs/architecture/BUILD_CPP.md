# `build.cpp` semantics

**Status:** Current on NODE5  
**Scope:** `src/TAO/API/build.cpp` (`BuildAndAccept` and its session-index rollback), plus the locks it shares with `src/TAO/API/transaction.cpp`, `src/TAO/API/notifications.cpp`, and `src/TAO/API/indexing/`  
**Related:** [Mempool semantics](MEMPOOL.md)

This document is the normative description of how the API builds, accepts, indexes, and rolls back a Tritium transaction. Later agents must not "simplify" the failure path back to `Transaction::Delete()` or `mempool.Remove()` alone.

---

## 1. What `BuildAndAccept` does

`BuildAndAccept` turns a contract list into one or more signed transactions (batches of at most 99 contracts, plus a fee contract when required) and admits each one to the mempool.

Per batch, after `CreateTransaction`, contract append, `AddFee`, `Build`, and `Sign`:

1. If `Authentication::Active(tx.hashGenesis)`, lock `mempool.MUTEX` **before** `Accept`. Hold it until indexing and any rollback finish.
2. `mempool.Accept(tx)`. This commits `Connect(FLAGS::MEMPOOL)` before inserting `mapLedger`, and may also admit orphan or conflict-dependent descendants. See [MEMPOOL.md](MEMPOOL.md).
3. If there is no active API session, return the hash. Do not index.
4. If the session is active but `!mempool.InPool(hashTx)`, throw. `Accept` can return true for a transaction that was never inserted into `mapLedger`. Do not snapshot or index that hash.
5. Lock `Transaction::IndexLock(tx.hashGenesis)` (pool lock already held).
6. Snapshot the SessionDB links `Index()` can mutate.
7. `TAO::API::Transaction::Index(hashTx)`.
8. On index failure, restore the snapshot. Only if that restore succeeds, disconnect the claimed descendant chain and the parent, commit that memory transaction, then `Remove` the maps.
9. On snapshot or restore failure, **leave the accepted mempool entries in place** and throw.

```mermaid
sequenceDiagram
    participant API as BuildAndAccept
    participant Pool as mempool.MUTEX
    participant Idx as IndexLock(genesis)
    participant SDB as SessionDB
    participant Ov as MEMPOOL overlay

    API->>Pool: lock if active session
    API->>Pool: Accept(tx)
    Note over Ov: Connect committed, then mapLedger.<br/>Descendants may already be live.
    alt not InPool
        API-->>API: throw, do not index
    else live
        API->>Idx: lock
        API->>SDB: CaptureSessionIndex
        API->>SDB: Index(hashTx)
        alt Index fails and snapshot is complete
            API->>SDB: RestoreSessionIndex
            API->>Ov: Disconnect descendants, then parent
            API->>Pool: Remove only after TxnCommit
        else snapshot incomplete or restore fails
            API-->>API: throw, keep mapLedger entries
        end
    end
```

---

## 2. Why `Index()` cannot be rolled back with `Delete()`

`Transaction::Index()` is not one atomic write. It returns false after the first failed step, leaving earlier writes committed:

| Step | Not first | First transaction |
|------|-----------|-------------------|
| 1 | Read previous tx, set `hashNextTx`, `WriteTx(prev)` | `WriteFirst(genesis, hash)` |
| 2 | `WriteTx(hash, *this)` | `WriteTx(hash, *this)` |
| 3 | If `hashNextTx == 0`, `WriteLast(genesis, hash)` | same |

`hashNextTx == 0` on the transaction being indexed means this call is publishing the sigchain tip. `Index()` does not publish `WriteLast` when it returns false: the last-link write is the last SessionDB mutation, and a failed `WriteLast` returns before it commits.

`Transaction::Delete()` uses the same non-atomic shape (rewrite previous or erase first/last, then erase the tx, then deindex). A failed `Delete()` can leave a partial sigchain. The failure path in `build.cpp` must **not** call `Delete()`. It restores the pre-index snapshot instead.

`index_registers()` runs only after those writes, and only when the tx is already ledger-indexed (`nStatus == ACCEPTED`). It logs warnings; it does not fail `Index()`.

---

## 3. Snapshot rules

`CaptureSessionIndex` / `RestoreSessionIndex` are file-local. They exist because SessionDB `Read*` returns false for **both** a missing key and an existing value that cannot be deserialized.

| Record | Existence check | Readable value | Confirmed absent |
|--------|-----------------|----------------|------------------|
| Previous tx (non-first) | `HasTx(hashPrevTx)` | `fHavePrev` | no `HasTx` (nothing to restore) |
| First link (first tx) | `HasFirst(genesis)` | `fHaveFirst` | `fAbsentFirst` |
| Last link | `HasLast(genesis)` | `fHaveLast` | `fAbsentLast` |
| This tx | `HasTx(hashTx)` | `fHaveTx` | `fAbsentTx` |

`HasLast` is `Exists("indexing.last", genesis)`. It is true when the key exists even if `ReadLast` cannot decode it. A false `ReadLast` with a true `HasLast` sets `fIncomplete`.

```mermaid
flowchart TD
    R[ReadLast returns false] --> H{HasLast?}
    H -->|no| Absent[fAbsentLast = true<br/>confirmed absence]
    H -->|yes| Bad[fIncomplete = true<br/>do not restore<br/>keep accepted mempool chain]
    ReadOK[ReadLast returns true] --> Keep[fHaveLast = true<br/>WriteLast snapshot on restore]
```

### Restore is fail-closed

- `fIncomplete` is not a successful restore. The caller must keep the accepted mempool entry.
- A confirmed-absent first link is erased only when the current value equals this tx hash. An unreadable first link (`HasFirst && !ReadFirst`) is a restore failure.
- A confirmed-absent tx record is erased only after `ReadTx` succeeds. An existing unreadable tx record is a restore failure.
- A last link is written back only when it was captured readable. If it was not captured and `HasLast` is now true, restore fails. Do not erase a last link on this path: a failed `Index()` did not publish one, and an unreadable one must not be guessed.
- Restore is retried up to 3 times. If it still fails, the error is raised and the pool entry stays.

---

## 4. Mempool rollback after a successful restore

Only after the SessionDB snapshot is restored:

1. `ClaimedDescendants(hashTx)` — nearest child first. This includes orphans `Accept` admitted before it returned.
2. `TxnBegin(FLAGS::MEMPOOL, INSTANCES::MEMORY)`.
3. `Disconnect(FLAGS::MEMPOOL)` from the **end** of that vector back toward the parent, then disconnect the parent. The predecessor must still be live while a child disconnects.
4. On any false return or exception: `TxnAbort`, leave every pool entry in place, throw.
5. `TxnCommit`. On failure: `TxnAbort`, leave the entries, throw.
6. `Remove` descendants (far first) then the parent. `Remove` only erases maps. A failed `Remove` is a warning: the overlay is already undone.

```mermaid
flowchart LR
    P[parent hashTx] --> C1[child]
    C1 --> C2[grandchild]
    C2 --> Disc[Disconnect C2, then C1, then P]
    Disc --> Rem[Remove C2, then C1, then P]
```

---

## 5. Locks

Two different writers touch the same SessionDB sigchain links, and only one of them takes `mempool.MUTEX`.

| Caller | Holds `CLIENT_MUTEX` | Holds `mempool.MUTEX` | Holds `IndexLock` |
|--------|----------------------|------------------------|-------------------|
| `BuildAndAccept` index path | no | yes, before `IndexLock` | yes, across snapshot, `Index`, restore |
| `Indexing::IndexSigchain` (Tritium merkle receive) | yes | no — must not take it here | yes, inside `Transaction::Index` |
| `Indexing::BuildIndexes` | no | no during the index loops | yes, before both rebuild loops; released before `BroadcastUnconfirmed` |
| `Notifications::SanitizeUnconfirmed` | no | yes, before hash collection through Disconnect / Delete / Remove | yes, before hash collection and again across delete; both released before the rebuild `BuildAndAccept` |
| `Transaction::Delete` | no | must already be held if the caller uses it | yes, internally |

`IndexLock` is a 256-way stripe of recursive mutexes, selected by `hashGenesis.Get64(0) % 256`. The same genesis always shares one stripe. It is recursive because `Index()` and `Delete()` lock it even when the caller already holds it.

```mermaid
flowchart TD
    subgraph order [Required order]
        A[Authentication session lock<br/>BuildAndAccept already holds this] --> B[mempool.MUTEX]
        B --> C[Transaction::IndexLock]
    end
    subgraph forbidden [Deadlock if inverted]
        D[IndexLock then mempool.MUTEX]
        E[mempool.MUTEX held into BuildAndAccept<br/>which waits on the authentication lock]
    end
```

`SanitizeUnconfirmed` must take `mempool.MUTEX`, then `IndexLock`, before it collects the unconfirmed hash list, and must still hold both across Disconnect / Delete / Remove. Otherwise a concurrent accept can append a successor during the scan, and `Delete` on the stale predecessor rewrites `indexing.last`. It drops `IndexLock` only while `SanitizeContract` takes `CLIENT_MUTEX` (that path already holds `CLIENT_MUTEX` and then `IndexLock`), then re-reads the tail under both locks and refuses to delete if the tail changed. It must drop both locks before calling `BuildAndAccept`, because `BuildAndAccept` takes the authentication lock and then `mempool.MUTEX`.

`IndexSigchain` must not take `mempool.MUTEX`. It already holds `CLIENT_MUTEX`. Session-index exclusion is `IndexLock`, not the pool lock.

---

## 6. What not to do

- Do not treat `ReadLast() == false` as absence. Call `HasLast()` first.
- Do not report a successful restore when any captured key exists but was unreadable.
- Do not call `Transaction::Delete()` to undo a failed `Index()`.
- Do not `Remove` a live tx without a committed `Disconnect(FLAGS::MEMPOOL)`.
- Do not disconnect the parent before its `mapClaimed` descendants.
- Do not index a hash that `Accept` did not leave in `mapLedger`.
- Do not restore a snapshot that was taken without `IndexLock`, and do not restore one taken outside `mempool.MUTEX` on the API accept path. `Check()` and `SanitizeUnconfirmed` can otherwise delete or replace the links between the snapshot and the restore.
- Do not acquire `mempool.MUTEX` while holding `IndexLock`.

---

## 7. File map

| Symbol | File |
|--------|------|
| `BuildAndAccept` | `src/TAO/API/build.cpp` |
| `CaptureSessionIndex` / `RestoreSessionIndex` | `src/TAO/API/build.cpp` (anonymous namespace) |
| `Transaction::Index` / `Delete` / `IndexLock` | `src/TAO/API/transaction.cpp` |
| `SessionDB::HasLast` / `ReadLast` | `src/LLD/session.cpp` |
| `Indexing::IndexSigchain` | `src/TAO/API/indexing/index.cpp` |
| `Indexing::BuildIndexes` | `src/TAO/API/indexing/build.cpp` |
| `Notifications::SanitizeUnconfirmed` | `src/TAO/API/notifications.cpp` |
