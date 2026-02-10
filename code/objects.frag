#version 450

layout(set=0, binding=0) uniform World{
    vec3 SKY_DIRECTION;
    vec3 SKY_ENERGY;

    vec3 SUN_DIRECTION;
    vec3 SUN_ENERGY;
};
struct Material {
    vec4 albedo;
    //uint flags; // brdf (2bits) | hasAlbedoTex + TexIndices | roughness | metalness
    uint brdf;
    uint hasAlbedoTex;
    uint albedoTexIndex;

};

layout(set=2, binding=0, std140) readonly buffer SSBO_Materials {
    Material MATERIALS[];
};

layout(set=3, binding=0) uniform sampler2D TEXTURE;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) flat in uint materialId;

layout(location = 0) out vec4 outColor;

#define BRDF_LAMBERTIAN  0x0u
#define BRDF_PBR         0x1u
#define BRDF_MIRROR      0x2u
#define BRDF_ENVIRONMENT 0x3u

void main() {
    vec3 n = normalize(normal);
    vec3 baseColor = vec3(0.0f, 1.0f, 0.0f);
    baseColor = texture(TEXTURE, texCoord).rgb;
    Material mat = MATERIALS[materialId];
    if (mat.brdf == BRDF_LAMBERTIAN) {
        if (mat.hasAlbedoTex != 0) {
            baseColor = texture(TEXTURE, texCoord).rgb;
        } else {
            baseColor = mat.albedo.rgb;
        }
        //vec3 e = SUN_ENERGY * vec3(0.5 * dot(n, SUN_DIRECTION) + 0.5);
        vec3 e = SKY_ENERGY * vec3(0.5 * dot(n, SKY_DIRECTION) + 0.5) + SUN_ENERGY * max(0.0, dot(n, SUN_DIRECTION)); // half lambertien
        outColor = vec4(e*baseColor / 3.1415926  , 1.0);
    }
   

    // if (materialId == 0) {
    //     Material mat = MATERIALS[materialId];
    //     outColor = vec4(mat.albedo.rgb, 1.0);
    // } else {
    //      outColor = vec4(0.0, 1.0, 0.0, 1.0);
    // }
}