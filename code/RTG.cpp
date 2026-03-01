#include "RTG.hpp"

#include "VK.hpp"
//#include "refsol.hpp"

#include <vulkan/vulkan_core.h>
#if defined(__APPLE__)
#include <vulkan/vulkan_beta.h> //for portability subset
#include <vulkan/vulkan_metal.h> //for VK_EXT_METAL_SURFACE_EXTENSION_NAME
#endif
#include <vulkan/vk_enum_string_helper.h> //useful for debug output
#include <vulkan/utility/vk_format_utils.h> //for getting format size
#include <GLFW/glfw3.h>

#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <fstream>
#include <set>

void RTG::Configuration::parse(int argc, char **argv) {
	for (int argi = 1; argi < argc; ++argi) {
		std::string arg = argv[argi];
		if (arg == "--debug") {
			debug = true;
		} else if (arg == "--no-debug") {
			debug = false;
		} else if (arg == "--physical-device") {
			if (argi + 1 >= argc) throw std::runtime_error("--physical-device requires a parameter (a device name).");
			argi += 1;
			physical_device_name = argv[argi];
		} else if (arg == "--drawing-size") {
			if (argi + 2 >= argc) throw std::runtime_error("--drawing-size requires two parameters (width and height).");
			auto conv = [&](std::string const &what) {
				argi += 1;
				std::string val = argv[argi];
				for (size_t i = 0; i < val.size(); ++i) {
					if (val[i] < '0' || val[i] > '9') {
						throw std::runtime_error("--drawing-size " + what + " should match [0-9]+, got '" + val + "'.");
					}
				}
				return std::stoul(val);
			};
			surface_extent.width = conv("width");
			surface_extent.height = conv("height");
		} else if (arg == "--headless") {
			headless = true;
		} else if (arg == "--scene") {
			if (argi + 1 >= argc) throw std::runtime_error("--scene requires a scene file.");
			argi += 1;
			scene_file = argv[argi];
		} else if(arg == "--print"){
			print = true;
		} else if(arg == "--camera"){
			if (argi + 1 >= argc) throw std::runtime_error("--camera requires a parameter (a camera name).");
			argi += 1;
			camera_name = argv[argi];
		} else if(arg == "--culling") {
			if (argi + 1 >= argc) throw std::runtime_error("--culling requires a parameter (none|frustum).");
			argi += 1;
			culling = argv[argi];
		} else if (arg == "--lambertian") {
			if (argi + 1 >= argc) throw std::runtime_error("--lambertian requires a path to output the lambertian environment map.");
			argi += 1;
			lambertian_env_output = argv[argi];
		} else if (arg == "--exposure") {
			if (argi + 1 >= argc) throw std::runtime_error("--exposure requires a float parameter.");
			argi += 1;
			try {
				exposure = std::stof(argv[argi]);
			} catch (...) {
				throw std::runtime_error("--exposure parameter '" + std::string(argv[argi]) + "' is not a valid float.");
			}
		} else if (arg == "--tone-map") {
			if (argi + 1 >= argc) throw std::runtime_error("--tone-map requires a parameter (linear|aces).");
			argi += 1;
			tone_map_operator = argv[argi];
			if (tone_map_operator != "linear" && tone_map_operator != "aces") {
				throw std::runtime_error("--tone-map operator must be 'linear' or 'aces', got '" + tone_map_operator + "'.");
			}
		} else {
			throw std::runtime_error("Unrecognized argument '" + arg + "'.");
		}
	}
}

void RTG::Configuration::usage(std::function< void(const char *, const char *) > const &callback) {
	callback("--debug, --no-debug", "Turn on/off debug and validation layers.");
	callback("--physical-device <name>", "Run on the named physical device (guesses, otherwise).");
	callback("--drawing-size <w> <h>", "Set the size of the surface to draw to.");
	callback("--headless", "Dont' create a window; read events from stdin");
	callback("--scene <path/*.s72>", "Specifies the scene(in .s72 format) to view");
	callback("--print", "Print loaded scene information");
	callback("--camera <name>", "View the scene throught the camera named <name>");
	callback("--culling <none|frustum>", "Start with specified culling mode: none or frustum.");
	callback("--lambertian <output_path>", "Pre-convolve the environment map for lambertian convolution and save the result to the specified path.");
	callback("--exposure <E>", "Set exposure value (default: 0); computed radiance is multiplied by 2^E before tone mapping.");
	callback("--tone-map <linear|aces>", "Select tone mapping operator (default: linear); linear applies no tone mapping, aces applies ACES RRT + ODT.");
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT *data,
	void *user_data
) {
	if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		std::cerr << "\x1b[91m" << "E: ";
	} else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		std::cerr << "\x1b[33m" << "w: ";
	} else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
		std::cerr << "\x1b[90m" << "i: ";
	} else {
		std::cerr << "\x1b[90m" << "v: ";
	}
	std::cerr << data->pMessage << "\x1b[0m" << std::endl;
	return VK_FALSE;
}

