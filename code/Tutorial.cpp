#include "Tutorial.hpp"

#include "VK.hpp"
#include "S72.hpp"
#include "Timer.hpp"
//#include "refsol.hpp"

#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image/stb_image.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <fstream>
#include <numeric>
#include <memory>
#include <algorithm>

void flip_image_y_inplace_rgba(uint8_t* pixels, int width, int height)
{
    const int rowSize = width * 4; // 4 bytes per pixel (RGBA)

    std::vector<uint8_t> tmp(rowSize); // temp row buffer

    for (int y = 0; y < height / 2; ++y) {
        uint8_t* rowTop    = pixels + y * rowSize;
        uint8_t* rowBottom = pixels + (height - 1 - y) * rowSize;

        // swap rows
        std::memcpy(tmp.data(),   rowTop,    rowSize);
        std::memcpy(rowTop,       rowBottom, rowSize);
        std::memcpy(rowBottom,    tmp.data(), rowSize);
    }
}

Tutorial::Tutorial(RTG &rtg_) : rtg(rtg_) {
	//refsol::Tutorial_constructor(rtg, &depth_format, &render_pass, &command_pool);
	viewport_rect = {0, 0, rtg.configuration.surface_extent.width, rtg.configuration.surface_extent.height};
	//Scene Graph
	try {
		s72 = S72::load(rtg.configuration.scene_file);
	} catch (std::exception &e) {
		std::cerr << "Failed to load s72-format scene from '" << rtg.configuration.scene_file << "':\n" << e.what() << std::endl;
	}

	if(rtg.configuration.print){
		//print out some scene information:
		rtg.helpers.print_scene_info(s72);
		rtg.helpers.print_scene_graph(s72);
	}

	if(rtg.configuration.camera_name != "") {
		auto it = s72.cameras.find(rtg.configuration.camera_name);
		if (it != s72.cameras.end()) {
			camera_mode = CameraMode::Scene;
			cur_scene_camera = &it->second;
		} else {
			throw std::runtime_error("Scene camera named " + rtg.configuration.camera_name + " not found");
		}
	}

	// Initialize culling mode from configuration
	{
		std::string c = rtg.configuration.culling;
		if (c == "none" || c == "") {
			culling_mode = CullingMode::None;
		} else if (c == "frustum") {
			culling_mode = CullingMode::Frustum;
		} else {
			throw std::runtime_error("Unrecognized culling mode: " + c);
		}
	}

	{
		world.SKY_ENERGY.r = 0.0f;
		world.SKY_ENERGY.g = 0.0f;
		world.SKY_ENERGY.b = 0.0f;
		// world.SKY_DIRECTION.x = 0.0f;
		// world.SKY_DIRECTION.y = 0.0f;
		// world.SKY_DIRECTION.z = 1.0f;
		world.SUN_ENERGY.r = 0.0f;
		world.SUN_ENERGY.g = 0.0f;
		world.SUN_ENERGY.b = 0.0f;
		// world.SUN_DIRECTION.x = 0.0f;
		// world.SUN_DIRECTION.y = 0.0f;
		// world.SUN_DIRECTION.z = 1.0f;
	}

	//select a depth format:
	depth_format = rtg.helpers.find_image_format(
		{VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32},
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);
	{//create render pass
		//attachemnts
		std::array<VkAttachmentDescription, 2> attachments {
			VkAttachmentDescription{ //color attachment
				.format = rtg.surface_format.format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = rtg.present_layout,
			},
			VkAttachmentDescription { //depth
			.format = depth_format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			},		
		};
		//subpass
		VkAttachmentReference color_attachment_ref {
			.attachment = 0,
			.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		};
		VkAttachmentReference depth_attachment_ref {
			.attachment = 1,
			.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};
		VkSubpassDescription subpass{
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount = 0,
			.pInputAttachments = nullptr,
			.colorAttachmentCount = 1,
			.pColorAttachments = &color_attachment_ref,
			.pDepthStencilAttachment = &depth_attachment_ref,
		};
		//dependencies
		std::array<VkSubpassDependency, 2> dependencies {
			VkSubpassDependency{
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			},
			VkSubpassDependency{
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			},
		};
		VkRenderPassCreateInfo create_info {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = uint32_t(attachments.size()),
			.pAttachments = attachments.data(),
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = uint32_t(dependencies.size()),
			.pDependencies = dependencies.data(),
		};
		VK(vkCreateRenderPass(rtg.device, &create_info, nullptr, &render_pass));
	}

	{//create command pool
		VkCommandPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = rtg.graphics_queue_family.value(),
		};
		VK(vkCreateCommandPool(rtg.device, &create_info, nullptr, &command_pool));
	}

	background_pipeline.create(rtg, render_pass, 0);
	lines_pipeline.create(rtg, render_pass, 0);
	objects_pipeline.create(rtg, render_pass, 0);

	{//create descriptor pool:
		uint32_t per_workspace = uint32_t(rtg.workspaces.size());
		std::array<VkDescriptorPoolSize, 2> pool_sizes {
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 2 * per_workspace,
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 2 * per_workspace,
			},
		};

		VkDescriptorPoolCreateInfo create_info {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0,
			.maxSets = 4 * per_workspace,
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool));
	}


	workspaces.resize(rtg.workspaces.size());
	for (Workspace &workspace : workspaces) {
		//refsol::Tutorial_constructor_workspace(rtg, command_pool, &workspace.command_buffer);
		{//allocate command buffer:
			VkCommandBufferAllocateInfo alloc_info {
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = command_pool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1,
			};
			VK(vkAllocateCommandBuffers(rtg.device, &alloc_info, &workspace.command_buffer));
		}

		{ //camera
			workspace.Camera_src = rtg.helpers.create_buffer(
				sizeof(LinesPipeline::Camera),
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				Helpers::Mapped
			);

			workspace.Camera = rtg.helpers.create_buffer(
					sizeof(LinesPipeline::Camera),
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					Helpers::Unmapped
			);
			//Descriptor set
			{
				VkDescriptorSetAllocateInfo alloc_info {
					.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
					.descriptorPool = descriptor_pool,
					.descriptorSetCount = 1,
					.pSetLayouts = &lines_pipeline.set0_Camera,
				};

				VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Camera_descriptors));
			}
			{
				VkDescriptorSetAllocateInfo alloc_info {
					.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
					.descriptorPool = descriptor_pool,
					.descriptorSetCount = 1,
					.pSetLayouts = &objects_pipeline.set1_Transforms,
				};

				VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Transforms_descriptors));
			}
		}

		{//World
			workspace.World_src = rtg.helpers.create_buffer(
				sizeof(ObjectsPipeline::World),
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				Helpers::Mapped
			);

			workspace.World = rtg.helpers.create_buffer(
					sizeof(ObjectsPipeline::World),
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					Helpers::Unmapped
			);
			//Descriptor set
			{
				VkDescriptorSetAllocateInfo alloc_info {
					.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
					.descriptorPool = descriptor_pool,
					.descriptorSetCount = 1,
					.pSetLayouts = &objects_pipeline.set0_World,
				};

				VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.World_descriptors));
			}
		}	
		
		{//Material
			size_t matCount = std::max(size_t(1), s72.materials.size());
			workspace.Material_src = rtg.helpers.create_buffer(
					sizeof(ObjectsPipeline::Material) * matCount,
					VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					Helpers::Mapped
			);

			workspace.Material = rtg.helpers.create_buffer(
				sizeof(ObjectsPipeline::Material) * matCount,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			);

			// rtg.helpers.transfer_to_buffer(materials.data(), bytes, Material);

			VkDescriptorSetAllocateInfo alloc_info {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &objects_pipeline.set3_Material,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Material_descriptors));
		}

		//descriptor write
		{
			VkDescriptorBufferInfo Camera_info{
				.buffer = workspace.Camera.handle,
				.offset = 0,
				.range = workspace.Camera.size,
			};
			VkDescriptorBufferInfo World_info{
					.buffer = workspace.World.handle,
					.offset = 0,
					.range = workspace.World.size,
			};

			VkDescriptorBufferInfo Material_info{
				.buffer = workspace.Material.handle,
				.offset = 0,
				.range = workspace.Material.size,
			};

			std::array<VkWriteDescriptorSet, 3> writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Camera_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &Camera_info,
				},
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.World_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &World_info,
				},
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Material_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &Material_info,
				},
			};

			vkUpdateDescriptorSets(
				rtg.device,
				uint32_t(writes.size()),
				writes.data(),
				0, //descriptor copy count
				nullptr
			);
		}
	}

	// Create a query pool for GPU timestamp measurements (two timestamps per workspace)
	if (!workspaces.empty()) {
		VkQueryPoolCreateInfo qp_info{
			.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.queryType = VK_QUERY_TYPE_TIMESTAMP,
			.queryCount = uint32_t(workspaces.size() * 2),
			.pipelineStatistics = 0,
		};
		VK(vkCreateQueryPool(rtg.device, &qp_info, nullptr, &query_pool));

		// assign query indices per workspace
		for (size_t i = 0; i < workspaces.size(); ++i) {
			workspaces[i].query_index = uint32_t(i * 2);
		}

		// get timestampPeriod from physical device limits
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(rtg.physical_device, &props);
		timestampPeriodNs = double(props.limits.timestampPeriod);
	}

	{//Material
		if (s72.materials.size() > 0) {
			materials.assign(s72.materials.size(), ObjectsPipeline::Material{
			.albedo = vec4(1.0f, 0.0f, 1.0f, 0.0f), // test purple color
			.brdf = ObjectsPipeline::BRDFType::BRDF_LAMBERTIAN,
			});
			for (auto const &pair : s72.materials) {
				const S72::Material& mat = pair.second; 
				ObjectsPipeline::Material& mat_out = materials[mat.index];
				if (auto* p = std::get_if<S72::Material::PBR>(&mat.brdf)) {
					mat_out.brdf = ObjectsPipeline::BRDFType::BRDF_PBR;
					//TODO
				} else if (auto* l = std::get_if<S72::Material::Lambertian>(&mat.brdf)) {
					mat_out.brdf = ObjectsPipeline::BRDFType::BRDF_LAMBERTIAN;
					if (auto* albedo =  std::get_if<S72::color>(&l->albedo)) {
						mat_out.albedo = vec4(albedo->r, albedo->g, albedo->b, 1.0f);
					} else if (auto* ptr_albedoTex =  std::get_if<S72::Texture*>(&l->albedo)){
						mat_out.hasAlbedoTex = 1;
						mat_out.albedoTexIndex = 0; //TODO: Set tex id
					}
				} else if (auto* m = std::get_if<S72::Material::Mirror>(&mat.brdf)) {
					mat_out.brdf = ObjectsPipeline::BRDFType::BRDF_MIRROR;
					//TODO
				} else if (auto* e = std::get_if<S72::Material::Environment>(&mat.brdf)) {
					mat_out.brdf = ObjectsPipeline::BRDFType::BRDF_ENVIRONMENT;
					//TODO
				}
			}
		} else {
			materials.emplace_back(ObjectsPipeline::Material{
				.albedo = vec4(0.8f, 0.8f, 0.8f, 0.0f), // default grey color
				.brdf = ObjectsPipeline::BRDFType::BRDF_LAMBERTIAN,
			});
		}
		
		
	}

	{ //objects
		std::vector< PosNorTanTexVertex > vertices;
		uint32_t first_offset = 0;
		for (auto const &pair : s72.meshes) {
			const std::string& mesh_name = pair.first;
        	const S72::Mesh&   mesh      = pair.second;
			ObjectVertices obj_vertices{
				.first = first_offset,
				.count = mesh.count,
			};
			std::cout << "Parsing mesh: " << mesh_name << std::endl;
			//read attributes
			auto getAttribute = [&mesh](const std::string& _name) -> const S72::Mesh::Attribute& {
				auto it = mesh.attributes.find(_name);
				if (it != mesh.attributes.end()) {
					return it->second;
				} else {
					throw std::runtime_error("Attribute not found: " + mesh.name + ": " +  _name);
				}
			};
			const auto& position = getAttribute("POSITION");
			// S72::Mesh::Attribute normal = mesh.attributes["NORMAL"];
			// S72::Mesh::Attribute tangent = mesh.attributes["TANGENT"];
			// S72::Mesh::Attribute texCoord = mesh.attributes["TEXCOORD"];
			std::ifstream in(position.src.path, std::ios::binary);
			if (!in) throw std::runtime_error("Failed to open data file: " + position.src.path);
			//std::cout << "position info" << position.src.path << " stride: " << position.stride << position.offset << std::endl;
			for (uint32_t i = 0; i < mesh.count; ++i) {
				vertices.emplace_back(); 
				assert(vertices.size() == (first_offset + i + 1));
				PosNorTanTexVertex& v = vertices[first_offset + i];
				// POSITION: R32G32B32_SFLOAT, offset 0, stride 48 (example)
				in.seekg(i * position.stride + position.offset, std::ios::beg);
				in.read(reinterpret_cast<char*>(&v.Position), sizeof(v.Position));
				obj_vertices.min_aabb_bound.x = std::min(obj_vertices.min_aabb_bound.x, v.Position.x);
				obj_vertices.min_aabb_bound.y = std::min(obj_vertices.min_aabb_bound.y, v.Position.y);
				obj_vertices.min_aabb_bound.z = std::min(obj_vertices.min_aabb_bound.z, v.Position.z);

				obj_vertices.max_aabb_bound.x = std::max(obj_vertices.max_aabb_bound.x, v.Position.x);
				obj_vertices.max_aabb_bound.y = std::max(obj_vertices.max_aabb_bound.y, v.Position.y);
				obj_vertices.max_aabb_bound.z = std::max(obj_vertices.max_aabb_bound.z, v.Position.z);
				//std::cout << "Vertex " << i << " position: " << v.Position.x << v.Position.y << v.Position.z << std::endl;
				// NORMAL
				// in.seekg(i * normal.stride + normal.offset, std::ios::beg);
				in.read(reinterpret_cast<char*>(&v.Normal), sizeof(v.Normal));
				// TANGENT: R32G32B32A32_SFLOAT, offset 24, stride 48
				// in.seekg(i * tangent.stride + tangent.offset, std::ios::beg);
				in.read(reinterpret_cast<char*>(&v.Tangent), sizeof(v.Tangent));
				// TEXCOORD: R32G32_SFLOAT, offset 40, stride 48
				// in.seekg(i * texCoord.stride + texCoord.offset, std::ios::beg);
				in.read(reinterpret_cast<char*>(&v.TexCoord), sizeof(v.TexCoord));
			}
			first_offset += mesh.count;
			object_vertices_list.emplace(mesh_name, obj_vertices); 
		}
		
		size_t bytes = vertices.size() * sizeof(vertices[0]);

		object_vertices = rtg.helpers.create_buffer(
			bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		rtg.helpers.transfer_to_buffer(vertices.data(), bytes, object_vertices);
	}

	{//texture
		{ //make some textures
			textures.reserve(1);
			if (!s72.textures.empty()) {
				for (const auto& pair : s72.textures) {
					// const std::string& tex_name = pair.first;
					const S72::Texture& tex = pair.second;
					int width, height, channels;
					// int ok = stbi_info(tex.path, &width, &height, &channels);
					unsigned char* data = stbi_load(tex.path.c_str(), &width, &height, &channels, 4);
					if (!data) {
						throw std::runtime_error("Failed to load texture image: " + tex.path);
					}
					// std::cout << "Texture size: " << width << "x" << height << " channels: " << channels << std::endl;
					flip_image_y_inplace_rgba(data, width, height);
					textures.emplace_back(rtg.helpers.create_image(
						VkExtent2D{.width = uint32_t(width), .height = uint32_t(height)},
						VK_FORMAT_R8G8B8A8_SRGB,
						VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
						Helpers::Unmapped
					));

					size_t image_size = width * height * 4; // 4 bytes per pixel (RGBA)
					rtg.helpers.transfer_to_image(data, image_size, textures.back());

					stbi_image_free(data);
				}
			} else {
				//dark grey / light grey checkerboard with a red square at the origin
				//make texture
				uint32_t size = 128;
				std::vector<uint32_t> data;
				data.reserve(size*size);
				for (uint32_t y = 0; y < size; ++y) {
					float fy = (y + 0.5f) / float(size);
					for (uint32_t x = 0; x < size; ++x) {
						float fx = (x + 0.5f) / float(size);
						if (fx < 0.05f && fy < 0.05f) {
							data.emplace_back(0xff0000ff); //red
						}
						else if ((fx < 0.5f) == (fy < 0.5f)) {
							data.emplace_back(0xff444444); //darkgery
						}
						else {
							data.emplace_back(0xffbbbbbb); //lightgrey
						}
					}
				}
				assert(data.size() == size*size);
				
				//texture in GPU
				textures.emplace_back(rtg.helpers.create_image(
					VkExtent2D{.width = size, .height = size},
					VK_FORMAT_R8G8B8A8_UNORM,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					Helpers::Unmapped
				));

				//transfer data
				rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
			}
			
			
		}

		{ //make image views for the textures
			texture_views.reserve(textures.size());
			for(Helpers::AllocatedImage const &image : textures) {
				VkImageViewCreateInfo create_info {
					.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					.flags = 0,
					.image = image.handle,
					.viewType = VK_IMAGE_VIEW_TYPE_2D,
					.format = image.format,
					//.components
					.subresourceRange{
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = 1,
					},
				};

				VkImageView image_view = VK_NULL_HANDLE;
				VK(vkCreateImageView(rtg.device, &create_info, nullptr, &image_view));

				texture_views.emplace_back(image_view);
			}
			assert(texture_views.size() == textures.size());
		}

		{ //make a sampler for the textures
			VkSamplerCreateInfo create_info {
				.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
				.flags = 0,
				.magFilter = VK_FILTER_NEAREST,
				.minFilter = VK_FILTER_NEAREST,
				.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
				.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
				.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
				.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
				.mipLodBias = 0.0f,
				.anisotropyEnable = VK_FALSE,
				.maxAnisotropy = 0.0f,
				.compareEnable = VK_FALSE,
				.compareOp = VK_COMPARE_OP_ALWAYS,
				.minLod = 0.0f,
				.maxLod = 0.0f,
				.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
				.unnormalizedCoordinates = VK_FALSE,
			};

			VK(vkCreateSampler(rtg.device, &create_info, nullptr, &texture_sampler));
		}
			
		{//create texture descriptor pool:
			uint32_t per_texture = uint32_t(textures.size());
			std::array<VkDescriptorPoolSize, 1> pool_sizes {
				VkDescriptorPoolSize{
					.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1 * 1 * per_texture, //1 descriptor per set, 1 set per texture
				},
			};

			VkDescriptorPoolCreateInfo create_info {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
				.flags = 0,
				.maxSets = 1 * per_texture,
				.poolSizeCount = uint32_t(pool_sizes.size()),
				.pPoolSizes = pool_sizes.data(),
			};

			VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &texture_descriptor_pool));
		}

		{//allocate and write the texture descriptor sets
			VkDescriptorSetAllocateInfo alloc_info {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = texture_descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &objects_pipeline.set2_TEXTURE,
			};
			texture_descriptors.assign(textures.size(), VK_NULL_HANDLE);
			for (VkDescriptorSet &descriptor_set : texture_descriptors) {
				VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &descriptor_set));
			}

			//write
			std::vector<VkDescriptorImageInfo> infos(textures.size());
			std::vector<VkWriteDescriptorSet> writes(textures.size());

			for(Helpers::AllocatedImage const &image : textures) {
				size_t i = &image - &textures[0];
				infos[i] = VkDescriptorImageInfo {
					.sampler = texture_sampler,
					.imageView = texture_views[i],
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				};
				writes[i] = VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = texture_descriptors[i],
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = &infos[i],
				};
			}

			vkUpdateDescriptorSets(
				rtg.device,
				uint32_t(writes.size()),
				writes.data(),
				0, //descriptor copy count
				nullptr
			);

		}		
	}

}

