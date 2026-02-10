#include "Tutorial.hpp"

#include "VK.hpp"
#include "S72.hpp"
//#include "refsol.hpp"

#include <GLFW/glfw3.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <fstream>

Tutorial::Tutorial(RTG &rtg_) : rtg(rtg_) {
	//refsol::Tutorial_constructor(rtg, &depth_format, &render_pass, &command_pool);
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
			size_t matCount = s72.materials.size();
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

	{//Material
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
		// materials.emplace_back(ObjectsPipeline::Material{
		// 	.albedo = vec4(1.0f, 0.0f, 0.0f, 0.0f), // test red color
		// 	.brdf = ObjectsPipeline::BRDFType::BRDF_LAMBERTIAN,
		// });
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
			textures.reserve(3);
			{//dark grey / light grey checkerboard with a red square at the origin
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

			
			{//texture 1 will be a classic xor texture
				//make texture
				uint32_t size = 256;
				std::vector<uint32_t> data;
				data.reserve(size*size);
				for (uint32_t y = 0; y < size; ++y) {
					for (uint32_t x = 0; x < size; ++x) {
						uint8_t r = uint8_t(x) ^ uint8_t(y);
						uint8_t g = uint8_t(x+128) ^ uint8_t(y);
						uint8_t b = uint8_t(x) ^ uint8_t(y+27);
						uint8_t a = 0xff;
						data.emplace_back(uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(a) << 24); // ABGR

					}
				}
				assert(data.size() == size*size);
				
				//texture in GPU
				textures.emplace_back(rtg.helpers.create_image(
					VkExtent2D{.width = size, .height = size},
					VK_FORMAT_R8G8B8A8_SRGB,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					Helpers::Unmapped
				));

				//transfer data
				rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
			} 
		
			{//texture 2
				//make texture
				uint32_t size = 128;
				std::vector<uint32_t> data;
				data.reserve(size*size);
				for (uint32_t y = 0; y < size; ++y) {
					//float fy = (y + 0.5f) / float(size);
					for (uint32_t x = 0; x < size; ++x) {
						//float fx = (x + 0.5f) / float(size);
						data.emplace_back(0xffffd700);	
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
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent,
			};
			vkCmdSetScissor(workspace.command_buffer, 0, 1, &scissor);
		}

		{
			VkViewport viewport {
				.x = 0.f,
				.y = 0.f,
				.width = float(rtg.swapchain_extent.width),
				.height = float(rtg.swapchain_extent.height),
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

		{//draw with the lines pipeline:
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
	}
}


void Tutorial::update(float dt) {
	time = std::fmod(time + dt, 60.0f);

	{// make some objects:
		object_instances.clear();
		fill_scene_graph(s72, object_instances);
	}

	if (camera_mode == CameraMode::Scene) {
		//camera orbit the origin;
		//float ang = float(M_PI) * 2.f * 10.f ;
		float ang = float(M_PI) * 2.f * 10.f * (time / 60.f);
		CLIP_FROM_WORLD = vulkan_perspective(
			60.f * float(M_PI) / 180.f,
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height),
			0.1f,
			1000.f
		) * vulkan_look_at(
			5.0f * std::cos(ang), 5.0f*std::sin(ang), 3.0f,
			0.f, 0.f, 0.5f,
			0.f, 0.f, 1.f
		);
	} else if (camera_mode == CameraMode::Free) {
		CLIP_FROM_WORLD = vulkan_perspective(
			free_camera.fov,
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height),
			free_camera.near,
			free_camera.far
		) * vulkan_orbit(
			free_camera.target_x, free_camera.target_y, free_camera.target_z,
			free_camera.azimuth, free_camera.elevation, free_camera.radius
		);
	} else {
		assert(0 && "only two camera modes");
	}

	{ //static sun and sky:
		assert(s72.lights.size() >= 1);
		
		world.SKY_DIRECTION.x = 0.0f;
		world.SKY_DIRECTION.y = 0.0f;
		world.SKY_DIRECTION.z = 1.0f;

		world.SKY_ENERGY.r = 0.1f;
		world.SKY_ENERGY.g = 0.1f;
		world.SKY_ENERGY.b = 0.2f;
	}

	lines_vertices.clear();
	lines_vertices.reserve(4);
	lines_vertices.emplace_back(PosColVertex{
		.Position{.x = -1.f, .y = -1.f, .z = 0.f},
		.Color{.r = 0xff, .g = 0xff, .b = 0xff, .a = 0xff}
	});
	lines_vertices.emplace_back(PosColVertex{
		.Position{.x = 1.f, .y = 1.f, .z = 0.f},
		.Color{.r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff}
	});
	lines_vertices.emplace_back(PosColVertex{
		.Position{.x = -1.f, .y = 1.f, .z = 0.f},
		.Color{.r = 0x00, .g = 0x00, .b = 0xff, .a = 0xff}
	});
	lines_vertices.emplace_back(PosColVertex{
		.Position{.x = 1.f, .y = -1.f, .z = 0.f},
		.Color{.r = 0x00, .g = 0x00, .b = 0xff, .a = 0xff}
	});

	assert(lines_vertices.size() == 4);

	// { //make some crossing lines at different depths:
	// 	lines_vertices.clear();
	// 	constexpr size_t count = 2 * 30 + 2 * 30;
	// 	lines_vertices.reserve(count);
	// 	//horizontal lines at z = 0.5f:
	// 	for (uint32_t i = 0; i < 30; ++i) {
	// 		float y = (i + 0.5f) / 30.0f * 2.0f - 1.0f;
	// 		lines_vertices.emplace_back(PosColVertex{
	// 			.Position{.x = -1.0f, .y = y, .z = 0.5f},
	// 			.Color{ .r = 0xff, .g = 0xff, .b = 0x00, .a = 0xff},
	// 		});
	// 		lines_vertices.emplace_back(PosColVertex{
	// 			.Position{.x = 1.0f, .y = y, .z = 0.5f},
	// 			.Color{ .r = 0xff, .g = 0xff, .b = 0x00, .a = 0xff},
	// 		});
	// 	}
	// 	//vertical lines at z = 0.0f (near) through 1.0f (far):
	// 	for (uint32_t i = 0; i < 30; ++i) {
	// 		float x = (i + 0.5f) / 30.0f * 2.0f - 1.0f;
	// 		float z = (i + 0.5f) / 30.0f;
	// 		lines_vertices.emplace_back(PosColVertex{
	// 			.Position{.x = x, .y =-1.0f, .z = z},
	// 			.Color{ .r = 0x44, .g = 0x00, .b = 0xff, .a = 0xff},
	// 		});
	// 		lines_vertices.emplace_back(PosColVertex{
	// 			.Position{.x = x, .y = 1.0f, .z = z},
	// 			.Color{ .r = 0x44, .g = 0x00, .b = 0xff, .a = 0xff},
	// 		});
	// 	}
	// 	assert(lines_vertices.size() == count);
	// }

	// for(PosColVertex &v : lines_vertices) {
	// 	vec4 res = CLIP_FROM_WORLD * vec4{v.Position.x, v.Position.y, v.Position.z, 1.0f};
	// 	v.Position.x = res[0] / res[3];
	// 	v.Position.y = res[1] / res[3];
	// 	v.Position.z = res[2] / res[3];
	// }


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
		camera_mode = CameraMode((int(camera_mode) + 1) % 2);
		//std::cout << "CameraMode Updated: " << int(camera_mode) << std::endl;
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
		
	}
	if(node->environment != nullptr){
		// std::cout << "Environment: " << node->environment->name;
	}
	if(node->light != nullptr){
		// std::cout << "Light: " << node->light->name;
		if(auto* sun = std::get_if<S72::Light::Sun>(&node->light->source)) {
			world.SUN_ENERGY.r = node->light->tint.r * sun->strength;
			world.SUN_ENERGY.g = node->light->tint.g * sun->strength;
			world.SUN_ENERGY.b = node->light->tint.b * sun->strength;

			vec3 sun_dir = vec3(0.0f, 0.0f, 1.f); //z- axis because shader use point to light
			vec3 transformed_sun_dir = glm::normalize(vec3(local_trans * vec4(sun_dir, 0.0f)));
			world.SUN_DIRECTION.x = transformed_sun_dir.x;
			world.SUN_DIRECTION.y = transformed_sun_dir.y;
			world.SUN_DIRECTION.z = transformed_sun_dir.z;
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
