#include "client.h"
#include "snd_public.h"
#include "../qcommon/strip.h"
/*

key up events are sent even if in console mode

*/

int Key_GetProtocolKey(mvversion_t protocol, int key16);

field_t		chatField;
qboolean	chat_team;

int			chat_playerNum;

keyGlobals_t	kg;

extern console_t con;


// do NOT blithely change any of the key names (3rd field) here, since they have to match the key binds
//	in the CFG files, they're also prepended with "KEYNAME_" when looking up StripEd references
//
keyname_t keynames[MAX_KEYS] =
{
    { 0x00, 0x00, NULL, A_NULL, false									},
    { 0x01, 0x01, "SHIFT", A_SHIFT, false 								},
    { 0x02, 0x02, "CTRL", A_CTRL, false   								},
    { 0x03, 0x03, "ALT", A_ALT, false									},
    { 0x04, 0x04, "CAPSLOCK", A_CAPSLOCK, false							},
    { 0x05, 0x05, "KP_NUMLOCK", A_NUMLOCK, false						},
    { 0x06, 0x06, "SCROLLLOCK", A_SCROLLLOCK, false						},
    { 0x07, 0x07, "PAUSE", A_PAUSE, false								},
    { 0x08, 0x08, "BACKSPACE", A_BACKSPACE, false						},
    { 0x09, 0x09, "TAB", A_TAB, false									},
    { 0x0a, 0x0a, "ENTER", A_ENTER, false								},
    { 0x0b, 0x0b, "KP_PLUS", A_KP_PLUS, false							},
    { 0x0c, 0x0c, "KP_MINUS", A_KP_MINUS, false							},
    { 0x0d, 0x0d, "KP_ENTER", A_KP_ENTER, false							},
    { 0x0e, 0x0e, "KP_DEL", A_KP_PERIOD, false							},
    { 0x0f, 0x0f, NULL, A_PRINTSCREEN, false							},
    { 0x10, 0x10, "KP_INS", A_KP_0, false								},
    { 0x11, 0x11, "KP_END", A_KP_1, false								},
    { 0x12, 0x12, "KP_DOWNARROW", A_KP_2, false							},
    { 0x13, 0x13, "KP_PGDN", A_KP_3, false								},
    { 0x14, 0x14, "KP_LEFTARROW", A_KP_4, false							},
    { 0x15, 0x15, "KP_5", A_KP_5, false									},
    { 0x16, 0x16, "KP_RIGHTARROW", A_KP_6, false						},
    { 0x17, 0x17, "KP_HOME", A_KP_7, false								},
    { 0x18, 0x18, "KP_UPARROW", A_KP_8, false							},
    { 0x19, 0x19, "KP_PGUP", A_KP_9, false								},
    { 0x1a, 0x1a, "CONSOLE", A_CONSOLE, false 							},
    { 0x1b, 0x1b, "ESCAPE", A_ESCAPE, false								},
    { 0x1c, 0x1c, "F1", A_F1, true										},
    { 0x1d, 0x1d, "F2", A_F2, true										},
    { 0x1e, 0x1e, "F3", A_F3, true										},
    { 0x1f, 0x1f, "F4", A_F4, true										},

    { 0x20, 0x20, "SPACE", A_SPACE, false								},
    { (word)'!', (word)'!', NULL, A_PLING, false		  				},
    { (word)'"', (word)'"', NULL, A_DOUBLE_QUOTE, false  				},
    { (word)'#', (word)'#', NULL, A_HASH, false		  					},
    { (word)'$', (word)'$', NULL, A_STRING, false						},
    { (word)'%', (word)'%', NULL, A_PERCENT, false						},
    { (word)'&', (word)'&', NULL, A_AND, false							},
    { 0x27, 0x27, NULL, A_SINGLE_QUOTE, false							},
    { (word)'(', (word)'(', NULL, A_OPEN_BRACKET, false					},
    { (word)')', (word)')', NULL, A_CLOSE_BRACKET, false				},
    { (word)'*', (word)'*', NULL, A_STAR, false							},
    { (word)'+', (word)'+', NULL, A_PLUS, false							},
    { (word)',', (word)',', NULL, A_COMMA, false						},
    { (word)'-', (word)'-', NULL, A_MINUS, false						},
    { (word)'.', (word)'.', NULL, A_PERIOD, false						},
    { (word)'/', (word)'/', NULL, A_FORWARD_SLASH, false				},
    { (word)'0', (word)'0', NULL, A_0, false							},
    { (word)'1', (word)'1', NULL, A_1, false							},
    { (word)'2', (word)'2', NULL, A_2, false							},
    { (word)'3', (word)'3', NULL, A_3, false							},
    { (word)'4', (word)'4', NULL, A_4, false							},
    { (word)'5', (word)'5', NULL, A_5, false							},
    { (word)'6', (word)'6', NULL, A_6, false							},
    { (word)'7', (word)'7', NULL, A_7, false							},
    { (word)'8', (word)'8', NULL, A_8, false							},
    { (word)'9', (word)'9', NULL, A_9, false							},
    { (word)':', (word)':', NULL, A_COLON, false						},
    { (word)';', (word)';', "SEMICOLON", A_SEMICOLON, false				},
    { (word)'<', (word)'<', NULL, A_LESSTHAN, false						},
    { (word)'=', (word)'=', NULL, A_EQUALS, false						},
    { (word)'>', (word)'>', NULL, A_GREATERTHAN, false					},
    { (word)'?', (word)'?', NULL, A_QUESTION, false						},

    { (word)'@', (word)'@', NULL, A_AT, false							},
    { (word)'A', (word)'a', NULL, A_CAP_A, false						},
    { (word)'B', (word)'b', NULL, A_CAP_B, false						},
    { (word)'C', (word)'c', NULL, A_CAP_C, false						},
    { (word)'D', (word)'d', NULL, A_CAP_D, false						},
    { (word)'E', (word)'e', NULL, A_CAP_E, false						},
    { (word)'F', (word)'f', NULL, A_CAP_F, false						},
    { (word)'G', (word)'g', NULL, A_CAP_G, false						},
    { (word)'H', (word)'h', NULL, A_CAP_H, false						},
    { (word)'I', (word)'i', NULL, A_CAP_I, false						},
    { (word)'J', (word)'j', NULL, A_CAP_J, false						},
    { (word)'K', (word)'k', NULL, A_CAP_K, false						},
    { (word)'L', (word)'l', NULL, A_CAP_L, false						},
    { (word)'M', (word)'m', NULL, A_CAP_M, false						},
    { (word)'N', (word)'n', NULL, A_CAP_N, false						},
    { (word)'O', (word)'o', NULL, A_CAP_O, false						},
    { (word)'P', (word)'p', NULL, A_CAP_P, false						},
    { (word)'Q', (word)'q', NULL, A_CAP_Q, false						},
    { (word)'R', (word)'r', NULL, A_CAP_R, false						},
    { (word)'S', (word)'s', NULL, A_CAP_S, false						},
    { (word)'T', (word)'t', NULL, A_CAP_T, false						},
    { (word)'U', (word)'u', NULL, A_CAP_U, false						},
    { (word)'V', (word)'v', NULL, A_CAP_V, false						},
    { (word)'W', (word)'w', NULL, A_CAP_W, false						},
    { (word)'X', (word)'x', NULL, A_CAP_X, false						},
    { (word)'Y', (word)'y', NULL, A_CAP_Y, false						},
    { (word)'Z', (word)'z', NULL, A_CAP_Z, false						},
    { (word)'[', (word)'[', NULL, A_OPEN_SQUARE, false					},
    { 0x5c, 0x5c, NULL, A_BACKSLASH, false								},
    { (word)']', (word)']', NULL, A_CLOSE_SQUARE, false 				},
    { (word)'^', (word)'^', NULL, A_CARET, false		 				},
    { (word)'_', (word)'_', NULL, A_UNDERSCORE, false					},

    { 0x60, 0x60, NULL, A_LEFT_SINGLE_QUOTE, false						},
    { (word)'A', (word)'a', NULL, A_LOW_A, false						},
    { (word)'B', (word)'b', NULL, A_LOW_B, false						},
    { (word)'C', (word)'c', NULL, A_LOW_C, false						},
    { (word)'D', (word)'d', NULL, A_LOW_D, false						},
    { (word)'E', (word)'e', NULL, A_LOW_E, false						},
    { (word)'F', (word)'f', NULL, A_LOW_F, false						},
    { (word)'G', (word)'g', NULL, A_LOW_G, false						},
    { (word)'H', (word)'h', NULL, A_LOW_H, false						},
    { (word)'I', (word)'i', NULL, A_LOW_I, false						},
    { (word)'J', (word)'j', NULL, A_LOW_J, false						},
    { (word)'K', (word)'k', NULL, A_LOW_K, false						},
    { (word)'L', (word)'l', NULL, A_LOW_L, false						},
    { (word)'M', (word)'m', NULL, A_LOW_M, false						},
    { (word)'N', (word)'n', NULL, A_LOW_N, false						},
    { (word)'O', (word)'o', NULL, A_LOW_O, false						},
    { (word)'P', (word)'p', NULL, A_LOW_P, false						},
    { (word)'Q', (word)'q', NULL, A_LOW_Q, false						},
    { (word)'R', (word)'r', NULL, A_LOW_R, false						},
    { (word)'S', (word)'s', NULL, A_LOW_S, false						},
    { (word)'T', (word)'t', NULL, A_LOW_T, false						},
    { (word)'U', (word)'u', NULL, A_LOW_U, false						},
    { (word)'V', (word)'v', NULL, A_LOW_V, false						},
    { (word)'W', (word)'w', NULL, A_LOW_W, false						},
    { (word)'X', (word)'x', NULL, A_LOW_X, false						},
    { (word)'Y', (word)'y', NULL, A_LOW_Y, false						},
    { (word)'Z', (word)'z', NULL, A_LOW_Z, false						},
    { (word)'{', (word)'{', NULL, A_OPEN_BRACE, false					},
    { (word)'|', (word)'|', NULL, A_BAR, false							},
    { (word)'}', (word)'}', NULL, A_CLOSE_BRACE, false					},
    { (word)'~', (word)'~', NULL, A_TILDE, false						},
    { 0x7f, 0x7f, "DEL", A_DELETE, false								},

    { 0x80, 0x80, "EURO", A_EURO, false  								},
    { 0x81, 0x81, "SHIFT", A_SHIFT2, false								},
    { 0x82, 0x82, "CTRL", A_CTRL2, false								},
    { 0x83, 0x83, "ALTGR", A_ALT2, false								},
    { 0x84, 0x84, "F5", A_F5, true										},
    { 0x85, 0x85, "F6", A_F6, true										},
    { 0x86, 0x86, "F7", A_F7, true										},
    { 0x87, 0x87, "F8", A_F8, true										},
    { 0x88, 0x88, "CIRCUMFLEX", A_CIRCUMFLEX, false  					},
    { 0x89, 0x89, "MWHEELUP", A_MWHEELUP, false							},
    { 0x8a, 0x9a, NULL, A_CAP_SCARON, false								},	// ******
    { 0x8b, 0x8b, "MWHEELDOWN", A_MWHEELDOWN, false						},
    { 0x8c, 0x9c, NULL, A_CAP_OE, false									},	// ******
    { 0x8d, 0x8d, "MOUSE1", A_MOUSE1, false								},
    { 0x8e, 0x8e, "MOUSE2", A_MOUSE2, false								},
    { 0x8f, 0x8f, "INS", A_INSERT, false								},
    { 0x90, 0x90, "HOME", A_HOME, false									},
    { 0x91, 0x91, "PGUP", A_PAGE_UP, false								},
    { 0x92, 0x92, NULL, A_RIGHT_SINGLE_QUOTE, false						},
    { 0x93, 0x93, NULL, A_LEFT_DOUBLE_QUOTE, false						},
    { 0x94, 0x94, NULL, A_RIGHT_DOUBLE_QUOTE, false						},
    { 0x95, 0x95, "F9", A_F9, true										},
    { 0x96, 0x96, "F10", A_F10, true									},
    { 0x97, 0x97, "F11", A_F11, true									},
    { 0x98, 0x98, "F12", A_F12, true									},
    { 0x99, 0x99, NULL, A_TRADEMARK, false								},
    { 0x8a, 0x9a, NULL, A_LOW_SCARON, false								},	// ******
    { 0x9b, 0x9b, "SHIFT_ENTER", A_ENTER, false							},
    { 0x8c, 0x9c, NULL, A_LOW_OE, false									},	// ******
    { 0x9d, 0x9d, "END", A_END, false									},
    { 0x9e, 0x9e, "PGDN", A_PAGE_DOWN, false							},
    { 0x9f, 0xff, NULL, A_CAP_YDIERESIS, false							},	// ******

    { 0xa0, 0,	  "SHIFT_SPACE", A_SPACE, false							},
    { 0xa1, 0xa1, NULL, A_EXCLAMDOWN, false								},	// upside down '!' - undisplayable
    { L'\u00A2', L'\u00A2', NULL, A_CENT, false	  			}, // cent sign
    { L'\u00A3', L'\u00A3', NULL, A_POUND, false	  		}, // pound (as in currency) symbol
    { 0xa4, 0,    "SHIFT_KP_ENTER", A_KP_ENTER, false					},
    { L'\u00A5', L'\u00A5', NULL, A_YEN, false		  		}, // yen symbol
    { 0xa6, 0xa6, "MOUSE3", A_MOUSE3, false								},
    { 0xa7, 0xa7, "MOUSE4", A_MOUSE4, false								},
    { 0xa8, 0xa8, "MOUSE5", A_MOUSE5, false								},
    { L'\u00A9', L'\u00A9', NULL, A_COPYRIGHT, false 		}, // copyright symbol
    { 0xaa, 0xaa, "UPARROW", A_CURSOR_UP, false							},
    { 0xab, 0xab, "DOWNARROW", A_CURSOR_DOWN, false						},
    { 0xac, 0xac, "LEFTARROW", A_CURSOR_LEFT, false						},
    { 0xad, 0xad, "RIGHTARROW", A_CURSOR_RIGHT, false					},
    { L'\u00AE', L'\u00AE', NULL, A_REGISTERED, false		}, // registered trademark symbol
    { 0xaf, 0,	  NULL, A_UNDEFINED_7, false							},
    { 0xb0, 0,	  NULL, A_UNDEFINED_8, false							},
    { 0xb1, 0,	  NULL, A_UNDEFINED_9, false							},
    { 0xb2, 0,	  NULL, A_UNDEFINED_10, false							},
    { 0xb3, 0,	  NULL, A_UNDEFINED_11, false							},
    { 0xb4, 0,	  NULL, A_UNDEFINED_12, false							},
    { 0xb5, 0,	  NULL, A_UNDEFINED_13, false							},
    { 0xb6, 0,	  NULL, A_UNDEFINED_14, false							},
    { 0xb7, 0,	  NULL, A_UNDEFINED_15, false							},
    { 0xb8, 0,	  NULL, A_UNDEFINED_16, false							},
    { 0xb9, 0,	  NULL, A_UNDEFINED_17, false							},
    { 0xba, 0,	  NULL, A_UNDEFINED_18, false							},
    { 0xbb, 0,	  NULL, A_UNDEFINED_19, false							},
    { 0xbc, 0,	  NULL, A_UNDEFINED_20, false							},
    { 0xbd, 0,	  NULL, A_UNDEFINED_21, false							},
    { 0xbe, 0,	  NULL, A_UNDEFINED_22, false							},
    { L'\u00BF', L'\u00BF', NULL, A_QUESTION_DOWN, false	}, // upside-down question mark

    { L'\u00C0', L'\u00E0', NULL, A_CAP_AGRAVE, false		},
    { L'\u00C1', L'\u00E1', NULL, A_CAP_AACUTE, false		},
    { L'\u00C2', L'\u00E2', NULL, A_CAP_ACIRCUMFLEX, false	},
    { L'\u00C3', L'\u00E3', NULL, A_CAP_ATILDE, false		},
    { L'\u00C4', L'\u00E4', NULL, A_CAP_ADIERESIS, false	},
    { L'\u00C5', L'\u00E5', NULL, A_CAP_ARING, false		},
    { L'\u00C6', L'\u00E6', NULL, A_CAP_AE, false			},
    { L'\u00C7', L'\u00E7', NULL, A_CAP_CCEDILLA, false		},
    { L'\u00C8', L'\u00E8', NULL, A_CAP_EGRAVE, false		},
    { L'\u00C9', L'\u00E9', NULL, A_CAP_EACUTE, false		},
    { L'\u00CA', L'\u00EA', NULL, A_CAP_ECIRCUMFLEX, false	},
    { L'\u00CB', L'\u00EB', NULL, A_CAP_EDIERESIS, false	},
    { L'\u00CC', L'\u00EC', NULL, A_CAP_IGRAVE, false		},
    { L'\u00CD', L'\u00ED', NULL, A_CAP_IACUTE, false		},
    { L'\u00CE', L'\u00EE', NULL, A_CAP_ICIRCUMFLEX, false	},
    { L'\u00CF', L'\u00EF', NULL, A_CAP_IDIERESIS, false	},
    { L'\u00D0', L'\u00F0', NULL, A_CAP_ETH, false			},
    { L'\u00D1', L'\u00F1', NULL, A_CAP_NTILDE, false		},
    { L'\u00D2', L'\u00F2', NULL, A_CAP_OGRAVE, false		},
    { L'\u00D3', L'\u00F3', NULL, A_CAP_OACUTE, false		},
    { L'\u00D4', L'\u00F4', NULL, A_CAP_OCIRCUMFLEX, false	},
    { L'\u00D5', L'\u00F5', NULL, A_CAP_OTILDE, false		},
    { L'\u00D6', L'\u00F6', NULL, A_CAP_ODIERESIS, false	},
    { L'\u00D7', L'\u00D7', "KP_STAR", A_MULTIPLY, false 	},
    { L'\u00D8', L'\u00F8', NULL, A_CAP_OSLASH, false		},
    { L'\u00D9', L'\u00F9', NULL, A_CAP_UGRAVE, false		},
    { L'\u00DA', L'\u00FA', NULL, A_CAP_UACUTE, false		},
    { L'\u00DB', L'\u00FB', NULL, A_CAP_UCIRCUMFLEX, false	},
    { L'\u00DC', L'\u00FC', NULL, A_CAP_UDIERESIS, false	},
    { L'\u00DD', L'\u00FD', NULL, A_CAP_YACUTE, false		},
    { L'\u00DE', L'\u00FE', NULL, A_CAP_THORN, false		},
    { L'\u00DF', L'\u00DF', NULL, A_GERMANDBLS, false 		},

    { L'\u00C0', L'\u00E0', NULL, A_LOW_AGRAVE, false		},
    { L'\u00C1', L'\u00E1', NULL, A_LOW_AACUTE, false		},
    { L'\u00C2', L'\u00E2', NULL, A_LOW_ACIRCUMFLEX, false	},
    { L'\u00C3', L'\u00E3', NULL, A_LOW_ATILDE, false		},
    { L'\u00C4', L'\u00E4', NULL, A_LOW_ADIERESIS, false	},
    { L'\u00C5', L'\u00E5', NULL, A_LOW_ARING, false		},
    { L'\u00C6', L'\u00E6', NULL, A_LOW_AE, false			},
    { L'\u00C7', L'\u00E7', NULL, A_LOW_CCEDILLA, false		},
    { L'\u00C8', L'\u00E8', NULL, A_LOW_EGRAVE, false		},
    { L'\u00C9', L'\u00E9', NULL, A_LOW_EACUTE, false		},
    { L'\u00CA', L'\u00EA', NULL, A_LOW_ECIRCUMFLEX, false	},
    { L'\u00CB', L'\u00EB', NULL, A_LOW_EDIERESIS, false	},
    { L'\u00CC', L'\u00EC', NULL, A_LOW_IGRAVE, false		},
    { L'\u00CD', L'\u00ED', NULL, A_LOW_IACUTE, false		},
    { L'\u00CE', L'\u00EE', NULL, A_LOW_ICIRCUMFLEX, false	},
    { L'\u00CF', L'\u00EF', NULL, A_LOW_IDIERESIS, false	},
    { L'\u00D0', L'\u00F0', NULL, A_LOW_ETH, false			},
    { L'\u00D1', L'\u00F1', NULL, A_LOW_NTILDE, false		},
    { L'\u00D2', L'\u00F2', NULL, A_LOW_OGRAVE, false		},
    { L'\u00D3', L'\u00F3', NULL, A_LOW_OACUTE, false		},
    { L'\u00D4', L'\u00F4', NULL, A_LOW_OCIRCUMFLEX, false	},
    { L'\u00D5', L'\u00F5', NULL, A_LOW_OTILDE, false		},
    { L'\u00D6', L'\u00F6', NULL, A_LOW_ODIERESIS, false	},
    { L'\u00F7', L'\u00F7', "KP_SLASH", A_DIVIDE, false 	},
    { L'\u00D8', L'\u00F8', NULL, A_LOW_OSLASH, false		},
    { L'\u00D9', L'\u00F9', NULL, A_LOW_UGRAVE, false		},
    { L'\u00DA', L'\u00FA', NULL, A_LOW_UACUTE, false		},
    { L'\u00DB', L'\u00FB', NULL, A_LOW_UCIRCUMFLEX, false	},
    { L'\u00DC', L'\u00FC', NULL, A_LOW_UDIERESIS, false	},
    { L'\u00DD', L'\u00FD', NULL, A_LOW_YACUTE, false		},
    { L'\u00DE', L'\u00FE', NULL, A_LOW_THORN, false		},
    { 0x9f, 0xff, NULL, A_LOW_YDIERESIS, false							},	// *******

    { 0x100, 0x100, "JOY0", A_JOY0, false								},
    { 0x101, 0x101, "JOY1", A_JOY1, false								},
    { 0x102, 0x102, "JOY2", A_JOY2, false								},
    { 0x103, 0x103, "JOY3", A_JOY3, false								},
    { 0x104, 0x104, "JOY4", A_JOY4, false								},
    { 0x105, 0x105, "JOY5", A_JOY5, false								},
    { 0x106, 0x106, "JOY6", A_JOY6, false								},
    { 0x107, 0x107, "JOY7", A_JOY7, false								},
    { 0x108, 0x108, "JOY8", A_JOY8, false								},
    { 0x109, 0x109, "JOY9", A_JOY9, false								},
    { 0x10a, 0x10a, "JOY10", A_JOY10, false								},
    { 0x10b, 0x10b, "JOY11", A_JOY11, false								},
    { 0x10c, 0x10c, "JOY12", A_JOY12, false								},
    { 0x10d, 0x10d, "JOY13", A_JOY13, false								},
    { 0x10e, 0x10e, "JOY14", A_JOY14, false								},
    { 0x10f, 0x10f, "JOY15", A_JOY15, false								},
    { 0x110, 0x110, "JOY16", A_JOY16, false								},
    { 0x111, 0x111, "JOY17", A_JOY17, false								},
    { 0x112, 0x112, "JOY18", A_JOY18, false								},
    { 0x113, 0x113, "JOY19", A_JOY19, false								},
    { 0x114, 0x114, "JOY20", A_JOY20, false								},
    { 0x115, 0x115, "JOY21", A_JOY21, false								},
    { 0x116, 0x116, "JOY22", A_JOY22, false								},
    { 0x117, 0x117, "JOY23", A_JOY23, false								},
    { 0x118, 0x118, "JOY24", A_JOY24, false								},
    { 0x119, 0x119, "JOY25", A_JOY25, false								},
    { 0x11a, 0x11a, "JOY26", A_JOY26, false								},
    { 0x11b, 0x11b, "JOY27", A_JOY27, false								},
    { 0x11c, 0x11c, "JOY28", A_JOY28, false								},
    { 0x11d, 0x11d, "JOY29", A_JOY29, false								},
    { 0x11e, 0x11e, "JOY30", A_JOY30, false								},
    { 0x11f, 0x11f, "JOY31", A_JOY31, false								},

    { 0x120, 0x120, "AUX0", A_AUX0, false								},
    { 0x121, 0x121, "AUX1", A_AUX1, false								},
    { 0x122, 0x122, "AUX2", A_AUX2, false								},
    { 0x123, 0x123, "AUX3", A_AUX3, false								},
    { 0x124, 0x124, "AUX4", A_AUX4, false								},
    { 0x125, 0x125, "AUX5", A_AUX5, false								},
    { 0x126, 0x126, "AUX6", A_AUX6, false								},
    { 0x127, 0x127, "AUX7", A_AUX7, false								},
    { 0x128, 0x128, "AUX8", A_AUX8, false								},
    { 0x129, 0x129, "AUX9", A_AUX9, false								},
    { 0x12a, 0x12a, "AUX10", A_AUX10, false								},
    { 0x12b, 0x12b, "AUX11", A_AUX11, false								},
    { 0x12c, 0x12c, "AUX12", A_AUX12, false								},
    { 0x12d, 0x12d, "AUX13", A_AUX13, false								},
    { 0x12e, 0x12e, "AUX14", A_AUX14, false								},
    { 0x12f, 0x12f, "AUX15", A_AUX15, false								},
    { 0x130, 0x130, "AUX16", A_AUX16, false								},
    { 0x131, 0x131, "AUX17", A_AUX17, false								},
    { 0x132, 0x132, "AUX18", A_AUX18, false								},
    { 0x133, 0x133, "AUX19", A_AUX19, false								},
    { 0x134, 0x134, "AUX20", A_AUX20, false								},
    { 0x135, 0x135, "AUX21", A_AUX21, false								},
    { 0x136, 0x136, "AUX22", A_AUX22, false								},
    { 0x137, 0x137, "AUX23", A_AUX23, false								},
    { 0x138, 0x138, "AUX24", A_AUX24, false								},
    { 0x139, 0x139, "AUX25", A_AUX25, false								},
    { 0x13a, 0x13a, "AUX26", A_AUX26, false								},
    { 0x13b, 0x13b, "AUX27", A_AUX27, false								},
    { 0x13c, 0x13c, "AUX28", A_AUX28, false								},
    { 0x13d, 0x13d, "AUX29", A_AUX29, false								},
    { 0x13e, 0x13e, "AUX30", A_AUX30, false								},
    { 0x13f, 0x13f, "AUX31", A_AUX31, false								}
};
static const size_t numKeynames = (sizeof(keynames) / sizeof(keynames[0])); // for auto-complete



