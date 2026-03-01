#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <stdexcept>

// Configure STB Image
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/stb_image/stb_image.h"
#include "external/stb_image/stb_image_write.h"

// Configure GLM
#include "mat4.hpp"

// Vulkan headers
#include "RTG.hpp"
#include "CubePipeline.hpp"
#include "Helpers.hpp"
#include "VK.hpp"

// Descriptor pool and set structures
struct CubeApplication {
    CubePipeline pipeline;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptors = VK_NULL_HANDLE;
    
    VkFence compute_fence = VK_NULL_HANDLE;
};

// Main application
int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: cube in.png --lambertian out.png" << std::endl;
        return 1;
    }

    std::string inputPath = argv[1];
    std::string mode = argv[2];
    std::string outputPath = argv[3];

    if (mode != "--lambertian") {
        std::cerr << "Error: Only --lambertian mode is supported currently." << std::endl;
        return 1;
    }

    // =========================================================================
    // 1. Load and Pre-process Input Image (CPU)
    // =========================================================================
    int width, height, channels;
    unsigned char* data = stbi_load(inputPath.c_str(), &width, &height, &channels, 4);
    if (!data) {
        std::cerr << "Failed to load input texture: " << inputPath << std::endl;
        return 1;
    }

    // Validation
    if (width != height / 6) {
        std::cerr << "Error: Input image is not a vertical cross 1x6 cubemap." << std::endl;
        stbi_image_free(data);
        return 1;
    }

    uint32_t inFaceSize = width;
    
    // Convert RGBE input to float on CPU
    std::cout << "Loading and decoding input map (" << inFaceSize << "x" << inFaceSize << " per face)..." << std::endl;
    std::vector<glm::vec4> inputCubemap(6 * inFaceSize * inFaceSize);
    
    for (uint32_t face = 0; face < 6; ++face) {
        for (uint32_t y = 0; y < inFaceSize; ++y) {
            for (uint32_t x = 0; x < inFaceSize; ++x) {
                int srcY = face * inFaceSize + y;
                int srcIdx = (srcY * width + x) * 4;
                glm::u8vec4 rgbe(data[srcIdx + 0], data[srcIdx + 1], data[srcIdx + 2], data[srcIdx + 3]);
                glm::vec3 rgb = rgbe_to_float(rgbe);
                
                uint32_t destIdx = face * inFaceSize * inFaceSize + y * inFaceSize + x;
                inputCubemap[destIdx] = glm::vec4(rgb, 1.0f);
            }
        }
    }
    
    stbi_image_free(data);
    std::cout << "Input data loaded and decoded." << std::endl;

    // =========================================================================
    // 2. Initialize Vulkan (RTG)
    // =========================================================================
    RTG::Configuration config;
    config.application_info.pApplicationName = "Cube Irradiance Convolution";
    config.application_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    config.headless = true; // No window needed
    config.debug = false;   // Disable debug output for cleaner stdout

    RTG rtg(config);

    CubeApplication app;

    // =========================================================================
    // 3. Create GPU Storage Images
    // =========================================================================
    std::cout << "Creating GPU resources..." << std::endl;
    
    // Input cubemap
    Helpers::AllocatedImage inputImage = rtg.helpers.create_image(
        {inFaceSize, inFaceSize},
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        Helpers::Unmapped,
        0,
        6 // Array layers for cubemap
    );

    // Output cubemap (32x32 by default)
    uint32_t outFaceSize = 32;
    Helpers::AllocatedImage outputImage = rtg.helpers.create_image(
        {outFaceSize, outFaceSize},
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        Helpers::Unmapped,
        0,
        6 // Array layers for cubemap
    );

    // Transfer input data to GPU
    rtg.helpers.transfer_to_image(inputCubemap.data(), inputCubemap.size() * sizeof(glm::vec4), inputImage);

    // Create image views for compute shader
    VkImageViewCreateInfo input_view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = inputImage.handle,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 6,
        },
    };
    VkImageView inputView = VK_NULL_HANDLE;
    VK(vkCreateImageView(rtg.device, &input_view_info, nullptr, &inputView));

    VkImageViewCreateInfo output_view_info = input_view_info;
    output_view_info.image = outputImage.handle;
    VkImageView outputView = VK_NULL_HANDLE;
    VK(vkCreateImageView(rtg.device, &output_view_info, nullptr, &outputView));

    // =========================================================================
    // 4. Set up Compute Pipeline
    // =========================================================================
    app.pipeline.create(rtg);

    // Create descriptor pool
    VkDescriptorPoolSize pool_sizes[1] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
    };
    VkDescriptorPoolCreateInfo pool_create_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = pool_sizes,
    };
    VK(vkCreateDescriptorPool(rtg.device, &pool_create_info, nullptr, &app.descriptor_pool));

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = app.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &app.pipeline.set0_layout,
    };
    VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &app.descriptors));

    // Write descriptor set
    VkDescriptorImageInfo input_image_info{
        .imageView = inputView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    VkDescriptorImageInfo output_image_info{
        .imageView = outputView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    VkWriteDescriptorSet writes[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = app.descriptors,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &input_image_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = app.descriptors,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &output_image_info,
        },
    };
    vkUpdateDescriptorSets(rtg.device, 2, writes, 0, nullptr);

    // Create fence for compute synchronization
    VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = 0,
    };
    VK(vkCreateFence(rtg.device, &fence_info, nullptr, &app.compute_fence));

    // =========================================================================
    // 5. Dispatch Compute Shader
    // =========================================================================
    std::cout << "Running GPU convolution (" << outFaceSize << "x" << outFaceSize << " per face)..." << std::endl;

    // Prepare command buffer
    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK(vkBeginCommandBuffer(rtg.helpers.compute_command_buffer, &begin_info));

    // Transition output image to GENERAL layout for compute shader
    VkImageMemoryBarrier output_barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = outputImage.handle,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 6,
        },
    };
    vkCmdPipelineBarrier(
        rtg.helpers.compute_command_buffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &output_barrier
    );

    // Bind pipeline and descriptors
    vkCmdBindPipeline(rtg.helpers.compute_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, app.pipeline.handle);
    vkCmdBindDescriptorSets(rtg.helpers.compute_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, app.pipeline.layout, 0, 1, &app.descriptors, 0, nullptr);

    // Push constants
    CubePipeline::Push push{
        .input_res = inFaceSize,
        .output_res = outFaceSize,
    };
    vkCmdPushConstants(rtg.helpers.compute_command_buffer, app.pipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);

    // Dispatch compute shader
    // Workgroup size is (8, 8, 1), so we need to cover (outFaceSize, outFaceSize, 6) with appropriate group counts
    uint32_t group_x = (outFaceSize + 7) / 8;
    uint32_t group_y = (outFaceSize + 7) / 8;
    uint32_t group_z = 6;
    vkCmdDispatch(rtg.helpers.compute_command_buffer, group_x, group_y, group_z);

    // Barrier after compute: wait for writes to complete
    VkMemoryBarrier compute_barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
    };
    vkCmdPipelineBarrier(
        rtg.helpers.compute_command_buffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        1, &compute_barrier,
        0, nullptr,
        0, nullptr
    );

    VK(vkEndCommandBuffer(rtg.helpers.compute_command_buffer));

    // Submit compute work
    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &rtg.helpers.compute_command_buffer,
    };
    VK(vkQueueSubmit(rtg.compute_queue, 1, &submit_info, app.compute_fence));

    // Wait for compute to complete
    VK(vkWaitForFences(rtg.device, 1, &app.compute_fence, VK_TRUE, UINT64_MAX));
    VK(vkResetFences(rtg.device, 1, &app.compute_fence));

    // =========================================================================
    // 6. Read Result Back to CPU
    // =========================================================================
    std::cout << "Reading results from GPU..." << std::endl;

    // Create staging buffer for output
    Helpers::AllocatedBuffer staging_buffer = rtg.helpers.create_buffer(
        outFaceSize * outFaceSize * 6 * sizeof(glm::vec4),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        Helpers::Mapped
    );

    // Copy image to staging buffer
    VK(vkResetCommandBuffer(rtg.helpers.transfer_command_buffer, 0));
    VkCommandBufferBeginInfo transfer_begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK(vkBeginCommandBuffer(rtg.helpers.transfer_command_buffer, &transfer_begin));

    // Transition output image for transfer
    VkImageMemoryBarrier transfer_barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = outputImage.handle,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 6,
        },
    };
    vkCmdPipelineBarrier(
        rtg.helpers.transfer_command_buffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &transfer_barrier
    );

    // Copy image to buffer (for cubemap, we need to copy all 6 faces)
    VkBufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 6,
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {outFaceSize, outFaceSize, 1},
    };
    vkCmdCopyImageToBuffer(rtg.helpers.transfer_command_buffer, outputImage.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging_buffer.handle, 1, &region);

    VK(vkEndCommandBuffer(rtg.helpers.transfer_command_buffer));

    // Submit transfer
    VkSubmitInfo transfer_submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &rtg.helpers.transfer_command_buffer,
    };
    VK(vkQueueSubmit(rtg.graphics_queue, 1, &transfer_submit, VK_NULL_HANDLE));
    vkQueueWaitIdle(rtg.graphics_queue);

    // Map and read staging buffer
    // 1. Calculate total element count (6 faces * pixels per face)
    size_t total_pixels = outFaceSize * outFaceSize * 6;
    size_t total_bytes = total_pixels * sizeof(glm::vec4);

    // 2. Map the pointer (cast to raw bytes or keep as type, doesn't matter for memcpy)
    glm::vec4* staged_data = static_cast<glm::vec4*>(staging_buffer.allocation.data());

    if (staged_data == nullptr) {
        throw std::runtime_error("Failed to map staging buffer memory! Pointer is NULL.");
    }

    // 3. Resize vector first to allocate memory
    std::vector<glm::vec4> outputCubemap(total_pixels);

    // 4. Memcpy directly into the vector's data array
    std::memcpy(outputCubemap.data(), staged_data, total_bytes);

    // =========================================================================
    // 7. Encode Output and Save
    // =========================================================================
    std::cout << "Encoding output map..." << std::endl;
    
    int outWidth = outFaceSize;
    int outHeight = outFaceSize * 6;
    std::vector<unsigned char> outData(outWidth * outHeight * 4);

    for (uint32_t face = 0; face < 6; ++face) {
        for (uint32_t y = 0; y < outFaceSize; ++y) {
            for (uint32_t x = 0; x < outFaceSize; ++x) {
                uint32_t src_idx = face * outFaceSize * outFaceSize + y * outFaceSize + x;
                glm::vec3 rgb = glm::vec3(outputCubemap[src_idx]);
                glm::u8vec4 rgbe = float_to_rgbe(rgb);

                int dstY = face * outFaceSize + y;
                int dstIdx = (dstY * outWidth + x) * 4;
                
                outData[dstIdx + 0] = rgbe.r;
                outData[dstIdx + 1] = rgbe.g;
                outData[dstIdx + 2] = rgbe.b;
                outData[dstIdx + 3] = rgbe.a;
            }
        }
    }

    if (stbi_write_png(outputPath.c_str(), outWidth, outHeight, 4, outData.data(), outWidth * 4)) {
        std::cout << "Saved " << outputPath << std::endl;
    } else {
        std::cerr << "Failed to write output image." << std::endl;
        return 1;
    }

    // =========================================================================
    // 8. Cleanup
    // =========================================================================
    vkDestroyFence(rtg.device, app.compute_fence, nullptr);
    vkDestroyDescriptorPool(rtg.device, app.descriptor_pool, nullptr);
    vkDestroyImageView(rtg.device, inputView, nullptr);
    vkDestroyImageView(rtg.device, outputView, nullptr);
    app.pipeline.destroy(rtg);
    rtg.helpers.destroy_buffer(std::move(staging_buffer));
    rtg.helpers.destroy_image(std::move(inputImage));
    rtg.helpers.destroy_image(std::move(outputImage));

    std::cout << "Done!" << std::endl;
    return 0;
}