// Copyright (C) 1999-2000 Id Software, Inc.
//
// q_shared.c -- stateless support routines that are included in each code dll
#include "q_shared.h"

/*
-------------------------
GetIDForString
-------------------------
*/


int GetIDForString(stringID_table_t *table, const char *string) {
	int	index = 0;

	while ((table[index].name != NULL) &&
		(table[index].name[0] != 0)) {
		if (!Q_stricmp(table[index].name, string))
			return table[index].id;

		index++;
	}

	return -1;
}

/*
-------------------------
GetStringForID
-------------------------
*/

const char *GetStringForID(stringID_table_t *table, int id) {
	int	index = 0;

	while ((table[index].name != NULL) &&
		(table[index].name[0] != 0)) {
		if (table[index].id == id)
			return table[index].name;

		index++;
	}

	return NULL;
}

/*
============
COM_SkipPath
============
*/
char *COM_SkipPath(char *pathname) {
	char	*last;

	last = pathname;
	while (*pathname) {
		if (*pathname == '/')
			last = pathname + 1;
		pathname++;
	}
	return last;
}

/*
============
COM_StripExtension

R_RemapShader exploit
http://www.exploit-db.com/exploits/1750/
http://ioqsrc.vampireducks.com/d8/dbe/q__shared_8c-source.html#l00061
============
*/
void COM_StripExtension(const char *in, char *out, size_t destsize) {
	int length;
	assert(out != in);
	Q_strncpyz(out, in, destsize);
	length = (int)strlen(out) - 1;
	while (length > 0 && out[length] != '.') {
		length--;
		if (out[length] == '/')
			return;   // no extension
	}
	if (length)
		out[length] = 0;
}


/*
==================
COM_DefaultExtension
==================
*/
void COM_DefaultExtension(char *path, size_t maxSize, const char *extension) {
	char	oldPath[MAX_QPATH];
	char	*src;

	//
	// if path doesn't have a .EXT, append extension
	// (extension should include the .)
	//
	src = path + strlen(path) - 1;

	while (*src != '/' && src != path) {
		if (*src == '.') {
			return;                 // it has an extension
		}
		src--;
	}

	Q_strncpyz(oldPath, path, sizeof(oldPath));
	Com_sprintf(path, maxSize, "%s%s", oldPath, extension);
}

/*
==================
COM_DefaultExtension

Append extension if it's missing or existing extension doesn't match
==================
*/
void COM_SanitizeExtension(char *path, size_t maxSize, const char *extension) {
	int			ext;
	char		*s;

	s = strrchr(path, '.');

	if (s != NULL && s > path && s[-1] != '/' && s[-1] != '\\') {
		if (!strcmp(s, extension)) {
			// path already has correct extension
			return;
		}
	}

	ext = (int)maxSize - (int)strlen(extension) - 1;
	if (ext < 0) {
		Com_Error(ERR_FATAL, "COM_SanitizeExtension: Extension would overflow");
	}
	path[ext] = '\0';
	Q_strcat(path, maxSize, extension);
}

/*
============================================================================

BYTE ORDER FUNCTIONS

============================================================================
*/
/*
// can't just use function pointers, or dll linkage can
// mess up when qcommon is included in multiple places
static short	(*_BigShort) (short l);
static short	(*_LittleShort) (short l);
static int		(*_BigLong) (int l);
static int		(*_LittleLong) (int l);
static qint64	(*_BigLong64) (qint64 l);
static qint64	(*_LittleLong64) (qint64 l);
static float	(*_BigFloat) (const float *l);
static float	(*_LittleFloat) (const float *l);

short	BigShort(short l){return _BigShort(l);}
short	LittleShort(short l) {return _LittleShort(l);}
int		BigLong (int l) {return _BigLong(l);}
int		LittleLong (int l) {return _LittleLong(l);}
qint64	BigLong64 (qint64 l) {return _BigLong64(l);}
qint64	LittleLong64 (qint64 l) {return _LittleLong64(l);}
float	BigFloat (const float *l) {return _BigFloat(l);}
float	LittleFloat (const float *l) {return _LittleFloat(l);}
*/

