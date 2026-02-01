# Camera Fusion Example

## Overview
This example demonstrates a realistic multi-camera fusion scenario using LightAP IPC zero-copy shared memory communication in **NORMAL mode**.

## Architecture
```
┌─────────────┐                     ┌────────────────┐
│ Camera 0    │───[1920×720×4]────▶ │                │
│ Publisher   │  @ 60FPS (16ms)     │                │
└─────────────┘                     │   Fusion       │
                                    │   Subscriber   │
┌─────────────┐                     │                │  ┌──────────────┐
│ Camera 1    │───[1920×720×4]────▶ │  ┌──────────┐  │  │  BMP Output  │
│ Publisher   │  @ 60FPS (16ms)     │  │Double    │  ├─▶│ fusion_*.bmp │
└─────────────┘                     │  │Buffer    │  │  │  (every 5s)  │
                                    │  │3840×1440 │  │  └──────────────┘
┌─────────────┐                     │  │×4×2      │  │
│ Camera 2    │───[1920×720×4]────▶ │  └──────────┘  │
│ Publisher   │  @ 60FPS (16ms)     │                │
└─────────────┘                     └────────────────┘
```

## Image Layout (3840×1440×4 bytes)
```
┌────────────┬────────────┐
│  Camera 0  │  Camera 1  │  ← Top row (720 pixels)
│ 1920×720×4 │ 1920×720×4 │
├────────────┼────────────┤
│   Camera 2 (centered)   │  ← Bottom row (720 pixels)
│      1920×720×4         │
│  (offset: 960, 720)     │
└─────────────────────────┘
```

## Features

### Zero-Copy Image Transfer
- Publishers use `Send(Fn&& write_fn)` to directly write image data to shared memory
- No intermediate buffers or memcpy operations
- Efficient for large payloads (1920×720×4 = 5.53 MB per frame)

### Double Buffering
- Subscriber maintains 2 large buffers (3840×1440×4 = 21.2 MB each)
- Back buffer: 3 worker threads write concurrently (each to a fixed region)
- Front buffer: File I/O thread saves to BMP (every 5 seconds)
- Atomic pointer swap ensures thread safety and zero contention

### Publisher Configuration
- **Payload Size**: 1920×720×4 = 5,529,600 bytes (5.53 MB)
- **Frame Rate**: 60 FPS (STMin = 16 ms)
- **Dispatch Policy**: Overwrite (no blocking on full queue)
- **Zero-Copy Send**: Uses lambda to directly write generated image
- **Simulated Content**: Colored gradient + checkerboard patterns

### Subscriber Configuration
- **STMin**: 16 ms (matches publisher rate, 60 FPS)
- **3 Worker Threads**: Each handles one camera stream
- **Concurrent Writes**: Each thread writes to its designated buffer region
- **File I/O Thread**: Saves front buffer to BMP every 5 seconds
- **Atomic Swap**: Front/back buffers swapped safely between worker and I/O threads

## Building
```bash
cd /workspace/LightAP/build
make camera_fusion_spmc_example
```

## Running
```bash
# Terminal 1: Start Camera 0
./modules/Core/camera_fusion_spmc_example --cam 0

# Terminal 2: Start Camera 1
./modules/Core/camera_fusion_spmc_example --cam 1

# Terminal 3: Start Camera 2
./modules/Core/camera_fusion_spmc_example --cam 2

# Terminal 4: Start Fusion Subscriber (wait for all cameras to start)
./modules/Core/camera_fusion_spmc_example --fusion
```

## Expected Output

### Publishers (Camera 0/1/2)
```
[Cam0 Publisher] Started publishing 1920x720x4 images @ 60FPS (STMin=16ms)
[Cam0] Sent frame 60 (zero-copy)
[Cam0] Sent frame 120 (zero-copy)
...
```

### Subscriber (Fusion)
```
[Fusion Subscriber] Listening to 3 camera streams
[Fusion] Worker-0: Received frame 0 from Cam0 (5529600 bytes)
[Fusion] Worker-1: Received frame 0 from Cam1 (5529600 bytes)
[Fusion] Worker-2: Received frame 0 from Cam2 (5529600 bytes)
[Fusion] Saved: fusion_spmc_00000.bmp (3840x1440)
[Fusion] Saved: fusion_spmc_00001.bmp (3840x1440)
...
```

## Output Files
- **fusion_spmc_00000.bmp** - First captured frame (at t=5s)
- **fusion_spmc_00001.bmp** - Second frame (at t=10s)
- **fusion_spmc_00002.bmp** - Third frame (at t=15s)
- ... (one BMP every 5 seconds)

Each BMP file is **21.2 MB** (3840×1440×4 bytes + 54-byte header)

## Verification
1. **Check frame rate**: Publishers should maintain ~60 FPS (16ms interval)
2. **Zero-copy confirmation**: No "send failed" messages (Overwrite policy)
3. **BMP files**: Images should show:
   - Top-left: Red gradient (Camera 0)
   - Top-right: Green gradient (Camera 1)
   - Bottom-center: Blue gradient (Camera 2)

## Performance Notes
- **Total throughput**: ~1 GB/s (180 FPS × 5.53 MB)
- **Double buffer overhead**: 42.4 MB (2 × 21.2 MB)
- **Zero-copy benefit**: No CPU-intensive memcpy, minimal cache pollution
- **STMin throttling**: Ensures publishers don't overwhelm subscribers

## IPC Mode Comparison
- **NORMAL mode** (used here): 32 subscribers, 256 queue capacity, ~100KB overhead
- **SHRINK mode**: 8 subscribers, 64 queue capacity, ~25KB overhead (too limited)
- **EXTEND mode**: 64 subscribers, 512 queue capacity, ~400KB overhead (overkill)

NORMAL mode is ideal for this 3-camera scenario (3 subscribers, moderate queue depth).

## Related Examples
- [ipc_chain_example.cpp](./ipc_chain_example.cpp) - STMin chain validation with 5-region forwarding
- [test_ipc_normal.cpp](./test_ipc_normal.cpp) - Basic NORMAL mode functional tests
