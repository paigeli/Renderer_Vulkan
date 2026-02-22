#pragma once

// only for 4x4 matrices 

#include "external/GLM/glm/vec3.hpp" // glm::vec3
#include "external/GLM/glm/vec4.hpp" // glm::vec4
#include "external/GLM/glm/mat4x4.hpp" // glm::mat4
#include "external/GLM/glm/ext/matrix_transform.hpp" // glm::translate, glm::rotate, glm::scale
#include "external/GLM/glm/ext/matrix_clip_space.hpp" // glm::perspective
#define GLM_ENABLE_EXPERIMENTAL
#include "external/GLM/glm/gtx/quaternion.hpp"

#include <array>
#include <cmath>
#include <cstdint>

//col-major
using mat4 = glm::mat4;
using vec3 = glm::vec3;
using vec4 = glm::vec4;

// - z axis with + x right and + y up
    // const float e = 1.f / std::tan(vfov / 2.f);
    // const float a = aspect;
    // const float n = near;
    // const float f = far;
    // return mat4{
    //   e/a, 0.f, 0.f, 0.f,
    //   0.f, -e, 0.f, 0.f,
    //   0.f, 0.f, -0.5f - 0.5f * (f+n) / (f-n), -1.f,
    //   0.f, 0.f, -(f*n) / (f-n), 0.f,
    // };
inline mat4 vulkan_perspective(float vfov, float aspect, float near, float far) {
    glm::mat4 proj = glm::perspectiveRH_ZO(
    vfov,
    aspect,
    near,
    far
    );
    proj[1][1] *= -1.0f; // to flip Y for Vulkan
    return proj;
}

// inline mat4 look_at(
//     float eye_x, float eye_y, float eye_z,
//     float target_x, float target_y, float target_z,
//     float up_x, float up_y, float up_z) {

//     float in_x = target_x - eye_x;
//     float in_y = target_y - eye_y;
//     float in_z = target_z - eye_z;

//     float inv_in_len = 1.0f / std::sqrt(in_x * in_x + in_y*in_y+in_z*in_z);
//     in_x *= inv_in_len;
//     in_y *= inv_in_len;
//     in_z *= inv_in_len;

//     float in_dot_up = in_x*up_x + in_y*up_y + in_z*up_z;
//     up_x -= in_dot_up * in_x;
//     up_y -= in_dot_up * in_y;
//     up_z -= in_dot_up * in_z;

//     float inv_up_len = 1.0f / std::sqrt(up_x * up_x + up_y*up_y+up_z*up_z);
//     up_x *= inv_up_len;
//     up_y *= inv_up_len;
//     up_z *= inv_up_len;

//     float right_x = in_y*up_z - in_z*up_y;
//     float right_y = in_z*up_x - in_x*up_z;
//     float right_z = in_x*up_y - in_y*up_x;

//     float right_dot_eye = right_x*eye_x + right_y*eye_y + right_z*eye_z;
//     float up_dot_eye = up_x*eye_x + up_y*eye_y + up_z*eye_z;
//     float in_dot_eye = in_x*eye_x + in_y*eye_y + in_z*eye_z;

//     return mat4{
//       right_x, up_x, -in_x, 0.f,  
//       right_y, up_y, -in_y, 0.f,  
//       right_z, up_z, -in_z, 0.f,  
//       -right_dot_eye, -up_dot_eye, in_dot_eye, 1.f,  
//     };
// }

inline mat4 vulkan_look_at(float eye_x, float eye_y, float eye_z,
    float target_x, float target_y, float target_z,
    float up_x, float up_y, float up_z) {
    vec3 eye = vec3(eye_x, eye_y, eye_z);
    vec3 target = vec3(target_x, target_y, target_z);
    vec3 up = vec3(up_x, up_y, up_z);
    return glm::lookAtRH(eye, target, up);
}

// inline mat4 orbit(
//     float target_x, float target_y, float target_z,
//     float azimuth, float elevation, float radius
// ) {
//     float ca = std::cos(azimuth);
//     float sa = std::sin(azimuth);
//     float ce = std::cos(elevation);
//     float se = std::sin(elevation);

//     // right
//     float right_x = -sa;
//     float right_y = ca;
//     float right_z = 0.0f;

