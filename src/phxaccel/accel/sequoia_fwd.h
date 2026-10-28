/*
 * NullMoth-derived accelerator compatibility for macOS Sequoia 24H32, x86_64.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 *
 * 27 inherited methods exist only as local symbols in IOAcceleratorFamily2
 * 485.2. They cannot be imported, even though nm --defined-only finds them.
 * Reach them through Apple's exported base vtables instead. These 61 entries
 * were checked against the exact 24H32 binary, including the two ABI header
 * words in each byte offset. Naked tail calls preserve every argument and the
 * return value without relying on the reconstructed return types.
 *
 * Reuse the upstream NM_TAHOE_FWD declaration hooks, but select this table only
 * for NM_SEQUOIA. The seven named-enum methods are exported and need no shim.
 */
#pragma once

#define PHX_SEQ_RESOURCE(X, C, T) \
    X(C, T, void, pageon, (IOAccelEvent*, bool), , 448) \
    X(C, T, void, pageon, (IOAccelEvent*, bool, unsigned long long), , 456) \
    X(C, T, void, pageoff, (IOAccelEvent*, bool, bool*), , 464) \
    X(C, T, uint64_t, getMemoryAllocParameter, (), , 488)

#define PHX_SEQ_MEMORY(X, C, T) \
    X(C, T, uint64_t, getLength, (), const, 304) \
    X(C, T, uint64_t, setTag, (unsigned int), , 376) \
    X(C, T, uint64_t, getTag, (), , 384)

#define PHX_SEQ_SURFACE(X, C, T) \
    X(C, T, void, submitCopyForward, (IOAccelEvent*, unsigned int, IOAccelResource2*, IOAccelResource2*, IOAccelBounds const*, unsigned int), , 2656) \
    X(C, T, void, submitUpdate, (unsigned int, IOAccelBounds*, unsigned int), , 2664)

#define PHX_SEQ_CONTEXT(X, C, T) \
    X(C, T, void*, getOwningTask, (), const, 2416) \
    X(C, T, void*, getGPUTask, (), const, 2424) \
    X(C, T, void, getOwningTaskPid, (), const, 2432) \
    X(C, T, void, retireCommandBuffer, (IOAccelEventFence*), , 2448) \
    X(C, T, void, setSubmissionError, (unsigned int), , 2480) \
    X(C, T, bool, isOpportunisticWorkload, (), const, 2536) \
    X(C, T, void, postTokenSanityCheck, (IOAccelCommandStreamInfo&), , 2608) \
    X(C, T, void, getDataBufferPrivate, (IOAccelResource2*, IOAccelResourcePrivate*, unsigned long long), , 2696) \
    X(C, T, void, validateDataBuffer, (unsigned long long, IOBufferMemoryDescriptor*, IOAccelResource2*), , 2704) \
    X(C, T, void, invalidate, (), , 2744) \
    X(C, T, void, getFrameDelimiter, (), , 2752)

#define PHX_SEQ_EVENT(X, C, T) \
    X(C, T, void, collectAllLiveEventIfNeeded, (), , 576)
#define PHX_SEQ_MAP(X, C, T) \
    X(C, T, bool, matchOptionBits, (unsigned int), , 304) \
    X(C, T, unsigned long long, getLength, (), const, 376)
#define PHX_SEQ_ACCEL(X, C, T) \
    X(C, T, uint64_t, tmpTotalVRAM, (), , 2160) \
    X(C, T, uint64_t, totalTextureMemory, (), , 2168)

#define PHX_SEQ_DECL(C, T, R, N, A, Q, O) R N A Q override;
#define PHX_SEQ_DEF(C, T, R, N, A, Q, O) \
    __attribute__((naked)) R C::N A Q { \
        __asm__("movq __ZTV" #T "@GOTPCREL(%rip), %r11\n\tjmpq *" #O "(%r11)"); \
    }

#define NM_TAHOE_FWD_NVResource PHX_SEQ_RESOURCE(PHX_SEQ_DECL, NVResource, 16IOAccelResource2)
#define NM_TAHOE_FWD_NVSysMemory PHX_SEQ_MEMORY(PHX_SEQ_DECL, NVSysMemory, 16IOAccelSysMemory)
#define NM_TAHOE_FWD_NVVidMemory PHX_SEQ_MEMORY(PHX_SEQ_DECL, NVVidMemory, 16IOAccelVidMemory)
#define NM_TAHOE_FWD_NVSurface PHX_SEQ_SURFACE(PHX_SEQ_DECL, NVSurface, 20IOAccelLegacySurface)
#define NM_TAHOE_FWD_NVGLContext PHX_SEQ_CONTEXT(PHX_SEQ_DECL, NVGLContext, 17IOAccelGLContext2)
#define NM_TAHOE_FWD_NV2DContext PHX_SEQ_CONTEXT(PHX_SEQ_DECL, NV2DContext, 17IOAccel2DContext2)
#define NM_TAHOE_FWD_NVCLContext PHX_SEQ_CONTEXT(PHX_SEQ_DECL, NVCLContext, 17IOAccelCLContext2)
#define NM_TAHOE_FWD_NVVideoContext PHX_SEQ_CONTEXT(PHX_SEQ_DECL, NVVideoContext, 20IOAccelVideoContext2)
#define NM_TAHOE_FWD_NVEventMachine PHX_SEQ_EVENT(PHX_SEQ_DECL, NVEventMachine, 24IOAccelEventMachineFast2)
#define NM_TAHOE_FWD_NVMemoryMap PHX_SEQ_MAP(PHX_SEQ_DECL, NVMemoryMap, 16IOAccelMemoryMap)
#define NM_TAHOE_FWD_NVAccel PHX_SEQ_ACCEL(PHX_SEQ_DECL, NVAccel, 22IOGraphicsAccelerator2)
#define NM_TAHOE_FWD_NVDisplayPipe
#define NM_TAHOE_FWD_NVDisplayMachine
#define NM_TAHOE_FWD_NVTask
#define NM_TAHOE_FWD(cls) NM_TAHOE_FWD_##cls
#define NM_TAHOE_FWD_DEFS \
    PHX_SEQ_RESOURCE(PHX_SEQ_DEF, NVResource, 16IOAccelResource2) \
    PHX_SEQ_MEMORY(PHX_SEQ_DEF, NVSysMemory, 16IOAccelSysMemory) \
    PHX_SEQ_MEMORY(PHX_SEQ_DEF, NVVidMemory, 16IOAccelVidMemory) \
    PHX_SEQ_SURFACE(PHX_SEQ_DEF, NVSurface, 20IOAccelLegacySurface) \
    PHX_SEQ_CONTEXT(PHX_SEQ_DEF, NVGLContext, 17IOAccelGLContext2) \
    PHX_SEQ_CONTEXT(PHX_SEQ_DEF, NV2DContext, 17IOAccel2DContext2) \
    PHX_SEQ_CONTEXT(PHX_SEQ_DEF, NVCLContext, 17IOAccelCLContext2) \
    PHX_SEQ_CONTEXT(PHX_SEQ_DEF, NVVideoContext, 20IOAccelVideoContext2) \
    PHX_SEQ_EVENT(PHX_SEQ_DEF, NVEventMachine, 24IOAccelEventMachineFast2) \
    PHX_SEQ_MAP(PHX_SEQ_DEF, NVMemoryMap, 16IOAccelMemoryMap) \
    PHX_SEQ_ACCEL(PHX_SEQ_DEF, NVAccel, 22IOGraphicsAccelerator2)