/*
=============================================================================

EDIT FIELDS

=============================================================================
*/

static void Key_CheckRep( void ) {
#ifndef NDEBUG
	assert( kg.killTail >= 0 && kg.killTail < KILL_RING_SIZE );
	assert( kg.killHead >= 0 && kg.killHead < KILL_RING_SIZE );

	if ( kg.yankIndex != -1 ) {
		assert( kg.killTail != kg.killHead );

		if ( kg.killTail > kg.killHead ) {
			assert( kg.killHead < kg.yankIndex && kg.yankIndex <= kg.killTail );
		} else {
			assert( 0 <= kg.yankIndex && kg.yankIndex < KILL_RING_SIZE );
			assert( kg.yankIndex <= kg.killTail || kg.killHead < kg.yankIndex );
		}
	}

	Field_CheckRep( &kg.g_consoleField );
#endif // NDEBUG
}

/*
===================
Field_Draw

Handles horizontal scrolling and cursor blinking
x, y, amd width are in pixels
===================
*/
void Field_VariableSizeDraw( field_t *edit, int x, int y, qboolean smallSize, qboolean showCursor ) {
	int		len;
	int		printLen;
	int		cursorChar;
	int		cursorOffset;
	int		scrollOffset;
	char	str[MAX_EDIT_LINE];

	len = strlen(edit->buffer);
	printLen = Q_PrintStrlen(edit->buffer, (qboolean)MV_USE102COLOR);

	cursorOffset = Q_PrintStrLenTo(edit->buffer, edit->cursor, NULL, MV_USE102COLOR);
	scrollOffset = Q_PrintStrLenTo(edit->buffer, edit->scroll, NULL, MV_USE102COLOR);

	if (scrollOffset > cursorOffset - 1)
		scrollOffset = MAX(0, cursorOffset - 1);

	if (scrollOffset > printLen - edit->widthInChars + 1)
		scrollOffset = MAX(0, printLen - edit->widthInChars + 1);

	if (scrollOffset < cursorOffset - edit->widthInChars + 1)
		scrollOffset = MAX(0, cursorOffset - edit->widthInChars + 1);

	edit->scroll = Q_PrintStrCharsTo(edit->buffer, scrollOffset, NULL, MV_USE102COLOR);

	Q_PrintStrCopy(str, edit->buffer, sizeof(str), edit->scroll, edit->widthInChars, MV_USE102COLOR);

	// draw it
	if ( smallSize ) {
		float	color[4];

		color[0] = color[1] = color[2] = color[3] = 1.0;
		SCR_DrawSmallStringExt( x, y, str, color, qfalse );
	} else {
		// draw big string with drop shadow
		SCR_DrawBigString( x, y, str, 1.0 );
	}

	// draw the cursor
	if ( !showCursor ) {
		return;
	}

	if ( (int)( cls.realtime >> 8 ) & 1 ) {
		return;		// off blink
	}

	if ( kg.key_overstrikeMode ) {
		cursorChar = 11;
	} else {
		cursorChar = 10;
	}

	cursorOffset = cursorOffset - scrollOffset;

	if ( smallSize ) {
		SCR_DrawSmallChar( x + cursorOffset * con.charWidth, y, cursorChar );
	} else {
		char cursorStr[] = { (char)cursorChar, '\0' };
		SCR_DrawBigString( x + cursorOffset * BIGCHAR_WIDTH, y, cursorStr, 1.0 );
	}
}

