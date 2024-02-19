#include "Culling.hpp"
#include <array>

BBox::BBox(
	std::size_t vertexCount,
	const std::function<jjyou::glsl::vec3(std::size_t)>& getVertexPos,
	jjyou::glsl::vec3 axisX/* = { 1.0f, 0.0f, 0.0f }*/,
	jjyou::glsl::vec3 axisY/* = { 0.0f, 1.0f, 0.0f }*/
) {
	axisX = jjyou::glsl::normalized(axisX);
	axisY = jjyou::glsl::normalized(axisY);
	jjyou::glsl::vec3 axisZ = jjyou::glsl::cross(axisX, axisY);
	this->axisRotation = jjyou::glsl::mat3(axisX, axisY, axisZ);
	jjyou::glsl::mat3 axisRotationT = jjyou::glsl::transpose(this->axisRotation);
	jjyou::glsl::vec3 min(std::numeric_limits<float>::max()), max(std::numeric_limits<float>::min());
	for (std::size_t i = 0; i < vertexCount; ++i) {
		jjyou::glsl::vec3 localPos = axisRotationT * getVertexPos(i);
		min = jjyou::glsl::min(min, localPos);
		max = jjyou::glsl::max(max, localPos);
	}
	this->center = (min + max) / 2.0f;
	this->extent = max - this->center;
}


