
// tr_image.c
#include "tr_local.h"

#ifndef DEDICATED
#include "glext.h"
#endif

#include <map>
using namespace std;


/*
 * Include file for users of JPEG library.
 * You will need to have included system headers that define at least
 * the typedefs FILE and size_t before you can include jpeglib.h.
 * (stdio.h is sufficient on ANSI-conforming systems.)
 * You may also wish to include "jerror.h".
 */

#ifndef DEDICATED
#include <jpeglib.h>
#include <png.h>

static void LoadTGA( const char *name, byte **pic, int *width, int *height, pixelFormat_t *format, qboolean skipJKA );
static void LoadJPG( const char *name, byte **pic, int *width, int *height, pixelFormat_t *format, qboolean skipJKA );

static byte			 s_intensitytable[256];
static unsigned char s_gammatable[256];

int		gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
int		gl_filter_max = GL_LINEAR;

//#define FILE_HASH_SIZE		1024	// actually the shader code still needs this (from another module, great),
//static	image_t*		hashTable[FILE_HASH_SIZE];

/*
** R_GammaCorrect
*/
void R_GammaCorrect( byte *buffer, int bufSize ) {
	int i;

	for ( i = 0; i < bufSize; i++ ) {
		buffer[i] = s_gammatable[buffer[i]];
	}
}

static const textureMode_t modes[] = {
	{"GL_NEAREST", GL_NEAREST, GL_NEAREST},
	{"GL_LINEAR", GL_LINEAR, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR}
};

// makeup a nice clean, consistant name to query for and file under, for map<> usage...
//
static char *GenerateImageMappingName( const char *name )
{
	static char sName[MAX_QPATH];
	int		i=0;
	char	letter;

	while (name[i] != '\0' && i<MAX_QPATH-1)
	{
		letter = tolower(name[i]);
		if (letter =='.') break;				// don't include extension
		if (letter =='\\') letter = '/';		// damn path names
		sName[i++] = letter;
	}
	sName[i]=0;

	return &sName[0];
}





/*
===============
GL_TextureMode
===============
*/
void GL_TextureMode( const char *string ) {
	image_t			*glt;
	const textureMode_t	*mode;

	mode = GetTextureMode(string);

	if ( mode == NULL ) {
		ri.Printf (PRINT_ALL, "bad filter name\n");
		for ( size_t i = 0 ; i < ARRAY_LEN(modes) ; i++ ) {
			ri.Printf( PRINT_ALL, "%s\n",modes[i].name);
		}
		return;
	}

	gl_filter_min = mode->minimize;
	gl_filter_max = mode->maximize;

	// change all the existing mipmap texture objects
	   				 R_Images_StartIteration();
	while ( (glt   = R_Images_GetNextIteration()) != NULL)
	{
		if ( glt->upload.textureMode && glt->upload.noMipMaps ) {
			continue;
		}

		GL_Bind (glt);

		if ( !glt->upload.textureMode ) {
			qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
			qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
		}

		if ( !glt->upload.noMipMaps ) {
			if(glConfig.textureFilterAnisotropicMax >= 2.0f) {
				float aniso = r_ext_texture_filter_anisotropic->value;
				aniso = Com_Clamp(1.0f, glConfig.textureFilterAnisotropicMax, aniso);
				qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
			}
		}
	}
}

static float R_BytesPerTex (int format)
{
	switch ( format ) {
	case 1:
		//"I    "
		return 1;
		break;
	case 2:
		//"IA   "
		return 2;
		break;
	case 3:
	case GL_RGB:
		//"RGB  "
		return glConfig.colorBits/8.0f;
		break;
	case 4:
	case GL_RGBA:
		//"RGBA "
		return glConfig.colorBits/8.0f;
		break;

	case GL_RGBA4:
		//"RGBA4"
		return 2;
		break;
	case GL_RGB5:
		//"RGB5 "
		return 2;
		break;

	case GL_RGBA8:
		//"RGBA8"
		return 4;
		break;
	case GL_RGB8:
		//"RGB8"
		return 4;
		break;

	case GL_RGB4_S3TC:
		//"S3TC "
		return 0.33333f;
		break;
	case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
		//"DXT1 "
		return 0.33333f;
		break;
	case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
		//"DXT5 "
		return 1;
		break;
	default:
		//"???? "
		return 4;
	}
}

/*
===============
R_SumOfUsedImages
===============
*/
float R_SumOfUsedImages( qboolean bUseFormat )
{
	int	total = 0;
	image_t *pImage;

					  R_Images_StartIteration();
	while ( (pImage = R_Images_GetNextIteration()) != NULL)
	{
		if ( pImage->frameUsed == tr.frameCount- 1 ) {//it has already been advanced for the next frame, so...
			if (bUseFormat)
			{
				float  bytePerTex = R_BytesPerTex (pImage->internalFormat);
				total += bytePerTex * (pImage->uploadWidth * pImage->uploadHeight);
			}
			else
			{
				total += pImage->uploadWidth * pImage->uploadHeight;
			}
		}
	}

	return total;
}

/*
===============
R_ImageList_f
===============
*/
void R_ImageList_f( void ) {
	int		i=0;
	image_t	*image;
	int		texels=0;
	float	texBytes = 0.0f;
	const char * const yesno[] = {"no ", "yes"};

	ri.Printf (PRINT_ALL, "\n      -w-- -h-- -mm- -TMU- -if-- wrap --name-------\n");

	int iNumImages = R_Images_StartIteration();
	while ( (image = R_Images_GetNextIteration()) != NULL)
	{
		texels   += image->uploadWidth*image->uploadHeight;
		texBytes += image->uploadWidth*image->uploadHeight * R_BytesPerTex (image->internalFormat);
		ri.Printf (PRINT_ALL,  "%4i: %4i %4i  %s   %d   ",
			i, image->uploadWidth, image->uploadHeight, yesno[!image->upload.noMipMaps], image->TMU );
		switch ( image->internalFormat ) {
		case 1:
			ri.Printf( PRINT_ALL, "I     " );
			break;
		case 2:
			ri.Printf( PRINT_ALL, "IA    " );
			break;
		case 3:
		case GL_RGB:
			ri.Printf( PRINT_ALL, "RGB   " );
			break;
		case 4:
		case GL_RGBA:
			ri.Printf( PRINT_ALL, "RGBA  " );
			break;
		case GL_RGBA8:
			ri.Printf( PRINT_ALL, "RGBA8 " );
			break;
		case GL_RGB8:
			ri.Printf( PRINT_ALL, "RGB8  " );
			break;
		case GL_RGB4_S3TC:
			ri.Printf( PRINT_ALL, "S3TC  " );
			break;
		case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
			ri.Printf( PRINT_ALL, "DXT1  " );
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
			ri.Printf( PRINT_ALL, "DXT5  " );
			break;
		case GL_RGBA4:
			ri.Printf( PRINT_ALL, "RGBA4 " );
			break;
		case GL_RGB5:
			ri.Printf( PRINT_ALL, "RGB5  " );
			break;
		default:
			ri.Printf( PRINT_ALL, "%-6d ", image->internalFormat );
		}

		switch ( image->wrapClampMode ) {
		case GL_REPEAT:
			ri.Printf( PRINT_ALL, "rept " );
			break;
		case GL_CLAMP:
			ri.Printf( PRINT_ALL, "clmp " );
			break;
		case GL_CLAMP_TO_EDGE:
			ri.Printf( PRINT_ALL, "clpE " );
			break;
		default:
			ri.Printf( PRINT_ALL, "%-4i ", image->wrapClampMode );
			break;
		}

		ri.Printf( PRINT_ALL, "%s\n", image->imgName );
		i++;
	}
	ri.Printf (PRINT_ALL, " ---------\n");
	ri.Printf (PRINT_ALL, "      -w-- -h-- -mm- -TMU- -if- wrap --name-------\n");
	ri.Printf (PRINT_ALL, " %i total texels (not including mipmaps)\n", texels );
	ri.Printf (PRINT_ALL, " %.2fMB total texture mem (not including mipmaps)\n", texBytes/1048576.0f );
	ri.Printf (PRINT_ALL, " %i total images\n\n", iNumImages );
}

//=======================================================================


template<int s>
static void R_LightScaleTexture (byte *in, int inwidth, int inheight )
{
	int		i, c;
	byte	*p;

	p = in;
	c = inwidth*inheight;

	if ( r_gammamethod->integer )
	{
		for (i=0 ; i<c ; i++, p+=s)
		{
			p[0] = s_intensitytable[p[0]];
			p[1] = s_intensitytable[p[1]];
			p[2] = s_intensitytable[p[2]];
		}
	}
	else
	{
		for (i=0 ; i<c ; i++, p+=s)
		{
			p[0] = s_gammatable[s_intensitytable[p[0]]];
			p[1] = s_gammatable[s_intensitytable[p[1]]];
			p[2] = s_gammatable[s_intensitytable[p[2]]];
		}
	}
}

static void R_LightScaleTextureGray (byte *in, int inwidth, int inheight )
{
	int		i, c;
	byte	*p;

	p = in;
	c = inwidth * inheight;

	if ( r_gammamethod->integer )
	{
		for (i = 0; i < c; i++)
		{
			p[i] = s_intensitytable[p[i]];
		}
	}
	else
	{
		for (i = 0; i < c; i++)
		{
			p[i] = s_gammatable[s_intensitytable[p[i]]];
		}
	}
}

/*
================
R_LightScaleTexture

Scale up the pixel values in a texture to increase the
lighting range
================
*/
static void R_LightScaleTexture (pixelFormat_t format, byte *data, int width, int height ) {
	switch (format) {
	case PXF_GRAY: R_LightScaleTextureGray( data, width, height ); break;
	case PXF_RGB:  R_LightScaleTexture<3>( data, width, height ); break;
	case PXF_BGR:  R_LightScaleTexture<3>( data, width, height ); break;
	case PXF_RGBA: R_LightScaleTexture<4>( data, width, height ); break;
	case PXF_BGRA: R_LightScaleTexture<4>( data, width, height ); break;
	}
}

/*
================
R_MipMapBox

Box filter
template parameter s is number of 8bit color samples per pixel (1, 3, 4)

Implementing all specializations only because GCC won't unwind the inner loop
================
*/
template<int s>
static void R_MipMapBox(byte *in, int width, int height) {
	int		i, j, k;
	byte	*out;
	int		row;

	row = width * s;
	out = in;
	width >>= 1;
	height >>= 1;

	if ( width == 0 || height == 0 ) {
		width += height;	// get largest
		for (i=0 ; i<width ; i++, out+=s, in+=2*s ) {
			for (k=0 ; k<s ; k++) {
				out[k] = ( in[k] + in[s+k] )>>1;
			}
		}
		return;
	}

	for (i=0 ; i<height ; i++, in+=row) {
		for (j=0 ; j<width ; j++, out+=s, in+=2*s) {
			for (k=0 ; k<s ; k++) {
				out[k] = (in[k] + in[s+k] + in[row+k] + in[row+s+k])>>2;
			}
		}
	}
}

template<>
void R_MipMapBox<1>(byte *in, int width, int height) {
	const int s = 1;
	int		i, j;
	byte	*out;
	int		row;

	row = width * s;
	out = in;
	width >>= 1;
	height >>= 1;

	if ( width == 0 || height == 0 ) {
		width += height;	// get largest
		for (i=0 ; i<width ; i++, out+=s, in+=2*s ) {
			out[0] = ( in[0] + in[s+0] )>>1;
		}
		return;
	}

	for (i=0 ; i<height ; i++, in+=row) {
		for (j=0 ; j<width ; j++, out+=s, in+=2*s) {
			out[0] = (in[0] + in[s+0] + in[row+0] + in[row+s+0])>>2;
		}
	}
}

template<>
void R_MipMapBox<3>(byte *in, int width, int height) {
	const int s = 3;
	int		i, j;
	byte	*out;
	int		row;

	row = width * s;
	out = in;
	width >>= 1;
	height >>= 1;

	if ( width == 0 || height == 0 ) {
		width += height;	// get largest
		for (i=0 ; i<width ; i++, out+=s, in+=2*s ) {
			out[0] = ( in[0] + in[s+0] )>>1;
			out[1] = ( in[1] + in[s+1] )>>1;
			out[2] = ( in[2] + in[s+2] )>>1;
		}
		return;
	}

	for (i=0 ; i<height ; i++, in+=row) {
		for (j=0 ; j<width ; j++, out+=s, in+=2*s) {
			out[0] = (in[0] + in[s+0] + in[row+0] + in[row+s+0])>>2;
			out[1] = (in[1] + in[s+1] + in[row+1] + in[row+s+1])>>2;
			out[2] = (in[2] + in[s+2] + in[row+2] + in[row+s+2])>>2;
		}
	}
}

template<>
void R_MipMapBox<4>(byte *in, int width, int height) {
	const int s = 4;
	int		i, j;
	byte	*out;
	int		row;

	row = width * s;
	out = in;
	width >>= 1;
	height >>= 1;

	if ( width == 0 || height == 0 ) {
		width += height;	// get largest
		for (i=0 ; i<width ; i++, out+=s, in+=2*s ) {
			out[0] = ( in[0] + in[s+0] )>>1;
			out[1] = ( in[1] + in[s+1] )>>1;
			out[2] = ( in[2] + in[s+2] )>>1;
			out[3] = ( in[3] + in[s+3] )>>1;
		}
		return;
	}

	for (i=0 ; i<height ; i++, in+=row) {
		for (j=0 ; j<width ; j++, out+=s, in+=2*s) {
			out[0] = (in[0] + in[s+0] + in[row+0] + in[row+s+0])>>2;
			out[1] = (in[1] + in[s+1] + in[row+1] + in[row+s+1])>>2;
			out[2] = (in[2] + in[s+2] + in[row+2] + in[row+s+2])>>2;
			out[3] = (in[3] + in[s+3] + in[row+3] + in[row+s+3])>>2;
		}
	}
}

