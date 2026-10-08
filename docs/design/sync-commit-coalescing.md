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

CONSENSUS and MERKLE do not share those counters. A MERKLE commit does not
count toward the CONSENSUS threshold. A data barrier is newer than every parked
journal, because the coordinator lock admits one commit at a time. The barrier
therefore syncs both groups and deletes every parked sequence, lowest sequence
first. Leaving an earlier other-group journal would replay it over the barrier.
Until a barrier, each group's journals stay in place, including copies on the
shared Contract and Register databases.

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
2. Rename that participant's `journal.dat` to `journal.<8-digit sequence>.pending`. The sequence is an unsigned decimal with no sign. Names that do not round-trip to that filename fail recovery and are not discarded. The rename rejects any existing destination, including a dangling symlink, instead of replacing that malformed record.
3. Fsync the journal directory.
4. Leave the transaction owner committed. Do not truncate the journal.

The next checkpoint opens a fresh `journal.dat`. A commit marker cannot share
a file with the next transaction, because `TxnRecovery()` stops at the first
`"commit"`.

On a flush:

1. Apply with `TxnCommit(true)`, which fsyncs sector files and every keychain
   file the durability tracker still has marked dirty, including earlier
   deferred applies.
2. Sync the other recovery group if it still has parked journals. Delete every
   parked sequence, lowest sequence first, and fsync the directory. Every
   parked journal is older than this barrier.
3. Truncate `journal.dat` as before.

Shutdown takes the flush path when `fShutdown` is set. Process exit
quiesces both recovery groups before it discards any parked sequence, lowest
sequence first. Contract and Register are destroyed first and can
hold both groups' sequences, so a per-database destructor must not delete a
sequence until the coordinator has synced the whole group that owns it.
If that quiesce or retire fails, the discard latch is cleared before
destructors run. A failed replay, failed data sync, or `RECOVERY_REQUIRED`
outcome leaves the parked journals in place. A directory or symlink named as
a journal is not a regular crash record and is retained rather than deleted
or followed.

## Recovery

Startup replays `journal.*.pending` in global sequence order before it inspects
an unparked `journal.dat`. Sequence numbers are shared by CONSENSUS and MERKLE,
so a full node does not replay one group and then the other. A client recovers
the MERKLE group. A parked sequence is replayed and discarded only when every
participant has that file, or when the missing copies are still that newest
sequence's unparked `journal.dat`. A partial rename is kept as a unit. A
sequence that exists only on Contract or Register is a partial MERKLE park when
Logical or Client still has `journal.dat`, and a partial CONSENSUS park when
only those exclusive journals do. It is not given to the other group.
`journal.dat` that was not part of that partial park is the newest commit and
is applied after every parked sequence. A `journal.dat` consumed as a partial
park is released after that sequence's data sync, before a later sequence or
the complete-journal pass can apply it again. A non-regular `journal.dat`,
including a symlink, blocks that partial-park path.

A participant is satisfied when `journal.dat` is a complete commit, or when
`journal.dat` is missing or empty and the participant has the group's latest
pending sequence. A non-empty journal without `"commit"` is still incomplete.

If the group is satisfied, complete `journal.dat` participants are applied and
fsynced. Pending-only dirtiness is fsynced. Parked journals are deleted only
after that fsync. Then `journal.dat` is released. If the group is not fully
satisfied, those complete journals are not applied and are not truncated.
Recovery fails so a later checkpoint cannot append over that crash record.

If a sequence is missing from any participant and is not the unparked
`journal.dat` of that same commit, that sequence and every higher one stay on
disk. Recovery fails instead of applying the subset and deleting it. An
incomplete `journal.dat` that never reached `"commit"` is still truncated when
the group has no partial parked sequence left to preserve.

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
