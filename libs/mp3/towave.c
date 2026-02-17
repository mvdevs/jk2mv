/*
 * towave.c - MP3 decoder C interface using minimp3
 *
 * Replaces the old Xing/FreeAmp decoder with minimp3 (public domain, single-header).
 * Implements the same C_MP3_* API surface expected by snd_mp3.cpp.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mp3struct.h"		// includes minimp3.h, defines MP3STREAM


#ifndef byte
typedef unsigned char byte;
#endif


typedef struct id3v1_1 {
	char id[3];
	char title[30];
	char artist[30];
	char album[30];
	char year[4];
	char comment[28];
	char zero;
	char track;
	char genre;
} id3v1_1;


static id3v1_1 *gpTAG;
#define BYTESREMAINING_ACCOUNT_FOR_REAR_TAG(_pvData, _iBytesRemaining)			\
	gpTAG = (id3v1_1*) (((byte *)_pvData + _iBytesRemaining)-sizeof(id3v1_1));	\
	if (!strncmp(gpTAG->id, "TAG", 3))											\
	{																			\
		_iBytesRemaining -= sizeof(id3v1_1);									\
	}


// Helper: scan to the first valid MP3 sync frame and read its info.
// Returns number of frame_bytes consumed by first frame, 0 on failure.
// Sets *pOffset to byte offset of first frame in data.
static int minimp3_find_first_frame(const byte *data, int dataLen, mp3dec_frame_info_t *info, int *pOffset)
{
	mp3dec_t dec;
	short pcmBuf[MINIMP3_MAX_SAMPLES_PER_FRAME];

	mp3dec_init(&dec);
	int samples = mp3dec_decode_frame(&dec, data, dataLen, pcmBuf, info);
	// minimp3 may set frame_bytes = dataLen even when no frame was decoded
	// (it consumed the entire buffer searching for sync). Check samples > 0
	// to ensure a frame was actually decoded successfully.
	if (samples <= 0 || info->frame_bytes <= 0)
	{
		return 0;
	}
	if (pOffset)
	{
		*pOffset = info->frame_offset;
	}
	return info->frame_bytes;
}


// char *return is non-NULL for any errors
//
char *C_MP3_IsValid(void *pvData, int iDataLen, int bStereoDesired)
{
	mp3dec_frame_info_t info;
	int remaining = iDataLen;

	BYTESREMAINING_ACCOUNT_FOR_REAR_TAG(pvData, remaining)

	int frameBytes = minimp3_find_first_frame((const byte*)pvData, remaining, &info, NULL);

	if (frameBytes == 0)
	{
		return "MP3ERR: Bad or unsupported file!";
	}

	if (bStereoDesired && info.channels != 2)
	{
		return "MP3ERR: Source file is not stereo!";
	}

	return NULL;
}


// char *return is non-NULL for any errors
//
char *C_MP3_GetHeaderData(void *pvData, int iDataLen, int *piRate, int *piWidth, int *piChannels, int bStereoDesired)
{
	mp3dec_frame_info_t info;
	int remaining = iDataLen;

	// Strip trailing ID3v1 tag so it doesn't confuse minimp3's frame matching
	BYTESREMAINING_ACCOUNT_FOR_REAR_TAG(pvData, remaining)

	int frameBytes = minimp3_find_first_frame((const byte*)pvData, remaining, &info, NULL);

	if (frameBytes == 0)
	{
		return "MP3ERR: Bad or unsupported file!";
	}

	*piRate     = info.hz;
	*piWidth    = 2;	// always 16-bit output
	*piChannels = bStereoDesired ? info.channels : 1;

	return NULL;
}


// Get total unpacked PCM size by decoding frame headers (fast scan).
//
char *C_MP3_GetUnpackedSize(void *pvData, int iSourceBytesRemaining, int *piUnpackedSize, int bStereoDesired)
{
	mp3dec_t dec;
	mp3dec_frame_info_t info;
	short pcmBuf[MINIMP3_MAX_SAMPLES_PER_FRAME];

	const byte *pData = (const byte *)pvData;
	int remaining = iSourceBytesRemaining;
	int totalBytes = 0;

	BYTESREMAINING_ACCOUNT_FOR_REAR_TAG(pvData, remaining)

	mp3dec_init(&dec);

	while (remaining > 0)
	{
		int samples = mp3dec_decode_frame(&dec, pData, remaining, pcmBuf, &info);
		if (info.frame_bytes <= 0)
			break;

		if (samples > 0)
		{
			int outChannels = bStereoDesired ? info.channels : 1;

			if (!bStereoDesired && info.channels == 2)
			{
				// Mono downmix: samples is per-channel, output will be in mono (done during full unpack)
				totalBytes += samples * 2;	// 16bit mono
			}
			else
			{
				totalBytes += samples * outChannels * 2;	// 16bit
			}
		}

		pData     += info.frame_bytes;
		remaining -= info.frame_bytes;
	}

	if (totalBytes == 0)
	{
		*piUnpackedSize = 0;
		return "MP3ERR: Bad or Unsupported MP3 file!";
	}

	*piUnpackedSize = totalBytes;
	return NULL;
}


// Fully decode MP3 to raw PCM.
//
char *C_MP3_UnpackRawPCM(void *pvData, int iSourceBytesRemaining, int *piUnpackedSize, void *pbUnpackBuffer, int bStereoDesired)
{
	mp3dec_t dec;
	mp3dec_frame_info_t info;
	short pcmBuf[MINIMP3_MAX_SAMPLES_PER_FRAME];

	const byte *pData = (const byte *)pvData;
	int remaining = iSourceBytesRemaining;
	int destOffset = 0;
	short *pDest = (short *)pbUnpackBuffer;

	BYTESREMAINING_ACCOUNT_FOR_REAR_TAG(pvData, remaining)

	mp3dec_init(&dec);

	while (remaining > 0)
	{
		int samples = mp3dec_decode_frame(&dec, pData, remaining, pcmBuf, &info);
		if (info.frame_bytes <= 0)
			break;

		if (samples > 0)
		{
			if (!bStereoDesired && info.channels == 2)
			{
				// Downmix stereo to mono
				int i;
				for (i = 0; i < samples; i++)
				{
					int mixed = ((int)pcmBuf[i*2] + (int)pcmBuf[i*2+1]) / 2;
					pDest[destOffset / 2] = (short)mixed;
					destOffset += 2;
				}
			}
			else
			{
				int outBytes = samples * info.channels * 2;
				memcpy((byte *)pbUnpackBuffer + destOffset, pcmBuf, outBytes);
				destOffset += outBytes;
			}
		}

		pData     += info.frame_bytes;
		remaining -= info.frame_bytes;
	}

	if (destOffset == 0)
	{
		*piUnpackedSize = 0;
		return "MP3ERR: Bad or Unsupported MP3 file!";
	}

	*piUnpackedSize = destOffset;
	return NULL;
}


// Initialize streaming decoder.
//
char *C_MP3Stream_DecodeInit(LP_MP3STREAM pSFX_MP3Stream, void *pvSourceData, int iSourceBytesRemaining,
							 int iGameAudioSampleRate, int iGameAudioSampleBits, int bStereoDesired)
{
	mp3dec_frame_info_t info;
	int firstFrameOffset = 0;

	memset(pSFX_MP3Stream, 0, sizeof(*pSFX_MP3Stream));

	// Initialize minimp3 decoder
	mp3dec_init(&pSFX_MP3Stream->dec);

	pSFX_MP3Stream->pbSourceData = (byte *)pvSourceData;
	pSFX_MP3Stream->iSourceBytesRemaining = iSourceBytesRemaining;
	pSFX_MP3Stream->iChannels = bStereoDesired ? 2 : 1;

	// Find first frame to get frame size and sample rate
	int frameBytes = minimp3_find_first_frame((const byte*)pvSourceData, iSourceBytesRemaining, &info, &firstFrameOffset);
	if (frameBytes == 0)
	{
		return "MP3ERR: Bad or unsupported file!";
	}

	pSFX_MP3Stream->iSourceFrameBytes = frameBytes;
	pSFX_MP3Stream->iSourceReadIndex = firstFrameOffset;
	pSFX_MP3Stream->iSampleRate = info.hz;

	// Account for trailing ID3v1 tag (but not for stereo/music streaming where data isn't fully loaded)
	if (!bStereoDesired)
	{
		BYTESREMAINING_ACCOUNT_FOR_REAR_TAG(pvSourceData, pSFX_MP3Stream->iSourceBytesRemaining)
		pSFX_MP3Stream->iSourceBytesRemaining -= pSFX_MP3Stream->iSourceReadIndex;
	}

	// Backup rewind state
	pSFX_MP3Stream->iRewind_SourceReadIndex      = pSFX_MP3Stream->iSourceReadIndex;
	pSFX_MP3Stream->iRewind_SourceBytesRemaining = pSFX_MP3Stream->iSourceBytesRemaining;

	// Re-init decoder fresh (the first decode above consumed one frame)
	mp3dec_init(&pSFX_MP3Stream->dec);

	return NULL;
}


// Decode one MP3 frame. Returns decoded byte count, 0 = finished.
//
unsigned int C_MP3Stream_Decode(LP_MP3STREAM pSFX_MP3Stream)
{
	mp3dec_frame_info_t info;
	short pcmBuf[MINIMP3_MAX_SAMPLES_PER_FRAME];

	if (pSFX_MP3Stream->iSourceBytesRemaining <= 0)
	{
		return 0;
	}

	const byte *pSrc = pSFX_MP3Stream->pbSourceData + pSFX_MP3Stream->iSourceReadIndex;
	int remaining = pSFX_MP3Stream->iSourceBytesRemaining;

	int samples = mp3dec_decode_frame(&pSFX_MP3Stream->dec, pSrc, remaining, pcmBuf, &info);

	if (info.frame_bytes <= 0)
	{
		return 0;
	}

	pSFX_MP3Stream->iSourceReadIndex      += info.frame_bytes;
	pSFX_MP3Stream->iSourceBytesRemaining -= info.frame_bytes;

#ifdef _DEBUG
	pSFX_MP3Stream->iSourceFrameCounter++;
#endif

	unsigned int outBytes = 0;

	if (samples > 0)
	{
		if (pSFX_MP3Stream->iChannels == 1 && info.channels == 2)
		{
			// Downmix stereo to mono
			int i;
			short *pOut = (short *)pSFX_MP3Stream->bDecodeBuffer;
			for (i = 0; i < samples; i++)
			{
				int mixed = ((int)pcmBuf[i*2] + (int)pcmBuf[i*2+1]) / 2;
				pOut[i] = (short)mixed;
			}
			outBytes = samples * 2;
		}
		else
		{
			outBytes = samples * info.channels * 2;
			memcpy(pSFX_MP3Stream->bDecodeBuffer, pcmBuf, outBytes);
		}
	}

	pSFX_MP3Stream->iBytesDecodedTotal      += outBytes;
	pSFX_MP3Stream->iBytesDecodedThisPacket  = outBytes;

	return outBytes;
}


// Rewind stream to beginning.
//
char *C_MP3Stream_Rewind(LP_MP3STREAM pSFX_MP3Stream)
{
	pSFX_MP3Stream->iSourceReadIndex      = pSFX_MP3Stream->iRewind_SourceReadIndex;
	pSFX_MP3Stream->iSourceBytesRemaining = pSFX_MP3Stream->iRewind_SourceBytesRemaining;
	pSFX_MP3Stream->iBytesDecodedTotal    = 0;

	// Re-init decoder state
	mp3dec_init(&pSFX_MP3Stream->dec);

	return NULL;
}