short   ShortSwap(short l) {
	unsigned short us = *(unsigned short *)&l;

	return
		((us & 0x00FFu) << 8u) |
		((us & 0xFF00u) >> 8u);
}

short	ShortNoSwap(short l) {
	return l;
}

int	LongSwap(int l) {
	unsigned int ui = *(unsigned int *)&l;

  return
    ((ui & 0x000000FFu) << 24u) |
    ((ui & 0x0000FF00u) <<  8u) |
    ((ui & 0x00FF0000u) >>  8u) |
    ((ui & 0xFF000000u) >> 24u);

}

int	LongNoSwap(int l) {
	return l;
}

qint64 Long64Swap(qint64 ll) {
	qint64	result;

	result.b0 = ll.b7;
	result.b1 = ll.b6;
	result.b2 = ll.b5;
	result.b3 = ll.b4;
	result.b4 = ll.b3;
	result.b5 = ll.b2;
	result.b6 = ll.b1;
	result.b7 = ll.b0;

	return result;
}

qint64 Long64NoSwap(qint64 ll) {
	return ll;
}

float FloatSwap(const float *f) {
	floatint_t out;

	out.f = *f;
	out.i = LongSwap(out.i);

	return out.f;
}

float FloatNoSwap(const float *f) {
	return *f;
}

/*
================
Swap_Init
================
*/
/*
void Swap_Init (void)
{
byte	swaptest[2] = {1,0};

// set the byte swapping variables in a portable manner
if ( *(short *)swaptest == 1)
{
_BigShort = ShortSwap;
_LittleShort = ShortNoSwap;
_BigLong = LongSwap;
_LittleLong = LongNoSwap;
_BigLong64 = Long64Swap;
_LittleLong64 = Long64NoSwap;
_BigFloat = FloatSwap;
_LittleFloat = FloatNoSwap;
}
else
{
_BigShort = ShortNoSwap;
_LittleShort = ShortSwap;
_BigLong = LongNoSwap;
_LittleLong = LongSwap;
_BigLong64 = Long64NoSwap;
_LittleLong64 = Long64Swap;
_BigFloat = FloatNoSwap;
_LittleFloat = FloatSwap;
}

}
*/

/*
============================================================================

PARSING

============================================================================
*/

static	char	com_token[MAX_TOKEN_CHARS];
static	char	com_parsename[MAX_TOKEN_CHARS];
static	int		com_lines;

void COM_BeginParseSession(const char *name) {
	com_lines = 0;
	Com_sprintf(com_parsename, sizeof(com_parsename), "%s", name);
}

int COM_GetCurrentParseLine(void) {
	return com_lines;
}

char *COM_Parse(const char **data_p) {
	return COM_ParseExt(data_p, qtrue);
}

void COM_ParseError(char *format, ...) {
	va_list argptr;
	static char string[4096];

	va_start(argptr, format);
	Q_vsnprintf(string, sizeof(string), format, argptr);
	va_end(argptr);

	Com_Printf("ERROR: %s, line %d: %s\n", com_parsename, com_lines, string);
}

void COM_ParseWarning(char *format, ...) {
	va_list argptr;
	static char string[4096];

	va_start(argptr, format);
	Q_vsnprintf(string, sizeof(string), format, argptr);
	va_end(argptr);

	Com_Printf("WARNING: %s, line %d: %s\n", com_parsename, com_lines, string);
}

/*
==============
COM_Parse

Parse a token out of a string
Will never return NULL, just empty strings

If "allowLineBreaks" is qtrue then an empty
string will be returned if the next token is
a newline.
==============
*/
const char *SkipWhitespace(const char *data, qboolean *hasNewLines) {
	int c;

	while ((c = *data) <= ' ') {
		if (!c) {
			return NULL;
		}
		if (c == '\n') {
			com_lines++;
			*hasNewLines = qtrue;
		}
		data++;
	}

	return data;
}

