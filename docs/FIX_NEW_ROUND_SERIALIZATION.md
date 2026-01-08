# Fix for NEW_ROUND/OLD_ROUND Packet Serialization

## Problem Statement

The GET_ROUND handler in `src/LLP/stateless_miner_connection.cpp` was correctly building 12-byte responses containing:
- Unified height (4 bytes)
- Channel height (4 bytes)
- Difficulty (4 bytes)

However, these packets were being sent with `LENGTH=0` on the wire, causing protocol errors on the miner side:

```
[LLP RECV] header=204 (0xcc) NEW_ROUND length=0
[Solo GET_ROUND] ❌ PROTOCOL ERROR: Invalid packet length
[Solo GET_ROUND]   Expected:  12 bytes (unified + channel + difficulty)
[Solo GET_ROUND]   Received:  0 bytes
```

## Root Cause

The `HasDataPayload()` function in `src/LLP/packets/packet.h` was not recognizing opcodes 204-205 (NEW_ROUND and OLD_ROUND) as packets that carry data payloads. This caused the `GetBytes()` serialization method to only serialize the header byte, dropping the LENGTH and DATA fields entirely.

### Serialization Logic

The `GetBytes()` method in `src/LLP/packets/packet.h` (lines 294-311) only includes LENGTH and DATA fields when `HasDataPayload()` returns true. See the source file for the complete implementation.

## Solution

Updated the `HasDataPayload()` function to return `true` for opcodes 204-205:

```cpp
bool HasDataPayload() const
{
    /* Boundary constants for mining round response packets (PR #151/PR #153)
     * These packets carry height and difficulty data for stateless mining.
     * NEW_ROUND (204): 12 bytes - unified height (4) + channel height (4) + difficulty (4)
     * OLD_ROUND (205): variable - rejection reason or stale height info
     */
    static const uint8_t ROUND_RESPONSE_FIRST = 204;  // NEW_ROUND
    static const uint8_t ROUND_RESPONSE_LAST = 205;   // OLD_ROUND
    
    /* Traditional data packets */
    if(HEADER < 128)
        return true;
    
    /* Mining round response packets carry height data (PR #151/PR #153) */
    if(HEADER >= ROUND_RESPONSE_FIRST && HEADER <= ROUND_RESPONSE_LAST)
        return true;
    
    // ... other packet types ...
    
    return false;
}
```

## Additional Opcodes Fixed

The same fix was applied to other response opcodes that carry data:

- **CHANNEL_ACK (206)**: Carries 1-byte channel number
- **Falcon authentication packets (207-212)**:
  - MINER_AUTH_INIT (207): pubkey + miner_id
  - MINER_AUTH_CHALLENGE (208): random nonce
  - MINER_AUTH_RESPONSE (209): signature
  - MINER_AUTH_RESULT (210): success/failure code
  - SESSION_START (211): session parameters
  - SESSION_KEEPALIVE (212): keepalive data
- **Reward address binding packets (213-214)**:
  - MINER_SET_REWARD (213): encrypted reward address
  - MINER_REWARD_RESULT (214): encrypted validation result

## Wire Format

### Before Fix
```
Byte 0: 0xCC (204 - NEW_ROUND header)
Total: 1 byte
Problem: LENGTH and DATA fields missing
```

### After Fix
```
Byte 0:      0xCC (204 - NEW_ROUND header)
Bytes 1-4:   0x00 0x00 0x00 0x0C (LENGTH = 12 bytes, big-endian)
Bytes 5-8:   [4 bytes] Unified height (little-endian)
Bytes 9-12:  [4 bytes] Channel height (little-endian)
Bytes 13-16: [4 bytes] Difficulty (little-endian)
Total: 17 bytes
```

## Code Paths

The complete packet flow from creation to wire:

1. **GET_ROUND Handler** (`src/LLP/stateless_miner_connection.cpp:1862-1865`)
   ```cpp
   Packet response(NEW_ROUND);
   response.DATA = vData;  // 12 bytes
   response.LENGTH = static_cast<uint32_t>(vData.size());
   respond(response);
   ```

2. **respond()** (`src/LLP/stateless_miner_connection.cpp:1995-1999`)
   ```cpp
   void StatelessMinerConnection::respond(const Packet& packet)
   {
       WritePacket(packet);
   }
   ```

3. **WritePacket()** (`src/LLP/base_connection.cpp:231-268`)
   ```cpp
   void BaseConnection<PacketType>::WritePacket(const PacketType& PACKET)
   {
       const std::vector<uint8_t> vBytes = PACKET.GetBytes();
       Write(vBytes, vBytes.size());
   }
   ```

4. **GetBytes()** (`src/LLP/packets/packet.h:294-311`)
   - Now includes LENGTH and DATA because `HasDataPayload()` returns `true`

## Test Coverage

Unit tests in `tests/unit/LLP/packet_round_response.cpp` verify:

1. **HasDataPayload() returns true for NEW_ROUND (204) and OLD_ROUND (205)**
   ```cpp
   TEST_CASE("Packet::HasDataPayload() for mining round response packets")
   {
       SECTION("NEW_ROUND (204) requires data payload")
       {
           LLP::Packet packet;
           packet.HEADER = 204;
           packet.LENGTH = 12;
           REQUIRE(packet.HasDataPayload() == true);
       }
   }
   ```

2. **GetBytes() serializes to correct size**
   ```cpp
   TEST_CASE("Packet::GetBytes() serialization for NEW_ROUND")
   {
       SECTION("NEW_ROUND with 12-byte payload serializes to 17 bytes")
       {
           LLP::Packet packet;
           packet.HEADER = 204;
           packet.LENGTH = 12;
           packet.DATA = { /* 12 bytes */ };
           
           std::vector<uint8_t> bytes = packet.GetBytes();
           REQUIRE(bytes.size() == 17);  // 1 + 4 + 12
       }
   }
   ```

3. **Header validation**
4. **Packet completion checks**

## Expected Outcome

After this fix, miners receive complete NEW_ROUND packets (values from actual production logs):

```
[LLP RECV] header=204 (0xcc) NEW_ROUND length=12
[Solo GET_ROUND] NEW_ROUND response received
[Solo GET_ROUND]   Unified height:  6538431    (actual production value)
[Solo GET_ROUND]   Channel height:  2303058    (actual production value)
[Solo GET_ROUND]   Difficulty:      0x1D00FFFF (actual production value)
```

Note: Test files use different constants (e.g., 6537420, 2302664) as mock data.

## Impact

- ✅ Miners receive complete 12-byte NEW_ROUND responses
- ✅ Template staleness detection works correctly
- ✅ No more "PROTOCOL ERROR: Invalid packet length" errors
- ✅ Backward compatible with existing protocol
- ✅ All response packets now properly serialize with data

## References

- **PR #153**: Fixed similar issue for BLOCK_DATA opcode
- **PR #151**: Defined the 12-byte GET_ROUND response schema
- **PR #152**: Ensured rate-limited responses still send valid data
- **Problem Statement**: Evidence from node/miner logs showing LENGTH=0 issue

## Files Modified

- `src/LLP/packets/packet.h` - Updated `HasDataPayload()` function

## Files With Test Coverage

The following test files verify the fix (see `tests/unit/LLP/` directory):
- `packet_round_response.cpp` - NEW_ROUND/OLD_ROUND serialization tests
- `packet_data_payload.cpp` - General HasDataPayload() tests  
- `test_get_round_schema.cpp` - GET_ROUND protocol schema tests

Note: Test files may be updated or reorganized as the codebase evolves.