Tutorial::~Tutorial() {
	//just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS) {
		std::cerr << "Failed to vkDeviceWaitIdle in Tutorial::~Tutorial [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
	}

	//texture
	if(texture_descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, texture_descriptor_pool, nullptr);
		texture_descriptor_pool = nullptr;

		texture_descriptors.clear();
	}
	if(texture_sampler) {
		vkDestroySampler(rtg.device, texture_sampler, nullptr);
		texture_sampler = VK_NULL_HANDLE;
	}
	for(VkImageView &view : texture_views) {
		vkDestroyImageView(rtg.device, view, nullptr);
		view = VK_NULL_HANDLE;
	}
	texture_views.clear();
	for(auto &texture : textures) {
		rtg.helpers.destroy_image(std::move(texture));
	}
	textures.clear();

	rtg.helpers.destroy_buffer(std::move(object_vertices));

	if (swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}

	for (Workspace &workspace : workspaces) {
		//refsol::Tutorial_destructor_workspace(rtg, command_pool, &workspace.command_buffer);
		if(workspace.command_buffer != VK_NULL_HANDLE) {
		vkFreeCommandBuffers(rtg.device, command_pool, 1, &workspace.command_buffer);
		workspace.command_buffer = VK_NULL_HANDLE;
	}

		if(workspace.lines_vertices_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices_src));
		}
		if(workspace.lines_vertices.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices));
		}

		if(workspace.Camera_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Camera_src));
		}
		if(workspace.Camera.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Camera));
		}
		if(workspace.World_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.World_src));
		}
		if(workspace.World.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.World));
		}
		if(workspace.Transforms_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
		}
		if(workspace.Transforms.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
		}
		if(workspace.Material_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Material_src));
		}
		if(workspace.Material.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Material));
		}
	}
	workspaces.clear();

	background_pipeline.destroy(rtg);
	lines_pipeline.destroy(rtg);
	objects_pipeline.destroy(rtg);

	if (query_pool) {
		vkDestroyQueryPool(rtg.device, query_pool, nullptr);
		query_pool = VK_NULL_HANDLE;
	}

	// Export collected stats to CSV
	if (!stats_map.empty()) {
		std::ofstream csv("frame_times.csv");
		if (csv) {
			csv << "frame,cpu_ms,gpu_us\n";
			// sort by frame index
			std::vector<uint64_t> frames;
			frames.reserve(stats_map.size());
			for (auto const &p : stats_map) frames.push_back(p.first);
			std::sort(frames.begin(), frames.end());
			for (uint64_t f : frames) {
				FrameStats const &fs = stats_map[f];
				csv << fs.frame << "," << fs.cpu << "," << fs.gpu << "\n";
			}
			csv.close();
			std::cout << "Wrote frame_times.csv (" << stats_map.size() << " entries)" << std::endl;
		} else {
			std::cerr << "Failed to open frame_times.csv for writing." << std::endl;
		}
	}

	if(descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, descriptor_pool, nullptr);
		descriptor_pool = nullptr;
	}

	//refsol::Tutorial_destructor(rtg, &render_pass, &command_pool);
	if(render_pass != VK_NULL_HANDLE) {
		vkDestroyRenderPass(rtg.device, render_pass, nullptr);
		render_pass = VK_NULL_HANDLE;
	}
	if(command_pool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(rtg.device, command_pool, nullptr);
		command_pool = VK_NULL_HANDLE;
	}
}

