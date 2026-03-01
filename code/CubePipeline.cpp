#include "CubePipeline.hpp"
#include "RTG.hpp"
#include "Helpers.hpp"
#include "VK.hpp"

static uint32_t comp_lambertian[] =
#include "spv/cube.comp.lambertian.inl"
;

void CubePipeline::create(RTG& rtg) {
	VkShaderModule comp_module = rtg.helpers.create_shader_module(comp_lambertian);

	// Create pipeline layout with descriptor set layout and push constants
	{
		std::array<VkDescriptorSetLayoutBinding, 2> bindings {
			// Input cubemap (storage image)
			VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = nullptr,
			},
			// Output cubemap (storage image)
			VkDescriptorSetLayoutBinding{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = nullptr,
			},
		};

		VkDescriptorSetLayoutCreateInfo layout_create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = uint32_t(bindings.size()),
			.pBindings = bindings.data(),
		};

		VK(vkCreateDescriptorSetLayout(rtg.device, &layout_create_info, nullptr, &set0_layout));
	}

	// Create push constant range
	VkPushConstantRange push_constant_range{
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.offset = 0,
		.size = sizeof(Push),
	};

	// Create pipeline layout
	{
		VkPipelineLayoutCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
			.pSetLayouts = &set0_layout,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &push_constant_range,
		};

		VK(vkCreatePipelineLayout(rtg.device, &create_info, nullptr, &layout));
	}

	// Create compute pipeline
	VkPipelineShaderStageCreateInfo shader_stage {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = comp_module,
		.pName = "main",
	};

	VkComputePipelineCreateInfo create_info {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = shader_stage,
		.layout = layout,
	};

	VK(vkCreateComputePipelines(rtg.device, VK_NULL_HANDLE, 1, &create_info, nullptr, &handle));

	// Clean up shader module
	vkDestroyShaderModule(rtg.device, comp_module, nullptr);
}

void CubePipeline::destroy(RTG& rtg) {
	if (handle != VK_NULL_HANDLE) {
		vkDestroyPipeline(rtg.device, handle, nullptr);
		handle = VK_NULL_HANDLE;
	}
	if (layout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(rtg.device, layout, nullptr);
		layout = VK_NULL_HANDLE;
	}
	if (set0_layout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(rtg.device, set0_layout, nullptr);
		set0_layout = VK_NULL_HANDLE;
	}
}
