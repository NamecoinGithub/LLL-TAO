# Template Format Specification

## Overview

This document provides the detailed wire format specification for Nexus mining templates. Templates are 216-byte serialized TritiumBlock structures delivered to miners for Proof-of-Work computation.

**Status:** Production (Active)  
**Version:** 1.0  
**Date:** 2026-01-12

## Quick Reference

```
Template Size: 216 bytes (serialized TritiumBlock)
Packet Size:   224 bytes (8-byte header + 216-byte template)
Encoding:      Little-endian
Opcode:        BLOCK_DATA (0x0000)
```

## Complete Packet Structure

### Full Mining Template Packet (224 bytes)

```
┌──────────────────────────────────────────────────┐
│ MessagePacket Header (8 bytes)                   │
├────────────────┬─────────────────────────────────┤
│ Offset │ Field │ Type     │ Value              │ Description
├────────┼───────┼──────────┼────────────────────┼─────────────────
│ 0-1    │ MSG   │ uint16_t │ 0x0000             │ BLOCK_DATA opcode
│ 2-3    │ FLAGS │ uint16_t │ 0x0000             │ Protocol flags
│ 4-7    │ LEN   │ uint32_t │ 216 (0x000000D8)   │ Payload size
├────────────────┴─────────────────────────────────┤
│ Payload: TritiumBlock Serialization (216 bytes)  │
├──────────────────────────────────────────────────┤
│ [See TritiumBlock Format below]                  │
└──────────────────────────────────────────────────┘

Total: 8 + 216 = 224 bytes
```

## TritiumBlock Format (216 bytes)

### Field-by-Field Breakdown

```
┌──────────────────────────────────────────────────────────┐
│ TritiumBlock Serialization (216 bytes)                   │
├────────┬─────────────────┬──────────┬────────────────────┤
│ Offset │ Field           │ Type     │ Size               │ Description
├────────┼─────────────────┼──────────┼────────────────────┼──────────────────────
│ 0-3    │ nVersion        │ uint32_t │ 4 bytes            │ Block version (8 for Tritium)
│ 4-131  │ hashPrevBlock   │ uint1024 │ 128 bytes          │ Previous block hash (1024-bit)
│ 132-195│ hashMerkleRoot  │ uint512  │ 64 bytes           │ Merkle root (512-bit)
│ 196-199│ nChannel        │ uint32_t │ 4 bytes            │ Mining channel (1=Prime, 2=Hash)
│ 200-203│ nHeight         │ uint32_t │ 4 bytes            │ Blockchain height
│ 204-207│ nBits           │ uint32_t │ 4 bytes            │ Difficulty target (compact)
│ 208-215│ nNonce          │ uint64_t │ 8 bytes            │ Nonce (MINER MODIFIES THIS)
└────────┴─────────────────┴──────────┴────────────────────┴──────────────────────┘

Total: 216 bytes (little-endian encoding)
```

### Critical Fields for Miners

**nChannel (offset 196-199):**
- Determines which mining algorithm to use
- `1` = Prime channel (Fermat prime number mining)
- `2` = Hash channel (SK1024 hashing)
- `0` = Stake (not valid for stateless mining)

**nHeight (offset 200-203):**
- Current blockchain height (unified across all channels)
- Used for staleness detection
- Increments with every block across all channels

**nBits (offset 204-207):**
- Compact encoding of difficulty target
- Format: `0xNNTTTTTT` where:
  - `NN` = exponent
  - `TTTTTT` = mantissa
- Example: `0x1D00FFFF` = target of `0x00FFFF * 2^(8*(0x1D-3))`

**nNonce (offset 208-215):**
- **MINER'S SEARCH SPACE**
- 64-bit value (0 to 2^64-1)
- Miner iterates this field during PoW search
- Node sends template with nNonce = 0
- Miner increments until hash < target

### Read-Only Fields

**nVersion (offset 0-3):**
- Block version identifier
- Current value: `8` (Tritium)
- Do not modify

