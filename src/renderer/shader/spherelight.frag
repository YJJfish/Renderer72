#version 450

layout(push_constant) uniform SphereLightShadowMapUniform {
	vec3 position;
	float radius;
	float limit;
} sphereLightShadowMapUniform;

layout(location = 0) in vec3 inPosition;

void main() {
	float distance = length(inPosition - sphereLightShadowMapUniform.position);
	distance = (distance - sphereLightShadowMapUniform.radius) / (sphereLightShadowMapUniform.limit - sphereLightShadowMapUniform.radius);
	gl_FragDepth = distance;
}