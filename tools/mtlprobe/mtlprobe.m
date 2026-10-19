// mtlprobe: lists the Metal devices and runs one compute kernel on the 780M (PhxAccel + NVMTLDriver + RADV).
// The name is on NVMTLDriver's built-in allow list. Build: clang -fobjc-arc -framework Foundation -framework Metal
//   -o mtlprobe mtlprobe.m
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

static NSString *const kSrc =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "kernel void k(device uint *out [[buffer(0)]], uint i [[thread_position_in_grid]]) {\n"
     "    out[i] = i * 0x9E3779B1u + 7u;\n"
     "}\n";

int main(int argc, char **argv) {
    @autoreleasepool {
        NSArray<id<MTLDevice>> *all = MTLCopyAllDevices();
        printf("devices: %lu\n", (unsigned long)all.count);
        id<MTLDevice> dev = nil;
        for (id<MTLDevice> d in all) {
            printf("  %s  class=%s registryID=0x%llx lowPower=%d headless=%d\n", d.name.UTF8String,
                   object_getClassName(d), d.registryID, d.lowPower, d.headless);
            if ([NSStringFromClass([d class]) hasPrefix:@"NVMTL"]) dev = d;
        }
        if (!dev) dev = MTLCreateSystemDefaultDevice();
        if (!dev) { printf("no Metal device\n"); return 1; }
        printf("using %s\n", dev.name.UTF8String);
        if (argc > 1 && !strcmp(argv[1], "list")) return 0;

        NSError *err = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:kSrc options:nil error:&err];
        if (!lib) { printf("library: %s\n", err.localizedDescription.UTF8String); return 2; }
        id<MTLFunction> fn = [lib newFunctionWithName:@"k"];
        id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:fn error:&err];
        if (!ps) { printf("pipeline: %s\n", err.localizedDescription.UTF8String); return 3; }
        const NSUInteger n = 4096;
        id<MTLBuffer> buf = [dev newBufferWithLength:n * 4 options:MTLResourceStorageModeShared];
        memset(buf.contents, 0, n * 4);
        id<MTLCommandQueue> q = [dev newCommandQueue];
        id<MTLCommandBuffer> cb = [q commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
        [enc setComputePipelineState:ps];
        [enc setBuffer:buf offset:0 atIndex:0];
        [enc dispatchThreads:MTLSizeMake(n, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [enc endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        printf("command buffer status %ld error %s\n", (long)cb.status, cb.error ? cb.error.localizedDescription.UTF8String : "none");
        const uint32_t *o = buf.contents;
        NSUInteger ok = 0;
        for (NSUInteger i = 0; i < n; i++) ok += o[i] == (uint32_t)(i * 0x9E3779B1u + 7u);
        printf("results: %lu/%lu correct (out[1]=0x%08x, expected 0x%08x)\n", (unsigned long)ok, (unsigned long)n, o[1],
               (uint32_t)(0x9E3779B1u + 7u));
        return ok == n ? 0 : 4;
    }
}
