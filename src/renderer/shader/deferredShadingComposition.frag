#version 450

#define PI 3.1415926535897932384626433832795
#define DIELECTRIC_SPECULAR 0.04

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

layout(push_constant) uniform DeferredShadingOption {
	int renderingMode;
	int enableSSAO;
} deferredShadingOption;

layout(set = 0, binding = 6) uniform sampler2D gBufferPositionDepth; // R32G32B32A32Sfloat: Position + Depth
layout(set = 0, binding = 7) uniform sampler2D gBufferNormal; // R8G8B8A8Unorm: Normal
layout(set = 0, binding = 8) uniform sampler2D gBufferAlbedo; // R8G8B8A8Unorm: Albedo
layout(set = 0, binding = 9) uniform sampler2D gBufferPbr; // R8G8B8A8Unorm: AO + Roughness + Metalness
layout(set = 0, binding = 10) uniform sampler2D ssao;
layout(set = 0, binding = 11) uniform sampler2D ssaoBlur;

layout (set = 1, binding = 0) uniform SkyboxUniform {
	mat4 model;
} skyboxUniform;
layout (set = 1, binding = 1) uniform samplerCube skyboxRadianceSampler;
layout (set = 1, binding = 2) uniform samplerCube skyboxLambertianSampler;
layout (set = 1, binding = 3) uniform sampler2D environmentBRDFSampler;

layout(location = 0) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

float G_SchlicksmithGGX(float NoL, float NoV, float roughness) {
	float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
	float GL = NoL / (NoL * (1.0 - k) + k);
	float GV = NoV / (NoV * (1.0 - k) + k);
	return GL * GV;
}

float D_GGX(float NoH, float roughness) {
	float alpha = roughness * roughness;
	float alpha2 = alpha * alpha;
	float denom = NoH * NoH * (alpha2 - 1.0) + 1.0;
	return alpha2 / (PI * denom * denom); 
}

float pow2(float x){
	return x * x;
}

float pow4(float x){
	return x * x * x * x;
}

