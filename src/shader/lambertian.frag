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
    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent.xyz);
    vec3 B = cross(N, T);
    if (inTangent.w < 0.0)
        B = -B;
    mat3 TBN = mat3(T, B, N);
    vec3 normal = TBN * (texture(normalMapSampler, inTexCoord).xyz * 2.0 - 1.0);
    vec3 light = mix(vec3(0,0,0), vec3(1,1,1), max(dot(normal, vec3(0, 0, -1)), 0.0) * 0.5 + 0.5);
    outColor = vec4(light * texture(baseColorSampler, inTexCoord).rgb, 1.0);
}