int COM_Compress(char *data_p) {
	char *in, *out;
	int c;
	qboolean newline = qfalse, whitespace = qfalse;

	in = out = data_p;
	if (in) {
		while ((c = *in) != 0) {
			// skip double slash comments
			if (c == '/' && in[1] == '/') {
				while (*in && *in != '\n') {
					in++;
				}
				// skip /* */ comments
			} else if (c == '/' && in[1] == '*') {
				while (*in && (*in != '*' || in[1] != '/'))
					in++;
				if (*in)
					in += 2;
				// record when we hit a newline
			} else if (c == '\n' || c == '\r') {
				newline = qtrue;
				in++;
				// record when we hit whitespace
			} else if (c == ' ' || c == '\t') {
				whitespace = qtrue;
				in++;
				// an actual token
			} else {
				// if we have a pending newline, emit it (and it counts as whitespace)
				if (newline) {
					*out++ = '\n';
					newline = qfalse;
					whitespace = qfalse;
				} if (whitespace) {
					*out++ = ' ';
					whitespace = qfalse;
				}

				// copy quoted strings unmolested
				if (c == '"') {
					*out++ = c;
					in++;
					while (1) {
						c = *in;
						if (c && c != '"') {
							*out++ = c;
							in++;
						} else {
							break;
						}
					}
					if (c == '"') {
						*out++ = c;
						in++;
					}
				} else {
					*out = c;
					out++;
					in++;
				}
			}
		}

		*out = 0;
	}
	return out - data_p;
}

char *COM_ParseExt(const char **data_p, qboolean allowLineBreaks) {
	int c = 0, len;
	qboolean hasNewLines = qfalse;
	const char *data;

	data = *data_p;
	len = 0;
	com_token[0] = 0;

	// make sure incoming data is valid
	if (!data) {
		*data_p = NULL;
		return com_token;
	}

	while (1) {
		// skip whitespace
		data = SkipWhitespace(data, &hasNewLines);
		if (!data) {
			*data_p = NULL;
			return com_token;
		}
		if (hasNewLines && !allowLineBreaks) {
			*data_p = data;
			return com_token;
		}

		c = *data;

		// skip double slash comments
		if (c == '/' && data[1] == '/') {
			data += 2;
			while (*data && *data != '\n') {
				data++;
			}
		}
		// skip /* */ comments
		else if (c == '/' && data[1] == '*') {
			data += 2;
			while (*data && (*data != '*' || data[1] != '/')) {
				data++;
			}
			if (*data) {
				data += 2;
			}
		} else {
			break;
		}
	}

	// handle quoted strings
	if (c == '\"') {
		data++;
		while (1) {
			c = *data++;
			if (c == '\"' || !c) {
				com_token[len] = 0;
				*data_p = (const char *)data;
				return com_token;
			}
			if (len < MAX_TOKEN_CHARS) {
				com_token[len] = c;
				len++;
			}
		}
	}

	// parse a regular word
	do {
		if (len < MAX_TOKEN_CHARS) {
			com_token[len] = c;
			len++;
		}
		data++;
		c = *data;
		if (c == '\n')
			com_lines++;
	} while (c>32);

	if (len == MAX_TOKEN_CHARS) {
		//		Com_Printf ("Token exceeded %i chars, discarded.\n", MAX_TOKEN_CHARS);
		len = 0;
	}
	com_token[len] = 0;

	*data_p = (const char *)data;
	return com_token;
}


