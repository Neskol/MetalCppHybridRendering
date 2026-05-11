//
//  Mtl_Engine.cpp
//  HybridRenderingPlayground
//
//  Created by Neskol on 2026.04.07.
//

#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTLFX_PRIVATE_IMPLEMENTATION
#define TARGET_FPS 120
#include <type_traits>
#if __cplusplus < 201703L
namespace std {
template <class From, class To>
constexpr bool is_convertible_v = is_convertible<From, To>::value;
}
#endif
#include "../Metal.hpp"
#include "../MetalFX/MetalFX.hpp"
#include "Mtl_Engine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <map>
#include <tuple>

using namespace simd;

struct Vertex {
    vector_float3 position;
    vector_float3 normal;
    vector_float2 uv;
};

static matrix_float4x4 make_perspective(float fovyRadians, float aspect, float nearZ, float farZ) {
    float y = 1.0f / tanf(fovyRadians * 0.5f);
    float x = y / aspect;
    float z = farZ / (nearZ - farZ);

    matrix_float4x4 m = matrix_identity_float4x4;
    m.columns[0] = simd_make_float4(x, 0.0f, 0.0f, 0.0f);
    m.columns[1] = simd_make_float4(0.0f, y, 0.0f, 0.0f);
    m.columns[2] = simd_make_float4(0.0f, 0.0f, z, -1.0f);
    m.columns[3] = simd_make_float4(0.0f, 0.0f, z * nearZ, 0.0f);
    return m;
}

static matrix_float4x4 make_look_at(vector_float3 eye, vector_float3 center, vector_float3 up) {
    vector_float3 z = normalize(eye - center);
    vector_float3 x = normalize(cross(up, z));
    vector_float3 y = cross(z, x);

    matrix_float4x4 m = matrix_identity_float4x4;
    m.columns[0] = simd_make_float4(x.x, y.x, z.x, 0.0f);
    m.columns[1] = simd_make_float4(x.y, y.y, z.y, 0.0f);
    m.columns[2] = simd_make_float4(x.z, y.z, z.z, 0.0f);
    m.columns[3] = simd_make_float4(-dot(x, eye), -dot(y, eye), -dot(z, eye), 1.0f);
    return m;
}


Mtl_Engine::Mtl_Engine(MTK::View* view)
    : view_(view), device_(nullptr), commandQueue_(nullptr), pipelineState_(nullptr), depthStencilState_(nullptr), aspect_(1280.0f / 720.0f),
      blas_(nullptr), tlas_(nullptr), rtComputePipelineState_(nullptr),
      gWorldPos_(nullptr), gNormal_(nullptr), depthTexture_(nullptr), rtOutput_(nullptr), denoiseOutput_(nullptr), motionVectors_(nullptr), temporalDenoiser_(nullptr), resetDenoiserHistory_(true),
      useRayTracing_(false), rayTracingSupported_(false), rayTracingSwitch_(true),
      fallbackVB_(nullptr), fallbackIB_(nullptr), vertexBuffer_(nullptr), materialDiffuseBuffer_(nullptr), rtTriangleIndexBuffer_(nullptr), rtGeometryTriangleBaseBuffer_(nullptr), axesVB_(nullptr), axesCount_(0), axesPipelineState_(nullptr),
      lightCenter_(simd_make_float3(0.0f, 0.0f, 0.0f)), lightU_(simd_make_float3(0.0f, 0.0f, 0.0f)), lightV_(simd_make_float3(0.0f, 0.0f, 0.0f)),
      lightNormal_(simd_make_float3(0.0f, -1.0f, 0.0f)),
      renderMode_(1), denoisingEnabled_(true), cameraPanSpeedFactor_(0.0f), metallicBias_(0.0f), roughnessBias_(0.0f), exposure_(1.0f), cameraOrbitAngle_(0.0f) {

    device_ = MTL::CreateSystemDefaultDevice();
    if (!device_) {
        std::cerr << "No Metal device." << std::endl;
        return;
    }

    commandQueue_ = device_->newCommandQueue();

    // Test if Metal is enabled
    std::cout << "Metal Device: " << device_->name()->utf8String() << std::endl;

    if (device_->supportsFamily(MTL::GPUFamilyMetal4)) {
        std::cout << "Metal API Version: Metal 4" << std::endl;
    } else if (device_->supportsFamily(MTL::GPUFamilyMetal3)) {
        std::cout << "Metal API Version: Metal 3" << std::endl;
    } else {
        std::cout << "Metal API Version: Metal 2 or lower" << std::endl;
    }

    rayTracingSupported_ = device_->supportsRaytracing();
    useRayTracing_ = rayTracingSupported_ && rayTracingSwitch_;

    std::cout << "Supports Ray Tracing: " << (rayTracingSupported_ ? "Yes" : "No") << std::endl;
    std::cout << "Enabled Ray Tracing: " << (useRayTracing_ ? "Yes" : "No") << std::endl;

    buildPipeline();
    // buildAxes();
    buildFallbackTriangle();

    minBounds_ = simd_make_float3(-1.0f, -1.0f, -1.0f);
    maxBounds_ = simd_make_float3(1.0f, 1.0f, 1.0f);

    // Loading model
    NS::Bundle* mainBundle = NS::Bundle::mainBundle();
    NS::String* resourcePath = mainBundle->resourcePath();
    std::string bundlePath = resourcePath ? resourcePath->utf8String() : "";
    bool loadedModel = false;

    if (!bundlePath.empty()) {
        // Try both common bundle layouts.
        loadedModel = loadOBJ(bundlePath + "/cornell-box.obj");
        if (!loadedModel) {
            loadedModel = loadOBJ(bundlePath + "/Models/cornell-box.obj");
        }
    }

    if (!loadedModel) {
        std::cout << "Falling back to local cornell-box.obj" << std::endl;
        loadedModel = loadOBJ("Models/cornell-box.obj");
    }

    if (!loadedModel) {
        std::cerr << "Failed to load cornell-box.obj from bundle or local paths." << std::endl;
    }

    createGBuffer(1280, 720);
    if (useRayTracing_) {
        buildAccelerationStructure();
        buildComputePipeline();
    }
}