void Field_Draw( field_t *edit, int x, int y, qboolean showCursor )
{
	Field_VariableSizeDraw( edit, x, y, qtrue, showCursor );
}

void Field_BigDraw( field_t *edit, int x, int y, qboolean showCursor )
{
	Field_VariableSizeDraw( edit, x, y, qfalse, showCursor );
}


static void Field_SaveHistory( field_t *edit ) {
#if 0
	if ( edit->currentTail != edit->historyHead ) {
		int prev = (edit->currentTail + FIELD_HISTORY_SIZE - 1) % FIELD_HISTORY_SIZE;
		assert( strcmp( edit->buffer, edit->bufferHistory[prev] ) );
	}
#endif
	edit->cursorHistory[edit->currentTail] = edit->cursor;
	edit->scrollHistory[edit->currentTail] = edit->scroll;
	edit->typing = qfalse;

	edit->currentTail = (edit->currentTail + 1) % FIELD_HISTORY_SIZE;
	edit->historyTail = edit->currentTail;
	memcpy( edit->bufferHistory[edit->currentTail], edit->buffer, MAX_EDIT_LINE );
	edit->buffer = edit->bufferHistory[edit->currentTail];

	if ( edit->historyTail == edit->historyHead )
		edit->historyHead = (edit->historyHead + 1) % FIELD_HISTORY_SIZE;
}