//     // up
//     float up_x = -se * ca;
//     float up_y = -se * sa;
//     float up_z = ce;

//     // out
//     float out_x = ce * ca;
//     float out_y = ce * sa;
//     float out_z = se;

//     // C_at
//     float eye_x = target_x + radius * out_x;
//     float eye_y = target_y + radius * out_y;
//     float eye_z = target_z + radius * out_z;

//     // return camera local from world matrix
//     float right_dot_eye = right_x*eye_x + right_y*eye_y + right_z*eye_z;
//     float up_dot_eye = up_x*eye_x + up_y*eye_y + up_z*eye_z;
//     float out_dot_eye = out_x*eye_x + out_y*eye_y + out_z*eye_z;

//     return mat4{
//       right_x, up_x, out_x, 0.f,  
//       right_y, up_y, out_y, 0.f,  
//       right_z, up_z, out_z, 0.f,  
//       -right_dot_eye, -up_dot_eye, -out_dot_eye, 1.f,  
//     };
// } 

inline mat4 vulkan_orbit(float target_x, float target_y, float target_z, float azimuth, float elevation, float radius) {
    vec3 target = vec3(target_x, target_y, target_z);

    float ca = std::cos(azimuth);
    float sa = std::sin(azimuth);
    float ce = std::cos(elevation);
    float se = std::sin(elevation);

    // up
    float up_x = -se * ca;
    float up_y = -se * sa;
    float up_z = ce;
    vec3 up = vec3(up_x, up_y, up_z);

    // out
    float out_x = ce * ca;
    float out_y = ce * sa;
    float out_z = se;

    // C_at
    float eye_x = target.x + radius * out_x;
    float eye_y = target.y + radius * out_y;
    float eye_z = target.z + radius * out_z;
    vec3 eye = vec3(eye_x, eye_y, eye_z);
    return glm::lookAtRH(eye, target, up);
} 

// inline mat4 quaternianToMatrix(vec4 quaternian){
// 	float x = quaternian[0];
// 	float y = quaternian[1];
// 	float z = quaternian[2];
// 	float w = quaternian[3];

// 	float m_00 = 1.0f - 2.0f * (y * y + z * z);
// 	float m_01 = 2.0f * (x * y - w * z);
// 	float m_02 = 2.0f * (x * z + w * y);

// 	float m_10 = 2.0f * (x * y + w * z);
// 	float m_11 = 1.0f - 2.0f * (x * x + z * z);
// 	float m_12 = 2.0f * (y * z - w * x);

// 	float m_20 = 2.0f * (x * z - w * y);
// 	float m_21 = 2.0f * (y * z + w * x);
// 	float m_22 = 1.0f - 2.0f * (x * x + y * y);

// 	return mat4{m_00, m_10, m_20, 0.f,
// 	            m_01, m_11, m_21, 0.f,
// 	            m_02, m_12, m_22, 0.f,
// 	            0.f,  0.f,  0.f,  1.f};
// }

inline glm::vec3 rgbe_to_float(glm::u8vec4 col) {
	//avoid decoding zero to a denormalized value:
	if (col == glm::u8vec4(0,0,0,0)) return glm::vec3(0.0f);

	int exp = int(col.a) - 128;
	return glm::vec3(
		std::ldexp((col.r + 0.5f) / 256.0f, exp),
		std::ldexp((col.g + 0.5f) / 256.0f, exp),
		std::ldexp((col.b + 0.5f) / 256.0f, exp)
	);
}

inline glm::u8vec4 float_to_rgbe(glm::vec3 col) {

	float d = std::max(col.r, std::max(col.g, col.b));

	//1e-32 is from the radiance code, and is probably larger than strictly necessary:
	if (d <= 1e-32f) {
		return glm::u8vec4(0,0,0,0);
	}

	int e;
	float fac = 255.999f * (std::frexp(d, &e) / d);

	//value is too large to represent, clamp to bright white:
	if (e > 127) {
		return glm::u8vec4(0xff, 0xff, 0xff, 0xff);
	}

	//scale and store:
	return glm::u8vec4(
		std::max(0, int32_t(col.r * fac)),
		std::max(0, int32_t(col.g * fac)),
		std::max(0, int32_t(col.b * fac)),
		e + 128
	);
}