Mtl_Engine::~Mtl_Engine() {
    if (pipelineState_) pipelineState_->release();
    if (depthStencilState_) depthStencilState_->release();
    if (commandQueue_) commandQueue_->release();
    if (fallbackVB_) fallbackVB_->release();
    if (fallbackIB_) fallbackIB_->release();
    if (axesVB_) axesVB_->release();
    if (axesPipelineState_) axesPipelineState_->release();
    if (vertexBuffer_) vertexBuffer_->release();
    if (materialDiffuseBuffer_) materialDiffuseBuffer_->release();
    if (rtTriangleIndexBuffer_) rtTriangleIndexBuffer_->release();
    if (rtGeometryTriangleBaseBuffer_) rtGeometryTriangleBaseBuffer_->release();
    for (auto& group : meshGroups_) {
        if (group.indexBuffer) group.indexBuffer->release();
    }
    if (blas_) blas_->release();
    if (tlas_) tlas_->release();
    if (rtComputePipelineState_) rtComputePipelineState_->release();
    if (gWorldPos_) gWorldPos_->release();
    if (gNormal_) gNormal_->release();
    if (depthTexture_) depthTexture_->release();
    if (rtOutput_) rtOutput_->release();
    if (denoiseOutput_) denoiseOutput_->release();
    if (motionVectors_) motionVectors_->release();
    if (temporalDenoiser_) temporalDenoiser_->release();
    if (device_) device_->release();
}

void Mtl_Engine::resize(float width, float height) {
    aspect_ = (height > 0.0f) ? (width / height) : 1.0f;
    createGBuffer(width, height);
}

void Mtl_Engine::setRenderMode(uint32_t renderMode) {
    renderMode_ = renderMode;
    rayTracingSwitch_ = (renderMode_ != 0);
    useRayTracing_ = rayTracingSupported_ && rayTracingSwitch_;
}

void Mtl_Engine::setCameraPanSpeedFactor(float speedFactor) {
    cameraPanSpeedFactor_ = speedFactor;
}

void Mtl_Engine::setMetallicBias(float metallicBias) {
    metallicBias_ = std::max(-1.0f, std::min(1.0f, metallicBias));
}

void Mtl_Engine::setRoughnessBias(float roughnessBias) {
    roughnessBias_ = std::max(-1.0f, std::min(1.0f, roughnessBias));
}

void Mtl_Engine::setExposure(float exposure) {
    exposure_ = std::max(exposure, 0.01f);
}

void Mtl_Engine::setDenoisingEnabled(bool enabled) {
    denoisingEnabled_ = enabled;
    resetDenoiserHistory_ = true;
}

