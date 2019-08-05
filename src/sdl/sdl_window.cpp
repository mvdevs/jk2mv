#include <SDL.h>
#include <SDL_syswm.h>
#include "../qcommon/qcommon.h"
#include "../sys/sys_local.h"
#include "../renderer/tr_public.h"
#include "sdl_icon.h"

#define CLIENT_WINDOW_TITLE "JK2MV"

enum rserr_t
{
	RSERR_OK,

	RSERR_INVALID_FULLSCREEN,
	RSERR_INVALID_MODE,

	RSERR_UNKNOWN
};

static SDL_Window *screen = NULL;
static SDL_GLContext opengl_context;
static float displayAspect;

static cvar_t *r_sdlDriver;

// Window cvars
cvar_t			*r_fullscreen;
static cvar_t	*r_noborder;
static cvar_t	*r_centerWindow;
static cvar_t	*r_customwidth;
static cvar_t	*r_customheight;
static cvar_t	*r_swapInterval;
static cvar_t	*r_stereo;
cvar_t			*r_mode;
cvar_t			*r_displayRefresh;
static cvar_t	*r_savedWindows;

// Window surface cvars
static cvar_t	*r_stencilbits;
static cvar_t	*r_depthbits;
static cvar_t	*r_colorbits;
static cvar_t	*r_ext_multisample;
static cvar_t	*r_allowsoftwaregl;
cvar_t			*r_gammamethod;

/*
** R_GetModeInfo
*/
typedef struct vidmode_s
{
    const char *description;
    int         width, height;
} vidmode_t;

static const vidmode_t r_vidModes[] = {
	// 4:3
	{ "Mode  0:  320x240",	320,  240},
	{ "Mode  1:  400x300",	400,  300},
	{ "Mode  2:  512x384",	512,  384},
	{ "Mode  3:  640x480",	640,  480},
	{ "Mode  4:  800x600",	800,  600},
	{ "Mode  5:  960x720",	960,  720},
	{ "Mode  6:  1024x768",   1024, 768},
	{ "Mode  7:  1152x864",   1152, 864},
	{ "Mode  8:  1280x1024",  1280, 1024},
	{ "Mode  9:  1600x1200",  1600, 1200},
	{ "Mode  10: 2048x1536",  2048, 1536},

	// 16:9
	{ "Mode  11: 960x540",	960,  540},
	{ "Mode  12: 960x544",	960,  544},
	{ "Mode  13: 1024x576",   1024, 576},
	{ "Mode  14: 1024x600",   1024, 600},
	{ "Mode  15: 1136x640",   1136, 640},
	{ "Mode  16: 1280x720",   1280, 720},
	{ "Mode  17: 1366x768",   1366, 768},
	{ "Mode  18: 1600x900",   1600, 900},
	{ "Mode  19: 1920x1080",  1920, 1080},
	{ "Mode  20: 2048x1152",  2048, 1152},
	{ "Mode  21: 2560x1440",  2560, 1440},
	{ "Mode  22: 2880x1620",  2880, 1620},
	{ "Mode  23: 3200x1800",  3200, 1800},
	{ "Mode  24: 3840x2160",  3840, 2160},
	{ "Mode  25: 4096x2304",  4096, 2304},
	{ "Mode  26: 5120x2880",  5120, 2880},

	// 16:10
	{ "Mode  27: 1280x800",   1280, 800},
	{ "Mode  28: 1440x900",   1440, 900},
	{ "Mode  29: 1680x1050",  1680, 1050},
	{ "Mode  30: 1920x1200",  1920, 1200},
	{ "Mode  31: 2560x1600",  2560, 1600},
};
static const int	s_numVidModes = ARRAY_LEN( r_vidModes );

#define R_MODE_FALLBACK 3 // 640x480

qboolean R_GetModeInfo( int *width, int *height, int mode ) {
	const vidmode_t	*vm;

    if ( mode < -1 ) {
        return qfalse;
	}
	if ( mode >= s_numVidModes ) {
		return qfalse;
	}

	if ( mode == -1 ) {
		*width = r_customwidth->integer;
		*height = r_customheight->integer;
		return qtrue;
	}

	vm = &r_vidModes[mode];

    *width  = vm->width;
    *height = vm->height;

    return qtrue;
}