void Tutorial::on_swapchain(RTG &rtg_, RTG::SwapchainEvent const &swapchain) {
	//[re]create framebuffers:
	//refsol::Tutorial_on_swapchain(rtg, swapchain, depth_format, render_pass, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
	//Clean up existing framebuffers
	if(swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}
	//allocate depth image for framebuffers to share
	swapchain_depth_image = rtg.helpers.create_image(
		swapchain.extent,
		depth_format,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		Helpers::Unmapped
	);
	{//create an image view of the depth images
		VkImageViewCreateInfo create_info {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchain_depth_image.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = depth_format,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
		};
		VK(vkCreateImageView(rtg.device, &create_info, nullptr, &swapchain_depth_image_view));	
	}

	//create framebuffers pointing to each swapchain image view and the shared depth image view
	swapchain_framebuffers.assign(swapchain.image_views.size(), VK_NULL_HANDLE);
	for(size_t i = 0; i < swapchain.image_views.size(); ++i) {
		std::array<VkImageView, 2> attachments {
			swapchain.image_views[i],
			swapchain_depth_image_view,
		};
		VkFramebufferCreateInfo create_info {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = render_pass,
			.attachmentCount = uint32_t(attachments.size()),
			.pAttachments = attachments.data(),
			.width = swapchain.extent.width,
			.height = swapchain.extent.height,
			.layers = 1,
		};
		VK(vkCreateFramebuffer(rtg.device, &create_info, nullptr, &swapchain_framebuffers[i]));
	}
}

void Tutorial::destroy_framebuffers() {
	//refsol::Tutorial_destroy_framebuffers(rtg, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
	for (VkFramebuffer &framebuffer : swapchain_framebuffers) {
		assert(framebuffer != VK_NULL_HANDLE);
		vkDestroyFramebuffer(rtg.device, framebuffer, nullptr);
		framebuffer = VK_NULL_HANDLE;
	}
	swapchain_framebuffers.clear();
	assert(swapchain_depth_image_view != VK_NULL_HANDLE);
	vkDestroyImageView(rtg.device, swapchain_depth_image_view, nullptr);
	swapchain_depth_image_view = VK_NULL_HANDLE;
	rtg.helpers.destroy_image(std::move(swapchain_depth_image));
}


