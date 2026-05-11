//
//  Mtl_Engine.h
//  HybridRenderingPlayground
//
//  Created by Neskol on 2026.04.07.
//

#pragma once

#include <simd/simd.h>
#include <stdint.h>

#ifdef __cplusplus
#include <map>
#include <string>
#include <vector>

namespace MTL {
    class Device;
    class Library;
    class RenderPassDescriptor;
    class RenderPipelineState;
    class Buffer;
    class CommandQueue;
    class DepthStencilState;
    class AccelerationStructure;
    class ComputePipelineState;
    class Texture;
}

namespace MTLFX {
    class TemporalDenoisedScaler;
}

namespace CA {
    class Drawable;
}

namespace MTK {
    class View;
}

struct Material {
    simd_float4 diffuse;
    simd_float4 emissive;
    simd_float4 specular;
    float shininess;
    float opacity;
    float ior;
    float reflection;
};

struct Uniforms {
    simd_float4x4 mvp;
    simd_float4 lightPos;
    simd_float4 lightU;
    simd_float4 lightV;
    simd_float4 lightNormal;
    simd_float4 cameraPos;
    simd_float4 tuning; // x: metallicBias, y: roughnessBias, z: exposure, w: renderMode
    simd_float4 sceneMin;
    simd_float4 sceneMax;
};

struct MeshGroup {
    MTL::Buffer *indexBuffer;
    uint32_t indexCount;
    Material material;
};

class Mtl_Engine {
public:
    explicit Mtl_Engine(MTK::View *view);
    ~Mtl_Engine();

    void resize(float width, float height);
    void draw(MTL::RenderPassDescriptor *rpd, CA::Drawable *drawable);

    void setRenderMode(uint32_t renderMode);
    void setCameraPanSpeedFactor(float speedFactor);
    void setMetallicBias(float metallicBias);
    void setRoughnessBias(float roughnessBias);
    void setExposure(float exposure);
    void setDenoisingEnabled(bool enabled);

    MTL::Device *getDevice() const { return device_; }
    static int getTargetFPS();

private:
    MTL::Library *buildLibrary();
    void buildPipeline();
    void buildFallbackTriangle();
    void buildAccelerationStructure();
    void buildComputePipeline();
    void createGBuffer(float width, float height);

    bool loadOBJ(const std::string &path);
    bool loadMTL(const std::string &path, std::map<std::string, Material> &materials);
    void buildAxes();

    MTK::View *view_;
    MTL::Device *device_;
    MTL::CommandQueue *commandQueue_;
    MTL::RenderPipelineState *pipelineState_;
    MTL::DepthStencilState *depthStencilState_;

    MTL::AccelerationStructure *blas_;
    MTL::AccelerationStructure *tlas_;
    MTL::ComputePipelineState *rtComputePipelineState_;

    MTL::Texture *gWorldPos_;
    MTL::Texture *gNormal_;
    MTL::Texture *depthTexture_;
    MTL::Texture *rtOutput_;
    MTL::Texture *denoiseOutput_;
    MTL::Texture *motionVectors_;
    MTLFX::TemporalDenoisedScaler *temporalDenoiser_;
    bool resetDenoiserHistory_;

    bool useRayTracing_;
    bool rayTracingSupported_;
    bool rayTracingSwitch_;
    float aspect_;

    MTL::Buffer *fallbackVB_;
    MTL::Buffer *fallbackIB_;

    MTL::Buffer *vertexBuffer_;
    MTL::Buffer *materialDiffuseBuffer_;
    MTL::Buffer *rtTriangleIndexBuffer_;
    MTL::Buffer *rtGeometryTriangleBaseBuffer_;
    std::vector<MeshGroup> meshGroups_;

    MTL::Buffer *axesVB_;
    uint32_t axesCount_;
    MTL::RenderPipelineState *axesPipelineState_;

    vector_float3 minBounds_;
    vector_float3 maxBounds_;
    vector_float3 lightCenter_;
    vector_float3 lightU_;
    vector_float3 lightV_;
    vector_float3 lightNormal_;

    uint32_t renderMode_;
    bool denoisingEnabled_;
    float cameraPanSpeedFactor_;
    float metallicBias_;
    float roughnessBias_;
    float exposure_;
    float cameraOrbitAngle_;
};
#endif


//// AI-assisted code ////
// Used Codex on GPT 5.4 to implement a bridge to Apple's RT sample //
// Apple's RT sample is used as a View frame, not to do with rendering process //
//// START ////

typedef struct AAPLNativeEngine AAPLNativeEngine;

#ifdef __cplusplus
extern "C" {
#endif

AAPLNativeEngine *CreateEngine(void *view);
void DestroyEngine(AAPLNativeEngine *engine);
void EngineResize(AAPLNativeEngine *engine, float width, float height);
void EngineDraw(AAPLNativeEngine *engine, void *renderPassDescriptor, void *drawable);
void EngineSetRenderMode(AAPLNativeEngine *engine, uint32_t renderMode);
void EngineSetCameraPanSpeedFactor(AAPLNativeEngine *engine, float speedFactor);
void EngineSetMetallicBias(AAPLNativeEngine *engine, float metallicBias);
void EngineSetRoughnessBias(AAPLNativeEngine *engine, float roughnessBias);
void EngineSetExposure(AAPLNativeEngine *engine, float exposure);
void EngineSetDenoisingEnabled(AAPLNativeEngine *engine, uint32_t enabled);

#ifdef __cplusplus
}
#endif

//// ENDS ////
//// AI-assisted code ////
