#pragma once

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <array>
#include "graphics_internal.hpp"
#include "sphere.hpp"

namespace renderer {

	struct SceneUniforms {
    glm::mat4 view_projection;
    glm::vec4 camera_position;
    glm::vec4 directional_light_direction;
    glm::vec4 directional_light_color;
    glm::vec4 ambient_light;
    uint32_t point_light_count = 0;
    uint32_t spot_light_count = 0;
    uint32_t _pad[2] = {};
	};

	struct ModelUniforms {
		glm::mat4 model;
		glm::mat4 normal_matrix = glm::mat4(1.0f);
		glm::vec4 albedo;
		glm::vec4 specular;
		float shininess = 32.0f;
		float _pad[3] = {};
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

		bool point_light_moved = false;
		
		float camera_distance = 3.0f;        
		float camera_yaw_degrees = 0.0f;
		float camera_pitch_degrees = 0.0f;	
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
		glm::vec3 light_direction = { 0.0f, -1.0f, 0.0f };
		glm::vec3 light_color = { 1.0f, 1.0f, 1.0f };
		float light_intensity = 1.0f;

		glm::vec3 ambient_color = { 1.0f, 1.0f, 1.0f };
		float ambient_intensity = 0.1f;

		glm::vec3 albedo = { 1.0f, 1.0f, 1.0f };
		glm::vec3 specular_color = { 1.0f, 1.0f, 1.0f };
		float shininess = 32.0f;
		static constexpr int MAX_POINT_LIGHTS = 16;

		struct PointLight {
			glm::vec4 position = { 0.0f, 0.0f, 0.0f, 0.0f };
			glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
		};

		int point_light_count = 0;
		std::array<PointLight, MAX_POINT_LIGHTS> point_lights = {};

		static constexpr int MAX_SPOT_LIGHTS = 8;

		struct SpotLight {
			glm::vec4 position  = { 0.0f, 0.0f, 0.0f, 0.0f };
			glm::vec4 direction = { 0.0f, -1.0f, 0.0f, 0.0f };
			glm::vec4 color     = { 1.0f, 1.0f, 1.0f, 1.0f };
			float inner_angle = 15.0f;
			float outer_angle = 30.0f;
			float _pad[2] = {};
		};

		int spot_light_count = 0;
		std::array<SpotLight, MAX_SPOT_LIGHTS> spot_lights = {};
	};

	extern Settings settings;

}