#version 450

#include "tonemap.glsl"

layout(set=0, binding=0) uniform World{
    vec3 SKY_DIRECTION;
    vec3 SKY_ENERGY;

    vec3 SUN_DIRECTION;
    vec3 SUN_ENERGY;
    vec4 EYE;  // EYE.w stores exposure
};

// Material flags packing in bits:
// bits 0-1: BRDF type
// bits 2-6: hasAlbedoTex, hasNormalTex, hasRoughnessTex, hasMetalnessTex, hasDisplacementTex
#define MAT_FLAG_BRDF_MASK              0x00000003u
#define MAT_FLAG_HAS_ALBEDO_TEX         0x00000004u
#define MAT_FLAG_HAS_NORMAL_TEX         0x00000008u
#define MAT_FLAG_HAS_ROUGHNESS_TEX      0x00000010u
#define MAT_FLAG_HAS_METALNESS_TEX      0x00000020u
#define MAT_FLAG_HAS_DISPLACEMENT_TEX   0x00000040u

struct Material {
    // Slot 0 (16 bytes)
    vec4 albedo;                    // RGBA color or placeholder
    
    // Slot 1 (16 bytes) - packed scalars
    uint flags;                     // brdf type (bits 0-1) + texture flags (bits 2-6)
    float roughness;                // default roughness when not textured
    float metalness;                // default metalness when not textured
    uint padding_scalar;            // padding to align slot
    
};

layout(set=2, binding=0, std140) readonly buffer SSBO_Materials {
    Material MATERIALS[];
};

// Material flag unpacking helpers
uint brdfType(Material mat) { return mat.flags & MAT_FLAG_BRDF_MASK; }
bool hasAlbedoTex(Material mat) { return (mat.flags & MAT_FLAG_HAS_ALBEDO_TEX) != 0u; }
bool hasNormalTex(Material mat) { return (mat.flags & MAT_FLAG_HAS_NORMAL_TEX) != 0u; }
bool hasRoughnessTex(Material mat) { return (mat.flags & MAT_FLAG_HAS_ROUGHNESS_TEX) != 0u; }
bool hasMetalnessTex(Material mat) { return (mat.flags & MAT_FLAG_HAS_METALNESS_TEX) != 0u; }

layout(set=3, binding=0) uniform sampler2D TEXTURE_Albedo;
layout(set=3, binding=1) uniform sampler2D TEXTURE_Normal;
layout(set=3, binding=2) uniform sampler2D TEXTURE_Displacement;
layout(set=3, binding=3) uniform sampler2D TEXTURE_Roughness;
layout(set=3, binding=4) uniform sampler2D TEXTURE_Metalness;
layout(set=0, binding=1) uniform samplerCube ENVIRONMENT_MAP;
layout(set=0, binding=2) uniform samplerCube ENVIRONMENT_LAMBERTIAN_MAP;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) flat in uint materialId;
layout(location = 4) in vec3 tangent;
layout(location = 5) in float handedness;

layout(location = 0) out vec4 outColor;

#define BRDF_LAMBERTIAN  0x0u
#define BRDF_PBR         0x1u
#define BRDF_MIRROR      0x2u
#define BRDF_ENVIRONMENT 0x3u

void main() {
    Material mat = MATERIALS[materialId];
    
    vec3 n = normalize(normal);
    vec3 ws_tangent = normalize(tangent);
    
    // Sample albedo texture if available
    vec3 baseColor = mat.albedo.rgb;
    if (hasAlbedoTex(mat)) {
        baseColor = texture(TEXTURE_Albedo, texCoord).rgb;
    }
    
    // Sample and apply normal map if available
    if (hasNormalTex(mat)) {
        vec3 normal_sample = texture(TEXTURE_Normal, texCoord).rgb;
        // Scale and bias: [0,1] -> [-1,1]
        vec3 normal_ts = normal_sample * 2.0 - 1.0;
        
        // Construct TBN matrix (tangent space -> world space)
        vec3 bitangent = cross(n, ws_tangent) * handedness;
        mat3 tbn_to_world = mat3(ws_tangent, bitangent, n);
        
        // Transform normal from tangent space to world space
        n = normalize(tbn_to_world * normal_ts);
    }
    
    // Roughness and metalness values available for PBR calculations:
    // - mat.roughness: default value or from TEXTURE_Roughness when hasRoughnessTex(mat)
    // - mat.metalness: default value or from TEXTURE_Metalness when hasMetalnessTex(mat)
    
    if (brdfType(mat) == BRDF_LAMBERTIAN || brdfType(mat) == BRDF_PBR) {
        baseColor = baseColor / 3.1415926; // divide by pi for energy conservation
        vec3 e = SKY_ENERGY * vec3(0.5 * dot(n, SKY_DIRECTION) + 0.5) + 
                 SUN_ENERGY * max(0.0, dot(n, SUN_DIRECTION)) + 
                 texture(ENVIRONMENT_LAMBERTIAN_MAP, n).rgb;
        outColor = vec4(e*baseColor, 1.0);
    }
    else if (brdfType(mat) == BRDF_ENVIRONMENT) {
        // sample environment using world-space normal
        baseColor = texture(ENVIRONMENT_MAP, normalize(n)).rgb;
        outColor = vec4(baseColor, 1.0);
    }
    else if (brdfType(mat) == BRDF_MIRROR) {
        // reflect view vector about normal and sample environment map
        vec3 V = normalize(EYE.xyz - position); // view vector from fragment to eye
        vec3 R = reflect(-V, normalize(n));
        baseColor = texture(ENVIRONMENT_MAP, R).rgb;
        outColor = vec4(baseColor, 1.0);
    }

    // Apply tone mapping (exposure packed in EYE.w)
    #ifdef TONEMAP_LINEAR
        outColor.rgb = tonemapLinear(outColor.rgb, EYE.w);
    #elif defined(TONEMAP_ACES)
        outColor.rgb = tonemapACES(outColor.rgb, EYE.w);
    #endif
}