#version 450

layout(push_constant) uniform SpotLightShadowMapUniform {
	mat4 perspective;
} spotLightShadowMapUniform;

layout(set = 0, binding = 0) uniform ObjectLevelUniform {
	mat4 model;
	mat4 normal;
} objectLevelUniform;

layout(location = 0) in vec3 inPosition;

void main() {
	vec3 position = vec3(objectLevelUniform.model * vec4(inPosition, 1.0));
	gl_Position = spotLightShadowMapUniform.perspective * vec4(position, 1.0);
	position.z += 0.05;
	gl_Position.z = (spotLightShadowMapUniform.perspective * vec4(position, 1.0)).z;
}