void Mtl_Engine::draw(MTL::RenderPassDescriptor* rpd, CA::Drawable* drawable) {
    if (!device_ || !commandQueue_ || !pipelineState_) {
        return;
    }

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    MTL::Texture* tex0 = rpd->colorAttachments()->object(0)->texture();
    if (!tex0) {
        pool->release();
        return;
    }

    if (!gWorldPos_ || gWorldPos_->width() != tex0->width() || gWorldPos_->height() != tex0->height()) {
        createGBuffer((float)tex0->width(), (float)tex0->height());
    }

    MTL::CommandBuffer* cb = commandQueue_->commandBuffer();
    if (!cb) {
        pool->release();
        return;
    }

    cameraOrbitAngle_ += cameraPanSpeedFactor_ * 0.01f;
    vector_float3 center = (maxBounds_ + minBounds_) * 0.5f;
    vector_float3 size = maxBounds_ - minBounds_;
    float maxDim = std::max(size.x, std::max(size.y, size.z));
    float cameraDistance = std::max(maxDim * 1.2f, 3.0f);
    float orbitRadius = std::max(maxDim * 0.25f, 0.25f);
    vector_float3 eye = simd_make_float3(
        center.x + sinf(cameraOrbitAngle_) * orbitRadius,
        center.y + maxDim * 0.10f,
        maxBounds_.z + cameraDistance
    );
    vector_float3 target = simd_make_float3(center.x, center.y, center.z - maxDim * 0.10f);
    matrix_float4x4 model = matrix_identity_float4x4;
    matrix_float4x4 viewM = make_look_at(eye, target, simd_make_float3(0.0f, 1.0f, 0.0f));
    matrix_float4x4 proj = make_perspective(65.0f * (float)M_PI / 180.0f, aspect_, 0.1f, cameraDistance * 10.0f);

    Uniforms uniforms = {};
    uniforms.mvp = proj * viewM * model;
    uniforms.lightPos = simd_make_float4(lightCenter_.x, lightCenter_.y, lightCenter_.z, 1.0f);
    uniforms.lightU = simd_make_float4(lightU_.x, lightU_.y, lightU_.z, 0.0f);
    uniforms.lightV = simd_make_float4(lightV_.x, lightV_.y, lightV_.z, 0.0f);
    uniforms.lightNormal = simd_make_float4(lightNormal_.x, lightNormal_.y, lightNormal_.z, 0.0f);
    uniforms.cameraPos = simd_make_float4(eye.x, eye.y, eye.z, 1.0f);
    uniforms.tuning = simd_make_float4(metallicBias_, roughnessBias_, exposure_, (float)renderMode_);
    uniforms.sceneMin = simd_make_float4(minBounds_.x, minBounds_.y, minBounds_.z, 0.0f);
    uniforms.sceneMax = simd_make_float4(maxBounds_.x, maxBounds_.y, maxBounds_.z, 0.0f);

    auto encodeScenePass = [&](const Uniforms& passUniforms,
                               MTL::Texture* rtColorTexture,
                               MTL::Texture* rtVisibilityTexture,
                               bool clearDrawable) {
        rpd->colorAttachments()->object(0)->setLoadAction(clearDrawable ? MTL::LoadActionClear : MTL::LoadActionLoad);
        rpd->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
        rpd->colorAttachments()->object(0)->setClearColor(MTL::ClearColor(0.1, 0.1, 0.1, 1.0));

        if (gWorldPos_ && gNormal_) {
            rpd->colorAttachments()->object(1)->setTexture(gWorldPos_);
            rpd->colorAttachments()->object(1)->setLoadAction(MTL::LoadActionClear);
            rpd->colorAttachments()->object(1)->setStoreAction(MTL::StoreActionStore);
            rpd->colorAttachments()->object(1)->setClearColor(MTL::ClearColor(0, 0, 0, 0));

            rpd->colorAttachments()->object(2)->setTexture(gNormal_);
            rpd->colorAttachments()->object(2)->setLoadAction(MTL::LoadActionClear);
            rpd->colorAttachments()->object(2)->setStoreAction(MTL::StoreActionStore);
            rpd->colorAttachments()->object(2)->setClearColor(MTL::ClearColor(0, 0, 0, 0));
        }

        if (rpd->depthAttachment()) {
            if (depthTexture_) {
                rpd->depthAttachment()->setTexture(depthTexture_);
            }
            if (rpd->depthAttachment()->texture()) {
                rpd->depthAttachment()->setLoadAction(MTL::LoadActionClear);
                rpd->depthAttachment()->setStoreAction(MTL::StoreActionStore);
                rpd->depthAttachment()->setClearDepth(1.0);
            }
        }

        MTL::RenderCommandEncoder* enc = cb->renderCommandEncoder(rpd);
        if (!enc) {
            return;
        }

        enc->setRenderPipelineState(pipelineState_);
        enc->setDepthStencilState(depthStencilState_);
        enc->setVertexBytes(&passUniforms, sizeof(Uniforms), 1);
        enc->setFragmentBytes(&passUniforms, sizeof(Uniforms), 1);
        enc->setFragmentTexture(rtColorTexture, 0);
        enc->setFragmentTexture(rtVisibilityTexture ? rtVisibilityTexture : rtColorTexture, 1);

        if (vertexBuffer_ && !meshGroups_.empty()) {
            enc->setVertexBuffer(vertexBuffer_, 0, 0);
            for (const auto& group : meshGroups_) {
                Material adjustedMaterial = group.material;
                if (passUniforms.tuning.w > 0.5f) {
                    adjustedMaterial.specular += simd_make_float4(metallicBias_, metallicBias_, metallicBias_, 0.0f);
                    adjustedMaterial.specular = simd_clamp(adjustedMaterial.specular, simd_make_float4(0.0f), simd_make_float4(1.0f));
                    adjustedMaterial.shininess = std::max(2.0f, adjustedMaterial.shininess * (1.0f - roughnessBias_));
                }
                enc->setFragmentBytes(&adjustedMaterial, sizeof(Material), 0);
                enc->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, group.indexCount, MTL::IndexTypeUInt32, group.indexBuffer, 0);
            }
        } else {
            enc->setVertexBuffer(fallbackVB_, 0, 0);
            enc->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, 3, MTL::IndexTypeUInt16, fallbackIB_, 0);
        }

        if (axesVB_ && axesPipelineState_) {
            enc->setRenderPipelineState(axesPipelineState_);
            matrix_float4x4 axesMVP = proj * viewM;
            enc->setVertexBytes(&axesMVP, sizeof(axesMVP), 1);
            enc->setVertexBuffer(axesVB_, 0, 0);
            enc->drawPrimitives(MTL::PrimitiveTypeLine, (NS::UInteger)0, axesCount_);
        }

        enc->endEncoding();
    };

    const bool shouldRunHybrid = useRayTracing_ && rtComputePipelineState_ && tlas_ && rtOutput_ && gWorldPos_ && gNormal_;
    if (shouldRunHybrid) {
        Uniforms prepassUniforms = uniforms;
        prepassUniforms.tuning.w = 0.0f;
        encodeScenePass(prepassUniforms, nullptr, nullptr, true);

        MTL::ComputeCommandEncoder* cce = cb->computeCommandEncoder();
        if (cce) {
            cce->setComputePipelineState(rtComputePipelineState_);
            cce->setTexture(rtOutput_, 0);
            cce->setTexture(gWorldPos_, 1);
            cce->setTexture(gNormal_, 2);
            cce->setAccelerationStructure(tlas_, 0);
            cce->setBytes(&uniforms, sizeof(Uniforms), 1);
            if (materialDiffuseBuffer_) {
                cce->setBuffer(materialDiffuseBuffer_, 0, 2);
            }
            uint32_t materialCount = (uint32_t)meshGroups_.size();
            cce->setBytes(&materialCount, sizeof(materialCount), 3);
            if (rtTriangleIndexBuffer_) {
                cce->setBuffer(rtTriangleIndexBuffer_, 0, 4);
            }
            if (rtGeometryTriangleBaseBuffer_) {
                cce->setBuffer(rtGeometryTriangleBaseBuffer_, 0, 5);
            }
            if (vertexBuffer_) {
                cce->setBuffer(vertexBuffer_, 0, 6);
            }

            MTL::Size threadsPerGrid = MTL::Size(rtOutput_->width(), rtOutput_->height(), 1);
            MTL::Size threadsPerThreadgroup = MTL::Size(8, 8, 1);
            cce->dispatchThreads(threadsPerGrid, threadsPerThreadgroup);
            cce->endEncoding();
        }

        bool denoiseApplied = false;
        MTL::Texture* depthTexture = (rpd->depthAttachment() != nullptr) ? rpd->depthAttachment()->texture() : nullptr;
        if (denoisingEnabled_ && temporalDenoiser_ && denoiseOutput_ && motionVectors_ && depthTexture) {
            if (temporalDenoiser_->depthTextureFormat() != depthTexture->pixelFormat()) {
                temporalDenoiser_->release();
                temporalDenoiser_ = nullptr;

                MTLFX::TemporalDenoisedScalerDescriptor* denoiseDescriptor = MTLFX::TemporalDenoisedScalerDescriptor::alloc()->init();
                if (denoiseDescriptor) {
                    denoiseDescriptor->setInputWidth(rtOutput_->width());
                    denoiseDescriptor->setInputHeight(rtOutput_->height());
                    denoiseDescriptor->setOutputWidth(rtOutput_->width());
                    denoiseDescriptor->setOutputHeight(rtOutput_->height());
                    denoiseDescriptor->setColorTextureFormat(MTL::PixelFormatRGBA16Float);
                    denoiseDescriptor->setDepthTextureFormat(depthTexture->pixelFormat());
                    denoiseDescriptor->setMotionTextureFormat(MTL::PixelFormatRG16Float);
                    denoiseDescriptor->setNormalTextureFormat(MTL::PixelFormatRGBA16Float);
                    denoiseDescriptor->setDiffuseAlbedoTextureFormat(MTL::PixelFormatRGBA16Float);
                    denoiseDescriptor->setSpecularAlbedoTextureFormat(MTL::PixelFormatRGBA16Float);
                    denoiseDescriptor->setRoughnessTextureFormat(MTL::PixelFormatRGBA16Float);
                    denoiseDescriptor->setOutputTextureFormat(MTL::PixelFormatRGBA16Float);
                    denoiseDescriptor->setAutoExposureEnabled(false);
                    temporalDenoiser_ = denoiseDescriptor->newTemporalDenoisedScaler(device_);
                    denoiseDescriptor->release();
                    resetDenoiserHistory_ = true;
                }
            }
        }

        if (denoisingEnabled_ && temporalDenoiser_ && denoiseOutput_ && motionVectors_ && depthTexture) {
            temporalDenoiser_->setColorTexture(rtOutput_);
            temporalDenoiser_->setDepthTexture(depthTexture);
            temporalDenoiser_->setMotionTexture(motionVectors_);
            temporalDenoiser_->setNormalTexture(gNormal_);
            temporalDenoiser_->setDiffuseAlbedoTexture(rtOutput_);
            temporalDenoiser_->setSpecularAlbedoTexture(rtOutput_);
            temporalDenoiser_->setRoughnessTexture(gNormal_);
            temporalDenoiser_->setOutputTexture(denoiseOutput_);
            temporalDenoiser_->setJitterOffsetX(0.0f);
            temporalDenoiser_->setJitterOffsetY(0.0f);
            temporalDenoiser_->setMotionVectorScaleX(1.0f);
            temporalDenoiser_->setMotionVectorScaleY(1.0f);
            temporalDenoiser_->setPreExposure(1.0f);
            temporalDenoiser_->setShouldResetHistory(resetDenoiserHistory_);
            temporalDenoiser_->encodeToCommandBuffer(cb);
            resetDenoiserHistory_ = false;
            denoiseApplied = true;
        }

        MTL::Texture* compositeColor = (denoiseApplied && denoiseOutput_) ? denoiseOutput_ : rtOutput_;
        MTL::Texture* compositeVisibility = rtOutput_;
        encodeScenePass(uniforms, compositeColor, compositeVisibility, true);
    } else {
        encodeScenePass(uniforms, nullptr, nullptr, true);
    }

    cb->presentDrawable((MTL::Drawable*)drawable);
    cb->commit();

    pool->release();
}