RTG::RTG(Configuration const &configuration_) : helpers(*this) {

	//copy input configuration:
	configuration = configuration_;

	//fill in flags/extensions/layers information:

	//create the `instance` (main handle to Vulkan library):
	// refsol::RTG_constructor_create_instance(
	// 	configuration.application_info,
	// 	configuration.debug,
	// 	&instance,
	// 	&debug_messenger
	// );
	{
		VkInstanceCreateFlags instance_flags = 0;
		std::vector<const char*> instance_extensions;
		std::vector<const char*> instance_layers;
		// add extension MoltenVK
#if defined(__APPLE__)
		instance_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

		instance_extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
		instance_extensions.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);
		instance_extensions.emplace_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#endif
		//add layers for debugging
		if (configuration.debug) {
			instance_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			instance_layers.emplace_back("VK_LAYER_KHRONOS_validation");
		}

		if (!configuration.headless) {// glfw
			glfwInit();
			if(!glfwVulkanSupported()) {
				throw std::runtime_error("GLFW reports Vulkan is not supported");
			}

			uint32_t count;
			const char**extensions = glfwGetRequiredInstanceExtensions(&count);
			if (extensions == nullptr) {
				throw std::runtime_error("GLFW failed to return a list of requested instance extensions. Perhaps it was not compiled with Vulkan support.");
			}
			for (uint32_t i = 0; i < count; ++i) {
				instance_extensions.emplace_back(extensions[i]);
			}
		}

		//debug messenger  structure
		VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
			.messageSeverity = 
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
			.messageType = 
				VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
			.pfnUserCallback = debug_callback,
			.pUserData = nullptr,
		};

		VkInstanceCreateInfo create_info {
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pNext = (configuration.debug ? &debug_messenger_create_info : nullptr),
			.flags = instance_flags,
			.pApplicationInfo = &configuration.application_info,
			.enabledLayerCount = uint32_t(instance_layers.size()),
			.ppEnabledLayerNames = instance_layers.data(),
			.enabledExtensionCount = uint32_t(instance_extensions.size()),
			.ppEnabledExtensionNames = instance_extensions.data(),
		};
		VK(vkCreateInstance(&create_info, nullptr, &instance));

		//create debug messenger
		if (configuration.debug) {
			PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
			if (!vkCreateDebugUtilsMessengerEXT) {
				throw std::runtime_error("Failed to look up debug utils create fn");
			}
			VK(vkCreateDebugUtilsMessengerEXT(instance, &debug_messenger_create_info, nullptr, &debug_messenger));
		}
	}

	//create the `window` and `surface` (where things get drawn):
	// refsol::RTG_constructor_create_surface(
	// 	configuration.application_info,
	// 	configuration.debug,
	// 	configuration.surface_extent,
	// 	instance,
	// 	&window,
	// 	&surface
	// );
	if (!configuration.headless) {
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		window = glfwCreateWindow(configuration.surface_extent.width, configuration.surface_extent.height, configuration.application_info.pApplicationName, nullptr, nullptr);
		if(!window) {
			throw std::runtime_error("GLFW failed to create window.");
		}
		VK(glfwCreateWindowSurface(instance, window, nullptr, &surface));
	}

	//select the `physical_device` -- the gpu that will be used to draw:
	// refsol::RTG_constructor_select_physical_device(
	// 	configuration.debug,
	// 	configuration.physical_device_name,
	// 	instance,
	// 	&physical_device
	// );
	{
		std::vector<std::string> physical_device_names;
		{//pick physical device
			uint32_t count = 0;
			VK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
			std::vector<VkPhysicalDevice> physical_devices(count);
			VK(vkEnumeratePhysicalDevices(instance, &count, physical_devices.data()));

			uint32_t best_score = 0;
			for (auto const &pd : physical_devices) {
				VkPhysicalDeviceProperties properties;
				vkGetPhysicalDeviceProperties(pd, &properties);
				VkPhysicalDeviceFeatures features;
				vkGetPhysicalDeviceFeatures(pd, &features);
				physical_device_names.emplace_back(properties.deviceName);

				if(!configuration.physical_device_name.empty()) {
					if (configuration.physical_device_name == properties.deviceName) {
						if (physical_device) {
							std::cerr << "WARNING: have two physical devices with the name '" << properties.deviceName << "'; using the first to be enumerated." << std::endl;
						} else {
							physical_device = pd;
						}
					}
				} else {
					uint32_t score = 1;
					if ( properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
						score += 0x800;
					}

					if (score > best_score) {
						best_score = score;
						physical_device = pd;
					}
				}
			}
		}

		if (physical_device == VK_NULL_HANDLE) {
			//report error
			std::cerr << "Physical devices: \n";
			for (std::string const &name : physical_device_names) {
				std::cerr << "		" << name << "\n";
			}
			std::cerr.flush();

			if (!configuration.physical_device_name.empty()) {
				throw std::runtime_error("No physical device with name '" + configuration.physical_device_name + "'.");
			} else {
				throw std::runtime_error("No suitable GPU found.");
			}
		}

		{//report device name
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties(physical_device, &properties);
			std::cout << "Selected physical device '" << properties.deviceName << "'." << std::endl;
		}
	}

	//select the `surface_format` and `present_mode` which control how colors are represented on the surface and how new images are supplied to the surface:
	// refsol::RTG_constructor_select_format_and_mode(
	// 	configuration.debug,
	// 	configuration.surface_formats,
	// 	configuration.present_modes,
	// 	physical_device,
	// 	surface,
	// 	&surface_format,
	// 	&present_mode
	// );
	if (configuration.headless) {
		// first requested format
		if (configuration.surface_formats.empty()) {
			throw std::runtime_error("No surface formats requested.");
		}
		surface_format = configuration.surface_formats[0];
		//always use VK_PRESENT_MODE_FIFO_KHR 
		bool have_fifo = false;
		for (auto const &mode : configuration.present_modes) {
			if (mode == VK_PRESENT_MODE_FIFO_KHR) {
				have_fifo = true;
				break;
			}
		}
		if (!have_fifo) {
			throw std::runtime_error("Configured present modes do not contain VK_PRESENT_MODE_FIFO_KHR.");
		}
		present_mode = VK_PRESENT_MODE_FIFO_KHR;
		present_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	} else {
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> present_modes;
		{
			uint32_t count = 0;
			VK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, nullptr));
			formats.resize(count);
			VK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, formats.data()));
		}
		{
			uint32_t count = 0;
			VK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, nullptr));
			present_modes.resize(count);
			VK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, present_modes.data()));
		}
		//find first available surface format matching config
		surface_format = [&](){
			for(auto const &config_format : configuration.surface_formats) {
				for (auto const &format : formats) {
					if (config_format.format == format.format && config_format.colorSpace == format.colorSpace) {
						return format;
					}
				}
			}
			throw std::runtime_error("No format matching requested format(s) found.");
		}();

		present_mode = [&](){
			for(auto const &config_mode : configuration.present_modes) {
				for (auto const &mode : present_modes) {
					if (config_mode == mode) {
						return mode;
					}
				}
			}
			throw std::runtime_error("No present mode matching requested mode(s) found.");
		}();

		present_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		//debug all supported formats and present modes
		if (configuration.debug) {
			std::cout << "Supported Surface Formats: \n";
			for(uint32_t i = 0; i < formats.size(); ++i) {
				VkSurfaceFormatKHR const &format = formats[i];
				// std::cout << " [" << i << "] format: " << string_VkFormat(format.format) << " colorSpace: " << string_VkColorSpaceKHR(format.colorSpace) << '\n';
				std::cout << " [" << i << "] " << string_VkFormat(format.format) << '\n';
			}
			std::cout << "Supported Present Modes: \n";
			for(uint32_t i = 0; i < present_modes.size(); ++i) {
				VkPresentModeKHR const &mode = present_modes[i];
				std::cout << " [" << i << "] " << string_VkPresentModeKHR(mode) << '\n';
			}
			std::cout.flush();
		}
	}

	//create the `device` (logical interface to the GPU) and the `queue`s to which we can submit commands:
	// refsol::RTG_constructor_create_device(
	// 	configuration.debug,
	// 	physical_device,
	// 	surface,
	// 	&device,
	// 	&graphics_queue_family,
	// 	&graphics_queue,
	// 	&present_queue_family,
	// 	&present_queue
	// );
	{
		{// look up queue indices
			uint32_t count = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
			std::vector<VkQueueFamilyProperties> queue_families(count);
			vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, queue_families.data());

			for (auto const &queue_family : queue_families) {
				uint32_t i = uint32_t(&queue_family - &queue_families[0]);

				if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
					if (!graphics_queue_family) graphics_queue_family = i;
				}
				if (queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT) {
					if (!compute_queue_family) compute_queue_family = i;
				}
				if (!configuration.headless) {
					VkBool32 present_support = VK_FALSE;
					VK(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &present_support));
					if(present_support == VK_TRUE) {
						if(!present_queue_family) present_queue_family = i;
					}
				}
			}

			// headless mode: present (copy to host) on graphics queue
			if (configuration.headless) {
				present_queue_family = graphics_queue_family;
			}

			if (!graphics_queue_family) {
				throw std::runtime_error("No queue with graphics support.");
			}
			if (!present_queue_family) {
				throw std::runtime_error("No queue with present support.");
			}
			if (!compute_queue_family) {
				throw std::runtime_error("No queue with compute support.");
			}
		}

		//select device extensions
		std::vector<const char*> device_extensions;
