#pragma once

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include "graphics_internal.hpp"
#include "sphere.hpp"

namespace renderer {

	struct UniformBufferObject {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};

	struct PushConstants {
		glm::vec4 tint;
	};

	bool initialize();

	void shutdown();

	void update(double time);

	void render(const graphics::internal::FrameData& fd);

	struct Settings {
		int projection_mode = 0;

		float camera_distance = 3.0f;
		float fov_degrees = 45.0f;
		float near_plane = 0.1f;
		float far_plane = 100.0f;
		float ortho_size = 1.5f;

		glm::vec3 position = { 0.0f, 0.0f, 0.0f };
		glm::vec3 rotation_degrees = { 0.0f, 0.0f, 0.0f };
		glm::vec3 scale = { 1.0f, 1.0f, 1.0f };

		glm::vec4 tint = { 1.0f, 1.0f, 1.0f, 1.0f };

		bool animation_playing = false;
		float animation_speed = 1.0f;
		float animation_radius = 1.5f;
		float animation_height = 0.5f;
		float animation_time = 0.0f;
	};

	extern Settings settings;

}