/*
================
R_MipMapBilinear

Bilinear filter on pixel data
template parameter s is number of 8bit color samples per pixel (1, 3, 4)
================
*/
template<int s>
static void R_MipMapBilinear( byte *in, int inWidth, int inHeight ) {
	int			i, j, k;
	byte		*outpix;
	int			inWidthMask, inHeightMask;
	int			total;
	int			outWidth, outHeight;
	byte		*temp;

	outWidth = inWidth >> 1;
	outHeight = inHeight >> 1;
	temp = (byte *)ri.Hunk_AllocateTempMemory( outWidth * outHeight * s );

	inWidthMask = inWidth - 1;
	inHeightMask = inHeight - 1;

	for ( i = 0 ; i < outHeight ; i++ ) {
		for ( j = 0 ; j < outWidth ; j++ ) {
			outpix = temp + s * (i * outWidth + j);
			for ( k = 0 ; k < s ; k++ ) {
				total =
					1 * in[s*(((i*2-1)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask)) + k] +
					2 * in[s*(((i*2-1)&inHeightMask)*inWidth + ((j*2+0)&inWidthMask)) + k] +
					2 * in[s*(((i*2-1)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask)) + k] +
					1 * in[s*(((i*2-1)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask)) + k] +

					2 * in[s*(((i*2+0)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask)) + k] +
					4 * in[s*(((i*2+0)&inHeightMask)*inWidth + ((j*2+0)&inWidthMask)) + k] +
					4 * in[s*(((i*2+0)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask)) + k] +
					2 * in[s*(((i*2+0)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask)) + k] +

					2 * in[s*(((i*2+1)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask)) + k] +
					4 * in[s*(((i*2+1)&inHeightMask)*inWidth + ((j*2+0)&inWidthMask)) + k] +
					4 * in[s*(((i*2+1)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask)) + k] +
					2 * in[s*(((i*2+1)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask)) + k] +

					1 * in[s*(((i*2+2)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask)) + k] +
					2 * in[s*(((i*2+2)&inHeightMask)*inWidth + ((j*2+0)&inWidthMask)) + k] +
					2 * in[s*(((i*2+2)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask)) + k] +
					1 * in[s*(((i*2+2)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask)) + k];
				outpix[k] = total / 36;
			}
		}
	}

	Com_Memcpy( in, temp, outWidth * outHeight * s );
	ri.Hunk_FreeTempMemory( temp );
}

/*
================
R_MipMap

Operates in place, quartering the size of the texture
================
*/
static void R_MipMap (byte *in, int width, int height, pixelFormat_t format) {
	if ( width == 1 && height == 1 ) {
		return;
	}

	if (r_simpleMipMaps->integer) {
		switch (format) {
		case PXF_GRAY: R_MipMapBox<1>(in, width, height); break;
		case PXF_RGB:  R_MipMapBox<3>(in, width, height); break;
		case PXF_BGR:  R_MipMapBox<3>(in, width, height); break;
		case PXF_RGBA: R_MipMapBox<4>(in, width, height); break;
		case PXF_BGRA: R_MipMapBox<4>(in, width, height); break;
		}
	} else {
		switch (format) {
		case PXF_GRAY: R_MipMapBilinear<1>(in, width, height); break;
		case PXF_RGB:  R_MipMapBilinear<3>(in, width, height); break;
		case PXF_BGR:  R_MipMapBilinear<3>(in, width, height); break;
		case PXF_RGBA: R_MipMapBilinear<4>(in, width, height); break;
		case PXF_BGRA: R_MipMapBilinear<4>(in, width, height); break;
		}
	}
}


/*
==================
R_BlendOverTexture

Apply a color blend over a set of pixels
==================
*/
static void R_BlendOverTexture( byte *data, int pixelCount, const byte blend[4] ) {
	int		i;
	int		inverseAlpha;
	int		premult[3];

	inverseAlpha = 255 - blend[3];
	premult[0] = blend[0] * blend[3];
	premult[1] = blend[1] * blend[3];
	premult[2] = blend[2] * blend[3];

	for ( i = 0 ; i < pixelCount ; i++, data+=4 ) {
		data[0] = ( data[0] * inverseAlpha + premult[0] ) >> 9;
		data[1] = ( data[1] * inverseAlpha + premult[1] ) >> 9;
		data[2] = ( data[2] * inverseAlpha + premult[2] ) >> 9;
	}
}

static const byte mipBlendColors[16][4] = {
	{0,0,0,0},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
};

static void R_PixelFormatSwizzle440( const byte * __restrict in, byte * __restrict out, int pixelcount ) {
	memcpy(out, in, pixelcount * 4);
}

static void R_PixelFormatSwizzle441( const byte * __restrict in, byte * __restrict out, int pixelcount ) {
	for (int i = 0; i < pixelcount; i++) {
		out[0] = in[2];
		out[1] = in[1];
		out[2] = in[0];
		out[3] = in[3];

		in  += 4;
		out += 4;
	}
}

static void R_PixelFormatSwizzle340( const byte * __restrict in, byte * __restrict out, int pixelcount ) {
	for (int i = 0; i < pixelcount; i++) {
		out[0] = in[0];
		out[1] = in[1];
		out[2] = in[2];
		out[3] = 255;

		in  += 3;
		out += 4;
	}
}

static void R_PixelFormatSwizzle341( const byte * __restrict in, byte * __restrict out, int pixelcount ) {
	for (int i = 0; i < pixelcount; i++) {
		out[0] = in[2];
		out[1] = in[1];
		out[2] = in[0];
		out[3] = 255;

		in  += 3;
		out += 4;
	}
}

static void R_PixelFormatSwizzle140( const byte * __restrict in, byte * __restrict out, int pixelcount ) {
	for (int i = 0; i < pixelcount; i++) {
		out[0] = in[0];
		out[1] = in[0];
		out[2] = in[0];
		out[3] = 255;

		in  += 1;
		out += 4;
	}
}

/*
==================
R_CovnvertToRGBA

Convert data in given pixel format to PXF_RGBA
==================
*/
static void R_ConvertToRGBA( pixelFormat_t informat, const byte * __restrict in, byte * __restrict out, int pixelcount ) {
	switch(informat) {
	case PXF_RGBA: R_PixelFormatSwizzle440(in, out, pixelcount); break;
	case PXF_BGRA: R_PixelFormatSwizzle441(in, out, pixelcount); break;
	case PXF_RGB : R_PixelFormatSwizzle340(in, out, pixelcount); break;
	case PXF_BGR : R_PixelFormatSwizzle341(in, out, pixelcount); break;
	case PXF_GRAY: R_PixelFormatSwizzle140(in, out, pixelcount); break;
	}
}

static int R_PixelFormatSamples( pixelFormat_t format ) {
	switch (format) {
	case PXF_GRAY: return 1;
	case PXF_RGB : return 3;
	case PXF_BGR : return 3;
	case PXF_RGBA: return 4;
	case PXF_BGRA: return 4;
	}

	assert(0);
	return 0;
}

static GLenum R_GLPixelFormat( pixelFormat_t format ) {
	switch (format) {
	case PXF_GRAY: return GL_LUMINANCE;
	case PXF_RGB : return GL_RGB;
	case PXF_BGR : return GL_BGR;
	case PXF_RGBA: return GL_RGBA;
	case PXF_BGRA: return GL_BGRA;
	}

	assert(0);
	return 0;
}

static GLint R_GLInternalFormat( qboolean isLightmap, qboolean noTC, qboolean hasAlpha ) {
	if ( !hasAlpha )
	{
		int texturebits;

		// Allow different bit depth when we are a lightmap
		if ( isLightmap && r_texturebitslm->integer > 0 ) {
			texturebits = r_texturebitslm->integer;
		} else {
			texturebits = r_texturebits->integer;
		}

		if ( glConfig.textureCompression == TC_S3TC && !noTC )
		{
			return GL_RGB4_S3TC;
		}
		else if ( glConfig.textureCompression == TC_S3TC_DXT && !noTC )
		{	// Compress purely color - no alpha
			return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
		}
		else if ( texturebits == 16 )
		{
			return GL_RGB5;
		}
		else if ( texturebits == 32 )
		{
			return GL_RGB8;
		}
		else
		{
			return GL_RGB;
		}
	}
	else
	{
		if ( glConfig.textureCompression == TC_S3TC_DXT && !noTC)
		{	// Compress both alpha and color
			return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
		}
		else if ( r_texturebits->integer == 16 )
		{
			return GL_RGBA4;
		}
		else if ( r_texturebits->integer == 32 )
		{
			return GL_RGBA8;
		}
		else
		{
			return GL_RGBA;
		}
	}

	assert(0);
	return GL_RGBA;
}

class CStringComparator
{
public:
	bool operator()(const char *s1, const char *s2) const { return(strcmp(s1, s2) < 0); }
};

typedef map <const char *, image_t *, CStringComparator>	AllocatedImages_t;
													AllocatedImages_t AllocatedImages;
													AllocatedImages_t::iterator itAllocatedImages;
int giTextureBindNum = 1024;	// will be set to this anyway at runtime, but wtf?


// return = number of images in the list, for those interested
//
int R_Images_StartIteration(void)
{
	itAllocatedImages = AllocatedImages.begin();
	return (int)AllocatedImages.size();
}

image_t *R_Images_GetNextIteration(void)
{
	if (itAllocatedImages == AllocatedImages.end())
		return NULL;

	image_t *pImage = (*itAllocatedImages).second;
	++itAllocatedImages;
	return pImage;
}

// clean up anything to do with an image_t struct, but caller will have to clear the internal to an image_t struct ready for either struct free() or overwrite...
//
// (avoid using ri.xxxx stuff here in case running on dedicated)
//
static void R_Images_DeleteImageContents( image_t *pImage )
{
	assert(pImage);	// should never be called with NULL
	if (pImage)
	{
		qglDeleteTextures( 1, &pImage->texnum );

		Z_Free(pImage);
	}
}




/*
===============
Upload32

===============
*/
static void Upload32( byte * const *mipmaps, qboolean customMip, image_t *image, qboolean isLightmap, pixelFormat_t format )
{
	byte 		*data;
	int			samples;
	int			level;
	int			width = image->width;
	int			height = image->height;
	upload_t	*upload = &image->upload;
	GLenum		glFormat;
	qboolean	hasAlpha;

	data = mipmaps[0];
	level = 1;

	//
	// perform optional picmip operation
	//
	if ( !upload->noPicMip ) {
		while( level <= r_picmip->integer && width + height > 2) {
			if (customMip && level < MAX_MIP_LEVELS && mipmaps[level]) {
				data = mipmaps[level];
			} else {
				R_MipMap( data, width, height, format );
			}

			width = max(width >> 1, 1);
			height = max(height >> 1, 1);
			level++;
		}
	}

	//
	// clamp to the current upper OpenGL limit
	// scale both axis down equally so we don't have to
	// deal with a half mip resampling
	//
	while ( width > glConfig.maxTextureSize	|| height > glConfig.maxTextureSize ) {
		if (customMip && level < MAX_MIP_LEVELS && mipmaps[level]) {
			data = mipmaps[level];
		} else {
			R_MipMap( data, width, height, format );
		}
		width >>= 1;
		height >>= 1;
		level++;
	}

	samples = R_PixelFormatSamples( format );
	glFormat = R_GLPixelFormat( format );
	hasAlpha = (qboolean)(samples == 4);
	image->internalFormat = R_GLInternalFormat( isLightmap, upload->noTC, hasAlpha );
	image->uploadWidth = width;
	image->uploadHeight = height;

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	if ( !upload->noMipMaps )
	{
		if ( r_openglMipMaps->integer && !r_colorMipLevels->integer && !customMip && glConfig.glVersion >= QGL_VERSION_1_4 )
		{
			if ( !upload->noLightScale )
				R_LightScaleTexture( format, data, width, height );

			qglTexParameteri( GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE );
			qglTexImage2D( GL_TEXTURE_2D, 0, image->internalFormat, width, height, 0, glFormat, GL_UNSIGNED_BYTE, data );
		}
		else
		{
			int			miplevel = 0;
			qboolean	processData = qtrue;
			const byte	*uploadData;
			byte		*datargba = NULL;

			if ( r_colorMipLevels->integer ) {
				datargba = (byte *)Hunk_AllocateTempMemory(width * height * 4);
				glFormat = GL_RGBA;
			}

			while ( 1 ) {
				if ( processData && !upload->noLightScale )
					R_LightScaleTexture( format, data, width, height );

				if ( r_colorMipLevels->integer ) {
					R_ConvertToRGBA( format, data, datargba, width * height );
					R_BlendOverTexture( datargba, width * height, mipBlendColors[miplevel] );
					uploadData = datargba;
				} else {
					uploadData = data;
				}

				qglTexImage2D( GL_TEXTURE_2D, miplevel, image->internalFormat, width, height, 0, glFormat, GL_UNSIGNED_BYTE, uploadData );

				if ( width == 1 && height == 1 )
					break;

				if ( customMip && level < MAX_MIP_LEVELS && mipmaps[level] ) {
					data = mipmaps[level];
					processData = qtrue;
				} else {
					R_MipMap( data, width, height, format );
					processData = qfalse;
				}

				width = max(width >> 1, 1);
				height = max(height >> 1, 1);

				miplevel++;
				level++;
			}

			if ( datargba )
				Hunk_FreeTempMemory( datargba );
		}
	}
	else
	{
		qglTexImage2D (GL_TEXTURE_2D, 0, image->internalFormat, width, height, 0, glFormat, GL_UNSIGNED_BYTE, data );
	}

	if (upload->textureMode)
	{
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, upload->textureMode->minimize);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, upload->textureMode->maximize);
	}
	else
	{
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}

	if (!upload->noMipMaps) {
		if(glConfig.textureFilterAnisotropicMax >= 2.0f) {
			float aniso = r_ext_texture_filter_anisotropic->value;
			aniso = Com_Clamp(1.0f, glConfig.textureFilterAnisotropicMax, aniso);
			qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
		}
	}

	GL_CheckErrors();
}