#if 0
// no longer used
/*
===============
COM_ParseInfos
===============
*/
int COM_ParseInfos(const char *buf, int max, char infos[][MAX_INFO_STRING]) {
	const char	*token;
	int		count;
	char	key[MAX_TOKEN_CHARS];

	count = 0;

	while (1) {
		token = COM_Parse(&buf);
		if (!token[0]) {
			break;
		}
		if (strcmp(token, "{")) {
			Com_Printf("Missing { in info file\n");
			break;
		}

		if (count == max) {
			Com_Printf("Max infos exceeded\n");
			break;
		}

		infos[count][0] = 0;
		while (1) {
			token = COM_ParseExt(&buf, qtrue);
			if (!token[0]) {
				Com_Printf("Unexpected end of info file\n");
				break;
			}
			if (!strcmp(token, "}")) {
				break;
			}
			Q_strncpyz(key, token, sizeof(key));

			token = COM_ParseExt(&buf, qfalse);
			if (!token[0]) {
				strcpy(token, "<NULL>");
			}
			Info_SetValueForKey(infos[count], key, token);
		}
		count++;
	}

	return count;
}
#endif

/*
===============
COM_ParseString
===============
*/
qboolean COM_ParseString(const char **data, const char **s) {
	//	*s = COM_ParseExt( data, qtrue );
	*s = COM_ParseExt(data, qfalse);
	if (s[0] == 0) {
		Com_Printf("unexpected EOF\n");
		return qtrue;
	}
	return qfalse;
}

/*
===============
COM_ParseInt
===============
*/
qboolean COM_ParseInt(const char **data, int *i) {
	const char	*token;

	token = COM_ParseExt(data, qfalse);
	if (token[0] == 0) {
		Com_Printf("unexpected EOF\n");
		return qtrue;
	}

	*i = atoi(token);
	return qfalse;
}

/*
===============
COM_ParseFloat
===============
*/
qboolean COM_ParseFloat(const char **data, float *f) {
	const char	*token;

	token = COM_ParseExt(data, qfalse);
	if (token[0] == 0) {
		Com_Printf("unexpected EOF\n");
		return qtrue;
	}

	*f = atof(token);
	return qfalse;
}

/*
===============
COM_ParseVec4
===============
*/
qboolean COM_ParseVec4(const char **buffer, vec4_t *c) {
	int i;
	float f;

	for (i = 0; i < 4; i++) {
		if (COM_ParseFloat(buffer, &f)) {
			return qtrue;
		}
		(*c)[i] = f;
	}
	return qfalse;
}

/*
==================
COM_MatchToken
==================
*/
void COM_MatchToken(const char **buf_p, char *match) {
	const char	*token;

	token = COM_Parse(buf_p);
	if (strcmp(token, match)) {
		Com_Error(ERR_DROP, "MatchToken: %s != %s", token, match);
	}
}


/*
=================
SkipBracedSection

The next token should be an open brace.
Skips until a matching close brace is found.
Internal brace depths are properly skipped.
=================
*/
void SkipBracedSection(const char **program) {
	char			*token;
	int				depth;

	depth = 0;
	do {
		token = COM_ParseExt(program, qtrue);
		if (token[1] == 0) {
			if (token[0] == '{') {
				depth++;
			} else if (token[0] == '}') {
				depth--;
			}
		}
	} while (depth && *program);
}

/*
=================
SkipRestOfLine
=================
*/
void SkipRestOfLine(const char **data) {
	const char	*p;
	int		c;

	p = *data;
	while ((c = *p++) != 0) {
		if (c == '\n') {
			com_lines++;
			break;
		}
	}

	*data = p;
}


void Parse1DMatrix(const char **buf_p, int x, float *m) {
	const char	*token;
	int			i;

	COM_MatchToken(buf_p, "(");

	for (i = 0; i < x; i++) {
		token = COM_Parse(buf_p);
		m[i] = atof(token);
	}

	COM_MatchToken(buf_p, ")");
}

void Parse2DMatrix(const char **buf_p, int y, int x, float *m) {
	int		i;

	COM_MatchToken(buf_p, "(");

	for (i = 0; i < y; i++) {
		Parse1DMatrix(buf_p, x, m + i * x);
	}

	COM_MatchToken(buf_p, ")");
}

void Parse3DMatrix(const char **buf_p, int z, int y, int x, float *m) {
	int		i;

	COM_MatchToken(buf_p, "(");

	for (i = 0; i < z; i++) {
		Parse2DMatrix(buf_p, y, x, m + i * x*y);
	}

	COM_MatchToken(buf_p, ")");
}