**hashPrevBlock (offset 4-131):**
- Hash of previous block (1024-bit)
- Links to blockchain history
- Do not modify

**hashMerkleRoot (offset 132-195):**
- Root hash of block transactions (512-bit)
- Wallet-signed by node
- Do not modify (except for nNonce iteration changes hash)

## Parsing Examples

### C++ Reference Implementation

```cpp
#include <cstdint>
#include <vector>
#include <cstring>

struct TritiumBlock {
    uint32_t nVersion;
    uint8_t  hashPrevBlock[128];  // 1024-bit = 128 bytes
    uint8_t  hashMerkleRoot[64];  // 512-bit = 64 bytes
    uint32_t nChannel;
    uint32_t nHeight;
    uint32_t nBits;
    uint64_t nNonce;
};

// Parse 216-byte template into TritiumBlock struct
bool ParseTemplate(const std::vector<uint8_t>& vTemplate, TritiumBlock& block)
{
    if (vTemplate.size() != 216) {
        return false;
    }
    
    const uint8_t* data = vTemplate.data();
    size_t offset = 0;
    
    // Parse nVersion (4 bytes, little-endian)
    block.nVersion = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    offset += 4;
    
    // Parse hashPrevBlock (128 bytes)
    std::memcpy(block.hashPrevBlock, data + offset, 128);
    offset += 128;
    
    // Parse hashMerkleRoot (64 bytes)
    std::memcpy(block.hashMerkleRoot, data + offset, 64);
    offset += 64;
    
    // Parse nChannel (4 bytes, little-endian)
    block.nChannel = data[offset] | (data[offset+1] << 8) | 
                     (data[offset+2] << 16) | (data[offset+3] << 24);
    offset += 4;
    
    // Parse nHeight (4 bytes, little-endian)
    block.nHeight = data[offset] | (data[offset+1] << 8) | 
                    (data[offset+2] << 16) | (data[offset+3] << 24);
    offset += 4;
    
    // Parse nBits (4 bytes, little-endian)
    block.nBits = data[offset] | (data[offset+1] << 8) | 
                  (data[offset+2] << 16) | (data[offset+3] << 24);
    offset += 4;
    
    // Parse nNonce (8 bytes, little-endian)
    block.nNonce = (uint64_t)data[offset] | 
                   ((uint64_t)data[offset+1] << 8) | 
                   ((uint64_t)data[offset+2] << 16) | 
                   ((uint64_t)data[offset+3] << 24) |
                   ((uint64_t)data[offset+4] << 32) | 
                   ((uint64_t)data[offset+5] << 40) | 
                   ((uint64_t)data[offset+6] << 48) | 
                   ((uint64_t)data[offset+7] << 56);
    
    return true;
}

// Serialize TritiumBlock back to 216 bytes (for submission)
std::vector<uint8_t> SerializeBlock(const TritiumBlock& block)
{
    std::vector<uint8_t> vData(216);
    size_t offset = 0;
    
    // Serialize nVersion (little-endian)
    vData[offset++] = (block.nVersion >> 0) & 0xFF;
    vData[offset++] = (block.nVersion >> 8) & 0xFF;
    vData[offset++] = (block.nVersion >> 16) & 0xFF;
    vData[offset++] = (block.nVersion >> 24) & 0xFF;
    
    // Serialize hashPrevBlock
    std::memcpy(vData.data() + offset, block.hashPrevBlock, 128);
    offset += 128;
    
    // Serialize hashMerkleRoot
    std::memcpy(vData.data() + offset, block.hashMerkleRoot, 64);
    offset += 64;
    
    // Serialize nChannel (little-endian)
    vData[offset++] = (block.nChannel >> 0) & 0xFF;
    vData[offset++] = (block.nChannel >> 8) & 0xFF;
    vData[offset++] = (block.nChannel >> 16) & 0xFF;
    vData[offset++] = (block.nChannel >> 24) & 0xFF;
    
    // Serialize nHeight (little-endian)
    vData[offset++] = (block.nHeight >> 0) & 0xFF;
    vData[offset++] = (block.nHeight >> 8) & 0xFF;
    vData[offset++] = (block.nHeight >> 16) & 0xFF;
    vData[offset++] = (block.nHeight >> 24) & 0xFF;
    
    // Serialize nBits (little-endian)
    vData[offset++] = (block.nBits >> 0) & 0xFF;
    vData[offset++] = (block.nBits >> 8) & 0xFF;
    vData[offset++] = (block.nBits >> 16) & 0xFF;
    vData[offset++] = (block.nBits >> 24) & 0xFF;
    
    // Serialize nNonce (little-endian, 8 bytes)
    vData[offset++] = (block.nNonce >> 0) & 0xFF;
    vData[offset++] = (block.nNonce >> 8) & 0xFF;
    vData[offset++] = (block.nNonce >> 16) & 0xFF;
    vData[offset++] = (block.nNonce >> 24) & 0xFF;
    vData[offset++] = (block.nNonce >> 32) & 0xFF;
    vData[offset++] = (block.nNonce >> 40) & 0xFF;
    vData[offset++] = (block.nNonce >> 48) & 0xFF;
    vData[offset++] = (block.nNonce >> 56) & 0xFF;
    
    return vData;
}
```

