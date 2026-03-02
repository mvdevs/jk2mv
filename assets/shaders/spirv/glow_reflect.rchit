#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT float hitDistance;

void main() {
	// Encode instance type in the sign of hitDistance so the ray-gen shader
	// can distinguish BSP from Ghoul2 in a single combined-mask trace:
	//   BSP  (customIndex 0 or 1):  hitDistance = gl_HitTEXT       (positive)
	//   G2   (customIndex 2):       hitDistance = -(gl_HitTEXT+1)  (< -1.0)
	// The miss shader returns -1.0, so all three states are unique.
	if (gl_InstanceCustomIndexEXT == 2) {
		hitDistance = -(gl_HitTEXT + 1.0);
	} else {
		hitDistance = gl_HitTEXT;
	}
}
