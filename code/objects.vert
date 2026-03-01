#version 450 // GLSL version 4.5

layout(location = 0) in vec3 Position;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec4 Tangent;      // xyz = tangent direction, w = handedness (±1)
layout(location = 3) in vec2 TexCoord;

struct InstanceData {
    mat4 CLIP_FROM_LOCAL;
    mat4 WORLD_FROM_LOCAL;
    mat4 WORLD_FROM_LOCAL_NORMAL;
    uint MATERIAL_INDEX;
};

layout(set=1, binding=0, std140) readonly buffer SSBO_InstanceData {
    InstanceData INSTANCEDATA[];
};

layout(location = 0) out vec3 position;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 texCoord;
layout(location = 3) flat out uint materialId;
layout(location = 4) out vec3 tangent;     // World-space tangent
layout(location = 5) out float handedness; // Bitangent handedness (±1)

void main() {
    gl_Position = INSTANCEDATA[gl_InstanceIndex].CLIP_FROM_LOCAL * vec4(Position, 1.0);
    position = mat4x3(INSTANCEDATA[gl_InstanceIndex].WORLD_FROM_LOCAL) * vec4(Position, 1.0);
    normal = mat3(INSTANCEDATA[gl_InstanceIndex].WORLD_FROM_LOCAL_NORMAL) * Normal;
    tangent = mat3(INSTANCEDATA[gl_InstanceIndex].WORLD_FROM_LOCAL_NORMAL) * Tangent.xyz;
    texCoord = TexCoord;
    materialId = INSTANCEDATA[gl_InstanceIndex].MATERIAL_INDEX;
    handedness = Tangent.w;
}