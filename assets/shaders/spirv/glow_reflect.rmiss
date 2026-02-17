#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT float hitDistance;

void main() {
	// Ray missed all geometry — no reflection surface found
	hitDistance = -1.0;
}
