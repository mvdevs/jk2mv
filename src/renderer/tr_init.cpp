// tr_init.c -- functions that are not called every frame

#include "tr_local.h"
#include "vk_local.h"

#ifndef DEDICATED
#if !defined __TR_WORLDEFFECTS_H
	#include "tr_WorldEffects.h"
#endif
#endif //!DEDICATED

#include "tr_font.h"

#ifdef G2_COLLISION_ENABLED
#if !defined (MINIHEAP_H_INC)
	#include "../qcommon/MiniHeap.h"
#endif

#include "../ghoul2/G2_local.h"
#endif


#ifdef G2_COLLISION_ENABLED
CMiniHeap *G2VertSpaceServer = NULL;
#endif

#ifndef DEDICATED
glconfig_t	glConfig;
renderState_t	renderState;
window_t	glWindow;
static void GfxInfo_f( void );
#endif


cvar_t	*r_ignoreFastPath;

cvar_t	*r_verbose;
cvar_t	*r_ignore;

cvar_t	*r_detailTextures;

cvar_t	*r_znear;

cvar_t	*r_skipBackEnd;

cvar_t	*r_measureOverdraw;
cvar_t	*r_fastsky;
cvar_t	*r_drawSun;
cvar_t	*r_dynamiclight;
cvar_t	*r_dlightBacks;

cvar_t	*r_lodbias;
cvar_t	*r_lodscale;
cvar_t	*r_autolodscalevalue;

cvar_t	*r_newDLights;

cvar_t	*r_norefresh;
cvar_t	*r_drawentities;
cvar_t	*r_drawworld;
cvar_t	*r_speeds;
cvar_t	*r_fullbright;
cvar_t	*r_vbo;
cvar_t	*r_cachedGeo;
cvar_t	*r_novis;
cvar_t	*r_nocull;
cvar_t	*r_facePlaneCull;
cvar_t	*r_showcluster;
cvar_t	*r_nocurves;

cvar_t	*r_surfaceSprites;
cvar_t	*r_surfaceWeather;

cvar_t	*r_windSpeed;
cvar_t	*r_windAngle;
cvar_t	*r_windGust;
cvar_t	*r_windDampFactor;
cvar_t	*r_windPointForce;
cvar_t	*r_windPointX;
cvar_t	*r_windPointY;

cvar_t	*r_ext_compressed_lightmaps;
cvar_t	*r_ext_texture_filter_anisotropic;

cvar_t	*r_DynamicGlow;
cvar_t	*r_DynamicGlowPasses;
cvar_t	*r_DynamicGlowDelta;
cvar_t	*r_DynamicGlowIntensity;
cvar_t	*r_DynamicGlowSoft;
cvar_t	*r_DynamicGlowWidth;
cvar_t	*r_DynamicGlowHeight;
cvar_t	*r_DynamicGlowReflections;
cvar_t	*r_DynamicGlowReflectionRadius;
cvar_t	*r_DynamicGlowReflectionIntensity;
cvar_t	*r_DynamicGlowReflectionFalloff;
cvar_t	*r_DynamicGlowReflectionG2Scale;
cvar_t	*r_DynamicGlowReflectionShadowIntensity;

cvar_t	*r_logFile;

cvar_t	*r_primitives;
cvar_t	*r_texturebits;
cvar_t	*r_texturebitslm;

cvar_t	*r_drawBuffer;
cvar_t	*r_lightmap;
cvar_t	*r_vertexLight;
cvar_t	*r_uiFullScreen;
cvar_t	*r_shadows;
cvar_t	*r_flares;
cvar_t	*r_nobind;
cvar_t	*r_singleShader;
cvar_t	*r_picmip;
cvar_t	*r_showtris;
cvar_t	*r_showsky;
cvar_t	*r_shownormals;
cvar_t	*r_finish;
cvar_t	*r_clear;
cvar_t	*r_textureMode;
cvar_t	*r_offsetFactor;
cvar_t	*r_offsetUnits;
cvar_t	*r_gamma;
cvar_t	*r_intensity;
cvar_t	*r_lockpvs;
cvar_t	*r_noportals;
cvar_t	*r_portalOnly;

cvar_t	*r_subdivisions;
cvar_t	*r_lodCurveError;

cvar_t	*r_overBrightBits;

cvar_t	*r_debugSurface;
cvar_t	*r_simpleMipMaps;

cvar_t	*r_showImages;

cvar_t	*r_ambientScale;
cvar_t	*r_directedScale;
cvar_t	*r_debugLight;
cvar_t	*r_debugSort;
cvar_t	*r_printShaders;

cvar_t	*r_maxpolys;
int		max_polys;
cvar_t	*r_maxpolyverts;
int		max_polyverts;

cvar_t	*r_modelpoolmegs;
cvar_t *r_screenshotJpegQuality;

cvar_t *r_convertModelBones;
cvar_t *r_loadSkinsJKA;

/*
Ghoul2 Insert Start
*/

cvar_t	*r_noServerGhoul2;
cvar_t	*r_Ghoul2AnimSmooth=0;
cvar_t	*r_Ghoul2UnSqashAfterSmooth=0;

/*
Ghoul2 Insert End
*/

cvar_t *r_consoleFont;
cvar_t *r_fontSharpness;
cvar_t *r_textureLODBias;
cvar_t *r_saberGlow;
cvar_t *r_environmentMapping;
cvar_t *r_printMissingModels;
cvar_t *r_newRemaps;

#ifndef DEDICATED

void RE_SetLightStyle(int style, int color);