static void Field_Undo( field_t *edit ) {
	if ( edit->currentTail == edit->historyHead )
		return;

	if ( edit->currentTail == edit->historyTail ) {
		edit->cursorHistory[edit->currentTail] = edit->cursor;
		edit->scrollHistory[edit->currentTail] = edit->scroll;
	}

	edit->currentTail = (FIELD_HISTORY_SIZE + edit->currentTail - 1) % FIELD_HISTORY_SIZE;
	edit->buffer = edit->bufferHistory[edit->currentTail];
	edit->cursor = edit->cursorHistory[edit->currentTail];
	edit->scroll = edit->scrollHistory[edit->currentTail];
	edit->typing = qfalse;
}

static void Field_Redo( field_t *edit ) {
	if ( edit->currentTail == edit->historyTail )
		return;

	edit->currentTail = (edit->currentTail + 1) % FIELD_HISTORY_SIZE;
	edit->buffer = edit->bufferHistory[edit->currentTail];
	edit->cursor = edit->cursorHistory[edit->currentTail];
	edit->scroll = edit->scrollHistory[edit->currentTail];
}

void Field_Paste( field_t *edit ) {
	char	*cbd;
	int		pasteLen, i;

	cbd = Sys_GetClipboardData();

	if ( !cbd ) {
		return;
	}

	edit->typing = qfalse;

	// send as if typed, so insert / overstrike works properly
	pasteLen = (int)strlen(cbd);
	for ( i = 0 ; i < pasteLen ; i++ ) {
		Field_CharEvent( edit, cbd[i] );
	}

	edit->typing = qfalse;

	Z_Free( cbd );
}

static void Field_ForwardWord( field_t *edit ) {
	char	*c;

	c = edit->buffer + edit->cursor;

	while ( *c != '\0' && !Q_isalnum( *c ) )
		c++;

	while ( Q_isalnum( *c ) )
		c++;

	edit->cursor = c - edit->buffer;
}

static void Field_BackwardWord( field_t *edit ) {
	int	cursor = edit->cursor;

	while (cursor > 0 && !Q_isalnum( edit->buffer[cursor - 1] ) )
		cursor--;

	while (cursor > 0 && Q_isalnum( edit->buffer[cursor - 1] ) )
		cursor--;

	edit->cursor = cursor;
}

static void Field_BackwardUnixWord( field_t *edit ) {
	int	cursor = edit->cursor;

	while (cursor > 0 && edit->buffer[cursor - 1] == ' ' )
		cursor--;

	while (cursor > 0 && edit->buffer[cursor - 1] != ' ' )
		cursor--;

	edit->cursor = cursor;
}

static char *Key_KillRingAdvance( void ) {
	kg.killTail = (kg.killTail + 1) % KILL_RING_SIZE;
	if ( kg.killTail == kg.killHead )
		kg.killHead = (kg.killHead + 1) % KILL_RING_SIZE;

	return kg.killRing[kg.killTail];
}

static void Field_KillLine( field_t *edit ) {
	char *killBuf;

	if ( edit->buffer[edit->cursor] == '\0' )
		return;

	Field_SaveHistory( edit );

	killBuf = Key_KillRingAdvance();

	Q_strncpyz( killBuf, edit->buffer + edit->cursor, MAX_EDIT_LINE );
	edit->buffer[edit->cursor] = '\0';
}

static void Field_LineDiscard( field_t *edit ) {
	char	*killBuf;
	int		len;

	if ( edit->cursor == 0 )
		return;

	Field_SaveHistory( edit );

	killBuf = Key_KillRingAdvance();
	len = strlen( edit->buffer + edit->cursor );

	memcpy( killBuf, edit->buffer, edit->cursor );
	killBuf[edit->cursor] = '\0';
	memmove( edit->buffer, edit->buffer + edit->cursor, len + 1 );

	edit->cursor = 0;
}

static void Field_KillWord( field_t *edit ) {
	char	*killBuf;
	int		start;
	int		end;
	int		len;

	if ( edit->cursor == (int)strlen(edit->buffer) )
		return;

	Field_SaveHistory( edit );

	killBuf = Key_KillRingAdvance();
	start = edit->cursor;

	Field_ForwardWord( edit );
	end = edit->cursor;
	len = end - start;
	assert( len > 0 );

	memcpy( killBuf, edit->buffer + start, len );
	killBuf[len] = '\0';
	len = strlen(edit->buffer + end);
	memmove( edit->buffer + start, edit->buffer + end, len + 1 );

	edit->cursor = start;
}

static void Field_BackwardKillWord( field_t *edit, qboolean unix_word ) {
	char	*killBuf;
	int		end;
	int		start;
	int		len;

	if ( edit->cursor == 0 )
		return;

	Field_SaveHistory( edit );

	killBuf = Key_KillRingAdvance();
	end = edit->cursor;

	if ( unix_word )
		Field_BackwardUnixWord( edit );
	else
		Field_BackwardWord( edit );

	start = edit->cursor;
	len = end - start;
	assert( len > 0 );

	memcpy( killBuf, edit->buffer + start, len );
	killBuf[len] = '\0';
	len = strlen(edit->buffer + end);
	memmove( edit->buffer + start, edit->buffer + end, len + 1 );
}


static void Key_YankReset( void ) {
	kg.yankIndex = -1;
}

static void Field_YankIndex( field_t *edit ) {
	char	buf[MAX_EDIT_LINE];

	assert( kg.yankIndex >= 0 && kg.yankIndex < KILL_RING_SIZE );
	assert( kg.killHead != kg.killTail );

	Q_strncpyz( buf, edit->buffer + edit->cursor, sizeof(buf) );
	Q_strncpyz( edit->buffer + edit->cursor, kg.killRing[kg.yankIndex], MAX_EDIT_LINE - edit->cursor );
	edit->cursor = strlen( edit->buffer );
	Q_strcat( edit->buffer, MAX_EDIT_LINE, buf );

	kg.yankIndex--;

	if ( kg.yankIndex == 0 )
		kg.yankIndex = kg.killTail;
	if ( kg.yankIndex == kg.killHead )
		kg.yankIndex = kg.killTail;

	assert( !(kg.killTail > kg.killHead) || (kg.killHead < kg.yankIndex && kg.yankIndex <= kg.killTail) );
	assert( !(kg.killTail < kg.killHead) || (kg.killHead < kg.yankIndex || kg.yankIndex <= kg.killTail) );
}

static void Field_Yank( field_t *edit ) {
	if ( kg.killHead == kg.killTail )
		return;
	if ( edit->cursor == MAX_EDIT_LINE - 1 )
		return;

	kg.yankIndex = kg.killTail;
	Field_SaveHistory( edit );
	Field_YankIndex( edit );
}

static void Field_YankPop( field_t *edit ) {
	if ( kg.yankIndex == -1 )
		return;

	Field_Undo( edit );
	Field_SaveHistory( edit );
	Field_YankIndex( edit );
}

/*
=================
Field_KeyDownEvent

Performs the basic line editing functions for the console,
in-game talk, and menu fields

Key events are used for non-printable characters, others are gotten from char events.
=================
*/
void Field_KeyDownEvent( field_t *edit, int key ) {
	int		len;

	if ( key == A_CTRL || key == A_ALT ) {
		return;
	}

	if ( edit->mod ) {
		if ( keynames[key].lower == 'u' && kg.keys[A_CTRL].down )
			Field_Undo( edit );

		edit->mod = qfalse;
		return;
	}

	if ( keynames[key].lower == 'y' && kg.keys[A_ALT].down )
	{
		Field_YankPop( edit );
		return;
	}

	Key_YankReset();

	if ( keynames[key].lower == 'k' && kg.keys[A_CTRL].down )
	{
		Field_KillLine( edit );
		return;
	}

	if ( keynames[key].lower == 'u' && kg.keys[A_CTRL].down )
	{
		Field_LineDiscard( edit );
		return;
	}

	len = (int)strlen(edit->buffer);

	if ( keynames[key].lower == 'd' && kg.keys[A_ALT].down )
	{
		Field_KillWord( edit );
		return;
	}

	if ( key == A_BACKSPACE && kg.keys[A_ALT].down )
	{
		Field_BackwardKillWord( edit, qfalse );
		return;
	}

	if ( keynames[key].lower == 'w' && kg.keys[A_CTRL].down )
	{
		Field_BackwardKillWord( edit, qtrue );
		return;
	}

	if ( keynames[key].lower == 'y' && kg.keys[A_CTRL].down )
	{
		Field_Yank( edit );
		return;
	}

	// shift-insert is paste
	if ( ( ( key == A_INSERT ) || ( key == A_KP_0 ) ) && kg.keys[A_SHIFT].down ) {
		Field_Paste( edit );
		return;
	}

	if ( key == A_DELETE || ( keynames[key].lower == 'd' && kg.keys[A_CTRL].down ) ) {
		if ( edit->cursor < len ) {
			Field_SaveHistory( edit );
			memmove( edit->buffer + edit->cursor,
				edit->buffer + edit->cursor + 1, len - edit->cursor );
		}
		return;
	}

	if ( key == A_CURSOR_RIGHT || ( keynames[key].lower == 'f' && kg.keys[A_CTRL].down ) )
	{
		if ( edit->cursor < len ) {
			edit->cursor++;
		}
		return;
	}

	if ( key == A_CURSOR_LEFT || ( keynames[key].lower == 'b' && kg.keys[A_CTRL].down ) )
	{
		if ( edit->cursor > 0 ) {
			edit->cursor--;
		}
		return;
	}

	if ( key == A_HOME ) {
		edit->cursor = 0;
		return;
	}

	if ( key == A_END ) {
		edit->cursor = len;
		return;
	}

	if ( keynames[key].lower == 'z' && kg.keys[A_CTRL].down && kg.keys[A_SHIFT].down )
	{
		Field_Redo( edit );
		return;
	}

	if (( keynames[key].lower == '/' && kg.keys[A_CTRL].down ) ||
		( keynames[key].lower == 'z' && kg.keys[A_CTRL].down ) ||
		( key == A_UNDERSCORE && kg.keys[A_CTRL].down ))
	{
		Field_Undo( edit );
		return;
	}

	if ( keynames[key].lower == 'x' && kg.keys[A_CTRL].down )
	{
		edit->mod = qtrue;
		return;
	}

	if ( keynames[key].lower == 'f' && kg.keys[A_ALT].down )
	{
		Field_ForwardWord( edit );
		return;
	}

	if ( keynames[key].lower == 'b' && kg.keys[A_ALT].down )
	{
		Field_BackwardWord( edit );
		return;
	}

	if ( key == A_INSERT ) {
		kg.key_overstrikeMode = (qboolean)!kg.key_overstrikeMode;
		return;
	}
}

