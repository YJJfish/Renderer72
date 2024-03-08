#version 450

vec3 positions[8] = vec3[](
    vec3(+1.0, +1.0, -1.0),
    vec3(+1.0, -1.0, -1.0),
    vec3(-1.0, -1.0, -1.0),
    vec3(-1.0, +1.0, -1.0),
	vec3(+1.0, +1.0, +1.0),
    vec3(+1.0, -1.0, +1.0),
    vec3(-1.0, -1.0, +1.0),
    vec3(-1.0, +1.0, +1.0)
);

int indices[36] = int[](
	// front
	0, 3, 2,
	0, 2, 1,
	// back
	4, 5, 6,
	4, 6, 7,
	// left
	7, 6, 2,
	7, 2, 3,
	// right
	0, 1, 5,
	0, 5, 4,
	// up
	2, 6, 5,
	2, 5, 1,
	// down
	0, 4, 7,
	0, 7, 3
);

layout(set = 0, binding = 0) uniform ViewLevelUniform {
	mat4 projection;
	mat4 view;
	vec4 viewPos;
} viewLevelUniform;


layout(location = 0) out vec3 outPosition;

void main() {
	vec3 inPosition = positions[indices[gl_VertexIndex]];
	outPosition = inPosition;
	gl_Position = (viewLevelUniform.projection * mat4(mat3(viewLevelUniform.view)) * vec4(inPosition, 1.0)).xyww;
}