### Python Reference Implementation

```python
import struct

class TritiumBlock:
    def __init__(self):
        self.nVersion = 0
        self.hashPrevBlock = bytes(128)  # 1024-bit
        self.hashMerkleRoot = bytes(64)  # 512-bit
        self.nChannel = 0
        self.nHeight = 0
        self.nBits = 0
        self.nNonce = 0

def parse_template(template_bytes):
    """Parse 216-byte template into TritiumBlock object."""
    if len(template_bytes) != 216:
        raise ValueError(f"Invalid template size: {len(template_bytes)} (expected 216)")
    
    block = TritiumBlock()
    offset = 0
    
    # Parse nVersion (4 bytes, little-endian)
    block.nVersion = struct.unpack('<I', template_bytes[offset:offset+4])[0]
    offset += 4
    
    # Parse hashPrevBlock (128 bytes)
    block.hashPrevBlock = template_bytes[offset:offset+128]
    offset += 128
    
    # Parse hashMerkleRoot (64 bytes)
    block.hashMerkleRoot = template_bytes[offset:offset+64]
    offset += 64
    
    # Parse nChannel (4 bytes, little-endian)
    block.nChannel = struct.unpack('<I', template_bytes[offset:offset+4])[0]
    offset += 4
    
    # Parse nHeight (4 bytes, little-endian)
    block.nHeight = struct.unpack('<I', template_bytes[offset:offset+4])[0]
    offset += 4
    
    # Parse nBits (4 bytes, little-endian)
    block.nBits = struct.unpack('<I', template_bytes[offset:offset+4])[0]
    offset += 4
    
    # Parse nNonce (8 bytes, little-endian)
    block.nNonce = struct.unpack('<Q', template_bytes[offset:offset+8])[0]
    
    return block

def serialize_block(block):
    """Serialize TritiumBlock to 216 bytes."""
    data = bytearray()
    
    # Serialize nVersion (little-endian)
    data.extend(struct.pack('<I', block.nVersion))
    
    # Serialize hashPrevBlock
    data.extend(block.hashPrevBlock)
    
    # Serialize hashMerkleRoot
    data.extend(block.hashMerkleRoot)
    
    # Serialize nChannel (little-endian)
    data.extend(struct.pack('<I', block.nChannel))
    
    # Serialize nHeight (little-endian)
    data.extend(struct.pack('<I', block.nHeight))
    
    # Serialize nBits (little-endian)
    data.extend(struct.pack('<I', block.nBits))
    
    # Serialize nNonce (little-endian, 8 bytes)
    data.extend(struct.pack('<Q', block.nNonce))
    
    return bytes(data)

# Example usage
def main():
    # Receive 224-byte packet from node
    packet = receive_packet()  # Implement your network receive
    
    # Skip 8-byte header, extract 216-byte template
    template = packet[8:224]
    
    # Parse template
    block = parse_template(template)
    
    print(f"Template received:")
    print(f"  Version: {block.nVersion}")
    print(f"  Channel: {block.nChannel} ({'Prime' if block.nChannel == 1 else 'Hash'})")
    print(f"  Height: {block.nHeight}")
    print(f"  Difficulty: 0x{block.nBits:08X}")
    print(f"  Nonce: {block.nNonce}")
    
    # Mine: iterate nonce
    target = compute_target(block.nBits)
    block.nNonce = 0
    
    while True:
        block_hash = compute_hash(block)
        if block_hash < target:
            print(f"Solution found! Nonce: {block.nNonce}")
            break
        block.nNonce += 1
    
    # Serialize and submit
    solution = serialize_block(block)
    submit_block(solution)
```