void Tutorial::render(RTG &rtg_, RTG::RenderParams const &render_params) {
	//assert that parameters are valid:
	assert(&rtg == &rtg_);
	assert(render_params.workspace_index < workspaces.size());
	assert(render_params.image_index < swapchain_framebuffers.size());

	//get more convenient names for the current workspace and target framebuffer:
	Workspace &workspace = workspaces[render_params.workspace_index];
	VkFramebuffer framebuffer = swapchain_framebuffers[render_params.image_index];

	// start-of-frame bookkeeping: increment frame counter and create a Timer
	static std::unique_ptr< Timer > timer;
	uint64_t this_frame = ++frame_counter;
	timer.reset(new Timer([this, this_frame](double elapsed){
		FrameStats &fs = stats_map[this_frame];
		fs.frame = this_frame;
		fs.cpu = elapsed * 1e3;
		cpu_times.push_back((elapsed * 1e3));
		if (cpu_times.size() > 1000) cpu_times.erase(cpu_times.begin(), cpu_times.begin() + (cpu_times.size() - 1000));
	}));

	//record (into `workspace.command_buffer`) commands that run a `render_pass` that just clears `framebuffer`:
	//refsol::Tutorial_render_record_blank_frame(rtg, render_pass, framebuffer, &workspace.command_buffer);

	//reset the command buffer
	VK(vkResetCommandBuffer(workspace.command_buffer, 0));
	{ // begin recording
		VkCommandBufferBeginInfo begin_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.pNext = nullptr,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,

		};
		VK(vkBeginCommandBuffer(workspace.command_buffer, &begin_info));
	}

	// reset and write a start timestamp for GPU timing (two queries per workspace)
	if (query_pool != VK_NULL_HANDLE) {
		vkCmdResetQueryPool(workspace.command_buffer, query_pool, workspace.query_index, 2);
		vkCmdWriteTimestamp(workspace.command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool, workspace.query_index);
		// associate this workspace query with the current global frame index
		query_to_frame[workspace.query_index] = this_frame;
	}

	if (!lines_vertices.empty()) {
		size_t needed_bytes = lines_vertices.size() * sizeof(lines_vertices[0]);
		if(workspace.lines_vertices_src.handle == VK_NULL_HANDLE || workspace.lines_vertices_src.size < needed_bytes) {
			size_t new_bytes = ((needed_bytes + 4096) / 4096) * 4096;
			if(workspace.lines_vertices_src.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices_src));
			}

			if(workspace.lines_vertices.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices));
			}

			workspace.lines_vertices_src = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				Helpers::Mapped
			);

			workspace.lines_vertices = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			);

			std::cout << "Re-allocated lines buffer to " << new_bytes << " bytes." << std::endl;
		}

		assert(workspace.lines_vertices_src.size == workspace.lines_vertices.size);
		assert(workspace.lines_vertices_src.size >= needed_bytes);

		assert(workspace.lines_vertices_src.allocation.mapped);
		std::memcpy(workspace.lines_vertices_src.allocation.data(), lines_vertices.data(), needed_bytes);

		VkBufferCopy copy_region {
			.srcOffset = 0,
			.dstOffset = 0,
			.size = needed_bytes,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.lines_vertices_src.handle, workspace.lines_vertices.handle, 1, &copy_region);
	}

	{ // camera info
		LinesPipeline::Camera camera{
			.CLIP_FROM_WORLD = CLIP_FROM_WORLD,
		};
		assert(workspace.Camera_src.size == sizeof(camera));

		memcpy(workspace.Camera_src.allocation.data(), &camera, sizeof(camera));

		assert(workspace.Camera_src.size == workspace.Camera.size);
		VkBufferCopy copy_region {
			.srcOffset = 0,
			.dstOffset = 0,
			.size = workspace.Camera_src.size,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.Camera_src.handle, workspace.Camera.handle, 1, &copy_region);
	}

	{ //upload world info:
		assert(workspace.World_src.size == sizeof(world)); //TODO:Check werid

		//host-side copy into World_src:
		memcpy(workspace.World_src.allocation.data(), &world, sizeof(world));

		//add device-side copy from World_src -> World:
		assert(workspace.World_src.size == workspace.World.size);
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = workspace.World_src.size,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.World_src.handle, workspace.World.handle, 1, &copy_region);
	}

	{//update material info
		assert(workspace.Material_src.size == materials.size() * sizeof(ObjectsPipeline::Material)); //TODO:Check werid

		//host-side copy into Material_src:
		// memcpy(workspace.Material_src.allocation.data(), &materials, sizeof(workspace.Material));
		ObjectsPipeline::Material *out = reinterpret_cast<ObjectsPipeline::Material*>(workspace.Material_src.allocation.data());
		for(ObjectsPipeline::Material const &inst : materials) {
			*out = inst;
			++out;
		}

		//add device-side copy from Material_src -> Material:
		assert(workspace.Material_src.size == workspace.Material.size);
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = workspace.Material_src.size,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.Material_src.handle, workspace.Material.handle, 1, &copy_region);
	}

	if (!object_instances.empty()) {
		size_t needed_bytes = object_instances.size() * sizeof(ObjectsPipeline::Transform);
		if(workspace.Transforms_src.handle == VK_NULL_HANDLE || workspace.Transforms_src.size < needed_bytes) {
			size_t new_bytes = ((needed_bytes + 4096) / 4096) * 4096;
			if(workspace.Transforms_src.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
			}

			if(workspace.Transforms.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
			}

			workspace.Transforms_src = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				Helpers::Mapped
			);

			workspace.Transforms = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			);

			VkDescriptorBufferInfo Transforms_info{
				.buffer = workspace.Transforms.handle,
				.offset = 0,
				.range = workspace.Transforms.size,
			};

			std::array<VkWriteDescriptorSet, 1> writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Transforms_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &Transforms_info,
				},
			};

			vkUpdateDescriptorSets(
				rtg.device,
				uint32_t(writes.size()),
				writes.data(),
				0, //descriptor copy count
				nullptr
			);

			std::cout << "Re-allocated object transforms buffer to " << new_bytes << " bytes." << std::endl;
		}

		assert(workspace.Transforms_src.size == workspace.Transforms.size);
		assert(workspace.Transforms_src.size >= needed_bytes);

		{
			assert(workspace.Transforms_src.allocation.mapped);
			ObjectsPipeline::Transform *out = reinterpret_cast<ObjectsPipeline::Transform*>(workspace.Transforms_src.allocation.data());
			for(ObjectInstance const &inst : object_instances) {
				*out = inst.transform;
				++out;
			}
		}

		VkBufferCopy copy_region {
			.srcOffset = 0,
			.dstOffset = 0,
			.size = needed_bytes,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.Transforms_src.handle, workspace.Transforms.handle, 1, &copy_region);
	}

	{//memory barrier
		VkMemoryBarrier memory_barrier {
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
		};

		vkCmdPipelineBarrier(workspace.command_buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
			0, //dependency flags
			1, &memory_barrier, //memorybarries
			0, nullptr, //buffer memory b
			0, nullptr //image mem b
		);
	}

	//GPU Commands
	{//render pass
		std::array<VkClearValue, 2> clear_values{
			VkClearValue{.color{.float32{0.5859375f, 0.76171875f, 0.91796875f, 1.f}}},
			VkClearValue{.depthStencil{.depth = 1.f, .stencil = 0}},
		};

		VkRenderPassBeginInfo begin_info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			//.pNext = nullptr,
			.renderPass = render_pass,
			.framebuffer = framebuffer,
			.renderArea{
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent,
			},
			.clearValueCount = uint32_t(clear_values.size()),
			.pClearValues = clear_values.data(),

		};

		vkCmdBeginRenderPass(workspace.command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

		//run pipelines
		{ //set scissor rectangle:
			VkRect2D scissor {
				.offset = {.x = viewport_rect.x, .y = viewport_rect.y},
				.extent = {.width = viewport_rect.width, .height = viewport_rect.height},
			};
			vkCmdSetScissor(workspace.command_buffer, 0, 1, &scissor);
		}

		{
			VkViewport viewport {
				.x = float(viewport_rect.x),
				.y = float(viewport_rect.y),
				.width = float(viewport_rect.width),
				.height = float(viewport_rect.height),
				.minDepth = 0.f,
				.maxDepth = 1.f,
			};
			vkCmdSetViewport(workspace.command_buffer, 0, 1, &viewport);
		}

		{
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, background_pipeline.handle);
			
			{// push constant
				BackgroundPipeline::Push push{
					.time = time,
				};
				vkCmdPushConstants(workspace.command_buffer, background_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
			}
			
			vkCmdDraw(workspace.command_buffer, 3, 1, 0, 0);
		}

		if(!lines_vertices.empty()){//draw with the lines pipeline:
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lines_pipeline.handle);
			{
				std::array<VkBuffer, 1> vertex_buffers {workspace.lines_vertices.handle};
				std::array<VkDeviceSize, 1> offsets{0};
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
			}
			{
				std::array<VkDescriptorSet, 1> descriptor_sets{
					workspace.Camera_descriptors,
				};
				vkCmdBindDescriptorSets(
					workspace.command_buffer,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					lines_pipeline.layout,
					0, //first set
					uint32_t(descriptor_sets.size()), descriptor_sets.data(),
					0, nullptr //dynamic offsets count, ptr
				);
			}
			vkCmdDraw(workspace.command_buffer, uint32_t(lines_vertices.size()), 1, 0, 0); // vertex count, instance count, first vertex, first instance.
		}

		{//draw with the objecs pipeline:
			if(!object_instances.empty()) {
				vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, objects_pipeline.handle);
				{ // vertexbuffer
					std::array<VkBuffer, 1> vertex_buffers {object_vertices.handle};
					std::array<VkDeviceSize, 1> offsets{0};
					vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
				}
				//Camera descriptor set is still bond but unused
				{//bind transform descriptor
					std::array<VkDescriptorSet, 3> descriptor_sets{
						workspace.World_descriptors,
						workspace.Transforms_descriptors,
						workspace.Material_descriptors,
					};
					vkCmdBindDescriptorSets(
						workspace.command_buffer,
						VK_PIPELINE_BIND_POINT_GRAPHICS,
						objects_pipeline.layout,
						0, //first set
						uint32_t(descriptor_sets.size()), descriptor_sets.data(),
						0, nullptr //dynamic offsets count, ptr
					);
				}

				for (ObjectInstance const &inst : object_instances) {
					uint32_t index = uint32_t(&inst - &object_instances[0]);
					vkCmdBindDescriptorSets(
						workspace.command_buffer,
						VK_PIPELINE_BIND_POINT_GRAPHICS,
						objects_pipeline.layout,
						3, //second set
						1, &texture_descriptors[inst.texture],
						0, nullptr //dynamic offsets count, ptr
					);
					vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index); // vertex count, instance count, first vertex, first instance.
				}
			}
		}


		vkCmdEndRenderPass(workspace.command_buffer);
		// write end timestamp for GPU timing
		if (query_pool != VK_NULL_HANDLE) {
			vkCmdWriteTimestamp(workspace.command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, workspace.query_index + 1);
		}
		
	}

	//end recording
	VK(vkEndCommandBuffer(workspace.command_buffer));

	//submit `workspace.command buffer` for the GPU to run:
	//refsol::Tutorial_render_submit(rtg, render_params, workspace.command_buffer);
	{
		std::array<VkSemaphore, 1> wait_semaphores {
			render_params.image_available
		};
		std::array<VkPipelineStageFlags, 1> wait_stages {
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		};
		static_assert(wait_semaphores.size() == wait_stages.size(), "every semaphore needs a stage");

		std::array<VkSemaphore, 1> signal_semaphores {
			render_params.image_done
		};
		VkSubmitInfo submit_info {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = uint32_t(wait_semaphores.size()),
			.pWaitSemaphores = wait_semaphores.data(),
			.pWaitDstStageMask = wait_stages.data(),
			.commandBufferCount = 1,
			.pCommandBuffers = &workspace.command_buffer,
			.signalSemaphoreCount = uint32_t(signal_semaphores.size()),
			.pSignalSemaphores = signal_semaphores.data(),
		};

		VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, render_params.workspace_available));
		// Read back GPU timestamp results (blocking wait) and store GPU time in seconds
		if (query_pool != VK_NULL_HANDLE) {
			uint64_t timestamps[2] = {0,0};
			VkResult r = vkGetQueryPoolResults(rtg.device, query_pool, workspace.query_index, 2, sizeof(timestamps), timestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
			if (r == VK_SUCCESS) {
				uint64_t delta = timestamps[1] - timestamps[0];
				double gpuSec = double(delta) * timestampPeriodNs * 1e-3; // timestampPeriodNs is nanoseconds per tick
				// associate gpu time with the frame index recorded when writing the start timestamp
				auto it = query_to_frame.find(workspace.query_index);
				if (it != query_to_frame.end()) {
					uint64_t frame = it->second;
					FrameStats &fs = stats_map[frame];
					fs.frame = frame;
					fs.gpu = gpuSec;
				}
				gpu_times.push_back(gpuSec);
				if (gpu_times.size() > 1000) gpu_times.erase(gpu_times.begin(), gpu_times.begin() + (gpu_times.size() - 1000));
				// periodic summary every 60 frames
				stats_frame_counter++;
				if ((stats_frame_counter % 60) == 0) {
					size_t n = std::min<size_t>(60, gpu_times.size());
					double sum_gpu = std::accumulate(gpu_times.end() - n, gpu_times.end(), 0.0);
					double avg_gpu = sum_gpu / double(n);
					size_t m = std::min<size_t>(60, cpu_times.size());
					double sum_cpu = (m>0) ? std::accumulate(cpu_times.end() - m, cpu_times.end(), 0.0) : 0.0;
					double avg_cpu = (m>0) ? (sum_cpu / double(m)) : 0.0;
					std::cout << "PERF avg(last " << n << ") cpu: " << avg_cpu << " ms, gpu: " << avg_gpu << " us, FPS: " << (1000.0 / avg_cpu) << std::endl;
				}
			} else {
				std::cerr << "WARNING: vkGetQueryPoolResults returned " << string_VkResult(r) << std::endl;
			}
		}
	}
}

