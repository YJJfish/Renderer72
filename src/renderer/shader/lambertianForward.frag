#version 450

#define PI 3.1415926535897932384626433832795

layout(set = 0, binding = 0) uniform ViewLevelUniform {
	mat4 projection;
	mat4 view;
	vec4 viewPos;
} viewLevelUniform;

struct SunLight {
	float cascadeSplits[4];
	mat4 orthographic[4]; // Project points in world space to texture uv
	vec3 direction; // Object to light, in world space
	float angle;
	vec3 tint; // Already multiplied by strength
	int shadow; // Shadow map size
};

struct SphereLight {
	vec3 position; // In world space
	float radius;
	vec3 tint; // Already multiplied by power
	float limit;
};

struct SpotLight {
	mat4 perspective; // Project points in world space to texture uv
	vec3 position; // In world space
	float radius;
	vec3 direction; // Object to light, in world space
	float fov;
	vec3 tint; // Already multiplied by power
	float blend;
	float limit;
	int shadow; // Shadow map size
};

layout(set = 0, binding = 1) readonly buffer Lights {

	int numSunLights;
	int numSunLightsNoShadow;
	int numSphereLights;
	int numSphereLightsNoShadow;
	int numSpotLights;
	int numSpotLightsNoShadow;

	SunLight sunLights[1];
	SunLight sunLightsNoShadow[16];
	SphereLight sphereLights[4];
	SphereLight sphereLightsNoShadow[1024];
	SpotLight spotLights[4];
	SpotLight spotLightsNoShadow[1024];

} lights;

layout(set = 0, binding = 2) uniform sampler shadowMapSampler;
layout(set = 0, binding = 3) uniform texture2D spotLightShadowMaps[4];
layout(set = 0, binding = 4) uniform textureCube sphereLightShadowMaps[4];
layout(set = 0, binding = 5) uniform texture2DArray sunLightShadowMaps[1];

layout (set = 2, binding = 0) uniform sampler2D normalMapSampler;
layout (set = 2, binding = 1) uniform sampler2D displacementMapSampler;
layout (set = 2, binding = 2) uniform sampler2D albedoSampler;

layout (set = 3, binding = 0) uniform SkyboxUniform {
	mat4 model;
} skyboxUniform;
layout (set = 3, binding = 2) uniform samplerCube skyboxLambertianSampler;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

