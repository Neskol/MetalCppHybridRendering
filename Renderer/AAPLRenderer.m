/*
Apple provides this ray tracing sample code.
This code is modified to work with Mtl_Engine, it does not participate in rendering.
Original code can be found at https://developer.apple.com/documentation/metal/rendering-reflections-in-real-time-using-ray-tracing

See the LICENSE.txt file for this sample’s licensing information.

Abstract:
Renderer adapter that bridges existing UI controls to the Metal C++ engine.
*/

#import "AAPLRenderer.h"
#import "Mtl_Engine.h"

@implementation AAPLRenderer
{
    AAPLNativeEngine *_engine;
}

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)view size:(CGSize)size
{
    self = [super init];
    if (self)
    {
        // Keep MTKView depth attachment consistent with the pipeline state's depth format.
        view.depthStencilPixelFormat = MTLPixelFormatDepth32Float;

        _engine = CreateEngine((__bridge void *)view);
        NSAssert(_engine, @"Failed to initialize native engine");

        EngineResize(_engine, (float)size.width, (float)size.height);

        [self setRenderMode:RMMetalRaytracing];
        [self setCameraPanSpeedFactor:0.0f];
        [self setMetallicBias:0.0f];
        [self setRoughnessBias:0.0f];
        [self setExposure:1.0f];
        [self setDenoisingEnabled:YES];
    }

    return self;
}

- (void)dealloc
{
    if (_engine)
    {
        DestroyEngine(_engine);
        _engine = NULL;
    }

#if !__has_feature(objc_arc)
    [super dealloc];
#endif
}

- (void)setRenderMode:(RenderMode)renderMode
{
    EngineSetRenderMode(_engine, (uint32_t)renderMode);
}

- (void)setCameraPanSpeedFactor:(float)speedFactor
{
    EngineSetCameraPanSpeedFactor(_engine, speedFactor);
}

- (void)setMetallicBias:(float)metallicBias
{
    EngineSetMetallicBias(_engine, metallicBias);
}

- (void)setRoughnessBias:(float)roughnessBias
{
    EngineSetRoughnessBias(_engine, roughnessBias);
}

- (void)setExposure:(float)exposure
{
    EngineSetExposure(_engine, exposure);
}

- (void)setDenoisingEnabled:(BOOL)enabled
{
    EngineSetDenoisingEnabled(_engine, enabled ? 1U : 0U);
}

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size
{
    (void)view;
    EngineResize(_engine, (float)size.width, (float)size.height);
}

- (void)drawInMTKView:(nonnull MTKView *)view
{
    id<CAMetalDrawable> drawable = view.currentDrawable;
    MTLRenderPassDescriptor *passDescriptor = view.currentRenderPassDescriptor;

    if (!drawable || !passDescriptor)
    {
        return;
    }

    EngineDraw(_engine, (__bridge void *)passDescriptor, (__bridge void *)drawable);
}

@end
