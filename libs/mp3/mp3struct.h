// Filename:	mp3struct.h
//
// MP3STREAM struct for streaming MP3 decode using minimp3.
//

#ifndef MP3STRUCT_H
#define MP3STRUCT_H

#include "minimp3.h"

#ifndef byte
typedef unsigned char byte;
#endif

typedef struct
{
	// minimp3 decoder state
	mp3dec_t		dec;

	// streaming state
	byte		*pbSourceData;			// pointer to raw MP3 data
	int			iSourceBytesRemaining;
	int			iSourceReadIndex;
	int			iSourceFrameBytes;		// size of first frame (for reference)
#ifdef _DEBUG
	int			iSourceFrameCounter;
#endif
	int			iBytesDecodedTotal;
	int			iBytesDecodedThisPacket;

	// rewind state
	int			iRewind_SourceBytesRemaining;
	int			iRewind_SourceReadIndex;

	// decode output buffer (max 1152 samples * 2 channels * 2 bytes = 4608)
	byte		bDecodeBuffer[2304*2];
	int			iCopyOffset;

	// stream properties
	int			iChannels;				// 1=mono, 2=stereo
	int			iSampleRate;			// native sample rate from MP3 header

	// time query fields for music
	int			iTimeQuery_UnpackedLength;
	int			iTimeQuery_SampleRate;
	int			iTimeQuery_Channels;
	int			iTimeQuery_Width;

} MP3STREAM, *LP_MP3STREAM;

#endif	// #ifndef MP3STRUCT_H

////////////////// eof /////////////////////