static void GL_ResetBinds(void)
{
	memset( glState.currenttextures, 0, sizeof( glState.currenttextures ) );

	if ( qglActiveTextureARB )
	{
		GL_SelectTexture( 1 );
		qglBindTexture( GL_TEXTURE_2D, 0 );
		GL_SelectTexture( 0 );
		qglBindTexture( GL_TEXTURE_2D, 0 );
	}
	else
	{
		qglBindTexture( GL_TEXTURE_2D, 0 );
	}
}


// special function used in conjunction with "devmapbsp"...
//
// (avoid using ri.xxxx stuff here in case running on dedicated)
//
void R_Images_DeleteLightMaps(void)
{
	AllocatedImages_t::iterator itImage = AllocatedImages.begin();

	while ( itImage != AllocatedImages.end() )
	{
		image_t *pImage = (*itImage).second;

		if (pImage->imgName[0] == '*' && strstr(pImage->imgName,"lightmap"))	// loose check, but should be ok
		{
			R_Images_DeleteImageContents(pImage);
			itImage = AllocatedImages.erase(itImage);
			continue;
		}

		++itImage;
	}

	GL_ResetBinds();
}

// special function currently only called by Dissolve code...
//
void R_Images_DeleteImage(image_t *pImage)
{
	// Even though we supply the image handle, we need to get the corresponding iterator entry...
	//
	AllocatedImages_t::iterator itImage = AllocatedImages.find(pImage->imgName);
	if (itImage != AllocatedImages.end())
	{
		R_Images_DeleteImageContents(pImage);
		AllocatedImages.erase(itImage);
	}
	else
	{
		assert(0);
	}
}

// called only at app startup, vid_restart, app-exit
//
void R_Images_Clear(void)
{
	image_t *pImage;
	//	int iNumImages =
					  R_Images_StartIteration();
	while ( (pImage = R_Images_GetNextIteration()) != NULL)
	{
		R_Images_DeleteImageContents(pImage);
	}

	AllocatedImages.clear();

	giTextureBindNum = 1024;
}


void RE_RegisterImages_Info_f( void )
{
	image_t *pImage	= NULL;
	int iImage		= 0;
	int iTexels		= 0;

	int iNumImages	= R_Images_StartIteration();
	while ( (pImage	= R_Images_GetNextIteration()) != NULL)
	{
		ri.Printf( PRINT_ALL, "%d: (%4dx%4dy) \"%s\"",iImage, pImage->uploadWidth, pImage->uploadHeight, pImage->imgName);
		ri.Printf( PRINT_DEVELOPER, ", levused %d",pImage->iLastLevelUsedOn);
		ri.Printf( PRINT_ALL, "\n");

		iTexels += pImage->uploadWidth * pImage->uploadHeight;
		iImage++;
	}
	ri.Printf( PRINT_ALL, "%d Images. %d (%.2fMB) texels total, (not including mipmaps)\n",iNumImages, iTexels, (float)iTexels / 1024.0f / 1024.0f);
	ri.Printf( PRINT_DEVELOPER, "RE_RegisterMedia_GetLevel(): %d",RE_RegisterMedia_GetLevel());
}


// implement this if you need to, do a find for the caller. I don't need it though, so far.
//
//void		RE_RegisterImages_LevelLoadBegin(const char *psMapName);


// currently, this just goes through all the images and dumps any not referenced on this level...
//
qboolean RE_RegisterImages_LevelLoadEnd(void)
{
	ri.Printf( PRINT_DEVELOPER, "RE_RegisterImages_LevelLoadEnd():\n");

//	int iNumImages = AllocatedImages.size();	// more for curiosity, really.

	qboolean bEraseOccured = qfalse;
	AllocatedImages_t::iterator itImage = AllocatedImages.begin();

	while ( itImage != AllocatedImages.end() )
	{
		bEraseOccured = qfalse;

		image_t *pImage = (*itImage).second;

		// don't un-register system shaders (*fog, *dlight, *white, *default), but DO de-register lightmaps ("*<mapname>/lightmap%d")
		if (pImage->imgName[0] != '*' || strchr(pImage->imgName,'/'))
		{
			// image used on this level?
			//
			if ( pImage->iLastLevelUsedOn != RE_RegisterMedia_GetLevel() )
			{
				// nope, so dump it...
				//
				ri.Printf( PRINT_DEVELOPER, "Dumping image \"%s\"\n",pImage->imgName);

				R_Images_DeleteImageContents(pImage);
				itImage = AllocatedImages.erase(itImage);
				bEraseOccured = qtrue;
				continue;
			}
		}

		++itImage;
	}


	// this check can be deleted AFAIC, it seems to be just a quake thing...
	//
//	iNumImages = R_Images_StartIteration();
//	if (iNumImages > MAX_DRAWIMAGES)
//	{
//		ri.Printf( PRINT_WARNING, "Level uses %d images, old limit was MAX_DRAWIMAGES (%d)\n", iNumImages, MAX_DRAWIMAGES);
//	}

	ri.Printf( PRINT_DEVELOPER, "RE_RegisterImages_LevelLoadEnd(): Ok\n");

	GL_ResetBinds();

	return bEraseOccured;
}



// returns image_t struct if we already have this, else NULL. No disk-open performed
//	(important for creating default images).
//
// This is called by both R_FindImageFile and anything that creates default images...
//
static image_t *R_FindImageFile_NoLoad(const char *name, const upload_t *upload, int glWrapClampMode )
{
	if (!name) {
		return NULL;
	}

	char *pName = GenerateImageMappingName(name);

	//
	// see if the image is already loaded
	//
	AllocatedImages_t::iterator itAllocatedImage = AllocatedImages.find(pName);
	if (itAllocatedImage != AllocatedImages.end())
	{
		image_t *pImage = (*itAllocatedImage).second;

		// the white image can be used with any set of parms, but other mismatches are errors...
		//
		if ( strcmp( pName, "*white" ) ) {
			if ( pImage->upload.noMipMaps != upload->noMipMaps ) {
				ri.Printf( PRINT_WARNING, "WARNING: reused image %s with mixed mipmap parm\n", pName );
			}
			if ( pImage->upload.noPicMip != upload->noPicMip ) {
				ri.Printf( PRINT_WARNING, "WARNING: reused image %s with mixed allowPicmip parm\n", pName );
			}
			if ( pImage->upload.noLightScale != upload->noLightScale ) {
				ri.Printf( PRINT_WARNING, "WARNING: reused image %s with mixed lightScale parm\n", pName );
			}
			if ( pImage->upload.noTC != upload->noTC ) {
				ri.Printf( PRINT_WARNING, "WARNING: reused image %s with mixed TC parm\n", pName );
			}
			if ( pImage->upload.textureMode != upload->textureMode ) {
				ri.Printf( PRINT_WARNING, "WARNING: reused image %s with mixed textureMode parm\n", pName );
			}
			if ( pImage->wrapClampMode != glWrapClampMode ) {
				ri.Printf( PRINT_WARNING, "WARNING: reused image %s with mixed glWrapClampMode parm\n", pName );
			}
		}

		pImage->iLastLevelUsedOn = RE_RegisterMedia_GetLevel();

		return pImage;
	}

	return NULL;
}



/*
================
R_CreateImage

This is the only way any image_t are created
================
*/
image_t *R_CreateImage( const char *name, byte *data, int width, int height,
	qboolean mipmap, qboolean allowPicmip, qboolean allowTC, int glWrapClampMode, pixelFormat_t format ) {
	qboolean	customMip = qfalse;
	byte		**mipmaps = &data;
	upload_t	upload = {
		(qboolean)!mipmap,
		(qboolean)!allowPicmip,
		mipmap,
		(qboolean)!allowTC,
		(mipmap ? NULL : GetTextureMode("GL_LINEAR"))
	};

	return R_CreateImageNew( name, mipmaps, customMip, width, height, &upload, glWrapClampMode, format );
}

image_t *R_CreateImageNew( const char *name, byte * const *mipmaps, qboolean customMip, int width, int height, const upload_t *upload, int glWrapClampMode, pixelFormat_t format ) {
	image_t		*image;
	qboolean	isLightmap = qfalse;

	if (strlen(name) >= MAX_QPATH ) {
		ri.Error (ERR_DROP, "R_CreateImage: \"%s\" is too long", name);
	}

	if(glConfig.clampToEdgeAvailable && glWrapClampMode == GL_CLAMP) {
		glWrapClampMode = GL_CLAMP_TO_EDGE;
	}

	if (name[0] == '*')
	{
		const char *psLightMapNameSearchPos = strrchr(name,'/');
		if (  psLightMapNameSearchPos && !strncmp( psLightMapNameSearchPos+1, "lightmap", 8 ) ) {
			isLightmap = qtrue;
		}
	}

	if ( (width&(width-1)) || (height&(height-1)) )
	{
		ri.Error( ERR_FATAL, "R_CreateImage: %s dimensions (%i x %i) not power of 2!",name,width,height);
	}

	image = R_FindImageFile_NoLoad(name, upload, glWrapClampMode );
	if (image) {
		return image;
	}

	image = (image_t*) ri.Malloc( sizeof( image_t ), TAG_IMAGE_T, qtrue );
//	memset(image,0,sizeof(*image));	// qtrue above does this

	image->texnum = 1024 + giTextureBindNum++;	// ++ is of course staggeringly important...

	// record which map it was used on...
	//
	image->iLastLevelUsedOn = RE_RegisterMedia_GetLevel();

	image->upload = *upload;

	Q_strncpyz(image->imgName, name, sizeof(image->imgName));

	image->width = width;
	image->height = height;
	image->wrapClampMode = glWrapClampMode;

	// lightmaps are always allocated on TMU 1
	if ( qglActiveTextureARB && isLightmap ) {
		image->TMU = 1;
	} else {
		image->TMU = 0;
	}

	if ( qglActiveTextureARB ) {
		GL_SelectTexture( image->TMU );
	}

	GL_Bind(image);

	Upload32( mipmaps, customMip, image, isLightmap, format );

	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glWrapClampMode );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glWrapClampMode );

	qglBindTexture( GL_TEXTURE_2D, 0 );	//jfm: i don't know why this is here, but it breaks lightmaps when there's only 1
	glState.currenttextures[glState.currenttmu] = 0;	//mark it not bound

	if ( image->TMU == 1 ) {
		GL_SelectTexture( 0 );
	}

    const char *psNewName = GenerateImageMappingName(name);
	Q_strncpyz(image->imgName, psNewName, sizeof(image->imgName));
	AllocatedImages[ image->imgName ] = image;

	return image;
}
#endif // !DEDICATED

/*
=================
GetTextureMode
=================
*/
const textureMode_t *GetTextureMode( const char *name )
{
#ifndef DEDICATED
	for (size_t i = 0; i < ARRAY_LEN(modes); i++) {
		if (!Q_stricmp(modes[i].name, name)) {
			return &modes[i];
		}
	}
#endif

	return NULL;
}

/*
=========================================================

TARGA LOADING

=========================================================
*/
/*
Ghoul2 Insert Start
*/

bool LoadTGAPalletteImage ( const char *name, byte **pic, int *width, int *height)
{
	int		columns, rows, numPixels;
	byte	*buf_p;
	byte	*buffer;
	TargaHeader	targa_header;
	byte	*dataStart;

	*pic = NULL;

	//
	// load the file
	//
	ri.FS_ReadFile ( name, (void **)&buffer);
	if (!buffer) {
		return false;
	}

	buf_p = buffer;

	targa_header.id_length = *buf_p++;
	targa_header.colormap_type = *buf_p++;
	targa_header.image_type = *buf_p++;

	targa_header.colormap_index = LittleShort ( *(short *)buf_p );
	buf_p += 2;
	targa_header.colormap_length = LittleShort ( *(short *)buf_p );
	buf_p += 2;
	targa_header.colormap_size = *buf_p++;
	targa_header.x_origin = LittleShort ( *(short *)buf_p );
	buf_p += 2;
	targa_header.y_origin = LittleShort ( *(short *)buf_p );
	buf_p += 2;
	targa_header.width = LittleShort ( *(short *)buf_p );
	buf_p += 2;
	targa_header.height = LittleShort ( *(short *)buf_p );
	buf_p += 2;
	targa_header.pixel_size = *buf_p++;
	targa_header.attributes = *buf_p++;

	if (targa_header.image_type!=1 )
	{
		ri.Error (ERR_DROP, "LoadTGAPalletteImage: Only type 1 (uncompressed pallettised) TGA images supported");
	}

	if ( targa_header.colormap_type == 0 )
	{
		ri.Error( ERR_DROP, "LoadTGAPalletteImage: colormaps ONLY supported" );
	}

	columns = targa_header.width;
	rows = targa_header.height;
	numPixels = columns * rows;

	if (width)
		*width = columns;
	if (height)
		*height = rows;

	*pic = (unsigned char *) ri.Malloc (numPixels, TAG_TEMP_WORKSPACE, qfalse );
	if (targa_header.id_length != 0)
	{
		buf_p += targa_header.id_length;  // skip TARGA image comment
	}
	dataStart = buf_p + (targa_header.colormap_length * (targa_header.colormap_size / 4));
	memcpy(*pic, dataStart, numPixels);
	ri.FS_FreeFile (buffer);

	return true;
}

