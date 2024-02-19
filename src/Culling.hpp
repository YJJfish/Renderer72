#pragma once
#include "fwd.hpp"

#include <functional>
#include <jjyou/glsl/glsl.hpp>

class BBox {
public:
	jjyou::glsl::vec3 center{};
	jjyou::glsl::vec3 extent{};
	jjyou::glsl::mat3 axisRotation = jjyou::glsl::mat3(1.0f);

	BBox(void) = default;
	BBox(const BBox&) = default;
	BBox(BBox&&) = default;
	BBox& operator=(const BBox&) = default;
	BBox& operator=(BBox&&) = default;

	BBox(
		std::size_t vertexCount,
		const std::function<jjyou::glsl::vec3(std::size_t)>& getVertexPos,
		jjyou::glsl::vec3 axisX = { 1.0f, 0.0f, 0.0f },
		jjyou::glsl::vec3 axisY = { 0.0f, 1.0f, 0.0f }
	);

	bool insideFrustum(
		const jjyou::glsl::mat4 projection,
		const jjyou::glsl::mat4 view,
		const jjyou::glsl::mat4 model
	);
};

class BSphere {
public:
	jjyou::glsl::vec3 center{};
	float radius = 0.0f;

	BSphere(void) = default;
	BSphere(const BSphere&) = default;
	BSphere(BSphere&&) = default;
	BSphere& operator=(const BSphere&) = default;
	BSphere& operator=(BSphere&&) = default;
};