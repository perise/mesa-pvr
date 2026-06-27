# Mesa pvr K3 Optimization Patches

Optimization patches for the Mesa pvr Vulkan driver targeting the
SpacemiT K3 SoC (PowerVR BXM-4-64, Rogue architecture).

Apply with:
```
git am 0001-*.patch 0002-*.patch 0003-*.patch 0004-*.patch 0005-*.patch 0006-*.patch
```

## Patches

### 0001 — PCO compiler optimizations
FMAD LP=true (half-precision multiply-accumulate), SAT folding, zero_mul
algebraic, modifier propagation (FNEG/FABS/FFLR → source modifiers).

### 0002 — GPU object caches and BO pools
rt_dataset LRU cache (16-slot), CSB BO pool (64-slot 32KB), vk_sync pool
(32-slot), suballocator BO pool[4]. Eliminates per-frame GEM alloc/free.

### 0003 — Vulkan extension support
VK_EXT_load_store_op_none (TBDR bandwidth), VK_KHR_shader_float16_int8
(native F16 ALU), VK_KHR_push_descriptor (per-slot BO cache).

### 0004 — Pipeline optimizations
robustBufferAccess=false (removes VS bounds-check instructions), Z-only FS
passthrough (skips USC on depth-only passes), maxImageDimension2D=2048 (caps
1080p shadow map to 2048×1152, 3.5× fewer depth pixels).

### 0005 — Zink push_descriptor + synchronization2
Enable VK_KHR_push_descriptor for dynamic UBOs in Zink; synchronization2
codepath for more precise pipeline barrier semantics.

### 0006 — tc buffer_subdata ISP error fix
Skip next_buf_list busy check on partial updates. Fixes 22ms/frame ISP
firmware error on pvr that reduced buffer scene from 186→0.5 FPS at 1080p.

## Benchmark results (glmark2-es2-wayland, SpacemiT BPI-F3)

| Resolution | Baseline | Optimized | Delta |
|-----------|----------|-----------|-------|
| 800×600   | 201      | 275       | +37%  |
| 1080p     | 126      | ~157+     | +25%  |

GPU clock: 1228 MHz, CPU: 2400 MHz + boost, SCHED_FIFO prio 20.