#ifndef DEDICATED
/*
Ghoul2 Insert End
*/
// My TGA loader...
//
//---------------------------------------------------
#pragma pack(push,1)
typedef struct
{
	byte	byIDFieldLength;	// must be 0
	byte	byColourmapType;	// 0 = truecolour, 1 = paletted, else bad
	byte	byImageType;		// 1 = colour mapped (palette), uncompressed, 2 = truecolour, uncompressed, else bad
	word	w1stColourMapEntry;	// must be 0
	word	wColourMapLength;	// 256 for 8-bit palettes, else 0 for true-colour
	byte	byColourMapEntrySize; // 24 for 8-bit palettes, else 0 for true-colour
	word	wImageXOrigin;		// ignored
	word	wImageYOrigin;		// ignored
	word	wImageWidth;		// in pixels
	word	wImageHeight;		// in pixels
	byte	byImagePlanes;		// bits per pixel	(8 for paletted, else 24 for true-colour)
	byte	byScanLineOrder;	// Image descriptor bytes
								// bits 0-3 = # attr bits (alpha chan)
								// bits 4-5 = pixel order/dir
								// bits 6-7 scan line interleave (00b=none,01b=2way interleave,10b=4way)
} TGAHeader_t;
#pragma pack(pop)


/*
=================
ReadTGAData

Templates for reading TGA data
samples - 1, 3 or 4 for grayscale, BGR, BGRA
xSwap - 0 for left-right, 1 for right-left TGA pixel order
ySwap - 0 for top-bottom, 1 for bottom-top TGA pixel order
=================
*/
// extra fast template for left-right top-bottom (second most common in base assets)
template<int samples>
static void ReadTGAData_LRTB(const byte * __restrict in, byte * __restrict out, int width, int height) {
	memcpy(out, in, width * height * samples);
}

// extra fast template for left-right bottom-top (most common in base assets)
template<int samples>
static void ReadTGAData_LRBT(const byte * __restrict in, byte * __restrict out, int width, int height) {
	const int rowlen = width * samples;
	const byte *pIn = in + rowlen * (height - 1);
	byte *pOut = out;

	for (int y = 0; y < height; y++) {
		memcpy(pOut, pIn, rowlen);
		pOut += rowlen;
		pIn  -= rowlen;
	}
}

// universal templates
template<int xSwap, int ySwap>
static void ReadTGAData_Gray(const byte * __restrict in, byte * __restrict out, int width, int height) {
	const int samples = 1;
	const int rowlen = width * samples;
	const int xStep = samples * (1 - 2 * xSwap);
	const int yStep = (xSwap - ySwap) * 2 * rowlen;
    const byte *pIn = in
		+ xSwap * rowlen - samples
		+ ySwap * rowlen * (height - 1);
	byte *pOut = out;

    for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			pOut[0] = pIn[0];
			pOut += samples;
			pIn += xStep;
		}
		pIn += yStep;
	}
}

template<int xSwap, int ySwap>
static void ReadTGAData_BGR(const byte * __restrict in, byte * __restrict out, int width, int height) {
	const int samples = 3;
	const int rowlen = width * samples;
	const int xStep = samples * (1 - 2 * xSwap);
	const int yStep = (xSwap - ySwap) * 2 * rowlen;
    const byte *pIn = in
		+ xSwap * rowlen - samples
		+ ySwap * rowlen * (height - 1);
	byte *pOut = out;

    for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			pOut[0] = pIn[0];
			pOut[1] = pIn[1];
			pOut[2] = pIn[2];
			pOut += samples;
			pIn += xStep;
		}
		pIn += yStep;
	}
}

template<int xSwap, int ySwap>
static void ReadTGAData_BGRA(const byte * __restrict in, byte * __restrict out, int width, int height) {
	const int samples = 4;
	const int rowlen = width * samples;
	const int xStep = samples * (1 - 2 * xSwap);
	const int yStep = (xSwap - ySwap) * 2 * rowlen;
    const byte *pIn = in
		+ xSwap * rowlen - samples
		+ ySwap * rowlen * (height - 1);
	byte *pOut = out;

    for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			pOut[0] = pIn[0];
			pOut[1] = pIn[1];
			pOut[2] = pIn[2];
			pOut[3] = pIn[3];
			pOut += samples;
			pIn += xStep;
		}
		pIn += yStep;
	}
}

// *pic == pic, else NULL for failed.
//
//  returns false if found but had a format error, else true for either OK or not-found (there's a reason for this)
//

void LoadTGA ( const char *name, byte **pic, int *width, int *height, pixelFormat_t *format, qboolean skipJKA)
{
	char sErrorString[1024];
	bool bFormatErrors = false;

	// these don't need to be declared or initialised until later, but the compiler whines that 'goto' skips them.
	//
	byte *pRGBA = NULL;
	byte *pOut	= NULL;
	byte *pIn	= NULL;


	*pic = NULL;

#define TGA_FORMAT_ERROR(blah) {sprintf(sErrorString,blah); bFormatErrors = true; goto TGADone;}
//#define TGA_FORMAT_ERROR(blah) ri.Error( ERR_DROP, blah );

	//
	// load the file
	//
	byte *pTempLoadedBuffer = 0;
	if ( skipJKA ) ri.FS_ReadFileSkipJKA( name, (void**)&pTempLoadedBuffer );
	else           ri.FS_ReadFile ( name, (void **)&pTempLoadedBuffer);
	if (!pTempLoadedBuffer) {
		return;
	}

	TGAHeader_t *pHeader = (TGAHeader_t *) pTempLoadedBuffer;

	if (pHeader->byColourmapType!=0)
	{
		TGA_FORMAT_ERROR("LoadTGA: colourmaps not supported\n" );
	}

	if (pHeader->byImageType != 2 && pHeader->byImageType != 3 && pHeader->byImageType != 10)
	{
		TGA_FORMAT_ERROR("LoadTGA: Only type 2 (RGB), 3 (gray), and 10 (RLE-RGB) images supported\n");
	}

	if (pHeader->w1stColourMapEntry != 0)
	{
		TGA_FORMAT_ERROR("LoadTGA: colourmaps not supported\n" );
	}

	if (pHeader->wColourMapLength !=0 && pHeader->wColourMapLength != 256)
	{
		TGA_FORMAT_ERROR("LoadTGA: ColourMapLength must be either 0 or 256\n" );
	}

	if (pHeader->byColourMapEntrySize != 0 && pHeader->byColourMapEntrySize != 24)
	{
		TGA_FORMAT_ERROR("LoadTGA: ColourMapEntrySize must be either 0 or 24\n" );
	}

	if (pHeader->byImagePlanes != 24 && pHeader->byImagePlanes != 32 && pHeader->byImagePlanes != 8)
	{
		TGA_FORMAT_ERROR("LoadTGA: Only 8, 24 and 32 bit TGA images supported\n");
	}

	if ((pHeader->byScanLineOrder&0x30)!=0x00 &&
		(pHeader->byScanLineOrder&0x30)!=0x10 &&
		(pHeader->byScanLineOrder&0x30)!=0x20 &&
		(pHeader->byScanLineOrder&0x30)!=0x30
		)
	{
		TGA_FORMAT_ERROR("LoadTGA: ScanLineOrder must be either 0x00,0x10,0x20, or 0x30\n");
	}



	// these last checks are so i can use ID's RLE-code. I don't dare fiddle with it or it'll probably break...
	//
	if ( pHeader->byImageType == 10)
	{
		if ((pHeader->byScanLineOrder & 0x30) != 0x00)
		{
			TGA_FORMAT_ERROR("LoadTGA: RLE-RGB Images (type 10) must be in bottom-to-top format\n");
		}
		if (pHeader->byImagePlanes != 24 && pHeader->byImagePlanes != 32)	// probably won't happen, but avoids compressed greyscales?
		{
			TGA_FORMAT_ERROR("LoadTGA: RLE-RGB Images (type 10) must be 24 or 32 bit\n");
		}
	}

	// now read the actual bitmap in...
	//
	// Image descriptor bytes
	// bits 0-3 = # attr bits (alpha chan)
	// bits 4-5 = pixel order/dir
	// bits 6-7 scan line interleave (00b=none,01b=2way interleave,10b=4way)
	//

	// feed back the results...
	//
	if (width)
		*width = pHeader->wImageWidth;
	if (height)
		*height = pHeader->wImageHeight;

	int samples;

	switch (pHeader->byImagePlanes) {
	case 8:
		samples = 1;
		*format = PXF_GRAY;
		break;
	case 24:
		samples = 3;
		*format = PXF_BGR;
		break;
	case 32:
		samples = 4;
		*format = PXF_BGRA;
		break;
	}

	pRGBA	= (byte *) ri.Malloc (pHeader->wImageWidth * pHeader->wImageHeight * samples, TAG_TEMP_WORKSPACE, qfalse);
	*pic	= pRGBA;
	pOut	= pRGBA;
	pIn		= pTempLoadedBuffer + sizeof(*pHeader);

	// I don't know if this ID-thing here is right, since comments that I've seen are at the end of the file,
	//	with a zero in this field. However, may as well...
	//
	if (pHeader->byIDFieldLength != 0)
		pIn += pHeader->byIDFieldLength;	// skip TARGA image comment

	if ( pHeader->byImageType == 2 || pHeader->byImageType == 3 )	// RGB or greyscale
	{
		int type = ((pHeader->byScanLineOrder & 0x30) << 8) | pHeader->byImagePlanes;
		int w = pHeader->wImageWidth;
		int h = pHeader->wImageHeight;

		// type first component (byScanLineOrder & 0x30):
		// 0x00 - left-right bottom-top
		// 0x10 - right-left bottom-top
		// 0x20 - left-right top-bottom
		// 0x30 - right-left top-bottom

		// type second component (byImagePlanes):
		// 0x08 -  8bit (gray)
		// 0x18 - 24bit (BGR)
		// 0x20 - 32bit (BGRA)

		switch (type) {
		case 0x0008: ReadTGAData_LRBT<1>   (pIn, pOut, w, h); break;
		case 0x0018: ReadTGAData_LRBT<3>   (pIn, pOut, w, h); break;
		case 0x0020: ReadTGAData_LRBT<4>   (pIn, pOut, w, h); break;
		case 0x1008: ReadTGAData_Gray<1, 1>(pIn, pOut, w, h); break;
		case 0x1018: ReadTGAData_BGR <1, 1>(pIn, pOut, w, h); break;
		case 0x1020: ReadTGAData_BGRA<1, 1>(pIn, pOut, w, h); break;
		case 0x2008: ReadTGAData_LRTB<1>   (pIn, pOut, w, h); break;
		case 0x2018: ReadTGAData_LRTB<3>   (pIn, pOut, w, h); break;
		case 0x2020: ReadTGAData_LRTB<4>   (pIn, pOut, w, h); break;
		case 0x3008: ReadTGAData_Gray<1, 0>(pIn, pOut, w, h); break;
		case 0x3018: ReadTGAData_BGR <1, 0>(pIn, pOut, w, h); break;
		case 0x3020: ReadTGAData_BGRA<1, 0>(pIn, pOut, w, h); break;
		default:
			assert(0);	// if we ever hit this, someone deleted a header check higher up
			TGA_FORMAT_ERROR("LoadTGA: Image can only have 8, 24 or 32 planes for RGB/greyscale\n");
			break;
		}
	}
	else
	if (pHeader->byImageType == 10)   // RLE-RGB
	{
		// I've no idea if this stuff works, I normally reject RLE targas, but this is from ID's code
		//	so maybe I should try and support it...
		//
		byte packetHeader, packetSize, j;

		for (int y = pHeader->wImageHeight-1; y >= 0; y--)
		{
			pOut = pRGBA + y * pHeader->wImageWidth * samples;
			for (int x=0; x<pHeader->wImageWidth;)
			{
				byte red, green, blue, alpha;
				packetHeader = *pIn++;
				packetSize   = 1 + (packetHeader & 0x7f);
				if (packetHeader & 0x80)		 // run-length packet
				{
					switch (pHeader->byImagePlanes)
					{
						case 24:

							blue	= *pIn++;
							green	= *pIn++;
							red		= *pIn++;
							alpha	= 255;
							break;

						case 32:

							blue	= *pIn++;
							green	= *pIn++;
							red		= *pIn++;
							alpha	= *pIn++;
							break;

						default:
							assert(0);	// if we ever hit this, someone deleted a header check higher up
							TGA_FORMAT_ERROR("LoadTGA: RLE-RGB can only have 24 or 32 planes\n");
							break;
					}

					for (j=0; j<packetSize; j++)
					{
						*pOut++	= blue;
						*pOut++	= green;
						*pOut++	= red;
						if (samples == 4)
							*pOut++	= alpha;
						x++;
						if (x == pHeader->wImageWidth)  // run spans across rows
						{
							x = 0;
							if (y > 0)
								y--;
							else
								goto breakOut;
							pOut = pRGBA + y * pHeader->wImageWidth * samples;
						}
					}
				}
				else
				{	// non run-length packet

					for (j=0; j<packetSize; j++)
					{
						switch (pHeader->byImagePlanes)
						{
							case 24:

								blue	= *pIn++;
								green	= *pIn++;
								red		= *pIn++;
								*pOut++ = blue;
								*pOut++ = green;
								*pOut++ = red;
								break;

							case 32:
								blue	= *pIn++;
								green	= *pIn++;
								red		= *pIn++;
								alpha	= *pIn++;
								*pOut++ = blue;
								*pOut++ = green;
								*pOut++ = red;
								*pOut++ = alpha;
								break;

							default:
								assert(0);	// if we ever hit this, someone deleted a header check higher up
								TGA_FORMAT_ERROR("LoadTGA: RLE-RGB can only have 24 or 32 planes\n");
								break;
						}
						x++;
						if (x == pHeader->wImageWidth)  // pixel packet run spans across rows
						{
							x = 0;
							if (y > 0)
								y--;
							else
								goto breakOut;
							pOut = pRGBA + y * pHeader->wImageWidth * samples;
						}
					}
				}
			}
			breakOut:;
		}
	}

TGADone:

	ri.FS_FreeFile (pTempLoadedBuffer);

	if (bFormatErrors)
	{
		ri.Error( ERR_DROP, "%s( File: \"%s\" )",sErrorString,name);
	}
}

