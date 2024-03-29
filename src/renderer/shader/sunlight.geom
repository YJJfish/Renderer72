#version 450

layout (triangles) in;
layout (triangle_strip, max_vertices=12) out;

layout(push_constant) uniform SunLightShadowMapUniform {
	//Why not passing 4 mat4 view matrices?
	//Because "push_constant" is only guaranteed to have at least 128 bytes.
	vec3 orthoX;
	vec3 orthoY;
	vec2 center[4];
	float width[4];
	float height[4];
	float zNear[4];
	float zFar[4];
} sunLightShadowMapUniform;

layout (location = 0) in vec3 inPosition[];

void main(void) {
	mat3 view = transpose(mat3(
		sunLightShadowMapUniform.orthoX,
		sunLightShadowMapUniform.orthoY,
		normalize(cross(sunLightShadowMapUniform.orthoX, sunLightShadowMapUniform.orthoY))
	));
	for (int level = 0; level < 4; ++level) {
		gl_Layer = level;
		for(int i = 0; i < 3; ++i) {
			// Rotate to sunlight space
			vec3 position = view * inPosition[i];
			// Translate to i-th cascade level
			position.xy -= sunLightShadowMapUniform.center[level];
			// Scale xy to -1.0~1.0
			position.x *= 2.0 / sunLightShadowMapUniform.width[level];
			position.y *= 2.0 / sunLightShadowMapUniform.height[level];
			// Scale z to 0.0~1.0
			position.z += 0.05;
			position.z = (position.z - sunLightShadowMapUniform.zNear[level]) / (sunLightShadowMapUniform.zFar[level] - sunLightShadowMapUniform.zNear[level]);
			// Finish
			gl_Position = vec4(position, 1.0);
			EmitVertex();
		}
		EndPrimitive();
	}
}