#include <imgui.h>
#include "renderer.hpp"
#include <cmath>
#include <fstream>
#include <vector>
#include <cstring>
#include <iostream>
#include <cstddef>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

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
		VkBuffer vk_scene_uniform_buffer = VK_NULL_HANDLE;
		VmaAllocation vma_scene_uniform_buffer_allocation = VK_NULL_HANDLE;
		void* scene_uniform_buffer_mapped = nullptr;

		VkDescriptorSetLayout vk_scene_set_layout = VK_NULL_HANDLE;
		VkDescriptorSet vk_scene_set = VK_NULL_HANDLE;

		VkBuffer vk_model_uniform_buffer = VK_NULL_HANDLE;
		VmaAllocation vma_model_uniform_buffer_allocation = VK_NULL_HANDLE;
		void* model_uniform_buffer_mapped = nullptr;

		VkDescriptorSetLayout vk_model_set_layout = VK_NULL_HANDLE;
		VkDescriptorSet vk_model_set = VK_NULL_HANDLE;

		VkDescriptorPool vk_descriptor_pool = VK_NULL_HANDLE;

		VkBuffer vk_point_lights_buffer = VK_NULL_HANDLE;
		VmaAllocation vma_point_lights_buffer_allocation = VK_NULL_HANDLE;
		void* point_lights_buffer_mapped = nullptr;

		VkBuffer vk_spot_lights_buffer = VK_NULL_HANDLE;
		VmaAllocation vma_spot_lights_buffer_allocation = VK_NULL_HANDLE;
		void* spot_lights_buffer_mapped = nullptr;

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

		bool createSceneUniformBuffer() {
			const VkDeviceSize buffer_size = sizeof(SceneUniforms);

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
								&buffer_info, &alloc_info,
								&vk_scene_uniform_buffer,
								&vma_scene_uniform_buffer_allocation,
								&allocation_info) != VK_SUCCESS) {
				std::cerr << "[renderer] Failed to create scene uniform buffer\n";
				return false;
			}

			scene_uniform_buffer_mapped = allocation_info.pMappedData;
			return scene_uniform_buffer_mapped != nullptr;
		}

		bool createModelUniformBuffer() {
			const VkDeviceSize buffer_size = sizeof(ModelUniforms);

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
								&buffer_info, &alloc_info,
								&vk_model_uniform_buffer,
								&vma_model_uniform_buffer_allocation,
								&allocation_info) != VK_SUCCESS) {
				std::cerr << "[renderer] Failed to create model uniform buffer\n";
				return false;
			}

			model_uniform_buffer_mapped = allocation_info.pMappedData;
			return model_uniform_buffer_mapped != nullptr;
		}

		bool createPointLightsBuffer() {
			const VkDeviceSize buffer_size =
				sizeof(Settings::PointLight) * Settings::MAX_POINT_LIGHTS;

			const VkBufferCreateInfo buffer_info = {
				.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
				.size = buffer_size,
				.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			};

			const VmaAllocationCreateInfo alloc_info = {
				.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
						VMA_ALLOCATION_CREATE_MAPPED_BIT,
				.usage = VMA_MEMORY_USAGE_AUTO,
			};

			VmaAllocationInfo allocation_info = {};
			if (vmaCreateBuffer(graphics::internal::context.allocator,
								&buffer_info, &alloc_info,
								&vk_point_lights_buffer,
								&vma_point_lights_buffer_allocation,
								&allocation_info) != VK_SUCCESS) {
				std::cerr << "[renderer] Failed to create point lights buffer\n";
				return false;
			}

			point_lights_buffer_mapped = allocation_info.pMappedData;
			return point_lights_buffer_mapped != nullptr;
		}

		void destroyPointLightsBuffer() {
			if (vk_point_lights_buffer != VK_NULL_HANDLE) {
				vmaDestroyBuffer(graphics::internal::context.allocator,
								vk_point_lights_buffer,
								vma_point_lights_buffer_allocation);
				vk_point_lights_buffer = VK_NULL_HANDLE;
				vma_point_lights_buffer_allocation = VK_NULL_HANDLE;
				point_lights_buffer_mapped = nullptr;
			}
		}

		bool createSpotLightsBuffer() {
			const VkDeviceSize buffer_size =
				sizeof(Settings::SpotLight) * Settings::MAX_SPOT_LIGHTS;

			const VkBufferCreateInfo buffer_info = {
				.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
				.size = buffer_size,
				.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			};

			const VmaAllocationCreateInfo alloc_info = {
				.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
						VMA_ALLOCATION_CREATE_MAPPED_BIT,
				.usage = VMA_MEMORY_USAGE_AUTO,
			};

			VmaAllocationInfo allocation_info = {};
			if (vmaCreateBuffer(graphics::internal::context.allocator,
								&buffer_info, &alloc_info,
								&vk_spot_lights_buffer,
								&vma_spot_lights_buffer_allocation,
								&allocation_info) != VK_SUCCESS) {
				std::cerr << "[renderer] Failed to create spot lights buffer\n";
				return false;
			}

			spot_lights_buffer_mapped = allocation_info.pMappedData;
			return spot_lights_buffer_mapped != nullptr;
		}

