#version 450

layout (set = 1, binding = 0) uniform SkyboxUniform {
	mat4 model;
} skyboxUniform;
layout (set = 1, binding = 1) uniform samplerCube skyboxRadianceSampler;

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec4 outColor;

void main() {
	vec3 envLight = textureLod(skyboxRadianceSampler, mat3(skyboxUniform.model) * inPosition, 0.0).rgb;
	outColor = vec4(envLight, 1.0);
	
	outColor.rgb = outColor.rgb / (outColor.rgb + vec3(1.0));
}