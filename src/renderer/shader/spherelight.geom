#version 450

layout (triangles) in;
layout (triangle_strip, max_vertices=18) out;

layout(push_constant) uniform SphereLightShadowMapUniform {
	vec3 position;
	float radius;
	float limit;
} sphereLightShadowMapUniform;

mat3[6] views = mat3[](
	mat3(1.0),
	mat3(1.0),
	mat3(1.0),
	mat3(1.0),
	mat3(1.0),
	mat3(1.0)
);

layout (location = 0) in vec3 inPosition[];
layout (location = 0) out vec3 outPosition;

void main(void) {
	float zFar = sphereLightShadowMapUniform.limit;
	float zNear = sphereLightShadowMapUniform.radius / sqrt(2.0);
	mat4 perspective = mat4(
		1.0, 0.0, 0.0, 0.0,
		0.0, 1.0, 0.0, 0.0,
		0.0, 0.0, zFar / (zFar - zNear), 1.0,
		0.0, 0.0, -(zFar * zNear) / (zFar - zNear), 0.0
	);
	for (int face = 0; face < 6; ++face) {
		gl_Layer = face;
		for(int i = 0; i < 3; ++i) {
			outPosition = inPosition[i];
			gl_Position = perspective * vec4(views[face] * (outPosition - sphereLightShadowMapUniform.position), 1.0);
			EmitVertex();
		}
		EndPrimitive();
	}
}