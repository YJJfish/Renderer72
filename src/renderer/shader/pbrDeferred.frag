#version 450

layout (set = 2, binding = 0) uniform sampler2D normalMapSampler;
layout (set = 2, binding = 1) uniform sampler2D displacementMapSampler;
layout (set = 2, binding = 2) uniform sampler2D albedoSampler;
layout (set = 2, binding = 3) uniform sampler2D roughnessSampler;
layout (set = 2, binding = 4) uniform sampler2D metalnessSampler;

layout(location = 0) in vec3 inPosition; // In view space
layout(location = 1) in vec3 inNormal; // In view space
layout(location = 2) in vec4 inTangent; // In view space
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec4 outPositionDepth; // R32G32B32A32Sfloat: Position + Depth
layout(location = 1) out vec4 outNormal; // R8G8B8A8Unorm: Normal
layout(location = 2) out vec4 outAlbedo; // R8G8B8A8Unorm: Albedo
layout(location = 3) out vec4 outPbr; // R8G8B8A8Unorm: AO + Roughness + Metalness

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
    mat3 TBN = mat3(T, B, N); // tangent space -> view space
    vec3 viewDir = normalize(-inPosition);
    vec3 tangentViewDir = normalize(transpose(TBN) * viewDir);
    vec2 texCoord = parallaxOcclusionMapping(inTexCoord, tangentViewDir);
    
    vec3 normal = normalize(TBN * (texture(normalMapSampler, texCoord).xyz * 2.0 - 1.0));
    vec4 albedo = texture(albedoSampler, texCoord);
    float roughness = texture(roughnessSampler, texCoord).x;
    float metalness = texture(metalnessSampler, texCoord).x;

	outPositionDepth = vec4(inPosition, gl_FragCoord.z);
	outNormal = vec4(normal * 0.5 + 0.5, 1.0);
	outAlbedo = albedo;
	outPbr = vec4(1.0, roughness, metalness, 1.0);
	
}