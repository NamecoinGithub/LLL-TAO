# Blockchain Rollback & Enhanced Mining Logging

## Overview

This document describes the blockchain rollback safety mechanism with **automatic rollback** and enhanced "training wheels" logging features added to help diagnose prime validation failures and block rejections in production.

## Problem Statement

Production nodes were experiencing:
- Prime validation failures: `ERROR: sign_block : Prime validation failed: base number is NOT prime`
- Block rejections: `📥 === SUBMIT_BLOCK: REJECTED (sign_block failed) ===`
- Difficulty in diagnosing why specific nonces were failing validation
- Risk of blockchain forks due to consecutive invalid block submissions

## Features Implemented

### 1. Automatic Rollback After N Consecutive Invalid Submissions

The node now automatically detects consecutive invalid block submissions and triggers a rollback when a threshold is exceeded.

**Key Features:**
- Tracks consecutive invalid block submissions across all mining connections
- Configurable threshold (default: 5 consecutive failures)
- Automatic rollback of N blocks (default: 1 block) when threshold exceeded
- Transaction resurrection: All transactions from rolled-back blocks are automatically re-inserted into the mempool
- Detailed logging of rollback operations

**Configuration Flags:**
```bash
# Set the threshold for automatic rollback (default: 5)
./nexus -rollback-threshold=5

# Set the number of blocks to rollback automatically (default: 1)
./nexus -auto-rollback-blocks=1
```

**Log Output Example:**
```
⚠️  Invalid block submission detected (count: 1)
   Reason: sign_block validation failed
⚠️  Invalid block submission detected (count: 2)
   Reason: sign_block validation failed
...
⚠️  Invalid block submission detected (count: 5)
   Reason: validate_block failed - network rejected or stale
⚠️  === AUTOMATIC ROLLBACK TRIGGERED ===
   Consecutive invalid submissions: 5
   Threshold: 5
   This may indicate a fork or mining issue
   Rolling back 1 block(s) to recover...
   Rolling back block 1000: 7d8f38ee9d9c...
   New best block: a1b2c3d4e5f6...
   New height: 999
✓ Automatic rollback successful
⚠️  === AUTOMATIC ROLLBACK COMPLETE ===
   Transactions from rolled-back blocks have been resurrected to mempool
```

### 2. Transaction Resurrection and Mempool Re-insertion

During rollback operations (both manual and automatic), all non-producer transactions from disconnected blocks are automatically resurrected and re-inserted into the mempool.

**Implementation:**
- Automatically handled in `BlockState::SetBest()` method
- Transactions are disconnected in reverse order to preserve signature chain ordering
- Producer transactions (coinbase, coinstake, hybrid) are excluded from resurrection
- Transactions are re-validated before mempool insertion
- Works for both Tritium and Legacy transactions

**Benefits:**
- Prevents transaction loss during rollbacks
- Maintains network consistency
- Allows transactions to be re-mined in future blocks
- No manual intervention required

### 3. Enhanced Prime Mining Diagnostic Logging

The `sign_block()` function in `src/LLP/stateless_miner_connection.cpp` now includes comprehensive diagnostic output for every step of prime validation:

**Output Example:**
```
🔍 === PRIME VALIDATION DIAGNOSTIC ===
  📊 Input Parameters:
     nNonce: 0x12345678
     hashMerkleRoot: 7d8f38ee...
  📐 Prime Calculation:
     ProofHash: a1b2c3d4...
     hashPrime = ProofHash + nNonce
     hashPrime: e5f6a7b8...
  🔢 Small Divisor Check:
     Testing divisibility by first 11 primes (2,3,5,7,11,13,17,19,23,29,31)
     Result: PASS ✅
  🧪 Fermat Test:
     Testing: hashPrime^(hashPrime-1) mod hashPrime == 1
     Result: PASS ✅
  📏 Cluster Analysis:
     Calculating Cunningham chain offsets...
     Offsets: [2,4,2,4,...]
     Cluster Size: 8
     Prime Difficulty: 7.234567
     Required Difficulty: 6.500000
     RESULT: VALID ✅
```

**Key Information Logged:**
- Input nonce and merkle root
- ProofHash and hashPrime calculation
- Small divisor test results (tests against first 11 primes: 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31)
- Fermat test results (checks if hashPrime^(hashPrime-1) mod hashPrime == 1)
- Cunningham chain offsets and cluster size
- Prime difficulty vs. required difficulty
- Final validation result (VALID ✅ or INVALID ❌)

### 2. Enhanced Hash Mining Diagnostic Logging

For hash channel (channel 2) mining, detailed proof-of-work validation logging:

**Output Example:**
```
🔍 === HASH VALIDATION DIAGNOSTIC ===
  📊 Input:
     nNonce: 0x9abcdef0
  🎯 Proof-of-Work:
     hashProof: 0000000a1b2c...
     nTarget:   00000fff0000...
     nBits:     0x1e0fffff
     Leading Zeros Required: 20
     Leading Zeros Found:    24
     RESULT: VALID ✅
```

**Key Information Logged:**
- Input nonce
- Hash proof value
- Target value and nBits
- Leading zeros required vs. found
- Final validation result

### 4. Block Validation - IsInvalidProof() Method

Added `IsInvalidProof()` method to `TAO::Ledger::Block` class:

**Location:** `src/TAO/Ledger/types/block.h` and `src/TAO/Ledger/block.cpp`

**Purpose:** Programmatically check if a block has invalid Proof-of-Work without throwing errors.

**Usage:**
```cpp
TAO::Ledger::TritiumBlock block;
// ... populate block ...

if(block.IsInvalidProof())
{
    // Handle invalid block
    debug::error("Block has invalid proof of work");
}
```

The method checks:
- For Prime channel (1): Base primality, offset validity, and difficulty threshold
- For Hash channel (2): Hash meets target requirement
- Returns `false` for non-PoW channels (PoS, Hybrid)

### 5. Manual Blockchain Rollback Mechanism

Enhanced the existing rollback mechanism with detailed logging and a new flag name.

**Command Line Flags:**
- `-revertblocks=<N>` (existing)
- `-rollback-invalid-blocks=<N>` (new alias)

Both flags manually rollback the blockchain by N blocks.

**Usage:**
```bash
# Rollback 5 blocks manually
./nexus -rollback-invalid-blocks=5

# Or using the original flag
./nexus -revertblocks=5
```

**Enhanced Logging Output:**
```
⚠️  === BLOCKCHAIN ROLLBACK INITIATED ===
   Reason: Manual rollback requested via -rollback-invalid-blocks flag
   Blocks to rollback: 5
   Current best block: 7d8f38ee9d9c...
   Current height: 1000
   Rolling back block 1000: 7d8f38ee9d9c...
   Rolling back block 999: a1b2c3d4e5f6...
   Rolling back block 998: 9876543210ab...
   Rolling back block 997: fedcba098765...
   Rolling back block 996: 0123456789ab...
   New best block: 0123456789ab...
   New height: 995
✓ Successfully rolled back 5 blocks
⚠️  === BLOCKCHAIN ROLLBACK COMPLETE ===
```

**Logging Includes:**
- Reason for rollback (manual or automatic)
- Number of blocks to rollback
- Current and new best block hashes
- Current and new heights
- Each individual block being rolled back
- Transaction resurrection confirmation
- Success or failure status

### 6. Unit Tests

Created comprehensive unit tests to validate all validation logic:

**Prime Validation Tests** (`tests/unit/TAO/Ledger/prime_validation_tests.cpp`):
- Fermat test accuracy with known primes and composites
- Small divisor check functionality
- PrimeCheck integration tests
- GetOffsets cluster detection
- GetPrimeDifficulty calculation
- Block IsInvalidProof validation

**Hash Validation Tests** (`tests/unit/TAO/Ledger/hash_validation_tests.cpp`):
- Difficulty target conversion (nBits ↔ target)
- Proof hash meets target validation
- BitCount for leading zeros calculation
- GetDifficulty calculation
- Block IsInvalidProof for hash channel

**Running Tests:**
```bash
make -f makefile.cli UNIT_TESTS=1 -j4
./nexus
```

## How to Use

### For Miners

1. **Enable verbose logging** to see all diagnostic output:
   ```bash
   ./nexus -verbose=3
   ```

2. **Monitor logs** for prime validation issues:
   ```bash
   tail -f ~/.Nexus/debug.log | grep "PRIME VALIDATION\|HASH VALIDATION"
   ```

3. **Check for validation failures** in your mining software:
   - Look for `RESULT: INVALID ❌` in logs
   - Check Small Divisor Check results
   - Verify Fermat Test results
   - Confirm prime difficulty meets requirements

### For Node Operators

1. **Configure automatic rollback protection**:
   ```bash
   # Set threshold for automatic rollback (default: 5)
   ./nexus -rollback-threshold=5
   
   # Set number of blocks to auto-rollback (default: 1)
   ./nexus -auto-rollback-blocks=1
   ```

2. **Manual rollback** if bad blocks were accepted:
   ```bash
   ./nexus -rollback-invalid-blocks=5
   ```

