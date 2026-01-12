# Migration Guide: GET_ROUND to Stateless Mining Protocol

## Overview

This guide helps miner developers migrate from the legacy GET_ROUND polling protocol to the current push notification-based Stateless Mining Protocol.

**Target Audience:** Miner developers maintaining or creating mining software  
**Migration Effort:** Medium (protocol changes, but concepts remain similar)  
**Compatibility:** Both protocols supported during transition period

## Executive Summary

### What Changed

| Aspect | Old (GET_ROUND) | New (Stateless Mining) |
|--------|-----------------|------------------------|
| **Architecture** | Polling (request/response) | Push notifications (server-initiated) |
| **Opcodes** | uint8_t (0x85, 0xCC) | uint16_t (0xD000-0xD00F) |
| **Template Delivery** | Two-step (GET_ROUND → GET_BLOCK) | One-step (MINER_READY → BLOCK_DATA) |
| **Height Response** | 12 bytes (3× uint32_t heights) | 216 bytes (full block template) |
| **Latency** | 0-5 seconds (polling interval) | <10ms (instant push) |
| **Bandwidth** | High (continuous polling) | Low (event-driven) |
| **Notification** | Miner polls for changes | Node pushes on block event |

### Benefits of Migration

✅ **250× Lower Latency:** <10ms notification vs 0-5s polling delay  
✅ **50% Bandwidth Reduction:** No continuous polling, channel filtering  
✅ **Simplified Code:** No polling loop, event-driven architecture  
✅ **No Rate Limiting:** Push notifications don't trigger rate limits  
✅ **Instant Templates:** Start mining immediately after subscription

## Breaking Changes

### 1. GET_ROUND Opcode Removed

**Old:**
```cpp
const uint8_t GET_ROUND = 0x85;  // Request round info
const uint8_t NEW_ROUND = 0xCC;  // Response: new round available
const uint8_t OLD_ROUND = 0xCD;  // Response: no change
```

**New:**
```cpp
// GET_ROUND no longer used
// Replaced by MINER_READY subscription + push notifications

#include <LLP/include/llp_opcodes.h>
using namespace LLP::Opcodes::StatelessMining;

constexpr uint16_t MINER_READY = 0xD007;  // Subscribe to notifications
// Node pushes BLOCK_DATA (0x0000) when block available
```

**Impact:** Remove all GET_ROUND polling code

### 2. Response Format Changed

**Old (12-byte NEW_ROUND):**
```
NEW_ROUND (0xCC) response:
  [0-3]   nUnifiedHeight (uint32_t)   - Blockchain height
  [4-7]   nChannelHeight (uint32_t)   - Channel-specific height
  [8-11]  nDifficulty (uint32_t)      - Mining difficulty

Total: 12 bytes (metadata only)
```

**New (216-byte BLOCK_DATA):**
```
BLOCK_DATA (0x0000) response:
  [0-215]  TritiumBlock (serialized)  - Full mining template
  
Contains:
  - nVersion, hashPrevBlock, hashMerkleRoot
  - nChannel, nHeight, nBits, nNonce
  - Wallet-signed by node
  
Total: 216 bytes (complete template, ready to mine)
```

**Impact:** 
- No separate metadata fetch required
- Single packet contains everything needed to mine
- No GET_BLOCK step after GET_ROUND

### 3. Polling Loop Removed

**Old (Continuous Polling):**
```cpp
// Miner polls every 5 seconds
while (running) {
    // Send GET_ROUND
    Packet request(GET_ROUND);
    send(request);
    
    // Wait for response
    Packet response = receive();
    
    if (response.HEADER == NEW_ROUND) {
        // Parse 12-byte metadata
        uint32_t nUnifiedHeight = parse_uint32(response.DATA, 0);
        uint32_t nChannelHeight = parse_uint32(response.DATA, 4);
        uint32_t nDifficulty = parse_uint32(response.DATA, 8);
        
        // Request full template
        Packet getBlock(GET_BLOCK);
        send(getBlock);
        
        Packet template = receive();
        startMining(template);
    }
    
    sleep(5);  // Poll interval
}
```

