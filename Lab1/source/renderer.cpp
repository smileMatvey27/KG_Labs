#include <imgui.h>
#include "renderer.hpp"
#include <cmath>
#include <fstream>
#include <vector>
#include <cstring>
#include <iostream>
#include <cstddef>

#include <glm/gtc/matrix_transform.hpp>

namespace renderer {

	Settings settings;

	namespace {
		VkCommandPool vk_upload_command_pool = VK_NULL_HANDLE;

		VkBuffer vk_vertex_buffer = VK_NULL_HANDLE;
		VmaAllocation vma_vertex_buffer_allocation = VK_NULL_HANDLE;
		uint32_t vk_vertex_count = 0;

		VkBuffer vk_index_buffer = VK_NULL_HANDLE;
		VmaAllocation vma_index_buffer_allocation = VK_NULL_HANDLE;
		uint32_t vk_index_count = 0;

		VkShaderModule vk_shader_module_vert = VK_NULL_HANDLE;
		VkShaderModule vk_shader_module_frag = VK_NULL_HANDLE;
		VkPipelineLayout vk_pipeline_layout = VK_NULL_HANDLE;
		VkPipeline vk_pipeline = VK_NULL_HANDLE;
		VkBuffer vk_uniform_buffer = VK_NULL_HANDLE;
		VmaAllocation vma_uniform_buffer_allocation = VK_NULL_HANDLE;
		void* uniform_buffer_mapped = nullptr;

		VkDescriptorSetLayout vk_descriptor_set_layout = VK_NULL_HANDLE;
		VkDescriptorPool vk_descriptor_pool = VK_NULL_HANDLE;
		VkDescriptorSet vk_descriptor_set = VK_NULL_HANDLE;

		bool createUploadCommandPool() {
			const VkCommandPoolCreateInfo info = {
				.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
				.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
				.queueFamilyIndex = graphics::internal::context.graphics_queue_index,
			};

			if (vkCreateCommandPool(graphics::internal::context.device,
				&info, nullptr,
				&vk_upload_command_pool) != VK_SUCCESS) {
				std::cerr << "[renderer] Failed to create upload command pool\n";
				return false;
			}
			return true;
		}

		void destroyUploadCommandPool() {
			if (vk_upload_command_pool != VK_NULL_HANDLE) {
				vkDestroyCommandPool(graphics::internal::context.device,
					vk_upload_command_pool, nullptr);
				vk_upload_command_pool = VK_NULL_HANDLE;
			}
		}

		VkCommandBuffer beginSingleTimeCommands() {
			const VkCommandBufferAllocateInfo alloc_info = {
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = vk_upload_command_pool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1,
			};

			VkCommandBuffer cmd = VK_NULL_HANDLE;
			vkAllocateCommandBuffers(graphics::internal::context.device,
				&alloc_info, &cmd);

			const VkCommandBufferBeginInfo begin_info = {
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
				.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
			};

			vkBeginCommandBuffer(cmd, &begin_info);
			return cmd;
		}

		void endSingleTimeCommands(VkCommandBuffer cmd) {
			vkEndCommandBuffer(cmd);

			const VkSubmitInfo submit = {
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.commandBufferCount = 1,
				.pCommandBuffers = &cmd,
			};

			vkQueueSubmit(graphics::internal::context.graphics_queue,
				1, &submit, VK_NULL_HANDLE);
			vkQueueWaitIdle(graphics::internal::context.graphics_queue);

			vkFreeCommandBuffers(graphics::internal::context.device,
				vk_upload_command_pool,
				1, &cmd);
		}

		bool createBuffer(VkDeviceSize size,
			VkBufferUsageFlags usage,
			VmaMemoryUsage memory_usage,
			VmaAllocationCreateFlags flags,
			VkBuffer& out_buffer,
			VmaAllocation& out_allocation,
			void** out_mapped) {
			const VkBufferCreateInfo buffer_info = {
				.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
				.size = size,
				.usage = usage,
				.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			};

			const VmaAllocationCreateInfo alloc_info = {
				.flags = flags,
				.usage = memory_usage,
			};

			VmaAllocationInfo allocation_info = {};

			if (vmaCreateBuffer(graphics::internal::context.allocator,
				&buffer_info, &alloc_info,
				&out_buffer, &out_allocation,
				&allocation_info) != VK_SUCCESS) {
				return false;
			}

			if (out_mapped != nullptr) {
				*out_mapped = allocation_info.pMappedData;
			}
			return true;
		}

