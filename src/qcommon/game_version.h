// Copyright (C) 2000-2002 Raven Software, Inc.
//
// Current version of the multi player game

// ouned: define this for legacy JK2MF http downloads. Should be undefined as soon as jk2mf is dead,
// because it's kind of a bad implementation in jk2mf
#define MV_MFDOWNLOADS
#define NTCLIENT_WORKAROUND

#define JK2MV_VERSION "1.0b3"

#ifndef _DEBUG
#	define	Q3_VERSION		"JK2MV: v" JK2MV_VERSION
#else
#	define	Q3_VERSION		"JK2MV:"
#endif