MTL::Library* Mtl_Engine::buildLibrary() {
    NS::Error* error = nullptr;

    // Prefer the app's compiled default Metal library.
    MTL::Library* library = device_->newDefaultLibrary();
    if (library) {
        return library;
    }

    // Fallback path for environments where source is loaded at runtime.
    NS::Bundle* mainBundle = NS::Bundle::mainBundle();
    NS::String* resourcePath = mainBundle->resourcePath();
    std::string basePath = resourcePath ? resourcePath->utf8String() : "";

    std::vector<std::string> shaderCandidates;
    shaderCandidates.emplace_back("Renderer/Shaders.metal");
    shaderCandidates.emplace_back("../Renderer/Shaders.metal");
    shaderCandidates.emplace_back("Shaders.metal");
    if (!basePath.empty()) {
        shaderCandidates.emplace_back(basePath + "/Shaders.metal");
        shaderCandidates.emplace_back(basePath + "/Renderer/Shaders.metal");
    }

    std::ifstream file;
    std::string shaderPath;
    for (const auto& candidate : shaderCandidates) {
        file.open(candidate);
        if (file.is_open()) {
            shaderPath = candidate;
            break;
        }
        file.clear();
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    if (source.empty()) {
        std::cerr << "Could not load Shaders.metal for runtime compile." << std::endl;
        return nullptr;
    }
    std::cout << "Compiling Metal shader from: " << shaderPath << std::endl;

    NS::String* mtlSource = NS::String::string(source.c_str(), NS::UTF8StringEncoding);
    MTL::CompileOptions* options = nullptr;
    library = device_->newLibrary(mtlSource, options, &error);

    if (!library) {
        if (error) {
            std::cerr << "Shader compilation error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    }
    return library;
}

void Mtl_Engine::buildPipeline() {
    NS::Error* error = nullptr;
    MTL::Library* library = buildLibrary();

    if (!library) {
        return;
    }

    NS::String* vsName = NS::String::string("vertexShader", NS::UTF8StringEncoding);
    NS::String* fsName = NS::String::string("fragmentShader", NS::UTF8StringEncoding);

    MTL::Function* vs = library->newFunction(vsName);
    MTL::Function* fs = library->newFunction(fsName);

    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vs);
    desc->setFragmentFunction(fs);

    MTL::VertexDescriptor* vd = MTL::VertexDescriptor::alloc()->init();
    vd->attributes()->object(0)->setFormat(MTL::VertexFormatFloat3);
    vd->attributes()->object(0)->setOffset(0);
    vd->attributes()->object(0)->setBufferIndex(0);

    vd->attributes()->object(1)->setFormat(MTL::VertexFormatFloat3);
    vd->attributes()->object(1)->setOffset(sizeof(vector_float3));
    vd->attributes()->object(1)->setBufferIndex(0);

    vd->attributes()->object(2)->setFormat(MTL::VertexFormatFloat2);
    vd->attributes()->object(2)->setOffset(sizeof(vector_float3) * 2);
    vd->attributes()->object(2)->setBufferIndex(0);

    vd->layouts()->object(0)->setStride(sizeof(Vertex));

    desc->setVertexDescriptor(vd);
    desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    desc->colorAttachments()->object(1)->setPixelFormat(MTL::PixelFormatRGBA16Float);
    desc->colorAttachments()->object(2)->setPixelFormat(MTL::PixelFormatRGBA16Float);
    desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

    pipelineState_ = device_->newRenderPipelineState(desc, &error);

    if (!pipelineState_) {
        if (error) {
            std::cerr << "Pipeline error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    }

    MTL::DepthStencilDescriptor* dsDesc = MTL::DepthStencilDescriptor::alloc()->init();
    dsDesc->setDepthCompareFunction(MTL::CompareFunctionLess);
    dsDesc->setDepthWriteEnabled(true);
    depthStencilState_ = device_->newDepthStencilState(dsDesc);
    dsDesc->release();

    // Build Axes Pipeline
    NS::String* vsAxesName = NS::String::string("vertexAxes", NS::UTF8StringEncoding);
    NS::String* fsAxesName = NS::String::string("fragmentAxes", NS::UTF8StringEncoding);
    MTL::Function* vsAxes = library->newFunction(vsAxesName);
    MTL::Function* fsAxes = library->newFunction(fsAxesName);

    desc->setVertexFunction(vsAxes);
    desc->setFragmentFunction(fsAxes);
    desc->setVertexDescriptor(vd);

    axesPipelineState_ = device_->newRenderPipelineState(desc, &error);
    if (!axesPipelineState_) {
        if (error) {
            std::cerr << "Axes Pipeline error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    }

    vsAxes->release();
    fsAxes->release();
    vd->release();
    desc->release();
    vs->release();
    fs->release();
    library->release();
}

void Mtl_Engine::buildFallbackTriangle() {
    static const Vertex vertices[] = {
        {{ 0.0f,  0.6f, 0.0f }, {0,0,1}, {0.5f, 1.0f}},
        {{-0.6f, -0.6f, 0.0f }, {0,0,1}, {0.0f, 0.0f}},
        {{ 0.6f, -0.6f, 0.0f }, {0,0,1}, {1.0f, 0.0f}},
    };
    static const uint16_t indices[] = { 0, 1, 2 };

    fallbackVB_ = device_->newBuffer(vertices, sizeof(vertices), MTL::ResourceStorageModeShared);
    fallbackIB_ = device_->newBuffer(indices, sizeof(indices), MTL::ResourceStorageModeShared);
}

bool Mtl_Engine::loadMTL(const std::string& path, std::map<std::string, Material>& materials) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Could not open MTL file: " << path << std::endl;
        return false;
    }

    std::string line;
    std::string currentMaterial;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string prefix;
        ss >> prefix;

        if (prefix == "newmtl") {
            ss >> currentMaterial;
            materials[currentMaterial] = {
                simd_make_float4(0.8f, 0.8f, 0.8f, 1.0f), // diffuse
                simd_make_float4(0.0f, 0.0f, 0.0f, 1.0f), // emissive
                simd_make_float4(0.0f, 0.0f, 0.0f, 1.0f), // specular
                0.0f, // shininess
                1.0f, // opacity
                1.0f, // ior
                0.0f  // reflection
            };
        } else if (prefix == "Kd") {
            float r, g, b;
            ss >> r >> g >> b;
            materials[currentMaterial].diffuse = simd_make_float4(r, g, b, 1.0f);
        } else if (prefix == "Ke") {
            float r, g, b;
            ss >> r >> g >> b;
            materials[currentMaterial].emissive = simd_make_float4(r, g, b, 1.0f);
        } else if (prefix == "Ks") {
            float r, g, b;
            ss >> r >> g >> b;
            materials[currentMaterial].specular = simd_make_float4(r, g, b, 1.0f);
        } else if (prefix == "Ns") {
            float ns;
            ss >> ns;
            materials[currentMaterial].shininess = ns;
        } else if (prefix == "d" || prefix == "Tr") {
            float d;
            ss >> d;
            materials[currentMaterial].opacity = (prefix == "d") ? d : (1.0f - d);
        } else if (prefix == "Ni") {
            float ni;
            ss >> ni;
            materials[currentMaterial].ior = ni;
        } else if (prefix == "refl") {
            float r;
            ss >> r;
            materials[currentMaterial].reflection = r;
        }
    }
    return true;
}

bool Mtl_Engine::loadOBJ(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Could not open OBJ file: " << path << std::endl;
        return false;
    }

    std::vector<vector_float3> positions;
    std::vector<vector_float3> normals;
    std::vector<vector_float2> uvs;
    std::vector<Vertex> outVertices;
    std::map<std::string, std::vector<uint32_t>> groupIndices;
    std::map<std::tuple<int, int, int>, uint32_t> vertexCache;
    std::map<std::string, Material> materials;

    std::string currentGroup = "default";
    std::string mtlLib;

    lightCenter_ = simd_make_float3(0.0f, 0.0f, 0.0f);
    lightU_ = simd_make_float3(0.0f, 0.0f, 0.0f);
    lightV_ = simd_make_float3(0.0f, 0.0f, 0.0f);
    lightNormal_ = simd_make_float3(0.0f, -1.0f, 0.0f);
    std::vector<vector_float3> lightVertices;

    minBounds_ = simd_make_float3(INFINITY, INFINITY, INFINITY);
    maxBounds_ = simd_make_float3(-INFINITY, -INFINITY, -INFINITY);

    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string prefix;
        ss >> prefix;

        if (prefix == "mtllib") {
            ss >> mtlLib;
            std::string mtlPath = path.substr(0, path.find_last_of("/\\") + 1) + mtlLib;
            loadMTL(mtlPath, materials);
        } else if (prefix == "usemtl") {
            ss >> currentGroup;
        } else if (prefix == "v") {
            float x, y, z;
            ss >> x >> y >> z;
            vector_float3 pos = simd_make_float3(x, y, z);
            positions.push_back(pos);
            minBounds_ = simd_min(minBounds_, pos);
            maxBounds_ = simd_max(maxBounds_, pos);
        } else if (prefix == "vn") {
            float x, y, z;
            ss >> x >> y >> z;
            normals.push_back(simd_make_float3(x, y, z));
        } else if (prefix == "vt") {
            float u, v;
            ss >> u >> v;
            uvs.push_back(simd_make_float2(u, v));
        } else if (prefix == "f") {
            for (int i = 0; i < 3; ++i) {
                std::string vStr;
                ss >> vStr;
                if (vStr.empty()) continue;

                int vIdx = -1, tIdx = -1, nIdx = -1;
                size_t firstSlash = vStr.find('/');
                if (firstSlash != std::string::npos) {
                    vIdx = std::stoi(vStr.substr(0, firstSlash)) - 1;
                    size_t secondSlash = vStr.find('/', firstSlash + 1);
                    if (secondSlash != std::string::npos) {
                        if (secondSlash > firstSlash + 1) {
                            tIdx = std::stoi(vStr.substr(firstSlash + 1, secondSlash - firstSlash - 1)) - 1;
                        }
                        nIdx = std::stoi(vStr.substr(secondSlash + 1)) - 1;
                    } else {
                        tIdx = std::stoi(vStr.substr(firstSlash + 1)) - 1;
                    }
                } else {
                    vIdx = std::stoi(vStr) - 1;
                }

                if (currentGroup == "Light" && vIdx >= 0 && vIdx < (int)positions.size()) {
                    lightVertices.push_back(positions[vIdx]);
                }

                auto key = std::make_tuple(vIdx, tIdx, nIdx);
                if (vertexCache.count(key)) {
                    groupIndices[currentGroup].push_back(vertexCache[key]);
                } else {
                    uint32_t newIdx = (uint32_t)outVertices.size();
                    vertexCache[key] = newIdx;
                    groupIndices[currentGroup].push_back(newIdx);

                    Vertex v = {};
                    if (vIdx >= 0 && vIdx < (int)positions.size()) v.position = positions[vIdx];
                    if (nIdx >= 0 && nIdx < (int)normals.size()) v.normal = normals[nIdx];
                    if (tIdx >= 0 && tIdx < (int)uvs.size()) v.uv = uvs[tIdx];
                    outVertices.push_back(v);
                }
            }
        }
    }

    if (outVertices.empty()) return false;

    if (lightVertices.size() >= 3) {
        std::vector<vector_float3> uniqueLightVertices;
        uniqueLightVertices.reserve(lightVertices.size());
        for (const auto& p : lightVertices) {
            bool exists = false;
            for (const auto& q : uniqueLightVertices) {
                if (simd_length_squared(p - q) < 1e-8f) {
                    exists = true;
                    break;
                }
            }
            if (!exists) uniqueLightVertices.push_back(p);
        }

        vector_float3 center = simd_make_float3(0.0f, 0.0f, 0.0f);
        for (const auto& p : uniqueLightVertices) {
            center += p;
        }
        center /= (float)uniqueLightVertices.size();
        lightCenter_ = center;

        vector_float3 p0 = uniqueLightVertices[0];
        vector_float3 p1 = uniqueLightVertices[1];
        vector_float3 p2 = uniqueLightVertices[2];
        vector_float3 normal = normalize(cross(p1 - p0, p2 - p0));
        if (!std::isfinite(normal.x) || simd_length_squared(normal) < 1e-8f) {
            normal = simd_make_float3(0.0f, -1.0f, 0.0f);
        }

        vector_float3 uDir = normalize(p1 - p0);
        if (!std::isfinite(uDir.x) || simd_length_squared(uDir) < 1e-8f) {
            uDir = simd_make_float3(1.0f, 0.0f, 0.0f);
        }
        vector_float3 vDir = normalize(cross(normal, uDir));
        if (!std::isfinite(vDir.x) || simd_length_squared(vDir) < 1e-8f) {
            vDir = simd_make_float3(0.0f, 0.0f, 1.0f);
        }

        float minU = INFINITY;
        float maxU = -INFINITY;
        float minV = INFINITY;
        float maxV = -INFINITY;
        for (const auto& p : uniqueLightVertices) {
            vector_float3 r = p - center;
            float u = dot(r, uDir);
            float v = dot(r, vDir);
            minU = std::min(minU, u);
            maxU = std::max(maxU, u);
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
        }

        lightU_ = uDir * std::max((maxU - minU) * 0.5f, 0.001f);
        lightV_ = vDir * std::max((maxV - minV) * 0.5f, 0.001f);
        lightNormal_ = normal;
    } else {
        vector_float3 center = (maxBounds_ + minBounds_) * 0.5f;
        lightCenter_ = simd_make_float3(center.x, maxBounds_.y - 0.1f, center.z);
        float lightExtent = std::max((maxBounds_.x - minBounds_.x) * 0.12f, 0.3f);
        lightU_ = simd_make_float3(lightExtent, 0.0f, 0.0f);
        lightV_ = simd_make_float3(0.0f, 0.0f, lightExtent);
        lightNormal_ = simd_make_float3(0.0f, -1.0f, 0.0f);
    }

    vertexBuffer_ = device_->newBuffer(outVertices.data(), outVertices.size() * sizeof(Vertex), MTL::ResourceStorageModeShared);

    for (auto const& [name, indices] : groupIndices) {
        if (indices.empty()) continue;
        MeshGroup group;
        group.indexCount = (uint32_t)indices.size();
        group.indexBuffer = device_->newBuffer(indices.data(), indices.size() * sizeof(uint32_t), MTL::ResourceStorageModeShared);
        if (materials.count(name)) {
            group.material = materials[name];
        } else {
            group.material = {
                simd_make_float4(0.8f, 0.8f, 0.8f, 1.0f),
                simd_make_float4(0.0f, 0.0f, 0.0f, 1.0f),
                simd_make_float4(0.0f, 0.0f, 0.0f, 1.0f),
                0.0f, 1.0f, 1.0f, 0.0f
            };
        }
        meshGroups_.push_back(group);
    }

    if (rtTriangleIndexBuffer_) {
        rtTriangleIndexBuffer_->release();
        rtTriangleIndexBuffer_ = nullptr;
    }
    if (rtGeometryTriangleBaseBuffer_) {
        rtGeometryTriangleBaseBuffer_->release();
        rtGeometryTriangleBaseBuffer_ = nullptr;
    }
    if (!meshGroups_.empty()) {
        std::vector<vector_uint3> triangleIndices;
        std::vector<uint32_t> triangleBaseByGeometry;
        triangleBaseByGeometry.reserve(meshGroups_.size());
        for (const auto& group : meshGroups_) {
            const uint32_t* idxData = static_cast<const uint32_t*>(group.indexBuffer->contents());
            uint32_t triCount = group.indexCount / 3;
            triangleBaseByGeometry.push_back((uint32_t)triangleIndices.size());
            for (uint32_t tri = 0; tri < triCount; ++tri) {
                uint32_t i0 = idxData[tri * 3 + 0];
                uint32_t i1 = idxData[tri * 3 + 1];
                uint32_t i2 = idxData[tri * 3 + 2];
                triangleIndices.push_back(simd_make_uint3(i0, i1, i2));
            }
        }

        if (!triangleIndices.empty()) {
            rtTriangleIndexBuffer_ = device_->newBuffer(triangleIndices.data(), triangleIndices.size() * sizeof(vector_uint3), MTL::ResourceStorageModeShared);
        }
        if (!triangleBaseByGeometry.empty()) {
            rtGeometryTriangleBaseBuffer_ = device_->newBuffer(triangleBaseByGeometry.data(), triangleBaseByGeometry.size() * sizeof(uint32_t), MTL::ResourceStorageModeShared);
        }
    }

    if (materialDiffuseBuffer_) {
        materialDiffuseBuffer_->release();
        materialDiffuseBuffer_ = nullptr;
    }
    if (!meshGroups_.empty()) {
        std::vector<vector_float3> diffuseByGroup;
        diffuseByGroup.reserve(meshGroups_.size());
        for (const auto& group : meshGroups_) {
            diffuseByGroup.push_back(simd_make_float3(group.material.diffuse.x, group.material.diffuse.y, group.material.diffuse.z));
        }
        materialDiffuseBuffer_ = device_->newBuffer(diffuseByGroup.data(), diffuseByGroup.size() * sizeof(vector_float3), MTL::ResourceStorageModeShared);
    }

    std::cout << "Loaded OBJ: " << path << " (" << outVertices.size() << " vertices, " << meshGroups_.size() << " groups)" << std::endl;
    std::cout << "Bounds: (" << minBounds_.x << "," << minBounds_.y << "," << minBounds_.z << ") to ("
              << maxBounds_.x << "," << maxBounds_.y << "," << maxBounds_.z << ")" << std::endl;
    return true;
}

void Mtl_Engine::buildAccelerationStructure() {
    if (!vertexBuffer_ || meshGroups_.empty()) return;

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    // 1. Build BLAS
    std::vector<MTL::AccelerationStructureTriangleGeometryDescriptor*> geometryDescriptors;
    for (const auto& group : meshGroups_) {
        auto geomDesc = MTL::AccelerationStructureTriangleGeometryDescriptor::descriptor();
        geomDesc->setVertexBuffer(vertexBuffer_);
        geomDesc->setVertexBufferOffset(0);
        geomDesc->setVertexStride(sizeof(Vertex));
        geomDesc->setIndexBuffer(group.indexBuffer);
        geomDesc->setIndexBufferOffset(0);
        geomDesc->setIndexType(MTL::IndexTypeUInt32);
        geomDesc->setTriangleCount(group.indexCount / 3);
        geomDesc->setOpaque(true);
        geometryDescriptors.push_back(geomDesc);
    }

    auto blasDesc = MTL::PrimitiveAccelerationStructureDescriptor::descriptor();
    NS::Array* geomArray = NS::Array::array((NS::Object* const*)geometryDescriptors.data(), geometryDescriptors.size());
    blasDesc->setGeometryDescriptors(geomArray);

    MTL::AccelerationStructureSizes blasSizes = device_->accelerationStructureSizes(blasDesc);
    blas_ = device_->newAccelerationStructure(blasSizes.accelerationStructureSize);

    MTL::Buffer* scratchBuffer = device_->newBuffer(blasSizes.buildScratchBufferSize, MTL::ResourceStorageModePrivate);
    if (!scratchBuffer && blasSizes.buildScratchBufferSize > 0) {
        std::cerr << "Failed to create BLAS scratch buffer" << std::endl;
        pool->release();
        return;
    }

    MTL::CommandBuffer* cb = commandQueue_->commandBuffer();
    if (!cb) {
        if (scratchBuffer) scratchBuffer->release();
        pool->release();
        return;
    }
    MTL::AccelerationStructureCommandEncoder* asEncoder = cb->accelerationStructureCommandEncoder();
    if (!asEncoder) {
        if (scratchBuffer) scratchBuffer->release();
        pool->release();
        return;
    }
    asEncoder->buildAccelerationStructure(blas_, blasDesc, scratchBuffer, 0);
    asEncoder->endEncoding();
    cb->commit();
    cb->waitUntilCompleted();

    if (scratchBuffer) scratchBuffer->release();

    // 2. Build TLAS
    auto tlasDesc = MTL::InstanceAccelerationStructureDescriptor::descriptor();
    tlasDesc->setInstanceCount(1);

    MTL::AccelerationStructureInstanceDescriptor instanceDesc;
    instanceDesc.accelerationStructureIndex = 0;
    instanceDesc.options = MTL::AccelerationStructureInstanceOptionNone;
    instanceDesc.intersectionFunctionTableOffset = 0;
    instanceDesc.mask = 0xFF;

    // Identity matrix (3x4)
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            instanceDesc.transformationMatrix[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }

    MTL::Buffer* instanceBuffer = device_->newBuffer(&instanceDesc, sizeof(instanceDesc), MTL::ResourceStorageModeShared);
    if (!instanceBuffer) {
        std::cerr << "Failed to create instance buffer" << std::endl;
        pool->release();
        return;
    }
    tlasDesc->setInstanceDescriptorBuffer(instanceBuffer);

    NS::Array* blasArray = NS::Array::array((NS::Object* const*)&blas_, 1);
    tlasDesc->setInstancedAccelerationStructures(blasArray);

    MTL::AccelerationStructureSizes tlasSizes = device_->accelerationStructureSizes(tlasDesc);
    tlas_ = device_->newAccelerationStructure(tlasSizes.accelerationStructureSize);

    MTL::Buffer* tlasScratchBuffer = device_->newBuffer(tlasSizes.buildScratchBufferSize, MTL::ResourceStorageModePrivate);
    if (!tlasScratchBuffer && tlasSizes.buildScratchBufferSize > 0) {
         std::cerr << "Failed to create TLAS scratch buffer" << std::endl;
         instanceBuffer->release();
         pool->release();
         return;
    }

    cb = commandQueue_->commandBuffer();
    if (!cb) {
        if (tlasScratchBuffer) tlasScratchBuffer->release();
        instanceBuffer->release();
        pool->release();
        return;
    }
    asEncoder = cb->accelerationStructureCommandEncoder();
    if (!asEncoder) {
        if (tlasScratchBuffer) tlasScratchBuffer->release();
        instanceBuffer->release();
        pool->release();
        return;
    }
    asEncoder->buildAccelerationStructure(tlas_, tlasDesc, tlasScratchBuffer, 0);
    asEncoder->endEncoding();
    cb->commit();
    cb->waitUntilCompleted();

    if (tlasScratchBuffer) tlasScratchBuffer->release();
    // Do NOT release instanceBuffer here.
    instanceBuffer->release();

    pool->release();
}

void Mtl_Engine::buildComputePipeline() {
    NS::Error* error = nullptr;
    MTL::Library* library = buildLibrary();

    if (!library) {
        return;
    }

    MTL::Function* rtFunc = library->newFunction(NS::String::string("rtMain", NS::UTF8StringEncoding));
    if (!rtFunc) {
        std::cerr << "Could not find rtMain function" << std::endl;
        return;
    }

    rtComputePipelineState_ = device_->newComputePipelineState(rtFunc, &error);
    if (!rtComputePipelineState_) {
        std::cerr << "Could not create compute pipeline: " << error->localizedDescription()->utf8String() << std::endl;
    }

    rtFunc->release();

    library->release();
}

void Mtl_Engine::createGBuffer(float width, float height) {
    if (!device_) {
        std::cerr << "createGBuffer: No Metal device." << std::endl;
        return;
    }

    if (gWorldPos_) { gWorldPos_->release(); gWorldPos_ = nullptr; }
    if (gNormal_) { gNormal_->release(); gNormal_ = nullptr; }
    if (depthTexture_) { depthTexture_->release(); depthTexture_ = nullptr; }
    if (rtOutput_) { rtOutput_->release(); rtOutput_ = nullptr; }
    if (denoiseOutput_) { denoiseOutput_->release(); denoiseOutput_ = nullptr; }
    if (motionVectors_) { motionVectors_->release(); motionVectors_ = nullptr; }
    if (temporalDenoiser_) { temporalDenoiser_->release(); temporalDenoiser_ = nullptr; }
    resetDenoiserHistory_ = true;

    if (width <= 0 || height <= 0) return;

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA16Float, (NS::UInteger)width, (NS::UInteger)height, false);
    if (desc) {
        desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        desc->setStorageMode(MTL::StorageModePrivate);
        gWorldPos_ = device_->newTexture(desc);

        gNormal_ = device_->newTexture(desc);

        desc->setUsage(MTL::TextureUsageShaderWrite | MTL::TextureUsageShaderRead);
        rtOutput_ = device_->newTexture(desc);
        denoiseOutput_ = device_->newTexture(desc);

        MTL::TextureDescriptor* depthDesc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatDepth32Float, (NS::UInteger)width, (NS::UInteger)height, false);
        if (depthDesc) {
            depthDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
            depthDesc->setStorageMode(MTL::StorageModePrivate);
            depthTexture_ = device_->newTexture(depthDesc);
        }

        MTL::TextureDescriptor* motionDesc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRG16Float, (NS::UInteger)width, (NS::UInteger)height, false);
        if (motionDesc) {
            motionDesc->setUsage(MTL::TextureUsageShaderRead);
            motionDesc->setStorageMode(MTL::StorageModeShared);
            motionVectors_ = device_->newTexture(motionDesc);

        }
        if (motionVectors_) {
            const NS::UInteger motionWidth = motionVectors_->width();
            const NS::UInteger motionHeight = motionVectors_->height();
            std::vector<uint16_t> zeroMotionData(motionWidth * motionHeight * 2, 0);
            MTL::Region fullRegion = MTL::Region(0, 0, 0, motionWidth, motionHeight, 1);
            motionVectors_->replaceRegion(fullRegion, 0, zeroMotionData.data(), motionWidth * sizeof(uint16_t) * 2);
        }

        MTLFX::TemporalDenoisedScalerDescriptor* denoiseDescriptor = MTLFX::TemporalDenoisedScalerDescriptor::alloc()->init();
        if (denoiseDescriptor) {
            denoiseDescriptor->setInputWidth((NS::UInteger)width);
            denoiseDescriptor->setInputHeight((NS::UInteger)height);
            denoiseDescriptor->setOutputWidth((NS::UInteger)width);
            denoiseDescriptor->setOutputHeight((NS::UInteger)height);
            denoiseDescriptor->setColorTextureFormat(MTL::PixelFormatRGBA16Float);
            denoiseDescriptor->setDepthTextureFormat(MTL::PixelFormatDepth32Float);
            denoiseDescriptor->setMotionTextureFormat(MTL::PixelFormatRG16Float);
            denoiseDescriptor->setNormalTextureFormat(MTL::PixelFormatRGBA16Float);
            denoiseDescriptor->setDiffuseAlbedoTextureFormat(MTL::PixelFormatRGBA16Float);
            denoiseDescriptor->setSpecularAlbedoTextureFormat(MTL::PixelFormatRGBA16Float);
            denoiseDescriptor->setRoughnessTextureFormat(MTL::PixelFormatRGBA16Float);

            denoiseDescriptor->setOutputTextureFormat(MTL::PixelFormatRGBA16Float);
            denoiseDescriptor->setAutoExposureEnabled(false);
            temporalDenoiser_ = denoiseDescriptor->newTemporalDenoisedScaler(device_);
            denoiseDescriptor->release();
        }
    }

    pool->release();
}