struct jpegErrorManager
{
    struct jpeg_error_mgr err;
    jmp_buf jmp_buffer;
};

char jpegLastErrorBuffer[JMSG_LENGTH_MAX];
void R_JPGErrorExit( j_common_ptr cinfo )
{
    jpegErrorManager *ownerr = (jpegErrorManager*)cinfo->err;

    /* Create the message */
    (*cinfo->err->format_message) (cinfo, jpegLastErrorBuffer);

    /* Jump to the jmp point */
    longjmp(ownerr->jmp_buffer, 1);
}

static void R_JPGOutputMessage( j_common_ptr cinfo )
{
	char buffer[JMSG_LENGTH_MAX];

	/* Create the message */
	(*cinfo->err->format_message) (cinfo, buffer);
	Com_Printf("%s\n", buffer);
}

void LoadJPG( const char *filename, unsigned char **pic, int *width, int *height, pixelFormat_t *format, qboolean skipJKA ) {
	/* This struct contains the JPEG decompression parameters and pointers to
	* working space (which is allocated as needed by the JPEG library).
	*/
	struct jpeg_decompress_struct cinfo = { NULL };
	/* We use our private extension JPEG error handler.
	* Note that this struct must live as long as the main JPEG parameter
	* struct, to avoid dangling-pointer problems.
	*/
	/* This struct represents a JPEG error handler.  It is declared separately
	* because applications often want to supply a specialized error handler
	* (see the second half of this file for an example).  But here we just
	* take the easy way out and use the standard error handler, which will
	* print a message on stderr and call exit() if compression fails.
	* Note that this struct must live as long as the main JPEG parameter
	* struct, to avoid dangling-pointer problems.
	*/
	jpegErrorManager jerr;
	/* More stuff */
	JSAMPARRAY buffer;		/* Output row buffer */
	unsigned int row_stride;  /* physical row width in output buffer */
	unsigned int pixelcount, memcount;
	byte *out;
	union {
		byte *b;
		void *v;
	} fbuffer;
	byte  *buf;
	/* In this example we want to open the input file before doing anything else,
	* so that the setjmp() error recovery below can assume the file is open.
	* VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
	* requires it in order to read binary files.
	*/

	int len;
	if ( skipJKA ) len = ri.FS_ReadFileSkipJKA ( filename, &fbuffer.v );
	else           len = ri.FS_ReadFile ( filename, &fbuffer.v);
	if (!fbuffer.b || len < 0) {
		return;
	}

	/* Step 1: allocate and initialize JPEG decompression object */

	/* We have to set up the error handler first, in case the initialization
	* step fails.  (Unlikely, but it could happen if you are out of memory.)
	* This routine fills in the contents of struct jerr, and returns jerr's
	* address which we place into the link field in cinfo.
	*/

	cinfo.err = jpeg_std_error(&jerr.err);
	jerr.err.output_message = R_JPGOutputMessage;
	jerr.err.error_exit = R_JPGErrorExit;

	if ( setjmp(jerr.jmp_buffer) )
	{
		Com_Printf("LoadJPG: failed to load jpeg \"%s\", because of error \"%s\".\n", filename, jpegLastErrorBuffer);

		// Free the memory to make sure we don't leak memory
		ri.FS_FreeFile (fbuffer.v);
		jpeg_destroy_decompress(&cinfo);
		return;
	}

	/* Now we can initialize the JPEG decompression object. */
	jpeg_create_decompress(&cinfo);

	/* Step 2: specify data source (eg, a file) */

	jpeg_mem_src(&cinfo, fbuffer.b, len);

	/* Step 3: read file parameters with jpeg_read_header() */

	(void) jpeg_read_header(&cinfo, TRUE);
	/* We can ignore the return value from jpeg_read_header since
	*   (a) suspension is not possible with the stdio data source, and
	*   (b) we passed TRUE to reject a tables-only JPEG file as an error.
	* See libjpeg.doc for more info.
	*/

	/* Step 4: set parameters for decompression */


	/* Make sure it always converts images to RGB color space. This will
	* automatically convert 8-bit greyscale images to RGB as well.	*/
	cinfo.out_color_space = JCS_RGB;
	*format = PXF_RGB;

	/* Step 5: Start decompressor */

	(void) jpeg_start_decompress(&cinfo);
	/* We can ignore the return value since suspension is not possible
	* with the stdio data source.
	*/

	/* We may need to do some setup of our own at this point before reading
	* the data.  After jpeg_start_decompress() we have the correct scaled
	* output image dimensions available, as well as the output colormap
	* if we asked for color quantization.
	* In this example, we need to make an output work buffer of the right size.
	*/
	/* JSAMPLEs per row in output buffer */
	pixelcount = cinfo.output_width * cinfo.output_height;

	if(!cinfo.output_width || !cinfo.output_height
		|| ((pixelcount * 4) / cinfo.output_width) / 4 != cinfo.output_height
		|| pixelcount > 0x1FFFFFFF || cinfo.output_components != 3
		)
	{
		// Free the memory to make sure we don't leak memory
		ri.FS_FreeFile (fbuffer.v);
		jpeg_destroy_decompress(&cinfo);

		Com_Printf("LoadJPG: %s has an invalid image format: %dx%d*4=%d, components: %d", filename,
			cinfo.output_width, cinfo.output_height, pixelcount * 4, cinfo.output_components);
		return;
	}

	memcount = pixelcount * 3;
	row_stride = cinfo.output_width * cinfo.output_components;

	out = (byte *)Z_Malloc(memcount, TAG_TEMP_WORKSPACE, qfalse);

	*width = cinfo.output_width;
	*height = cinfo.output_height;

	/* Step 6: while (scan lines remain to be read) */
	/*		   jpeg_read_scanlines(...); */

	/* Here we use the library's state variable cinfo.output_scanline as the
	* loop counter, so that we don't have to keep track ourselves.
	*/
	while (cinfo.output_scanline < cinfo.output_height) {
		/* jpeg_read_scanlines expects an array of pointers to scanlines.
		* Here the array is only one element long, but you could ask for
		* more than one scanline at a time if that's more convenient.
		*/
		buf = ((out+(row_stride*cinfo.output_scanline)));
		buffer = &buf;
		(void) jpeg_read_scanlines(&cinfo, buffer, 1);
	}

	buf = out;

	*pic = out;

	/* Step 7: Finish decompression */

	(void) jpeg_finish_decompress(&cinfo);
	/* We can ignore the return value since suspension is not possible
	* with the stdio data source.
	*/

	/* Step 8: Release JPEG decompression object */

	/* This is an important step since it will release a good deal of memory. */
	jpeg_destroy_decompress(&cinfo);

	/* After finish_decompress, we can close the input file.
	* Here we postpone it until after no more JPEG errors are possible,
	* so as to simplify the setjmp error logic above.  (Actually, I don't
	* think that jpeg_destroy can do an error exit, but why assume anything...)
	*/
	ri.FS_FreeFile (fbuffer.v);
	/* At this point you may want to check to see whether any corrupt-data
	* warnings occurred (test whether jerr.pub.num_warnings is nonzero).
	*/

	/* And we're done! */
}


/* Expanded data destination object for stdio output */

typedef struct {
  struct jpeg_destination_mgr pub; /* public fields */

  byte* outfile;		/* target stream */
  int	size;
} my_destination_mgr;

typedef my_destination_mgr * my_dest_ptr;


/*
 * Initialize destination --- called by jpeg_start_compress
 * before any data is actually written.
 */

void init_destination (j_compress_ptr cinfo)
{
  my_dest_ptr dest = (my_dest_ptr) cinfo->dest;

  dest->pub.next_output_byte = dest->outfile;
  dest->pub.free_in_buffer = dest->size;
}


/*
 * Empty the output buffer --- called whenever buffer fills up.
 *
 * In typical applications, this should write the entire output buffer
 * (ignoring the current state of next_output_byte & free_in_buffer),
 * reset the pointer & count to the start of the buffer, and return TRUE
 * indicating that the buffer has been dumped.
 *
 * In applications that need to be able to suspend compression due to output
 * overrun, a FALSE return indicates that the buffer cannot be emptied now.
 * In this situation, the compressor will return to its caller (possibly with
 * an indication that it has not accepted all the supplied scanlines).  The
 * application should resume compression after it has made more room in the
 * output buffer.  Note that there are substantial restrictions on the use of
 * suspension --- see the documentation.
 *
 * When suspending, the compressor will back up to a convenient restart point
 * (typically the start of the current MCU). next_output_byte & free_in_buffer
 * indicate where the restart point will be if the current call returns FALSE.
 * Data beyond this point will be regenerated after resumption, so do not
 * write it out when emptying the buffer externally.
 */

boolean empty_output_buffer (j_compress_ptr cinfo)
{
  return TRUE;
}

/*
 * Terminate destination --- called by jpeg_finish_compress
 * after all data has been written.  Usually needs to flush buffer.
 *
 * NB: *not* called by jpeg_abort or jpeg_destroy; surrounding
 * application must deal with any cleanup that should happen even
 * for error exit.
 */

static size_t hackSize;

void term_destination (j_compress_ptr cinfo)
{
  my_dest_ptr dest = (my_dest_ptr) cinfo->dest;
  size_t datacount = dest->size - dest->pub.free_in_buffer;
  hackSize = datacount;
}


/*
 * Prepare for output to a stdio stream.
 * The caller must have already opened the stream, and is responsible
 * for closing it after finishing compression.
 */

static void jpegDest(j_compress_ptr cinfo, byte* outfile, int size)
{
	my_dest_ptr dest;

	/* The destination object is made permanent so that multiple JPEG images
	* can be written to the same file without re-executing jpeg_stdio_dest.
	* This makes it dangerous to use this manager and a different destination
	* manager serially with the same JPEG object, because their private object
	* sizes may be different.  Caveat programmer.
	*/
	if (cinfo->dest == NULL) {	/* first time for this JPEG object? */
		cinfo->dest = (struct jpeg_destination_mgr *)
			(*cinfo->mem->alloc_small) ((j_common_ptr)cinfo, JPOOL_PERMANENT,
			sizeof(my_destination_mgr));
	}

	dest = (my_dest_ptr)cinfo->dest;
	dest->pub.init_destination = init_destination;
	dest->pub.empty_output_buffer = empty_output_buffer;
	dest->pub.term_destination = term_destination;
	dest->outfile = outfile;
	dest->size = size;
}