### Rust Reference Implementation

```rust
use std::convert::TryInto;

#[repr(C)]
pub struct TritiumBlock {
    pub n_version: u32,
    pub hash_prev_block: [u8; 128],  // 1024-bit
    pub hash_merkle_root: [u8; 64],  // 512-bit
    pub n_channel: u32,
    pub n_height: u32,
    pub n_bits: u32,
    pub n_nonce: u64,
}

impl TritiumBlock {
    /// Parse 216-byte template into TritiumBlock
    pub fn from_bytes(template: &[u8]) -> Result<Self, &'static str> {
        if template.len() != 216 {
            return Err("Invalid template size (expected 216 bytes)");
        }
        
        let mut offset = 0;
        
        // Parse nVersion (4 bytes, little-endian)
        let n_version = u32::from_le_bytes(template[offset..offset+4].try_into().unwrap());
        offset += 4;
        
        // Parse hashPrevBlock (128 bytes)
        let hash_prev_block: [u8; 128] = template[offset..offset+128].try_into().unwrap();
        offset += 128;
        
        // Parse hashMerkleRoot (64 bytes)
        let hash_merkle_root: [u8; 64] = template[offset..offset+64].try_into().unwrap();
        offset += 64;
        
        // Parse nChannel (4 bytes, little-endian)
        let n_channel = u32::from_le_bytes(template[offset..offset+4].try_into().unwrap());
        offset += 4;
        
        // Parse nHeight (4 bytes, little-endian)
        let n_height = u32::from_le_bytes(template[offset..offset+4].try_into().unwrap());
        offset += 4;
        
        // Parse nBits (4 bytes, little-endian)
        let n_bits = u32::from_le_bytes(template[offset..offset+4].try_into().unwrap());
        offset += 4;
        
        // Parse nNonce (8 bytes, little-endian)
        let n_nonce = u64::from_le_bytes(template[offset..offset+8].try_into().unwrap());
        
        Ok(TritiumBlock {
            n_version,
            hash_prev_block,
            hash_merkle_root,
            n_channel,
            n_height,
            n_bits,
            n_nonce,
        })
    }
    
    /// Serialize TritiumBlock to 216 bytes
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut data = Vec::with_capacity(216);
        
        // Serialize nVersion (little-endian)
        data.extend_from_slice(&self.n_version.to_le_bytes());
        
        // Serialize hashPrevBlock
        data.extend_from_slice(&self.hash_prev_block);
        
        // Serialize hashMerkleRoot
        data.extend_from_slice(&self.hash_merkle_root);
        
        // Serialize nChannel (little-endian)
        data.extend_from_slice(&self.n_channel.to_le_bytes());
        
        // Serialize nHeight (little-endian)
        data.extend_from_slice(&self.n_height.to_le_bytes());
        
        // Serialize nBits (little-endian)
        data.extend_from_slice(&self.n_bits.to_le_bytes());
        
        // Serialize nNonce (little-endian, 8 bytes)
        data.extend_from_slice(&self.n_nonce.to_le_bytes());
        
        data
    }
}
```

