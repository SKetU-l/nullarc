// Offscreen Metal smoke test. No display/WindowServer changes.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <stdint.h>

int main(void) {
    @autoreleasepool {
        NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
        for (id<MTLDevice> candidate in devices)
            printf("Metal device: %s\n", candidate.name.UTF8String);
        if (devices.count != 1) {
            fprintf(stderr, "Expected exactly one EliteBook Metal device; refusing an ambiguous test\n");
            return 1;
        }
        id<MTLDevice> device = devices[0];
        NSError *error = nil;
        NSString *source = @"#include <metal_stdlib>\nusing namespace metal;\n"
            "vertex float4 vs(uint i [[vertex_id]]) {\n"
            " float2 p[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};\n"
            " return float4(p[i],0,1); }\n"
            "fragment float4 fs() { return float4(1,0,0,1); }\n";
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library) {
            fprintf(stderr, "Shader compile failed: %s\n", error.description.UTF8String);
            return 1;
        }
        MTLRenderPipelineDescriptor *pipelineDesc = [MTLRenderPipelineDescriptor new];
        pipelineDesc.vertexFunction = [library newFunctionWithName:@"vs"];
        pipelineDesc.fragmentFunction = [library newFunctionWithName:@"fs"];
        pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
        MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
            width:64 height:64 mipmapped:NO];
        desc.storageMode = MTLStorageModeManaged;
        desc.usage = MTLTextureUsageRenderTarget;
        id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        if (!pipeline || !texture || !queue || !commands) {
            fprintf(stderr, "Metal allocation/pipeline failed: %s\n", error.description.UTF8String);
            return 1;
        }
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 1, 1);
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> render = [commands renderCommandEncoderWithDescriptor:pass];
        if (!render) { fprintf(stderr, "Render encoder failed\n"); return 1; }
        [render setRenderPipelineState:pipeline];
        [render drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [render endEncoding];
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        if (!blit) { fprintf(stderr, "Blit encoder failed\n"); return 1; }
        [blit synchronizeResource:texture];
        [blit endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted) {
            fprintf(stderr, "GPU command failed: %s\n", commands.error.description.UTF8String);
            return 1;
        }
        uint8_t pixels[64 * 64 * 4];
        [texture getBytes:pixels bytesPerRow:64 * 4 fromRegion:MTLRegionMake2D(0, 0, 64, 64) mipmapLevel:0];
        for (unsigned i = 0; i < 64 * 64; ++i) {
            if (pixels[4*i] != 255 || pixels[4*i+1] || pixels[4*i+2] || pixels[4*i+3] != 255) {
                fprintf(stderr, "FAIL pixel %u: %u,%u,%u,%u (expected 255,0,0,255)\n", i,
                        pixels[4*i], pixels[4*i+1], pixels[4*i+2], pixels[4*i+3]);
                return 1;
            }
        }
        puts("PASS: shader compilation, offscreen triangle, GPU completion and pixel readback");
        puts("This is a smoke test, not authorization to enable WindowServer acceleration.");
    }
    return 0;
}