/*
==================
Field_CharEvent
==================
*/
void Field_CharEvent( field_t *edit, int ch ) {
	int		len;

	if ( edit->mod ) {
		if ( ch >= 32 )
			edit->mod = qfalse;
		return;
	}

	if ( ch == 'y' - 'a' + 1 ) // ctr-y is handled in Field_KeyEvent
		return;
	if ( ch == 'w' - 'a' + 1 )
		return;

	Key_YankReset();

	if ( ch == 'v' - 'a' + 1 ) {	// ctrl-v is paste
		Field_Paste( edit );
		return;
	}

	if ( ch == 'c' - 'a' + 1 ) {	// ctrl-c clears the field
		if ( edit->buffer[0] != '\0' ) {
			Field_SaveHistory( edit );

			memset(edit->buffer, 0, MAX_EDIT_LINE);
			edit->cursor = 0;
		}
		return;
	}

	len = (int)strlen(edit->buffer);

	if ( ch == 'h' - 'a' + 1 )	{	// ctrl-h is backspace
		if ( edit->cursor > 0 ) {
			Field_SaveHistory( edit );
			memmove( edit->buffer + edit->cursor - 1,
				edit->buffer + edit->cursor, len + 1 - edit->cursor );
			edit->cursor--;
		}
		return;
	}

	if ( ch == 'a' - 'a' + 1 ) {    // ctrl-a is home
		edit->cursor = 0;
	}

	if ( ch == 'e' - 'a' + 1 ) {    // ctrl-e is end
		edit->cursor = len;
	}

	//
	// ignore any other non printable chars
	//
	if ( ch < 32 ) {
		return;
	}

	if ( kg.key_overstrikeMode ) {
		if ( edit->cursor == MAX_EDIT_LINE - 1 )
			return;
		if ( !edit->typing ) {
			Field_SaveHistory( edit );
			edit->typing = qtrue;
		}
		edit->buffer[edit->cursor] = ch;
		edit->cursor++;
	} else {	// insert mode
		if ( len == MAX_EDIT_LINE - 1 ) {
			return; // all full
		}
		if ( !edit->typing ) {
			Field_SaveHistory( edit );
			edit->typing = qtrue;
		}
		memmove( edit->buffer + edit->cursor + 1,
			edit->buffer + edit->cursor, len + 1 - edit->cursor );
		edit->buffer[edit->cursor] = ch;
		edit->cursor++;
	}

	if ( edit->cursor == len + 1) {
		edit->buffer[edit->cursor] = 0;
	}
}

static void keyConcatArgs( void ) {
	int		i;
	char	*arg;

	for ( i = 1 ; i < Cmd_Argc() ; i++ ) {
		Q_strcat( kg.g_consoleField.buffer, MAX_EDIT_LINE, " " );
		arg = Cmd_Argv( i );
		while (*arg) {
			if (*arg == ' ') {
				Q_strcat( kg.g_consoleField.buffer, MAX_EDIT_LINE,  "\"");
				break;
			}
			arg++;
		}
		Q_strcat( kg.g_consoleField.buffer, MAX_EDIT_LINE,  Cmd_Argv( i ) );
		if (*arg == ' ') {
			Q_strcat( kg.g_consoleField.buffer, MAX_EDIT_LINE,  "\"");
		}
	}
}

static void ConcatRemaining( const char *src, const char *start ) {
	const char *str;

	str = strstr(src, start);
	if (!str) {
		keyConcatArgs();
		return;
	}

	str += strlen(start);
	Q_strcat( kg.g_consoleField.buffer, MAX_EDIT_LINE, str);
}

/*
=============================================================================

CONSOLE LINE EDITING

==============================================================================
*/

static const char *completionString;
static char shortestMatch[MAX_TOKEN_CHARS];
static int	matchCount;

/*
===============
FindMatches

===============
*/
static void FindMatches( const char *s ) {
	int		i;

	if ( Q_stricmpn( s, completionString, (int)strlen( completionString ) ) ) {
		return;
	}
	matchCount++;
	if ( matchCount == 1 ) {
		Q_strncpyz( shortestMatch, s, sizeof( shortestMatch ) );
		return;
	}

	// cut shortestMatch to the amount common with s
	for ( i = 0 ; s[i] ; i++ ) {
		if ( tolower(shortestMatch[i]) != tolower(s[i]) ) {
			shortestMatch[i] = 0;
			break;
		}
	}
	if (!s[i])
	{
		shortestMatch[i] = 0;
	}
}

/*
===============
CompleteCommand

Tab expansion
===============
*/
/*
void CompleteCommand( void )
{
	field_t		*edit;
	field_t		temp;

	edit = &kg.g_consoleField;

	// only look at the first token for completion purposes
	Cmd_TokenizeString( edit->buffer );

	completionString = Cmd_Argv(0);
	if ( completionString[0] == '\\' || completionString[0] == '/' ) {
		completionString++;
	}
	matchCount = 0;
	shortestMatch[0] = 0;

	if ( strlen( completionString ) == 0 ) {
		return;
	}

	Cmd_CommandCompletion( FindMatches );
	Cvar_CommandCompletion( FindMatches );

	if ( matchCount == 0 ) {
		return;	// no matches
	}

	Com_Memcpy(&temp, edit, sizeof(field_t));

	if ( matchCount == 1 ) {
		Com_sprintf( edit->buffer, MAX_EDIT_LINE, "\\%s", shortestMatch );
		if ( Cmd_Argc() == 1 ) {
			Q_strcat( kg.g_consoleField.buffer, MAX_EDIT_LINE, " " );
		} else {
			ConcatRemaining( temp.buffer, completionString );
		}
		edit->cursor = (int)strlen(edit->buffer);
		return;
	}

	// multiple matches, complete to shortest
	Com_sprintf( edit->buffer, MAX_EDIT_LINE, "\\%s", shortestMatch );
	edit->cursor = (int)strlen(edit->buffer);
	ConcatRemaining( temp.buffer, completionString );

	Com_Printf( "]%s\n", edit->buffer );

	// run through again, printing matches
	Cmd_CommandCompletion( PrintMatches );
	Cvar_CommandCompletion( PrintMatches );
}
*/
void CompleteCommand( void )
{ // This is now calling Field_AutoComplete2 and adds a '\' if we found a match... (Hybrid between the old and the new Completion)
	field_t		*edit;
	char		temp[MAX_EDIT_LINE];

	// Field_AutoComplete( &kg.g_consoleField );
	Field_AutoComplete2( &kg.g_consoleField, qtrue, qtrue, qfalse );

	edit = &kg.g_consoleField;

	// only look at the first token for completion purposes
	Cmd_TokenizeString( edit->buffer );

	completionString = Cmd_Argv(0);
	if ( completionString[0] == '\\' || completionString[0] == '/' ) {
		completionString++;
	}
	matchCount = 0;
	shortestMatch[0] = 0;

	if ( strlen( completionString ) == 0 ) {
		return;
	}

	Cmd_CommandCompletion( FindMatches );
	Cvar_CommandCompletion( FindMatches );

	if ( matchCount == 0 ) {
		return;	// no matches
	}

	Com_Memcpy(temp, edit->buffer, MAX_EDIT_LINE);

	Com_sprintf(edit->buffer, MAX_EDIT_LINE, "\\%s", shortestMatch);
	if (matchCount == 1) {
		if (Cmd_Argc() == 1) {
			Q_strcat(edit->buffer, MAX_EDIT_LINE, " ");
		} else {
			ConcatRemaining(temp, completionString);
		}
		edit->cursor = (int)strlen(edit->buffer);
	} else {
		// multiple matches, complete to shortest
		edit->cursor = (int)strlen(edit->buffer);
		ConcatRemaining(temp, completionString);
	}
}

