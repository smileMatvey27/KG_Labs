#include "application.hpp"
#include "renderer.hpp"

#include <vulkan/vulkan.h>
#include <imgui.h>

namespace application {

bool initialize() {
	if (!renderer::initialize()) {
		return false;
	}
	return true;
}

void shutdown() {
	vkDeviceWaitIdle(graphics::internal::context.device);
	renderer::shutdown();
}

void update(double time) {
	renderer::update(time);
	ImGui::ShowDemoWindow();
}

void render(const graphics::internal::FrameData& fd) {
	renderer::render(fd);
}

}