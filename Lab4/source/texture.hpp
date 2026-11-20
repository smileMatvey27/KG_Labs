#pragma once

#include <string>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace texture {

struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

bool load(const std::string& path, Texture& out_texture);

void destroy(Texture& texture);

}