/*
** R_ModeList_f
*/
static void R_ModeList_f( void )
{
	int i;

	Com_Printf( "\n" );
	Com_Printf( "Mode -2: Use desktop resolution\n" );
	Com_Printf( "Mode -1: Use r_customWidth and r_customHeight variables\n" );
	for ( i = 0; i < s_numVidModes; i++ )
	{
		Com_Printf( "%s\n", r_vidModes[i].description );
	}
	Com_Printf( "\n" );
}

// Procedures for saving and restoring window position
typedef struct {
	SDL_Rect	display;
	int			xpos;
	int			ypos;
} savedWindow_t;

#define MAX_SAVED 8

static void GLimp_SerializeWindowPosition( char *token, size_t size, const savedWindow_t *saved )
{
	// generate saved window data eg. 1280x800+1024+0:-30+60 first 4
	// integers are X11 geometry format for current display, last 2
	// are window position relative to current display
	Com_sprintf( token, size, "%dx%d%+d%+d:%+d%+d",
		saved->display.w, saved->display.h,
		saved->display.x, saved->display.y,
		saved->xpos, saved->ypos );
}

static qboolean GLimp_DeserializeWindowPosition( savedWindow_t *saved, const char *token )
{
	char	testToken[MAX_CVAR_VALUE_STRING];
	int		consumed;
	int		ret;

	ret = sscanf( token, "%dx%d%d%d:%d%d%n",
		&saved->display.w, &saved->display.h,
		&saved->display.x, &saved->display.y,
		&saved->xpos, &saved->ypos, &consumed );

	// make sure we read exactly 6 integers
	if ( ret != 6 )
		return qfalse;

	// make sure there are no extra characters at the end
	if ( token[consumed] != '\0' )
		return qfalse;

	// range tests; coordinates can be negative and visible
	if ( saved->display.w <= 0 || saved->display.h <= 0 )
		return qfalse;

	// inverse function test
	GLimp_SerializeWindowPosition( testToken, sizeof( testToken ), saved );

	if ( strcmp( token, testToken ) )
		return qfalse;

	return qtrue;
}

static int SDL_RectCmp( SDL_Rect *r1, SDL_Rect *r2 )
{
	return r1->w != r2->w || r1->h != r2->h || r1->x != r2->x || r1->y != r2->y;
}

void GLimp_SaveWindowPosition( void )
{
	char			oldString[MAX_CVAR_VALUE_STRING];
	char			newString[MAX_CVAR_VALUE_STRING];
	char			*token;
	int				display;
	int				i;
	savedWindow_t	saved;
	savedWindow_t	newSaved;

	if ( !screen )
		return;

	// display containing window's center
	display = SDL_GetWindowDisplayIndex( screen );

	if ( display < 0 )
		return;

	if ( SDL_GetDisplayBounds( display, &newSaved.display ) != 0 )
		return;

	SDL_GetWindowPosition( screen, &newSaved.xpos, &newSaved.ypos );

	// workaround for x11 decorations shifting new window down
#if SDL_VERSION_ATLEAST(2, 0, 5)
	// SDL_GetWindowBordersSize segfaults on macOS
	if ( !strcmp( SDL_GetCurrentVideoDriver(), "x11" ) ) {
		int				top, left;

		if ( SDL_GetWindowBordersSize( screen, &top, &left, NULL, NULL ) == 0 ) {
			newSaved.ypos -= top;
			newSaved.xpos -= left;
		}
	}
#endif

	newSaved.xpos -= newSaved.display.x;
	newSaved.ypos -= newSaved.display.y;

	Q_strncpyz( oldString, r_savedWindows->string, sizeof( oldString ) );

	GLimp_SerializeWindowPosition( newString, sizeof( newString ), &newSaved );

	// deserialize oldString and write it back
	token = strtok( oldString, " " );
	i = 1;

	while ( token && i < MAX_SAVED ) {
		// skip malformed token
		if ( GLimp_DeserializeWindowPosition( &saved, token ) ) {
			// replace token describing position on the same display
			if ( SDL_RectCmp( &saved.display, &newSaved.display ) ) {
				Q_strcat( newString, sizeof( newString ), " " );
				Q_strcat( newString, sizeof( newString ), token );

				i++;
			}
		}

		token = strtok( NULL, " " );
	}

	Cvar_Set( "r_savedWindows", newString );
}

