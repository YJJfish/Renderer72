#version 450

layout(set = 0, binding = 0) uniform ViewLevelUniform {
	mat4 projection;
	mat4 view;
    vec4 viewPos;
} viewLevelUniform;

layout (set = 2, binding = 0) uniform sampler2D normalMapSampler;
layout (set = 2, binding = 1) uniform sampler2D displacementMapSampler;

layout (set = 3, binding = 0) uniform SkyboxUniform {
	mat4 model;
} skyboxUniform;
layout (set = 3, binding = 1) uniform samplerCube skyboxRadianceSampler;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

vec2 parallaxOcclusionMapping(vec2 uv, vec3 tangentViewDir) {
    const float heightScale = 0.05;
    const float minLayers = 32;
    const float maxLayers = 128;
    float numLayers = mix(maxLayers, minLayers, abs(tangentViewDir.z));
    float layerDepth = 1.0 / numLayers;
	float currLayerDepth = 0.0;
	vec2 deltaUV = tangentViewDir.xy * heightScale / (tangentViewDir.z * numLayers);
    deltaUV.y = -deltaUV.y;
	vec2 currUV = uv;
	float height = texture(displacementMapSampler, currUV).x;
	while(height > currLayerDepth) {
		currLayerDepth += layerDepth;
		currUV -= deltaUV;
		height = texture(displacementMapSampler, currUV).x;
	}
	vec2 prevUV = currUV + deltaUV;
	float nextDepth = height - currLayerDepth;
	float prevDepth = texture(displacementMapSampler, prevUV).x - currLayerDepth + layerDepth;
	return mix(currUV, prevUV, nextDepth / (nextDepth - prevDepth));
}

void main() {
    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent.xyz);
    vec3 B = normalize(cross(N, T));
    if (inTangent.w < 0.0)
        B = -B;
    mat3 TBN = mat3(T, B, N);
    vec3 viewDir = normalize(vec3(viewLevelUniform.viewPos) - inPosition);
    vec3 tangentViewDir = normalize(transpose(TBN) * viewDir);
    vec2 texCoord = parallaxOcclusionMapping(inTexCoord, tangentViewDir);
    if (texCoord.x < 0.0 || texCoord.x > 1.0 || texCoord.y < 0.0 || texCoord.y > 1.0) {
		discard;
	}
    vec3 normal = TBN * (texture(normalMapSampler, texCoord).xyz * 2.0 - 1.0);
    vec3 reflected = reflect(-viewDir, normal);
    vec3 envLight = textureLod(skyboxRadianceSampler, mat3(skyboxUniform.model) * reflected, 0.0).rgb;
    outColor = vec4(envLight, 1.0);

	outColor.rgb = outColor.rgb / (outColor.rgb + vec3(1.0));
}