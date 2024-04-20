#version 450

layout(set = 0, binding = 0) uniform ViewLevelUniform {
	mat4 projection;
	mat4 view;
	vec4 viewPos;
} viewLevelUniform;

layout(set = 1, binding = 0) uniform ObjectLevelUniform {
	mat4 model;
	mat4 normal;
} objectLevelUniform;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inColor;

layout(location = 0) out vec3 outPosition; // In view space
layout(location = 1) out vec3 outNormal; // In view space
layout(location = 2) out vec4 outTangent; // In view space
layout(location = 3) out vec2 outTexCoord;


void main() {
	outPosition = vec3(viewLevelUniform.view * objectLevelUniform.model * vec4(inPosition, 1.0));
	gl_Position = viewLevelUniform.projection * vec4(outPosition, 1.0);
	outNormal = normalize(mat3(viewLevelUniform.view) * mat3(objectLevelUniform.normal) * inNormal);
	outTangent = vec4(normalize(mat3(viewLevelUniform.view) * mat3(objectLevelUniform.normal) * inTangent.xyz), inTangent.w);
	outTexCoord = inTexCoord;
}