static qboolean GLimp_GetSavedWindowPosition( int *dsp, int *xpos, int *ypos )
{
	savedWindow_t	saved;
	qboolean		found = qfalse;
	char			buf[MAX_CVAR_VALUE_STRING];
	char			*token;
	int				numDisplays = SDL_GetNumVideoDisplays();
	int				display = numDisplays;
	int				x, y;

	// find latest window position token matching existing display
	Q_strncpyz( buf, r_savedWindows->string, sizeof( buf ) );

	token = strtok( buf, " " );

	while ( token && !found ) {
		if ( GLimp_DeserializeWindowPosition( &saved, token ) ) {
			// use saved token if the same display is present
			for ( display = 0; display < numDisplays; display++ ) {
				SDL_Rect	rect;

				if ( SDL_GetDisplayBounds( display, &rect ) != 0 )
					continue;

				if ( !SDL_RectCmp( &saved.display, &rect ) ) {
					x = saved.xpos + saved.display.x;
					y = saved.ypos + saved.display.y;
					found = qtrue;
					break;
				}
			}
		}

		token = strtok( NULL, " " );
	}

	if ( found ) {
		*dsp = display;
		*xpos = x;
		*ypos = y;
	}

	return found;
}

/*
===============
GLimp_Minimize

Minimize the game so that user is back at the desktop
===============
*/
void GLimp_Minimize(void)
{
	SDL_MinimizeWindow( screen );
}

void WIN_Present( window_t *window )
{
	if ( window->api == GRAPHICS_API_OPENGL )
	{
		SDL_GL_SwapWindow(screen);

		if ( r_swapInterval->modified )
		{
			r_swapInterval->modified = qfalse;
			if ( SDL_GL_SetSwapInterval( r_swapInterval->integer ) == -1 )
			{
				Com_DPrintf( "SDL_GL_SetSwapInterval failed: %s\n", SDL_GetError() );
			}
		}
	}

	if ( r_fullscreen->modified )
	{
		bool	fullscreen;
		bool	needToToggle;
		bool	sdlToggled = qfalse;

		// Find out the current state
		fullscreen = (SDL_GetWindowFlags( screen ) & SDL_WINDOW_FULLSCREEN) != 0;

		if ( r_fullscreen->integer && Cvar_VariableIntegerValue( "in_nograb" ) )
		{
			Com_Printf( "Fullscreen not allowed with in_nograb 1\n" );
			Cvar_Set( "r_fullscreen", "0" );
			r_fullscreen->modified = qfalse;
		}

		// Is the state we want different from the current state?
		needToToggle = !!r_fullscreen->integer != fullscreen;

		if ( needToToggle )
		{
			fullscreen = r_fullscreen->integer ? SDL_WINDOW_FULLSCREEN : 0;
			sdlToggled = SDL_SetWindowFullscreen( screen, fullscreen ) >= 0;

			// SDL_WM_ToggleFullScreen didn't work, so do it the slow way
			if ( !sdlToggled )
			{
				Cbuf_AddText( "vid_restart\n" );
			}
			else if ( !fullscreen )
			{
				int x, y;
				int display = 0;

				if ( !GLimp_GetSavedWindowPosition( &display, &x, &y ) )
				{
					x = y = SDL_WINDOWPOS_CENTERED;
				}

				SDL_SetWindowPosition( screen, x, y );

				IN_Restart();
			}
		}

		r_fullscreen->modified = qfalse;
	}
}

/*
===============
GLimp_CompareModes
===============
*/
static int GLimp_CompareModes( const void *a, const void *b )
{
	const float ASPECT_EPSILON = 0.001f;
	const SDL_Rect *modeA = (const SDL_Rect *)a;
	const SDL_Rect *modeB = (const SDL_Rect *)b;
	float aspectA = (float)modeA->w / (float)modeA->h;
	float aspectB = (float)modeB->w / (float)modeB->h;
	int areaA = modeA->w * modeA->h;
	int areaB = modeB->w * modeB->h;
	float aspectDiffA = fabsf( aspectA - displayAspect );
	float aspectDiffB = fabsf( aspectB - displayAspect );
	float aspectDiffsDiff = aspectDiffA - aspectDiffB;

	if( aspectDiffsDiff > ASPECT_EPSILON )
		return 1;
	else if( aspectDiffsDiff < -ASPECT_EPSILON )
		return -1;
	else
		return areaA - areaB;
}