static const uint16_t aabb_edges[24] = {
    0,1, 1,2, 2,3, 3,0,   // bottom
    4,5, 5,6, 6,7, 7,4,   // top
    0,4, 1,5, 2,6, 3,7    // vertical
};

std::array<vec3, 8> get_aabb_corners(vec3 const& mn, vec3 const& mx) {
    return {
        vec3(mn.x, mn.y, mn.z),
        vec3(mx.x, mn.y, mn.z),
        vec3(mx.x, mx.y, mn.z),
        vec3(mn.x, mx.y, mn.z),
        vec3(mn.x, mn.y, mx.z),
        vec3(mx.x, mn.y, mx.z),
        vec3(mx.x, mx.y, mx.z),
        vec3(mn.x, mx.y, mx.z)
    };
}

static const std::array<vec3, 8> clipCorners = {
    // near plane
    glm::vec3(-1.f,  1.f, 0.f), // near top-left
    glm::vec3( 1.f,  1.f, 0.f), // near top-right
    glm::vec3( 1.f, -1.f, 0.f), // near bottom-right
    glm::vec3(-1.f, -1.f, 0.f), // near bottom-left

    // far plane
    glm::vec3(-1.f,  1.f, 1.f), // far top-left
    glm::vec3( 1.f,  1.f, 1.f), // far top-right
    glm::vec3( 1.f, -1.f, 1.f), // far bottom-right
    glm::vec3(-1.f, -1.f, 1.f)  // far bottom-left
};

std::array<glm::vec3, 8> get_frustum_corners_trans(const glm::mat4& proj)
{
    glm::mat4 invProj = glm::inverse(proj);
    std::array<glm::vec3, 8> viewCorners;

    for (size_t i = 0; i < 8; ++i) {
        glm::vec4 v = invProj * vec4(clipCorners[i], 1.0f);   // back to view space (homogeneous)
        v /= v.w;                                 // perspective divide
        viewCorners[i] = glm::vec3(v);           // now in camera local space
    }

    return viewCorners;
}

std::array<vec3, 8> get_camera_frustrum_corners(Tutorial::OrbitCamera const& cam, float g_aspect)
{
    // 1) Camera position from spherical coords
    vec3 target(cam.target_x, cam.target_y, cam.target_z);

    float cosE = std::cos(cam.elevation);
    float sinE = std::sin(cam.elevation);
    float cosA = std::cos(cam.azimuth);
    float sinA = std::sin(cam.azimuth);

    vec3 eye(
        target.x + cam.radius * cosE * cosA,
        target.y + cam.radius * sinE,
        target.z + cam.radius * cosE * sinA
    );

    // 2) Basis vectors (right, up, forward)
    vec3 forward = glm::normalize(target - eye);          // look at target
    vec3 worldUp = vec3(0.0f, 1.0f, 0.0f);

    vec3 right   =  glm::normalize( glm::cross(forward, worldUp));
    vec3 up      = glm::normalize(glm::cross(right, forward));

    // 3) Frustum dimensions at near and far planes
    float tanHalfFov = std::tan(cam.fov * 0.5f);

    float nh = cam.near * tanHalfFov;
    float nw = nh * g_aspect;

    float fh = cam.far * tanHalfFov;
    float fw = fh * g_aspect;

    vec3 nc = eye + forward * cam.near; // near plane center
    vec3 fc = eye + forward * cam.far;  // far plane center

    std::array<vec3, 8> corners;

    // Near plane
    corners[0] = nc + up * nh - right * nw; // near top-left
    corners[1] = nc + up * nh + right * nw; // near top-right
    corners[2] = nc - up * nh + right * nw; // near bottom-right
    corners[3] = nc - up * nh - right * nw; // near bottom-left

    // Far plane
    corners[4] = fc + up * fh - right * fw; // far top-left
    corners[5] = fc + up * fh + right * fw; // far top-right
    corners[6] = fc - up * fh + right * fw; // far bottom-right
    corners[7] = fc - up * fh - right * fw; // far bottom-left

    return corners;
}

