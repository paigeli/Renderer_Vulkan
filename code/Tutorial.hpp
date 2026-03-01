#pragma once

#include "PosColVertex.hpp"
#include "PosNorTanTexVertex.hpp"
#include "mat4.hpp"
#include "RTG.hpp"

struct Tutorial : RTG::Application {

	Tutorial(RTG &);
	Tutorial(Tutorial const &) = delete; //you shouldn't be copying this object
	~Tutorial();

	//kept for use in destructor:
	RTG &rtg;
	S72 s72;

	//--------------------------------------------------------------------
	//Resources that last the lifetime of the application:

	//chosen format for depth buffer:
	VkFormat depth_format{};
	//Render passes describe how pipelines write to images:
	VkRenderPass render_pass = VK_NULL_HANDLE;

	//Pipelines:
	struct BackgroundPipeline {
		// no descriptor set layouts
		//push constants
		struct Push {
			float time;
		};

		//layout
		VkPipelineLayout layout = VK_NULL_HANDLE;

		//no vertex bindings

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG&, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG&);
	} background_pipeline;

	struct LinesPipeline {
		// descriptor set layouts
		VkDescriptorSetLayout set0_Camera = VK_NULL_HANDLE;

		//types for descriptors
		struct Camera {
			mat4 CLIP_FROM_WORLD;
		};
		static_assert(sizeof(Camera) == 16 * 4, "camera buffer structure is packed");

		//no push constants

		//layout
		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = PosColVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG&, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG&);
	} lines_pipeline;

	struct ObjectsPipeline {
		// descriptor set layouts
		VkDescriptorSetLayout set0_World = VK_NULL_HANDLE;
		VkDescriptorSetLayout set1_Transforms = VK_NULL_HANDLE;
		VkDescriptorSetLayout set2_TEXTURE = VK_NULL_HANDLE;
		VkDescriptorSetLayout set3_Material = VK_NULL_HANDLE;

		//types for descriptors
		struct World {
			struct { float x, y, z, padding_; } SKY_DIRECTION;
			struct { float r, g, b, padding_; } SKY_ENERGY;
			struct { float x, y, z, padding_; } SUN_DIRECTION;
			struct { float r, g, b, padding_; } SUN_ENERGY;
			struct { float x, y, z, w; } EYE;  // w stores exposure
		};
		static_assert(sizeof(World) == 4*4 + 4*4 + 4*4 + 4*4 + 4*4, "World is the expected size.");
		struct Transform {
			mat4 CLIP_FROM_LOCAL;
			mat4 WORLD_FROM_LOCAL;
			mat4 WORLD_FROM_LOCAL_NORMAL;
			uint32_t MATERIAL_INDEX = 0;
			uint32_t padding_0 = 0;
			uint32_t padding_1 = 0;
			uint32_t padding_2 = 0;
		};
		static_assert(sizeof(Transform) == 16 * 4 * 3 + 4*4, "Transform structure is the expected size");
		enum BRDFType : uint32_t {
			BRDF_LAMBERTIAN = 0,
			BRDF_PBR = 1,        
 			BRDF_MIRROR = 2,      
			BRDF_ENVIRONMENT = 3,
		};

		// Material flags packing in bits:
		// bits 0-1: BRDF type
		// bits 2-6: hasAlbedoTex, hasNormalTex, hasRoughnessTex, hasMetalnessTex, hasDisplacementTex
		static constexpr uint32_t MAT_FLAG_BRDF_MASK              = 0x00000003u;
		static constexpr uint32_t MAT_FLAG_HAS_ALBEDO_TEX         = 0x00000004u;
		static constexpr uint32_t MAT_FLAG_HAS_NORMAL_TEX         = 0x00000008u;
		static constexpr uint32_t MAT_FLAG_HAS_ROUGHNESS_TEX      = 0x00000010u;
		static constexpr uint32_t MAT_FLAG_HAS_METALNESS_TEX      = 0x00000020u;
		static constexpr uint32_t MAT_FLAG_HAS_DISPLACEMENT_TEX   = 0x00000040u;

		struct Material {
			// Slot 0 (16 bytes)
			vec4 albedo;                    // RGBA color or placeholder
			
			// Slot 1 (16 bytes) - packed scalars
			uint32_t flags;                 // brdf type (bits 0-1) + texture presence flags (bits 2-6)
			float roughness;                // default roughness when not textured (PBR)
			float metalness;                // default metalness when not textured (PBR)
			uint32_t padding_scalar;        // padding to align to 16 bytes
		};
		static_assert(sizeof(Material) == 32, "Material should be 32 bytes (2 x 16-byte slots)");
		//no push constants

		//layout
		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = PosNorTanTexVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG&, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG&);
	} objects_pipeline;

	//pools from which per-workspace things are allocated:
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

	//workspaces hold per-render resources:
	struct Workspace {
		VkCommandBuffer command_buffer = VK_NULL_HANDLE; //from the command pool above; reset at the start of every render.

		Helpers::AllocatedBuffer lines_vertices_src;
		Helpers::AllocatedBuffer lines_vertices; //decive local

		Helpers::AllocatedBuffer Camera_src;
		Helpers::AllocatedBuffer Camera;
		VkDescriptorSet Camera_descriptors;

		Helpers::AllocatedBuffer World_src;
		Helpers::AllocatedBuffer World;
		VkDescriptorSet World_descriptors;

		Helpers::AllocatedImage Env_src;
		VkImageView Env;
		VkSampler env_sampler = VK_NULL_HANDLE;
		
		Helpers::AllocatedImage Env_Lambertian_src;
		VkImageView Env_Lambertian;

		//ObjectsPipeline::Transofrms data
		Helpers::AllocatedBuffer Transforms_src;
		Helpers::AllocatedBuffer Transforms;
		VkDescriptorSet Transforms_descriptors;	

		Helpers::AllocatedBuffer Material_src;
		Helpers::AllocatedBuffer Material;
		VkDescriptorSet Material_descriptors;

		// index of the first timestamp query assigned to this workspace (uses two queries: start/end)
		uint32_t query_index = 0;
	};
	std::vector< Workspace > workspaces;

	// Query pool for GPU timestamp measurements
	VkQueryPool query_pool = VK_NULL_HANDLE;

	// Timestamp period in nanoseconds per timestamp tick (from physical device limits)
	double timestampPeriodNs = 1.0;

	// Collected stats (seconds)
	std::vector<double> cpu_times;
	std::vector<double> gpu_times;
	uint32_t stats_frame_counter = 0;

	// Per-frame stats mapped by frame index
	struct FrameStats {
		uint64_t frame = 0;
		double cpu = -1.0;
		double gpu = -1.0;
	};
	std::unordered_map<uint64_t, FrameStats> stats_map;

	// map from workspace query index -> frame index (start timestamp)
	std::unordered_map<uint32_t, uint64_t> query_to_frame;

	// global frame counter
	uint64_t frame_counter = 0;

	//-------------------------------------------------------------------
	//static scene resources:
	Helpers::AllocatedBuffer object_vertices;
	struct ObjectVertices{
		uint32_t first = 0;
		uint32_t count = 0;
		vec3 min_aabb_bound = vec3(std::numeric_limits<float>::max());
		vec3 max_aabb_bound = vec3(std::numeric_limits<float>::lowest());
	};
	//a performant way would be unordered_map<string, ObjectVertices>
	std::unordered_map<std::string, ObjectVertices> object_vertices_list;

	std::vector<Helpers::AllocatedImage> textures;
	std::vector<VkImageView> texture_views;
	VkSampler texture_sampler = VK_NULL_HANDLE;
	VkDescriptorPool texture_descriptor_pool = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> texture_descriptors;

	//--------------------------------------------------------------------
	//Resources that change when the swapchain is resized:

	virtual void on_swapchain(RTG &, RTG::SwapchainEvent const &) override;

	Helpers::AllocatedImage swapchain_depth_image;
	VkImageView swapchain_depth_image_view = VK_NULL_HANDLE;
	std::vector< VkFramebuffer > swapchain_framebuffers;
	//used from on_swapchain and the destructor: (framebuffers are created in on_swapchain)
	void destroy_framebuffers();

	//--------------------------------------------------------------------
	//Resources that change when time passes or the user interacts:

	virtual void update(float dt) override;
	virtual void on_input(InputEvent const &) override;

	//modal action, intercepts inputs:
	std::function<void(InputEvent const&)> action;

	float time = 0.0f;

	// Animation playback state
	float playback_time = 0.0f; // current animation playback position (seconds)
	bool playback_playing = true; // whether animations advance with time

	enum class CameraMode {
		Scene = 0,
		Free = 1,
		Debug = 2,
		CameraMode_Count,
	} camera_mode = CameraMode::Free;

	// Culling modes
	enum class CullingMode {
		None = 0,
		Frustum = 1,
	};

	CullingMode culling_mode = CullingMode::None;

	struct OrbitCamera {
		float target_x = 0.f, target_y = 0.f, target_z = 0.f;
		float radius = 3.5f;
		float azimuth = 0.25f * float(M_PI);
		float elevation = 0.1f * float(M_PI);
		float fov = 60.f / 180.f * float(M_PI);
		float near = 0.1f;
		float far = 1000.0f;
		mat4 proj = mat4(1.f);
		mat4 view = mat4(1.f);
	} free_camera;

	struct UserCamera {
		float fov = 60.f / 180.f * float(M_PI);
		float near = 0.1f;
		float far = 1000.0f;
		vec3 translation = vec3(0.f, 0.f, 5.f);
		vec3 rotation = vec3(0.f); //radians
		//scale by defaut is 1
	};
	
	UserCamera user_camera;
	//UserCamera debug_camera;
	OrbitCamera debug_camera;

	S72::Camera* cur_scene_camera = nullptr;
	std::variant<std::monostate, S72::Camera*, OrbitCamera*> previous_camera = std::monostate{};

	mat4 CLIP_FROM_WORLD;
	struct ViewportRect {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
	} viewport_rect;

	std::vector<LinesPipeline::Vertex> lines_vertices;
	ObjectsPipeline::World world;
	std::vector<ObjectsPipeline::Material> materials;
	std::unordered_map<S72::Material*, uint32_t> material_ptr_to_index;
	std::unordered_map<S72::Texture*, uint32_t> texture_ptr_to_index;

	struct ObjectInstance {
		ObjectVertices vertices;
		ObjectsPipeline::Transform transform;
		uint32_t albedo_tex = std::numeric_limits<uint32_t>::max();
		uint32_t normal_tex = std::numeric_limits<uint32_t>::max();
		uint32_t displacement_tex = std::numeric_limits<uint32_t>::max();
		uint32_t roughness_tex = std::numeric_limits<uint32_t>::max();
		uint32_t metalness_tex = std::numeric_limits<uint32_t>::max();
		VkDescriptorSet texture_set = VK_NULL_HANDLE;
		// std::string material = "";
	};
	std::vector<ObjectInstance> object_instances;

	//--------------------------------------------------------------------
	//Helper functions:
	void traverse_children(S72 &s72, S72::Node* node, size_t &instanceIndex, mat4 local_trans, std::vector<ObjectInstance> &objects);
	void fill_scene_graph(S72 &s72,  std::vector<ObjectInstance> &object_instances);
	ObjectsPipeline::Transform makeInstanceData(mat4 world_from_local, uint32_t material_index);
	bool aabb_intersects_frustum_SAT(const mat4& clip, const ObjectInstance& instance);

	//--------------------------------------------------------------------
	//Rendering function, uses all the resources above to queue work to draw a frame:

	virtual void render(RTG &, RTG::RenderParams const &) override;
};