/*
===============
GLimp_DetectAvailableModes
===============
*/
static bool GLimp_DetectAvailableModes(void)
{
	int i, j;
	char buf[ MAX_STRING_CHARS ] = { 0 };
	SDL_Rect *modes;
	int numModes = 0;

	int display = SDL_GetWindowDisplayIndex( screen );
	if ( display < 0 )
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: Couldn't get window display index, no resolutions detected: %s\n", SDL_GetError() );
		return false;
	}

	SDL_DisplayMode windowMode;

	if( SDL_GetWindowDisplayMode( screen, &windowMode ) < 0 )
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: Couldn't get window display mode, no resolutions detected (%s).\n", SDL_GetError() );
		return false;
	}

	int numDisplayModes = SDL_GetNumDisplayModes( display );
	if ( numDisplayModes < 0 )
		Com_Error( ERR_FATAL, "SDL_GetNumDisplayModes() FAILED (%s)", SDL_GetError() );

	modes = (SDL_Rect *)SDL_calloc( (size_t)numDisplayModes, sizeof( SDL_Rect ) );
	if ( !modes )
		Com_Error( ERR_FATAL, "Out of memory" );

	for( i = 0; i < numDisplayModes; i++ )
	{
		SDL_DisplayMode mode;

		if( SDL_GetDisplayMode( display, i, &mode ) < 0 )
			continue;

		if( !mode.w || !mode.h )
		{
			Com_Printf( "Display supports any resolution\n" );
			SDL_free( modes );
			return true;
		}

		if( windowMode.format != mode.format )
			continue;

		// SDL can give the same resolution with different refresh rates.
		// Only list resolution once.
		for( j = 0; j < numModes; j++ )
		{
			if( mode.w == modes[ j ].w && mode.h == modes[ j ].h )
				break;
		}

		if( j != numModes )
			continue;

		modes[ numModes ].w = mode.w;
		modes[ numModes ].h = mode.h;
		numModes++;
	}

	if( numModes > 1 )
		qsort( modes, numModes, sizeof( SDL_Rect ), GLimp_CompareModes );

	for( i = 0; i < numModes; i++ )
	{
		const char *newModeString = va( "%ux%u ", modes[ i ].w, modes[ i ].h );

		if( strlen( newModeString ) < (int)sizeof( buf ) - strlen( buf ) )
			Q_strcat( buf, sizeof( buf ), newModeString );
		else
			Com_Printf( "Skipping mode %ux%u, buffer too small\n", modes[ i ].w, modes[ i ].h );
	}

	if( *buf )
	{
		buf[ strlen( buf ) - 1 ] = 0;
		Com_Printf( "Available modes: '%s'\n", buf );
		Cvar_Set( "r_availableModes", buf );
	}

	SDL_free( modes );
	return true;
}