## Validation Rules

### Template Freshness

**Height Check:**
```
Current blockchain height: 6541702
Template height:           6541702

Status: FRESH ✓

Template height:           6541701

Status: STALE ✗ (reject, request new template)
```

**Staleness Detection:**
- Compare template.nHeight with current blockchain height
- If template.nHeight < current_height: STALE
- Stale submissions will be rejected with OLD_ROUND error

### Metadata Validation

**Channel Validation:**
```
Valid channels: 1 (Prime), 2 (Hash)
Invalid: 0 (Stake - not for stateless mining)

nChannel = 1 → VALID ✓ (Prime)
nChannel = 2 → VALID ✓ (Hash)
nChannel = 0 → INVALID ✗ (Stake not supported)
nChannel = 3 → INVALID ✗ (Unknown channel)
```

**Version Check:**
```
Expected: nVersion = 8 (Tritium)

nVersion = 8 → VALID ✓
nVersion != 8 → INVALID ✗ (Legacy or future version)
```

### Block Structure Validation

**Size Check:**
```
Template size must be exactly 216 bytes

Size = 216 → VALID ✓
Size ≠ 216 → INVALID ✗ (Corrupted or wrong format)
```

**Field Bounds:**
```
nVersion:   Must be 8
nChannel:   Must be 1 or 2
nHeight:    > 0
nBits:      > 0 (difficulty must be set)
nNonce:     0 to 2^64-1 (any value valid)
```

## Difficulty Target Calculation

### Compact Format (nBits)

The `nBits` field uses compact encoding:

```
nBits format: 0xNNTTTTTT

Where:
  NN      = Exponent (1 byte)
  TTTTTT  = Mantissa (3 bytes)

Target = Mantissa × 2^(8 × (Exponent - 3))
```

### Example: nBits = 0x1D00FFFF

```
Exponent: 0x1D = 29
Mantissa: 0x00FFFF = 65535

Target = 65535 × 2^(8 × (29 - 3))
       = 65535 × 2^208
       = 0x00FFFF000000000000000000000000000000000000000000000000

Valid hash must be: hash(block) < 0x00FFFF0000...
```

### C++ Target Expansion

```cpp
uint1024_t ExpandCompact(uint32_t nBits)
{
    uint32_t nSize = nBits >> 24;
    uint32_t nWord = nBits & 0x007FFFFF;
    
    uint1024_t result;
    if (nSize <= 3) {
        result = nWord >> (8 * (3 - nSize));
    } else {
        result = nWord;
        result <<= (8 * (nSize - 3));
    }
    
    return result;
}
```

## Mining Process Flow

### Template to Solution

```
1. RECEIVE TEMPLATE (224 bytes)
   ├─ Skip 8-byte header
   └─ Extract 216-byte template

2. PARSE TEMPLATE
   ├─ Deserialize TritiumBlock
   ├─ Extract nBits → Calculate target
   └─ Initialize nNonce = 0

3. PROOF-OF-WORK LOOP
   ├─ Set block.nNonce = current_nonce
   ├─ Serialize block (216 bytes)
   ├─ Hash block:
   │  ├─ Prime channel: Fermat prime test
   │  └─ Hash channel: SK1024 hash
   ├─ Compare hash < target
   ├─ If valid: GOTO 4
   └─ Else: nNonce++, GOTO 3

4. SUBMIT SOLUTION
   ├─ Serialize block with winning nNonce
   ├─ Create SUBMIT_BLOCK packet:
   │  ├─ Header: 0x0001 (SUBMIT_BLOCK)
   │  ├─ Length: 216 + signature_size
   │  └─ Data: block (216 bytes) + signature
   └─ Send to node

5. RECEIVE RESULT
   ├─ BLOCK_ACCEPTED (0x00C8): Success! ✓
   └─ BLOCK_REJECTED (0x00C9): Try again ✗
```