void Tutorial::update(float dt) {
	// CPU timing moved to render(); update() just advances state
	time = std::fmod(time + dt, 60.0f);

	// advance animation playback time (only when playing)
	if (playback_playing) {
		playback_time += dt;
	}

	lines_vertices.clear();
	viewport_rect = {0, 0, rtg.configuration.surface_extent.width, rtg.configuration.surface_extent.height};

	if (camera_mode == CameraMode::Scene) {
		if (!cur_scene_camera) {
			assert(s72.cameras.size() > 0 && "Scene Camera mode selected but no cameras in the scene");
			cur_scene_camera = &s72.cameras.begin()->second;
		}
		assert(cur_scene_camera);
		if(auto* perspective_params = std::get_if<S72::Camera::Perspective>(&cur_scene_camera->projection)) {
			mat4 proj = vulkan_perspective(
			perspective_params->vfov,
			perspective_params->aspect,
			// rtg.swapchain_extent.width / float(rtg.swapchain_extent.height),
			perspective_params->near,
			perspective_params->far);
			mat4 view = glm::inverse(cur_scene_camera->transform);
			CLIP_FROM_WORLD = proj * view;

			//aspect : pillarbox & letterbox
			float window_aspect = rtg.swapchain_extent.width / float(rtg.swapchain_extent.height);
			if (window_aspect > perspective_params->aspect) {
				// Pillarbox (bars left/right)
				viewport_rect.height = rtg.swapchain_extent.height;
				viewport_rect.width  = uint32_t(rtg.swapchain_extent.height * perspective_params->aspect + 0.5f);
				viewport_rect.x      = int32_t((rtg.swapchain_extent.width  - viewport_rect.width) / 2);
				viewport_rect.y      = 0;
			} else {
				// Letterbox (bars top/bottom)
				viewport_rect.width  = rtg.swapchain_extent.width;
				viewport_rect.height = uint32_t(rtg.swapchain_extent.width / perspective_params->aspect + 0.5f);
				viewport_rect.x      = 0;
				viewport_rect.y      = int32_t((rtg.swapchain_extent.height - viewport_rect.height) / 2);
			}
		} else {
			throw std::runtime_error("Failed to get Scene Camera Params");
		}
	} else if (camera_mode == CameraMode::Free) {
		viewport_rect = {0, 0, rtg.configuration.surface_extent.width, rtg.configuration.surface_extent.height};
		free_camera.proj = vulkan_perspective(
			free_camera.fov,
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height),
			free_camera.near,
			free_camera.far);
		
		free_camera.view = vulkan_orbit(
			free_camera.target_x, free_camera.target_y, free_camera.target_z,
			free_camera.azimuth, free_camera.elevation, free_camera.radius
		);

		CLIP_FROM_WORLD = free_camera.proj * free_camera.view;
	} else if (camera_mode == CameraMode::Debug) {
		viewport_rect = {0, 0, rtg.configuration.surface_extent.width, rtg.configuration.surface_extent.height};
		// mat4 proj = vulkan_perspective(
		// debug_camera.fov,
		// rtg.swapchain_extent.width / float(rtg.swapchain_extent.height),
		// debug_camera.near,
		// debug_camera.far);
 		// mat4 transform = glm::translate(glm::mat4(1.f), debug_camera.translation);
		// // Apply Rotation (Order matters: Y -> X -> Z is standard for cameras/objects)
		// transform = glm::rotate(transform, debug_camera.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f)); // Yaw
		// transform = glm::rotate(transform, debug_camera.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f)); // Pitch
		// transform = glm::rotate(transform, debug_camera.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f)); // Roll
		// mat4 view = glm::inverse(transform);
		// CLIP_FROM_WORLD = proj * view;
		CLIP_FROM_WORLD = vulkan_perspective(
			debug_camera.fov,
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height),
			debug_camera.near,
			debug_camera.far
		) * vulkan_orbit(
			debug_camera.target_x, debug_camera.target_y, debug_camera.target_z,
			debug_camera.azimuth, debug_camera.elevation, debug_camera.radius
		);

		//draw previous camera frustums in the debug camera mode:
		if (auto* prev_cam_ptr = std::get_if<OrbitCamera*>(&previous_camera)) {
			OrbitCamera* prev_cam = *prev_cam_ptr;
			std::vector<PosColVertex> debug_vertices;
			// std::array<vec3, 8> corners = get_camera_frustrum_corners(
			// 	*prev_cam,
			// 	rtg.swapchain_extent.width / float(rtg.swapchain_extent.height)
			// );
			std::array<vec3, 8> corners = get_frustum_corners_trans(prev_cam->proj);
			for (auto& corner : corners) {
				vec4 transformed_corner = glm::inverse(prev_cam->view) * vec4(corner, 1.f);
				debug_vertices.emplace_back(PosColVertex{
					.Position{.x = transformed_corner.x, .y = transformed_corner.y, .z = transformed_corner.z},
					.Color{.r = 0x00, .g = 0x00, .b = 0xff, .a = 0xff}
				});
			}
			for (size_t i = 0; i < 24; i += 2) {
				lines_vertices.emplace_back(debug_vertices[aabb_edges[i]]);
				lines_vertices.emplace_back(debug_vertices[aabb_edges[i + 1]]);
			}
		} else if (auto* prev_scene_cam_ptr = std::get_if<S72::Camera*>(&previous_camera)) {
			S72::Camera* prev_scene_cam = *prev_scene_cam_ptr;
			std::vector<PosColVertex> debug_vertices;
			if (auto* perspective_params = std::get_if<S72::Camera::Perspective>(&prev_scene_cam->projection)) {
				mat4 proj = vulkan_perspective(
				perspective_params->vfov,
				perspective_params->aspect,
				perspective_params->near,
				perspective_params->far);
				std::array<vec3, 8> corners = get_frustum_corners_trans(proj);
				for (auto& corner : corners) {
					vec4 transformed_corner = prev_scene_cam->transform * vec4(corner, 1.f);
					debug_vertices.emplace_back(PosColVertex{
						.Position{.x = transformed_corner.x, .y = transformed_corner.y, .z = transformed_corner.z},
						.Color{.r = 0x00, .g = 0x00, .b = 0xff, .a = 0xff}
					});
				}
				for (size_t i = 0; i < 24; i += 2) {
					lines_vertices.emplace_back(debug_vertices[aabb_edges[i]]);
					lines_vertices.emplace_back(debug_vertices[aabb_edges[i + 1]]);
				}
			}
		}
	} else {
		assert(0 && "only three camera modes");
	}
	

	{// make some objects:
		object_instances.clear();
		// Apply drivers (in file order) to override node properties
		for (auto const &drv : s72.drivers) {
			if (drv.times.empty()) continue;
			float t = playback_time;
			// find interval
			auto const &times = drv.times;
			size_t i = 0;
			if (t <= times.front()) {
				i = 0;
			} else if (t >= times.back()) {
				i = times.size() - 1;
			} else {
				// find index so that times[i] <= t < times[i+1]
				for (size_t k = 0; k + 1 < times.size(); ++k) {
					if (times[k] <= t && t < times[k+1]) { i = k; break; }
				}
			}

			// sample value depending on channel width
			if (drv.channel == S72::Driver::Channel::translation || drv.channel == S72::Driver::Channel::scale) {
				const size_t W = 3;
				auto const &vals = drv.values;
				std::array<float,3> samp{};
				if (i + 1 >= times.size()) {
					// use last
					for (size_t c = 0; c < W; ++c) samp[c] = vals[i*W + c];
				} else {
					float t0 = times[i];
					float t1 = times[i+1];
					float alpha = (t1 == t0) ? 0.0f : (t - t0) / (t1 - t0);
					if (drv.interpolation == S72::Driver::Interpolation::STEP) {
						for (size_t c = 0; c < W; ++c) samp[c] = vals[i*W + c];
					} else { // LINEAR
						for (size_t c = 0; c < W; ++c) {
							float v0 = vals[i*W + c];
							float v1 = vals[(i+1)*W + c];
							samp[c] = glm::mix(v0, v1, alpha);
						}
					}
				}
				if (drv.channel == S72::Driver::Channel::translation) {
					drv.node.translation = S72::vec3(samp[0], samp[1], samp[2]);
				} else {
					drv.node.scale = S72::vec3(samp[0], samp[1], samp[2]);
				}
			} else if (drv.channel == S72::Driver::Channel::rotation) {
				const size_t W = 4;
				auto const &vals = drv.values;
				glm::quat q;
				if (i + 1 >= times.size()) {
					// last
					q = glm::quat(
						vals[i*W + 3], // w
						vals[i*W + 0], // x
						vals[i*W + 1],
						vals[i*W + 2]
					);
				} else {
					float t0 = times[i];
					float t1 = times[i+1];
					float alpha = (t1 == t0) ? 0.0f : (t - t0) / (t1 - t0);
					glm::quat q0 = glm::quat(vals[i*W + 3], vals[i*W + 0], vals[i*W + 1], vals[i*W + 2]);
					glm::quat q1 = glm::quat(vals[(i+1)*W + 3], vals[(i+1)*W + 0], vals[(i+1)*W + 1], vals[(i+1)*W + 2]);
					if (drv.interpolation == S72::Driver::Interpolation::STEP) {
						q = q0;
					} else if (drv.interpolation == S72::Driver::Interpolation::SLERP) {
						q = glm::normalize(glm::slerp(q0, q1, alpha));
					} else { // LINEAR on components then normalize
						glm::quat qmix = glm::mix(q0, q1, alpha);
						q = glm::normalize(qmix);
					}
				}
				drv.node.rotation = q;
			} else {
				throw std::runtime_error("unsupported driver channel");
			}
		}

		fill_scene_graph(s72, object_instances);

		// Apply culling if requested
		if (culling_mode == CullingMode::Frustum) {
			mat4 cullClip = CLIP_FROM_WORLD;
			if (camera_mode == CameraMode::Debug) {
				// when in debug camera, cull as if rendering the previously-selected camera
				if (auto *prev_cam_ptr = std::get_if<OrbitCamera*>(&previous_camera)) {
					OrbitCamera *prev_cam = *prev_cam_ptr;
					cullClip = prev_cam->proj * prev_cam->view;
				} else if (auto *prev_scene_cam_ptr = std::get_if<S72::Camera*>(&previous_camera)) {
					S72::Camera *prev_scene_cam = *prev_scene_cam_ptr;
					if (auto *perspective_params = std::get_if<S72::Camera::Perspective>(&prev_scene_cam->projection)) {
						mat4 proj = vulkan_perspective(
							perspective_params->vfov,
							perspective_params->aspect,
							perspective_params->near,
							perspective_params->far
						);
						mat4 view = glm::inverse(prev_scene_cam->transform);
						cullClip = proj * view;
					}
				}
			}

			std::vector<ObjectInstance> filtered;
			filtered.reserve(object_instances.size());
			for (auto &inst : object_instances) {
				if (aabb_intersects_frustum_SAT(cullClip, inst)) {
					filtered.emplace_back(inst);
				}
			}
			object_instances.swap(filtered);
		}
	}
}