/*
============================================================================

LIBRARY REPLACEMENT FUNCTIONS

============================================================================
*/

int Q_isprint(int c) {
	if (c >= 0x20 && c <= 0x7E)
		return (1);
	return (0);
}

int Q_islower(int c) {
	if (c >= 'a' && c <= 'z')
		return (1);
	return (0);
}

int Q_isupper(int c) {
	if (c >= 'A' && c <= 'Z')
		return (1);
	return (0);
}

int Q_isalpha(int c) {
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
		return (1);
	return (0);
}

int Q_isdigit(int c) {
	return (c >= '0' && c <= '9');
}

int Q_isalnum(int c) {
	return Q_isdigit(c) | Q_isalpha(c);
}

char* Q_strrchr(const char* string, int c) {
	char cc = c;
	const char *s;
	const char *sp = NULL;

	s = (const char *)string;

	while (*s) {
		if (*s == cc)
			sp = s;
		s++;
	}
	if (cc == 0)
		sp = s;

	return (char *)sp;
}

/*
=============
Q_strncpyz

Safe strncpy that ensures a trailing zero
=============
*/
void Q_strncpyz(char *dest, const char *src, size_t destsize) {
	strncpy(dest, src, destsize - 1);
	dest[destsize - 1] = 0;
}

int Q_stricmpn(const char *s1, const char *s2, int n) {
	int		c1, c2;

	// bk001129 - moved in 1.17 fix not in id codebase
	if (s1 == NULL) {
		if (s2 == NULL)
			return 0;
		else
			return -1;
	} else if (s2 == NULL)
		return 1;



	do {
		c1 = *s1++;
		c2 = *s2++;

		if (!n--) {
			return 0;		// strings are equal until end point
		}

		if (c1 != c2) {
			if (c1 >= 'a' && c1 <= 'z') {
				c1 -= ('a' - 'A');
			}
			if (c2 >= 'a' && c2 <= 'z') {
				c2 -= ('a' - 'A');
			}
			if (c1 != c2) {
				return c1 < c2 ? -1 : 1;
			}
		}
	} while (c1);

	return 0;		// strings are equal
}

int Q_strncmp(const char *s1, const char *s2, int n) {
	int		c1, c2;

	do {
		c1 = *s1++;
		c2 = *s2++;

		if (!n--) {
			return 0;		// strings are equal until end point
		}

		if (c1 != c2) {
			return c1 < c2 ? -1 : 1;
		}
	} while (c1);

	return 0;		// strings are equal
}

int Q_stricmp(const char *s1, const char *s2) {
	return (s1 && s2) ? Q_stricmpn(s1, s2, 99999) : -1;
}

char *Q_stristr(const char *str, char *charset) {
	int i;

	while (*str) {
		for (i = 0; charset[i] && str[i]; i++) {
			if (toupper(charset[i]) != toupper(str[i])) break;
		}
		if (!charset[i]) {
			return (char *) str;
		}
		str++;
	}
	return NULL;
}

char *Q_strlwr(char *s1) {
	char	*s;

	s = s1;
	while (*s) {
		*s = tolower(*s);
		s++;
	}
	return s1;
}

char *Q_strupr(char *s1) {
	char	*s;

	s = s1;
	while (*s) {
		*s = toupper(*s);
		s++;
	}
	return s1;
}


// never goes past bounds or leaves without a terminating 0
void Q_strcat(char *dest, size_t size, const char *src) {
	size_t		l1;

	l1 = strlen(dest);
	Q_strncpyz(dest + l1, src, size - l1);
}


int Q_PrintStrlen(const char *string, qboolean use102color) {
	int			len;
	const char	*p;

	if (!string) {
		return 0;
	}

	len = 0;
	p = string;
	while (*p) {
		if (Q_IsColorString(p) || (use102color && Q_IsColorString_1_02(p))) {
			p += 2;
			continue;
		}
		p++;
		len++;
	}

	return len;
}