#if defined(__APPLE__)
		device_extensions.emplace_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif
		if (!configuration.headless) {
			//swapchain ext
			device_extensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		}
		
		{//create the logical device
			std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
			std::set<uint32_t> unique_queue_families {
				graphics_queue_family.value(),
				present_queue_family.value(),
				compute_queue_family.value()
			};
			float queue_priorities[1] = {1.0f};
			for (uint32_t queue_family : unique_queue_families) {
				queue_create_infos.emplace_back(VkDeviceQueueCreateInfo{
					.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
					.queueFamilyIndex = queue_family,
					.queueCount = 1,
					.pQueuePriorities = queue_priorities,
				});
			}

			VkDeviceCreateInfo create_info {
				.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
				.queueCreateInfoCount = uint32_t(queue_create_infos.size()),
				.pQueueCreateInfos = queue_create_infos.data(),
				//device layers are depreciated
				.enabledLayerCount = 0,
				.ppEnabledLayerNames = nullptr,
				
				.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
				.ppEnabledExtensionNames = device_extensions.data(),
				// use VkPhysicalDeviceFeatures to request specific fdeatures ; 
				.pEnabledFeatures = nullptr,
			};

			VK(vkCreateDevice(physical_device, &create_info, nullptr, &device));

			vkGetDeviceQueue(device, graphics_queue_family.value(), 0, &graphics_queue);
			vkGetDeviceQueue(device, present_queue_family.value(), 0, &present_queue);
			vkGetDeviceQueue(device, compute_queue_family.value(), 0, &compute_queue);
		}
	}

	//run any resource creation required by Helpers structure:
	helpers.create();

	//create initial swapchain:
	recreate_swapchain();

	//create workspace resources:
	workspaces.resize(configuration.workspaces);
	for (auto &workspace : workspaces) {
		//refsol::RTG_constructor_per_workspace(device, &workspace);
		{//create workspace fences:
			VkFenceCreateInfo create_info {
				.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
				.flags = VK_FENCE_CREATE_SIGNALED_BIT,
			};
			VK(vkCreateFence(device, &create_info, nullptr, &workspace.workspace_available));
		}
		{//create workspace semaphores:
			VkSemaphoreCreateInfo create_info {
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			};
			VK(vkCreateSemaphore(device, &create_info, nullptr, &workspace.image_available));

		}
	}
}

