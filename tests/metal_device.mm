// CI helper: absence of a GPU is explicitly skipped, never reported as a pixel
// pass.
#import <Metal/Metal.h>
#include <cstdio>
int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            std::puts("No Metal device available; GPU suites require a "
                      "physical/GPU-enabled runner");
            return 77;
        }
        std::printf("Metal device: %s\n", device.name.UTF8String);
        return 0;
    }
}
