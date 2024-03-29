#version 450

layout(push_constant) uniform SphereLightShadowMapUniform {
	vec3 position;
	float radius;
	mat4 perspective;
	float limit;
} sphereLightShadowMapUniform;

layout(location = 0) in vec3 inPosition;

void main() {
	float distance = length(inPosition - sphereLightShadowMapUniform.position) + 0.05;
	gl_FragDepth = (distance - sphereLightShadowMapUniform.radius) / (sphereLightShadowMapUniform.limit - sphereLightShadowMapUniform.radius);
}