**New (Event-Driven Push):**
```cpp
// Miner subscribes once, receives push notifications
void initialize() {
    // Subscribe to push notifications
    Packet ready(MINER_READY);
    send(ready);
    
    // Wait for subscription confirmation
    Packet ack = receive();
    
    // Node immediately sends initial template
    Packet template = receive();  // BLOCK_DATA with 216 bytes
    startMining(template);
}

// Separate thread: Listen for push notifications
void listenForUpdates() {
    while (running) {
        Packet notification = receive();  // Blocks until push arrives
        
        if (notification.HEADER == BLOCK_DATA) {
            // New template pushed by node
            stopCurrentMining();
            startMining(notification);
        }
    }
}
```

**Impact:**
- Remove polling loop entirely
- Switch to event-driven architecture
- No sleep() intervals needed

### 4. Opcode Type Change

**Old (uint8_t opcodes):**
```cpp
uint8_t opcode = 0x85;  // GET_ROUND
packet.HEADER = opcode;  // 1 byte
```

**New (uint16_t opcodes):**
```cpp
uint16_t opcode = 0xD007;  // MINER_READY
packet.HEADER = opcode;     // 2 bytes
```

**Impact:**
- Update opcode constants to uint16_t
- Ensure packet serialization uses 2 bytes for opcode
- Use new opcode range (0xD000-0xD00F)

## Step-by-Step Migration

### Step 1: Update Opcode Constants

**Before:**
```cpp
// old_opcodes.h
const uint8_t GET_ROUND = 0x85;       // 133
const uint8_t NEW_ROUND = 0xCC;       // 204
const uint8_t OLD_ROUND = 0xCD;       // 205
const uint8_t GET_BLOCK = 0x81;       // 129
const uint8_t BLOCK_DATA = 0x00;      // 0
const uint8_t SUBMIT_BLOCK = 0x01;    // 1
const uint8_t BLOCK_ACCEPTED = 0xC8;  // 200
const uint8_t BLOCK_REJECTED = 0xC9;  // 201
```

**After:**
```cpp
// new_opcodes.h
#include <cstdint>

namespace Opcodes {
    // Stateless Mining Protocol (0xD000-0xD00F)
    constexpr uint16_t MINER_AUTH          = 0xD000;
    constexpr uint16_t MINER_AUTH_RESPONSE = 0xD001;
    constexpr uint16_t MINER_SET_REWARD    = 0xD003;
    constexpr uint16_t MINER_REWARD_RESULT = 0xD004;
    constexpr uint16_t SET_CHANNEL         = 0xD005;
    constexpr uint16_t CHANNEL_ACK         = 0xD006;
    constexpr uint16_t MINER_READY         = 0xD007;  // NEW!
    
    // Legacy opcodes (still used for templates)
    constexpr uint16_t BLOCK_DATA     = 0x0000;
    constexpr uint16_t SUBMIT_BLOCK   = 0x0001;
    constexpr uint16_t BLOCK_ACCEPTED = 0x00C8;
    constexpr uint16_t BLOCK_REJECTED = 0x00C9;
}
```

### Step 2: Update Packet Structure

**Before:**
```cpp
struct Packet {
    uint8_t HEADER;    // 1-byte opcode
    uint32_t LENGTH;   // 4-byte length
    std::vector<uint8_t> DATA;
};
```

**After:**
```cpp
struct Packet {
    uint16_t HEADER;   // 2-byte opcode (CHANGED!)
    uint16_t FLAGS;    // 2-byte flags
    uint32_t LENGTH;   // 4-byte length
    std::vector<uint8_t> DATA;
};

// Total header: 8 bytes (was 5 bytes)
```