void Tutorial::on_input(InputEvent const &evt) {
	//if there is a current action, it gets input priority
	if (action) {
		action(evt);
		return;
	}
	//general controls 
	if (evt.type == InputEvent::KeyDown && evt.key.key == GLFW_KEY_TAB) {
		//switch camera modes
		if (camera_mode == CameraMode::Scene) {
			previous_camera = cur_scene_camera;
		} else if (camera_mode == CameraMode::Free) {
			previous_camera = &free_camera;
		} else if (camera_mode == CameraMode::Debug) {
			previous_camera = &debug_camera;
		}
		camera_mode = CameraMode((int(camera_mode) + 1) % uint8_t(CameraMode::CameraMode_Count));
		//std::cout << "CameraMode Updated: " << int(camera_mode) << std::endl;
		return;
	}
	if (evt.type == InputEvent::KeyDown && evt.key.key == GLFW_KEY_D) {
		//switch camera modes
		if (camera_mode == CameraMode::Scene) {
			previous_camera = cur_scene_camera;
		} else if (camera_mode == CameraMode::Free) {
			previous_camera = &free_camera;
		} else if (camera_mode == CameraMode::Debug) {
			previous_camera = &debug_camera;
		}
		camera_mode = CameraMode::Debug;
		//std::cout << "CameraMode Updated: " << int(camera_mode) << std::endl;
		return;
	}

	if (evt.type == InputEvent::KeyDown && evt.key.key == GLFW_KEY_RIGHT) {
		//cycle scene camera 
		auto it = s72.cameras.find(cur_scene_camera->name);
		assert(it != s72.cameras.end());
		++it;
		if (it == s72.cameras.end())
			it = s72.cameras.begin();
		cur_scene_camera = &it->second;
		//std::cout << "Current Scene Camera Updated: " <<  << std::endl;
		return;
	}

	// Animation playback controls: Space toggles play/pause, R restarts
	if (evt.type == InputEvent::KeyDown && evt.key.key == GLFW_KEY_SPACE) {
		playback_playing = !playback_playing;
		return;
	}
	if (evt.type == InputEvent::KeyDown && evt.key.key == GLFW_KEY_R) {
		playback_time = 0.0f;
		return;
	}

	if (camera_mode == CameraMode::Free) {

		if(evt.type == InputEvent::MouseWheel) {
			//change distance by 10% every scroll click:
			free_camera.radius *= std::exp(std::log(1.1f) * -evt.wheel.y);
			free_camera.radius = std::max(free_camera.radius, 0.5f * free_camera.near);
			free_camera.radius = std::min(free_camera.radius, 2.0f * free_camera.far);
			return;
		}

		if(evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT && (evt.button.mods & GLFW_MOD_SHIFT)) {
			//std::cout << "Panning started." << std::endl;
			float init_x = evt.button.x;
			float init_y = evt.button.y;
			OrbitCamera init_camera = free_camera;
			action = [this, init_x, init_y, init_camera](InputEvent const &evt) {
				if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
					// cancel upon button lifted:
					action = nullptr;
					//std::cout << "Panning ended." << std::endl;
					return;
				}
				if(evt.type == InputEvent::MouseMotion) {
					//Handle motion
					float height = 2.f * std::tan(free_camera.fov * 0.5f) * free_camera.radius;
					float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.height * height; //why height not weight
					float dy = -(evt.motion.y - init_y) / rtg.swapchain_extent.height * height; 
					
					mat4 camera_from_world = vulkan_orbit(
							init_camera.target_x, init_camera.target_y, init_camera.target_z,
							init_camera.azimuth, init_camera.elevation, init_camera.radius);
					
					//move distance
					free_camera.target_x = init_camera.target_x - dx * camera_from_world[0][0] - dy * camera_from_world[0][1];
					free_camera.target_y = init_camera.target_y - dx * camera_from_world[1][0] - dy * camera_from_world[1][1];
					free_camera.target_z = init_camera.target_z - dx * camera_from_world[2][0] - dy * camera_from_world[2][1];
					return;
				}
			};
			
			return;
		}

		if(evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
			//std::cout << "Tumble started." << std::endl;
			float init_x = evt.button.x;
			float init_y = evt.button.y;
			OrbitCamera init_camera = free_camera;
			action = [this, init_x, init_y, init_camera](InputEvent const &evt) {
				if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
					// cancel upon button lifted:
					action = nullptr;
					//std::cout << "Tumble ended." << std::endl;
					return;
				}
				if(evt.type == InputEvent::MouseMotion) {
					//Handle motion
					float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.height; //why height not weight
					float dy = -(evt.motion.y - init_y) / rtg.swapchain_extent.height; 
					//rotate
					float speed = float(M_PI); //one full height == 180 
					float flip_x = (std::abs(init_camera.elevation) > 0.5f * float(M_PI) ? -1.0f : 1.0f);
					free_camera.azimuth = init_camera.azimuth - dx * speed * flip_x;
					free_camera.elevation = init_camera.elevation - dy * speed;
					// [-pi. pi]
					const float twopi = 2.0f * float(M_PI);
					free_camera.azimuth -= std::round(free_camera.azimuth / twopi) * twopi;
					free_camera.elevation -= std::round(free_camera.elevation / twopi) * twopi;
					return;
				}
			};
			
			return;
		}
	}

	if (camera_mode == CameraMode::Debug) {
				if(evt.type == InputEvent::MouseWheel) {
			//change distance by 10% every scroll click:
			debug_camera.radius *= std::exp(std::log(1.1f) * -evt.wheel.y);
			debug_camera.radius = std::max(debug_camera.radius, 0.5f * debug_camera.near);
			debug_camera.radius = std::min(debug_camera.radius, 2.0f * debug_camera.far);
			return;
		}

		if(evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT && (evt.button.mods & GLFW_MOD_SHIFT)) {
			//std::cout << "Panning started." << std::endl;
			float init_x = evt.button.x;
			float init_y = evt.button.y;
			OrbitCamera init_camera = debug_camera;
			action = [this, init_x, init_y, init_camera](InputEvent const &evt) {
				if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
					// cancel upon button lifted:
					action = nullptr;
					//std::cout << "Panning ended." << std::endl;
					return;
				}
				if(evt.type == InputEvent::MouseMotion) {
					//Handle motion
					float height = 2.f * std::tan(debug_camera.fov * 0.5f) * debug_camera.radius;
					float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.height * height; //why height not weight
					float dy = -(evt.motion.y - init_y) / rtg.swapchain_extent.height * height; 
					
					mat4 camera_from_world = vulkan_orbit(
							init_camera.target_x, init_camera.target_y, init_camera.target_z,
							init_camera.azimuth, init_camera.elevation, init_camera.radius);
					
					//move distance
					debug_camera.target_x = init_camera.target_x - dx * camera_from_world[0][0] - dy * camera_from_world[0][1];
					debug_camera.target_y = init_camera.target_y - dx * camera_from_world[1][0] - dy * camera_from_world[1][1];
					debug_camera.target_z = init_camera.target_z - dx * camera_from_world[2][0] - dy * camera_from_world[2][1];
					return;
				}
			};
			
			return;
		}

		if(evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
			//std::cout << "Tumble started." << std::endl;
			float init_x = evt.button.x;
			float init_y = evt.button.y;
			OrbitCamera init_camera = debug_camera;
			action = [this, init_x, init_y, init_camera](InputEvent const &evt) {
				if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
					// cancel upon button lifted:
					action = nullptr;
					//std::cout << "Tumble ended." << std::endl;
					return;
				}
				if(evt.type == InputEvent::MouseMotion) {
					//Handle motion
					float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.height; //why height not weight
					float dy = -(evt.motion.y - init_y) / rtg.swapchain_extent.height; 
					//rotate
					float speed = float(M_PI); //one full height == 180 
					float flip_x = (std::abs(init_camera.elevation) > 0.5f * float(M_PI) ? -1.0f : 1.0f);
					debug_camera.azimuth = init_camera.azimuth - dx * speed * flip_x;
					debug_camera.elevation = init_camera.elevation - dy * speed;
					// [-pi. pi]
					const float twopi = 2.0f * float(M_PI);
					debug_camera.azimuth -= std::round(debug_camera.azimuth / twopi) * twopi;
					debug_camera.elevation -= std::round(debug_camera.elevation / twopi) * twopi;
					return;
				}
			};
			
			return;
		}

		// if(evt.type == InputEvent::MouseWheel) {
		// 	// //change distance by 10% every scroll click:
		// 	// free_camera.radius *= std::exp(std::log(1.1f) * -evt.wheel.y);
		// 	// free_camera.radius = std::max(free_camera.radius, 0.5f * free_camera.near);
		// 	// free_camera.radius = std::min(free_camera.radius, 2.0f * free_camera.far);
		// 	return;
		// }

		// if(evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT && (evt.button.mods & GLFW_MOD_SHIFT)) {
		// 	// //std::cout << "Panning started." << std::endl;
		// 	// float init_x = evt.button.x;
		// 	// float init_y = evt.button.y;
		// 	// OrbitCamera init_camera = free_camera;
		// 	// action = [this, init_x, init_y, init_camera](InputEvent const &evt) {
		// 	// 	if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
		// 	// 		// cancel upon button lifted:
		// 	// 		action = nullptr;
		// 	// 		//std::cout << "Panning ended." << std::endl;
		// 	// 		return;
		// 	// 	}
		// 	// 	if(evt.type == InputEvent::MouseMotion) {
		// 	// 		//Handle motion
		// 	// 		float height = 2.f * std::tan(free_camera.fov * 0.5f) * free_camera.radius;
		// 	// 		float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.height * height; //why height not weight
		// 	// 		float dy = -(evt.motion.y - init_y) / rtg.swapchain_extent.height * height; 
					
		// 	// 		mat4 camera_from_world = vulkan_orbit(
		// 	// 				init_camera.target_x, init_camera.target_y, init_camera.target_z,
		// 	// 				init_camera.azimuth, init_camera.elevation, init_camera.radius);
					
		// 	// 		//move distance
		// 	// 		free_camera.target_x = init_camera.target_x - dx * camera_from_world[0][0] - dy * camera_from_world[0][1];
		// 	// 		free_camera.target_y = init_camera.target_y - dx * camera_from_world[1][0] - dy * camera_from_world[1][1];
		// 	// 		free_camera.target_z = init_camera.target_z - dx * camera_from_world[2][0] - dy * camera_from_world[2][1];
		// 	// 		return;
		// 	// 	}
		// 	// };
			
		// 	return;
		// }

		// if(evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_RIGHT) {
		// 	// //std::cout << "Tumble started." << std::endl;
		// 	float init_x = evt.button.x;
		// 	float init_y = evt.button.y;
		// 	UserCamera init_camera = debug_camera;
		// 	action = [this, init_x, init_y, init_camera](InputEvent const &evt) {
		// 		if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_RIGHT) {
		// 			// cancel upon button lifted:
		// 			action = nullptr;
		// 			//std::cout << "Tumble ended." << std::endl;
		// 			return;
		// 		}
		// 		if(evt.type == InputEvent::MouseMotion) {
		// 			//Handle motion
		// 			float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.height; //why height not weight
		// 			float dy = -(evt.motion.y - init_y) / rtg.swapchain_extent.height; 
		// 			//rotate
		// 			float speed = float(M_PI); //one full height == 180 
		// 			// float flip_x = (std::abs(init_camera.elevation) > 0.5f * float(M_PI) ? -1.0f : 1.0f);
		// 			debug_camera.rotation.y = init_camera.rotation.x - dx * speed;
		// 			debug_camera.rotation.x = init_camera.rotation.y + dy * speed;
		// 			// [-pi. pi]
		// 			// const float twopi = 2.0f * float(M_PI);
		// 			// debug_camera.rotation.y -= std::round(free_camera.azimuth / twopi) * twopi;
		// 			// free_camera.elevation -= std::round(free_camera.elevation / twopi) * twopi;
		// 			return;
		// 		}
		// 	};
			
		// 	return;
		// }
	}
}