RTG::~RTG() {
	//don't destroy until device is idle:
	if (device != VK_NULL_HANDLE) {
		if (VkResult result = vkDeviceWaitIdle(device); result != VK_SUCCESS) {
			std::cerr << "Failed to vkDeviceWaitIdle in RTG::~RTG [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
		}
	}

	//destroy workspace resources:
	for (auto &workspace : workspaces) {
		//refsol::RTG_destructor_per_workspace(device, &workspace);
		if(workspace.workspace_available != VK_NULL_HANDLE) {
			vkDestroyFence(device, workspace.workspace_available, nullptr);
			workspace.workspace_available = VK_NULL_HANDLE;
		}
		if(workspace.image_available != VK_NULL_HANDLE) {
			vkDestroySemaphore(device, workspace.image_available, nullptr);
			workspace.image_available = VK_NULL_HANDLE;
		}
	}
	workspaces.clear();

	//destroy the swapchain:
	destroy_swapchain();

	//destroy Helpers structure resources:
	helpers.destroy();

	//destroy the rest of the resources:
	//refsol::RTG_destructor( &device, &surface, &window, &debug_messenger, &instance );
	if (device != VK_NULL_HANDLE) {
		vkDestroyDevice(device, nullptr);
		device = VK_NULL_HANDLE;
	}

	if (surface != VK_NULL_HANDLE) {
		vkDestroySurfaceKHR(instance, surface, nullptr);
		surface = VK_NULL_HANDLE;
	}

	if (window != nullptr) {
		glfwDestroyWindow(window);
		window = nullptr;
	}

	if (debug_messenger != VK_NULL_HANDLE) {
		PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
		if (vkDestroyDebugUtilsMessengerEXT) {
			vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
			debug_messenger = VK_NULL_HANDLE;
		}
	}

	if (instance != VK_NULL_HANDLE) {
		vkDestroyInstance(instance, nullptr);
		instance = VK_NULL_HANDLE;
	}
}


