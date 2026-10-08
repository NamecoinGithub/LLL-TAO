# Sync-commit coalescing

PR #715 made every LLD commit fsync the sector file and the keychain files it
touched. During Legacy-era full sync that keychain is the production
`256 * 256 * 64` bucket hashmap, about 180 MB. One best-chain block is one
CONSENSUS `TxnCommit()`. Fsyncing dirty 4 KB pages of that file on every block
stalls `LegacyBlock::Accept()` and shows up as a sustained multi-megabyte
writeback. PR #717 only lets readers proceed during that sync. It does not
reduce the write volume.

This patch keeps the #715 crash rules and stops doing that fsync on every
block. It is not a config option and it is not a fast-sync mode.

## Call site

```cpp
bool TxnCommit(uint8_t nFlags = 0,
               uint16_t nInstances = INSTANCES::CONSENSUS,
               uint32_t nSyncCommitBlocks = SYNC_COMMIT_BLOCKS_DEFAULT);
```

`SYNC_COMMIT_BLOCKS` is 32. `SYNC_COMMIT_BYTES` is 8 MiB of parked journals.
The node binary defaults the third argument to 32. Unit tests default it to 1
so existing journal-release tests keep a per-commit barrier. Passing `1` is
the A/B equivalent of the rejected `-synccommitblocks` option.

A commit still returns only after its journal decision is durable. It may
return before the sector and keychain data fsync. That fsync happens when any
of these is true:

- `nSyncCommitBlocks < 2`
- `config::fShutdown` is set
- deferred commits in that recovery group plus this one reach `nSyncCommitBlocks`
- that group's parked journal bytes plus this commit reach `SYNC_COMMIT_BYTES`

CONSENSUS and MERKLE do not share those counters. A MERKLE flush cannot reset a
CONSENSUS batch or delete its parked journals, including journals on the
Contract and Register databases both groups use. The other group still flushes
on its own 32-commit or 8 MiB barrier.

`BlockState::SetBest()` still publishes genesis, checkpoints, and ChainState
only after `TxnCommit()` returns. That order is unchanged. A deferred return
is safe to publish because the parked journal, not the data fsync, is the
crash record.

## What is durable when

```mermaid
sequenceDiagram
    participant Accept as LegacyBlock::Accept
    participant Guard as TransactionGuard
    participant LLD as TxnCommit
    participant Journal as journal.dat
    participant Park as journal.N.pending
    participant Data as sector and keychain

    Accept->>Guard: TxnBegin (CONSENSUS group)
    Accept->>LLD: legacy tx writes and state.Index
    LLD->>Data: SyncCreated only (new files, not dirty hashmap)
    LLD->>Journal: append records and "commit", fsync file and directory
    Note over Journal: Decision is durable here
    LLD->>Data: apply payloads
    LLD->>Data: SyncCreated for files created by that apply
    Note over Data: New files are durable; dirty pages stay deferred
    LLD->>Park: rename journal.dat and fsync directory
    LLD-->>Accept: COMMITTED (ChainState may publish)
    Note over Park,Data: Crash rolls the parked journal forward

    Accept->>LLD: later commit hits 32 blocks, 8 MiB, or shutdown
    LLD->>Data: fsync every file still tracked dirty
    LLD->>Park: delete pending journals only after that fsync
    LLD->>Journal: truncate journal.dat and fsync directory
```

Journal checkpoint still happens before apply. Data fsync still happens before
a parked journal is deleted. Those two orders are the #715 invariants. The
change is how many commits share one data fsync.

## Coordinator

`DurableCommitCoordinator` still owns begin, checkpoint, apply, and release.
LLD recovery stays in that layer. There is no `BlockchainSync` class.

On a deferred commit, after every selected participant has a complete journal:

1. Apply each participant with `TxnCommit(false)`. That fsyncs files created during the apply, not update-dirty pages. A crash after the journal is parked must still be able to open those files while replaying.
2. Rename that participant's `journal.dat` to `journal.<8-digit sequence>.pending`. The sequence is an unsigned decimal with no sign. Names that do not round-trip to that filename fail recovery and are not discarded.
3. Fsync the journal directory.
4. Leave the transaction owner committed. Do not truncate the journal.

The next checkpoint opens a fresh `journal.dat`. A commit marker cannot share
a file with the next transaction, because `TxnRecovery()` stops at the first
`"commit"`.

On a flush:

1. Apply with `TxnCommit(true)`, which fsyncs sector files and every keychain
   file the durability tracker still has marked dirty, including earlier
   deferred applies.
2. If this recovery group parked journals, delete those sequences and fsync
   the directory. The other group's sequences stay.
3. Truncate `journal.dat` as before.

Shutdown takes the flush path when `fShutdown` is set. Process exit also
fsyncs sector and keychain files in the database destructor and discards
parked journals only after both syncs succeed and startup recovery succeeded.
A failed replay, failed data sync, or `RECOVERY_REQUIRED` outcome leaves the
parked journals in place.

## Recovery

Startup replays `journal.*.pending` in sequence order before it inspects
`journal.dat`. A client recovers the MERKLE group. A full node recovers that
group as well whenever Logical or Client has parked journals, then recovers
CONSENSUS. Shared Contract and Register sequences owned by the other group are
not discarded. Replay is idempotent.

A participant is satisfied when `journal.dat` is a complete commit, or when
`journal.dat` is missing or empty and the participant has the group's latest
pending sequence. A non-empty journal without `"commit"` is still incomplete.

If the group is satisfied, complete `journal.dat` participants are applied and
fsynced. Pending-only dirtiness is fsynced. Parked journals are deleted only
after that fsync. Then `journal.dat` is released.

If the group is not satisfied, older parked journals are synced and discarded,
and the incomplete `journal.dat` is truncated without being applied.

## What this does not do

- It does not skip the journal, the journal fsync, or the directory fsync.
- It does not drop payload bytes. The same sector and keychain bytes are
  written. They are fsynced in batches instead of once per block.
- It does not weaken reorg atomicity. A reorg is still one CONSENSUS
  transaction. Its journal is parked or flushed as a group.
- It does not add a nexus.conf switch. Call `TxnCommit(..., 1)` for a
  per-commit data barrier.

Passing `1` from a steady-state path, after catch-up, is the supported way to
leave the coalesced policy. Shutdown and recovery already return to a full
data barrier without a mode flag.
