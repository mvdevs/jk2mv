#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT float hitDistance;

void main() {
	// Ray hit geometry — report the hit distance for screen-space glow lookup
	hitDistance = gl_HitTEXT;
}
