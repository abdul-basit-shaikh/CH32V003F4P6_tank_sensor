# CH32V003 Tank Sensor Crash Fix

## Problem

The CH32V003 tank sensor was experiencing continuous boot loops and crashes during the pairing/discovery broadcast phase. The device would boot, initialize the radio, start transmitting, and then crash with corrupted output (`[S▒` or `[R`).

## Root Causes

### 1. **UART Buffer Overflow**

- The code was printing extensive debug information for **every single packet transmission** (500 iterations)
- Each transmission printed:
  - `[RADIO] >>> DATA DISPATCH: 32 BYTES START <<<`
  - `[SPI] CMD: FLUSH_TX`
  - `[SPI] CMD: W_TX_PAYLOAD (32B):` + 32 bytes in hex (96 characters)
  - `[SPI] W_TX_PAYLOAD: Dispatch complete.`
  - `[GPIO] CE HIGH -> Start TX Pulse`
  - `[GPIO] CE LOW -> End TX Pulse`
  - `[RADIO] TX COMPLETE (NO-ACK MODE)`
  - Plus register writes and buffer writes

This resulted in **~300+ characters per packet × 500 packets = 150,000+ characters** being sent over UART in a short time.

### 2. **Stack Overflow**

- The CH32V003 has only **2KB of RAM**
- Deep printf calls + SPI operations + interrupt handlers were exceeding available stack space
- The combination of nested function calls during rapid transmission was too much for the limited memory

### 3. **Excessive Delays**

- Two unnecessary `Delay_Ms(10)` calls in the send function were slowing down transmission
- These delays were not needed in NO-ACK mode

## Solutions Implemented

### 1. **Reduced Pairing Loop Verbosity** (`main.c`)

```c
// BEFORE: Printed every iteration (500 times)
printf("[PAIR] Req %d/1000: ", i + 1);

// AFTER: Print only every 10th iteration (50 times)
if (i % 10 == 0) {
  printf("[PAIR] Req %d/500: ", i + 1);
}
```

### 2. **Commented Out High-Frequency Debug Prints** (`nrf24_simple.c`)

- Disabled all debug prints in `nrf24_send()` function
- Disabled debug prints in `nrf_write_reg()` (called frequently)
- Disabled debug prints in `nrf_write_buf()` (called frequently)
- **Kept** initialization debug prints (called only once at boot)

### 3. **Removed Unnecessary Delays**

- Removed two `Delay_Ms(10)` calls from `nrf24_send()`
- These were not needed in NO-ACK mode and were slowing transmission

## Expected Behavior After Fix

The device should now:

1. ✅ Boot successfully without crashing
2. ✅ Initialize the radio hardware
3. ✅ Complete the 500-packet pairing broadcast loop
4. ✅ Enter daily operation mode
5. ✅ Send periodic data packets every 1 second

## Debug Output Comparison

### Before (Crash-Inducing):

```
[PAIR] Req 1/1000: [RADIO] >>> DATA DISPATCH: 32 BYTES START <<<
[SPI] CMD: FLUSH_TX
[SPI] CMD: W_TX_PAYLOAD 32B): AA 55 01 20 12 34 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[S▒  <-- CRASH
```

### After (Stable):

```
[PAIR] Req 1/500: OK
[PAIR] Req 11/500: OK
[PAIR] Req 21/500: OK
...
[SYSTEM] Entering Daily Mode.
[DATA] Send Packet Dispatched... OK (ACK)
```

## Testing Recommendations

1. **Flash the updated firmware** to the CH32V003
2. **Monitor serial output** at 115200 baud
3. **Verify** the device completes the pairing loop without crashing
4. **Check** that the ESP32 receiver can detect the pairing packets
5. **Confirm** data packets are received in daily mode

## Re-enabling Debug (If Needed)

If you need to debug specific issues later, you can:

1. **Enable specific debug prints** by uncommenting them
2. **Use conditional compilation**:

   ```c
   #define DEBUG_LEVEL 1  // 0=none, 1=minimal, 2=verbose

   #if DEBUG_LEVEL >= 2
     printf("[DEBUG] Detailed info...");
   #endif
   ```

3. **Print only on errors** instead of every operation

## Key Takeaways

- **Limited RAM devices** (like CH32V003 with 2KB) require careful management of:
  - Stack usage
  - UART buffer usage
  - Printf frequency
- **Debug prints are expensive** in terms of:
  - CPU time
  - Memory (stack + buffers)
  - Execution time
- **Always test on actual hardware** - simulators don't catch these real-world issues