int Q_PrintStrCharsTo(const char *str, int pos, char *color, qboolean use102color) {
	int			advance = 0;
	char		lastColor = 0;
	int			i;

	for (i = 0; advance < pos && str[i]; i++) {
		if (Q_IsColorString(&str[i]) || (use102color && Q_IsColorString_1_02(&str[i]))) {
			i++;
			lastColor = str[i];
		} else {
			advance++;
		}
	}

	if (color) {
		*color = lastColor;
	}

	return i;
}


int Q_PrintStrLenTo(const char *str, int chars, char *color, qboolean use102color) {
	int		offset = 0;
	char	lastColor = 0;
	int		i;

	for (i = 0; i < chars && str[i]; i++) {
		if (Q_IsColorString(&str[i]) || (use102color && Q_IsColorString_1_02(&str[i]))) {
			i++;
			lastColor = str[i];
		} else {
			offset++;
		}
	}

	if (color) {
		*color = lastColor;
	}

	return offset;
}

// copy a substring of len printable characters from 'from' offset, saving initial color
void Q_PrintStrCopy(char *dst, const char *src, int dstSize, int from, int len, qboolean use102color) {
	int		fromOffset = Q_PrintStrLenTo(src, from, NULL, use102color);
	int		to;
	char	color;

	from = Q_PrintStrCharsTo(src, fromOffset, &color, use102color);
	to = Q_PrintStrCharsTo(src, fromOffset + len, NULL, use102color);

	assert(dstSize >= 3);

	if (color) {
		*dst++ = Q_COLOR_ESCAPE;
		*dst++ = color;
		dstSize -= 2;
	}

	Q_strncpyz(dst, src + from, MIN(dstSize, to - from + 1));
}

char *Q_CleanStr(char *string, qboolean use102color) {
	char*	d;
	char*	s;
	int		c;

	s = string;
	d = string;
	while ((c = *s) != 0) {
		if (Q_IsColorString(s) || (use102color && Q_IsColorString_1_02(s))) {
			s++;
		} else if (c >= 0x20 && c <= 0x7E) {
			*d++ = c;
		}
		s++;
	}
	*d = '\0';

	return string;
}


#if defined(_MSC_VER) && _MSC_VER < 1900
/*
=============
Q_vsnprintf

Special wrapper function for Microsoft's broken _vsnprintf() function.
=============
*/

size_t Q_vsnprintf(char *str, size_t size, const char *format, va_list ap) {
	int retval;
	retval = _vsnprintf(str, size, format, ap);
	if (retval < 0 || retval == size) {
		// Microsoft doesn't adhere to the C99 standard of vsnprintf,
		// which states that the return value must be the number of
		// bytes written if the output string had sufficient length.
		//
		// Obviously we cannot determine that value from Microsoft's
		// implementation, so we have no choice but to return size.
		str[size - 1] = '\0';
		return size;
	}
	return retval;
}
#endif

void QDECL Com_sprintf(char *dest, size_t size, const char *fmt, ...) {
	size_t		len;
	va_list		argptr;

	va_start(argptr, fmt);
	len = Q_vsnprintf(dest, size, fmt, argptr);
	va_end(argptr);

	if (len >= size) {
		Com_Printf("Com_sprintf: overflow of %zu in %zu\n", len, size);
	}
}


/*
============
va

does a varargs printf into a temp buffer, so I don't need to have
varargs versions of all text functions.
FIXME: make this buffer size safe someday
============
*/
#define	MAX_VA_STRING	32000
#define MAX_VA_BUFFERS 4

char * QDECL va(const char *format, ...) {
	va_list		argptr;
	static char	string[MAX_VA_BUFFERS][MAX_VA_STRING];	// in case va is called by nested functions
	static int	index = 0;
	char		*buf;

	va_start(argptr, format);
	buf = (char *)&string[index++ & 3];
	Q_vsnprintf(buf, MAX_VA_STRING, format, argptr);
	va_end(argptr);

	return buf;
}


