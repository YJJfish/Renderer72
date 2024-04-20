#version 450

layout(set = 0, binding = 0) uniform ViewLevelUniform {
	mat4 projection;
	mat4 view;
	vec4 viewPos;
} viewLevelUniform;

vec4 ndcPositions[6] = vec4[](
	vec4(-1.0, -1.0, 0.5, 1.0),
	vec4(-1.0, +1.0, 0.5, 1.0),
	vec4(+1.0, +1.0, 0.5, 1.0),

	vec4(-1.0, -1.0, 0.5, 1.0),
	vec4(+1.0, +1.0, 0.5, 1.0),
	vec4(+1.0, -1.0, 0.5, 1.0)
);

vec2 texCoords[6] = vec2[](
	vec2(0.0, 0.0),
	vec2(0.0, 1.0),
	vec2(1.0, 1.0),

	vec2(0.0, 0.0),
	vec2(1.0, 1.0),
	vec2(1.0, 0.0)
);

layout(location = 0) out vec2 outTexCoord;

void main() {
	gl_Position = ndcPositions[gl_VertexIndex];
	outTexCoord = texCoords[gl_VertexIndex];
}