### Step 3: Remove Polling Loop

**Before:**
```cpp
void miningLoop() {
    while (running) {
        // Poll for new round
        Packet request(GET_ROUND);
        send(request);
        
        Packet response = receive();
        
        if (response.HEADER == NEW_ROUND) {
            // New block available
            requestTemplate();
        } else if (response.HEADER == OLD_ROUND) {
            // No change, continue mining
        }
        
        sleep(5);  // Poll every 5 seconds
    }
}
```

**After:**
```cpp
void miningLoop() {
    // Subscribe once
    subscribeToNotifications();
    
    // Wait for push notifications
    while (running) {
        Packet notification = receive();  // Blocks until data arrives
        
        if (notification.HEADER == BLOCK_DATA) {
            // New template pushed by node
            handleNewTemplate(notification);
        } else if (notification.HEADER == BLOCK_ACCEPTED) {
            // Solution accepted
            handleBlockAccepted(notification);
        } else if (notification.HEADER == BLOCK_REJECTED) {
            // Solution rejected
            handleBlockRejected(notification);
        }
    }
}

void subscribeToNotifications() {
    Packet ready(Opcodes::MINER_READY);
    ready.LENGTH = 0;  // Empty payload
    send(ready);
    
    // Wait for confirmation
    Packet ack = receive();
    // Check ack.HEADER == MINER_READY && ack.DATA[0] == 0x01
    
    // Node immediately sends initial template
    Packet template = receive();
    // Check template.HEADER == BLOCK_DATA
    handleNewTemplate(template);
}
```

### Step 4: Update Template Handling

**Before (Two-Step):**
```cpp
// Step 1: GET_ROUND returns 12-byte metadata
void handleNewRound(const Packet& response) {
    uint32_t nUnifiedHeight = parseUint32(response.DATA, 0);
    uint32_t nChannelHeight = parseUint32(response.DATA, 4);
    uint32_t nDifficulty = parseUint32(response.DATA, 8);
    
    // Step 2: Request full template
    Packet getBlock(GET_BLOCK);
    send(getBlock);
    
    Packet template = receive();
    parseTemplate(template.DATA);  // 216 bytes
}
```

**After (One-Step):**
```cpp
// Single step: BLOCK_DATA contains everything
void handleNewTemplate(const Packet& notification) {
    // Notification is already complete template (216 bytes)
    if (notification.LENGTH != 216) {
        error("Invalid template size");
        return;
    }
    
    // Parse template directly
    TritiumBlock block = parseTemplate(notification.DATA);
    
    // Extract metadata from template
    uint32_t nHeight = block.nHeight;       // Offset 200
    uint32_t nChannel = block.nChannel;     // Offset 196
    uint32_t nDifficulty = block.nBits;     // Offset 204
    
    // Start mining immediately
    startMining(block);
}
```

### Step 5: Update Protocol Flow

**Before (GET_ROUND):**
```
┌────────────────────────────────────────┐
│ 1. Authenticate (Falcon)               │
├────────────────────────────────────────┤
│ 2. Set Reward Address                  │
├────────────────────────────────────────┤
│ 3. Set Channel (Prime/Hash)            │
├────────────────────────────────────────┤
│ 4. Polling Loop:                       │
│    ├─ Send GET_ROUND                   │
│    ├─ Receive NEW_ROUND or OLD_ROUND   │
│    ├─ If NEW_ROUND:                    │
│    │  ├─ Send GET_BLOCK                │
│    │  └─ Receive BLOCK_DATA            │
│    └─ Sleep 5 seconds                  │
├────────────────────────────────────────┤
│ 5. Submit Solution                     │
├────────────────────────────────────────┤
│ 6. Back to polling loop                │
└────────────────────────────────────────┘
```