/*
=====================================================================

INFO STRINGS

=====================================================================
*/

/*
===============
Info_ValueForKey

Searches the string for the given
key and returns the associated value, or an empty string.
FIXME: overflow check?
===============
*/
char *Info_ValueForKey(const char *s, const char *key) {
	char	pkey[BIG_INFO_KEY];
	static	char value[2][BIG_INFO_VALUE];	// use two buffers so compares
											// work without stomping on each other
	static	int	valueindex = 0;
	char	*o;

	if (!s || !key) {
		return "";
	}

	if (strlen(s) >= BIG_INFO_STRING) {
		Com_Error(ERR_DROP, "Info_ValueForKey: oversize infostring");
	}

	valueindex ^= 1;
	if (*s == '\\')
		s++;
	while (1) {
		o = pkey;
		while (*s != '\\') {
			if (!*s)
				return "";
			*o++ = *s++;
		}
		*o = 0;
		s++;

		o = value[valueindex];

		while (*s != '\\' && *s) {
			*o++ = *s++;
		}
		*o = 0;

		if (!Q_stricmp(key, pkey))
			return value[valueindex];

		if (!*s)
			break;
		s++;
	}

	return "";
}


/*
===================
Info_NextPair

Used to itterate through all the key/value pairs in an info string
===================
*/
void Info_NextPair(const char **head, char *key, char *value) {
	char	*o;
	const char	*s;

	s = *head;

	if (*s == '\\') {
		s++;
	}
	key[0] = 0;
	value[0] = 0;

	o = key;
	while (*s != '\\') {
		if (!*s) {
			*o = 0;
			*head = s;
			return;
		}
		*o++ = *s++;
	}
	*o = 0;
	s++;

	o = value;
	while (*s != '\\' && *s) {
		*o++ = *s++;
	}
	*o = 0;

	*head = s;
}


/*
===================
Info_RemoveKey
===================
*/
void Info_RemoveKey(char *s, const char *key) {
	char	*start;
	char	pkey[MAX_INFO_KEY];
	char	value[MAX_INFO_VALUE];
	char	*o;

	if (strlen(s) >= MAX_INFO_STRING) {
		Com_Error(ERR_DROP, "Info_RemoveKey: oversize infostring");
	}

	if (strchr(key, '\\')) {
		return;
	}

	while (1) {
		start = s;
		if (*s == '\\')
			s++;
		o = pkey;
		while (*s != '\\') {
			if (!*s)
				return;
			*o++ = *s++;
		}
		*o = 0;
		s++;

		o = value;
		while (*s != '\\' && *s) {
			if (!*s)
				return;
			*o++ = *s++;
		}
		*o = 0;

		if (!strcmp(key, pkey)) {
			memmove(start, s, strlen(s) + 1); // remove this part
			return;
		}

		if (!*s)
			return;
	}

}

/*
===================
Info_RemoveKey_Big
===================
*/
void Info_RemoveKey_Big(char *s, const char *key) {
	char	*start;
	char	pkey[BIG_INFO_KEY];
	char	value[BIG_INFO_VALUE];
	char	*o;

	if (strlen(s) >= BIG_INFO_STRING) {
		Com_Error(ERR_DROP, "Info_RemoveKey_Big: oversize infostring");
	}

	if (strchr(key, '\\')) {
		return;
	}

	while (1) {
		start = s;
		if (*s == '\\')
			s++;
		o = pkey;
		while (*s != '\\') {
			if (!*s)
				return;
			*o++ = *s++;
		}
		*o = 0;
		s++;

		o = value;
		while (*s != '\\' && *s) {
			if (!*s)
				return;
			*o++ = *s++;
		}
		*o = 0;

		if (!strcmp(key, pkey)) {
			memmove(start, s, strlen(s) + 1); // remove this part
			return;
		}

		if (!*s)
			return;
	}

}




/*
==================
Info_Validate

Some characters are illegal in info strings because they
can mess up the server's parsing
==================
*/
qboolean Info_Validate(const char *s) {
	if (strchr(s, '\"')) {
		return qfalse;
	}
	if (strchr(s, ';')) {
		return qfalse;
	}
	return qtrue;
}