/*
===============
GLimp_SetMode
===============
*/
static rserr_t GLimp_SetMode(glconfig_t *glConfig, const windowDesc_t *windowDesc, const char *windowTitle, int mode, qboolean fullscreen, qboolean noborder)
{
	int perChannelColorBits;
	int colorBits, depthBits, stencilBits;
	int samples;
	int i = 0;
	SDL_Surface *icon = NULL;
	Uint32 flags = SDL_WINDOW_SHOWN;
	SDL_DisplayMode desktopMode;
	int display = 0;
	int x = SDL_WINDOWPOS_CENTERED, y = SDL_WINDOWPOS_CENTERED;

	if ( windowDesc->api == GRAPHICS_API_OPENGL )
	{
		flags |= SDL_WINDOW_OPENGL;
	}

	Com_Printf( "Initializing display\n");

	icon = SDL_CreateRGBSurfaceFrom(
		(void *)CLIENT_WINDOW_ICON.pixel_data,
		CLIENT_WINDOW_ICON.width,
		CLIENT_WINDOW_ICON.height,
		CLIENT_WINDOW_ICON.bytes_per_pixel * 8,
		CLIENT_WINDOW_ICON.bytes_per_pixel * CLIENT_WINDOW_ICON.width,
		0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
		);

	// If a window exists, note its display index
	if ( screen != NULL )
	{
		display = SDL_GetWindowDisplayIndex( screen );
		if ( display < 0 )
		{
			Com_DPrintf( "SDL_GetWindowDisplayIndex() failed: %s\n", SDL_GetError() );
		}
	}

	if( display >= 0 && SDL_GetDesktopDisplayMode( display, &desktopMode ) == 0 )
	{
		displayAspect = (float)desktopMode.w / (float)desktopMode.h;

		Com_Printf( "Display aspect: %.3f\n", displayAspect );
	}
	else
	{
		Com_Memset( &desktopMode, 0, sizeof( SDL_DisplayMode ) );

		Com_Printf( "Cannot determine display aspect, assuming 1.333\n" );
	}

	Com_Printf( "...setting mode %d:", mode );

	if (mode == -2)
	{
		// use desktop video resolution
		if( desktopMode.h > 0 )
		{
			glConfig->vidWidth = desktopMode.w;
			glConfig->vidHeight = desktopMode.h;
		}
		else
		{
			glConfig->vidWidth = 640;
			glConfig->vidHeight = 480;
			Com_Printf( "Cannot determine display resolution, assuming 640x480\n" );
		}

		//glConfig.windowAspect = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
	}
	else if ( !R_GetModeInfo( &glConfig->vidWidth, &glConfig->vidHeight, /*&glConfig.windowAspect,*/ mode ) )
	{
		Com_Printf( " invalid mode\n" );
		SDL_FreeSurface(icon);
		return RSERR_INVALID_MODE;
	}
	Com_Printf( " %d %d\n", glConfig->vidWidth, glConfig->vidHeight);

	// Destroy existing state if it exists
	if( opengl_context != NULL )
	{
		SDL_GL_DeleteContext( opengl_context );
		opengl_context = NULL;
	}

	if( screen != NULL )
	{
		SDL_GetWindowPosition( screen, &x, &y );
		Com_DPrintf( "Existing window at %dx%d before being destroyed\n", x, y );
		SDL_DestroyWindow( screen );
		screen = NULL;
	} else {
		if ( GLimp_GetSavedWindowPosition( &display, &x, &y ) )
			Com_DPrintf( "Found saved window at %dx%d display %d\n", x, y, display );
	}

	if ( r_centerWindow->integer )
	{
		x = SDL_WINDOWPOS_CENTERED_DISPLAY( display );
		y = SDL_WINDOWPOS_CENTERED_DISPLAY( display );
	}

	if( fullscreen )
	{
		flags |= SDL_WINDOW_FULLSCREEN;
		glConfig->isFullscreen = qtrue;
	}
	else
	{
		if( noborder )
			flags |= SDL_WINDOW_BORDERLESS;

		glConfig->isFullscreen = qfalse;
	}

	colorBits = r_colorbits->integer;
	if ((!colorBits) || (colorBits >= 32))
		colorBits = 24;

	if (!r_depthbits->integer)
		depthBits = 24;
	else
		depthBits = r_depthbits->integer;

	stencilBits = r_stencilbits->integer;
	samples = r_ext_multisample->integer;

	if ( windowDesc->api == GRAPHICS_API_OPENGL )
	{
		for (i = 0; i < 16; i++)
		{
			int testColorBits, testDepthBits, testStencilBits;

			// 0 - default
			// 1 - minus colorBits
			// 2 - minus depthBits
			// 3 - minus stencil
			if ((i % 4) == 0 && i)
			{
				// one pass, reduce
				switch (i / 4)
				{
					case 2 :
						if (colorBits == 24)
							colorBits = 16;
						break;
					case 1 :
						if (depthBits == 24)
							depthBits = 16;
						else if (depthBits == 16)
							depthBits = 8;
					case 3 :
						if (stencilBits == 24)
							stencilBits = 16;
						else if (stencilBits == 16)
							stencilBits = 8;
				}
			}

			testColorBits = colorBits;
			testDepthBits = depthBits;
			testStencilBits = stencilBits;

			if ((i % 4) == 3)
			{ // reduce colorBits
				if (testColorBits == 24)
					testColorBits = 16;
			}

			if ((i % 4) == 2)
			{ // reduce depthBits
				if (testDepthBits == 24)
					testDepthBits = 16;
				else if (testDepthBits == 16)
					testDepthBits = 8;
			}

			if ((i % 4) == 1)
			{ // reduce stencilBits
				if (testStencilBits == 24)
					testStencilBits = 16;
				else if (testStencilBits == 16)
					testStencilBits = 8;
				else
					testStencilBits = 0;
			}

			if (testColorBits == 24)
				perChannelColorBits = 8;
			else
				perChannelColorBits = 4;

			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, testDepthBits );
			SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, testStencilBits );

			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, samples ? 1 : 0 );
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, samples );

			if ( windowDesc->gl.majorVersion )
			{
				int compactVersion = windowDesc->gl.majorVersion * 100 + windowDesc->gl.minorVersion * 10;

				SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, windowDesc->gl.majorVersion );
				SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, windowDesc->gl.minorVersion );

				if ( windowDesc->gl.profile == GLPROFILE_ES || compactVersion >= 320 )
				{
					int profile;
					switch ( windowDesc->gl.profile )
					{
					default:
					case GLPROFILE_COMPATIBILITY:
						profile = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
						break;

					case GLPROFILE_CORE:
						profile = SDL_GL_CONTEXT_PROFILE_CORE;
						break;

					case GLPROFILE_ES:
						profile = SDL_GL_CONTEXT_PROFILE_ES;
						break;
					}

					SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, profile );
				}
			}

			if ( windowDesc->gl.contextFlags & GLCONTEXT_DEBUG )
			{
				SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG );
			}

			if(r_stereo->integer)
			{
				glConfig->stereoEnabled = qtrue;
				SDL_GL_SetAttribute(SDL_GL_STEREO, 1);
			}
			else
			{
				glConfig->stereoEnabled = qfalse;
				SDL_GL_SetAttribute(SDL_GL_STEREO, 0);
			}

			SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
			SDL_GL_SetAttribute( SDL_GL_ACCELERATED_VISUAL, !r_allowsoftwaregl->integer);

			if( ( screen = SDL_CreateWindow( windowTitle, x, y,
					glConfig->vidWidth, glConfig->vidHeight, flags ) ) == NULL )
			{
				if ( samples > 0 ) {
					SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 0 );
					SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, 0 );

					screen = SDL_CreateWindow( windowTitle, x, y,
						glConfig->vidWidth, glConfig->vidHeight, flags );
				}
			}

			if ( screen == NULL ) {
				Com_DPrintf( "SDL_CreateWindow failed: %s\n", SDL_GetError( ) );
				continue;
			}


