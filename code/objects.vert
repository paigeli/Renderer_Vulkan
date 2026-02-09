#version 450 // GLSL version 4.5

layout(location = 0) in vec3 Position;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec4 Tangent;
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

void main() {
    // gl_Position = CLIP_FROM_WORLD * vec4(Position, 1.0);
    // position = Position;
    // normal = Normal;
    // texCoord = TexCoord;
    gl_Position = INSTANCEDATA[gl_InstanceIndex].CLIP_FROM_LOCAL * vec4(Position, 1.0);
    position = mat4x3(INSTANCEDATA[gl_InstanceIndex].WORLD_FROM_LOCAL) * vec4(Position, 1.0);
    normal = mat3(INSTANCEDATA[gl_InstanceIndex].WORLD_FROM_LOCAL_NORMAL) * Normal;
    texCoord = TexCoord;
    materialId = INSTANCEDATA[gl_InstanceIndex].MATERIAL_INDEX;
}