void Mtl_Engine::buildAxes() {
    float size = 1000.0f; // Large enough to be "infinite"
    Vertex vertices[] = {
        {{ 0.0f, 0.0f, 0.0f }, {1,0,0}, {0,0}}, {{ size, 0.0f, 0.0f }, {1,0,0}, {0,0}}, // X - Red
        {{ 0.0f, 0.0f, 0.0f }, {0,1,0}, {0,0}}, {{ 0.0f, size, 0.0f }, {0,1,0}, {0,0}}, // Y - Green
        {{ 0.0f, 0.0f, 0.0f }, {0,0,1}, {0,0}}, {{ 0.0f, 0.0f, size }, {0,0,1}, {0,0}}, // Z - Blue
        {{ 0.0f, 0.0f, 0.0f }, {0.5,0,0}, {0,0}}, {{ -size, 0.0f, 0.0f }, {0.5,0,0}, {0,0}},
        {{ 0.0f, 0.0f, 0.0f }, {0,0.5,0}, {0,0}}, {{ 0.0f, -size, 0.0f }, {0,0.5,0}, {0,0}},
        {{ 0.0f, 0.0f, 0.0f }, {0,0,0.5}, {0,0}}, {{ 0.0f, 0.0f, -size }, {0,0,0.5}, {0,0}},
    };
    axesCount_ = sizeof(vertices) / sizeof(Vertex);
    axesVB_ = device_->newBuffer(vertices, sizeof(vertices), MTL::ResourceStorageModeShared);
    if (!axesVB_) {
        std::cerr << "Failed to create axesVB" << std::endl;
    }
}