void Tutorial::traverse_children(S72 &_s72, S72::Node* node, size_t &id, mat4 local_trans, std::vector<ObjectInstance> &objects){
	//Print node information
	if(node->camera != nullptr){
		// std::cout << "Camera: " << node->camera->name;
		s72.cameras[node->camera->name].transform = local_trans;
	}
	if(node->mesh != nullptr){
		//std::cout << "Mesh: " << node->mesh->name;
		//make objectvertices
		auto it = object_vertices_list.find(node->mesh->name);
		if (it == object_vertices_list.end()) throw std::runtime_error("Failed to find the mesh in object vertices list");
		uint32_t matId = 0;
		if(node->mesh->material != nullptr){
			// objects.back().material = node->mesh->material->name;
			matId = node->mesh->material->index;
			//std::cout << " {Material: " <<node->mesh->material->name << "}";
		}
		ObjectInstance obj = ObjectInstance{
			.vertices = it->second,
			.transform = makeInstanceData(local_trans, matId),
		};
		objects.emplace_back(obj);
		id++;
		

		//draw bouding box
		if (camera_mode == CameraMode::Debug) {
			auto corners = get_aabb_corners(it->second.min_aabb_bound, it->second.max_aabb_bound);
			std::vector<PosColVertex> debug_vertices;
			for (vec3 &corner : corners) {
				vec4 transformed_corner = local_trans * vec4(corner, 1.0f);
				debug_vertices.emplace_back(PosColVertex{
					.Position{.x = transformed_corner.x, .y = transformed_corner.y, .z = transformed_corner.z},
					.Color{.r = 0xff, .g = 0xff, .b = 0x00, .a = 0xff}
				});
			}

			for (size_t i = 0; i < 24; i += 2) {
				lines_vertices.emplace_back(debug_vertices[aabb_edges[i]]);
				lines_vertices.emplace_back(debug_vertices[aabb_edges[i + 1]]);
			}
		}
	}
	if(node->environment != nullptr){
		// std::cout << "Environment: " << node->environment->name;
	}
	if(node->light != nullptr){
		// std::cout << "Light: " << node->light->name;
		if(auto* sun = std::get_if<S72::Light::Sun>(&node->light->source)) {
			if(sun->angle == 3.14159f) {
				world.SKY_ENERGY.r = node->light->tint.r * sun->strength;
				world.SKY_ENERGY.g = node->light->tint.g * sun->strength;
				world.SKY_ENERGY.b = node->light->tint.b * sun->strength;

				vec3 sun_dir = vec3(0.0f, 0.0f, 1.f); //z- axis because shader use point to light
				vec3 transformed_sun_dir = glm::normalize(vec3(local_trans * vec4(sun_dir, 0.0f)));
				world.SKY_DIRECTION.x = transformed_sun_dir.x;
				world.SKY_DIRECTION.y = transformed_sun_dir.y;
				world.SKY_DIRECTION.z = transformed_sun_dir.z;
			} else if (sun->angle == 0.0f) {
				world.SUN_ENERGY.r = node->light->tint.r * sun->strength;
				world.SUN_ENERGY.g = node->light->tint.g * sun->strength;
				world.SUN_ENERGY.b = node->light->tint.b * sun->strength;

				vec3 sun_dir = vec3(0.0f, 0.0f, 1.f); //z- axis because shader use point to light
				vec3 transformed_sun_dir = glm::normalize(vec3(local_trans * vec4(sun_dir, 0.0f)));
				world.SUN_DIRECTION.x = transformed_sun_dir.x;
				world.SUN_DIRECTION.y = transformed_sun_dir.y;
				world.SUN_DIRECTION.z = transformed_sun_dir.z;
			} else {
				throw std::runtime_error("Unexpected Light Type");
			}
		}
	}
	for(S72::Node* child : node->children){
		mat4 new_trans = local_trans * glm::translate(glm::mat4(1.f), child->translation) * glm::toMat4(child->rotation) * glm::scale(glm::mat4(1.0f), child->scale); // TRS
		traverse_children(_s72, child, id, new_trans, objects);
	}
}

void Tutorial::fill_scene_graph(S72 &_s72, std::vector<ObjectInstance> &objects){
	size_t instanceId = 0;
	for (S72::Node* root : _s72.scene.roots) {
		mat4 local_trans = glm::translate(glm::mat4(1.f), root->translation) * glm::toMat4(root->rotation) * glm::scale(glm::mat4(1.0f), root->scale); // TRS
		traverse_children(_s72, root, instanceId, local_trans, objects);
	}
	// std::cout << "Print Objects Materials" << std::endl;
	// for (auto obj : objects) {
	// 	std::cout << "--" << obj.material << std::endl;
	// }
}

Tutorial::ObjectsPipeline::Transform Tutorial::makeInstanceData(mat4 world_from_local, uint32_t material_index) {
	return ObjectsPipeline::Transform {
		.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * world_from_local,
		.WORLD_FROM_LOCAL = world_from_local,
		.WORLD_FROM_LOCAL_NORMAL = world_from_local, //since our matrices are orthonormal, the inverse transpose is simply the matrix itself.
		.MATERIAL_INDEX = material_index,
	};
}

bool overlapsOnAxis(
    const vec3& axis,
    const std::array<vec3, 8>& _frustumCorners,
    const std::array<vec3, 8>& _boxCorners
) {
    // Ignore degenerate axis
    float len2 = glm::dot(axis, axis);
    if (len2 < 1e-8f)
        return true; // treat as non-separating

    glm::vec3 n = glm::normalize(axis);

    // Project frustum
    float frMin =  std::numeric_limits<float>::infinity();
    float frMax = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < 8; ++i) {
        float d = glm::dot(_frustumCorners[i], n);
        frMin = std::min(frMin, d);
        frMax = std::max(frMax, d);
    }

    // Project box
    float bxMin =  std::numeric_limits<float>::infinity();
    float bxMax = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < 8; ++i) {
        float d = glm::dot(_boxCorners[i], n);
        bxMin = std::min(bxMin, d);
        bxMax = std::max(bxMax, d);
    }

    // Interval overlap test
    // [frMin, frMax] vs [bxMin, bxMax]
    if (bxMax < frMin || bxMin > frMax)
        return false; // separating axis found

    return true; // overlap on this axis
}


bool Tutorial::aabb_intersects_frustum_SAT(const mat4& clip, const ObjectInstance& instance) {
	// --- 1. Frustum face normals from clip (column-major) ---
    vec3 frustumNormals[6] = {
        vec3(clip[0][3] + clip[0][0], clip[1][3] + clip[1][0], clip[2][3] + clip[2][0]), // left
        vec3(clip[0][3] - clip[0][0], clip[1][3] - clip[1][0], clip[2][3] - clip[2][0]), // right
        vec3(clip[0][3] + clip[0][1], clip[1][3] + clip[1][1], clip[2][3] + clip[2][1]), // bottom
        vec3(clip[0][3] - clip[0][1], clip[1][3] - clip[1][1], clip[2][3] - clip[2][1]), // top
        vec3(clip[0][3] + clip[0][2], clip[1][3] + clip[1][2], clip[2][3] + clip[2][2]), // near
        vec3(clip[0][3] - clip[0][2], clip[1][3] - clip[1][2], clip[2][3] - clip[2][2])  // far
    };

    // Normalize and discard nearly-parallel duplicates (keep 5)
    std::vector<vec3> axes;
    axes.reserve(5 + 3 + 18);

    auto addAxisIfUnique = [&](const vec3& a, std::vector<vec3>& axes_list) {
        float len2 = glm::dot(a, a);
        if (len2 < 1e-8f) return;
        vec3 n = glm::normalize(a);
        for (auto& u : axes_list) {
            float d = std::abs(glm::dot(u, n));
            if (d > 0.999f) // almost parallel
                return;
        }
        axes_list.emplace_back(n);
    };

    // Add frustum face normals (should give you 5 unique)
    for (int i = 0; i < 6; ++i) {
		addAxisIfUnique(frustumNormals[i], axes);
	}
	assert(axes.size() <= 5 && "Expected at most 5 unique frustum normals");
        
    // --- 2. Box face normals (in world space) ---
	const mat4& world_from_local = instance.transform.WORLD_FROM_LOCAL;

    // Transform local axes to world
    vec3 boxAxesWorld[3] = {
        glm::normalize(vec3(world_from_local[0])),
        glm::normalize(vec3(world_from_local[1])),
        glm::normalize(vec3(world_from_local[2]))
    };

    for (int i = 0; i < 3; ++i) {
		axes.emplace_back(boxAxesWorld[i]);
	}

    // --- 3. Edge directions for frustum and box (for cross products) ---

    // Frustum edges: directions of intersections between pairs of planes.
    std::array<vec3, 8> frustum_corners = get_frustum_corners_trans(clip);
	std::vector<vec3> frustumEdges;
	for (size_t i = 0; i < 24; i += 2) {
		addAxisIfUnique((frustum_corners[aabb_edges[i + 1]] - frustum_corners[aabb_edges[i]]), frustumEdges);
	}
	assert(frustumEdges.size() <= 6 && "Expected at most 6 unique frustum edges");

    // Box edges: its local axes transformed (3 unique, signs don’t matter for cross)
    vec3 boxEdges[3] = {
        boxAxesWorld[0],
        boxAxesWorld[1],
        boxAxesWorld[2]
    };

    // --- 4. Cross-product axes (frustum edges × box edges) ---
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 3; ++j) {
            vec3 c = glm::normalize(glm::cross(frustumEdges[i], boxEdges[j]));
            axes.emplace_back(c);
        }
    }

	// box corners in world space
	std::array<vec3, 8> boxCorners;
	auto corners = get_aabb_corners(instance.vertices.min_aabb_bound, instance.vertices.max_aabb_bound);
	for (vec3 &corner : corners) {
		vec4 transformed_corner = world_from_local * vec4(corner, 1.0f);
		boxCorners[&corner - &corners[0]] = vec3(transformed_corner);
	}

    // --- 5. SAT on all axes ---
    for (const vec3& axis : axes) {
        if (!overlapsOnAxis(axis, frustum_corners, boxCorners))
            return false; // separating axis found
    }

    return true; // no separating axis -> intersection
}