void RTG::recreate_swapchain() {
	// refsol::RTG_recreate_swapchain(
	// 	configuration.debug,
	// 	device,
	// 	physical_device,
	// 	surface,
	// 	surface_format,
	// 	present_mode,
	// 	graphics_queue_family,
	// 	present_queue_family,
	// 	&swapchain,
	// 	&swapchain_extent,
	// 	&swapchain_images,
	// 	&swapchain_image_views,
	// 	&swapchain_image_dones
	// );

	if (configuration.headless) {
		assert(surface == VK_NULL_HANDLE);

		//make a fake swapchain

		//set extent
		swapchain_extent = configuration.surface_extent;

		// set number of images
		uint32_t requested_count = 3; // enough for FIFO

		{//create headless command pool
			VkCommandPoolCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
				.flags = 0,
				.queueFamilyIndex = graphics_queue_family.value(),
			};
			VK(vkCreateCommandPool(device, &create_info, nullptr, &headless_command_pool));
		}
		//headless sawpchain
		assert(headless_swapchain.empty());
		headless_swapchain.reserve(requested_count);
		for(uint32_t i = 0; i < requested_count; ++i) {
			//add headless swapchain image
			HeadlessSwapchainImage &h = headless_swapchain.emplace_back();
			//allocate image data
			h.image = helpers.create_image(
				swapchain_extent,
				surface_format.format,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);
			//allocate buffer data
			h.buffer = helpers.create_buffer(
				swapchain_extent.width * swapchain_extent.height * vkuFormatTexelBlockSize(surface_format.format) / vkuFormatTexelsPerBlock(surface_format.format),
				VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				Helpers::Mapped
			);
			{//create and record copy command 
				VkCommandBufferAllocateInfo alloc_info {
					.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
					.commandPool = headless_command_pool,
					.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
					.commandBufferCount = 1,
				};
				VK(vkAllocateCommandBuffers(device, &alloc_info, &h.copy_command));

				VkCommandBufferBeginInfo begin_info{
					.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
					.flags = 0,
				};

				VK(vkBeginCommandBuffer(h.copy_command, &begin_info));

				VkBufferImageCopy region{
					.bufferOffset = 0,
					.bufferRowLength = swapchain_extent.width,
					.bufferImageHeight = swapchain_extent.height,
					.imageSubresource{
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.mipLevel = 0,
						.baseArrayLayer = 0,
						.layerCount = 1,
					},
					.imageOffset{ .x = 0, .y = 0, .z = 0 },
					.imageExtent{
						.width = swapchain_extent.width,
						.height = swapchain_extent.height,
						.depth = 1
					},
				};

				vkCmdCopyImageToBuffer(
					h.copy_command,
					h.image.handle,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					h.buffer.handle,
					1, &region
				);

				VK(vkEndCommandBuffer(h.copy_command));
			}
			{//create fences:
				VkFenceCreateInfo create_info {
					.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
					.flags = VK_FENCE_CREATE_SIGNALED_BIT,
				};
				VK(vkCreateFence(device, &create_info, nullptr, &h.image_presented));
			}
		}

		// fill in swapchain images
		assert(swapchain_images.empty());
		swapchain_images.assign(requested_count, VK_NULL_HANDLE);
		for (uint32_t i = 0; i < requested_count; ++i) {
			swapchain_images[i] = headless_swapchain[i].image.handle;
		}
	} else {
		assert(surface != VK_NULL_HANDLE);

		//request a swapchain from the windowing system 

		//clean up swapchian
		if(!swapchain_images.empty()) {
			destroy_swapchain();
		}
		//size image count and transform
		VkSurfaceCapabilitiesKHR capabilities;
		VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities));
		swapchain_extent = capabilities.currentExtent;
		uint32_t requested_count = capabilities.minImageCount + 1;
		if(capabilities.maxImageCount != 0) {
			requested_count = std::min(capabilities.maxImageCount, requested_count);
		}
		{//make the swapchain
			VkSwapchainCreateInfoKHR create_info {
				.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
				.surface = surface,
				.minImageCount = requested_count,
				.imageFormat = surface_format.format,
				.imageColorSpace = surface_format.colorSpace,
				.imageExtent = swapchain_extent,
				.imageArrayLayers = 1,
				.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				.preTransform = capabilities.currentTransform,
				.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
				.presentMode = present_mode,
				.clipped = VK_TRUE,
				.oldSwapchain = VK_NULL_HANDLE, // could be more efficient by passing old swapchain handle 
			};
			
			std::vector<uint32_t> queue_family_indices {
				graphics_queue_family.value(),
				present_queue_family.value(),
			};

			if(queue_family_indices[0] != queue_family_indices[1]) {
				//if images will be presented on a diff queue, make sure they are shared
				create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
				create_info.queueFamilyIndexCount = uint32_t(queue_family_indices.size());
				create_info.pQueueFamilyIndices = queue_family_indices.data();
			} else {
				create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			}

			VK(vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain));
		}

		{//get the swapchain images
			uint32_t count = 0;
			VK(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr));
			swapchain_images.resize(count);
			VK(vkGetSwapchainImagesKHR(device, swapchain, &count, swapchain_images.data()));
		}
	}

	//make image views for swapchain images:
	swapchain_image_views.assign(swapchain_images.size(), VK_NULL_HANDLE);
	for(size_t i = 0; i < swapchain_image_views.size(); ++i) {
		VkImageViewCreateInfo create_info {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchain_images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = surface_format.format,
			.components{
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};
		VK(vkCreateImageView(device, &create_info, nullptr, &swapchain_image_views[i]));
	}

	{//make image done semaphores
		VkSemaphoreCreateInfo create_info {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};

		swapchain_image_dones.assign(swapchain_images.size(), VK_NULL_HANDLE);
		for(size_t i = 0; i < swapchain_image_dones.size(); ++i) {
			VK(vkCreateSemaphore(device, &create_info, nullptr, &swapchain_image_dones[i]));
		}
	}

	if(configuration.debug) {
		std::cout << "Swapchain is now " << swapchain_images.size() << " images of size " << swapchain_extent.width << "x" << swapchain_extent.height << "." << std::endl;
	}
}