#ifndef MACOS_X
			SDL_SetWindowIcon(screen, icon);
#endif

			if( fullscreen )
			{
				SDL_DisplayMode mode;

				switch( testColorBits )
				{
					case 16: mode.format = SDL_PIXELFORMAT_RGB565; break;
					case 24: mode.format = SDL_PIXELFORMAT_RGB24;  break;
					default: Com_DPrintf( "testColorBits is %d, can't fullscreen\n", testColorBits ); continue;
				}

				mode.w = glConfig->vidWidth;
				mode.h = glConfig->vidHeight;
				mode.refresh_rate = glConfig->displayFrequency = r_displayRefresh->integer;
				mode.driverdata = NULL;

				if( SDL_SetWindowDisplayMode( screen, &mode ) < 0 )
				{
					Com_DPrintf( "SDL_SetWindowDisplayMode failed: %s\n", SDL_GetError( ) );
					continue;
				}
			}

			if( ( opengl_context = SDL_GL_CreateContext( screen ) ) == NULL )
			{
				Com_Printf( "SDL_GL_CreateContext failed: %s\n", SDL_GetError( ) );
				continue;
			}

			if ( SDL_GL_SetSwapInterval( r_swapInterval->integer ) == -1 )
			{
				Com_DPrintf( "SDL_GL_SetSwapInterval failed: %s\n", SDL_GetError() );
			}

			glConfig->colorBits = testColorBits;
			glConfig->depthBits = testDepthBits;
			glConfig->stencilBits = testStencilBits;

			Com_Printf( "Using %d color bits, %d depth, %d stencil display.\n",
					glConfig->colorBits, glConfig->depthBits, glConfig->stencilBits );
			break;
		}

		if (opengl_context == NULL) {
			SDL_FreeSurface(icon);
			return RSERR_UNKNOWN;
		}
	}
	else
	{
		// Just create a regular window
		if( ( screen = SDL_CreateWindow( windowTitle, x, y,
				glConfig->vidWidth, glConfig->vidHeight, flags ) ) == NULL )
		{
			Com_DPrintf( "SDL_CreateWindow failed: %s\n", SDL_GetError( ) );
		}
		else
		{
#ifndef MACOS_X
			SDL_SetWindowIcon(screen, icon);
#endif

			if( fullscreen )
			{
				if( SDL_SetWindowDisplayMode( screen, NULL ) < 0 )
				{
					Com_DPrintf( "SDL_SetWindowDisplayMode failed: %s\n", SDL_GetError( ) );
				}
			}
		}
	}

	glConfig->displayDPI = 96.0f;
#if SDL_VERSION_ATLEAST(2, 0, 4)
	float ddpi;
	if (!SDL_GetDisplayDPI(display, &ddpi, NULL, NULL)) {
		glConfig->displayDPI = ddpi;
	}
	Com_DPrintf("SDL_CreateWindow: Screen DPI: %f\n", glConfig->displayDPI);
#endif

	SDL_FreeSurface(icon);

	if (!GLimp_DetectAvailableModes())
	{
		return RSERR_UNKNOWN;
	}

	return RSERR_OK;
}