void RE_GetBModelVerts( int bmodelIndex, vec3_t *verts, vec3_t normal );

#endif // !DEDICATED

static void AssertCvarRange( cvar_t *cv, float minVal, float maxVal, qboolean shouldBeIntegral )
{
	if ( shouldBeIntegral )
	{
		if ( ( int ) cv->value != cv->integer )
		{
			ri.Printf( PRINT_WARNING, "WARNING: cvar '%s' must be integral (%f)\n", cv->name, cv->value );
			ri.Cvar_Set( cv->name, va( "%d", cv->integer ) );
		}
	}

	if ( cv->value < minVal )
	{
		ri.Printf( PRINT_WARNING, "WARNING: cvar '%s' out of range (%f < %f)\n", cv->name, cv->value, minVal );
		ri.Cvar_Set( cv->name, va( "%f", minVal ) );
	}
	else if ( cv->value > maxVal )
	{
		ri.Printf( PRINT_WARNING, "WARNING: cvar '%s' out of range (%f > %f)\n", cv->name, cv->value, maxVal );
		ri.Cvar_Set( cv->name, va( "%f", maxVal ) );
	}
}

#ifndef DEDICATED

extern bool g_bDynamicGlowSupported;

static void R_InitCapabilities(void) {
	Com_Printf("Initializing Vulkan capabilities\n");

	// Vulkan supports all features natively
	glConfig.textureEnvAddAvailable = qtrue;
	glConfig.textureFilterAnisotropicMax = 16.0f;
	glConfig.clampToEdgeAvailable = qtrue;
	glConfig.maxActiveTextures = 4;

	// Vulkan always supports dynamic glow via shader passes
	g_bDynamicGlowSupported = true;

	// Vulkan supports post-processing gamma
	glConfig.deviceSupportsPostprocessingGamma = qtrue;

	// Vulkan supports LOD bias via sampler
	glConfig.textureLODBiasAvailable = qtrue;

	// No GL version tracking needed in Vulkan
	glConfig.glVersion = QGL_VERSION_1_4; // pretend we have 1.4 for legacy checks

	// Texture compression: always available in Vulkan
	glConfig.textureCompression = TC_NONE;
}

/*
** InitVulkan
**
** This function is responsible for initializing a valid Vulkan subsystem.
*/
static void InitVulkan(void) {
	static char vkVersionString[32];

	if (glConfig.vidWidth == 0) {
		windowDesc_t windowDesc = { GRAPHICS_API_VULKAN };
		memset(&glConfig, 0, sizeof(glConfig));

        glWindow = WIN_Init(&windowDesc, &glConfig);

		// Initialize capability flags (before VK_Init for early config)
		R_InitCapabilities();

		WIN_InitGammaMethod(&glConfig);

		// Initialize Vulkan subsystem
		VK_Init();

		// Set config from actual Vulkan device properties
		glConfig.vendor_string = "Vulkan";
		glConfig.renderer_string = (const char *)vk.deviceProperties.deviceName;
		Com_sprintf( vkVersionString, sizeof( vkVersionString ), "%d.%d.%d",
			VK_VERSION_MAJOR( vk.deviceProperties.apiVersion ),
			VK_VERSION_MINOR( vk.deviceProperties.apiVersion ),
			VK_VERSION_PATCH( vk.deviceProperties.apiVersion ) );
		glConfig.version_string = vkVersionString;
		glConfig.extensions_string = VK_GetEnabledExtensionsString();

		// Set texture size limit from device, capped to fit in the staging buffer.
		// VK_STAGING_BUFFER_SIZE bytes must hold width*height*4 (RGBA8), so
		// max dimension = sqrt(VK_STAGING_BUFFER_SIZE / 4) = 2048 for 16 MB.
		{
			int deviceMax = vk.deviceProperties.limits.maxImageDimension2D;
			int stagingMax = (int)sqrt( (double)VK_STAGING_BUFFER_SIZE / 4.0 );
			glConfig.maxTextureSize = deviceMax < stagingMax ? deviceMax : stagingMax;
		}

		// Set pixel format info based on actual Vulkan formats
		glConfig.colorBits = 32; // B8G8R8A8 swapchain
		if ( vk.depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ) {
			glConfig.depthBits = 32;
			glConfig.stencilBits = 8;
		} else if ( vk.depthFormat == VK_FORMAT_D24_UNORM_S8_UINT ) {
			glConfig.depthBits = 24;
			glConfig.stencilBits = 8;
		} else {
			glConfig.depthBits = 32;
			glConfig.stencilBits = 0;
		}

		// set default state
		R_SetDefaultState();
	} else {
		// set default state
		R_SetDefaultState();
	}
}

#endif //!DEDICATED

/*
==============================================================================

						SCREEN SHOTS

==============================================================================
*/
#ifndef DEDICATED

/*
==================
R_ScreenshotFilename
==================
*/
void R_ScreenshotFilename( int lastNumber, char *fileName, const char *psExt ) {
	int		a,b,c,d;

	if ( lastNumber < 0 || lastNumber > 9999 ) {
		Com_sprintf( fileName, MAX_OSPATH, "screenshots/shot9999%s",psExt );
		return;
	}

	a = lastNumber / 1000;
	lastNumber -= a*1000;
	b = lastNumber / 100;
	lastNumber -= b*100;
	c = lastNumber / 10;
	lastNumber -= c*10;
	d = lastNumber;

	Com_sprintf( fileName, MAX_OSPATH, "screenshots/shot%i%i%i%i%s"
		, a, b, c, d, psExt );
}