void destroySpotLightsBuffer() {
    if (vk_spot_lights_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(graphics::internal::context.allocator,
                         vk_spot_lights_buffer,
                         vma_spot_lights_buffer_allocation);
        vk_spot_lights_buffer = VK_NULL_HANDLE;
        vma_spot_lights_buffer_allocation = VK_NULL_HANDLE;
        spot_lights_buffer_mapped = nullptr;
    }
}

		void destroySceneUniformBuffer() {
			if (vk_scene_uniform_buffer != VK_NULL_HANDLE) {
				vmaDestroyBuffer(graphics::internal::context.allocator,
								vk_scene_uniform_buffer,
								vma_scene_uniform_buffer_allocation);
				vk_scene_uniform_buffer = VK_NULL_HANDLE;
				vma_scene_uniform_buffer_allocation = VK_NULL_HANDLE;
				scene_uniform_buffer_mapped = nullptr;
			}
		}

		void destroyModelUniformBuffer() {
			if (vk_model_uniform_buffer != VK_NULL_HANDLE) {
				vmaDestroyBuffer(graphics::internal::context.allocator,
								vk_model_uniform_buffer,
								vma_model_uniform_buffer_allocation);
				vk_model_uniform_buffer = VK_NULL_HANDLE;
				vma_model_uniform_buffer_allocation = VK_NULL_HANDLE;
				model_uniform_buffer_mapped = nullptr;
			}
		}

		bool createSceneDescriptorSetLayout() {
			const VkDescriptorSetLayoutBinding bindings[3] = {
				{
					.binding = 0,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					.pImmutableSamplers = nullptr,
				},
				{
					.binding = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
					.pImmutableSamplers = nullptr,
				},
				{
					.binding = 2,                                          
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
					.pImmutableSamplers = nullptr,
				},
			};

			const VkDescriptorSetLayoutCreateInfo info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = 3,                                    
				.pBindings = bindings,
			};

			if (vkCreateDescriptorSetLayout(graphics::internal::context.device,
											&info, nullptr,
											&vk_scene_set_layout) != VK_SUCCESS) {
				std::cerr << "[renderer] Failed to create scene descriptor set layout\n";
				return false;
			}
			return true;
		}

		bool createModelDescriptorSetLayout() {
			const VkDescriptorSetLayoutBinding binding = {
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				.pImmutableSamplers = nullptr,
			};

			const VkDescriptorSetLayoutCreateInfo info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = 1,
				.pBindings = &binding,
			};

			if (vkCreateDescriptorSetLayout(graphics::internal::context.device,
											&info, nullptr,
											&vk_model_set_layout) != VK_SUCCESS) {
				std::cerr << "[renderer] Failed to create model descriptor set layout\n";
				return false;
			}
			return true;
		}

		void destroySceneDescriptorSetLayout() {
			if (vk_scene_set_layout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(graphics::internal::context.device,
											vk_scene_set_layout,
											nullptr);
				vk_scene_set_layout = VK_NULL_HANDLE;
			}
		}

		void destroyModelDescriptorSetLayout() {
			if (vk_model_set_layout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(graphics::internal::context.device,
											vk_model_set_layout,
											nullptr);
				vk_model_set_layout = VK_NULL_HANDLE;
			}
		}

		bool createDescriptorPool() {
			const VkDescriptorPoolSize pool_sizes[2] = {
				{
					.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.descriptorCount = 2,
				},
				{
					.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.descriptorCount = 2,
				},
			};

			const VkDescriptorPoolCreateInfo pool_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
				.maxSets = 2,
				.poolSizeCount = 2,
				.pPoolSizes = pool_sizes,
			};

			if (vkCreateDescriptorPool(graphics::internal::context.device,
									&pool_info, nullptr,
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

		bool createSceneDescriptorSet() {
			const VkDescriptorSetAllocateInfo alloc_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = vk_descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &vk_scene_set_layout,
			};

			if (vkAllocateDescriptorSets(graphics::internal::context.device,
										&alloc_info,
										&vk_scene_set) != VK_SUCCESS) {
				std::cerr << "[renderer] Failed to allocate scene descriptor set\n";
				return false;
			}

			const VkDescriptorBufferInfo scene_buffer_info = {
				.buffer = vk_scene_uniform_buffer,
				.offset = 0,
				.range = sizeof(SceneUniforms),
			};

			const VkDescriptorBufferInfo point_buffer_info = {
				.buffer = vk_point_lights_buffer,
				.offset = 0,
				.range = VK_WHOLE_SIZE,
			};

			const VkDescriptorBufferInfo spot_buffer_info = {
				.buffer = vk_spot_lights_buffer,
				.offset = 0,
				.range = VK_WHOLE_SIZE,
			};

			const VkWriteDescriptorSet writes[3] = {
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = vk_scene_set,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &scene_buffer_info,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = vk_scene_set,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &point_buffer_info,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = vk_scene_set,
					.dstBinding = 2,                                           
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &spot_buffer_info,
				},
			};

			vkUpdateDescriptorSets(graphics::internal::context.device,
								3, writes, 0, nullptr);
			return true;
		}

		bool createModelDescriptorSet() {
			const VkDescriptorSetAllocateInfo alloc_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = vk_descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &vk_model_set_layout,
			};

			if (vkAllocateDescriptorSets(graphics::internal::context.device,
										&alloc_info,
										&vk_model_set) != VK_SUCCESS) {
				std::cerr << "[renderer] Failed to allocate model descriptor set\n";
				return false;
			}

			const VkDescriptorBufferInfo buffer_info = {
				.buffer = vk_model_uniform_buffer,
				.offset = 0,
				.range = sizeof(ModelUniforms),
			};

			const VkWriteDescriptorSet write = {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = vk_model_set,
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

		const VkDescriptorSetLayout set_layouts[2] = {
		vk_scene_set_layout,
		vk_model_set_layout,
		};

		const VkPipelineLayoutCreateInfo info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 2,
			.pSetLayouts = set_layouts,
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
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(sphere::Vertex, position) 
			},
			{ 
				.location = 1, 
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(sphere::Vertex, normal)   
			},
			{ 
				.location = 2, 
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(sphere::Vertex, color) 
			},
		};

		const VkPipelineVertexInputStateCreateInfo vertex_input = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = 1,
			.pVertexBindingDescriptions = &binding,
			.vertexAttributeDescriptionCount = 3,
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
			.cullMode = VK_CULL_MODE_NONE,
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
		if (!createSceneUniformBuffer())       return false;
		if (!createModelUniformBuffer())       return false;
		if (!createPointLightsBuffer())        return false;
		if (!createSpotLightsBuffer())         return false;

		if (!createSceneDescriptorSetLayout()) return false;
		if (!createModelDescriptorSetLayout()) return false;

		if (!createDescriptorPool())           return false;

		if (!createSceneDescriptorSet())       return false;
		if (!createModelDescriptorSet())       return false;

		if (!createPipelineLayout())           return false;
		if (!createPipeline())                 return false;
		if (!createUploadCommandPool())        return false;
		if (!createVertexBuffer())             return false;
		if (!createIndexBuffer())              return false;
		return true;
	}

	void shutdown() {
		destroyIndexBuffer();
		destroyVertexBuffer();
		destroyUploadCommandPool();
		destroyPipeline();
		destroyDescriptorPool();
		destroyModelDescriptorSetLayout();
		destroySceneDescriptorSetLayout();
		destroySpotLightsBuffer();
		destroyPointLightsBuffer();
		destroyModelUniformBuffer();
		destroySceneUniformBuffer();
	}

	void update(double time) {
		ImGui::Begin("Sphere");

		if (ImGui::CollapsingHeader("Projection", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::RadioButton("Perspective", &settings.projection_mode, 0);
			ImGui::SameLine();
			ImGui::RadioButton("Orthographic", &settings.projection_mode, 1);

			if (settings.projection_mode == 0) {
				ImGui::SliderFloat("FOV", &settings.fov_degrees, 10.0f, 120.0f, "%.1f deg");
				ImGui::SliderFloat("Near", &settings.near_plane, 0.01f, 10.0f);
				ImGui::SliderFloat("Far", &settings.far_plane, 0.1f, 200.0f);
				if (settings.far_plane <= settings.near_plane) {
					settings.far_plane = settings.near_plane + 0.1f;
				}
			}
			else {
				ImGui::SliderFloat("Ortho size", &settings.ortho_size, 0.1f, 10.0f);
			}

			if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::SliderFloat("Distance", &settings.camera_distance,       0.5f,  20.0f);
				ImGui::SliderFloat("Yaw",      &settings.camera_yaw_degrees,  -180.0f, 180.0f, "%.1f deg");
				ImGui::SliderFloat("Pitch",    &settings.camera_pitch_degrees, -89.0f,  89.0f, "%.1f deg");

				if (ImGui::Button("Reset camera")) {
					settings.camera_distance = 3.0f;
					settings.camera_yaw_degrees = 0.0f;
					settings.camera_pitch_degrees = 0.0f;
				}
			}
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
			}

			ImGui::SliderFloat("Speed", &settings.animation_speed, 0.0f, 5.0f);
			ImGui::SliderFloat("Radius", &settings.animation_radius, 0.0f, 5.0f);
			ImGui::SliderFloat("Height", &settings.animation_height, -2.0f, 2.0f);

			ImGui::Text("Animation time: %.2f s", settings.animation_time);
		}
		if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat3("Direction", &settings.light_direction.x, -1.0f, 1.0f);
			ImGui::ColorEdit3("Light color", &settings.light_color.x);
			ImGui::SliderFloat("Intensity", &settings.light_intensity, 0.0f, 3.0f);

			if (ImGui::Button("Reset light")) {
				settings.light_direction = { 0.0f, -1.0f, 0.0f };
				settings.light_color = { 1.0f, 1.0f, 1.0f };
				settings.light_intensity = 1.0f;
			}
		}

		if (ImGui::CollapsingHeader("Ambient Light", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::ColorEdit3("Ambient color", &settings.ambient_color.x);
			ImGui::SliderFloat("Ambient intensity", &settings.ambient_intensity, 0.0f, 3.0f);
		}

		if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::ColorEdit3("Albedo", &settings.albedo.x);
			ImGui::ColorEdit3("Specular", &settings.specular_color.x);
			ImGui::SliderFloat("Shininess", &settings.shininess, 1.0f, 256.0f);

			if (ImGui::Button("Reset material")) {
				settings.albedo = { 1.0f, 1.0f, 1.0f };
				settings.specular_color = { 1.0f, 1.0f, 1.0f };
				settings.shininess = 32.0f;
			}
		}
		if (ImGui::CollapsingHeader("Point Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Count: %d / %d", settings.point_light_count, Settings::MAX_POINT_LIGHTS);

			if (ImGui::Button("Add point light")) {
				if (settings.point_light_count < Settings::MAX_POINT_LIGHTS) {
					int i = settings.point_light_count;
					settings.point_lights[i].position = glm::vec4(2.0f, 1.0f, 0.0f, 0.0f);
					settings.point_lights[i].color = glm::vec4(1.0f, 0.5f, 0.3f, 2.0f);
					settings.point_light_count++;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove point light")) {
				if (settings.point_light_count > 0) {
					settings.point_light_count--;
				}
			}

			for (int i = 0; i < settings.point_light_count; ++i) {
				ImGui::PushID(i);
				if (ImGui::TreeNodeEx("", ImGuiTreeNodeFlags_DefaultOpen, "Point Light #%d", i)) {
					ImGui::SliderFloat3("Position", &settings.point_lights[i].position.x, -5.0f, 5.0f);
					ImGui::ColorEdit3("Color", &settings.point_lights[i].color.x);
					ImGui::SliderFloat("Intensity", &settings.point_lights[i].color.w, 0.0f, 10.0f);
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}

		if (ImGui::CollapsingHeader("Spot Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Count: %d / %d",
						settings.spot_light_count,
						Settings::MAX_SPOT_LIGHTS);

			if (ImGui::Button("Add spot light")) {
				if (settings.spot_light_count < Settings::MAX_SPOT_LIGHTS) {
					int i = settings.spot_light_count;
					settings.spot_lights[i].position = glm::vec4(0.0f, 3.0f, 0.0f, 0.0f);
					settings.spot_lights[i].direction = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
					settings.spot_lights[i].color = glm::vec4(0.3f, 0.6f, 1.0f, 3.0f);
					settings.spot_lights[i].inner_angle = 15.0f;
					settings.spot_lights[i].outer_angle = 30.0f;
					settings.spot_light_count++;
				}
			}

			ImGui::SameLine();

			if (ImGui::Button("Remove spot light")) {
				if (settings.spot_light_count > 0) {
					settings.spot_light_count--;
				}
			}

			for (int i = 0; i < settings.spot_light_count; ++i) {
				ImGui::PushID(1000 + i);
				if (ImGui::TreeNodeEx("",
									ImGuiTreeNodeFlags_DefaultOpen,
									"Spot Light #%d",
									i)) {
					ImGui::SliderFloat3("Position",
										&settings.spot_lights[i].position.x,
										-5.0f, 5.0f);
					ImGui::SliderFloat3("Direction",
										&settings.spot_lights[i].direction.x,
										-1.0f, 1.0f);
					ImGui::ColorEdit3("Color",
									&settings.spot_lights[i].color.x);
					ImGui::SliderFloat("Intensity",
									&settings.spot_lights[i].color.w,
									0.0f, 10.0f);
					ImGui::SliderFloat("Inner angle",
									&settings.spot_lights[i].inner_angle,
									1.0f, 89.0f, "%.1f deg");
					ImGui::SliderFloat("Outer angle",
									&settings.spot_lights[i].outer_angle,
									1.0f, 89.0f, "%.1f deg");
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}

		ImGui::End();

		static double last_time = 0.0;
		const double delta = (last_time == 0.0) ? 0.0 : (time - last_time);
		last_time = time;

		if (settings.animation_playing) {
			settings.animation_time += float(delta) * settings.animation_speed;
		}

		glm::vec3 anim_offset = { 0.0f, 0.0f, 0.0f };
		float anim_spin_degrees = 0.0f;
		if (settings.animation_playing || settings.animation_time > 0.0f) {
			const float t = settings.animation_time;
			const float r = settings.animation_radius;
			const float h = settings.animation_height;

			anim_offset.x = r * std::sin(t);
			anim_offset.z = r * std::sin(t) * std::cos(t);
			anim_offset.y = h * std::sin(2.0f * t);

			anim_spin_degrees = t * 60.0f;
		}

		if (scene_uniform_buffer_mapped == nullptr || model_uniform_buffer_mapped == nullptr) {
			return;
		}

		const uint32_t w = graphics::internal::context.swapchain_extent.width;
		const uint32_t h = graphics::internal::context.swapchain_extent.height;
		if (w == 0 || h == 0) {
			return;
		}

		const float aspect = float(w) / float(h);

		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, settings.position + anim_offset);
		model = glm::rotate(model, glm::radians(settings.rotation_degrees.x), glm::vec3(1, 0, 0));
		model = glm::rotate(model, glm::radians(settings.rotation_degrees.y + anim_spin_degrees), glm::vec3(0, 1, 0));
		model = glm::rotate(model, glm::radians(settings.rotation_degrees.z), glm::vec3(0, 0, 1));
		model = glm::scale(model, settings.scale);
		const glm::mat4 normal_matrix = glm::transpose(glm::inverse(model));

		const float yaw = glm::radians(settings.camera_yaw_degrees);
		const float pitch = glm::radians(settings.camera_pitch_degrees);
		const float r = settings.camera_distance;

		const glm::vec3 camera_pos = {
			r * std::cos(pitch) * std::sin(yaw),
			r * std::sin(pitch),
			r * std::cos(pitch) * std::cos(yaw),
		};

		glm::mat4 view = glm::lookAt(
			camera_pos,
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

		for (int i = 0; i < settings.spot_light_count; ++i) {
			if (glm::length(glm::vec3(settings.spot_lights[i].direction)) < 1e-4f) {
				settings.spot_lights[i].direction = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
			}
		}

		const size_t lights_size = sizeof(Settings::PointLight) * Settings::MAX_POINT_LIGHTS;
		std::memcpy(point_lights_buffer_mapped, settings.point_lights.data(), lights_size);

		const size_t spots_size =
			sizeof(Settings::SpotLight) * Settings::MAX_SPOT_LIGHTS;
		std::memcpy(spot_lights_buffer_mapped, settings.spot_lights.data(), spots_size);

		glm::vec3 safe_light_direction = settings.light_direction;
		if (glm::length(safe_light_direction) < 1e-4f) {
			safe_light_direction = glm::vec3(0.0f, -1.0f, 0.0f);
		}

		SceneUniforms scene = {};
		scene.view_projection = projection * view;
		scene.camera_position = glm::vec4(camera_pos, 1.0f);
		scene.directional_light_direction = glm::vec4(safe_light_direction, 0.0f);
		scene.directional_light_color = glm::vec4(settings.light_color, settings.light_intensity);
		scene.ambient_light = glm::vec4(settings.ambient_color, settings.ambient_intensity);
		scene.point_light_count = uint32_t(settings.point_light_count);
		scene.spot_light_count = uint32_t(settings.spot_light_count);

		std::memcpy(scene_uniform_buffer_mapped, &scene, sizeof(scene));

		ModelUniforms model_data = {};
		model_data.model = model;
		model_data.normal_matrix = normal_matrix;
		model_data.albedo = glm::vec4(settings.albedo, 1.0f);
		model_data.specular = glm::vec4(settings.specular_color, 1.0f);
		model_data.shininess = settings.shininess;

		std::memcpy(model_uniform_buffer_mapped, &model_data, sizeof(model_data));
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

		const VkDescriptorSet descriptor_sets[2] = {
			vk_scene_set,
			vk_model_set,
		};
		
		vkCmdBindDescriptorSets(fd.command_buffer,
								VK_PIPELINE_BIND_POINT_GRAPHICS,
								vk_pipeline_layout,
								0, 2, descriptor_sets,
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