/*
====================
Console_Key

Handles history and console scrollback
====================
*/
void Console_Key (int key) {
	// ctrl-L clears screen
	if ( keynames[ key ].lower == 'l' && kg.keys[A_CTRL].down ) {
		Cbuf_AddText ("clear\n");
		return;
	}

	// enter finishes the line
	if ( key == A_ENTER || key == A_KP_ENTER ||
		(keynames[ key ].lower == 'm' && kg.keys[A_CTRL].down) ||
		(keynames[ key ].lower == 'j' && kg.keys[A_CTRL].down) )
	{
		// if not in the game explicitly prepent a slash if needed
		if ( cls.state != CA_ACTIVE && kg.g_consoleField.buffer[0] != '\\'
			&& kg.g_consoleField.buffer[0] != '/' ) {
			char	temp[MAX_STRING_CHARS];

			Q_strncpyz( temp, kg.g_consoleField.buffer, sizeof( temp ) );
			Com_sprintf( kg.g_consoleField.buffer, MAX_EDIT_LINE, "\\%s", temp );
			kg.g_consoleField.cursor++;
		}
		else
		{	// Added this to automatically make explicit commands not need slashes.
			CompleteCommand();
		}

		Com_Printf ( "]%s\n", kg.g_consoleField.buffer );

		// leading slash is an explicit command
		if ( kg.g_consoleField.buffer[0] == '\\' || kg.g_consoleField.buffer[0] == '/' ) {
			Cbuf_AddText( kg.g_consoleField.buffer+1 );	// valid command
			Cbuf_AddText ("\n");
		} else {
			// other text will be chat messages
			if ( !kg.g_consoleField.buffer[0] ) {
				return;	// empty lines just scroll the console without adding to history
			} else {
				Cbuf_AddText ("cmd say ");
				Cbuf_AddText( kg.g_consoleField.buffer );
				Cbuf_AddText ("\n");
			}
		}

		// copy line to history buffer
		memcpy( kg.historyEditLines[kg.nextHistoryLine % COMMAND_HISTORY],
			kg.g_consoleField.buffer,
			MAX_EDIT_LINE );
		kg.nextHistoryLine++;
		kg.historyLine = kg.nextHistoryLine;

		Field_Clear( &kg.g_consoleField );

		if ( cls.state == CA_DISCONNECTED ) {
			SCR_UpdateScreen ();	// force an update, because the command
		}							// may take some time
		return;
	}

	// command completion

	if (key == A_TAB) {
		//CompleteCommand();
		Field_AutoComplete( &kg.g_consoleField ); // for auto-complete (copied from OpenJK)
		return;
	}

	// command history (ctrl-p ctrl-n for unix style)

	if ( ( key == A_CURSOR_UP ) || ( ( keynames[ key ].lower == 'p' ) && kg.keys[A_CTRL].down ) )
	{
		int len;

		if ( kg.nextHistoryLine - kg.historyLine < COMMAND_HISTORY && kg.historyLine > 0 )
		{
			kg.historyLine--;
		}

		Field_Clear( &kg.g_consoleField );
		memcpy( kg.g_consoleField.buffer,
				kg.historyEditLines[ kg.historyLine % COMMAND_HISTORY ],
				MAX_EDIT_LINE );
		len = strlen( kg.g_consoleField.buffer );
		kg.g_consoleField.cursor = len;
		kg.g_consoleField.scroll = MAX(0, len - kg.g_consoleField.widthInChars);
		return;
	}

	if ( ( key == A_CURSOR_DOWN ) || ( ( keynames[ key ].lower == 'n' ) && kg.keys[A_CTRL].down ) )
	{
		int len;

		if (kg.historyLine == kg.nextHistoryLine)
			return;
		kg.historyLine++;

		Field_Clear( &kg.g_consoleField );
		memcpy( kg.g_consoleField.buffer,
				kg.historyEditLines[ kg.historyLine % COMMAND_HISTORY ],
				MAX_EDIT_LINE );
		len = strlen( kg.g_consoleField.buffer );
		kg.g_consoleField.cursor = len;
		kg.g_consoleField.scroll = MAX(0, len - kg.g_consoleField.widthInChars);
		return;
	}

	// Fast scrolling and mousewheel-scrolling
	if ( (key == A_PAGE_UP || kg.keys[A_MWHEELUP].down) && kg.keys[A_CTRL].down ) {
		Con_PageUp(2*4);
		return;
	}

	if ( (key == A_PAGE_DOWN || kg.keys[A_MWHEELDOWN].down) && kg.keys[A_CTRL].down ) {
		Con_PageDown(2*4);
		return;
	}

	// console scrolling
	if ( key == A_PAGE_UP || kg.keys[A_MWHEELUP].down ) {
		Con_PageUp(2);
		return;
	}

	if ( key == A_PAGE_DOWN || kg.keys[A_MWHEELDOWN].down ) {
		Con_PageDown(2);
		return;
	}

	// ctrl-home = top of console
	if ( key == A_HOME && kg.keys[A_CTRL].down ) {
		Con_Top();
		return;
	}

	// ctrl-end = bottom of console
	if ( key == A_END && kg.keys[A_CTRL].down ) {
		Con_Bottom();
		return;
	}

	// pass to the normal editline routine
	Field_KeyDownEvent( &kg.g_consoleField, key );
	Key_CheckRep();
}

//============================================================================


/*
================
Message_Key

In game talk message
================
*/
void Message_Key( int key ) {

	char	buffer[MAX_STRING_CHARS];


	if (key == A_ESCAPE) {
		cls.keyCatchers &= ~KEYCATCH_MESSAGE;
		Field_Clear( &chatField );
		return;
	}

	if ( key == A_ENTER || key == A_KP_ENTER  ||
		(keynames[ key ].lower == 'm' && kg.keys[A_CTRL].down) ||
		(keynames[ key ].lower == 'j' && kg.keys[A_CTRL].down) )
	{
		if ( chatField.buffer[0] && cls.state == CA_ACTIVE ) {
			if (chat_playerNum != -1 )

				Com_sprintf( buffer, sizeof( buffer ), "tell %i \"%s\"\n", chat_playerNum, chatField.buffer );

			else if (chat_team)

				Com_sprintf( buffer, sizeof( buffer ), "say_team \"%s\"\n", chatField.buffer );
			else
				Com_sprintf( buffer, sizeof( buffer ), "say \"%s\"\n", chatField.buffer );



			CL_AddReliableCommand( buffer );
		}
		cls.keyCatchers &= ~KEYCATCH_MESSAGE;
		Field_Clear( &chatField );
		return;
	}

	Field_KeyDownEvent( &chatField, key );
	Field_CheckRep( &chatField );
}

//============================================================================


qboolean Key_GetOverstrikeMode( void ) {
	return kg.key_overstrikeMode;
}


void Key_SetOverstrikeMode( qboolean state ) {
	kg.key_overstrikeMode = state;
}


/*
===================
Key_IsDown
===================
*/
qboolean Key_IsDown( int keynum ) {
	if ( keynum == -1 ) {
		return qfalse;
	}

	return kg.keys[ keynames[keynum].upper ].down;
}


/*
===================
Key_StringToKeynum

Returns a key number to be used to index keys[] by looking at
the given string.  Single ascii characters return themselves, while
the K_* names are matched up.

0x11 will be interpreted as raw hex, which will allow new controlers
to be configured even if they don't have defined names.
===================
*/
int Key_StringToKeynum( char *str ) {
	int			i;

	if ( !str || !str[0] )
	{
		return -1;
	}
	// If single char bind, presume ascii char bind
	if ( !str[1] )
	{
		return keynames[ (unsigned char)str[0] ].upper;
	}

	// scan for a text match
	for ( i = 0 ; i < MAX_KEYS ; i++ )
	{
		if ( keynames[i].name && !Q_stricmp( str, keynames[i].name ) )
		{
			return keynames[i].keynum;
		}
	}

	// check for hex code
	if ( str[0] == '0' && str[1] == 'x' && strlen( str ) == 4)
	{
		int		n1, n2;

		n1 = str[2];
		if ( n1 >= '0' && n1 <= '9' )
		{
			n1 -= '0';
		}
		else if ( n1 >= 'A' && n1 <= 'F' )
		{
			n1 = n1 - 'A' + 10;
		}
		else
		{
			n1 = 0;
		}

		n2 = str[3];
		if ( n2 >= '0' && n2 <= '9' )
		{
			n2 -= '0';
		}
		else if ( n2 >= 'A' && n2 <= 'F' )
		{
			n2 = n2 - 'A' + 10;
		}
		else
		{
			n2 = 0;
		}
		return n1 * 16 + n2;
	}

	return -1;
}


static char tinyString[16];
static const char *Key_KeynumValid( int keynum )
{
	if ( keynum == -1 )
	{
		return "<KEY NOT FOUND>";
	}
	if ( keynum < 0 || keynum >= MAX_KEYS )
	{
		return "<OUT OF RANGE>";
	}
	return NULL;
}

static const char *Key_KeyToName( int keynum )
{
	return keynames[keynum].name;
}


static const char *Key_KeyToAscii( int keynum )
{
	if(!keynames[keynum].lower)
	{
		return(NULL);
	}
	if(keynum == A_SPACE)
	{
		tinyString[0] = (char)A_SHIFT_SPACE;
	}
	else if(keynum == A_ENTER)
	{
		tinyString[0] = (char)A_SHIFT_ENTER;
	}
	else if(keynum == A_KP_ENTER)
	{
		tinyString[0] = (char)A_SHIFT_KP_ENTER;
	}
	else
	{
		tinyString[0] = keynames[keynum].upper;
	}
	tinyString[1] = 0;
	return tinyString;
}

static const char *Key_KeyToHex( int keynum )
{
	int		i, j;

	i = keynum >> 4;
	j = keynum & 15;

	tinyString[0] = '0';
	tinyString[1] = 'x';
	tinyString[2] = i > 9 ? i - 10 + 'A' : i + '0';
	tinyString[3] = j > 9 ? j - 10 + 'A' : j + '0';
	tinyString[4] = 0;

	return tinyString;
}

// Returns the ascii code of the keynum
const char *Key_KeynumToAscii( int keynum )
{
	const char	*name;

	name = Key_KeynumValid(keynum);

	// check for printable ascii
	if ( !name && keynum > 0 && keynum < 256 )
	{
		name = Key_KeyToAscii(keynum);
	}
	// Check for name (for JOYx and AUXx buttons)
	if ( !name )
	{
		name = Key_KeyToName(keynum);
	}
	// Fallback to hex number
	if ( !name )
	{
		name = Key_KeyToHex(keynum);
	}
	return name;
}


/*
===================
Key_KeynumToString

Returns a string (either a single ascii char, a K_* name, or a 0x11 hex string) for the
given keynum.
===================
*/
// Returns a console/config file friendly name for the key
const char *Key_KeynumToString( int keynum )
{
	const char	*name;

	name = Key_KeynumValid(keynum);

	// Check for friendly name
	if ( !name )
	{
		name = Key_KeyToName(keynum);
	}
	// check for printable ascii
	if ( !name && keynum > 0 && keynum < 256)
	{
		name = Key_KeyToAscii(keynum);
	}
	// Fallback to hex number
	if ( !name )
	{
		name = Key_KeyToHex(keynum);
	}
	return name;
}



/*
===================
Key_SetBinding
===================
*/
void Key_SetBinding( int keynum, const char *binding ) {
	if ( keynum == -1 ) {
		return;
	}

	// free old bindings
	if ( kg.keys[ keynames[keynum].upper ].binding ) {
		Z_Free( (void *)kg.keys[ keynames[keynum].upper ].binding );
		kg.keys[ keynames[keynum].upper ].binding = NULL;
	}

	// allocate memory for new binding
	if (binding)
	{
		kg.keys[ keynames[keynum].upper ].binding = CopyString( binding );
	}

	// consider this like modifying an archived cvar, so the
	// file write will be triggered at the next oportunity
	cvar_modifiedFlags |= CVAR_ARCHIVE;
}