void RTG::destroy_swapchain() {
	// refsol::RTG_destroy_swapchain(
	// 	device,
	// 	&swapchain,
	// 	&swapchain_images,
	// 	&swapchain_image_views,
	// 	&swapchain_image_dones
	// );

	VK(vkDeviceWaitIdle(device)); //wait for any rendering to old swapchain to finish

	//clean up semaphores used for waiting on the swapchain
	for (auto &semaphore : swapchain_image_dones) {
		vkDestroySemaphore(device, semaphore, nullptr);
		semaphore = nullptr;
	}
	swapchain_image_dones.clear();

	//clean up image views
	for (auto &image_view : swapchain_image_views) {
		vkDestroyImageView(device, image_view, nullptr);
		image_view = nullptr;
	}
	swapchain_image_views.clear();

	//forget handles to swapchain images 
	swapchain_images.clear();

	if(configuration.headless) {
		//destroy headless swapchain images
		for (auto& h : headless_swapchain) {
			helpers.destroy_image(std::move(h.image));
			helpers.destroy_buffer(std::move(h.buffer));
			h.copy_command = VK_NULL_HANDLE;
			vkDestroyFence(device, h.image_presented, nullptr);
			h.image_presented = VK_NULL_HANDLE;
		}
		headless_swapchain.clear();

		//destroy command pool
		vkDestroyCommandPool(device, headless_command_pool, nullptr);
		headless_command_pool = VK_NULL_HANDLE;
	} else {
		//deallocate the swapchain and its images
		if(swapchain != VK_NULL_HANDLE) {
			vkDestroySwapchainKHR(device, swapchain, nullptr);
			swapchain = VK_NULL_HANDLE;
		}
	}
}

static void cursor_pos_callback(GLFWwindow *window, double xpos, double ypos) {
	std::vector<InputEvent> *event_queue = reinterpret_cast<std::vector<InputEvent>*>(glfwGetWindowUserPointer(window));
	if(!event_queue) return;

	InputEvent event;
	std::memset(&event, '\0', sizeof(event));

	event.type = InputEvent::MouseMotion;
	event.motion.x = float(xpos);
	event.motion.y = float(ypos);
	event.motion.state = 0;
	for(int b = 0; b < 8 && b < GLFW_MOUSE_BUTTON_LAST; ++b) {
		if(glfwGetMouseButton(window, b) == GLFW_PRESS) {
			event.motion.state |= (1 << b);
		}
	}

	event_queue->emplace_back(event);
}

static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods) {
	std::vector<InputEvent> *event_queue = reinterpret_cast<std::vector<InputEvent>*>(glfwGetWindowUserPointer(window));
	if(!event_queue) return;

	InputEvent event;
	std::memset(&event, '\0', sizeof(event));

	if (action == GLFW_PRESS) {
		event.type = InputEvent::MouseButtonDown;
	} else if (action == GLFW_RELEASE) {
		event.type = InputEvent::MouseButtonUp;
	} else {
		std::cerr << "Strange: unknow mouse button action" << std::endl;
	}

	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);
	event.button.x = float(xpos);
	event.button.y = float(ypos);
	event.button.state = 0;
	for(int b = 0; b < 8 && b < GLFW_MOUSE_BUTTON_LAST; ++b) {
		if(glfwGetMouseButton(window, b) == GLFW_PRESS) {
			event.motion.state |= (1 << b);
		}
	}
	event.button.button = uint8_t(button);
	event.button.mods = uint8_t(mods);

	event_queue->emplace_back(event);
}

static void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
	std::vector<InputEvent> *event_queue = reinterpret_cast<std::vector<InputEvent>*>(glfwGetWindowUserPointer(window));
	if(!event_queue) return;

	InputEvent event;
	std::memset(&event, '\0', sizeof(event));

	event.type = InputEvent::MouseWheel;
	event.wheel.x = float(xoffset);
	event.wheel.y = float(yoffset);

	event_queue->emplace_back(event);
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) {
	std::vector<InputEvent> *event_queue = reinterpret_cast<std::vector<InputEvent>*>(glfwGetWindowUserPointer(window));
	if(!event_queue) return;

	InputEvent event;
	std::memset(&event, '\0', sizeof(event));

	if (action == GLFW_PRESS) {
		event.type = InputEvent::KeyDown;
	} else if (action == GLFW_RELEASE) {
		event.type = InputEvent::KeyUp;
	} else if (action == GLFW_REPEAT) {
		//ignore
		return;
	} else {
		std::cerr << "Strange: unknow keyboard action" << std::endl;
	}

	event.key.key = key;
	event.key.mods = mods;

	event_queue->emplace_back(event);
}