3. **Monitor for consecutive rejections** indicating potential fork:
   - Watch for patterns of REJECTED blocks
   - Automatic rollback will trigger if threshold is exceeded
   - Check if multiple miners are submitting similar invalid blocks
   - Review prime validation diagnostics to identify the issue

4. **Enable debug logging** to capture detailed validation steps:
   ```bash
   ./nexus -verbose=3 -debug=1
   ```

## Fork Prevention

The enhanced logging and automatic rollback help prevent forks by:
1. **Early Detection**: Detailed diagnostics catch validation issues before block acceptance
2. **Automatic Recovery**: Consecutive invalid submissions trigger automatic rollback
3. **Transaction Preservation**: All transactions are resurrected to mempool during rollback
4. **Clear Diagnostics**: Each validation step is logged, making issues traceable
5. **Configurable Thresholds**: Node operators can tune sensitivity to their needs
6. **Test Coverage**: Unit tests ensure validation logic is correct

## Technical Details

### Automatic Rollback Mechanism

**Tracking:**
- Static counters track consecutive invalid submissions across all mining connections
- Counter increments on any validation failure (sign_block or validate_block)
- Counter resets to zero on successful block acceptance

**Triggering:**
- Default threshold: 5 consecutive invalid submissions
- Configurable via `-rollback-threshold=<N>`
- Logs warning messages as counter increases

**Execution:**
- Rolls back N blocks (default: 1, configurable via `-auto-rollback-blocks=<N>`)
- Uses existing `BlockState::SetBest()` method
- Automatically handles transaction resurrection

**Transaction Resurrection:**
- Implemented in `BlockState::SetBest()` in `src/TAO/Ledger/state.cpp`
- Disconnects blocks in reverse order to preserve signature chain ordering
- Excludes producer transactions (coinbase, coinstake, hybrid)
- Re-validates and re-inserts transactions into mempool
- Works for both Tritium and Legacy transactions

### Prime Validation Steps

1. **Small Divisor Test**: Check if the number is divisible by first 11 primes (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31)
2. **Fermat Test**: Verify that `hashPrime^(hashPrime-1) mod hashPrime == 1`
3. **Cluster Analysis**: Find Cunningham chain offsets within gap of 12
4. **Difficulty Check**: Ensure cluster difficulty meets required threshold

### Hash Validation Steps

1. **Target Conversion**: Convert nBits to 1024-bit target value
2. **Hash Comparison**: Verify `hashProof <= nTarget`
3. **Leading Zeros**: Calculate and display leading zeros for diagnostics

## Security Considerations

- All validation failures are logged but do not crash the node
- Automatic rollback has safeguards (won't rollback past genesis)
- Rollback operations are logged with full details for audit trail
- Unit tests ensure validation logic cannot be bypassed
- IsInvalidProof() provides safe programmatic validation checking
- Transaction resurrection prevents transaction loss during rollbacks

## Files Modified

- `src/LLP/types/stateless_miner_connection.h` - Added tracking for consecutive invalid blocks
- `src/LLP/stateless_miner_connection.cpp` - Enhanced logging, automatic rollback, invalid block tracking
- `src/TAO/Ledger/types/block.h` - Added IsInvalidProof() declaration
- `src/TAO/Ledger/block.cpp` - Implemented IsInvalidProof() method
- `src/TAO/Ledger/chainstate.cpp` - Enhanced rollback logging
- `tests/unit/TAO/Ledger/prime_validation_tests.cpp` - New test file
- `tests/unit/TAO/Ledger/hash_validation_tests.cpp` - New test file
- `makefile.cli` - Added new test files to build
- `docs/BLOCKCHAIN_ROLLBACK_AND_LOGGING.md` - Comprehensive documentation

## Success Criteria

✅ Node can detect and log invalid blocks with detailed diagnostics  
✅ All prime validation steps are logged in detail  
✅ All hash validation steps are logged in detail  
✅ Unit tests pass with comprehensive coverage of validation logic  
✅ Manual rollback mechanism works with detailed logging  
✅ **Automatic rollback triggers after N consecutive invalid submissions**  
✅ **Transactions are resurrected to mempool during rollback**  
✅ AI and developers can use detailed logs to diagnose issues  

## Configuration Reference

| Flag | Default | Description |
|------|---------|-------------|
| `-rollback-threshold=<N>` | 5 | Number of consecutive invalid submissions before auto-rollback |
| `-auto-rollback-blocks=<N>` | 1 | Number of blocks to rollback automatically |
| `-rollback-invalid-blocks=<N>` | 0 | Manual rollback of N blocks (same as -revertblocks) |
| `-revertblocks=<N>` | 0 | Manual rollback of N blocks (original flag) |
| `-verbose=<N>` | 0 | Logging verbosity (3 recommended for mining diagnostics) |