		void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
			VkCommandBuffer cmd = beginSingleTimeCommands();

			const VkBufferCopy copy_region = {
				.srcOffset = 0,
				.dstOffset = 0,
				.size = size,
			};

			vkCmdCopyBuffer(cmd, src, dst, 1, &copy_region);

			endSingleTimeCommands(cmd);
		}

		bool createUniformBuffer() {
			const VkDeviceSize buffer_size = sizeof(UniformBufferObject);

			const VkBufferCreateInfo buffer_info = {
				.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
				.size = buffer_size,
				.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			};

			const VmaAllocationCreateInfo alloc_info = {
				.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
						 VMA_ALLOCATION_CREATE_MAPPED_BIT,
				.usage = VMA_MEMORY_USAGE_AUTO,
			};

			VmaAllocationInfo allocation_info = {};

			if (vmaCreateBuffer(graphics::internal::context.allocator,
				&buffer_info,
				&alloc_info,
				&vk_uniform_buffer,
				&vma_uniform_buffer_allocation,
				&allocation_info) != VK_SUCCESS) {
				std::cerr << "[renderer] Failed to create uniform buffer\n";
				return false;
			}

			uniform_buffer_mapped = allocation_info.pMappedData;
			return uniform_buffer_mapped != nullptr;
		}

		void destroyUniformBuffer() {
			if (vk_uniform_buffer != VK_NULL_HANDLE) {
				vmaDestroyBuffer(graphics::internal::context.allocator,
					vk_uniform_buffer,
					vma_uniform_buffer_allocation);
				vk_uniform_buffer = VK_NULL_HANDLE;
				vma_uniform_buffer_allocation = VK_NULL_HANDLE;
				uniform_buffer_mapped = nullptr;
			}
		}

		bool createDescriptorSetLayout() {
			const VkDescriptorSetLayoutBinding ubo_binding = {
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
				.pImmutableSamplers = nullptr,
			};

			const VkDescriptorSetLayoutCreateInfo layout_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = 1,
				.pBindings = &ubo_binding,
			};

			if (vkCreateDescriptorSetLayout(graphics::internal::context.device,
				&layout_info,
				nullptr,
				&vk_descriptor_set_layout) != VK_SUCCESS) {
				std::cerr << "[renderer] Failed to create descriptor set layout\n";
				return false;
			}