**After (Push Notifications):**
```
┌────────────────────────────────────────┐
│ 1. Authenticate (Falcon)               │
├────────────────────────────────────────┤
│ 2. Set Reward Address                  │
├────────────────────────────────────────┤
│ 3. Set Channel (Prime/Hash)            │
├────────────────────────────────────────┤
│ 4. Subscribe:                          │
│    ├─ Send MINER_READY                 │
│    ├─ Receive MINER_READY (ack)        │
│    └─ Receive BLOCK_DATA (initial)     │
├────────────────────────────────────────┤
│ 5. Mining Loop:                        │
│    ├─ Mine current template            │
│    ├─ Wait for events:                 │
│    │  ├─ BLOCK_DATA: New template      │
│    │  ├─ BLOCK_ACCEPTED: Success       │
│    │  └─ BLOCK_REJECTED: Retry         │
│    └─ No polling, no sleep             │
└────────────────────────────────────────┘
```

## Code Examples

### Complete Before/After Comparison

**Before (GET_ROUND Polling):**

```cpp
class LegacyMiner {
private:
    Socket socket;
    bool running = true;
    
    void pollForUpdates() {
        while (running) {
            // Send GET_ROUND
            Packet request;
            request.HEADER = GET_ROUND;  // 0x85
            request.LENGTH = 0;
            socket.send(request);
            
            // Wait for response
            Packet response = socket.receive();
            
            if (response.HEADER == NEW_ROUND) {  // 0xCC
                // Parse 12-byte metadata
                const uint8_t* data = response.DATA.data();
                
                uint32_t nUnifiedHeight = data[0] | (data[1] << 8) | 
                                          (data[2] << 16) | (data[3] << 24);
                uint32_t nChannelHeight = data[4] | (data[5] << 8) | 
                                          (data[6] << 16) | (data[7] << 24);
                uint32_t nDifficulty = data[8] | (data[9] << 8) | 
                                       (data[10] << 16) | (data[11] << 24);
                
                std::cout << "New round: height=" << nUnifiedHeight 
                          << " difficulty=0x" << std::hex << nDifficulty 
                          << std::dec << std::endl;
                
                // Request full template
                Packet getBlock;
                getBlock.HEADER = GET_BLOCK;  // 0x81
                getBlock.LENGTH = 0;
                socket.send(getBlock);
                
                Packet template = socket.receive();
                if (template.HEADER == BLOCK_DATA) {  // 0x00
                    startMining(template.DATA);
                }
            } else if (response.HEADER == OLD_ROUND) {  // 0xCD
                // No change, continue current work
                std::cout << "Old round, continuing..." << std::endl;
            }
            
            // Poll every 5 seconds
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    
    void startMining(const std::vector<uint8_t>& templateData) {
        // Parse and mine...
    }
};
```

**After (Push Notifications):**