vec2 parallaxOcclusionMapping(vec2 uv, vec3 tangentViewDir) {
    const float heightScale = 0.05;
    const float minLayers = 8;
    const float maxLayers = 32;
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

float pow2(float x){
	return x * x;
}

float pow4(float x){
	return x * x * x * x;
}

vec3 computeSpotLight(SpotLight spotLight, vec3 fragPosition, vec3 fragNormal, vec3 albedo) {
	float distance = length(spotLight.position - fragPosition);
	if (distance >= spotLight.limit)
		return vec3(0.0);
	distance = max(distance, spotLight.radius);
	vec3 lightDir = normalize(spotLight.position - fragPosition);
	float NoL = dot(lightDir, fragNormal);
	if (NoL <= 0.0)
		return vec3(0.0);
	float phi = acos(dot(lightDir, spotLight.direction));
	if (phi >= spotLight.fov / 2.0)
		return vec3(0.0);
	float blend = min(1.0, (spotLight.fov / 2.0 - phi) / (spotLight.fov * spotLight.blend / 2.0));
	vec3 lightIntensity = blend * spotLight.tint / (4.0 * PI * pow2(distance)) * (1.0 - pow4(distance / spotLight.limit));
	// Lambertian
	return NoL * lightIntensity * albedo;
}

float computeSpotLightShadow(SpotLight spotLight, vec3 fragPosition, sampler shadowMapSampler, texture2D shadowMapTexture){
	vec3 lightToFrag = spotLight.position - fragPosition;
	float distance = length(lightToFrag);
	if (distance <= spotLight.radius)
		return 1.0;
	if (distance >= spotLight.limit)
		return 0.0;
	vec4 projectToLight = spotLight.perspective * vec4(fragPosition, 1.0);
	projectToLight.xyz /= projectToLight.w;
	projectToLight.xy = projectToLight.xy * 0.5 + 0.5;
	// PCF
	float dx = 1.05 / float(spotLight.shadow);
	int count = 0;
	for (int x = -2; x <= 2; ++x) {
		for (int y = -2; y <= 2; ++y) {
			float closestDepth = texture(sampler2D(shadowMapTexture, shadowMapSampler), projectToLight.xy + vec2(x, y) * dx).r;
			if (projectToLight.z <= closestDepth)
				count += 1;
		}
	}
	return float(count) / 25.0;
}

vec3 computeSphereLight(SphereLight sphereLight, vec3 fragPosition, vec3 fragNormal, vec3 albedo) {
	float distance = length(sphereLight.position - fragPosition);
	if (distance >= sphereLight.limit)
		return vec3(0.0);
	distance = max(distance, sphereLight.radius);
	vec3 lightDir = normalize(sphereLight.position - fragPosition);
	float NoL = dot(lightDir, fragNormal);
	if (NoL <= 0.0)
		return vec3(0.0);
	vec3 lightIntensity = sphereLight.tint / (4.0 * PI * pow2(distance)) * (1.0 - pow4(distance / sphereLight.limit));
	// Lambertian
	return NoL * lightIntensity * albedo;
}

/* Reference: https://learnopengl.com/Advanced-Lighting/Shadows/Point-Shadows
 * The PCF filtering of omnidirectional shadow mapping is slightly different from
 * that of perspective shadow mapping.
 * We want to sample rays with different directions and use them to fetch texels in the cubemap.
 */
vec3 sphereLightShadowMapPcfDirections[20] = vec3[]
(
   vec3( 1,  1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1,  1,  1), 
   vec3( 1,  1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
   vec3( 1,  1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1,  1,  0),
   vec3( 1,  0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1,  0, -1),
   vec3( 0,  1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0,  1, -1)
);
float computeSphereLightShadow(SphereLight sphereLight, vec3 fragPosition, sampler shadowMapSampler, textureCube shadowMapTexture){
	vec3 lightToFrag = fragPosition - sphereLight.position;
	float distance = length(lightToFrag);
	if (distance <= sphereLight.radius)
		return 1.0;
	if (distance >= sphereLight.limit)
		return 0.0;
	lightToFrag = normalize(lightToFrag);
	// PCF
	int count = 0;
	for (int i = 0; i < 20; ++i) {
		float closestDepth = texture(samplerCube(shadowMapTexture, shadowMapSampler), lightToFrag + sphereLightShadowMapPcfDirections[i] * 0.0005).r;
		closestDepth = sphereLight.radius + (sphereLight.limit - sphereLight.radius) * closestDepth;
		if (distance <= closestDepth)
			count += 1;
	}
	return float(count) / 20.0;
}

vec3 computeSunLight(SunLight sunLight, vec3 fragPosition, vec3 fragNormal, vec3 albedo) {
	float theta = acos(dot(sunLight.direction, fragNormal)) - sunLight.angle / 2.0;
	if (theta >= PI / 2.0)
		return vec3(0.0);
	theta = max(theta, 0.0);
	float NoL = cos(theta);
	vec3 lightIntensity = sunLight.tint;
	// Lambertian
	return NoL * lightIntensity * albedo;
}

float computeSunLightShadow(SunLight sunLight, vec3 fragPosition, sampler shadowMapSampler, texture2DArray shadowMapTexture){
	vec3 viewSpaceFragPosition = vec3(viewLevelUniform.view * vec4(fragPosition, 1.0));
	int cascadeIndex = 0;
	for(int i = 0; i < 4; ++i)
		if(viewSpaceFragPosition.z < sunLight.cascadeSplits[i]) {	
			cascadeIndex = i;
			break;
		}
	vec4 projectToLight = sunLight.orthographic[cascadeIndex] * vec4(fragPosition, 1.0);
	projectToLight.xyz /= projectToLight.w;
	projectToLight.xy = projectToLight.xy * 0.5 + 0.5;
	// PCF
	float dx = 1.05 / float(sunLight.shadow);
	int count = 0;
	for (int x = -2; x <= 2; ++x) {
		for (int y = -2; y <= 2; ++y) {
			float closestDepth = texture(sampler2DArray(shadowMapTexture, shadowMapSampler), vec3(projectToLight.xy + vec2(x, y) * dx, cascadeIndex)).r;
			if (projectToLight.z <= closestDepth)
				count += 1;
		}
	}
	return float(count) / 25.0;
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

	outColor = vec4(0.0, 0.0, 0.0, albedo.a);

	// Sun light
	for (int i = 0; i < lights.numSunLightsNoShadow; ++i) {
		outColor.rgb += computeSunLight(lights.sunLightsNoShadow[i], inPosition, normal, albedo.rgb);
	}
	for (int i = 0; i < lights.numSunLights; ++i) {
		float shadow = computeSunLightShadow(lights.sunLights[i], inPosition, shadowMapSampler, sunLightShadowMaps[i]);
		outColor.rgb += shadow * computeSunLight(lights.sunLights[i], inPosition, normal, albedo.rgb);
	}

	// Sphere light
	for (int i = 0; i < lights.numSphereLightsNoShadow; ++i) {
		outColor.rgb += computeSphereLight(lights.sphereLightsNoShadow[i], inPosition, normal, albedo.rgb);
	}
	for (int i = 0; i < lights.numSphereLights; ++i) {
		float shadow = computeSphereLightShadow(lights.sphereLights[i], inPosition, shadowMapSampler, sphereLightShadowMaps[i]);
		outColor.rgb += shadow * computeSphereLight(lights.sphereLights[i], inPosition, normal, albedo.rgb);
	}

	// Spot light
	for (int i = 0; i < lights.numSpotLightsNoShadow; ++i) {
		outColor.rgb += computeSpotLight(lights.spotLightsNoShadow[i], inPosition, normal, albedo.rgb);
	}
	for (int i = 0; i < lights.numSpotLights; ++i) {
		float shadow = computeSpotLightShadow(lights.spotLights[i], inPosition, shadowMapSampler, spotLightShadowMaps[i]);
		outColor.rgb += shadow * computeSpotLight(lights.spotLights[i], inPosition, normal, albedo.rgb);
	}

	// Environment lighting
    vec3 envLight = textureLod(skyboxLambertianSampler, mat3(skyboxUniform.model) * normal, 0.0).rgb;
    outColor.rgb += envLight * albedo.rgb / PI;

	// Tone mapping
	outColor.rgb = outColor.rgb / (outColor.rgb + vec3(1.0));

	
	/*vec3 mask = vec3(0.0);
	vec3 viewSpacePosition = vec3(viewLevelUniform.view * vec4(inPosition, 1.0));
	if (viewSpacePosition.z <= lights.sunLights[0].cascadeSplits[0])
		mask = vec3(1.0, 0.6, 0.6);
	else if (viewSpacePosition.z <= lights.sunLights[0].cascadeSplits[1])
		mask = vec3(0.6, 1.0, 0.6);
	else if (viewSpacePosition.z <= lights.sunLights[0].cascadeSplits[2])
		mask = vec3(0.6, 0.6, 1.0);
	else if (viewSpacePosition.z <= lights.sunLights[0].cascadeSplits[3])
		mask = vec3(1.0, 1.0, 0.6);
	else
		mask = vec3(0.6, 0.6, 0.6);
	outColor.rgb = mask * outColor.rgb;*/


}