/*
===================
Key_GetBinding
===================
*/
const char *Key_GetBinding( int keynum ) {
	if (keynum == -1) {
		return "<KEY NOT FOUND>";
	}

	if (keynum < 0 || keynum >= MAX_KEYS) {
		return "<KEY OUT OF RANGE>";
	}

	return kg.keys[ keynum ].binding;
}

/*
===================
Key_GetKey
===================
*/

int Key_GetKey(const char *binding) {
  int i;

  if (binding) {
  	for (i=0 ; i<256 ; i++) {
      if (kg.keys[i].binding && Q_stricmp(binding, kg.keys[i].binding) == 0) {
        return i;
      }
    }
  }
  return -1;
}

/*
===================
Key_Unbind_f
===================
*/
void Key_Unbind_f (void)
{
	int		b;

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("unbind <key> : remove commands from a key\n");
		return;
	}

	b = Key_StringToKeynum (Cmd_Argv(1));
	if (b==-1)
	{
		Com_Printf ("\"%s\" isn't a valid key\n", Cmd_Argv(1));
		return;
	}

	Key_SetBinding (b, "");
}

/*
===================
Key_Unbindall_f
===================
*/
void Key_Unbindall_f (void)
{
	int		i;

	for (i = 0; i < MAX_KEYS ; i++)
	{
		if (kg.keys[i].binding)
		{
			Key_SetBinding (i, "");
		}
	}
}



/*
===================
Key_Bind_f
===================
*/
void Key_Bind_f (void)
{
	int			c, b;

	c = Cmd_Argc();

	if (c < 2)
	{
		Com_Printf ("bind <key> [command] : attach a command to a key\n");
		return;
	}
	b = Key_StringToKeynum (Cmd_Argv(1));
	if (b==-1)
	{
		Com_Printf ("\"%s\" isn't a valid key\n", Cmd_Argv(1));
		return;
	}

	if (c == 2)
	{
		if (kg.keys[b].binding)
			Com_Printf ("\"%s\" = \"%s\"\n", Cmd_Argv(1), kg.keys[b].binding );
		else
			Com_Printf ("\"%s\" is not bound\n", Cmd_Argv(1) );
		return;
	}

	// copy the rest of the command line
	Key_SetBinding (b, Cmd_ArgsFrom (2));
}

/*
============
Key_WriteBindings

Writes lines containing "bind key value"
============
*/
void Key_WriteBindings( fileHandle_t f ) {
	int		i;

	FS_Printf (f, "unbindall\n" );
	for (i=0 ; i<MAX_KEYS ; i++) {
		if (kg.keys[i].binding && kg.keys[i].binding[0] ) {
			FS_Printf (f, "bind %s \"%s\"\n", Key_KeynumToString(i), kg.keys[i].binding);
		}
	}
}




/*
============
Key_Bindlist_f

============
*/
void Key_Bindlist_f( void ) {
	int		i;

	for ( i = 0 ; i < MAX_KEYS ; i++ ) {
		if ( kg.keys[i].binding && kg.keys[i].binding[0] ) {
			Com_Printf( "Key : %s (%s) \"%s\"\n", Key_KeynumToAscii(i), Key_KeynumToString(i), kg.keys[i].binding );
		}
	}
}


/*
============
Key_KeynameCompletion
============
*/
void Key_KeynameCompletion( callbackFunc_t callback ) { // for auto-complete (copied from OpenJK)
	for ( size_t i=0; i<numKeynames; i++ ) {
		if ( keynames[i].name )
			callback( keynames[i].name );
	}
}

/*
====================
Key_CompleteUnbind
====================
*/
static void Key_CompleteUnbind( char *args, int argNum ) { // for auto-complete (copied from OpenJK)
	if ( argNum == 2 ) {
		// Skip "unbind "
		char *p = Com_SkipTokens( args, 1, " " );
		if ( p > args )
			Field_CompleteKeyname();
	}
}

/*
====================
Key_CompleteBind
====================
*/
static void Key_CompleteBind( char *args, int argNum ) { // for auto-complete (copied from OpenJK)
	char *p;

	if ( argNum == 2 ) {
		// Skip "bind "
		p = Com_SkipTokens( args, 1, " " );

		if ( p > args )
			Field_CompleteKeyname();
	}
	else if ( argNum >= 3 ) {
		// Skip "bind <key> "
		p = Com_SkipTokens( args, 2, " " );

		if ( p > args )
			Field_CompleteCommand( p, qtrue, qtrue, qtrue );
	}
}

/*
===================
CL_InitKeyCommands
===================
*/
void CL_InitKeyCommands( void ) {
	// register our functions
	Cmd_AddCommand ("bind",Key_Bind_f);
	Cmd_SetCommandCompletionFunc( "bind", Key_CompleteBind );
	Cmd_AddCommand ("unbind",Key_Unbind_f);
	Cmd_SetCommandCompletionFunc( "unbind", Key_CompleteUnbind );
	Cmd_AddCommand ("unbindall",Key_Unbindall_f);
	Cmd_AddCommand ("bindlist",Key_Bindlist_f);
}

/*
===================
CL_AddKeyUpCommands
===================
*/
void CL_AddKeyUpCommands( int key, const char *kb, int time ) {
	int i;
	char button[1024], *buttonPtr;
	char	cmd[1024];
	qboolean keyevent;

	if ( !kb ) {
		return;
	}
	keyevent = qfalse;
	buttonPtr = button;
	for ( i = 0; ; i++ ) {
		if ( kb[i] == ';' || !kb[i] ) {
			*buttonPtr = '\0';
			if ( button[0] == '+') {
				// button commands add keynum and time as parms so that multiple
				// sources can be discriminated and subframe corrected
				Com_sprintf (cmd, sizeof(cmd), "-%s %i %i\n", button+1, key, time);
				Cbuf_AddText (cmd);
				keyevent = qtrue;
			} else {
				if (keyevent) {
					// down-only command
					Cbuf_AddText (button);
					Cbuf_AddText ("\n");
				}
			}
			buttonPtr = button;
			while ( (kb[i] <= ' ' || kb[i] == ';') && kb[i] != 0 ) {
				i++;
			}
		}
		*buttonPtr++ = kb[i];
		if ( !kb[i] ) {
			break;
		}
	}
}

/*
===================
CL_KeyEvent

Called by the system for both key up and key down events
===================
*/
void CL_KeyEvent (int key, qboolean down, int time) {
	const char	*kb;
	char		cmd[1024];

	// update auto-repeat status and BUTTON_ANY status
	kg.keys[ keynames[key].upper ].down = down;
	if (down)
	{
		kg.keys[ keynames[key].upper ].repeats++;
		if ( kg.keys[ keynames[key].upper ].repeats == 1)
		{
			kg.anykeydown = qtrue;
			kg.keyDownCount++;
		}
	}
	else
	{
		kg.keys[ keynames[key].upper ].repeats = 0;
		kg.keyDownCount--;
		if(kg.keyDownCount <= 0)
		{
			kg.anykeydown = qfalse;
			kg.keyDownCount = 0;
		}
	}

	if ( key == A_ENTER && kg.keys[A_ALT].down ) {
		if (!down) {
			return;
		}

		Cvar_SetValue( "r_fullscreen", !Cvar_VariableIntegerValue( "r_fullscreen" ) );
		return;
	}

	// console key is hardcoded, so the user can never unbind it
	if (key == A_CONSOLE) {
		if (!down) {
			return;
		}

		Con_ToggleConsole_f ();
		return;
	}


	// kg.keys can still be used for bound actions
	if ( down && /*( key < 128 || key == A_MOUSE1 ) && */ ( /*clc.demoplaying ||*/ cls.state == CA_CINEMATIC ) && !cls.keyCatchers) {

		if (Cvar_VariableValue ("com_cameraMode") == 0) {
			Cvar_Set ("nextdemo","");
			key = A_ESCAPE;
		}
	}


	// escape is always handled special
	if ( key == A_ESCAPE && down ) {
		if ( cls.keyCatchers & KEYCATCH_MESSAGE ) {
			// clear message mode
			Message_Key( key );
			return;
		}

		// escape always gets out of CGAME stuff
		if (cls.keyCatchers & KEYCATCH_CGAME) {
			cls.keyCatchers &= ~KEYCATCH_CGAME;
			VM_Call (cgvm, CG_EVENT_HANDLING, CGAME_EVENT_NONE);
			return;
		}

		if ( !( cls.keyCatchers & KEYCATCH_UI ) ) {
			if ( cls.state == CA_ACTIVE /*&& !clc.demoplaying*/ ) { // Allow pressing of buttons in demos (so you can view the console and menus)
				VM_Call(uivm, UI_SET_ACTIVE_MENU, UIMENU_INGAME);
			}
			else {
				CL_Disconnect_f();
				S_StopAllSounds();
				VM_Call(uivm, UI_SET_ACTIVE_MENU, UIMENU_MAIN);
			}
			return;
		}

		VM_Call(uivm, UI_KEY_EVENT, Key_GetProtocolKey(VM_GetGameversion(uivm), key), down);
		return;
	}

	//
	// key up events only perform actions if the game key binding is
	// a button command (leading + sign).  These will be processed even in
	// console mode and menu mode, to keep the character from continuing
	// an action started before a mode switch.
	//
	if (!down) {
		kb = kg.keys[ keynames[key].upper ].binding;

		CL_AddKeyUpCommands( key, kb, time );

		if ( cls.keyCatchers & KEYCATCH_UI && uivm ) {
			VM_Call(uivm, UI_KEY_EVENT, Key_GetProtocolKey(VM_GetGameversion(uivm), key), down);
		} else if ( cls.keyCatchers & KEYCATCH_CGAME && cgvm ) {
			VM_Call(cgvm, CG_KEY_EVENT, Key_GetProtocolKey(VM_GetGameversion(cgvm), key), down);
		}

		return;
	}


	// distribute the key down event to the apropriate handler
	if ( cls.keyCatchers & KEYCATCH_CONSOLE ) {
		Console_Key( key );
	} else if ( cls.keyCatchers & KEYCATCH_UI ) {
		if ( uivm ) {
			VM_Call(uivm, UI_KEY_EVENT, Key_GetProtocolKey(VM_GetGameversion(uivm), key), down);
		}
	} else if ( cls.keyCatchers & KEYCATCH_CGAME ) {
		if ( cgvm ) {
			VM_Call(cgvm, CG_KEY_EVENT, Key_GetProtocolKey(VM_GetGameversion(cgvm), key), down);
		}
	} else if ( cls.keyCatchers & KEYCATCH_MESSAGE ) {
		Message_Key( key );
	} else if ( cls.state == CA_DISCONNECTED ) {
		Console_Key( key );
	} else {
		// send the bound action
		kb = kg.keys[ keynames[key].upper ].binding;
		if (kb)
		{
			if (kb[0] == '+') {
				int i;
				char button[1024], *buttonPtr;
				buttonPtr = button;
				for ( i = 0; ; i++ ) {
					if ( kb[i] == ';' || !kb[i] ) {
						*buttonPtr = '\0';
						if ( button[0] == '+') {
							// button commands add keynum and time as parms so that multiple
							// sources can be discriminated and subframe corrected
							Com_sprintf (cmd, sizeof(cmd), "%s %i %i\n", button, key, time);
							Cbuf_AddText (cmd);
						} else {
							// down-only command
							Cbuf_AddText (button);
							Cbuf_AddText ("\n");
						}
						buttonPtr = button;
						while ( (kb[i] <= ' ' || kb[i] == ';') && kb[i] != 0 ) {
							i++;
						}
					}
					*buttonPtr++ = kb[i];
					if ( !kb[i] ) {
						break;
					}
				}
			} else {
				// down-only command
				Cbuf_AddText (kb);
				Cbuf_AddText ("\n");
			}
		}
	}
}


