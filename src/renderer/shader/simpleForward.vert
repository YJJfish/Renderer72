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
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outColor;

void main() {
	outPosition = vec3(objectLevelUniform.model * vec4(inPosition, 1.0));
	gl_Position = viewLevelUniform.projection * viewLevelUniform.view * vec4(outPosition, 1.0);
	outNormal = mat3(objectLevelUniform.normal) * inNormal;
	outColor = inColor;
}