/*
====================
R_LevelShot

levelshots are specialized 256*256 thumbnails for
the menu system, sampled down from full screen distorted images
====================
*/
static void R_LevelShot( void ) {

	Com_sprintf(tr.levelshotName, sizeof(tr.levelshotName), "levelshots/%s.tga", tr.world->baseName);
	tr.levelshot = qtrue;
}

/*
==================
R_ScreenShotTGA_f

screenshot
screenshot [silent]
screenshot [levelshot]
screenshot [filename]

Doesn't print the pacifier message if there is a second arg
==================
*/
void R_ScreenShotTGA_f (void) {
	char		checkname[MAX_OSPATH];
	static	int	lastNumber = -1;
	qboolean	silent;

	if ( !strcmp( ri.Cmd_Argv(1), "levelshot" ) ) {
		R_LevelShot();
		return;
	}

	if ( !strcmp( ri.Cmd_Argv(1), "silent" ) ) {
		silent = qtrue;
	} else {
		silent = qfalse;
	}

	if ( ri.Cmd_Argc() == 2 && !silent ) {
		// explicit filename
		Com_sprintf( checkname, MAX_OSPATH, "screenshots/%s.tga", ri.Cmd_Argv( 1 ) );
	} else {
		// scan for a free filename

		// if we have saved a previous screenshot, don't scan
		// again, because recording demo avis can involve
		// thousands of shots
		if ( lastNumber == -1 ) {
			lastNumber = 0;
		}
		// scan for a free number
		for ( ; lastNumber <= 9999 ; lastNumber++ ) {
			R_ScreenshotFilename( lastNumber, checkname, ".tga" );

			if (!ri.FS_FileExists( checkname ))
			{
				break; // file doesn't exist
			}
		}

		if ( lastNumber >= 9999 ) {
			ri.Printf (PRINT_ALL, "ScreenShot: Couldn't create a file\n");
			return;
 		}

		lastNumber++;
	}

	Q_strncpyz(tr.screenshotTGAName, checkname, sizeof(tr.screenshotTGAName));
	tr.screenshotTGA = qtrue;
	tr.screenshotTGASilent = silent;
}

//jpeg  vession
void R_ScreenShot_f (void) {
	char		checkname[MAX_OSPATH];
	static	int	lastNumber = -1;
	qboolean	silent;

	if ( !strcmp( ri.Cmd_Argv(1), "levelshot" ) ) {
		R_LevelShot();
		return;
	}
	if ( !strcmp( ri.Cmd_Argv(1), "silent" ) ) {
		silent = qtrue;
	} else {
		silent = qfalse;
	}

	if ( ri.Cmd_Argc() == 2 && !silent ) {
		// explicit filename
		Com_sprintf( checkname, MAX_OSPATH, "screenshots/%s.jpg", ri.Cmd_Argv( 1 ) );
	} else {
		// scan for a free filename

		// if we have saved a previous screenshot, don't scan
		// again, because recording demo avis can involve
		// thousands of shots
		if ( lastNumber == -1 ) {
			lastNumber = 0;
		}
		// scan for a free number
		for ( ; lastNumber <= 9999 ; lastNumber++ ) {
			R_ScreenshotFilename( lastNumber, checkname, ".jpg" );

			if (!ri.FS_FileExists( checkname ))
			{
				break; // file doesn't exist
			}
		}

		if ( lastNumber == 10000 ) {
			ri.Printf (PRINT_ALL, "ScreenShot: Couldn't create a file\n");
			return;
 		}

		lastNumber++;
	}

	Q_strncpyz(tr.screenshotJPEGName, checkname, sizeof(tr.screenshotJPEGName));
	tr.screenshotJPEG = qtrue;
	tr.screenshotJPEGQuality = r_screenshotJpegQuality->integer;
	tr.screenshotJPEGSilent = silent;
}

//============================================================================

/*
** R_SetDefaultState
*/
void R_SetDefaultState( void )
{
	// Vulkan: default state is managed via pipeline state objects
	// Just initialize the software-side state tracking

	R_SetTextureMode( r_textureMode->string );

	//
	// make sure our state vector is set correctly
	//
	renderState.stateBits = GLS_DEPTHTEST_DISABLE | GLS_DEPTHMASK_TRUE;
}


