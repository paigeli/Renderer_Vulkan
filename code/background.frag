#version 450

layout(location = 0) in vec2 position;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push {
    float time;
};

void main() {
    //outColor = vec4(fract(gl_FragCoord.x / 100), gl_FragCoord.y / 400, 0.2, 1.0);

    // ivec2 pixel = ivec2(gl_FragCoord.xy);
    // uint isOdd = (pixel.x & 1) ^ (pixel.y & 1); 
    // const vec3 firstColor = vec3(0.f, 1.f, 0.f);
    // const vec3 secColor = vec3(0.f, 0.f, 1.f);
    // const vec3 finalColor = mix(firstColor, secColor, isOdd);
    // //outColor = vec4(secColor, 1.f);
    // outColor = vec4(finalColor, 1.f);

    //outColor = vec4(0.0, 0.0, 0.0, 1.0);

    // const vec2 center = vec2(0.5, 0.5);
    // float dist = distance(position, center);
    // vec3 finalColor = vec3(position * dist, 0.0);
    // outColor = vec4(finalColor, 1.0);

    //outColor = vec4(fract(position.x + time), position.y, 0.0, 1.0);

    // const vec2 center = vec2(0.5, 0.5);
    // float dist = distance(position, center);
    // float t = time * 2.0;
    // float wave = sin(60.0 * dist - t);

    // float v = wave * 0.5 + 0.5;

    // float rings = smoothstep(0.5, 1.0, v);
    // float rings_narrow = smoothstep(0.95, 1.0, v);

    // float fade = 1.0 - smoothstep(0.15, 0.5, dist);

    // float intensity = rings * fade;
    // vec3 waveColor = mix(vec3(0.267, 0.435, 0.675), vec3(0.627, 0.796, 0.878), rings_narrow);
    // vec3 finalColor = mix(vec3(0.0, 0.1, 0.2), waveColor, intensity);
    // //vec3 finalColor = vec3(intensity, intensity, intensity);
    // outColor = vec4(finalColor, 1.0);

    // vec3 lightBlue = vec3(0.6, 0.8, 1.0);  // top
    // vec3 darkBlue  = vec3(0.0, 0.0, 0.3);  // bottom

    // // vUV.y = 0 at bottom, 1 at top (adjust depending on your setup)
    // float t = position.y;

    // vec3 color = mix(lightBlue, darkBlue, t);
    outColor = vec4(0.0, 0.0, 0.0, 1.0);
}