/*
==================
Info_SetValueForKey

Changes or adds a key/value pair
==================
*/
qboolean Info_SetValueForKey(char *s, const char *key, const char *value) {
	char	newi[MAX_INFO_STRING];

	if (strlen(s) >= MAX_INFO_STRING) {
		Com_Error(ERR_DROP, "Info_SetValueForKey: oversize infostring");
	}

	if (strchr(key, '\\') || strchr(value, '\\')) {
		Com_Printf("Can't use keys or values with a \\\n");
		return qfalse;
	}

	if (strchr(key, ';') || strchr(value, ';')) {
		Com_Printf("Can't use keys or values with a semicolon\n");
		return qfalse;
	}

	if (strchr(key, '\"') || strchr(value, '\"')) {
		Com_Printf("Can't use keys or values with a \"\n");
		return qfalse;
	}

	Info_RemoveKey(s, key);

	if (!strlen(value))
		return qfalse;

	Com_sprintf(newi, sizeof(newi), "\\%s\\%s", key, value);

	// q3infoboom exploit
	if (strlen(newi) + strlen(s) >= MAX_INFO_STRING) {
		Com_Printf("Info string length exceeded\n");
		return qfalse;
	}

	strcat(newi, s);
	strcpy(s, newi);
	return qtrue;
}

/*
==================
Info_SetValueForKey_Big

Changes or adds a key/value pair
==================
*/
void Info_SetValueForKey_Big(char *s, const char *key, const char *value) {
	char	newi[BIG_INFO_STRING];

	if (strlen(s) >= BIG_INFO_STRING) {
		Com_Error(ERR_DROP, "Info_SetValueForKey: oversize infostring");
	}

	if (strchr(key, '\\') || strchr(value, '\\')) {
		Com_Printf("Can't use keys or values with a \\\n");
		return;
	}

	if (strchr(key, ';') || strchr(value, ';')) {
		Com_Printf("Can't use keys or values with a semicolon\n");
		return;
	}

	if (strchr(key, '\"') || strchr(value, '\"')) {
		Com_Printf("Can't use keys or values with a \"\n");
		return;
	}

	Info_RemoveKey_Big(s, key);

	if (!strlen(value))
		return;

	Com_sprintf(newi, sizeof(newi), "\\%s\\%s", key, value);

	// q3infoboom exploit
	if (strlen(newi) + strlen(s) >= BIG_INFO_STRING) {
		Com_Printf("BIG Info string length exceeded\n");
		return;
	}

	strcat(s, newi);
}


//rww - convience function..
int Q_irand(int value1, int value2) {
	int r;

	r = rand() % value2;
	r += value1;

	return r;
}

//====================================================================


// for auto-complete (copied from OpenJK)

/*
============
Com_TruncateLongString

Assumes buffer is atleast TRUNCATE_LENGTH big
============
*/
void Com_TruncateLongString(char *buffer, const char *s) {
	int length = (int)strlen(s);

	if (length <= TRUNCATE_LENGTH)
		Q_strncpyz(buffer, s, TRUNCATE_LENGTH);
	else {
		Q_strncpyz(buffer, s, (TRUNCATE_LENGTH / 2) - 3);
		Q_strcat(buffer, TRUNCATE_LENGTH, " ... ");
		Q_strcat(buffer, TRUNCATE_LENGTH, s + length - (TRUNCATE_LENGTH / 2) + 3);
	}
}

/*
==================
Com_SkipTokens
==================
*/
char *Com_SkipTokens(char *s, int numTokens, char *sep) {
	int sepCount = 0;
	char *p = s;

	while (sepCount < numTokens) {
		if (strchr(sep, *p++)) {
			sepCount++;
			while (strchr(sep, *p))
				p++;
		} else if (*p == '\0')
			break;
	}

	if (sepCount == numTokens)
		return p;
	else
		return s;
}
