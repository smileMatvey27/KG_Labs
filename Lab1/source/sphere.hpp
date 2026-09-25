#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace sphere {

	struct Vertex {
		glm::vec3 position;
		glm::vec3 color;
	};

	struct Mesh {
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
	};

	Mesh generate(float radius = 1.0f, int target_vertices = 100);

}