int Mtl_Engine::getTargetFPS()
{
    return TARGET_FPS;
}

//// AI-assisted code ////
// Used Codex on GPT 5.4 to implement a bridge to Apple's RT sample //
// Apple's RT sample is used as a View frame, not to do with rendering process //
//// START ////

extern "C" AAPLNativeEngine* CreateEngine(void* view) {
    return reinterpret_cast<AAPLNativeEngine*>(new Mtl_Engine((MTK::View*)view));
}

extern "C" void DestroyEngine(AAPLNativeEngine* engine) {
    delete reinterpret_cast<Mtl_Engine*>(engine);
}

extern "C" void EngineResize(AAPLNativeEngine* engine, float width, float height) {
    if (!engine) {
        return;
    }
    reinterpret_cast<Mtl_Engine*>(engine)->resize(width, height);
}

extern "C" void EngineDraw(AAPLNativeEngine* engine, void* renderPassDescriptor, void* drawable) {
    if (!engine || !renderPassDescriptor || !drawable) {
        return;
    }
    reinterpret_cast<Mtl_Engine*>(engine)->draw(
        reinterpret_cast<MTL::RenderPassDescriptor*>(renderPassDescriptor),
        reinterpret_cast<CA::Drawable*>(drawable)
    );
}

extern "C" void EngineSetRenderMode(AAPLNativeEngine* engine, uint32_t renderMode) {
    if (!engine) {
        return;
    }
    reinterpret_cast<Mtl_Engine*>(engine)->setRenderMode(renderMode);
}