/*
===============
GLimp_StartDriverAndSetMode
===============
*/
static qboolean GLimp_StartDriverAndSetMode(glconfig_t *glConfig, const windowDesc_t *windowDesc, int mode, qboolean fullscreen, qboolean noborder)
{
	rserr_t err;

	if (!SDL_WasInit(SDL_INIT_VIDEO))
	{
		const char *driverName;

		if (SDL_Init(SDL_INIT_VIDEO) == -1)
		{
			Com_Printf( "SDL_Init( SDL_INIT_VIDEO ) FAILED (%s)\n", SDL_GetError());
			return qfalse;
		}

		driverName = SDL_GetCurrentVideoDriver();

		if (!driverName)
		{
			Com_Error( ERR_FATAL, "No video driver initialized" );
			return qfalse;
		}

		Com_Printf( "SDL using driver \"%s\"\n", driverName );
		Cvar_Set( "r_sdlDriver", driverName );
	}

	if (SDL_GetNumVideoDisplays() <= 0)
	{
		Com_Error( ERR_FATAL, "SDL_GetNumVideoDisplays() FAILED (%s)", SDL_GetError() );
	}

	if (fullscreen && Cvar_VariableIntegerValue( "in_nograb" ) )
	{
		Com_Printf( "Fullscreen not allowed with in_nograb 1\n");
		Cvar_Set( "r_fullscreen", "0" );
		r_fullscreen->modified = qfalse;
		fullscreen = qfalse;
	}

	err = GLimp_SetMode(glConfig, windowDesc, CLIENT_WINDOW_TITLE, mode, fullscreen, noborder);

	switch ( err )
	{
		case RSERR_INVALID_FULLSCREEN:
			Com_Printf( "...WARNING: fullscreen unavailable in this mode\n" );
			return qfalse;
		case RSERR_INVALID_MODE:
			Com_Printf( "...WARNING: could not set the given mode (%d)\n", mode );
			return qfalse;
		case RSERR_UNKNOWN:
			Com_Printf( "...ERROR: no display modes could be found.\n" );
			return qfalse;
		default:
			break;
	}

	return qtrue;
}

window_t WIN_Init( const windowDesc_t *windowDesc, glconfig_t *glConfig )
{
	Cmd_AddCommand("modelist", R_ModeList_f);
	Cmd_AddCommand("minimize", GLimp_Minimize);

	r_sdlDriver			= Cvar_Get( "r_sdlDriver",			"",			CVAR_ROM );

	// Window cvars
	r_fullscreen		= Cvar_Get( "r_fullscreen",			"1",		CVAR_ARCHIVE | CVAR_GLOBAL );
	r_noborder			= Cvar_Get( "r_noborder",			"0",		CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH );
	r_centerWindow		= Cvar_Get( "r_centerWindow",		"0",		CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH );
	r_customwidth		= Cvar_Get( "r_customwidth",		"1600",		CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH );
	r_customheight		= Cvar_Get( "r_customheight",		"1024",		CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH );
	r_swapInterval		= Cvar_Get( "r_swapInterval",		"0",		CVAR_ARCHIVE | CVAR_GLOBAL );
	r_stereo			= Cvar_Get( "r_stereo",				"0",		CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH );
	r_mode				= Cvar_Get( "r_mode",				"-2",		CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH );
	r_displayRefresh	= Cvar_Get( "r_displayRefresh",		"0",		CVAR_LATCH );
	r_savedWindows		= Cvar_Get( "r_savedWindows",		" ",		CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_ROM );

	// Window render surface cvars
	r_stencilbits		= Cvar_Get( "r_stencilbits",		"8",		CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH );
	r_depthbits			= Cvar_Get( "r_depthbits",			"0",		CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH );
	r_colorbits			= Cvar_Get( "r_colorbits",			"0",		CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH );
	r_ext_multisample	= Cvar_Get( "r_ext_multisample",	"0",		CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH );
	r_allowsoftwaregl	= Cvar_Get( "r_allowsoftwaregl",	"0",		CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH );
	r_gammamethod		= Cvar_Get( "r_gammamethod",		"2",		CVAR_ARCHIVE | CVAR_GLOBAL | CVAR_LATCH );
	Cvar_Get( "r_availableModes", "", CVAR_ROM );

	// Create the window and set up the context
	if(!GLimp_StartDriverAndSetMode( glConfig, windowDesc, r_mode->integer,
										(qboolean)!!r_fullscreen->integer, (qboolean)!!r_noborder->integer ))
	{
		if( r_mode->integer != R_MODE_FALLBACK )
		{
			Com_Printf( "Setting r_mode %d failed, falling back on r_mode %d\n", r_mode->integer, R_MODE_FALLBACK );

			if (!GLimp_StartDriverAndSetMode( glConfig, windowDesc, R_MODE_FALLBACK, qfalse, qfalse ))
			{
				// Nothing worked, give up
				Com_Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem" );
			}
		}
	}

	// This depends on SDL_INIT_VIDEO, hence having it here
	IN_Init( screen );

	// window_t is only really useful for Windows if the renderer wants to create a D3D context.
	window_t window = {};

	window.api = windowDesc->api;

#if defined(_WIN32)
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);

	if ( SDL_GetWindowWMInfo(screen, &info) )
	{
		switch(info.subsystem) {
			case SDL_SYSWM_WINDOWS:
				window.handle = info.info.win.window;
				break;

			default:
				break;
		}
	}