void RTG::run(Application &application) {
	//refsol::RTG_run(*this, application);
	//initial on_swapchain
	auto on_swapchain = [&, this]() {
		application.on_swapchain(*this, SwapchainEvent{
			.extent = swapchain_extent,
			.images = swapchain_images,
			.image_views = swapchain_image_views,
		});
	};
	on_swapchain();

	//setup event handling
	std::vector<InputEvent> event_queue;
	if (!configuration.headless) {
		glfwSetWindowUserPointer(window, &event_queue);

		glfwSetCursorPosCallback(window, cursor_pos_callback);
		glfwSetMouseButtonCallback(window, mouse_button_callback);
		glfwSetScrollCallback(window, scroll_callback);
		glfwSetKeyCallback(window, key_callback);
	}
	

	uint32_t headless_next_image = 0;

	//setup time handling
	std::chrono::high_resolution_clock::time_point before = std::chrono::high_resolution_clock::now();

	while(configuration.headless || !glfwWindowShouldClose(window)) {
		float headless_dt = 0.0f;
		std::string headless_save = "";
		//event handling
		if (configuration.headless) {
			// parse event 
			std::string line;
			while (std::getline(std::cin, line)) {
				try {
					std::istringstream iss(line);
					iss.imbue(std::locale::classic());

					//ready type
					std::string type;
					if(!(iss >> type)) throw std::runtime_error("failed to read event type");
					//type specific parsing
					if (type == "AVAILABLE") { // AVAILABLE dt [save.ppm]
						// read dt 
						if (!(iss >> headless_dt)) throw std::runtime_error("failed to read dt");
						if (headless_dt < 0.0f) throw std::runtime_error("dt less than zero");
						// check for save file
						if (iss >> headless_save) {
							if(!headless_save.ends_with(".ppm")) throw std::runtime_error("out filename ("" + headless_save + "") must end with .ppm");
						}
						//check for trailing junk
						char junk;
						if(iss >> junk) throw std::runtime_error("trailing junk in event line");
						//stop parsing events so a fram can draw
						break;
					} else {
						throw std::runtime_error("unrecognized type");
					}
				} catch (std::exception &e) {
					std::cerr << "WARNING: failed to parse event (" << e.what() << ") from: "" << line << ""; ignoring it." << std::endl;
				}
			}
			if (!std::cin) break;
		} else {
			glfwPollEvents();
		}
		
		//deliver to application
		for(InputEvent const &input : event_queue) {
			application.on_input(input);
		}
		event_queue.clear();
		{//elapsed time handling
			std::chrono::high_resolution_clock::time_point after = std::chrono::high_resolution_clock::now();
			float dt = float(std::chrono::duration<double>(after-before).count());
			before = after;
			dt = std::min(dt, 0.1f);

			//headless mode override dt:
			if(configuration.headless) dt = headless_dt;

			application.update(dt);
		}

		//render handling
		uint32_t workspace_index;
		{//acquire a workspace
			assert(next_workspace < workspaces.size());
			workspace_index = next_workspace;
			next_workspace = (next_workspace + 1) & workspaces.size();
			
			//wait until the workspace is not in use
			VK(vkWaitForFences(device, 1, &workspaces[workspace_index].workspace_available, VK_TRUE, UINT64_MAX));

			//mark workspace in use
			VK(vkResetFences(device, 1, &workspaces[workspace_index].workspace_available));
		}

		//acquire an image (resize)
		uint32_t image_index = -1U;
		if (configuration.headless) {
			assert(swapchain == VK_NULL_HANDLE);
			//acquire the least-recently-used image 
			assert(headless_next_image < uint32_t(headless_swapchain.size()));
			image_index = headless_next_image;
			headless_next_image = (headless_next_image + 1) % uint32_t(headless_swapchain.size());
			// wait for image to be done copying to buffer 
			VK(vkWaitForFences(device, 1, &headless_swapchain[image_index].image_presented, VK_TRUE, UINT64_MAX));
			//save buffer 
			if (headless_swapchain[image_index].save_to != "") {
				headless_swapchain[image_index].save();
				headless_swapchain[image_index].save_to = "";
			}
			headless_swapchain[image_index].save_to = headless_save;
			//mark next copy as pending 
			VK(vkResetFences(device, 1, &headless_swapchain[image_index].image_presented));
			//signal GPU that image is available for rendering to
			VkSubmitInfo submit_info {
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.signalSemaphoreCount = 1,
				.pSignalSemaphores = &workspaces[workspace_index].image_available,
			};
			VK(vkQueueSubmit(graphics_queue, 1, &submit_info, nullptr));
		} else {
			retry:
			if (VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT16_MAX, workspaces[workspace_index].image_available, VK_NULL_HANDLE, &image_index);
				result == VK_ERROR_OUT_OF_DATE_KHR) {
				std::cerr << "Recreating swapchain because vkAcquireNextImageKHR returned " << string_VkResult(result) << "." << std::endl;
				recreate_swapchain();
				on_swapchain();

				goto retry;
			} else if (result == VK_SUBOPTIMAL_KHR) {
				std::cerr << "Suboptimal swapchain format -- ignoring for the moment." << std::endl;
			} else if (result != VK_SUCCESS) {
				throw std::runtime_error("Failed to acquire swapchain image (" + std::string(string_VkResult(result)) + ")!");
			}
		}

		//queue rendering work		
		application.render(*this, RenderParams{
			.workspace_index = workspace_index,
			.image_index = image_index,
			.image_available = workspaces[workspace_index].image_available,
			.image_done = swapchain_image_dones[image_index],
			.workspace_available = workspaces[workspace_index].workspace_available,
		});

		if(configuration.headless) {
			//submit and copy command we recorded
			//wait in transfer stage for image done to be signaled
			VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			VkSubmitInfo submit_info {
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.waitSemaphoreCount = 1,
				.pWaitSemaphores = &swapchain_image_dones[image_index],
				.pWaitDstStageMask = &wait_stage,
				.commandBufferCount = 1,
				.pCommandBuffers = &headless_swapchain[image_index].copy_command,
			};

			VK(vkQueueSubmit(graphics_queue, 1, &submit_info, headless_swapchain[image_index].image_presented));
		} else {//present image (resize swapchain)
			VkPresentInfoKHR present_info {
				.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
				.waitSemaphoreCount = 1,
				.pWaitSemaphores = &swapchain_image_dones[image_index],
				.swapchainCount = 1,
				.pSwapchains = &swapchain,
				.pImageIndices = &image_index,
			};

			assert(present_queue);

			if(VkResult result = vkQueuePresentKHR(present_queue, &present_info);
				result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
				std::cerr << "Recreating swapchain because vkQueuePresentKHR returned " << string_VkResult(result) << "." << std::endl;
				recreate_swapchain();
				on_swapchain();
			} else if (result != VK_SUCCESS) {
			throw std::runtime_error("Failed to queue presentation image (" + std::string(string_VkResult(result)) + ")!");
			}
		}
	}

	//wait for any in-flight headless feames marked for saving to finish:P
	if(configuration.headless) {
		for (size_t i = 0; i < headless_swapchain.size(); ++i) {
			uint32_t image_index = headless_next_image;
			headless_next_image = (headless_next_image + 1) % uint32_t(headless_swapchain.size());
			//block util is finished 
			VK(vkWaitForFences(device, 1, &headless_swapchain[image_index].image_presented, VK_TRUE, UINT64_MAX));
			//save buffer 
			if (headless_swapchain[image_index].save_to != "") {
				headless_swapchain[image_index].save();
				headless_swapchain[image_index].save_to = "";
			}
		}
	}

	//tear down event handing 
	if (!configuration.headless) {
		glfwSetCursorPosCallback(window, nullptr);
		glfwSetMouseButtonCallback(window, nullptr);
		glfwSetScrollCallback(window, nullptr);
		glfwSetKeyCallback(window, nullptr);

		glfwSetWindowUserPointer(window, nullptr);
	}
}