bool BBox::insideFrustum(
	const jjyou::glsl::mat4 projection,
	const jjyou::glsl::mat4 view,
	const jjyou::glsl::mat4 model
) {
	// Initialize 8 corner points
	auto transformToClip = [&](const jjyou::glsl::vec3& v) ->jjyou::glsl::vec4 {
		return projection * view * model * jjyou::glsl::vec4(this->axisRotation * v, 1.0f);
		};
	std::array<jjyou::glsl::vec4, 8> corners = { {
		transformToClip(jjyou::glsl::vec3(this->center.x + this->extent.x, this->center.y + this->extent.y, this->center.z + this->extent.z)),
		transformToClip(jjyou::glsl::vec3(this->center.x + this->extent.x, this->center.y - this->extent.y, this->center.z + this->extent.z)),
		transformToClip(jjyou::glsl::vec3(this->center.x - this->extent.x, this->center.y - this->extent.y, this->center.z + this->extent.z)),
		transformToClip(jjyou::glsl::vec3(this->center.x - this->extent.x, this->center.y + this->extent.y, this->center.z + this->extent.z)),
		transformToClip(jjyou::glsl::vec3(this->center.x + this->extent.x, this->center.y + this->extent.y, this->center.z - this->extent.z)),
		transformToClip(jjyou::glsl::vec3(this->center.x + this->extent.x, this->center.y - this->extent.y, this->center.z - this->extent.z)),
		transformToClip(jjyou::glsl::vec3(this->center.x - this->extent.x, this->center.y - this->extent.y, this->center.z - this->extent.z)),
		transformToClip(jjyou::glsl::vec3(this->center.x - this->extent.x, this->center.y + this->extent.y, this->center.z - this->extent.z)),
	} };
	// 1. True positives: If any of these corners is in the frustum, return true.
	for (const auto& corner : corners)
		if (-corner.w <= corner.x &&
			corner.x <= corner.w &&
			-corner.w <= corner.y &&
			corner.y <= corner.w &&
			0 <= corner.z &&
			corner.z <= corner.w)
			return true;
	// 2. True negatives: If all of these corners is on the outside of one frustum plane, return false.
	bool allOutside;
	allOutside = true;
	for (const auto& corner : corners)
		if (!(corner.x < -corner.w)) {
			allOutside = false;
			break;
		}
	if (allOutside) return false;
	allOutside = true;
	for (const auto& corner : corners)
		if (!(corner.x > corner.w)) {
			allOutside = false;
			break;
		}
	if (allOutside) return false;
	allOutside = true;
	for (const auto& corner : corners)
		if (!(corner.y < -corner.w)) {
			allOutside = false;
			break;
		}
	if (allOutside) return false;
	allOutside = true;
	for (const auto& corner : corners)
		if (!(corner.y > corner.w)) {
			allOutside = false;
			break;
		}
	if (allOutside) return false;
	allOutside = true;
	for (const auto& corner : corners)
		if (!(corner.z < 0.0f)) {
			allOutside = false;
			break;
		}
	if (allOutside) return false;
	allOutside = true;
	for (const auto& corner : corners)
		if (!(corner.z > corner.w)) {
			allOutside = false;
			break;
		}
	if (allOutside) return false;
	// 3. Use the frustom to clip the 6 planes of the bbox.
	//    If any of the planes has area > 0 after being clipped, return true.
	std::array<std::array<int, 4>, 6> indices = { {
		{{0, 1, 2, 3}}, // front
		{{7, 6, 4, 5}}, // back
		{{2, 6, 7, 3}}, // left
		{{0, 4, 5, 1}}, // right
		{{1, 5, 6, 2}}, // up
		{{0, 3, 7, 4}}, //down
	} };
	for (int bboxPlaneIdx = 0; bboxPlaneIdx < 6; ++bboxPlaneIdx) {
		const std::array<int, 4>& bboxPlaneVertexIdx = indices[bboxPlaneIdx];
		std::vector<jjyou::glsl::vec4> polygon;
		for (const auto& vIdx : bboxPlaneVertexIdx)
			polygon.push_back(corners[vIdx]);
		auto clip_line_with_one_condition = [](
			const jjyou::glsl::vec4& a, const jjyou::glsl::vec4& b, int condition/* = 0, 1, 2, 3, 4, 5*/,
			std::function<void(const jjyou::glsl::vec4&&)> const& emit_vertex
			) -> void {
				jjyou::glsl::vec4 ba = b - a;

				// Determine portion of line over which:
				// 		pt = (b-a) * t + a
				//  	-pt.w <= pt.x <= pt.w
				//  	-pt.w <= pt.y <= pt.w
				//  	0     <= pt.z <= pt.w
				// ... as a range [min_t, max_t]:

				float min_t = 0.0f;
				float max_t = 1.0f;

				// want to set range of t for equations:
				//    -a.w + t * -ba.w <= a.x + t * ba.x	(0)
				//    a.x + t * ba.x <= a.w + t * ba.w		(1)
				//    -a.w + t * -ba.w <= a.y + t * ba.y	(2)
				//    a.y + t * ba.y <= a.w + t * ba.w		(3)
				//    0 + t * 0 <= a.z + t * ba.z			(4)
				//    a.z + t * ba.z <= a.w + t * ba.w		(5)
				// They are all in the form of:
				//    l + t * dl <= r + t * dr
				float l = 0.0f, dl = 0.0f, r = 0.0f, dr = 0.0f;
				switch (condition) {
				case 0:
					l = -a.w; dl = -ba.w; r = a.x; dr = ba.x;
					break;
				case 1:
					l = a.x; dl = ba.x; r = a.w; dr = ba.w;
					break;
				case 2:
					l = -a.w; dl = -ba.w; r = a.y; dr = ba.y;
					break;
				case 3:
					l = a.y; dl = ba.y; r = a.w; dr = ba.w;
					break;
				case 4:
					l = 0.0f; dl = 0.0f; r = a.z; dr = ba.z;
					break;
				case 5:
					l = a.z; dl = ba.z; r = a.w; dr = ba.w;
					break;
				default:
					break;
				}
				if (dr == dl) {
					// want: l - r <= 0
					if (l - r > 0.0f) {
						// works for none of range, so make range empty:
						min_t = 1.0f;
						max_t = 0.0f;
					}
				}
				else if (dr > dl) {
					// since dr - dl is positive:
					// want: (l - r) / (dr - dl) <= t
					min_t = std::max(min_t, (l - r) / (dr - dl));
				}
				else { // dr < dl
					// since dr - dl is negative:
					// want: (l - r) / (dr - dl) >= t
					max_t = std::min(max_t, (l - r) / (dr - dl));
				}
				if (min_t < max_t) {
					emit_vertex(a + min_t * ba);
					if (max_t != 1.0f)
						// If max_t == 1.0f, this vertex will be emitted by the next clipped line.
						emit_vertex(a + max_t * ba);
				}
				else if (min_t == max_t) {
					if (min_t != 1.0f)
						emit_vertex(a + min_t * ba);
				}
			};
		std::vector<jjyou::glsl::vec4> clippedPolygon;
		std::function<void(const jjyou::glsl::vec4&)> push_vertex = [&](const jjyou::glsl::vec4& v) -> void {
			clippedPolygon.push_back(v);
			};
		for (int condition = 0; condition <= 5; ++condition) {
			if (polygon.size() <= 2)
				break;
			for (int i = 0; i <= static_cast<int>(polygon.size()) - 2; ++i)
				clip_line_with_one_condition(polygon[i], polygon[i + 1], condition, push_vertex);
			clip_line_with_one_condition(polygon.back(), polygon[0], condition, push_vertex);
			std::swap(polygon, clippedPolygon);
			clippedPolygon.clear();
		}
		if (polygon.size() >= 3)
			return true;
	}
	return false;
}