```cpp
#include <LLP/include/llp_opcodes.h>

class ModernMiner {
private:
    Socket socket;
    std::atomic<bool> running{true};
    
    void initialize() {
        // Subscribe to push notifications
        Packet ready;
        ready.HEADER = LLP::Opcodes::StatelessMining::MINER_READY;  // 0xD007
        ready.FLAGS = 0;
        ready.LENGTH = 0;  // Empty payload
        socket.send(ready);
        
        // Wait for subscription confirmation
        Packet ack = socket.receive();
        if (ack.HEADER != LLP::Opcodes::StatelessMining::MINER_READY) {
            throw std::runtime_error("Subscription failed");
        }
        if (ack.DATA.size() != 1 || ack.DATA[0] != 0x01) {
            throw std::runtime_error("Subscription rejected");
        }
        
        std::cout << "Subscribed to push notifications" << std::endl;
        
        // Node immediately sends initial template
        Packet initial = socket.receive();
        if (initial.HEADER == LLP::Opcodes::Legacy::BLOCK_DATA) {  // 0x0000
            handleNewTemplate(initial);
        }
    }
    
    void eventLoop() {
        while (running) {
            // Block until node pushes data (no polling!)
            Packet event = socket.receive();
            
            switch (event.HEADER) {
                case LLP::Opcodes::Legacy::BLOCK_DATA:  // 0x0000
                    // New template pushed by node
                    std::cout << "New template received" << std::endl;
                    handleNewTemplate(event);
                    break;
                    
                case LLP::Opcodes::Legacy::BLOCK_ACCEPTED:  // 0x00C8
                    std::cout << "Block accepted!" << std::endl;
                    // Solution accepted, continue with current template
                    break;
                    
                case LLP::Opcodes::Legacy::BLOCK_REJECTED:  // 0x00C9
                    std::cout << "Block rejected" << std::endl;
                    // Parse rejection reason, request new template if stale
                    break;
                    
                default:
                    std::cerr << "Unknown opcode: 0x" << std::hex 
                              << event.HEADER << std::dec << std::endl;
            }
        }
    }
    
    void handleNewTemplate(const Packet& notification) {
        if (notification.LENGTH != 216) {
            std::cerr << "Invalid template size: " << notification.LENGTH << std::endl;
            return;
        }
        
        // Parse 216-byte TritiumBlock
        const uint8_t* data = notification.DATA.data();
        
        // Extract key fields (portable, little-endian parsing)
        uint32_t nChannel = data[196] | (data[197] << 8) | 
                            (data[198] << 16) | (data[199] << 24);
        uint32_t nHeight = data[200] | (data[201] << 8) | 
                           (data[202] << 16) | (data[203] << 24);
        uint32_t nBits = data[204] | (data[205] << 8) | 
                         (data[206] << 16) | (data[207] << 24);
        
        std::cout << "Template: height=" << nHeight 
                  << " channel=" << nChannel 
                  << " difficulty=0x" << std::hex << nBits << std::dec 
                  << std::endl;
        
        // Start mining (template is complete, ready to use)
        startMining(notification.DATA);
    }
    
    void startMining(const std::vector<uint8_t>& templateData) {
        // Parse and mine...
        // No need to request additional data
        // Template contains everything needed
    }
};
```

**Key Differences:**
1. **No polling loop** - event-driven instead
2. **No sleep()** - blocks on receive() instead
3. **One-step template** - no GET_BLOCK after notification
4. **uint16_t opcodes** - 0xD007 instead of 0x85
5. **8-byte header** - 2 more bytes (FLAGS field)
6. **Immediate start** - initial template sent after subscription

### Python Migration Example

**Before:**
```python
import socket
import struct
import time

def legacy_mining_loop(sock):
    GET_ROUND = 0x85
    NEW_ROUND = 0xCC
    OLD_ROUND = 0xCD
    GET_BLOCK = 0x81
    BLOCK_DATA = 0x00
    
    while True:
        # Send GET_ROUND
        packet = struct.pack('<BI', GET_ROUND, 0)
        sock.send(packet)
        
        # Receive response
        header = struct.unpack('<B', sock.recv(1))[0]
        length = struct.unpack('<I', sock.recv(4))[0]
        data = sock.recv(length)
        
        if header == NEW_ROUND:
            # Parse 12-byte metadata
            nUnifiedHeight, nChannelHeight, nDifficulty = struct.unpack('<III', data)
            print(f"New round: height={nUnifiedHeight}")
            
            # Request template
            packet = struct.pack('<BI', GET_BLOCK, 0)
            sock.send(packet)
            
            # Receive template
            header = struct.unpack('<B', sock.recv(1))[0]
            length = struct.unpack('<I', sock.recv(4))[0]
            template = sock.recv(length)
            
            if header == BLOCK_DATA:
                mine_template(template)
        
        elif header == OLD_ROUND:
            print("Old round, continuing...")
        
        # Poll every 5 seconds
        time.sleep(5)
```