## Wire Format Examples

### Example 1: Prime Mining Template

```
Complete packet (224 bytes):

[MessagePacket Header - 8 bytes]
00 00                     # MESSAGE: BLOCK_DATA (0x0000)
00 00                     # FLAGS: 0x0000
D8 00 00 00               # LENGTH: 216 (0x000000D8)

[TritiumBlock - 216 bytes]
08 00 00 00               # nVersion: 8
[128 bytes]               # hashPrevBlock: 0x...
[64 bytes]                # hashMerkleRoot: 0x...
01 00 00 00               # nChannel: 1 (Prime)
86 D1 9E 00               # nHeight: 6541702 (0x009ED186)
FF FF 00 1D               # nBits: 0x1D00FFFF
00 00 00 00 00 00 00 00   # nNonce: 0 (miner starts here)
```

### Example 2: Hash Mining Template

```
Complete packet (224 bytes):

[MessagePacket Header - 8 bytes]
00 00                     # MESSAGE: BLOCK_DATA (0x0000)
00 00                     # FLAGS: 0x0000
D8 00 00 00               # LENGTH: 216

[TritiumBlock - 216 bytes]
08 00 00 00               # nVersion: 8
[128 bytes]               # hashPrevBlock: 0x...
[64 bytes]                # hashMerkleRoot: 0x...
02 00 00 00               # nChannel: 2 (Hash)
87 D1 9E 00               # nHeight: 6541703
FC E6 22 04               # nBits: 0x0422E6FC
00 00 00 00 00 00 00 00   # nNonce: 0
```

## Common Pitfalls

### Endianness Errors

**WRONG (Big-Endian):**
```cpp
uint32_t nHeight = (data[200] << 24) | (data[201] << 16) | 
                   (data[202] << 8) | data[203];
```

**CORRECT (Little-Endian):**
```cpp
uint32_t nHeight = data[200] | (data[201] << 8) | 
                   (data[202] << 16) | (data[203] << 24);
```

### Nonce Field Size

**WRONG (32-bit nonce):**
```cpp
uint32_t nNonce = data[208] | (data[209] << 8) | 
                  (data[210] << 16) | (data[211] << 24);
// Missing 4 bytes! nNonce is 64-bit (8 bytes)
```

**CORRECT (64-bit nonce):**
```cpp
uint64_t nNonce = (uint64_t)data[208] | ((uint64_t)data[209] << 8) |
                  ((uint64_t)data[210] << 16) | ((uint64_t)data[211] << 24) |
                  ((uint64_t)data[212] << 32) | ((uint64_t)data[213] << 40) |
                  ((uint64_t)data[214] << 48) | ((uint64_t)data[215] << 56);
```

### Offset Errors

**WRONG (Incorrect offsets):**
```
nChannel at offset 192  ✗ (should be 196)
nHeight at offset 196   ✗ (should be 200)
nBits at offset 200     ✗ (should be 204)
nNonce at offset 204    ✗ (should be 208)
```

**CORRECT (Per specification):**
```
nChannel at offset 196  ✓
nHeight at offset 200   ✓
nBits at offset 204     ✓
nNonce at offset 208    ✓
```

## Performance Optimization

### Zero-Copy Parsing

Instead of deserializing entire struct, access fields directly:

```cpp
// Fast (portable): Read nHeight directly with proper endianness handling
const uint8_t* data = template.data();
uint32_t nHeight = data[200] | (data[201] << 8) | 
                   (data[202] << 16) | (data[203] << 24);
// Works on all architectures (portable)

// Alternative (non-portable): Direct cast on little-endian systems only
// WARNING: Only works on little-endian architectures (x86, ARM)
// Will produce incorrect results on big-endian systems (MIPS, PowerPC, etc.)
const uint8_t* data = template.data();
uint32_t nHeight = *reinterpret_cast<const uint32_t*>(data + 200);
// ⚠️ NOT RECOMMENDED - Use bitwise approach above for portable code

// Slow: Full deserialization for one field
TritiumBlock block = ParseTemplate(template);
uint32_t nHeight = block.nHeight;
```

