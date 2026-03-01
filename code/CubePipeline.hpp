#pragma once

#include <vulkan/vulkan_core.h>

struct RTG;

struct CubePipeline {
	// Descriptor set layout for storage images (input, output)
	VkDescriptorSetLayout set0_layout = VK_NULL_HANDLE;

	// Push constants
	struct Push {
		uint32_t input_res;
		uint32_t output_res;
	};
	static_assert(sizeof(Push) == 8, "Push constants structure is packed");

	// Pipeline layout
	VkPipelineLayout layout = VK_NULL_HANDLE;

	// Compute pipeline handle
	VkPipeline handle = VK_NULL_HANDLE;

	void create(RTG& rtg);
	void destroy(RTG& rtg);
};
