# Stateless Mining Protocol

## Overview

The Stateless Mining Protocol is the production-ready protocol for Nexus blockchain mining, implementing a push notification model with Falcon-1024 quantum-resistant authentication. This protocol enables miners to participate in the network without maintaining full blockchain state.

**Status:** Production (Active since PR #170)  
**Version:** 1.0  
**Date:** 2026-01-12

## Key Features

- ✅ **Push Notifications:** Server-initiated NEW_BLOCK messages (no polling required)
- ✅ **Falcon-1024 Authentication:** Post-quantum secure session management
- ✅ **Wallet-Signed Blocks:** Node operator's wallet signs all blocks (consensus requirement)
- ✅ **216-Byte Templates:** Compact, serialized TritiumBlock format
- ✅ **Channel Isolation:** Prime and Hash miners receive only relevant notifications
- ✅ **Backward Compatible:** Supports both legacy and stateless miners

## Architecture Overview

### Design Principles

1. **Stateless Miners:** Miners don't need full blockchain
2. **Stateful Nodes:** Nodes maintain blockchain and sign blocks with wallet
3. **Push > Poll:** Instant notifications instead of continuous polling
4. **Security Layers:** Falcon auth for sessions, wallet signatures for consensus

### Role Separation

```
┌─────────────────────────────────────────────────┐
│              MINER (Stateless)                  │
├─────────────────────────────────────────────────┤
│ • Falcon keys (session auth)                    │
│ • No blockchain required                        │
│ • Receives 216-byte templates                   │
│ • Performs Proof-of-Work                        │
│ • Submits solutions                             │
└─────────────────────────────────────────────────┘
                      ↕
┌─────────────────────────────────────────────────┐
│               NODE (Stateful)                   │
├─────────────────────────────────────────────────┤
│ • Full blockchain state                         │
│ • Wallet keys (block signing)                   │
│ • Creates 216-byte templates                    │
│ • Pushes templates when blockchain advances     │
│ • Validates submitted solutions                 │
│ • Routes rewards to miner addresses             │
└─────────────────────────────────────────────────┘
```

## Complete Protocol Flow

### Phase 1: Authentication

Miners authenticate using Falcon-1024 signatures for quantum-resistant security.

```
Miner → Node:   MINER_AUTH (0xD000)
                ├─ Genesis (32 bytes)
                ├─ Pubkey length (2 bytes)
                ├─ Falcon-1024 public key (1793 bytes)
                ├─ Miner ID length (2 bytes)
                └─ Miner ID (variable)

Node → Miner:   MINER_AUTH_RESPONSE (0xD001)
                ├─ Status (1 byte): 0x01 = success, 0x00 = failure
                └─ Session ID (4 bytes): SHA256(falcon_pubkey)[0:4]
```

**Key Points:**
- Genesis must exist on blockchain (validation via `LLD::Ledger->HasFirst()`)
- Falcon-1024 public key = 1793 bytes
- Session ID derived from public key hash
- ChaCha20-Poly1305 encryption optional (automatic for remote connections)

### Phase 2: Configuration

Miners configure reward address and mining channel.

```
Miner → Node:   MINER_SET_REWARD (0xD003)
                └─ Reward address (32 bytes): NXS address for mining rewards

Node → Miner:   MINER_REWARD_RESULT (0xD004)
                └─ Status (1 byte): 0x01 = success, 0x00 = failure

Miner → Node:   SET_CHANNEL (0xD005)
                └─ Channel (1 byte): 1 = Prime, 2 = Hash

Node → Miner:   CHANNEL_ACK (0xD006)
                └─ Channel (1 byte): Confirmation
```

**Key Points:**
- Reward address validated for format only (consensus validates existence)
- Channel 0 (Stake) is invalid for stateless mining
- Channel must be set before requesting templates

### Phase 3: Subscription

Miners subscribe to push notifications for their channel.

```
Miner → Node:   MINER_READY (0xD007)
                (empty payload - subscription request)

Node → Miner:   MINER_READY (0xD007)
                └─ Ack (1 byte): 0x01 = subscribed

Node → Miner:   BLOCK_DATA (0x0000)
                └─ Initial template (216 bytes): Serialized TritiumBlock
```

**Key Points:**
- MINER_READY subscribes to channel-specific push notifications
- Node immediately sends current template (no waiting for next block)
- Miners can start hashing immediately after subscription
- **This eliminates the 0.00 GIPS idle problem**

### Phase 4: Mining

Miners iterate nonce values searching for valid Proof-of-Work.

```
[Miner receives 216-byte template]
  └─ Deserialize to extract nonce, difficulty, merkle root
  └─ Initialize nonce = 0

Loop:
  1. Set block.nNonce = current_nonce
  2. Hash block with nonce
  3. Check if hash meets difficulty target
  4. If valid: go to Phase 6 (submit)
  5. If not: increment nonce and repeat
  
[CPU cores show GIPS > 0 during mining]
```

**Key Points:**
- Template is fully signed by node's wallet (consensus requirement)
- Miner only modifies nNonce field (offset 208-215)
- Difficulty target encoded in nBits field (offset 204-207)
- Mining continues until valid nonce found or new template arrives

### Phase 5: Push Notification (Network Event)

When a new block is found on the network, the node pushes updated templates.

```
[Network: New block validated]
  ↓
Node → Miners:  BLOCK_DATA (0x0000)
                └─ Updated template (216 bytes): Next block to mine

[Miners detect new template]
  ↓
Miners:
  1. Detect template changed (compare merkle root)
  2. Abandon current work (nonce search)
  3. Deserialize new template
  4. Reset nonce = 0
  5. Resume mining with new template
```

**Key Points:**
- **Server-push eliminates polling** (0-5s latency → <10ms)
- Only channel-specific miners notified:
  - Prime block → Only Prime miners notified
  - Hash block → Only Hash miners notified
- Miners don't need to poll GET_ROUND
- Templates are channel-filtered at server (50% bandwidth reduction)

### Phase 6: Solution Submission

When a miner finds a valid nonce, they submit the complete block.

```
Miner → Node:   SUBMIT_BLOCK (0x0001)
                └─ Solved block (216 bytes + signature):
                   ├─ Block with winning nonce (216 bytes)
                   └─ Wallet signature (already included by node)

Node:
  1. Validate block structure
  2. Verify Proof-of-Work (check hash meets difficulty)
  3. Validate signature (wallet signature)
  4. Check for stale template (height changed?)
  5. Submit to blockchain

Node → Miner:   BLOCK_ACCEPTED (0x00C8) or BLOCK_REJECTED (0x00C9)
                └─ Result details

[If accepted]
  ↓
Node → All:     BLOCK_DATA (0x0000)
                └─ Next template (216 bytes) to all subscribed miners
```

**Key Points:**
- Block already wallet-signed by node (miner doesn't sign)
- Node validates PoW: `hash(block) < target_from_nBits`
- Stale submissions rejected if blockchain advanced
- Rewards paid to address from MINER_SET_REWARD
- All miners receive next template after block accepted

## Opcode Reference

All opcodes in the `0xD000-0xDFFF` range (StatelessMining namespace).

| Opcode | Name | Direction | Payload | Description |
|--------|------|-----------|---------|-------------|
| `0xD000` | MINER_AUTH | Miner → Node | Genesis(32) + PubkeyLen(2) + Pubkey(1793) + IDLen(2) + ID(var) | Falcon-1024 authentication |
| `0xD001` | MINER_AUTH_RESPONSE | Node → Miner | Status(1) + SessionID(4) | Authentication result |
| `0xD003` | MINER_SET_REWARD | Miner → Node | Address(32) | Set reward address |
| `0xD004` | MINER_REWARD_RESULT | Node → Miner | Status(1) | Reward binding result |
| `0xD005` | SET_CHANNEL | Miner → Node | Channel(1) | Select Prime(1) or Hash(2) |
| `0xD006` | CHANNEL_ACK | Node → Miner | Channel(1) | Channel confirmed |
| `0xD007` | MINER_READY | Miner → Node | Empty | Subscribe to push notifications |
| `0xD007` | MINER_READY | Node → Miner | Ack(1) | Subscription confirmed |
| `0x0000` | BLOCK_DATA | Node → Miner | Block(216) | Mining template (legacy opcode) |
| `0x0001` | SUBMIT_BLOCK | Miner → Node | Block(216+sig) | Submit solution (legacy opcode) |
| `0x00C8` | BLOCK_ACCEPTED | Node → Miner | Details | Solution accepted (legacy opcode) |
| `0x00C9` | BLOCK_REJECTED | Node → Miner | Reason | Solution rejected (legacy opcode) |

**Note:** Template delivery uses legacy opcodes (BLOCK_DATA, SUBMIT_BLOCK) for backward compatibility. Authentication and configuration use new 0xD000+ opcodes.

## Wire Format Specifications

### MessagePacket Structure

All messages use the LLP MessagePacket format:

```
┌────────────────────────────────────┐
│ MessagePacket (8-byte header)      │
├────────────────────────────────────┤
│ [0-1]   MESSAGE (uint16_t)         │  Opcode
│ [2-3]   FLAGS (uint16_t)           │  Protocol flags
│ [4-7]   LENGTH (uint32_t)          │  Payload size
├────────────────────────────────────┤
│ [8...]  DATA (LENGTH bytes)        │  Payload data
└────────────────────────────────────┘
```

**Encoding:** Little-endian  
**Total Size:** 8 + LENGTH bytes

### Template Format (216 Bytes)

The mining template is a serialized TritiumBlock:

```
┌────────────────────────────────────┐
│ TritiumBlock Serialization         │
├────────────────────────────────────┤
│ [0-3]     nVersion (uint32_t)      │  Block version
│ [4-131]   hashPrevBlock (1024-bit) │  Previous block hash
│ [132-195] hashMerkleRoot (512-bit) │  Merkle root
│ [196-199] nChannel (uint32_t)      │  Mining channel (1=Prime, 2=Hash)
│ [200-203] nHeight (uint32_t)       │  Blockchain height
│ [204-207] nBits (uint32_t)         │  Difficulty target
│ [208-215] nNonce (uint64_t)        │  Nonce (miner modifies this)
│           └─ Miner iterates this field during PoW search
└────────────────────────────────────┘
Total: 216 bytes
```

**Key Fields:**
- **nChannel (offset 196):** Tells miner which algorithm to use
- **nHeight (offset 200):** Current blockchain height (for staleness detection)
- **nBits (offset 204):** Compact difficulty target
- **nNonce (offset 208):** **Miner's search space**

**Mining Process:**
1. Node creates template with nNonce = 0
2. Node signs template with wallet
3. Miner receives template, extracts nBits target
4. Miner iterates nNonce from 0 to 2^64-1
5. For each nonce: hash(template) and compare to target
6. If hash < target: submit block with winning nonce

### Complete Packet Example

Template delivery packet (224 bytes total):

```
┌────────────────────────────────────┐
│ MessagePacket Header (8 bytes)     │
├────────────────────────────────────┤
│ MESSAGE:  0x0000 (BLOCK_DATA)      │
│ FLAGS:    0x0000                   │
│ LENGTH:   216 (0x000000D8)         │
├────────────────────────────────────┤
│ DATA: TritiumBlock (216 bytes)     │
│ ├─ nVersion: 8                     │
│ ├─ hashPrevBlock: 0x...            │
│ ├─ hashMerkleRoot: 0x...           │
│ ├─ nChannel: 1 (Prime)             │
│ ├─ nHeight: 6541702                │
│ ├─ nBits: 0x1D00FFFF               │
│ └─ nNonce: 0 (initial)             │
└────────────────────────────────────┘
```

## Security Model

### Two-Layer Authentication

**Layer 1: Falcon Session Authentication**
- Purpose: Secure miner sessions
- Key Type: Disposable Falcon-1024 keys (1793 bytes)
- Scope: Miner-to-node communication
- Lifetime: Session only (can be regenerated)
- Security: Post-quantum resistant

**Layer 2: Wallet Block Signing**
- Purpose: Blockchain consensus
- Key Type: Node operator's wallet keys
- Scope: Block validation across network
- Lifetime: Permanent (tied to node identity)
- Security: Nexus consensus requirement

### Separation of Concerns

```
┌─────────────────────────────────────────────────┐
│ Falcon Authentication (Miner Sessions)          │
├─────────────────────────────────────────────────┤
│ • Proves miner identity to node                 │
│ • Establishes ChaCha20 session key              │
│ • Allows 500+ miners per node                   │
│ • Keys can be rotated/regenerated               │
│ • NOT used for block signing                    │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│ Wallet Signing (Block Consensus)                │
├─────────────────────────────────────────────────┤
│ • Node's wallet signs ALL blocks                │
│ • Required by Nexus consensus                   │
│ • Validates blocks across network               │
│ • Tied to node operator's identity              │
│ • Miners never sign blocks                      │
└─────────────────────────────────────────────────┘
```

**Why This Design?**
- Miners don't need wallet keys (simplified setup)
- Node controls block signing (consensus requirement)
- Rewards routed to miner's specified address
- Separation enables pool operations (hundreds of miners, one node)

### Node Configuration

Node operators must configure wallet auto-unlock:

```bash
# nexus.conf - REQUIRED for block signing
-autologin=username:password    # Wallet auto-login
# OR
-unlock=mining                  # Alternative unlock method

# Optional: Whitelist miner Falcon keys
-minerallowkey=<falcon_pubkey_hash>
```

**Without Wallet Unlock:**
```
Error: Mining not unlocked - use -unlock=mining or -autologin
CRITICAL: Nexus consensus requires wallet-signed blocks
Falcon authentication is for miner sessions, NOT block signing
```

## Error Handling

### Common Rejection Reasons

**OLD_ROUND (Stale Template):**
```
Cause: Block height changed between GET_BLOCK and SUBMIT_BLOCK
Solution: Request new template, resume mining
Miner Action: Already handled by push notifications
```

**INVALID_POW (Proof-of-Work Failed):**
```
Cause: hash(block) > difficulty_target
Solution: Verify nonce calculation, check hash algorithm
Common Issue: Incorrect nonce field offset or endianness
```

**INVALID_SIGNATURE (Wallet Signature Invalid):**
```
Cause: Node wallet not unlocked or signature corrupted
Solution: Check node configuration (-autologin or -unlock=mining)
Pool Operator: Ensure wallet is unlocked before accepting miners
```

**NOT_AUTHENTICATED:**
```
Cause: Miner sent GET_BLOCK or SUBMIT_BLOCK before MINER_AUTH
Solution: Complete authentication flow first
Flow: MINER_AUTH → MINER_AUTH_RESPONSE → MINER_SET_REWARD → SET_CHANNEL → GET_BLOCK
```

**CHANNEL_NOT_SET:**
```
Cause: Miner sent GET_BLOCK without setting channel via SET_CHANNEL
Solution: Send SET_CHANNEL(1 or 2) before requesting templates
```

**INVALID_CHANNEL:**
```
Cause: Miner requested channel 0 (Stake) for stateless mining
Solution: Use channel 1 (Prime) or 2 (Hash) only
Note: Stake mining requires full node + balance (not stateless)
```

### Retry Strategies

**Stale Template (OLD_ROUND):**
- Don't retry old template
- Wait for push notification (automatic)
- Push arrives within <10ms of block validation

**Network Disconnect:**
- Reconnect to node
- Re-authenticate (new Falcon session)
- Re-subscribe (MINER_READY)
- Receive fresh template automatically

**Invalid PoW (Bug):**
- Don't spam submissions
- Fix nonce calculation
- Verify hash algorithm matches channel (Prime vs Hash)
- Test with known-good template

## Performance Considerations

### Push vs Polling Efficiency

**Legacy Polling (GET_ROUND):**
```
Every 5 seconds:
  Miner → Node: GET_ROUND (12 bytes)
  Node → Miner: NEW_ROUND or OLD_ROUND (12 bytes)
  
Latency: 0-5 seconds (average 2.5s)
Bandwidth: 24 bytes × 12 requests/min = 288 bytes/min/miner
CPU: Continuous request processing
```

**Push Notifications (Current):**
```
[Block validated]
  ↓ <10ms
Node → Miners: BLOCK_DATA (224 bytes)

Latency: <10ms (250× faster)
Bandwidth: 224 bytes per block (only when needed)
CPU: Event-driven (80% reduction)
```

**Savings (100 miners, 1 block/minute):**
- Network requests: 100,000 GET_ROUND/day → 1,440 pushes/day (98% reduction)
- Latency: 2.5s average → <10ms (250× faster)
- CPU overhead: Continuous → Event-driven (80% reduction)

### Template Caching

The implementation includes 30× performance improvement via template caching:

```cpp
// First GET_BLOCK for height 6541702:
CreateBlock()  // 30ms - full block generation

// Subsequent GET_BLOCK for same height:
ReuseTemplate()  // 1ms - cache lookup

Speedup: 30× faster for repeated requests
```

**Cache Invalidation:**
- Template cached per (height, channel)
- Invalidated when blockchain advances
- Automatic cleanup on NEW_BLOCK event

### Bandwidth Optimization

**Server-Side Channel Filtering:**
```
Prime block validated:
  ↓
Node checks all connections:
  - Prime miners (channel 1): Send BLOCK_DATA ✓
  - Hash miners (channel 2): Skip (wrong channel) ✗

Result: 50% bandwidth reduction (no cross-channel pollution)
```

**Push > Poll Bandwidth:**
- Polling: 288 bytes/min/miner (continuous)
- Push: ~224 bytes/block (~1-2 blocks/min)
- Savings: 20-50% depending on block rate

## Testing and Validation

### Unit Tests

**Authentication Flow:**
```bash
# Test Falcon-1024 auth with valid keys
# Expected: MINER_AUTH_RESPONSE with session ID

# Test invalid genesis
# Expected: MINER_AUTH_RESPONSE with status 0x00
```

**Template Delivery:**
```bash
# Test MINER_READY subscription
# Expected: Immediate BLOCK_DATA with 216-byte template

# Test channel filtering
# Expected: Prime miners receive Prime blocks only
```

**Solution Validation:**
```bash
# Test valid PoW submission
# Expected: BLOCK_ACCEPTED

# Test stale template submission
# Expected: BLOCK_REJECTED with OLD_ROUND reason
```

### Integration Tests

**End-to-End Mining:**
```bash
# Start node with wallet unlocked
./nexus -autologin=user:pass

# Connect miner (separate process)
./nexus-miner --genesis=<32-byte-hex> --reward=<nxs-address> --channel=prime

# Verify sequence:
1. MINER_AUTH (miner → node)
2. MINER_AUTH_RESPONSE (node → miner, status=0x01)
3. MINER_SET_REWARD (miner → node)
4. MINER_REWARD_RESULT (node → miner, status=0x01)
5. SET_CHANNEL (miner → node, channel=1)
6. CHANNEL_ACK (node → miner)
7. MINER_READY (miner → node)
8. MINER_READY (node → miner, ack=0x01)
9. BLOCK_DATA (node → miner, 216 bytes)
10. Miner shows GIPS > 0.00 (mining active)

# Wait for block found
11. SUBMIT_BLOCK (miner → node)
12. BLOCK_ACCEPTED (node → miner)
13. BLOCK_DATA (node → all miners, next template)
```

### Performance Benchmarking

**Latency Test:**
```bash
# Measure push notification latency
# Expected: <10ms from block validation to miner notification

# Compare with polling
# Legacy: 0-5s (average 2.5s)
# Current: <10ms (250× faster)
```

**Throughput Test:**
```bash
# 500 miners, 1 block/minute
# Expected: All miners notified within 100ms
# Expected: No packet loss
# Expected: Channel filtering working (Prime miners don't receive Hash blocks)
```

## Migration from GET_ROUND

See [MIGRATION_GET_ROUND_TO_GETBLOCK.md](MIGRATION_GET_ROUND_TO_GETBLOCK.md) for detailed migration guide.

**Summary:**
- Old: Polling with GET_ROUND (0x85) → NEW_ROUND (0xCC)
- New: Push with MINER_READY (0xD007) → BLOCK_DATA (0x0000)
- Breaking: GET_ROUND no longer needed
- Benefit: 250× lower latency, 50% bandwidth reduction

## References

### Documentation
- [TEMPLATE_FORMAT.md](TEMPLATE_FORMAT.md) - Detailed wire format specification
- [MIGRATION_GET_ROUND_TO_GETBLOCK.md](MIGRATION_GET_ROUND_TO_GETBLOCK.md) - Migration guide
- [PUSH_NOTIFICATION_PROTOCOL.md](PUSH_NOTIFICATION_PROTOCOL.md) - Push notification details
- [MINING_ARCHITECTURE.md](MINING_ARCHITECTURE.md) - Architecture overview
- [archive/GET_ROUND_PROTOCOL.md](archive/GET_ROUND_PROTOCOL.md) - Legacy protocol (deprecated)

### Source Code
- `src/LLP/include/llp_opcodes.h` - Opcode definitions (lines 199-293)
- `src/LLP/stateless_miner_connection.cpp` - Protocol implementation
- `src/LLP/types/stateless_miner_connection.h` - Connection interface
- `src/TAO/Ledger/types/block.h` - Block structure

### External Resources
- Nexus Mining: https://nexus.io/mining
- NexusMiner (client): https://github.com/Nexusoft/NexusMiner

## Changelog

### Version 1.0 (2026-01-12)
- Initial comprehensive documentation
- Documented push notification model (PR #170)
- 216-byte template format specification
- Opcode reference (0xD000-0xD00C)
- Security model (Falcon + Wallet signatures)
- Performance analysis (push vs polling)
- Migration guide from GET_ROUND

## Glossary

- **Stateless Mining:** Mining without full blockchain state
- **Push Notification:** Server-initiated message (vs client polling)
- **Falcon-1024:** Post-quantum signature algorithm (NIST standard)
- **TritiumBlock:** Nexus block format (216 bytes serialized)
- **Channel:** Mining algorithm (Prime=1, Hash=2, Stake=0)
- **Unified Height:** Total blockchain height (all channels)
- **Channel Height:** Height for specific channel only
- **nBits:** Compact difficulty target encoding
- **nNonce:** 64-bit value miners iterate for PoW
- **Merkle Root:** 512-bit hash of block transactions
- **Wallet Signing:** Node operator's signature on blocks (consensus requirement)

---

**Document Version:** 1.0  
**Last Updated:** 2026-01-12  
**Maintainer:** Nexus Development Team
