#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(inNormal);
    vec3 light = mix(vec3(0,0,0), vec3(1,1,1), max(dot(normal, vec3(0, 0, -1)), 0.0) * 0.5 + 0.5);
	outColor = vec4(light * inColor.rgb, inColor.a);
}