			return true;
		}

		void destroyDescriptorSetLayout() {
			if (vk_descriptor_set_layout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(graphics::internal::context.device,
					vk_descriptor_set_layout,
					nullptr);
				vk_descriptor_set_layout = VK_NULL_HANDLE;
			}
		}

		bool createDescriptorPool() {
			const VkDescriptorPoolSize pool_size = {
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1,
			};

			const VkDescriptorPoolCreateInfo pool_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
				.maxSets = 1,
				.poolSizeCount = 1,
				.pPoolSizes = &pool_size,
			};

			if (vkCreateDescriptorPool(graphics::internal::context.device,
				&pool_info,
				nullptr,
				&vk_descriptor_pool) != VK_SUCCESS) {
				std::cerr << "[renderer] Failed to create descriptor pool\n";
				return false;
			}

			return true;
		}

		void destroyDescriptorPool() {
			if (vk_descriptor_pool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(graphics::internal::context.device,
					vk_descriptor_pool,
					nullptr);
				vk_descriptor_pool = VK_NULL_HANDLE;
			}
		}

		bool createDescriptorSet() {
			const VkDescriptorSetAllocateInfo alloc_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = vk_descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &vk_descriptor_set_layout,
			};

			if (vkAllocateDescriptorSets(graphics::internal::context.device,
				&alloc_info,
				&vk_descriptor_set) != VK_SUCCESS) {
				std::cerr << "[renderer] Failed to allocate descriptor set\n";
				return false;
			}

			const VkDescriptorBufferInfo buffer_info = {
				.buffer = vk_uniform_buffer,
				.offset = 0,
				.range = sizeof(UniformBufferObject),
			};

			const VkWriteDescriptorSet write = {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = vk_descriptor_set,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pImageInfo = nullptr,
				.pBufferInfo = &buffer_info,
				.pTexelBufferView = nullptr,
			};

			vkUpdateDescriptorSets(graphics::internal::context.device,
				1, &write, 0, nullptr);

			return true;
		}

	}

	bool readFile(const char* path, std::vector<char>& out) {
		std::ifstream file(path, std::ios::ate | std::ios::binary);
		if (!file.is_open()) {
			std::cerr << "[renderer] Failed to open file: " << path << "\n";
			return false;
		}

		const size_t size = size_t(file.tellg());
		out.resize(size);

		file.seekg(0);
		file.read(out.data(), std::streamsize(size));
		file.close();

		return true;
	}

	VkShaderModule createShaderModule(const std::vector<char>& code) {
		const VkShaderModuleCreateInfo info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = code.size(),
			.pCode = reinterpret_cast<const uint32_t*>(code.data()),
		};

		VkShaderModule module = VK_NULL_HANDLE;
		if (vkCreateShaderModule(graphics::internal::context.device,
			&info, nullptr, &module) != VK_SUCCESS) {
			std::cerr << "[renderer] Failed to create shader module\n";
			return VK_NULL_HANDLE;
		}
		return module;
	}

	bool createVertexBuffer() {
		const sphere::Mesh mesh = sphere::generate(1.0f);
		vk_vertex_count = uint32_t(mesh.vertices.size());

		const VkDeviceSize buffer_size = VkDeviceSize(mesh.vertices.size() * sizeof(sphere::Vertex));

		VkBuffer staging_buffer = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = VK_NULL_HANDLE;
		void* staging_mapped = nullptr;

		if (!createBuffer(buffer_size,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_MEMORY_USAGE_AUTO,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
			VMA_ALLOCATION_CREATE_MAPPED_BIT,
			staging_buffer, staging_alloc, &staging_mapped)) {
			std::cerr << "[renderer] Failed to create vertex staging buffer\n";
			return false;
		}

		std::memcpy(staging_mapped, mesh.vertices.data(), size_t(buffer_size));

		if (!createBuffer(buffer_size,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
			0,
			vk_vertex_buffer, vma_vertex_buffer_allocation, nullptr)) {
			std::cerr << "[renderer] Failed to create vertex buffer\n";
			vmaDestroyBuffer(graphics::internal::context.allocator,
				staging_buffer, staging_alloc);
			return false;
		}

		copyBuffer(staging_buffer, vk_vertex_buffer, buffer_size);

		vmaDestroyBuffer(graphics::internal::context.allocator,
			staging_buffer, staging_alloc);

		std::cerr << "[renderer] Vertex buffer created: "
			<< vk_vertex_count << " vertices, "
			<< buffer_size << " bytes\n";

		return true;
	}

	void destroyVertexBuffer() {
		if (vk_vertex_buffer != VK_NULL_HANDLE) {
			vmaDestroyBuffer(graphics::internal::context.allocator,
				vk_vertex_buffer, vma_vertex_buffer_allocation);
			vk_vertex_buffer = VK_NULL_HANDLE;
			vma_vertex_buffer_allocation = VK_NULL_HANDLE;
		}
	}

	bool createIndexBuffer() {
		const sphere::Mesh mesh = sphere::generate(1.0f);
		vk_index_count = uint32_t(mesh.indices.size());

		const VkDeviceSize buffer_size = VkDeviceSize(mesh.indices.size() * sizeof(uint32_t));

		VkBuffer staging_buffer = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = VK_NULL_HANDLE;
		void* staging_mapped = nullptr;

		if (!createBuffer(buffer_size,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_MEMORY_USAGE_AUTO,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
			VMA_ALLOCATION_CREATE_MAPPED_BIT,
			staging_buffer, staging_alloc, &staging_mapped)) {
			std::cerr << "[renderer] Failed to create index staging buffer\n";
			return false;
		}

		std::memcpy(staging_mapped, mesh.indices.data(), size_t(buffer_size));

		if (!createBuffer(buffer_size,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
			0,
			vk_index_buffer, vma_index_buffer_allocation, nullptr)) {
			std::cerr << "[renderer] Failed to create index buffer\n";
			vmaDestroyBuffer(graphics::internal::context.allocator,
				staging_buffer, staging_alloc);
			return false;
		}

		copyBuffer(staging_buffer, vk_index_buffer, buffer_size);

		vmaDestroyBuffer(graphics::internal::context.allocator,
			staging_buffer, staging_alloc);

		std::cerr << "[renderer] Index buffer created: "
			<< vk_index_count << " indices, "
			<< buffer_size << " bytes\n";

		return true;
	}

	void destroyIndexBuffer() {
		if (vk_index_buffer != VK_NULL_HANDLE) {
			vmaDestroyBuffer(graphics::internal::context.allocator,
				vk_index_buffer, vma_index_buffer_allocation);
			vk_index_buffer = VK_NULL_HANDLE;
			vma_index_buffer_allocation = VK_NULL_HANDLE;
		}
	}

	bool createPipelineLayout() {
		const VkPushConstantRange push_range = {
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.offset = 0,
			.size = sizeof(PushConstants),
		};

		const VkPipelineLayoutCreateInfo info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
			.pSetLayouts = &vk_descriptor_set_layout,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &push_range,
		};

		if (vkCreatePipelineLayout(graphics::internal::context.device,
			&info, nullptr,
			&vk_pipeline_layout) != VK_SUCCESS) {
			std::cerr << "[renderer] Failed to create pipeline layout\n";
			return false;
		}
		return true;
	}

	bool createPipeline() {
		std::vector<char> vert_code;
		std::vector<char> frag_code;
		if (!readFile("shaders/sphere.vert.spv", vert_code)) return false;
		if (!readFile("shaders/sphere.frag.spv", frag_code)) return false;

		vk_shader_module_vert = createShaderModule(vert_code);
		vk_shader_module_frag = createShaderModule(frag_code);
		if (vk_shader_module_vert == VK_NULL_HANDLE) return false;
		if (vk_shader_module_frag == VK_NULL_HANDLE) return false;

		const VkPipelineShaderStageCreateInfo vert_stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vk_shader_module_vert,
			.pName = "main",
		};

		const VkPipelineShaderStageCreateInfo frag_stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = vk_shader_module_frag,
			.pName = "main",
		};

		const VkPipelineShaderStageCreateInfo stages[] = { vert_stage, frag_stage };

		const VkVertexInputBindingDescription binding = {
			.binding = 0,
			.stride = sizeof(sphere::Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		};

		const VkVertexInputAttributeDescription attributes[] = {
			{
				.location = 0,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(sphere::Vertex, position),
			},
			{
				.location = 1,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(sphere::Vertex, color),
			},
		};

		const VkPipelineVertexInputStateCreateInfo vertex_input = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = 1,
			.pVertexBindingDescriptions = &binding,
			.vertexAttributeDescriptionCount = 2,
			.pVertexAttributeDescriptions = attributes,
		};

		const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			.primitiveRestartEnable = VK_FALSE,
		};

		const VkPipelineViewportStateCreateInfo viewport_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.scissorCount = 1,
		};

		const VkPipelineRasterizationStateCreateInfo rasterizer = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.depthClampEnable = VK_FALSE,
			.rasterizerDiscardEnable = VK_FALSE,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.cullMode = VK_CULL_MODE_BACK_BIT,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.depthBiasEnable = VK_FALSE,
			.lineWidth = 1.0f,
		};

		const VkPipelineMultisampleStateCreateInfo multisampling = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
			.sampleShadingEnable = VK_FALSE,
		};

		const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = VK_TRUE,
			.depthWriteEnable = VK_TRUE,
			.depthCompareOp = VK_COMPARE_OP_LESS,
			.depthBoundsTestEnable = VK_FALSE,
			.stencilTestEnable = VK_FALSE,
		};

		const VkPipelineColorBlendAttachmentState blend_attachment = {
			.blendEnable = VK_FALSE,
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
							  VK_COLOR_COMPONENT_G_BIT |
							  VK_COLOR_COMPONENT_B_BIT |
							  VK_COLOR_COMPONENT_A_BIT,
		};

		const VkPipelineColorBlendStateCreateInfo color_blending = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.logicOpEnable = VK_FALSE,
			.attachmentCount = 1,
			.pAttachments = &blend_attachment,
		};

		const VkDynamicState dynamic_states[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
		};

		const VkPipelineDynamicStateCreateInfo dynamic_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = 2,
			.pDynamicStates = dynamic_states,
		};

		const VkGraphicsPipelineCreateInfo pipeline_info = {
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.stageCount = 2,
			.pStages = stages,
			.pVertexInputState = &vertex_input,
			.pInputAssemblyState = &input_assembly,
			.pViewportState = &viewport_state,
			.pRasterizationState = &rasterizer,
			.pMultisampleState = &multisampling,
			.pDepthStencilState = &depth_stencil,
			.pColorBlendState = &color_blending,
			.pDynamicState = &dynamic_state,
			.layout = vk_pipeline_layout,
			.renderPass = graphics::internal::context.render_pass,
			.subpass = 0,
		};

		if (vkCreateGraphicsPipelines(graphics::internal::context.device,
			VK_NULL_HANDLE,
			1, &pipeline_info, nullptr,
			&vk_pipeline) != VK_SUCCESS) {
			std::cerr << "[renderer] Failed to create graphics pipeline\n";
			return false;
		}

		return true;
	}

	void destroyPipeline() {
		if (vk_pipeline != VK_NULL_HANDLE) {
			vkDestroyPipeline(graphics::internal::context.device, vk_pipeline, nullptr);
			vk_pipeline = VK_NULL_HANDLE;
		}
		if (vk_pipeline_layout != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(graphics::internal::context.device, vk_pipeline_layout, nullptr);
			vk_pipeline_layout = VK_NULL_HANDLE;
		}
		if (vk_shader_module_vert != VK_NULL_HANDLE) {
			vkDestroyShaderModule(graphics::internal::context.device, vk_shader_module_vert, nullptr);
			vk_shader_module_vert = VK_NULL_HANDLE;
		}
		if (vk_shader_module_frag != VK_NULL_HANDLE) {
			vkDestroyShaderModule(graphics::internal::context.device, vk_shader_module_frag, nullptr);
			vk_shader_module_frag = VK_NULL_HANDLE;
		}
	}

	bool initialize() {
		if (!createUniformBuffer())       return false;
		if (!createDescriptorSetLayout()) return false;
		if (!createDescriptorPool())      return false;
		if (!createDescriptorSet())       return false;
		if (!createPipelineLayout())      return false;
		if (!createPipeline())            return false;
		if (!createUploadCommandPool())   return false;
		if (!createVertexBuffer())		  return false;
		if (!createIndexBuffer())         return false;
		return true;
	}

	void shutdown() {
		destroyIndexBuffer();
		destroyVertexBuffer();
		destroyUploadCommandPool();
		destroyPipeline();
		destroyDescriptorPool();
		destroyDescriptorSetLayout();
		destroyUniformBuffer();
	}

	void update(double time) {
		// ============ UI ============
		ImGui::Begin("Settings");

		if (ImGui::CollapsingHeader("Projection", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::RadioButton("Perspective", &settings.projection_mode, 0);
			ImGui::SameLine();
			ImGui::RadioButton("Orthographic", &settings.projection_mode, 1);

			if (settings.projection_mode == 0) {
				ImGui::SliderFloat("FOV", &settings.fov_degrees, 10.0f, 120.0f, "%.1f deg");
				ImGui::SliderFloat("Near", &settings.near_plane, 0.01f, 1.0f);
				ImGui::SliderFloat("Far", &settings.far_plane, 10.0f, 500.0f);
			}
			else {
				ImGui::SliderFloat("Ortho size", &settings.ortho_size, 0.1f, 10.0f);
			}

			ImGui::SliderFloat("Camera distance", &settings.camera_distance, 0.5f, 20.0f);
		}

		if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat3("Position", &settings.position.x, -5.0f, 5.0f);
			ImGui::SliderFloat3("Rotation", &settings.rotation_degrees.x, -180.0f, 180.0f);
			ImGui::SliderFloat3("Scale", &settings.scale.x, 0.1f, 5.0f);

			if (ImGui::Button("Reset transform")) {
				settings.position = { 0.0f, 0.0f, 0.0f };
				settings.rotation_degrees = { 0.0f, 0.0f, 0.0f };
				settings.scale = { 1.0f, 1.0f, 1.0f };
			}
		}

		if (ImGui::CollapsingHeader("Color", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::ColorEdit4("Tint", &settings.tint.x);

			if (ImGui::Button("Reset color")) {
				settings.tint = { 1.0f, 1.0f, 1.0f, 1.0f };
			}

			ImGui::TextWrapped(
				"Tip: vertex colors are procedural (based on the vertex normal). "
				"The tint above is multiplied with them in the fragment shader.");
		}

		if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::Button(settings.animation_playing ? "Pause" : "Play")) {
				settings.animation_playing = !settings.animation_playing;
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset animation")) {
				settings.animation_time = 0.0f;
				settings.animation_playing = false;
				settings.position = { 0.0f, 0.0f, 0.0f };
				settings.rotation_degrees = { 0.0f, 0.0f, 0.0f };
			}

			ImGui::SliderFloat("Speed", &settings.animation_speed, 0.0f, 5.0f);
			ImGui::SliderFloat("Radius", &settings.animation_radius, 0.0f, 5.0f);
			ImGui::SliderFloat("Height", &settings.animation_height, -2.0f, 2.0f);

			ImGui::Text("Animation time: %.2f s", settings.animation_time);
		}

		ImGui::End();

		// ============ Animation ============
		static double last_time = 0.0;
		const double delta = (last_time == 0.0) ? 0.0 : (time - last_time);
		last_time = time;

		if (settings.animation_playing) {
			settings.animation_time += float(delta) * settings.animation_speed;
		}

		if (settings.animation_playing || settings.animation_time > 0.0f) {
			const float t = settings.animation_time;
			const float r = settings.animation_radius;
			const float h = settings.animation_height;

			settings.position.x = std::cos(t) * r;
			settings.position.z = std::sin(t) * r;
			settings.position.y = std::sin(t * 2.0f) * h;

			settings.rotation_degrees.y = t * 60.0f;
		}

		// ============ Matrices ============
		if (uniform_buffer_mapped == nullptr) {
			return;
		}

		const uint32_t w = graphics::internal::context.swapchain_extent.width;
		const uint32_t h = graphics::internal::context.swapchain_extent.height;
		if (w == 0 || h == 0) {
			return;
		}

		const float aspect = float(w) / float(h);

		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, settings.position);
		model = glm::rotate(model, glm::radians(settings.rotation_degrees.x), glm::vec3(1, 0, 0));
		model = glm::rotate(model, glm::radians(settings.rotation_degrees.y), glm::vec3(0, 1, 0));
		model = glm::rotate(model, glm::radians(settings.rotation_degrees.z), glm::vec3(0, 0, 1));
		model = glm::scale(model, settings.scale);

		glm::mat4 view = glm::lookAt(
			glm::vec3(0.0f, 0.0f, settings.camera_distance),
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f));

		glm::mat4 projection;
		if (settings.projection_mode == 0) {
			projection = glm::perspectiveRH_ZO(
				glm::radians(settings.fov_degrees),
				aspect,
				settings.near_plane,
				settings.far_plane);
		}
		else {
			const float half_size = settings.ortho_size;
			projection = glm::orthoRH_ZO(
				-half_size * aspect, half_size * aspect,
				-half_size, half_size,
				settings.near_plane, settings.far_plane);
		}

		UniformBufferObject ubo = {};
		ubo.model = model;
		ubo.view = view;
		ubo.projection = projection;

		std::memcpy(uniform_buffer_mapped, &ubo, sizeof(ubo));
	}

	void render(const graphics::internal::FrameData& fd) {
		if (vk_pipeline == VK_NULL_HANDLE) {
			return;
		}

		vkResetCommandBuffer(fd.command_buffer, 0);

		const VkCommandBufferBeginInfo begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};

		if (vkBeginCommandBuffer(fd.command_buffer, &begin_info) != VK_SUCCESS) {
			return;
		}

		const VkClearValue clear_values[2] = {
			{.color = { { 0.1f, 0.1f, 0.15f, 1.0f } } },
			{.depthStencil = { 1.0f, 0 } },
		};

		const VkRenderPassBeginInfo render_pass_begin = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = graphics::internal::context.render_pass,
			.framebuffer = fd.framebuffer,
			.renderArea = {
				.offset = { 0, 0 },
				.extent = graphics::internal::context.swapchain_extent,
			},
			.clearValueCount = 2,
			.pClearValues = clear_values,
		};

		vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin,
			VK_SUBPASS_CONTENTS_INLINE);

		const VkViewport viewport = {
			.x = 0.0f,
			.y = 0.0f,
			.width = float(graphics::internal::context.swapchain_extent.width),
			.height = float(graphics::internal::context.swapchain_extent.height),
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};
		vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);

		const VkRect2D scissor = {
			.offset = { 0, 0 },
			.extent = graphics::internal::context.swapchain_extent,
		};
		vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

		vkCmdBindPipeline(fd.command_buffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk_pipeline);

		vkCmdBindDescriptorSets(fd.command_buffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk_pipeline_layout,
			0, 1, &vk_descriptor_set,
			0, nullptr);

		const PushConstants pc = { .tint = settings.tint };
		vkCmdPushConstants(fd.command_buffer,
			vk_pipeline_layout,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(PushConstants),
			&pc);

		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(fd.command_buffer, 0, 1,
			&vk_vertex_buffer, &offset);

		vkCmdBindIndexBuffer(fd.command_buffer,
			vk_index_buffer,
			0,
			VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(fd.command_buffer,
			vk_index_count,
			1,
			0, 0, 0);

		vkCmdEndRenderPass(fd.command_buffer);

		vkEndCommandBuffer(fd.command_buffer);
	}

}