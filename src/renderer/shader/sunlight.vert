#version 450

layout(set = 0, binding = 0) uniform ObjectLevelUniform {
	mat4 model;
	mat4 normal;
} objectLevelUniform;

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 outPosition;

void main() {
	outPosition = vec3(objectLevelUniform.model * vec4(inPosition, 1.0));
}