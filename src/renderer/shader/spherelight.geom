#version 450

layout (triangles) in;
layout (triangle_strip, max_vertices=18) out;

layout(push_constant) uniform SphereLightShadowMapUniform {
	vec3 position;
	float radius;
	mat4 perspective;
	float limit;
} sphereLightShadowMapUniform;

// views[t] transforms a vector pointing to the +Z face to one pointing to another face
mat3 views[6] = mat3[](
	mat3(
		0,  0,  -1,
		0,  1,  0,
		1,  0,  0
	),
	mat3(
		0,  0,  1,
		0,  1,  0,
		-1,  0,  0
	),
	mat3(
		1,  0,  0,
		0,  0,  -1,
		0,  1,  0
	),
	mat3(
		1,  0,  0,
		0,  0,  1,
		0,  -1,  0
	),
	mat3(
		1,  0,  0,
		0,  1,  0,
		0,  0,  1
	),
	mat3(
		-1,  0,  0,
		0,  1,  0,
		0,  0,  -1
	)
);

layout (location = 0) in vec3 inPosition[];
layout (location = 0) out vec3 outPosition;

void main(void) {
	for (int face = 0; face < 6; ++face) {
		gl_Layer = face;
		// Inverse the order because we are flipping the image vertically. Otherwise counter-clockwise points become clockwise.
		for(int i = 2; i >= 0; --i) {
			outPosition = inPosition[i];
			gl_Position = sphereLightShadowMapUniform.perspective * vec4(transpose(views[face]) * (outPosition - sphereLightShadowMapUniform.position), 1.0);
			gl_Position.y = -gl_Position.y;
			EmitVertex();
		}
		EndPrimitive();
	}
}