void RTG::HeadlessSwapchainImage::save() const {
	if (save_to == "") return;

	if (image.format == VK_FORMAT_B8G8R8A8_SRGB) {
		// get a pointer to the image data copies to the buffer 
		char const *bgra = reinterpret_cast<char const *>(buffer.allocation.data());
		//convert bgra -> rgb data
		std::vector<char> rgb(image.extent.height * image.extent.width * 3);
		for (uint32_t y = 0; y < image.extent.height; ++y) {
			for (uint32_t x = 0; x < image.extent.width; ++x) {
				rgb[(y * image.extent.width + x) * 3 + 0] = bgra[(y * image.extent.width + x) * 4 + 2];
				rgb[(y * image.extent.width + x) * 3 + 1] = bgra[(y * image.extent.width + x) * 4 + 1];
				rgb[(y * image.extent.width + x) * 3 + 2] = bgra[(y * image.extent.width + x) * 4 + 0];
			}
		}
		//write ppm file
		std::ofstream ppm(save_to, std::ios::binary);
		ppm << "P6\n"; //magic number + newline
		ppm << image.extent.width << " " << image.extent.height << "\n";
		ppm << "255\n"; //max color value
		ppm.write(rgb.data(), rgb.size());
	} else {
		std::cerr << "WARNING: saving format " << string_VkFormat(image.format) << " not supported." << std::endl;
	}
} 