**Speedup:** Bitwise field access is 10-50× faster than full deserialization

### Nonce Iteration

```cpp
// Fast (portable): Modify nonce in-place with proper endianness
uint8_t* pNonce = template.data() + 208;
uint64_t nonce = 0;

while (true) {
    // Write nonce (little-endian)
    pNonce[0] = (nonce >> 0) & 0xFF;
    pNonce[1] = (nonce >> 8) & 0xFF;
    pNonce[2] = (nonce >> 16) & 0xFF;
    pNonce[3] = (nonce >> 24) & 0xFF;
    pNonce[4] = (nonce >> 32) & 0xFF;
    pNonce[5] = (nonce >> 40) & 0xFF;
    pNonce[6] = (nonce >> 48) & 0xFF;
    pNonce[7] = (nonce >> 56) & 0xFF;
    
    if (hash(template) < target) break;
    nonce++;
}

// Alternative (non-portable): Direct cast on little-endian systems
// ⚠️ Only for x86/ARM little-endian architectures
uint64_t* pNonce = reinterpret_cast<uint64_t*>(template.data() + 208);
while (true) {
    if (hash(template) < target) break;
    (*pNonce)++;  // In-place increment
}

// Slow: Deserialize/serialize each iteration (DON'T DO THIS)
while (true) {
    TritiumBlock block = ParseTemplate(template);
    if (hash(block) < target) break;
    block.nNonce++;
    template = SerializeBlock(block);
}
```

**Speedup:** In-place nonce modification is 100× faster than serialization overhead

**Portability Note:** The bitwise approach works on all architectures (big-endian, little-endian, strict alignment). The `reinterpret_cast` approach only works reliably on little-endian systems with relaxed alignment requirements (x86, ARM).

## Troubleshooting

### Template Size Mismatch

```
Received: 220 bytes
Expected: 224 bytes (8 header + 216 template)

Possible causes:
1. Reading packet header incorrectly
2. Legacy block format (220 bytes for old blocks)
3. Network corruption

Solution: Verify MessagePacket LENGTH field = 216
```

### Invalid Channel

```
Template shows nChannel = 0

Error: Stake channel (0) not valid for stateless mining

Solution: 
1. Check node configuration
2. Ensure SET_CHANNEL(1 or 2) was sent before MINER_READY
3. Verify channel filtering on node side
```

### Nonce Overflow

```
Nonce reached 2^64-1, no solution found

Possible causes:
1. Difficulty too high (extremely rare)
2. Template became stale during mining
3. Hash algorithm mismatch (Prime vs Hash)

Solution:
1. Wait for NEW_BLOCK push notification
2. Receive fresh template
3. Reset nonce = 0 and continue
```

## References

### Documentation
- [STATELESS_MINING_PROTOCOL.md](STATELESS_MINING_PROTOCOL.md) - Complete protocol
- [MIGRATION_GET_ROUND_TO_GETBLOCK.md](MIGRATION_GET_ROUND_TO_GETBLOCK.md) - Migration guide

### Source Code
- `src/TAO/Ledger/types/block.h` - TritiumBlock definition
- `src/TAO/Ledger/include/difficulty.h` - nBits encoding/decoding
- `src/LLP/stateless_miner_connection.cpp` - Template serialization (line 589)

## Changelog

### Version 1.0 (2026-01-12)
- Initial specification
- 216-byte TritiumBlock format
- C++, Python, Rust parsing examples
- Validation rules
- Performance optimization tips

---

**Document Version:** 1.0  
**Last Updated:** 2026-01-12  
**Maintainer:** Nexus Development Team