vec3 computeSpotLight(SpotLight spotLight, vec3 fragPosition, vec3 fragNormal, vec3 viewDir, vec3 F0, vec3 albedo, float roughness, float metalness) {
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
	// Pbr
	// Reference: https://github.com/SaschaWillems/Vulkan/blob/master/shaders/glsl/pbrbasic/pbr.frag
	vec3 H = normalize(viewDir + lightDir);
	float NoH = clamp(dot(H, fragNormal), 0.0, 1.0);
	float NoV = clamp(dot(viewDir, fragNormal), 0.01, 1.0);
	float D = D_GGX(NoH, roughness);
	float G = G_SchlicksmithGGX(NoL, NoV, roughness);
	vec3 F_analytic = fresnelSchlick(NoV, F0);
	vec3 specular = D * F_analytic * G / (4.0 * NoL * NoV + 0.001);
	vec3 kD = (vec3(1.0) - F_analytic) * (1.0 - metalness);
	return NoL * lightIntensity * (kD * albedo / PI + specular);
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

vec3 computeSphereLight(SphereLight sphereLight, vec3 fragPosition, vec3 fragNormal, vec3 viewDir, vec3 F0, vec3 albedo, float roughness, float metalness) {
	float distance = length(sphereLight.position - fragPosition);
	if (distance >= sphereLight.limit)
		return vec3(0.0);
	distance = max(distance, sphereLight.radius);
	vec3 lightDir = normalize(sphereLight.position - fragPosition);
	float NoL = dot(lightDir, fragNormal);
	if (NoL <= 0.0)
		return vec3(0.0);
	vec3 lightIntensity = sphereLight.tint / (4.0 * PI * pow2(distance)) * (1.0 - pow4(distance / sphereLight.limit));
	// Pbr
	// Reference: https://github.com/SaschaWillems/Vulkan/blob/master/shaders/glsl/pbrbasic/pbr.frag
	vec3 H = normalize(viewDir + lightDir);
	float NoH = clamp(dot(H, fragNormal), 0.0, 1.0);
	float NoV = clamp(dot(viewDir, fragNormal), 0.01, 1.0);
	float D = D_GGX(NoH, roughness);
	float G = G_SchlicksmithGGX(NoL, NoV, roughness);
	vec3 F_analytic = fresnelSchlick(NoV, F0);
	vec3 specular = D * F_analytic * G / (4.0 * NoL * NoV + 0.001);
	vec3 kD = (vec3(1.0) - F_analytic) * (1.0 - metalness);
	return NoL * lightIntensity * (kD * albedo / PI + specular);
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

vec3 computeSunLight(SunLight sunLight, vec3 fragPosition, vec3 fragNormal, vec3 viewDir, vec3 F0, vec3 albedo, float roughness, float metalness) {
	vec3 lightDir = sunLight.direction;
	float theta = acos(dot(lightDir, fragNormal)) - sunLight.angle / 2.0;
	if (theta >= PI / 2.0)
		return vec3(0.0);
	theta = max(theta, 0.0);
	float NoL = cos(theta);
	vec3 lightIntensity = sunLight.tint;
	// Pbr
	// Reference: https://github.com/SaschaWillems/Vulkan/blob/master/shaders/glsl/pbrbasic/pbr.frag
	vec3 H = normalize(viewDir + lightDir);
	float NoH = clamp(dot(H, fragNormal), 0.0, 1.0);
	float NoV = clamp(dot(viewDir, fragNormal), 0.01, 1.0);
	
	float D = D_GGX(NoH, roughness);
	float G = G_SchlicksmithGGX(NoL, NoV, roughness);
	vec3 F_analytic = fresnelSchlick(NoV, F0);
	vec3 specular = D * F_analytic * G / (4.0 * NoL * NoV + 0.001);
	vec3 kD = (vec3(1.0) - F_analytic) * (1.0 - metalness);
	return NoL * lightIntensity * (kD * albedo / PI + specular);
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

// Extract information in G-buffer and transform to world space.
// Of course we can compute the lighting in view space directly,
// but I don't want to change A3's code.
// So let's compute everything in world space.
void extractGBuffer(
	in vec2 texCoord,
	out vec3 position,
	out float depth,
	out vec3 normal,
	out vec4 albedo,
	out float ao,
	out float roughness,
	out float metalness
) {
	mat4 invView = inverse(viewLevelUniform.view);
	vec4 gPositionDepth = texture(gBufferPositionDepth, texCoord);
	position = gPositionDepth.xyz; position = vec3(invView * vec4(position, 1.0));
	depth = gPositionDepth.w;
	normal = texture(gBufferNormal, texCoord).xyz * 2.0 - 1.0; normal = normalize(mat3(invView) * normal);
	albedo = texture(gBufferAlbedo, texCoord);
	vec3 gPbr = texture(gBufferPbr, texCoord).xyz;
	ao = gPbr.x;
	roughness = gPbr.y;
	metalness = gPbr.z;
}

void main() {
	vec3 position;
	float depth;
	vec3 normal;
	vec4 albedo;
	float ao;
	float roughness;
	float metalness;
	extractGBuffer(
		inTexCoord,
		position,
		depth,
		normal,
		albedo,
		ao,
		roughness,
		metalness
	);
	
	if (depth == 1.0)
		discard;
	
	gl_FragDepth = depth;

	if (deferredShadingOption.renderingMode == 1) { // SSAO
		outColor = vec4(texture(ssao, inTexCoord).rrr, 1.0); return;
	}
	else if (deferredShadingOption.renderingMode == 2) { // SSAO blur
		outColor = vec4(texture(ssaoBlur, inTexCoord).rrr, 1.0); return;
	}
	else if (deferredShadingOption.renderingMode == 3) { // Albedo
		outColor = albedo; return;
	}
	else if (deferredShadingOption.renderingMode == 4) { // Normal
		outColor = vec4(normal * 2.0 + 1.0, 1.0); return;
	}
	else if (deferredShadingOption.renderingMode == 5) { // Depth
		outColor = vec4(vec3(depth), 1.0); return;
	}
	else if (deferredShadingOption.renderingMode == 6) { // Metalness
		outColor = vec4(vec3(metalness), 1.0); return;
	}
	else if (deferredShadingOption.renderingMode == 7) { // Roughness
		outColor = vec4(vec3(roughness), 1.0); return;
	}

	float ssaoSample = texture(ssaoBlur, inTexCoord).r;
	if (deferredShadingOption.enableSSAO == 0)
		ssaoSample = 1.0;

	vec3 viewDir = normalize(vec3(viewLevelUniform.viewPos) - position);
	vec3 lightDir = reflect(-viewDir, normal);
    vec3 H = normalize(lightDir + viewDir);
    float NoV = clamp(dot(normal, viewDir), 0.0, 1.0);
    vec3 F0 = mix(vec3(DIELECTRIC_SPECULAR), albedo.rgb, metalness);

	outColor = vec4(0.0, 0.0, 0.0, albedo.a);

	// Sun light
	for (int i = 0; i < lights.numSunLightsNoShadow; ++i) {
		outColor.rgb += computeSunLight(lights.sunLightsNoShadow[i], position, normal, viewDir, F0, albedo.rgb, roughness, metalness);
	}
	for (int i = 0; i < lights.numSunLights; ++i) {
		float shadow = computeSunLightShadow(lights.sunLights[i], position, shadowMapSampler, sunLightShadowMaps[i]);
		outColor.rgb += shadow * computeSunLight(lights.sunLights[i], position, normal, viewDir, F0, albedo.rgb, roughness, metalness);
	}

	// Sphere light
	for (int i = 0; i < lights.numSphereLightsNoShadow; ++i) {
		outColor.rgb += computeSphereLight(lights.sphereLightsNoShadow[i], position, normal, viewDir, F0, albedo.rgb, roughness, metalness);
	}
	for (int i = 0; i < lights.numSphereLights; ++i) {
		float shadow = computeSphereLightShadow(lights.sphereLights[i], position, shadowMapSampler, sphereLightShadowMaps[i]);
		outColor.rgb += shadow * computeSphereLight(lights.sphereLights[i], position, normal, viewDir, F0, albedo.rgb, roughness, metalness);
	}

	// Spot light
	for (int i = 0; i < lights.numSpotLightsNoShadow; ++i) {
		outColor.rgb += computeSpotLight(lights.spotLightsNoShadow[i], position, normal, viewDir, F0, albedo.rgb, roughness, metalness);
	}
	for (int i = 0; i < lights.numSpotLights; ++i) {
		float shadow = computeSpotLightShadow(lights.spotLights[i], position, shadowMapSampler, spotLightShadowMaps[i]);
		outColor.rgb += shadow * computeSpotLight(lights.spotLights[i], position, normal, viewDir, F0, albedo.rgb, roughness, metalness);
	}

	// Environment lighting
    vec3 F_ibl = fresnelSchlickRoughness(NoV, F0, roughness);
    vec3 kD = (1.0 - F_ibl) * (1.0 - metalness);

    vec2 Fab = texture(environmentBRDFSampler, vec2(NoV, roughness)).xy;
    vec3 radiance = textureLod(skyboxRadianceSampler, mat3(skyboxUniform.model) * lightDir, roughness * textureQueryLevels(skyboxRadianceSampler)).rgb;
    vec3 irradiance = texture(skyboxLambertianSampler, mat3(skyboxUniform.model) * normal).rgb;

    outColor.rgb += ssaoSample * ((F_ibl * Fab.x + Fab.y) * radiance + kD * albedo.rgb * irradiance / PI);

	// Tone mapping
    outColor.rgb = outColor.rgb / (outColor.rgb + vec3(1.0));
}