#endif

	return window;
}

void WIN_InitGammaMethod(glconfig_t *glConfig) {
	if (r_gammamethod->integer == GAMMA_POSTPROCESSING && !glConfig->deviceSupportsPostprocessingGamma) {
		Com_Printf("postprocessing gamma correction not supported, falling back to hardware gamma...\n");
		r_gammamethod->integer = GAMMA_HARDWARE;
	}

	if (r_gammamethod->integer == GAMMA_HARDWARE) {
		glConfig->deviceSupportsGamma = (qboolean)(SDL_SetWindowBrightness(screen, 1.0f) >= 0);
		if (!glConfig->deviceSupportsGamma) {
			Com_Printf("hardware gamma correction not supported, proceeding without gamma correction...\n");
			r_gammamethod->integer = GAMMA_NONE;
		}
	}
}

/*
===============
GLimp_Shutdown
===============
*/
void WIN_Shutdown( void )
{
	Cmd_RemoveCommand("modelist");
	Cmd_RemoveCommand("minimize");

	IN_Shutdown();

	SDL_QuitSubSystem( SDL_INIT_VIDEO );
	screen = NULL;
}

void GLimp_EnableLogging( qboolean enable )
{
}

void GLimp_LogComment( char *comment )
{
}

void WIN_SetGamma( glconfig_t *glConfig, byte red[256], byte green[256], byte blue[256] )
{
	Uint16 table[3][256];
	int i, j;

	if( r_gammamethod->integer != GAMMA_HARDWARE )
		return;

	for (i = 0; i < 256; i++)
	{
		table[0][i] = ( ( ( Uint16 ) red[i] ) << 8 ) | red[i];
		table[1][i] = ( ( ( Uint16 ) green[i] ) << 8 ) | green[i];
		table[2][i] = ( ( ( Uint16 ) blue[i] ) << 8 ) | blue[i];
	}

#if defined(_WIN32)
	// Win2K and newer put this odd restriction on gamma ramps...
	{
		OSVERSIONINFO	vinfo;

		vinfo.dwOSVersionInfoSize = sizeof( vinfo );
		GetVersionEx( &vinfo );
		if( vinfo.dwMajorVersion >= 5 && vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT )
		{
			Com_DPrintf( "performing gamma clamp.\n" );
			for( j = 0 ; j < 3 ; j++ )
			{
				for( i = 0 ; i < 128 ; i++ )
				{
                                        table[j][i] = MIN(table[j][i], (128 + i) << 8);
				}

                                table[j][127] = MIN(table[j][127], 254 << 8);
			}
		}
	}
#endif

	// enforce constantly increasing
	for (j = 0; j < 3; j++)
	{
		for (i = 1; i < 256; i++)
		{
			if (table[j][i] < table[j][i-1])
				table[j][i] = table[j][i-1];
		}
	}

	if ( SDL_SetWindowGammaRamp( screen, table[0], table[1], table[2] ) < 0 )
	{
		Com_DPrintf( "SDL_SetWindowGammaRamp() failed: %s\n", SDL_GetError() );
	}
}

void *WIN_GL_GetProcAddress( const char *proc )
{
	return SDL_GL_GetProcAddress( proc );
}

void WIN_SetTaskbarState(tbstate_t state, uint64_t current, uint64_t total) {
	SDL_SysWMinfo info;

	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(screen, &info))
		return;

#ifdef SDL_VIDEO_DRIVER_WINDOWS
	Sys_SetTaskbarState(info.info.win.window, state, current, total);
#endif
}
