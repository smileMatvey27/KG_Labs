#include "texture.hpp"

#include <iostream>
#include <vector>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "graphics_internal.hpp"

namespace texture {

    namespace {

        VkCommandBuffer beginSingleTimeCommands(VkCommandPool pool) {
            const VkCommandBufferAllocateInfo alloc_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = pool,
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

        void endSingleTimeCommands(VkCommandPool pool, VkCommandBuffer cmd) {
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
                                pool, 1, &cmd);
        }

        void transitionImageLayout(VkCommandBuffer cmd,
                                VkImage image,
                                VkImageLayout old_layout,
                                VkImageLayout new_layout) {
            VkImageMemoryBarrier barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = old_layout,
                .newLayout = new_layout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            };

            VkPipelineStageFlags src_stage;
            VkPipelineStageFlags dst_stage;

            if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
                new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            }
            else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                    new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }
            else {
                std::cerr << "[texture] Unsupported layout transition\n";
                return;
            }

            vkCmdPipelineBarrier(cmd,
                                src_stage, dst_stage,
                                0,
                                0, nullptr,
                                0, nullptr,
                                1, &barrier);
        }

    }

    bool load(const std::string& path, Texture& out_texture) {
        int width = 0, height = 0, channels = 0;
        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr) {
            std::cerr << "[texture] Failed to load texture: " << path << "\n";
            return false;
        }

        const VkDeviceSize image_size = VkDeviceSize(width) * VkDeviceSize(height) * 4;

        std::cerr << "[texture] Loaded " << path << " ("
                << width << "x" << height << ", "
                << channels << " channels)\n";

        const VkImageCreateInfo image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = {
                .width = uint32_t(width),
                .height = uint32_t(height),
                .depth = 1,
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        const VmaAllocationCreateInfo image_alloc = {
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 
        };

        if (vmaCreateImage(graphics::internal::context.allocator,
                        &image_info, &image_alloc,
                        &out_texture.image, &out_texture.allocation,
                        nullptr) != VK_SUCCESS) {
            std::cerr << "[texture] Failed to create VkImage\n";
            stbi_image_free(pixels);
            return false;
        }

        VkBuffer staging_buffer = VK_NULL_HANDLE;
        VmaAllocation staging_alloc = VK_NULL_HANDLE;

        const VkBufferCreateInfo staging_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = image_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        const VmaAllocationCreateInfo staging_alloc_info = {
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };

        VmaAllocationInfo staging_alloc_result = {};

        if (vmaCreateBuffer(graphics::internal::context.allocator,
                            &staging_info, &staging_alloc_info,
                            &staging_buffer, &staging_alloc,
                            &staging_alloc_result) != VK_SUCCESS) {
            std::cerr << "[texture] Failed to create staging buffer\n";
            stbi_image_free(pixels);
            return false;
        }

        std::memcpy(staging_alloc_result.pMappedData, pixels, size_t(image_size));
        stbi_image_free(pixels);

        const VkCommandPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = graphics::internal::context.graphics_queue_index,
        };

        VkCommandPool cmd_pool = VK_NULL_HANDLE;
        vkCreateCommandPool(graphics::internal::context.device,
                            &pool_info, nullptr, &cmd_pool);

        const VkCommandBuffer cmd = beginSingleTimeCommands(cmd_pool);

        transitionImageLayout(cmd, out_texture.image,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        const VkBufferImageCopy copy_region = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = {
                .width = uint32_t(width),
                .height = uint32_t(height),
                .depth = 1,
            },
        };

        vkCmdCopyBufferToImage(cmd, staging_buffer, out_texture.image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1, &copy_region);

        transitionImageLayout(cmd, out_texture.image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        endSingleTimeCommands(cmd_pool, cmd);

        vmaDestroyBuffer(graphics::internal::context.allocator,
                        staging_buffer, staging_alloc);
        vkDestroyCommandPool(graphics::internal::context.device,
                            cmd_pool, nullptr);

        const VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = out_texture.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        if (vkCreateImageView(graphics::internal::context.device,
                            &view_info, nullptr,
                            &out_texture.image_view) != VK_SUCCESS) {
            std::cerr << "[texture] Failed to create image view\n";
            return false;
        }

        const VkSamplerCreateInfo sampler_info = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1.0f,
            .compareEnable = VK_FALSE,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };

        if (vkCreateSampler(graphics::internal::context.device,
                            &sampler_info, nullptr,
                            &out_texture.sampler) != VK_SUCCESS) {
            std::cerr << "[texture] Failed to create sampler\n";
            return false;
        }

        std::cerr << "[texture] Successfully loaded: " << path << "\n";
        return true;
    }

    void destroy(Texture& texture) {
        const VkDevice device = graphics::internal::context.device;

        if (texture.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, texture.sampler, nullptr);
            texture.sampler = VK_NULL_HANDLE;
        }
        if (texture.image_view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, texture.image_view, nullptr);
            texture.image_view = VK_NULL_HANDLE;
        }
        if (texture.image != VK_NULL_HANDLE) {
            vmaDestroyImage(graphics::internal::context.allocator,
                            texture.image, texture.allocation);
            texture.image = VK_NULL_HANDLE;
            texture.allocation = VK_NULL_HANDLE;
        }
    }
}