/*
================
GfxInfo_f
================
*/
void GfxInfo_f( void )
{
	const char * const enablestrings[] =
	{
		"disabled",
		"enabled"
	};
	const char * const fsstrings[] =
	{
		"windowed",
		"fullscreen"
	};

	const char * const tc_table[] =
	{
		"None",
		"S3TC",
		"DXT",
	};

	ri.Printf( PRINT_ALL, "\nVENDOR: %s\n", glConfig.vendor_string );
	ri.Printf( PRINT_ALL, "RENDERER: %s\n", glConfig.renderer_string );
	ri.Printf( PRINT_ALL, "VERSION: %s\n", glConfig.version_string );
	ri.Printf( PRINT_ALL, "MAX_TEXTURE_SIZE: %d\n", glConfig.maxTextureSize );
	ri.Printf( PRINT_ALL, "MAX_ACTIVE_TEXTURES: %d\n", glConfig.maxActiveTextures );
	ri.Printf( PRINT_ALL, "\nPIXELFORMAT: color(%d-bits) Z(%d-bit) stencil(%d-bits)\n", glConfig.colorBits, glConfig.depthBits, glConfig.stencilBits );
	ri.Printf( PRINT_ALL, "MODE: %d, %d x %d %s hz:", r_mode->integer, glConfig.winWidth, glConfig.winHeight, fsstrings[r_fullscreen->integer == 1] );

	if ( glConfig.displayFrequency )
	{
		ri.Printf( PRINT_ALL, "%d\n", glConfig.displayFrequency );
	}
	else
	{
		ri.Printf( PRINT_ALL, "N/A\n" );
	}

	ri.Printf( PRINT_ALL, "renderer size: %d x %d\n", glConfig.vidWidth, glConfig.vidHeight );
	ri.Printf( PRINT_ALL, "display scale: %d%%\n", (int)roundf(glConfig.displayScale * 100.0f));

	// gamma correction
	if (r_gammamethod->integer == GAMMA_POSTPROCESSING) {
		ri.Printf(PRINT_ALL, "gamma method: postprocessing\n");
	} else if (r_gammamethod->integer == GAMMA_HARDWARE) {
		ri.Printf(PRINT_ALL, "gamma method: hardware\n");
	} else {
		ri.Printf(PRINT_ALL, "gamma method: none\n");
	}

	// rendering primitives
	{
		int		primitives;

		// Vulkan: always use indexed draw
		ri.Printf( PRINT_ALL, "rendering primitives: " );
		primitives = r_primitives->integer;
		if ( primitives == 0 ) {
			primitives = 2;
		}
		if ( primitives == -1 ) {
			ri.Printf( PRINT_ALL, "none\n" );
		} else if ( primitives == 2 ) {
			ri.Printf( PRINT_ALL, "indexed draw\n" );
		} else if ( primitives == 1 ) {
			ri.Printf( PRINT_ALL, "array element\n" );
		} else if ( primitives == 3 ) {
			ri.Printf( PRINT_ALL, "immediate mode\n" );
		}
	}

	ri.Printf( PRINT_ALL, "texturemode: %s\n", r_textureMode->string );
	ri.Printf( PRINT_ALL, "picmip: %d\n", r_picmip->integer );
	ri.Printf( PRINT_ALL, "texture bits: %d\n", r_texturebits->integer );
	ri.Printf( PRINT_ALL, "lightmap texture bits: %d\n", r_texturebitslm->integer );
	ri.Printf( PRINT_ALL, "multitexture: %s\n", enablestrings[1] );  // Vulkan: always available
	ri.Printf( PRINT_ALL, "compiled vertex arrays: %s\n", enablestrings[1] );  // Vulkan: always available
	ri.Printf( PRINT_ALL, "texenv add: %s\n", enablestrings[glConfig.textureEnvAddAvailable != 0] );
	ri.Printf( PRINT_ALL, "compressed textures: %s\n", enablestrings[glConfig.textureCompression != TC_NONE] );
	ri.Printf( PRINT_ALL, "compressed lightmaps: %s\n", enablestrings[(r_ext_compressed_lightmaps->integer != 0 && glConfig.textureCompression != TC_NONE)] );
	ri.Printf( PRINT_ALL, "texture compression method: %s\n", tc_table[glConfig.textureCompression] );
	if (glConfig.textureFilterAnisotropicMax >= 2.0f && r_ext_texture_filter_anisotropic->value > 1.0f) {
		float aniso = r_ext_texture_filter_anisotropic->value;
		aniso = Com_Clamp(1.0f, glConfig.textureFilterAnisotropicMax, aniso);
		ri.Printf( PRINT_ALL, "anisotropic filtering: %s (level: %.1f)\n", enablestrings[1], aniso );
	} else {
		ri.Printf( PRINT_ALL, "anisotropic filtering: %s\n", enablestrings[0] );
	}
	ri.Printf( PRINT_ALL, "Dynamic Glow: %s\n", enablestrings[r_DynamicGlow->integer] );

	if ( glConfig.smpActive ) {
		ri.Printf( PRINT_ALL, "Using dual processor acceleration\n" );
	}
	if ( r_finish->integer ) {
		ri.Printf( PRINT_ALL, "Forcing finish\n" );
	}
	if ( r_displayRefresh ->integer ) {
		ri.Printf( PRINT_ALL, "Display refresh set to %d\n", r_displayRefresh->integer );
	}
	if (tr.world)
	{
		ri.Printf( PRINT_ALL, "Light Grid size set to (%.2f %.2f %.2f)\n", tr.world->lightGridSize[0], tr.world->lightGridSize[1], tr.world->lightGridSize[2] );
	}
}

