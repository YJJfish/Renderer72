#version 450

layout (set = 2, binding = 0) uniform sampler2D normalMapSampler;
layout (set = 2, binding = 1) uniform sampler2D displacementMapSampler;
layout (set = 2, binding = 2) uniform sampler2D baseColorSampler;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(inNormal);
    vec3 tangent = normalize(inTangent.xyz);
    vec3 bitangent = cross(normal, tangent);
    if (inTangent.w < 0.0)
        bitangent = -bitangent;
    outColor = vec4(texture(baseColorSampler, inTexCoord), 1.0);
}