size_t SaveJPGToBuffer(byte *buffer, size_t bufSize, int quality,
	int image_width, int image_height, byte *image_buffer, int padding)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPROW row_pointer[1];	/* pointer to JSAMPLE row[s] */
	my_dest_ptr dest;
	int row_stride;		/* physical row width in image buffer */
	size_t outcount;

	/* Step 1: allocate and initialize JPEG compression object */

	cinfo.err = jpeg_std_error(&jerr);

	/* Now we can initialize the JPEG compression object. */
	jpeg_create_compress(&cinfo);

	/* Step 2: specify data destination (eg, a file) */
	/* Note: steps 2 and 3 can be done in either order. */

	jpegDest(&cinfo, buffer, (int)bufSize);

	/* Step 3: set parameters for compression */
	cinfo.image_width = image_width; 	/* image width and height, in pixels */
	cinfo.image_height = image_height;
	cinfo.input_components = 3;		/* # of color components per pixel */
	cinfo.in_color_space = JCS_RGB; 	/* colorspace of input image */
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE /* limit to baseline-JPEG values */);

	/* If quality is set high, disable chroma subsampling */
	if (quality >= 85) {
		cinfo.comp_info[0].h_samp_factor = 1;
		cinfo.comp_info[0].v_samp_factor = 1;
	}

	/* Step 4: Start compressor */

	jpeg_start_compress(&cinfo, TRUE);

	/* Step 5: while (scan lines remain to be written) */
	/*           jpeg_write_scanlines(...); */

	row_stride = image_width * cinfo.input_components + padding; /* JSAMPLEs per row in image_buffer */

	while (cinfo.next_scanline < cinfo.image_height) {
		/* jpeg_write_scanlines expects an array of pointers to scanlines.
		* Here the array is only one element long, but you could pass
		* more than one scanline at a time if that's more convenient.
		*/
		row_pointer[0] = &image_buffer[((cinfo.image_height - 1)*row_stride) - cinfo.next_scanline * row_stride];
		(void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	/* Step 6: Finish compression */
	jpeg_finish_compress(&cinfo);

	dest = (my_dest_ptr)cinfo.dest;
	outcount = dest->size - dest->pub.free_in_buffer;

	/* Step 7: release JPEG compression object */
	jpeg_destroy_compress(&cinfo);

	/* And we're done! */
	return outcount;
}


void SaveJPG(const char * filename, int quality, int image_width, int image_height, byte *image_buffer, int padding)
{
	byte *out;
	size_t bufSize;

	bufSize = image_width * image_height * 3;
	out = (byte *)Hunk_AllocateTempMemory((int)bufSize);

	bufSize = SaveJPGToBuffer(out, bufSize, quality, image_width, image_height, image_buffer, padding);
	ri.FS_WriteFile(filename, out, (int)bufSize);

	Hunk_FreeTempMemory(out);
}


//===================================================================

void user_read_data(png_structp png_ptr, png_bytep data, png_size_t length);

void png_print_error(png_structp png_ptr, png_const_charp err)
{
	ri.Printf(PRINT_ERROR, "%s\n", err);
}

void png_print_warning(png_structp png_ptr, png_const_charp warning)
{
	ri.Printf(PRINT_WARNING, "%s\n", warning);
}

bool IsPowerOfTwo(int i) { return (i & (i - 1)) == 0; }

struct PNGFileReader
{
	PNGFileReader(char *buf) : buf(buf), offset(0), png_ptr(NULL), info_ptr(NULL) {}
	~PNGFileReader()
	{
		ri.FS_FreeFile(buf);

		// Destroys both structs
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	}

	int Read(byte **data, int *width, int *height, pixelFormat_t *format)
	{
		// Setup the pointers
		*data = NULL;
		*width = 0;
		*height = 0;

		// Make sure we're actually reading PNG data.
		const int SIGNATURE_LEN = 8;

		byte ident[SIGNATURE_LEN];
		memcpy(ident, buf, SIGNATURE_LEN);

		if (!png_check_sig(ident, SIGNATURE_LEN))
		{
			ri.Printf(PRINT_ERROR, "PNG signature not found in given image.");
			return 0;
		}

		png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, png_print_error, png_print_warning);
		if (png_ptr == NULL)
		{
			ri.Printf(PRINT_ERROR, "Could not allocate enough memory to load the image.");
			return 0;
		}

		info_ptr = png_create_info_struct(png_ptr);
		if (setjmp(png_jmpbuf(png_ptr)))
		{
			return 0;
		}

		// We've read the signature
		offset += SIGNATURE_LEN;

		// Setup reading information, and read header
		png_set_read_fn(png_ptr, (png_voidp)this, &user_read_data);
#ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
		// This generic "ignore all, except required chunks" requires 1.6.0 or newer"
		png_set_keep_unknown_chunks(png_ptr, PNG_HANDLE_CHUNK_NEVER, NULL, -1);
#endif
		png_set_sig_bytes(png_ptr, SIGNATURE_LEN);
		png_read_info(png_ptr, info_ptr);

		png_uint_32 width_;
		png_uint_32 height_;
		int depth;
		int colortype;

		png_get_IHDR(png_ptr, info_ptr, &width_, &height_, &depth, &colortype, NULL, NULL, NULL);

		// While modern OpenGL can handle non-PoT textures, it's faster to handle only PoT
		// so that the graphics driver doesn't have to fiddle about with the texture when uploading.
		if (!IsPowerOfTwo(width_) || !IsPowerOfTwo(height_))
		{
			ri.Printf(PRINT_ERROR, "Width or height is not a power-of-two.\n");
			return 0;
		}

		// This function is equivalent to using what used to be LoadPNG32. LoadPNG8 also existed,
		// but this only seemed to be used by the RMG system which does not work in JKA. If this
		// does need to be re-implemented, then colortype should be PNG_COLOR_TYPE_PALETTE or
		// PNG_COLOR_TYPE_GRAY.
		if (colortype != PNG_COLOR_TYPE_RGB && colortype != PNG_COLOR_TYPE_RGBA)
		{
			ri.Printf(PRINT_ERROR, "Image is not 24-bit or 32-bit.");
			return 0;
		}

		int samples;
		// Read the png data
		switch (colortype) {
		case PNG_COLOR_TYPE_RGB:
			*format = PXF_RGB;
			samples = 3;
			break;
		case PNG_COLOR_TYPE_RGBA:
			*format = PXF_RGBA;
			samples = 4;
			break;
		}

		png_read_update_info(png_ptr, info_ptr);

		byte *tempData = (byte *)ri.Malloc(width_ * height_ * samples, TAG_IMAGE_T, qfalse);
		if (!tempData)
		{
			ri.Printf(PRINT_ERROR, "Could not allocate enough memory to load the image.");
			return 0;
		}

		// Dynamic array of row pointers, with 'height' elements, initialized to NULL.
		byte **row_pointers = (byte **)ri.Malloc(sizeof(byte *) * height_, TAG_IMAGE_T, qfalse);
		if (!row_pointers)
		{
			ri.Printf(PRINT_ERROR, "Could not allocate enough memory to load the image.");

			ri.Free(tempData);

			return 0;
		}

		// Re-set the jmp so that these new memory allocations can be reclaimed
		if (setjmp(png_jmpbuf(png_ptr)))
		{
			ri.Free(row_pointers);
			ri.Free(tempData);
			return 0;
		}

		for (unsigned int i = 0; i < height_; i++)
		{
			row_pointers[i] = tempData + i * samples * width_;
		}

		png_read_image(png_ptr, row_pointers);

		// Finish reading
		png_read_end(png_ptr, NULL);

		ri.Free(row_pointers);

		// Finally assign all the parameters
		*data = tempData;
		*width = width_;
		*height = height_;

		return 1;
	}

	void ReadBytes(void *dest, size_t len)
	{
		memcpy(dest, buf + offset, len);
		offset += len;
	}

private:
	char *buf;
	size_t offset;
	png_structp png_ptr;
	png_infop info_ptr;
};

void user_read_data(png_structp png_ptr, png_bytep data, png_size_t length) {
	png_voidp r = png_get_io_ptr(png_ptr);
	PNGFileReader *reader = (PNGFileReader *)r;
	reader->ReadBytes(data, length);
}

// Loads a PNG image from file.
void LoadPNG(const char *filename, byte **data, int *width, int *height, pixelFormat_t *format, qboolean skipJKA)
{
	char *buf = NULL;
	int len;
	if ( skipJKA ) len = ri.FS_ReadFileSkipJKA(filename, (void**)&buf);
	else           len = ri.FS_ReadFile(filename, (void **)&buf);
	if (len < 0 || buf == NULL)
	{
		return;
	}

	PNGFileReader reader(buf);
	reader.Read(data, width, height, format);
}




/*
=================
R_LoadImage

Loads any of the supported image types into a cannonical
32 bit format.
=================
*/
void R_LoadImage( const char *shortname, byte **pic, int *width, int *height, pixelFormat_t *format )
{
	char	name[MAX_QPATH];

	*pic = NULL;
	*width = 0;
	*height = 0;
	*format = PXF_RGBA;

	// First try without JKA assets
	COM_StripExtension(shortname,name,sizeof(name));
	COM_DefaultExtension(name, sizeof(name), ".jpg");
	LoadJPG( name, pic, width, height, format, qtrue );
	if (*pic) {
		return;
	}

	COM_StripExtension(shortname,name,sizeof(name));
	COM_DefaultExtension(name, sizeof(name), ".png");
	LoadPNG( name, pic, width, height, format, qtrue ); 			// try png first
	if (*pic){
		return;
	}

	COM_StripExtension(shortname,name,sizeof(name));
	COM_DefaultExtension(name, sizeof(name), ".tga");
	LoadTGA( name, pic, width, height, format, qtrue );            // try tga first
	if (*pic){
		return;
	}


	// Retry with JKA assets
	COM_StripExtension(shortname,name,sizeof(name));
	COM_DefaultExtension(name, sizeof(name), ".jpg");
	LoadJPG( name, pic, width, height, format, qfalse );
	if (*pic) {
		return;
	}

	COM_StripExtension(shortname,name,sizeof(name));
	COM_DefaultExtension(name, sizeof(name), ".png");
	LoadPNG( name, pic, width, height, format, qfalse ); 			// try png first
	if (*pic){
		return;
	}

	COM_StripExtension(shortname,name,sizeof(name));
	COM_DefaultExtension(name, sizeof(name), ".tga");
	LoadTGA( name, pic, width, height, format, qfalse );            // try tga first
	if (*pic){
		return;
	}
}


/*
===============
R_FindImageFile

Finds or loads the given image.
Returns NULL if it fails, not a default image.
==============
*/
image_t *R_FindImageFile( const char *name, qboolean mipmap, qboolean allowPicmip, qboolean allowTC, int glWrapClampMode ) {
	upload_t upload = {
		(qboolean)!mipmap,
		(qboolean)!allowPicmip,
		(qboolean)!mipmap,
		(qboolean)!allowTC,
		(mipmap ? NULL : GetTextureMode("GL_LINEAR"))
	};

	return R_FindImageFileNew(name, &upload, glWrapClampMode);
}

/*
==============
R_MipMapLevel

All arguments must be powers of two.

Returns mipmap level (possibly negative) of a picture (w,h) relative
to a picture of dimensions (ref_w,ref_h). Returns MAX_MIP_LEVELS
if passed dimensions don't occur in any valid mipmap chain.
==============
*/
static int R_MipMapLevel(int ref_w, int ref_h, int w, int h)
{
	int level = 0;
	int	mult = -1;

	if (w < ref_w) {
		int temp;

		temp = w;
		w = ref_w;
		ref_w = temp;
		temp = h;
		h = ref_h;
		ref_h = temp;

		mult = 1;
	}

	ref_w >>= 1;
	ref_h >>= 1;

	while (w != 0 || h != 0) {
		w >>= 1;
		h >>= 1;

		if (w == ref_w && h == ref_h) {
			return mult * level;
		}

		level++;
	}

	return MAX_MIP_LEVELS;
}

image_t	*R_FindImageFileNew( const char *name, const upload_t *upload, int glWrapClampMode ) {
	image_t		*image;
	int			width, height;
	byte		*pic;
	char		*pName;
	byte		**mipmaps;
	byte		*mipdata[2 * MAX_MIP_LEVELS - 1] = { NULL };
	qboolean	customMip;
	pixelFormat_t	format;

	if (!name || com_dedicated->integer) {	// stop ghoul2 horribleness as regards image loading from server
		return NULL;
	}

	// need to do this here as well as in R_CreateImage, or R_FindImageFile_NoLoad() may complain about
	//	different clamp parms used...
	//
	if(glConfig.clampToEdgeAvailable && glWrapClampMode == GL_CLAMP) {
		glWrapClampMode = GL_CLAMP_TO_EDGE;
	}

	image = R_FindImageFile_NoLoad(name, upload, glWrapClampMode );
	if (image) {
		return image;
	}

	//
	// load the pic from disk
	//
	R_LoadImage( name, &pic, &width, &height, &format );
	if ( pic == NULL ) {                                    // if we dont get a successful load
		return NULL;                                        // bail
	}


	// refuse to find any files not power of 2 dims...
	//
	if ( (width&(width-1)) || (height&(height-1)) )
	{
		ri.Printf( PRINT_ALL, "Refusing to load non-power-2-dims(%d,%d) pic \"%s\"...\n", width,height,name );
		ri.Free( pic );
		return NULL;
	}

	customMip = qfalse;

	if (!upload->noMipMaps) {
		int		minLevel = MAX_MIP_LEVELS - 1;

		mipdata[minLevel] = pic;
		pName = GenerateImageMappingName(name);

		for (int n = 1; n < MAX_MIP_LEVELS; n++) {
			char	mipName[MAX_QPATH];
			int		w, h;
			int		level;
			pixelFormat_t mipFormat;

			Com_sprintf(mipName, sizeof(mipName), "%s_mip%d", pName, n);
			R_LoadImage(mipName, &pic, &w, &h, &mipFormat);

			if (!pic) {
				break;
			}

			if (mipFormat != format) {
				ri.Printf( PRINT_ALL, "Mipmap image format different than base image \"%s\"...\n", mipName );
				ri.Free( pic );
				continue;
			}

			if ( (w&(w-1)) || (h&(h-1)) )
			{
				ri.Printf( PRINT_ALL, "Refusing to load non-power-2-dims(%d,%d) pic \"%s\"...\n", w, h, mipName );
				ri.Free( pic );
				continue;
			}

			level = minLevel;
			level += R_MipMapLevel(width, height, w, h);
			if (level < 0 || minLevel + MAX_MIP_LEVELS <= level ) {
				ri.Printf( PRINT_ALL, "Wrong mipmap dimensions (%d,%d) \"%s\"\n", w, h, mipName);
				ri.Free( pic );
				continue;
			}
			if (level < minLevel) {
				minLevel = level;
				width = w;
				height = h;
			}

			if (mipdata[level]) {
				ri.Printf (PRINT_WARNING, "Multiple images for mipmap (%d,%d). Using \"%s\"\n", w, h, mipName);
				ri.Free(mipdata[level]);
			}

			mipdata[level] = pic;
			customMip = qtrue;
		}

		mipmaps = &mipdata[minLevel];
	} else { // noMipMaps
		mipmaps = &pic;
	}

	image = R_CreateImageNew( name, mipmaps, customMip, width, height, upload, glWrapClampMode, format );

	if (customMip) {
		for (size_t n = 0; n < ARRAY_LEN(mipdata); n++) {
			if (mipdata[n]) {
				ri.Free( mipdata[n] );
				mipdata[n] = NULL;
			}
		}
	} else {
	  ri.Free( *mipmaps );
	  *mipmaps = NULL;
	}

	return image;
}


