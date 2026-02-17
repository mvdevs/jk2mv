/*
===========================================================================
vk_shaders.h - Vulkan shader reference header

GLSL shader sources live in assets/shaders/spirv/ and are compiled to
SPIR-V (.spv) by glslangValidator during the CMake build.  The .spv
files are packed into assetsmv.pk3 and loaded at runtime.

Shader overview:
  single_tex.vert  - Main vertex shader (single-tex and multi-tex).
                     Includes GPU-accelerated:
                       * Diffuse lighting  (GPU_FLAG_DIFFUSE_LIGHTING)
                       * Specular alpha    (GPU_FLAG_SPECULAR_ALPHA)
                       * Envmap texcoords  (GPU_FLAG_ENVMAP_TC)
                       * Fog modulation    (GPU_FLAG_FOG_MODULATE_*)
                       * Fog pass          (GPU_FLAG_FOG_PASS)
                       * Dynamic lights    (GPU_FLAG_DLIGHT_PASS)
                     Controlled via gpuFlags in the GPUParams UBO (set 2).
  single_tex.frag  - Fragment shader for single-texture surfaces.
  multi_tex.frag   - Fragment shader for lightmapped / multi-texture.
  gamma.vert       - Fullscreen triangle for post-process.
  gamma.frag       - Gamma LUT lookup.
  glow.vert/frag   - Glow blur passes.
  glow_composite.frag - Glow overlay composite.

Manual compilation:
  glslangValidator -V single_tex.vert -o single_tex_vert.spv
  glslangValidator -V single_tex.frag -o single_tex_frag.spv
  glslangValidator -V multi_tex.frag  -o multi_tex_frag.spv
  glslangValidator -V gamma.vert      -o gamma_vert.spv
  glslangValidator -V gamma.frag      -o gamma_frag.spv
  glslangValidator -V glow.vert       -o glow_vert.spv
  glslangValidator -V glow.frag       -o glow_frag.spv
  glslangValidator -V glow_composite.frag -o glow_composite_frag.spv
===========================================================================
*/

#ifndef VK_SHADERS_H
#define VK_SHADERS_H

#endif // VK_SHADERS_H
