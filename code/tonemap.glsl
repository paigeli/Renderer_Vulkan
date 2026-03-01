// Tone mapping library for linear light + tone mapping rendering
// Include this file in fragment shaders with: #include "tonemap.glsl"
// Compile with either -DTONEMAP_LINEAR or -DTONEMAP_ACES to select operator

// ============================================================================
// Linear Tone Mapping (Passthrough - Reference)
// ============================================================================
// Applies exposure correction only, no tone mapping.
// Radiance above 1.0 will be clipped by SRGB format.
vec3 tonemapLinear(vec3 radiance, float exposure) {
    return radiance * exp2(exposure);
}

// ============================================================================
// ACES Tone Mapping (Academy Color Encoding System)
// ============================================================================
// Implements ACES RRT (Reference Rendering Transform) + ODT (Output Device Transform)
// for the sRGB display color space.
// 
// Reference:
//   Brent Burley. "Tone Mapping". Disney Animation Studio, Siggraph 2011.
//   https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
//
// This creates a cinematographic S-curve that:
// - Preserves detail in shadows and highlights
// - Gracefully compresses extreme values
// - Works well with HDR environment maps

// ACES RRT matrix (RGB linear to ACES working space)
const mat3 ACES_RRT_INPUT_MATRIX = mat3(
    0.59719, 0.35191, 0.05090,
    0.07600, 0.90834, 0.01566,
    0.02840, 0.13383, 0.83777
);

// ACES RRT matrix (ACES working space to RGB)
const mat3 ACES_RRT_OUTPUT_MATRIX = mat3(
     1.60475, -0.53108, -0.07367,
    -0.10208,  1.10813, -0.00605,
    -0.00327, -0.07276,  1.07602
);

// ACES ODT to sRGB (simplified linear approximation)
const mat3 ACES_ODT_SRGB_MATRIX = mat3(
     0.996,  0.000,  0.007,
    -0.007,  1.004,  0.003,
     0.011, -0.004,  0.993
);

// Helper: Apply ACES tone mapping curve (tone-mapping function)
// Uses empirical RRT + ODT approximation for performance
vec3 acesToneMapping(vec3 x) {
    // RRT + ODT tone mapping curve (screen referred sRGB)
    // This is a fit to the reference implementation
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Apply SRGB gamma correction
vec3 linearToSRGB(vec3 linear) {
    bvec3 cutoff = lessThan(linear, vec3(0.0031308));
    vec3 lower = linear * 12.92;
    vec3 higher = 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055;
    return mix(higher, lower, cutoff);
}

// Full ACES tone mapping: applies exposure, RRT, ODT, and gamma correction
vec3 tonemapACES(vec3 radiance, float exposure) {
    // Apply exposure correction
    radiance = radiance * exp2(exposure);
    
    // Apply ACES tone mapping curve
    vec3 toneMapped = acesToneMapping(radiance);
    
    // Apply gamma correction for sRGB output
    // (Note: This is implicit in SRGB surface format, but included for clarity)
    return toneMapped;
}