/*
================
R_CreateDlightImage
================
*/
#define	DLIGHT_SIZE	16
static void R_CreateDlightImage( void ) {
	int		x,y;
	byte	data[DLIGHT_SIZE][DLIGHT_SIZE];
	int		b;

	// make a centered inverse-square falloff blob for dynamic lighting
	for (x=0 ; x<DLIGHT_SIZE ; x++) {
		for (y=0 ; y<DLIGHT_SIZE ; y++) {
			float	d;

			d = ( DLIGHT_SIZE/2 - 0.5f - x ) * ( DLIGHT_SIZE/2 - 0.5f - x ) +
				( DLIGHT_SIZE/2 - 0.5f - y ) * ( DLIGHT_SIZE/2 - 0.5f - y );
			b = 4000 / d;
			if (b > 255) {
				b = 255;
			} else if ( b < 75 ) {
				b = 0;
			}
			data[y][x] = b;
		}
	}
	tr.dlightImage = R_CreateImage("*dlight", (byte *)data, DLIGHT_SIZE, DLIGHT_SIZE, qfalse, qfalse, qfalse, GL_CLAMP, PXF_GRAY );
}


/*
=================
R_InitFogTable
=================
*/
void R_InitFogTable( void ) {
	int		i;
	float	d;

	for ( i = 0 ; i < FOG_TABLE_SIZE ; i++ ) {
		d = powf( (float)i/(FOG_TABLE_SIZE-1), 0.5f );

		tr.fogTable[i] = d;
	}
}

/*
================
R_FogFactor

Returns a 0.0 to 1.0 fog density value
This is called for each texel of the fog texture on startup
and for each vertex of transparent shaders in fog dynamically
================
*/
float	R_FogFactor( float s, float t ) {
	float	d;

	s -= 1.0f/512;
	if ( s < 0 ) {
		return 0;
	}
	if ( t < 1.0f/32 ) {
		return 0;
	}
	if ( t < 31.0f/32 ) {
		s *= (t - 1.0f/32.0f) * (32.0f/30.0f);
	}

	// we need to leave a lot of clamp range
	s *= 8;

	if ( s > 1.0f ) {
		s = 1.0f;
	}

	d = tr.fogTable[ (int)(s * (FOG_TABLE_SIZE-1)) ];

	return d;
}

/*
================
R_CreateFogImage
================
*/
#define	FOG_S	256
#define	FOG_T	32
static void R_CreateFogImage( void ) {
	int		x,y;
	byte	*data;
	float	g;
	float	d;
	float	borderColor[4];

	data = (unsigned char *)ri.Hunk_AllocateTempMemory( FOG_S * FOG_T * 4 );

	g = 2.0;

	// S is distance, T is depth
	for (x=0 ; x<FOG_S ; x++) {
		for (y=0 ; y<FOG_T ; y++) {
			d = R_FogFactor( ( x + 0.5f ) / FOG_S, ( y + 0.5f ) / FOG_T );

			data[(y*FOG_S+x)*4+0] =
			data[(y*FOG_S+x)*4+1] =
			data[(y*FOG_S+x)*4+2] = 255;
			data[(y*FOG_S+x)*4+3] = 255*d;
		}
	}
	// standard openGL clamping doesn't really do what we want -- it includes
	// the border color at the edges.  OpenGL 1.2 has clamp-to-edge, which does
	// what we want.
	tr.fogImage = R_CreateImage("*fog", (byte *)data, FOG_S, FOG_T, qfalse, qfalse, qfalse, GL_CLAMP, PXF_RGBA );
	ri.Hunk_FreeTempMemory( data );

	borderColor[0] = 1.0;
	borderColor[1] = 1.0;
	borderColor[2] = 1.0;
	borderColor[3] = 1;

	qglTexParameterfv( GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor );
}

/*
==================
R_CreateDefaultImage
==================
*/
#define	DEFAULT_SIZE	16
static void R_CreateDefaultImage( void ) {
	int		x;
	byte	data[DEFAULT_SIZE][DEFAULT_SIZE];

	// the default image will be a box, to allow you to see the mapping coordinates
	Com_Memset( data, 32, sizeof( data ) );
	for ( x = 0 ; x < DEFAULT_SIZE ; x++ ) {
		data[0][x] = 255;

		data[x][0] = 255;

		data[DEFAULT_SIZE-1][x] = 255;

		data[x][DEFAULT_SIZE-1] = 255;
	}
	tr.defaultImage = R_CreateImage("*default", (byte *)data, DEFAULT_SIZE, DEFAULT_SIZE, qtrue, qfalse, qfalse, GL_REPEAT, PXF_GRAY );
}

static void R_BindGlowImages( void ) {
	// Update dynamic glow textures when vidWidth/vidHeight changes

	qglDisable( GL_TEXTURE_2D );
	qglEnable( GL_TEXTURE_RECTANGLE_ARB );

	if (tr.screenGlow) {
		// Create the scene glow image. - AReis
		qglBindTexture( GL_TEXTURE_RECTANGLE_ARB, tr.screenGlow );
		qglTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA16, glConfig.vidWidth, glConfig.vidHeight, 0, GL_RGB, GL_FLOAT, 0 );
		qglTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP );
		qglTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP );
	}
	if (tr.sceneImage) {
		// Create the scene image. - AReis
		qglBindTexture( GL_TEXTURE_RECTANGLE_ARB, tr.sceneImage );
		qglTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA16, glConfig.vidWidth, glConfig.vidHeight, 0, GL_RGB, GL_FLOAT, 0 );
		qglTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP );
		qglTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP );
	}

	if ( r_DynamicGlowWidth->integer > glConfig.vidWidth  )
	{
		r_DynamicGlowWidth->integer = glConfig.vidWidth;
	}
	if ( r_DynamicGlowHeight->integer > glConfig.vidHeight  )
	{
		r_DynamicGlowHeight->integer = glConfig.vidHeight;
	}

	if (tr.blurImage) {
		// Create the minimized scene blur image.
		qglBindTexture( GL_TEXTURE_RECTANGLE_ARB, tr.blurImage );
		qglTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA16, r_DynamicGlowWidth->integer, r_DynamicGlowHeight->integer, 0, GL_RGB, GL_FLOAT, 0 );
		qglTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP );
		qglTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP );
	}

	qglDisable( GL_TEXTURE_RECTANGLE_ARB );
	qglEnable( GL_TEXTURE_2D );
}

/*
==================
R_CreateBuiltinImages
==================
*/
void R_CreateBuiltinImages( void ) {
	int		x,y;
	byte	data[DEFAULT_SIZE][DEFAULT_SIZE];

	R_CreateDefaultImage();

	// we use a solid white image instead of disabling texturing
	Com_Memset( data, 255, sizeof( data ) );
	tr.whiteImage = R_CreateImage("*white", (byte *)data, 8, 8, qfalse, qfalse, qfalse, GL_REPEAT, PXF_GRAY );

	tr.screenGlow = 1024 + giTextureBindNum++;
	tr.sceneImage = 1024 + giTextureBindNum++;
	tr.blurImage = 1024 + giTextureBindNum++;

	R_BindGlowImages( );

	// with overbright bits active, we need an image which is some fraction of full color,
	// for default lightmaps, etc
	for (x=0 ; x<DEFAULT_SIZE ; x++) {
		for (y=0 ; y<DEFAULT_SIZE ; y++) {
			data[y][x] = tr.identityLightByte;
		}
	}


	tr.identityLightImage = R_CreateImage("*identityLight", (byte *)data, 8, 8, qfalse, qfalse, qfalse, GL_REPEAT, PXF_GRAY );


	for(x=0;x<32;x++) {
		// scratchimage is usually used for cinematic drawing
		tr.scratchImage[x] = R_CreateImage(va("*scratch%d",x), (byte *)data, DEFAULT_SIZE, DEFAULT_SIZE, qfalse, qtrue, qfalse, GL_CLAMP, PXF_GRAY );
	}

	if (r_newDLights->integer)
	{
		tr.dlightImage = R_FindImageFile("gfx/2d/dlight", qtrue, qfalse, qfalse, GL_CLAMP);
	}
	else
	{
		R_CreateDlightImage();
	}
	R_CreateFogImage();
}

/*
===============
R_UpdateImages

Update images when renderer size changes
===============
*/
void R_UpdateImages( void ) {
	R_BindGlowImages();
}

/*
===============
R_SetColorMappings
===============
*/
void R_SetColorMappings( void ) {
	int		i, j;
	float	g;
	int		inf;
	int		shift;

	// setup the overbright lighting
	tr.overbrightBits = r_overBrightBits->integer;

	if (r_gammamethod->integer == GAMMA_NONE)
	{
		tr.overbrightBits = 0;
	}

	// never overbright in windowed mode
	if (r_gammamethod->integer == GAMMA_HARDWARE && !glConfig.isFullscreen) {
		tr.overbrightBits = 0;
	}

	if ( tr.overbrightBits > 1 ) {
		tr.overbrightBits = 1;
	}

	if ( tr.overbrightBits < 0 ) {
		tr.overbrightBits = 0;
	}

	tr.identityLight = 1.0f / ( 1 << tr.overbrightBits );
	tr.identityLightByte = 255 * tr.identityLight;


	if ( r_intensity->value < 1.0f ) {
		ri.Cvar_Set( "r_intensity", "1" );
	}

	if ( r_gamma->value < 0.5f ) {
		ri.Cvar_Set( "r_gamma", "0.5" );
	} else if ( r_gamma->value > 3.0f ) {
		ri.Cvar_Set( "r_gamma", "3.0" );
	}

	g = r_gamma->value;
	shift = tr.overbrightBits;

	if (r_gammamethod->integer != GAMMA_POSTPROCESSING) {
		for (i = 0; i < 256; i++) {
			if (g == 1) {
				inf = i;
			} else {
				inf = 255 * powf(i / 255.0f, 1.0f / g) + 0.5f;
			}
			inf <<= shift;
			if (inf < 0) {
				inf = 0;
			}
			if (inf > 255) {
				inf = 255;
			}
			s_gammatable[i] = inf;
		}
	} else {
		byte gammaCorrected[64];
		
		for (int i = 0; i < 64; i++) {
			if (g == 1.0f) {
				inf = (int)(((float)i / 63.0f) * 255.0f + 0.5f);
			} else {
				inf = (int)(255.0f * powf(i / 63.0f, 1.0f / g) + 0.5f);
			}

			gammaCorrected[i] = Com_Clampi(0, 255, inf << shift);
		}
		
		byte *lutTable = (byte *)Hunk_AllocateTempMemory(64 * 64 * 64 * 3);
		byte *write = lutTable;
		for (int z = 0; z < 64; z++) {
			for (int y = 0; y < 64; y++) {
				for (int x = 0; x < 64; x++) {
					*write++ = gammaCorrected[x];
					*write++ = gammaCorrected[y];
					*write++ = gammaCorrected[z];
				}
			}
		}

		qglBindTexture(GL_TEXTURE_3D, tr.gammaLUTImage);
		qglPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		qglTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, 64, 64, 64, GL_RGB, GL_UNSIGNED_BYTE, lutTable);
		
		Hunk_FreeTempMemory(lutTable);
	}

	for (i=0 ; i<256 ; i++) {
		j = i * r_intensity->value;
		if (j > 255) {
			j = 255;
		}
		s_intensitytable[i] = j;
	}

	// gamma correction
	if (r_gammamethod->integer == GAMMA_HARDWARE) {
		WIN_SetGamma( &glConfig, s_gammatable, s_gammatable, s_gammatable );
	}
}

/*
===============
R_InitImages
===============
*/
void	R_InitImages( void ) {
	// gamma render target
	if (r_gammamethod->integer == GAMMA_POSTPROCESSING) {
		qglEnable(GL_TEXTURE_3D);
		tr.gammaLUTImage = 1024 + giTextureBindNum++;
		qglBindTexture(GL_TEXTURE_3D, tr.gammaLUTImage);
		qglTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 64, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		qglTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		qglTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		qglTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		qglTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		qglTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		qglDisable(GL_TEXTURE_3D);
		qglEnable(GL_TEXTURE_2D);
	}

	// build brightness translation tables
	R_SetColorMappings();

	// create default texture and white texture
	R_CreateBuiltinImages();
}