#endif // !DEDICATED
/*
===============
R_Register
===============
*/
void R_Register( void )
{
	//
	// latched and archived variables
	//

	r_ext_compressed_lightmaps = ri.Cvar_Get("r_ext_compress_lightmaps", "0", CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH);
	r_ext_texture_filter_anisotropic = ri.Cvar_Get("r_ext_texture_filter_anisotropic", "16", CVAR_ARCHIVE | CVAR_GLOBAL);

	r_DynamicGlow = ri.Cvar_Get( "r_DynamicGlow", "1", CVAR_ARCHIVE | CVAR_GLOBAL );
	r_DynamicGlowPasses = ri.Cvar_Get("r_DynamicGlowPasses", "3", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_DynamicGlowDelta = ri.Cvar_Get("r_DynamicGlowDelta", "0.02", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_DynamicGlowIntensity = ri.Cvar_Get("r_DynamicGlowIntensity", "1.75", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_DynamicGlowSoft = ri.Cvar_Get("r_DynamicGlowSoft", "1.5", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_DynamicGlowWidth = ri.Cvar_Get("r_DynamicGlowWidth", "320", CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH);
	r_DynamicGlowHeight = ri.Cvar_Get("r_DynamicGlowHeight", "240", CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH);
	r_DynamicGlowReflections = ri.Cvar_Get("r_DynamicGlowReflections", "1", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_DynamicGlowReflectionRadius = ri.Cvar_Get("r_DynamicGlowReflectionRadius", "1.0", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_DynamicGlowReflectionIntensity = ri.Cvar_Get("r_DynamicGlowReflectionIntensity", "0.2", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_DynamicGlowReflectionFalloff = ri.Cvar_Get("r_DynamicGlowReflectionFalloff", "20", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_DynamicGlowReflectionG2Scale = ri.Cvar_Get("r_DynamicGlowReflectionG2Scale", "0.3", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_DynamicGlowReflectionShadowIntensity = ri.Cvar_Get("r_DynamicGlowReflectionShadowIntensity", "0.7", CVAR_ARCHIVE | CVAR_GLOBAL);

	r_picmip = ri.Cvar_Get("r_picmip", "0", CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH);
	AssertCvarRange( r_picmip, 0, 16, qtrue );
	r_detailTextures = ri.Cvar_Get("r_detailtextures", "1", CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH);
	r_texturebits = ri.Cvar_Get("r_texturebits", "0", CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH);
	r_texturebitslm = ri.Cvar_Get("r_texturebitslm", "0", CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH);
	r_overBrightBits = ri.Cvar_Get("r_overBrightBits", "1", CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH);
	r_intensity = ri.Cvar_Get("r_intensity", "1", CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH);

	r_simpleMipMaps = ri.Cvar_Get("r_simpleMipMaps", "0", CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH);
	r_vertexLight = ri.Cvar_Get("r_vertexLight", "0", CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH);
	r_uiFullScreen = ri.Cvar_Get( "r_uifullscreen", "0", 0);
	r_subdivisions = ri.Cvar_Get("r_subdivisions", "4", CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH);
	r_ignoreFastPath = ri.Cvar_Get("r_ignoreFastPath", "1", CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH);
	r_newRemaps = ri.Cvar_Get("r_newRemaps", "0", CVAR_CHEAT ); // Only used for testing. Classic remaps are supposed to remain fullbright,
	                                                            // because that is how they have been used by maps and serverside mods for
	                                                            // more than 20 years. Servers can set a configstring for "mvremap" now.

	//
	// temporary latched variables that can only change over a restart
	//
	r_fullbright = ri.Cvar_Get ("r_fullbright", "0", CVAR_CHEAT );
	r_vbo = ri.Cvar_Get ("r_vbo", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_cachedGeo = ri.Cvar_Get ("r_cachedGeo", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_singleShader = ri.Cvar_Get ("r_singleShader", "0", CVAR_CHEAT | CVAR_LATCH );

	//
	// archived variables that can change at any time
	//
	r_lodCurveError = ri.Cvar_Get("r_lodCurveError", "250", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_lodbias = ri.Cvar_Get("r_lodbias", "0", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_autolodscalevalue = ri.Cvar_Get( "r_autolodscalevalue", "0", CVAR_ROM );

	r_flares = ri.Cvar_Get("r_flares", "1", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_znear = ri.Cvar_Get( "r_znear", "4", CVAR_CHEAT );
	AssertCvarRange( r_znear, 0.001f, 200, qtrue );
	r_fastsky = ri.Cvar_Get("r_fastsky", "0", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_drawSun = ri.Cvar_Get("r_drawSun", "1", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_dynamiclight = ri.Cvar_Get("r_dynamiclight", "1", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_dlightBacks = ri.Cvar_Get("r_dlightBacks", "1", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_finish = ri.Cvar_Get("r_finish", "0", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_textureMode = ri.Cvar_Get("r_textureMode", "GL_LINEAR_MIPMAP_LINEAR", CVAR_ARCHIVE | CVAR_GLOBAL);

	// gamma correction
	r_gamma = ri.Cvar_Get("r_gamma", "1", CVAR_ARCHIVE | CVAR_GLOBAL);

	r_facePlaneCull = ri.Cvar_Get("r_facePlaneCull", "1", CVAR_ARCHIVE | CVAR_GLOBAL);

	r_primitives = ri.Cvar_Get("r_primitives", "0", CVAR_ARCHIVE | CVAR_GLOBAL);

	r_ambientScale = ri.Cvar_Get( "r_ambientScale", "0.6", CVAR_CHEAT );
	r_directedScale = ri.Cvar_Get( "r_directedScale", "1", CVAR_CHEAT );

	//
	// temporary variables that can change at any time
	//
	r_showImages = ri.Cvar_Get( "r_showImages", "0", CVAR_CHEAT );

	r_debugLight = ri.Cvar_Get( "r_debuglight", "0", CVAR_TEMP );
	r_debugSort = ri.Cvar_Get( "r_debugSort", "0", CVAR_CHEAT );
	r_printShaders = ri.Cvar_Get( "r_printShaders", "0", 0 );

	r_surfaceSprites = ri.Cvar_Get ("r_surfaceSprites", "1", CVAR_TEMP);
	r_surfaceWeather = ri.Cvar_Get ("r_surfaceWeather", "0", CVAR_TEMP);

	r_windSpeed = ri.Cvar_Get ("r_windSpeed", "0", 0);
	r_windAngle = ri.Cvar_Get ("r_windAngle", "0", 0);
	r_windGust = ri.Cvar_Get ("r_windGust", "0", 0);
	r_windDampFactor = ri.Cvar_Get ("r_windDampFactor", "0.1", 0);
	r_windPointForce = ri.Cvar_Get ("r_windPointForce", "0", 0);
	r_windPointX = ri.Cvar_Get ("r_windPointX", "0", 0);
	r_windPointY = ri.Cvar_Get ("r_windPointY", "0", 0);

	r_nocurves = ri.Cvar_Get ("r_nocurves", "0", CVAR_CHEAT );
	r_drawworld = ri.Cvar_Get ("r_drawworld", "1", CVAR_CHEAT );
	r_lightmap = ri.Cvar_Get ("r_lightmap", "0", CVAR_CHEAT );
	r_portalOnly = ri.Cvar_Get ("r_portalOnly", "0", CVAR_CHEAT );

	r_skipBackEnd = ri.Cvar_Get ("r_skipBackEnd", "0", CVAR_CHEAT);

	r_newDLights = ri.Cvar_Get ("r_newDLights", "0", 0);

	r_measureOverdraw = ri.Cvar_Get( "r_measureOverdraw", "0", CVAR_CHEAT );
	r_lodscale = ri.Cvar_Get( "r_lodscale", "5", 0 );
	r_norefresh = ri.Cvar_Get ("r_norefresh", "0", CVAR_CHEAT);
	r_drawentities = ri.Cvar_Get ("r_drawentities", "1", CVAR_CHEAT );
	r_ignore = ri.Cvar_Get( "r_ignore", "1", CVAR_CHEAT );
	r_nocull = ri.Cvar_Get ("r_nocull", "0", CVAR_CHEAT);
	r_novis = ri.Cvar_Get ("r_novis", "0", CVAR_CHEAT);
	r_showcluster = ri.Cvar_Get ("r_showcluster", "0", CVAR_CHEAT);
	r_speeds = ri.Cvar_Get ("r_speeds", "0", CVAR_CHEAT);
	r_verbose = ri.Cvar_Get( "r_verbose", "0", CVAR_CHEAT );
	r_logFile = ri.Cvar_Get( "r_logFile", "0", CVAR_CHEAT );
	r_debugSurface = ri.Cvar_Get ("r_debugSurface", "0", CVAR_CHEAT);
	r_nobind = ri.Cvar_Get ("r_nobind", "0", CVAR_CHEAT);
	r_showtris = ri.Cvar_Get ("r_showtris", "0", CVAR_CHEAT);
	r_showsky = ri.Cvar_Get ("r_showsky", "0", CVAR_CHEAT);
	r_shownormals = ri.Cvar_Get ("r_shownormals", "0", CVAR_CHEAT);
	r_clear = ri.Cvar_Get ("r_clear", "0", 0);
	r_offsetFactor = ri.Cvar_Get( "r_offsetfactor", "-1", CVAR_CHEAT );
	r_offsetUnits = ri.Cvar_Get( "r_offsetunits", "-2", CVAR_CHEAT );
	r_drawBuffer = ri.Cvar_Get( "r_drawBuffer", "GL_BACK", CVAR_CHEAT );
	r_lockpvs = ri.Cvar_Get ("r_lockpvs", "0", CVAR_CHEAT);
	r_noportals = ri.Cvar_Get ("r_noportals", "0", CVAR_CHEAT);
	r_shadows = ri.Cvar_Get( "cg_shadows", "1", 0 );

	r_maxpolys = ri.Cvar_Get( "r_maxpolys", va("%d", MAX_POLYS), 0);
	r_maxpolyverts = ri.Cvar_Get( "r_maxpolyverts", va("%d", MAX_POLYVERTS), 0);

	r_convertModelBones = ri.Cvar_Get( "r_convertModelBones", "1", CVAR_ARCHIVE | CVAR_GLOBAL );
	r_loadSkinsJKA = ri.Cvar_Get( "r_loadSkinsJKA", "1", CVAR_ARCHIVE | CVAR_GLOBAL );
/*
Ghoul2 Insert Start
*/
	r_noServerGhoul2 = ri.Cvar_Get( "r_noserverghoul2", "0", CVAR_CHEAT);

	r_Ghoul2AnimSmooth = ri.Cvar_Get( "r_ghoul2animsmooth", ".3", 0 );
	r_Ghoul2UnSqashAfterSmooth = ri.Cvar_Get( "r_ghoul2unsqashaftersmooth", "1", 0 );
/*
Ghoul2 Insert End
*/

	r_modelpoolmegs = Cvar_Get("r_modelpoolmegs", "20", CVAR_ARCHIVE | CVAR_GLOBAL);

	// make sure all the commands added here are also
	// removed in R_Shutdown
#ifndef DEDICATED
	ri.Cmd_AddCommand( "imagelist", R_ImageList_f );
	ri.Cmd_AddCommand( "shaderlist", R_ShaderList_f );
	ri.Cmd_AddCommand( "skinlist", R_SkinList_f );
	ri.Cmd_AddCommand( "screenshot", R_ScreenShot_f );
	ri.Cmd_AddCommand( "screenshot_tga", R_ScreenShotTGA_f );
	ri.Cmd_AddCommand( "gfxinfo", GfxInfo_f );
	ri.Cmd_AddCommand("r_we", R_WorldEffect_f);
	ri.Cmd_AddCommand( "imagecacheinfo", RE_RegisterImages_Info_f);
#endif
	ri.Cmd_AddCommand("modellist", R_Modellist_f);
	ri.Cmd_AddCommand( "modelcacheinfo", RE_RegisterModels_Info_f);

	r_screenshotJpegQuality = ri.Cvar_Get("r_screenshotJpegQuality", "95", CVAR_ARCHIVE | CVAR_GLOBAL);

	r_consoleFont = ri.Cvar_Get("r_consoleFont", "1", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_consoleFont->modified = qtrue;
	r_fontSharpness = ri.Cvar_Get("r_fontSharpness", "1", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_textureLODBias = ri.Cvar_Get("r_textureLODBias", "0", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_saberGlow = ri.Cvar_Get("r_saberGlow", "1", CVAR_ARCHIVE | CVAR_LATCH);
	r_environmentMapping = ri.Cvar_Get("r_environmentMapping", "1", CVAR_ARCHIVE | CVAR_GLOBAL);
	r_printMissingModels = ri.Cvar_Get("r_printMissingModels", "0", CVAR_ARCHIVE | CVAR_GLOBAL);
}

#ifdef G2_COLLISION_ENABLED
#define G2_VERT_SPACE_SERVER_SIZE 256
#endif

/*
===============
R_Init
===============
*/
void R_Init( void ) {
	int i;
	byte *ptr;

	ri.Printf( PRINT_ALL, "----- R_Init -----\n" );

	// clear all our internal state
	Com_Memset( &tr, 0, sizeof( tr ) );
	Com_Memset( &backEnd, 0, sizeof( backEnd ) );
#ifndef DEDICATED
	Com_Memset( &tess, 0, sizeof( tess ) );
#endif

//	Swap_Init();

#ifndef DEDICATED
	Com_Memset( tess.constantColor255, 255, sizeof( tess.constantColor255 ) );
#endif
	//
	// init function tables
	//
	for ( i = 0; i < FUNCTABLE_SIZE; i++ )
	{
		tr.sinTable[i]		= sinf( DEG2RAD( i * 360.0f / ( ( float ) ( FUNCTABLE_SIZE - 1 ) ) ) );
		tr.squareTable[i]	= ( i < FUNCTABLE_SIZE/2 ) ? 1.0f : -1.0f;
		tr.sawToothTable[i] = (float)i / FUNCTABLE_SIZE;
		tr.inverseSawToothTable[i] = 1.0f - tr.sawToothTable[i];

		if ( i < FUNCTABLE_SIZE / 2 )
		{
			if ( i < FUNCTABLE_SIZE / 4 )
			{
				tr.triangleTable[i] = ( float ) i / ( FUNCTABLE_SIZE / 4 );
			}
			else
			{
				tr.triangleTable[i] = 1.0f - tr.triangleTable[i-FUNCTABLE_SIZE / 4];
			}
		}
		else
		{
			tr.triangleTable[i] = -tr.triangleTable[i-FUNCTABLE_SIZE/2];
		}
	}
#ifndef DEDICATED
	R_InitFogTable();

	R_NoiseInit();
#endif
	R_Register();

	max_polys = r_maxpolys->integer;
	if (max_polys < MAX_POLYS)
		max_polys = MAX_POLYS;

	max_polyverts = r_maxpolyverts->integer;
	if (max_polyverts < MAX_POLYVERTS)
		max_polyverts = MAX_POLYVERTS;

	ptr = (unsigned char *)ri.Hunk_Alloc( sizeof( *backEndData ) + sizeof(srfPoly_t) * max_polys + sizeof(polyVert_t) * max_polyverts, h_low);
	backEndData = (backEndData_t *) ptr;
	backEndData->polys = (srfPoly_t *) ((char *) ptr + sizeof( *backEndData ));
	backEndData->polyVerts = (polyVert_t *) ((char *) ptr + sizeof( *backEndData ) + sizeof(srfPoly_t) * max_polys);
	
#ifndef DEDICATED
	R_InitNextFrame();

	for(i = 0; i < MAX_LIGHT_STYLES; i++)
	{
		RE_SetLightStyle(i, -1);
	}
	InitVulkan();

	R_InitImages();
	R_InitShaders();
	R_InitSkins();
	R_InitFonts();
#endif
	R_ModelInit();
#ifndef DEDICATED

#ifdef G2_COLLISION_ENABLED
	if (!G2VertSpaceServer)
	{
		G2VertSpaceServer = new CMiniHeap(G2_VERT_SPACE_SERVER_SIZE * 1024);
	}
#endif

#endif
	ri.Printf( PRINT_ALL, "----- finished R_Init -----\n" );
}

/*
===============
RE_Shutdown
===============
*/
void RE_Shutdown( qboolean destroyWindow ) {

	ri.Printf( PRINT_ALL, "RE_Shutdown( %i )\n", destroyWindow );


	ri.Cmd_RemoveCommand ("imagelist");
	ri.Cmd_RemoveCommand ("shaderlist");
	ri.Cmd_RemoveCommand ("skinlist");
	ri.Cmd_RemoveCommand ("screenshot");
	ri.Cmd_RemoveCommand ("screenshot_tga");
	ri.Cmd_RemoveCommand ("gfxinfo");
	ri.Cmd_RemoveCommand ("r_we");
	ri.Cmd_RemoveCommand ("imagecacheinfo");

	ri.Cmd_RemoveCommand ("modellist");
	ri.Cmd_RemoveCommand ("modelcacheinfo");


#ifndef DEDICATED
	// Vulkan: glow and gamma resources are cleaned up by VK_Shutdown

	R_ShutdownFonts();
	if ( tr.registered ) {
		R_SyncRenderThread();
		if (destroyWindow)
		{
			R_DeleteTextures();		// only do this for vid_restart now, not during things like map load
		}
	}

	// shut down Vulkan and platform window
	if ( destroyWindow ) {
		VK_Shutdown();
		WIN_Shutdown();
		glConfig.vidWidth = 0;
	} else {
		// Map change without vid_restart — invalidate acceleration structures
		// so they are rebuilt from the new map's BSP geometry.
		VK_InvalidateGlowReflectAccelStruct();
	}
#endif //!DEDICATED

	tr.registered = qfalse;

#ifdef G2_COLLISION_ENABLED
	if (G2VertSpaceServer)
	{
		delete G2VertSpaceServer;
		G2VertSpaceServer = 0;
	}
#endif
}

#ifndef DEDICATED

/*
=============
RE_EndRegistration

Touch all images to make sure they are resident
=============
*/
void RE_EndRegistration( void ) {
	R_SyncRenderThread();
}

void RE_GetLightStyle(int style, color4ub_t color)
{
	if ((unsigned)style >= (unsigned)MAX_LIGHT_STYLES)
	{
		ri.Error( ERR_FATAL, "RE_GetLightStyle: %d is out of range", (int)style );
		return;
	}

	memcpy(color, styleColors[style], 4);
}

void RE_SetLightStyle(int style, int color)
{
	if ((unsigned)style >= (unsigned)MAX_LIGHT_STYLES)
	{
		ri.Error( ERR_FATAL, "RE_SetLightStyle: %d is out of range", (int)style );
		return;
	}

	memcpy(styleColors[style], &color, 4);
}

void RE_UpdateGLConfig( glconfig_t *glconfigOut ) {
	int		oldWidth = glConfig.vidWidth;
	int		oldHeight = glConfig.vidHeight;

	WIN_UpdateGLConfig( &glConfig );

	if (oldWidth != glConfig.vidWidth || oldHeight != glConfig.vidHeight) {
		R_SyncRenderThread();
		R_UpdateImages();
	}

	glconfigOut->vidWidth = glConfig.vidWidth;
	glconfigOut->vidHeight = glConfig.vidHeight;
	glconfigOut->displayScale = glConfig.displayScale;
}

#endif //!DEDICATED
/*
@@@@@@@@@@@@@@@@@@@@@
GetRefAPI

@@@@@@@@@@@@@@@@@@@@@
*/
refexport_t *GetRefAPI ( int apiVersion, refimport_t *rimp ) {
	static refexport_t	re;

	ri = *rimp;

	Com_Memset( &re, 0, sizeof( re ) );

	if ( apiVersion != REF_API_VERSION ) {
		ri.Printf(PRINT_ALL, "Mismatched REF_API_VERSION: expected %i, got %i\n",
			REF_API_VERSION, apiVersion );
		return NULL;
	}

	// the RE_ functions are Renderer Entry points

	re.Shutdown = RE_Shutdown;
#ifndef DEDICATED
	re.BeginRegistration = RE_BeginRegistration;
	re.RegisterModel = RE_RegisterModel;
	re.RegisterSkin = RE_RegisterSkin;
	re.RegisterShader = RE_RegisterShader;
	re.RegisterShaderNoMip = RE_RegisterShaderNoMip;
	re.LoadWorld = RE_LoadWorldMap;
	re.SetWorldVisData = RE_SetWorldVisData;
	re.EndRegistration = RE_EndRegistration;

	re.UpdateGLConfig = RE_UpdateGLConfig;
	re.BeginFrame = RE_BeginFrame;
	re.EndFrame = RE_EndFrame;
	re.SwapBuffers = RE_SwapBuffers;

	re.MarkFragments = R_MarkFragments;
	re.LerpTag = R_LerpTag;
	re.ModelBounds = R_ModelBounds;

	re.DrawRotatePic = RE_RotatePic;
	re.DrawRotatePic2 = RE_RotatePic2;

	re.ClearScene = RE_ClearScene;
	re.AddRefEntityToScene = RE_AddRefEntityToScene;
	re.AddMiniRefEntityToScene = RE_AddMiniRefEntityToScene;
	re.AddPolyToScene = RE_AddPolyToScene;
	re.LightForPoint = R_LightForPoint;
	re.AddLightToScene = RE_AddLightToScene;
	re.AddAdditiveLightToScene = RE_AddAdditiveLightToScene;
	re.RenderScene = RE_RenderScene;

	re.SetColor = RE_SetColor;
	re.DrawStretchPic = RE_StretchPic;
	re.DrawStretchRaw = RE_StretchRaw;
	re.UploadCinematic = RE_UploadCinematic;

	re.RegisterFont = RE_RegisterFont;
	re.Font_StrLenPixels = RE_Font_StrLenPixels;
	re.Font_StrLenChars = RE_Font_StrLenChars;
	re.Font_HeightPixels = RE_Font_HeightPixels;
	re.Font_DrawString = RE_Font_DrawString;
	re.Language_IsAsian = Language_IsAsian;
	re.Language_UsesSpaces = Language_UsesSpaces;
	re.AnyLanguage_ReadCharFromString = AnyLanguage_ReadCharFromString;

	re.RemapShader = R_RemapShader;
	re.RemapShaderAdvanced = R_RemapShaderAdvanced;
	re.RemoveAdvancedRemaps = R_RemoveAdvancedRemaps;
	re.GetEntityToken = R_GetEntityToken;
	re.inPVS = R_inPVS;

	re.GetLightStyle = RE_GetLightStyle;
	re.SetLightStyle = RE_SetLightStyle;

	re.GetBModelVerts = RE_GetBModelVerts;

	re.CaptureFrameRaw = RE_CaptureFrameRaw;
	re.CaptureFrameJPEG = RE_CaptureFrameJPEG;
#endif //!DEDICATED
	return &re;
}