/*
===================
CL_CharEvent

Normal keyboard characters, already shifted / capslocked / etc
===================
*/
void CL_CharEvent( int key ) {
	// distribute the key down event to the apropriate handler
	if ( cls.keyCatchers & KEYCATCH_CONSOLE )
	{
		Field_CharEvent( &kg.g_consoleField, key );
		Key_CheckRep();
	}
	else if ( cls.keyCatchers & KEYCATCH_UI )
	{
		VM_Call(uivm, UI_KEY_EVENT, key | K_CHAR_FLAG, qtrue);
	}
	else if ( cls.keyCatchers & KEYCATCH_MESSAGE )
	{
		Field_CharEvent( &chatField, key );
		Field_CheckRep( &chatField );
	}
	else if ( cls.state == CA_DISCONNECTED )
	{
		Field_CharEvent( &kg.g_consoleField, key );
		Key_CheckRep();
	}
}


/*
===================
Key_ClearStates
===================
*/
void Key_ClearStates (void)
{
	int		i;

	kg.anykeydown = qfalse;

	for ( i=0 ; i < MAX_KEYS ; i++ ) {
		if ( kg.keys[i].down ) {
			CL_KeyEvent( i, qfalse, 0 );

		}
		kg.keys[i].down = qfalse;
		kg.keys[i].repeats = 0;
	}
}

/*
===================
multiprotocol support
===================
*/

static mvKeyconversion_t mvKeyconversion[] =
{
	{ K_TAB, A_TAB },
	{ K_ENTER, A_ENTER },
	{ K_ESCAPE, A_ESCAPE },
	{ K_SPACE, A_SPACE },

	{ K_BACKSPACE, A_BACKSPACE },

	//{ K_COMMAND, A_COMMAND },
	{ K_CAPSLOCK, A_CAPSLOCK },
	//{ K_POWER, A_POWER },
	{ K_PAUSE, A_PAUSE },

	{ K_UPARROW, A_CURSOR_UP },
	{ K_DOWNARROW, A_CURSOR_DOWN },
	{ K_LEFTARROW, A_CURSOR_LEFT },
	{ K_RIGHTARROW, A_CURSOR_RIGHT },

	{ K_ALT, A_ALT },
	{ K_ALT, A_ALT2 }, // 1.02 only knows ALT, 1.03+ knows ALT and ALTGR
	{ K_CTRL, A_CTRL },
	{ K_SHIFT, A_SHIFT },
	{ K_INS, A_INSERT },
	{ K_DEL, A_DELETE },
	{ K_PGDN, A_PAGE_DOWN },
	{ K_PGUP, A_PAGE_UP },
	{ K_HOME, A_HOME },
	{ K_END, A_END },

	{ K_F1, A_F1 },
	{ K_F2, A_F2 },
	{ K_F3, A_F3 },
	{ K_F4, A_F4 },
	{ K_F5, A_F5 },
	{ K_F6, A_F6 },
	{ K_F7, A_F7 },
	{ K_F8, A_F8 },
	{ K_F9, A_F9 },
	{ K_F10, A_F10 },
	{ K_F11, A_F11 },
	{ K_F12, A_F12 },
	/*
	{ K_F13, A_F13 },
	{ K_F14, A_F14 },
	{ K_F15, A_F15 },
	*/

	{ K_KP_HOME, A_KP_7 },
	{ K_KP_UPARROW, A_KP_8 },
	{ K_KP_PGUP, A_KP_9 },
	{ K_KP_LEFTARROW, A_KP_4 },
	{ K_KP_5, A_KP_5 },
	{ K_KP_RIGHTARROW, A_KP_6 },
	{ K_KP_END, A_KP_1 },
	{ K_KP_DOWNARROW, A_KP_2 },
	{ K_KP_PGDN, A_KP_3 },
	{ K_KP_ENTER, A_KP_ENTER },
	{ K_KP_INS, A_KP_0 },
	{ K_KP_DEL, A_KP_PERIOD },
	{ K_KP_SLASH, A_DIVIDE },
	{ K_KP_MINUS, A_KP_MINUS },
	{ K_KP_PLUS, A_KP_PLUS },
	{ K_KP_NUMLOCK, A_NUMLOCK },
	{ K_KP_STAR, A_MULTIPLY },
	//{ K_KP_EQUALS, A_KP_EQUALS },

	{ K_MOUSE1, A_MOUSE1 },
	{ K_MOUSE2, A_MOUSE2 },
	{ K_MOUSE3, A_MOUSE3 },
	{ K_MOUSE4, A_MOUSE4 },
	{ K_MOUSE5, A_MOUSE5 },

	{ K_MWHEELDOWN, A_MWHEELDOWN },
	{ K_MWHEELUP, A_MWHEELUP },

	{ K_JOY1, A_JOY1 },
	{ K_JOY2, A_JOY2 },
	{ K_JOY3, A_JOY3 },
	{ K_JOY4, A_JOY4 },
	{ K_JOY5, A_JOY5 },
	{ K_JOY6, A_JOY6 },
	{ K_JOY7, A_JOY7 },
	{ K_JOY8, A_JOY8 },
	{ K_JOY9, A_JOY9 },
	{ K_JOY10, A_JOY10 },
	{ K_JOY11, A_JOY11 },
	{ K_JOY12, A_JOY12 },
	{ K_JOY13, A_JOY13 },
	{ K_JOY14, A_JOY14 },
	{ K_JOY15, A_JOY15 },
	{ K_JOY16, A_JOY16 },
	{ K_JOY17, A_JOY17 },
	{ K_JOY18, A_JOY18 },
	{ K_JOY19, A_JOY19 },
	{ K_JOY20, A_JOY20 },
	{ K_JOY21, A_JOY21 },
	{ K_JOY22, A_JOY22 },
	{ K_JOY23, A_JOY23 },
	{ K_JOY24, A_JOY24 },
	{ K_JOY25, A_JOY25 },
	{ K_JOY26, A_JOY26 },
	{ K_JOY27, A_JOY27 },
	{ K_JOY28, A_JOY28 },
	{ K_JOY29, A_JOY29 },
	{ K_JOY30, A_JOY30 },
	{ K_JOY31, A_JOY31 },
	// { K_JOY32, A_JOY0 }, //FIXME: 1.02 has JOY32, 1.04 has JOY0, but they're not really mapped

	{ K_AUX1, A_AUX1 },
	{ K_AUX2, A_AUX2 },
	{ K_AUX3, A_AUX3 },
	{ K_AUX4, A_AUX4 },
	{ K_AUX5, A_AUX5 },
	{ K_AUX6, A_AUX6 },
	{ K_AUX7, A_AUX7 },
	{ K_AUX8, A_AUX8 },
	{ K_AUX9, A_AUX9 },
	{ K_AUX10, A_AUX10 },
	{ K_AUX11, A_AUX11 },
	{ K_AUX12, A_AUX12 },
	{ K_AUX13, A_AUX13 },
	{ K_AUX14, A_AUX14 },
	{ K_AUX15, A_AUX15 },
	{ K_AUX16, A_AUX16 },

	{ K_LAST_KEY, MAX_KEYS },
};
static int mvKeyconversionCount = sizeof(mvKeyconversion) / sizeof(mvKeyconversion[0]);


int Key_GetProtocolKey_New(mvversion_t version, int key, qboolean to15, qboolean invert) {
	int i;

	// We don't need to convert anything if we're not dealing with 1.02, cause internally we use the 1.03/1.04 values
	if ( version != VERSION_1_02 )
		return key;

	// Char events don't need conversion
	if ( key & K_CHAR_FLAG )
		return key;

	for ( i = 0; i < mvKeyconversionCount; i++ )
	{ // Find matching key
		if ( (key == mvKeyconversion[i].key16 && to15) || (key == mvKeyconversion[i].key15 && !to15) )
		{ // Found a match
			return (to15 ? (int)mvKeyconversion[i].key15 : (int)mvKeyconversion[i].key16);
		}
	}

	// Prevent double entries for 1.02 (Example: if 1.02 asks for K_CTRL it will be as if it asked for A_CTRL, if 1.02 asks for something that has the same number as A_CTRL it will count as A_CTRL, too: the CTRL key is handled twice. Solution: check if key would get altered by the inverse replacement).
	if ( !invert && Key_GetProtocolKey_New( version, key, (qboolean)!to15, qtrue ) != key ) return -1;

	// Limit the maximum
	if ( (to15 && key >= K_LAST_KEY) || (!to15 && key >= MAX_KEYS) ) return -1;

	// Return the key unmodified
	return key;
}

int Key_GetProtocolKey(mvversion_t version, int key16) {
	// Converts key16 to key15 (if not on 1.02)
	return Key_GetProtocolKey_New(version, key16, qtrue, qfalse);
}

int Key_GetProtocolKey15(mvversion_t version, int key15) {
	// Converts key15 to key16 (if not on 1.02)
	return Key_GetProtocolKey_New(version, key15, qfalse, qfalse);
}
