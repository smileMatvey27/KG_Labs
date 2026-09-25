#include "sphere.hpp"

#include <cmath>

namespace sphere {

	Mesh generate(float radius, int target_vertices) {
		int slices = 9;   
		int stacks = 9;

		Mesh mesh;

		const float pi = 3.14159265358979323846f;

		for (int i = 0; i <= stacks; ++i) {
			float theta = pi * float(i) / float(stacks);
			float sin_theta = std::sin(theta);
			float cos_theta = std::cos(theta);

			for (int j = 0; j <= slices; ++j) {
				float phi = 2.0f * pi * float(j) / float(slices);
				float sin_phi = std::sin(phi);
				float cos_phi = std::cos(phi);

				glm::vec3 normal = {
					sin_theta * cos_phi,
					cos_theta,
					sin_theta * sin_phi,
				};

				Vertex v{};
				v.position = normal * radius;

				v.color = normal * 0.5f + 0.5f;

				mesh.vertices.push_back(v);
			}
		}

		for (int i = 0; i < stacks; ++i) {
			for (int j = 0; j < slices; ++j) {
				uint32_t a = i * (slices + 1) + j;
				uint32_t b = a + 1;
				uint32_t c = (i + 1) * (slices + 1) + j;
				uint32_t d = c + 1;

				mesh.indices.push_back(a);
				mesh.indices.push_back(c);
				mesh.indices.push_back(b);

				mesh.indices.push_back(b);
				mesh.indices.push_back(c);
				mesh.indices.push_back(d);
			}
		}

		(void)target_vertices;

		return mesh;
	}

}