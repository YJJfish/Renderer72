#version 450

layout(set = 0, binding = 0) uniform sampler2D ssao;

layout(push_constant) uniform SSAOBlurParameters {
	int blurRadius;
} ssaoBlurParameters;

layout(location = 0) in vec2 inTexCoord;

layout(location = 0) out float outSSAOBlur;

float square(float x){
	return x * x;
}

void main() {
	vec2 texelSize = 1.0 / vec2(textureSize(ssao, 0));
	float result = 0.0;
	for (int x = -ssaoBlurParameters.blurRadius; x <= ssaoBlurParameters.blurRadius; x++) {
		for (int y = -ssaoBlurParameters.blurRadius; y <= ssaoBlurParameters.blurRadius; y++) {
			vec2 offset = vec2(float(x), float(y)) * texelSize;
			result += texture(ssao, inTexCoord + offset).r;
		}
	}
	outSSAOBlur = result / square(float(ssaoBlurParameters.blurRadius * 2 + 1));
}