//
//  Shaders.metal
//  HybridRenderingPlayground
//
//  Created by Neskol on 2026.04.07.
//

#include <metal_stdlib>
using namespace metal;
using namespace raytracing;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
};

struct RTTriangleVertex {
    float3 position;
    float3 normal;
    float2 uv;
};

struct VSOut {
    float4 position [[position]];
    float3 worldPos;
    float3 normal;
};

struct Uniforms {
    float4x4 mvp;
    float4 lightPos;
    float4 lightU;
    float4 lightV;
    float4 lightNormal;
    float4 cameraPos;
    float4 tuning;   // x: metallicBias, y: roughnessBias, z: exposure, w: renderMode
    float4 sceneMin; // xyz: bounds min
    float4 sceneMax; // xyz: bounds max
};

struct Material {
    float4 diffuse;
    float4 emissive;
    float4 specular;
    float shininess;
    float opacity;
    float ior;
    float reflection;
};

struct GBufferOut {
    float4 color    [[color(0)]];
    float4 worldPos [[color(1)]];
    float4 normal   [[color(2)]];
};

static float3 ToneMapACES(float3 x) {
    constexpr float a = 2.51;
    constexpr float b = 0.03;
    constexpr float c = 2.43;
    constexpr float d = 0.59;
    constexpr float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

inline uint Hash32(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

inline float Rand01(thread uint &state) {
    state = Hash32(state + 0x9e3779b9U);
    return float(state & 0x00FFFFFFU) / float(0x01000000U);
}

inline void BuildBasis(float3 n, thread float3 &t, thread float3 &b) {
    float3 up = (fabs(n.y) < 0.999) ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    t = normalize(cross(up, n));
    b = normalize(cross(n, t));
}

inline float3 SampleCosineWeightedHemisphere(float2 u) {
    float phi = 2.0 * M_PI_F * u.x;
    float cosPhi;
    float sinPhi = sincos(phi, cosPhi);
    float cosTheta = sqrt(u.y);
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    return float3(sinTheta * cosPhi, cosTheta, sinTheta * sinPhi);
}

inline float3 ResolveHitNormal(intersection_result<instancing, triangle_data> hit,
                               float3 rayDirection,
                               device const uint3 *triangleIndices,
                               device const uint *geometryTriangleBase,
                               device const RTTriangleVertex *vertices,
                               uint geometryCount) {
    if (!triangleIndices || !geometryTriangleBase || !vertices || geometryCount == 0) {
        return normalize(-rayDirection);
    }

    uint geomID = min(hit.geometry_id, geometryCount - 1);
    uint triBase = geometryTriangleBase[geomID];
    uint3 tri = triangleIndices[triBase + hit.primitive_id];

    float2 uv = hit.triangle_barycentric_coord;
    float w = saturate(1.0 - uv.x - uv.y);
    float3 shadingNormal = vertices[tri.x].normal * w + vertices[tri.y].normal * uv.x + vertices[tri.z].normal * uv.y;

    float3 faceNormal = cross(vertices[tri.y].position - vertices[tri.x].position,
                              vertices[tri.z].position - vertices[tri.x].position);

    float3 normal = (length_squared(shadingNormal) > 1e-8) ? normalize(shadingNormal) : normalize(faceNormal);
    if (!all(isfinite(normal)) || length_squared(normal) < 1e-8) {
        normal = normalize(-rayDirection);
    }

    if (dot(normal, rayDirection) > 0.0) {
        normal = -normal;
    }
    return normal;
}

inline float3 ResolveGeometryAlbedo(uint geometryID,
                                    device const float3 *materialDiffuse,
                                    uint materialCount) {
    if (!materialDiffuse || materialCount == 0) {
        return float3(0.72, 0.68, 0.62);
    }

    uint idx = min(geometryID, materialCount - 1);
    float3 albedo = materialDiffuse[idx];
    if (!all(isfinite(albedo)) || length_squared(albedo) < 1e-8) {
        return float3(0.72, 0.68, 0.62);
    }
    return saturate(albedo);
}

inline float3 EvaluateDirectAtPoint(float3 p,
                                    float3 n,
                                    float3 surfaceAlbedo,
                                    constant Uniforms &uniforms,
                                    intersector<instancing, triangle_data> inter,
                                    instance_acceleration_structure accelerationStructure,
                                    thread uint &rngState,
                                    uint sampleCount) {
    float3 outRadiance = float3(0.0);
    float3 lightNormal = normalize(uniforms.lightNormal.xyz);
    constexpr float kLightIntensity = 9.0;

    for (uint i = 0; i < sampleCount; ++i) {
        float2 jitter = float2(Rand01(rngState), Rand01(rngState)) * 2.0 - 1.0;
        float3 lightSample = uniforms.lightPos.xyz
            + uniforms.lightU.xyz * jitter.x
            + uniforms.lightV.xyz * jitter.y;

        float3 LVec = lightSample - p;
        float d = length(LVec);
        if (d <= 1e-4) {
            continue;
        }

        float3 L = LVec / d;
        float nDotL = max(dot(n, L), 0.0);
        float lightFacing = max(dot(-L, lightNormal), 0.0);
        if (nDotL <= 0.0 || lightFacing <= 0.0) {
            continue;
        }

        ray shadowRay;
        shadowRay.origin = p + n * 0.001;
        shadowRay.direction = L;
        shadowRay.min_distance = 0.001;
        shadowRay.max_distance = d - 0.01;

        auto shadowHit = inter.intersect(shadowRay, accelerationStructure, 0xFF);
        if (shadowHit.type != intersection_type::none) {
            continue;
        }

        float attenuation = 1.0 / (1.0 + 0.18 * d * d);
        outRadiance += surfaceAlbedo * (nDotL * lightFacing * attenuation * kLightIntensity / float(sampleCount));
    }

    return outRadiance;
}

vertex VSOut vertexShader(VertexIn in [[stage_in]],
                          constant Uniforms &uniforms [[buffer(1)]]) {
    VSOut out;
    out.position = uniforms.mvp * float4(in.position, 1.0);
    out.worldPos = in.position;
    out.normal = normalize(in.normal);
    return out;
}

fragment GBufferOut fragmentShader(VSOut in [[stage_in]],
                                   constant Material &material [[buffer(0)]],
                                   constant Uniforms &uniforms [[buffer(1)]],
                                   texture2d<float> rtOutput [[texture(0)]],
                                   texture2d<float> rtVisibility [[texture(1)]]) {
    bool rtEnabled = (uniforms.tuning.w > 0.5);
    bool reflectionsOnly = (uniforms.tuning.w > 1.5);

    uint2 tid = uint2(in.position.xy);
    float4 rtSample = float4(0.0);
    float4 rtVisibilitySample = float4(0.0);
    if (rtEnabled) {
        rtSample = rtOutput.read(tid);
        rtVisibilitySample = rtVisibility.read(tid);
    }

    if (reflectionsOnly) {
        GBufferOut out;
        out.color = float4(rtSample.rgb, material.opacity);
        out.worldPos = float4(in.worldPos, material.reflection);
        out.normal = float4(normalize(in.normal), material.ior);
        return out;
    }

    float3 n = normalize(in.normal);

    // Render emissive light mesh directly to avoid self-lighting singularities.
    float emissiveStrength = max(material.emissive.x, max(material.emissive.y, material.emissive.z));
    if (emissiveStrength > 0.001) {
        float3 lightColor = ToneMapACES(material.emissive.xyz * 8.0 * max(uniforms.tuning.z, 0.01));
        GBufferOut out;
        out.color = float4(lightColor, material.opacity);
        out.worldPos = float4(in.worldPos, material.reflection);
        out.normal = float4(n, material.ior);
        return out;
    }

    float3 V = normalize(uniforms.cameraPos.xyz - in.worldPos);
    float3 lightNormal = normalize(uniforms.lightNormal.xyz);
    constexpr float kLightIntensity = 9.0;

    float3 ambient = material.diffuse.xyz * 0.12;
    float roughnessFactor = rtEnabled ? clamp(1.0 - uniforms.tuning.y, 0.1, 2.0) : 1.0;
    float shininess = max(material.shininess * roughnessFactor, 12.0);
    shininess = min(shininess, 64.0);
    float metallic = rtEnabled ? clamp(uniforms.tuning.x, -1.0, 1.0) : 0.0;
    float3 directDiffuse = float3(0.0);
    float3 directSpecular = float3(0.0);
    constexpr float2 lightOffsets[4] = {
        float2(-0.5, -0.5),
        float2( 0.5, -0.5),
        float2(-0.5,  0.5),
        float2( 0.5,  0.5)
    };
    constexpr float kMinLightDist = 0.08;
    for (uint i = 0; i < 4; ++i) {
        float3 lightSample = uniforms.lightPos.xyz
            + uniforms.lightU.xyz * lightOffsets[i].x
            + uniforms.lightV.xyz * lightOffsets[i].y;
        float3 L_vec = lightSample - in.worldPos;
        float d = max(length(L_vec), kMinLightDist);
        float3 L = L_vec / d;
        float lightFacing = max(dot(-L, lightNormal), 0.0);
        float nDotL = max(dot(n, L), 0.0);
        if (lightFacing <= 0.0 || nDotL <= 0.0) {
            continue;
        }
        float attenuation = 1.0 / (1.0 + 0.18 * d * d);
        directDiffuse += material.diffuse.xyz * (nDotL * lightFacing * attenuation * kLightIntensity * 0.25);
        if (!rtEnabled) {
            float3 H = normalize(L + V);
            float spec = pow(max(dot(n, H), 0.0), shininess);
            directSpecular += material.specular.xyz * (spec * attenuation * (0.5 + metallic) * 0.25);
        }
    }

    float3 directColor = ambient + directDiffuse + directSpecular;
    directColor += material.emissive.xyz * 8.0;

    if (rtEnabled) {
        float visibility = clamp(1.0 - rtVisibilitySample.a, 0.0, 1.0);
        visibility = pow(visibility, 1.2);
        float3 directNoAmbientNoEmissive = directColor - ambient - (material.emissive.xyz * 8.0);
        directColor = ambient + (material.emissive.xyz * 8.0) + (directNoAmbientNoEmissive * visibility);
        directColor += rtSample.rgb;
    }

    float3 mappedColor = ToneMapACES(directColor * max(uniforms.tuning.z, 0.01));

    GBufferOut out;
    out.color = float4(mappedColor, material.opacity);
    out.worldPos = float4(in.worldPos, material.reflection);
    out.normal = float4(n, material.ior);
    return out;
}

kernel void rtMain(texture2d<float, access::write> outImage [[texture(0)]],
                   texture2d<float> positions [[texture(1)]],
                   texture2d<float> normals [[texture(2)]],
                   constant Uniforms &uniforms [[buffer(1)]],
                   instance_acceleration_structure accelerationStructure [[buffer(0)]],
                   device const float3 *materialDiffuse [[buffer(2)]],
                   constant uint &materialCount [[buffer(3)]],
                   device const uint3 *triangleIndices [[buffer(4)]],
                   device const uint *geometryTriangleBase [[buffer(5)]],
                   device const RTTriangleVertex *vertexData [[buffer(6)]],
                   uint2 tid [[thread_position_in_grid]]) {
    if (tid.x >= outImage.get_width() || tid.y >= outImage.get_height()) {
        return;
    }

    float4 posPack = positions.read(tid);
    if (all(posPack.xyz == 0.0) && posPack.w == 0.0) {
        outImage.write(float4(0.0), tid);
        return;
    }

    float3 hitPos = posPack.xyz;
    float reflectionCoeff = saturate(posPack.w);

    float4 normalPack = normals.read(tid);
    float3 N = normalize(normalPack.xyz);
    float ior = max(normalPack.w, 1.0);
    bool refractive = (ior > 1.01);

    float3 V = normalize(uniforms.cameraPos.xyz - hitPos);
    intersector<instancing, triangle_data> inter;

    uint seed = Hash32((tid.x + 1U) * 9781U ^ (tid.y + 1U) * 6271U);
    constexpr uint kPrimaryShadowSamples = 20;
    constexpr uint kSecondarySamples = 6;

    float visibility = 0.0;
    float3 accumulatedColor = float3(0.0);
    float3 lightNormal = normalize(uniforms.lightNormal.xyz);

    // Primary direct visibility and highlight term.
    for (uint i = 0; i < kPrimaryShadowSamples; ++i) {
        float2 jitter = float2(Rand01(seed), Rand01(seed)) * 2.0 - 1.0;
        float3 lightSample = uniforms.lightPos.xyz
            + uniforms.lightU.xyz * jitter.x
            + uniforms.lightV.xyz * jitter.y;

        float3 LVec = lightSample - hitPos;
        float d = length(LVec);
        if (d <= 1e-4) {
            continue;
        }

        float3 L = LVec / d;
        float nDotL = max(dot(N, L), 0.0);
        float lightFacing = max(dot(-L, lightNormal), 0.0);
        if (nDotL <= 0.0 || lightFacing <= 0.0) {
            continue;
        }

        ray shadowRay;
        shadowRay.origin = hitPos + N * 0.001;
        shadowRay.direction = L;
        shadowRay.min_distance = 0.001;
        shadowRay.max_distance = d - 0.01;

        auto shadowHit = inter.intersect(shadowRay, accelerationStructure, 0xFF);
        if (shadowHit.type == intersection_type::none) {
            visibility += 1.0 / float(kPrimaryShadowSamples);
            float attenuation = 1.0 / (1.0 + 0.18 * d * d);
            float3 H = normalize(L + V);
            float spec = pow(max(dot(N, H), 0.0), 128.0);
            accumulatedColor += float3(spec * nDotL * lightFacing * attenuation * 0.08 / float(kPrimaryShadowSamples));
        } else {
            // Small transmission keeps penumbra from collapsing to hard black.
            visibility += 0.08 / float(kPrimaryShadowSamples);
        }
    }

    // Secondary diffuse rays: simulate one-bounce indirect illumination.
    float3 tangent;
    float3 bitangent;
    BuildBasis(N, tangent, bitangent);
    for (uint i = 0; i < kSecondarySamples; ++i) {
        float3 localDir = SampleCosineWeightedHemisphere(float2(Rand01(seed), Rand01(seed)));
        float3 bounceDir = normalize(localDir.x * tangent + localDir.y * N + localDir.z * bitangent);

        ray bounceRay;
        bounceRay.origin = hitPos + N * 0.001;
        bounceRay.direction = bounceDir;
        bounceRay.min_distance = 0.001;
        bounceRay.max_distance = 1000.0;

        auto bounceHit = inter.intersect(bounceRay, accelerationStructure, 0xFF);
        if (bounceHit.type == intersection_type::none) {
            continue;
        }

        float3 bouncePos = bounceRay.origin + bounceRay.direction * bounceHit.distance;
        float3 bounceNormal = ResolveHitNormal(bounceHit, bounceRay.direction, triangleIndices, geometryTriangleBase, vertexData, materialCount);
        float3 bounceAlbedo = ResolveGeometryAlbedo(bounceHit.geometry_id, materialDiffuse, materialCount);
        float3 bounceLight = EvaluateDirectAtPoint(bouncePos, bounceNormal, bounceAlbedo, uniforms, inter, accelerationStructure, seed, 2);
        float nDotWi = max(dot(N, bounceDir), 0.0);
        accumulatedColor += bounceLight * (nDotWi / float(kSecondarySamples) * 0.22);
    }

    // Reflection for metallic/reflective surfaces.
    if (reflectionCoeff > 0.0) {
        float3 R = normalize(reflect(-V, N));
        ray reflRay;
        reflRay.origin = hitPos + N * 0.001;
        reflRay.direction = R;
        reflRay.min_distance = 0.001;
        reflRay.max_distance = 1000.0;

        auto reflHit = inter.intersect(reflRay, accelerationStructure, 0xFF);
        if (reflHit.type != intersection_type::none) {
            float3 reflPos = reflRay.origin + reflRay.direction * reflHit.distance;
            float3 reflNormal = ResolveHitNormal(reflHit, reflRay.direction, triangleIndices, geometryTriangleBase, vertexData, materialCount);
            float3 reflTint = ResolveGeometryAlbedo(reflHit.geometry_id, materialDiffuse, materialCount);
            float3 reflDirect = EvaluateDirectAtPoint(reflPos, reflNormal, reflTint, uniforms, inter, accelerationStructure, seed, 3);
            accumulatedColor += (reflDirect + reflTint * 0.14) * reflectionCoeff;
        }
    }

    // Refraction for transparent surfaces.
    if (refractive) {
        float3 I = normalize(-V);
        float3 refractN = N;
        bool inside = dot(I, refractN) > 0.0;
        if (inside) {
            refractN = -refractN;
        }
        float eta = inside ? ior : (1.0 / ior);
        float3 T = refract(I, refractN, eta);
        if (length_squared(T) < 1e-6) {
            T = normalize(reflect(I, refractN));
        }

        ray refrRay;
        refrRay.origin = hitPos + T * 0.001;
        refrRay.direction = normalize(T);
        refrRay.min_distance = 0.001;
        refrRay.max_distance = 1000.0;

        auto refrHit = inter.intersect(refrRay, accelerationStructure, 0xFF);
        if (refrHit.type != intersection_type::none) {
            float3 refrPos = refrRay.origin + refrRay.direction * refrHit.distance;
            float3 refrNormal = ResolveHitNormal(refrHit, refrRay.direction, triangleIndices, geometryTriangleBase, vertexData, materialCount);
            float3 transmittance = ResolveGeometryAlbedo(refrHit.geometry_id, materialDiffuse, materialCount);
            float3 refrDirect = EvaluateDirectAtPoint(refrPos, refrNormal, transmittance, uniforms, inter, accelerationStructure, seed, 3);

            float f0 = pow((ior - 1.0) / (ior + 1.0), 2.0);
            float fresnel = f0 + (1.0 - f0) * pow(1.0 - saturate(dot(N, V)), 5.0);
            accumulatedColor += refrDirect * (1.0 - fresnel) * 0.85 + transmittance * (1.0 - fresnel) * 0.10;
        }
    }

    float shadowFactor = clamp(1.0 - visibility, 0.0, 1.0);
    outImage.write(float4(accumulatedColor, shadowFactor), tid);
}

vertex VSOut vertexAxes(VertexIn in [[stage_in]],
                        constant float4x4 &mvp [[buffer(1)]]) {
    VSOut out;
    out.position = mvp * float4(in.position, 1.0);
    out.normal = in.normal;
    return out;
}

fragment float4 fragmentAxes(VSOut in [[stage_in]]) {
    return float4(in.normal, 1.0);
}