/*
===============
R_DeleteTextures
===============
*/
// (only gets called during vid_restart now (and app exit), not during map load)
//
void R_DeleteTextures( void ) {

	R_Images_Clear();
	GL_ResetBinds();
}

/*
============================================================================

SKINS

============================================================================
*/

/*
==================
CommaParse

This is unfortunate, but the skin files aren't
compatable with our normal parsing rules.
==================
*/
static char *CommaParse( char **data_p ) {
	int c = 0, len;
	char *data;
	static	char	com_token[MAX_TOKEN_CHARS];

	data = *data_p;
	len = 0;
	com_token[0] = 0;

	// make sure incoming data is valid
	if ( !data ) {
		*data_p = NULL;
		return com_token;
	}

	while ( 1 ) {
		// skip whitespace
		while( (c = *data) <= ' ') {
			if( !c ) {
				break;
			}
			data++;
		}


		c = *data;

		// skip double slash comments
		if ( c == '/' && data[1] == '/' )
		{
			while (*data && *data != '\n')
				data++;
		}
		// skip /* */ comments
		else if ( c=='/' && data[1] == '*' )
		{
			while ( *data && ( *data != '*' || data[1] != '/' ) )
			{
				data++;
			}
			if ( *data )
			{
				data += 2;
			}
		}
		else
		{
			break;
		}
	}

	if ( c == 0 ) {
		return "";
	}

	// handle quoted strings
	if (c == '\"')
	{
		data++;
		while (1)
		{
			c = *data++;
			if (c=='\"' || !c)
			{
				com_token[len] = 0;
				*data_p = ( char * ) data;
				return com_token;
			}
			if (len < MAX_TOKEN_CHARS)
			{
				com_token[len] = c;
				len++;
			}
		}
	}

	// parse a regular word
	do
	{
		if (len < MAX_TOKEN_CHARS)
		{
			com_token[len] = c;
			len++;
		}
		data++;
		c = *data;
	} while (c>32 && c != ',' );

	if (len == MAX_TOKEN_CHARS)
	{
//		Com_Printf ("Token exceeded %i chars, discarded.\n", MAX_TOKEN_CHARS);
		len = 0;
	}
	com_token[len] = 0;

	*data_p = ( char * ) data;
	return com_token;
}

/*
===============
RE_SplitSkins
input = skinname, possibly being a macro for three skins
return= true if three part skins found
output= qualified names to three skins if return is true, undefined if false
===============
*/
bool RE_SplitSkins(const char *INname, char *skinhead, char *skintorso, char *skinlower)
{	//INname= "models/players/jedi_tf/|head01_skin1|torso01|lower01";
	if (strchr(INname, '|'))
	{
		char name[MAX_QPATH];
		Q_strncpyz(name, INname, sizeof(name));
		char *p = strchr(name, '|');
		*p=0;
		p++;
		//fill in the base path
		strcpy (skinhead, name);
		strcpy (skintorso, name);
		strcpy (skinlower, name);

		//now get the the individual files

		//advance to second
		char *p2 = strchr(p, '|');
		assert(p2);
		if (!p2)
		{
			return false;
		}
		*p2=0;
		p2++;
		strcat (skinhead, p);
		strcat (skinhead, ".skin");


		//advance to third
		p = strchr(p2, '|');
		assert(p);
		if (!p)
		{
			return false;
		}
		*p=0;
		p++;
		strcat (skintorso,p2);
		strcat (skintorso, ".skin");

		strcat (skinlower,p);
		strcat (skinlower, ".skin");

		return true;
	}
	return false;
}

// given a name, go get the skin we want and return
qhandle_t RE_RegisterIndividualSkin( const char *name , qhandle_t hSkin)
{
	skin_t			*skin;
	skinSurface_t	*surf;
	char			*text, *text_p;
	char			*token;
	char			surfName[MAX_QPATH];

	// load and parse the skin file
	ri.FS_ReadFile( name, (void **)&text );
	if ( !text ) {
#ifndef FINAL_BUILD
		ri.Printf( PRINT_ALL, "WARNING: RE_RegisterSkin( '%s' ) failed to load!\n", name );
#endif
		return 0;
	}

	assert (tr.skins[hSkin]);	//should already be setup, but might be an 3part append

	skin = tr.skins[hSkin];

	text_p = text;
	while ( text_p && *text_p ) {
		// get surface name
		token = CommaParse( &text_p );
		Q_strncpyz( surfName, token, sizeof( surfName ) );

		if ( !token[0] ) {
			break;
		}
		// lowercase the surface name so skin compares are faster
		Q_strlwr( surfName );

		if ( *text_p == ',' ) {
			text_p++;
		}

		if ( !strncmp( token, "tag_", 4 ) ) {	//these aren't in there, but just in case you load an id style one...
			continue;
		}

		// parse the shader name
		token = CommaParse( &text_p );

		if ( r_loadSkinsJKA->integer == 2 && !strcmp( &surfName[strlen(surfName)-4], "_off") )
		{
			if ( !strcmp( token ,"*off" ) )
			{
				continue;	//don't need these double offs
			}
			surfName[strlen(surfName)-4] = 0;	//remove the "_off"
		}
		if ( ARRAY_LEN(skin->surfaces) <= (unsigned)skin->numSurfaces )
		{
			assert( ARRAY_LEN(skin->surfaces) > (unsigned)skin->numSurfaces );
			ri.Printf( PRINT_ALL, "WARNING: RE_RegisterSkin( '%s' ) more than %u surfaces!\n", name, (unsigned int )(sizeof(skin->surfaces) / sizeof(*(skin->surfaces))) );
			break;
		}
		surf = skin->surfaces[ skin->numSurfaces ] = (skinSurface_t *) Hunk_Alloc( sizeof( *skin->surfaces[0] ), h_low );
		Q_strncpyz( surf->name, surfName, sizeof( surf->name ) );

		/*
		if (gServerSkinHack)
		{
			surf->shader = R_FindServerShader( token, lightmapsNone, stylesDefault, qtrue );
		}
		else
		{
		*/
			surf->shader = R_FindShader( token, lightmapsNone, stylesDefault, qtrue, qfalse, qtrue );
		/*
		}
		*/
		skin->numSurfaces++;
	}

	ri.FS_FreeFile( text );


	// never let a skin have 0 shaders
	if ( skin->numSurfaces == 0 ) {
		return 0;		// use default skin
	}

	return hSkin;
}


/*
===============
RE_RegisterSkin

===============
*/
qhandle_t RE_RegisterSkin( const char *name ) {
	qhandle_t	hSkin;
	skin_t		*skin;
	skinSurface_t	*surf;
	char		*text, *text_p;
	char		*token;
	char		surfName[MAX_QPATH];
	char		nameBuffer[MAX_QPATH+2]; // +2, so we can detect cases when we exceed MAX_QPATH

	if ( !name || !name[0] ) {
		Com_Printf( "Empty name passed to RE_RegisterSkin\n" );
		return 0;
	}

	if ( r_loadSkinsJKA->integer ) // FIXME: Add a new feature to the cgame API to "control" this and disable this "hack"?
	{ // JK2 compatibility hack - (the jk2 cgame code calls RegisterSkin with "modelpath/model_*.skin", but the SplitSkin function expects "modelpath/*", so we have to remove the "model_" and ".skin" if it's appears to be a splitable skin)
		// Doing this in the SplitSkin function isn't an option, as we limited the buffer to MAX_QPATH and increasing it would require changes in a lot more functions.
		const char *pos = strstr(name, "model_|");
		if ( pos && strlen(pos) > 7 )
		{
			Q_strncpyz(nameBuffer, name, pos-name+1);
			Q_strcat(nameBuffer, sizeof(nameBuffer), pos+6);

			pos = strstr(nameBuffer, ".skin");
			if ( pos ) nameBuffer[strlen(nameBuffer)-strlen(pos)] = 0;
			name = nameBuffer;
		}
	}

	if ( strlen( name ) >= MAX_QPATH ) {
		Com_Printf( "Skin name exceeds MAX_QPATH\n" );
		return 0;
	}


	// see if the skin is already loaded
	for ( hSkin = 1; hSkin < tr.numSkins ; hSkin++ ) {
		skin = tr.skins[hSkin];
		if ( !Q_stricmp( skin->name, name ) ) {
			if( skin->numSurfaces == 0 ) {
				return 0;		// default skin
			}
			return hSkin;
		}
	}

	// allocate a new skin
	if ( tr.numSkins == MAX_SKINS ) {
		ri.Printf( PRINT_WARNING, "WARNING: RE_RegisterSkin( '%s' ) MAX_SKINS hit\n", name );
		return 0;
	}
	tr.numSkins++;
	skin = (struct skin_s *)ri.Hunk_Alloc( sizeof( skin_t ), h_low );
	tr.skins[hSkin] = skin;
	Q_strncpyz( skin->name, name, sizeof( skin->name ) );
	skin->numSurfaces = 0;

	// make sure the render thread is stopped
	R_SyncRenderThread();

	if ( r_loadSkinsJKA->integer )
	{ // JKA-Style skin-loading
		char skinhead[MAX_QPATH]={0};
		char skintorso[MAX_QPATH]={0};
		char skinlower[MAX_QPATH]={0};
		if ( RE_SplitSkins(name, (char*)&skinhead, (char*)&skintorso, (char*)&skinlower ) )
		{//three part
			hSkin = RE_RegisterIndividualSkin(skinhead, hSkin);
			if (hSkin)
			{
				hSkin = RE_RegisterIndividualSkin(skintorso, hSkin);
				if (hSkin)
				{
					hSkin = RE_RegisterIndividualSkin(skinlower, hSkin);
				}
			}
		}
		else
		{//single skin
			hSkin = RE_RegisterIndividualSkin(name, hSkin);
		}
	}
	else
	{ // Original JK2-Style skin-loading
		// If not a .skin file, load as a single shader
		if ( strcmp( name + strlen( name ) - 5, ".skin" ) ) {
			skin->numSurfaces = 1;
			skin->surfaces[0] = (skinSurface_t *)ri.Hunk_Alloc( sizeof(*skin->surfaces[0]), h_low );
			skin->surfaces[0]->shader = R_FindShader( name, lightmapsNone, stylesDefault, qtrue );
			return hSkin;
		}
		
		// load and parse the skin file
		ri.FS_ReadFile( name, (void **)&text );
		if ( !text ) {
#ifndef FINAL_BUILD
			ri.Printf( PRINT_ALL, "WARNING: RE_RegisterSkin( '%s' ) failed to load!\n", name );
#endif
			return 0;
		}

		text_p = text;
		while ( text_p && *text_p ) {
			// get surface name
			token = CommaParse( &text_p );
			Q_strncpyz( surfName, token, sizeof( surfName ) );

			if ( !token[0] ) {
				break;
			}
			// lowercase the surface name so skin compares are faster
			Q_strlwr( surfName );

			if ( *text_p == ',' ) {
				text_p++;
			}

			if ( strstr( token, "tag_" ) ) {
				continue;
			}

			// parse the shader name
			token = CommaParse( &text_p );

			assert ( skin->numSurfaces < MD3_MAX_SURFACES );

			surf = skin->surfaces[ skin->numSurfaces ] = (skinSurface_t *)ri.Hunk_Alloc( sizeof( *skin->surfaces[0] ), h_low );
			Q_strncpyz( surf->name, surfName, sizeof( surf->name ) );
			surf->shader = R_FindShader( token, lightmapsNone, stylesDefault, qtrue );
			skin->numSurfaces++;
		}

		ri.FS_FreeFile( text );


		// never let a skin have 0 shaders
		if ( skin->numSurfaces == 0 ) {
			return 0;		// use default skin
		}
	}

	return hSkin;
}

#endif // !DEDICATED
/*
===============
R_InitSkins
===============
*/
void	R_InitSkins( void ) {
	skin_t		*skin;

	tr.numSkins = 1;

	// make the default skin have all default shaders
	skin = tr.skins[0] = (struct skin_s *)ri.Hunk_Alloc( sizeof( skin_t ), h_low );
	Q_strncpyz( skin->name, "<default skin>", sizeof( skin->name )  );
	skin->numSurfaces = 1;
	skin->surfaces[0] = (skinSurface_t *)ri.Hunk_Alloc( sizeof( *skin->surfaces[0] ), h_low );
	skin->surfaces[0]->shader = tr.defaultShader;
}

/*
===============
R_GetSkinByHandle
===============
*/
skin_t	*R_GetSkinByHandle( qhandle_t hSkin ) {
	if ( hSkin < 1 || hSkin >= tr.numSkins ) {
		return tr.skins[0];
	}
	return tr.skins[ hSkin ];
}

#ifndef DEDICATED
/*
===============
R_SkinList_f
===============
*/
void	R_SkinList_f( void ) {
	int			i, j;
	skin_t		*skin;

	ri.Printf (PRINT_ALL, "------------------\n");

	for ( i = 0 ; i < tr.numSkins ; i++ ) {
		skin = tr.skins[i];

		ri.Printf( PRINT_ALL, "%3i:%s\n", i, skin->name );
		for ( j = 0 ; j < skin->numSurfaces ; j++ ) {
			ri.Printf( PRINT_ALL, "       %s = %s\n",
				skin->surfaces[j]->name, skin->surfaces[j]->shader->name );
		}
	}
	ri.Printf (PRINT_ALL, "------------------\n");
}

#endif // !DEDICATED
