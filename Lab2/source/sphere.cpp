#include "sphere.hpp"

#include <algorithm>
#include <cmath>

namespace sphere {

	Mesh generate(float radius, int target_vertices) {
		int slices = 14;
		int stacks = 8;
		if (target_vertices != 100 && target_vertices >= 6) {
			slices = std::max(3, int(std::ceil(std::sqrt(float(target_vertices) * 1.96f))));
			stacks = std::max(2, int(std::ceil((float(target_vertices) - 2.0f) / float(slices))) + 1);
		}

		Mesh mesh;

		const float pi = 3.14159265358979323846f;

		auto push = [&](const glm::vec3& normal) {
			Vertex v{};
			v.position = normal * radius;
			v.normal = normal;
			v.color = normal * 0.5f + 0.5f;
			mesh.vertices.push_back(v);
		};

		push(glm::vec3(0.0f, 1.0f, 0.0f));
		for (int i = 1; i < stacks; ++i) {
			const float theta = pi * float(i) / float(stacks);
			const float sin_theta = std::sin(theta);
			const float cos_theta = std::cos(theta);
			for (int j = 0; j < slices; ++j) {
				const float phi = 2.0f * pi * float(j) / float(slices);
				push(glm::vec3(sin_theta * std::cos(phi), cos_theta, sin_theta * std::sin(phi)));
			}
		}
		push(glm::vec3(0.0f, -1.0f, 0.0f));

		for (int j = 0; j < slices; ++j) {
			mesh.indices.push_back(0);
			mesh.indices.push_back(1 + j);
			mesh.indices.push_back(1 + (j + 1) % slices);
		}
		for (int i = 0; i < stacks - 2; ++i) {
			for (int j = 0; j < slices; ++j) {
				const uint32_t a = 1 + i * slices + j;
				const uint32_t b = 1 + i * slices + (j + 1) % slices;
				mesh.indices.push_back(a);
				mesh.indices.push_back(a + slices);
				mesh.indices.push_back(b);
				mesh.indices.push_back(b);
				mesh.indices.push_back(a + slices);
				mesh.indices.push_back(b + slices);
			}
		}
		const uint32_t bottom = uint32_t(mesh.vertices.size()) - 1;
		const uint32_t base = 1 + (stacks - 2) * slices;
		for (int j = 0; j < slices; ++j) {
			mesh.indices.push_back(bottom);
			mesh.indices.push_back(base + (j + 1) % slices);
			mesh.indices.push_back(base + j);
		}

		return mesh;
	}

}