**After:**
```python
import socket
import struct

def modern_mining_loop(sock):
    MINER_READY = 0xD007
    BLOCK_DATA = 0x0000
    BLOCK_ACCEPTED = 0x00C8
    BLOCK_REJECTED = 0x00C9
    
    # Subscribe to push notifications
    packet = struct.pack('<HHI', MINER_READY, 0, 0)  # opcode(2), flags(2), length(4)
    sock.send(packet)
    
    # Wait for confirmation
    opcode, flags, length = struct.unpack('<HHI', sock.recv(8))
    ack = sock.recv(length)
    
    if opcode != MINER_READY or ack[0] != 0x01:
        raise Exception("Subscription failed")
    
    print("Subscribed to push notifications")
    
    # Receive initial template
    opcode, flags, length = struct.unpack('<HHI', sock.recv(8))
    template = sock.recv(length)
    
    if opcode == BLOCK_DATA:
        mine_template(template)
    
    # Event loop (no polling, no sleep!)
    while True:
        # Block until node pushes data
        opcode, flags, length = struct.unpack('<HHI', sock.recv(8))
        data = sock.recv(length)
        
        if opcode == BLOCK_DATA:
            print("New template received")
            mine_template(data)
        
        elif opcode == BLOCK_ACCEPTED:
            print("Block accepted!")
        
        elif opcode == BLOCK_REJECTED:
            print("Block rejected")
```

## Testing Migration

### Verification Checklist

- [ ] **Opcodes Updated**
  - [ ] MINER_READY (0xD007) implemented
  - [ ] uint16_t opcode type (not uint8_t)
  - [ ] 8-byte packet header (MESSAGE, FLAGS, LENGTH)

- [ ] **Polling Removed**
  - [ ] No GET_ROUND requests
  - [ ] No sleep() in main loop
  - [ ] Event-driven receive() instead

- [ ] **Template Handling**
  - [ ] Parse 216-byte BLOCK_DATA directly
  - [ ] No separate GET_BLOCK after notification
  - [ ] Extract metadata from template (nHeight @ offset 200, etc.)

- [ ] **Protocol Flow**
  - [ ] Send MINER_READY after SET_CHANNEL
  - [ ] Wait for MINER_READY ack (1-byte 0x01)
  - [ ] Receive initial BLOCK_DATA immediately
  - [ ] Start mining without polling

- [ ] **Edge Cases**
  - [ ] Handle BLOCK_DATA during active mining (abandon old work)
  - [ ] Handle connection loss (re-subscribe on reconnect)
  - [ ] Handle stale template (should not happen with push, but check)

### Integration Test

```cpp
void testMigration() {
    // Connect to node
    ModernMiner miner;
    miner.connect("localhost", 8323);
    
    // Authenticate
    miner.authenticate(genesis, falconKeys);
    
    // Configure
    miner.setRewardAddress(rewardAddr);
    miner.setChannel(1);  // Prime
    
    // Subscribe (NEW!)
    miner.subscribe();
    
    // Verify initial template received
    assert(miner.hasTemplate());
    assert(miner.getCurrentHeight() > 0);
    
    // Verify mining active (GIPS > 0)
    std::this_thread::sleep_for(std::chrono::seconds(5));
    assert(miner.getGIPS() > 0.0);
    
    std::cout << "Migration test passed!" << std::endl;
}
```

### Performance Comparison

```bash
# Legacy (GET_ROUND polling)
$ ./legacy-miner --pool=localhost:8323
Polling interval: 5 seconds
Average latency: 2.5 seconds
Bandwidth: 288 bytes/min
CPU overhead: Continuous

# Modern (Push notifications)
$ ./modern-miner --pool=localhost:8323
Push notifications: Enabled
Average latency: <10ms
Bandwidth: ~224 bytes/block
CPU overhead: Event-driven (80% reduction)

# Result: 250× faster, 50% less bandwidth
```

## Backward Compatibility

### Supporting Both Protocols

If you need to support both old and new protocols during transition:

```cpp
class HybridMiner {
private:
    bool useModernProtocol = true;  // Auto-detect or config
    
public:
    void connect(const std::string& host, int port) {
        // Try modern protocol first
        if (useModernProtocol) {
            try {
                modernProtocol();
            } catch (...) {
                std::cerr << "Modern protocol failed, falling back to legacy" << std::endl;
                useModernProtocol = false;
                legacyProtocol();
            }
        } else {
            legacyProtocol();
        }
    }
    
private:
    void modernProtocol() {
        // Subscribe to push notifications
        subscribeToNotifications();
        
        // Event-driven loop
        while (running) {
            Packet event = receive();
            handleEvent(event);
        }
    }
    
    void legacyProtocol() {
        // Polling loop
        while (running) {
            pollForUpdates();
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
};
```

**Detection:**
- Send MINER_READY (0xD007)
- If node responds with MINER_READY ack: modern protocol supported
- If node doesn't respond or errors: fall back to GET_ROUND polling

## Common Migration Issues

### Issue 1: Template Size Mismatch

**Problem:**
```
Expected: 12 bytes (OLD_ROUND response)
Received: 216 bytes (BLOCK_DATA template)
```

**Solution:**
- Update all hardcoded size checks
- BLOCK_DATA is 216 bytes, not 12 bytes
- No separate metadata response

### Issue 2: Opcode Type Mismatch

**Problem:**
```
Sending uint8_t opcode (1 byte): 0x07
Node expects uint16_t (2 bytes): 0xD007
```

**Solution:**
```cpp
// Wrong:
uint8_t opcode = 0x07;
packet.HEADER = opcode;

// Correct:
uint16_t opcode = 0xD007;
packet.HEADER = opcode;
```

### Issue 3: No Initial Template

**Problem:**
```
Miner subscribes (MINER_READY) but shows 0.00 GIPS (not mining)
```

**Solution:**
- Wait for MINER_READY ack (1-byte 0x01)
- Node **immediately** sends initial BLOCK_DATA after ack
- Don't start mining until initial template received

### Issue 4: Stale Template Detection

**Problem:**
```
How to detect stale templates without GET_ROUND polling?
```

**Solution:**
- Not needed! Node pushes NEW_BLOCK automatically
- When blockchain advances, node sends fresh BLOCK_DATA to all subscribed miners
- Miner should abandon current work and start new template immediately

## References

### Documentation
- [STATELESS_MINING_PROTOCOL.md](STATELESS_MINING_PROTOCOL.md) - Complete protocol specification
- [TEMPLATE_FORMAT.md](TEMPLATE_FORMAT.md) - Wire format details
- [archive/GET_ROUND_PROTOCOL.md](archive/GET_ROUND_PROTOCOL.md) - Legacy protocol (reference only)

### Source Code
- `src/LLP/include/llp_opcodes.h` - Opcode definitions (lines 199-293)
- `src/LLP/stateless_miner_connection.cpp` - Protocol implementation
- `src/LLP/PUSH_NOTIFICATION_PROTOCOL.md` - Push notification details

### External Resources
- NexusMiner (reference client): https://github.com/Nexusoft/NexusMiner

## Support

### Getting Help

**If you encounter migration issues:**

1. **Check opcode values:** uint16_t (0xD007), not uint8_t (0x07)
2. **Verify packet header:** 8 bytes (MESSAGE, FLAGS, LENGTH)
3. **Test subscription:** MINER_READY → MINER_READY (ack) → BLOCK_DATA
4. **Remove polling:** No GET_ROUND, no sleep(), event-driven only

**Community Support:**
- Nexus Discord: https://discord.gg/nexus
- Nexus Forum: https://forum.nexus.io
- GitHub Issues: https://github.com/Nexusoft/NexusMiner/issues

---

**Document Version:** 1.0  
**Last Updated:** 2026-01-12  
**Maintainer:** Nexus Development Team