extern "C" void EngineSetCameraPanSpeedFactor(AAPLNativeEngine* engine, float speedFactor) {
    if (!engine) {
        return;
    }
    reinterpret_cast<Mtl_Engine*>(engine)->setCameraPanSpeedFactor(speedFactor);
}

extern "C" void EngineSetMetallicBias(AAPLNativeEngine* engine, float metallicBias) {
    if (!engine) {
        return;
    }
    reinterpret_cast<Mtl_Engine*>(engine)->setMetallicBias(metallicBias);
}

extern "C" void EngineSetRoughnessBias(AAPLNativeEngine* engine, float roughnessBias) {
    if (!engine) {
        return;
    }
    reinterpret_cast<Mtl_Engine*>(engine)->setRoughnessBias(roughnessBias);
}

extern "C" void EngineSetExposure(AAPLNativeEngine* engine, float exposure) {
    if (!engine) {
        return;
    }
    reinterpret_cast<Mtl_Engine*>(engine)->setExposure(exposure);
}

extern "C" void EngineSetDenoisingEnabled(AAPLNativeEngine* engine, uint32_t enabled) {
    if (!engine) {
        return;
    }
    reinterpret_cast<Mtl_Engine*>(engine)->setDenoisingEnabled(enabled != 0U);
}

//// ENDS ////
//// AI-assisted code ////