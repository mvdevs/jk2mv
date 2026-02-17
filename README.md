This branch adds a Vulkan renderer and then builds several large improvements on top of it.

I'm currently learning how to work with AI efficiently and wanted to do that in a codebase I know well and with clear goals that make sense.
It's **completely written by AI** under my steering. I didn't write a single line of code myself.

Do not expect me to actually care about code quality or whether it stole parts of it from somewhere. The goal is to prove what is possible now, nothing more. Look at this from the viewpoint of a player. Everyone can mod it now. No limits anymore. The future is now.

AI generated summary of what has been changed in this branch:


## Vulkan renderer

- Replaces the OpenGL backend with a Vulkan renderer (`vk_*` codepath).
- Uses SPIR-V shaders (`assets/shaders/spirv/*`) and Vulkan pipelines/render passes instead of GLSL + GL state.
- Initializes Vulkan via SDL (surface/instance extensions come from SDL).
- Reports Vulkan device/API info through existing `glConfig` paths.

## Performance optimizations

- Adds static “world VBO” building for eligible BSP surfaces: groups world faces/triangles by `(shader, fogNum)` and uploads them once to device-local GPU buffers for lower CPU overhead during rendering.
- Tightens multiple Vulkan/renderer hot paths and updates several SPIR-V shaders.

## Ray-traced glow reflections

- Adds hardware ray-traced glow reflections using `VK_KHR_ray_tracing_pipeline` + `VK_KHR_acceleration_structure`.
- Adds new tuning CVARs:
  - `r_DynamicGlowReflections`
  - `r_DynamicGlowReflectionRadius`
  - `r_DynamicGlowReflectionIntensity`
  - `r_DynamicGlowReflectionFalloff`
  - `r_DynamicGlowReflectionG2Scale`
  - `r_DynamicGlowReflectionShadowIntensity`
- Integrates reflections into the glow pipeline (dispatch after glow render, composite additively).
- When RT reflections are enabled, skips legacy dynamic-light projection to avoid double work.

## Modern defaults and UX polish

- Raises default visual quality settings:
  - MSAA default: `r_ext_multisample` → `16`
  - Anisotropic filtering default: `r_ext_texture_filter_anisotropic` → `16`
  - Texture detail defaults: `r_picmip` → `0`, `r_simpleMipMaps` → `0`
  - Enables: `r_flares` and `r_drawSun`
  - Texture filtering default: `r_textureMode` → `GL_LINEAR_MIPMAP_LINEAR`
- Improves Vulkan “enabled extensions” reporting.
- Improves per-monitor scaling behavior on Linux (x11/wayland) and clamps unreasonable DPI values.

## Sound improvements

- Updates MP3 playback/decoding behavior around minimp3 (native sample rate) and fixes length calculations accordingly.
- Adds `s_hrtf` and `s_occlusion`.
- Implements optional sound occlusion via OpenAL EFX lowpass filters (loaded at runtime for portability).
- Switches to manual distance attenuation (smooth rolloff) while keeping positional audio for HRTF.

## VM improvements

- Optimizes the x86 VM code generator with better stack/register tracking and fewer redundant memory operations.
