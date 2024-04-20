#version 450

layout(set = 0, binding = 0) uniform SSAOParameters {
	vec4 ssaoSamples[256];
} ssaoParameters;

layout(set = 0, binding = 1) uniform sampler2D ssaoNoise;

layout(push_constant) uniform SSAOParametersPushConstant{
	mat4 projection;
	int numSamples; // Must be multiples of 64
	float ssaoSampleRadius;
} ssaoParametersPushConstant;

layout(set = 0, binding = 2) uniform sampler2D gBufferPositionDepth; // R32G32B32A32Sfloat: Position + Depth
layout(set = 0, binding = 3) uniform sampler2D gBufferNormal; // R8G8B8A8Unorm: Normal
layout(set = 0, binding = 4) uniform sampler2D gBufferAlbedo; // R8G8B8A8Unorm: Albedo
layout(set = 0, binding = 5) uniform sampler2D gBufferPbr; // R8G8B8A8Unorm: AO + Roughness + Metalness

layout(location = 0) in vec2 inTexCoord;

layout(location = 0) out float outSSAO;

void main() {

	vec2 noiseTexScale = vec2(textureSize(gBufferPositionDepth, 0)) / vec2(textureSize(ssaoNoise, 0));

	vec3 position = texture(gBufferPositionDepth, inTexCoord).xyz;
	vec3 normal = normalize(texture(gBufferNormal, inTexCoord).xyz * 2.0 - 1.0);

	vec3 noise = normalize(texture(ssaoNoise, inTexCoord * noiseTexScale).xyz);
	
	// Create TBN matrix
	vec3 tangent = normalize(noise - normal * dot(noise, normal));
	vec3 bitangent = normalize(cross(normal, tangent));
	mat3 TBN = mat3(tangent, bitangent, normal);

	// Compute occlusion factor
	// https://learnopengl.com/code_viewer_gh.php?code=src/5.advanced_lighting/9.ssao/9.ssao.fs
	float occlusion = 0.0;
	for(int i = 0; i < ssaoParametersPushConstant.numSamples; ++i) {
		// Compute sample position
		vec3 samplePos = TBN * vec3(ssaoParameters.ssaoSamples[i]);
		samplePos = position + samplePos * ssaoParametersPushConstant.ssaoSampleRadius; 
		
		// Project sample position.
		vec4 projected = ssaoParametersPushConstant.projection * vec4(samplePos, 1.0);
		if (projected.w <= 0) continue;
		projected.xyz /= projected.w;
		projected.xy = projected.xy * 0.5 + 0.5; // transform to range 0.0 - 1.0
		if (projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 || projected.y > 1.0)
			continue;

		// Surface depth from B buffer
		float surfaceDepth = texture(gBufferPositionDepth, projected.xy).z;
		
		// Range check & Accumulate
		float rangeCheck = smoothstep(0.0, 1.0, ssaoParametersPushConstant.ssaoSampleRadius / abs(position.z - surfaceDepth));
		occlusion += (surfaceDepth + 0.01 < samplePos.z ? 1.0 : 0.0) * rangeCheck;		   
	}
	
	outSSAO = 1.0 - (occlusion / float(ssaoParametersPushConstant.numSamples));

}