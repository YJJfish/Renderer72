#version 450

#define PI 3.1415926535897932384626433832795
#define DIELECTRIC_SPECULAR 0.04

layout(set = 0, binding = 0) uniform ViewLevelUniform {
	mat4 projection;
	mat4 view;
	vec4 viewPos;
} viewLevelUniform;

layout (set = 2, binding = 0) uniform sampler2D normalMapSampler;
layout (set = 2, binding = 1) uniform sampler2D displacementMapSampler;
layout (set = 2, binding = 2) uniform sampler2D albedoSampler;
layout (set = 2, binding = 3) uniform sampler2D roughnessSampler;
layout (set = 2, binding = 4) uniform sampler2D metalnessSampler;

layout (set = 3, binding = 0) uniform SkyboxUniform {
	mat4 model;
} skyboxUniform;
layout (set = 3, binding = 1) uniform samplerCube skyboxRadianceSampler;
layout (set = 3, binding = 2) uniform samplerCube skyboxLambertianSampler;
layout (set = 3, binding = 3) uniform sampler2D environmentBRDFSampler;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

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
    vec3 normal = normalize(TBN * (texture(normalMapSampler, texCoord).xyz * 2.0 - 1.0));
    vec4 albedo = texture(albedoSampler, texCoord);
    float roughness = texture(roughnessSampler, texCoord).x;
    float metalness = texture(metalnessSampler, texCoord).x;

    vec3 lightDir = reflect(-viewDir, normal);
    vec3 H = normalize(lightDir + viewDir);
    float NoV = clamp(dot(normal, viewDir), 0.0, 1.0);

    vec3 F0 = mix(vec3(DIELECTRIC_SPECULAR), albedo.rgb, metalness);
    vec3 F = fresnelSchlickRoughness(NoV, F0, roughness);
    vec3 kD = (1.0 - F) * (1.0 - metalness);

    vec2 Fab = texture(environmentBRDFSampler, vec2(NoV, roughness)).xy;
    vec3 radiance = textureLod(skyboxRadianceSampler, mat3(skyboxUniform.model) * lightDir, roughness * textureQueryLevels(skyboxRadianceSampler)).rgb;
    vec3 irradiance = texture(skyboxLambertianSampler, mat3(skyboxUniform.model) * normal).rgb;

    outColor = vec4((F * Fab.x + Fab.y) * radiance + kD * albedo.rgb * irradiance / PI, albedo.a);

    outColor.rgb = outColor.rgb / (outColor.rgb + vec3(1.0));
}