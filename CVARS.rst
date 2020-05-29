.. Keep this file in sync with wiki entries

======================
New and Modified Cvars
======================

:Name: com_busyWait
:Values: "0", "1"
:Default: "0"
:Description:
   Enable / Disable old "busy" game loop using 100% CPU time.

..

:Name: com_debugMessage
:Values: "0", "1"
:Default: "0"
:Description:
   Print warnings about field overflows in network messages. Useful
   for debugging modules.

..

:Name: com_timestamps
:Values: "0", "1"
:Default: "1"
:Description:
   Print timestamps in qconsole.log and system console.

..

:Name: fs_forcegame
:Values: Foldername
:Default: "" (Not set)
:Description:
   Overrides the active folder, allowing a server/client to store configs and
   other data in a specific folder independent of the active mod (``fs_game``).
   All new configs, screenshots, demos, etc. stored by the game end up in the
   specified folder. This folder may also be "base".

   Load order:

   | ``base``
   | ``fs_basegame cvar``
   | ``fs_game cvar``
   | ``fs_forcegame cvar``

-----------
Client-Side
-----------

:Name: cl_autoDemo
:Values: "0", "1"
:Default: "0"
:Description:
   When enabled, starts recording a demo automatically on joining a
   server. Current and single last demo are stored
   in ``demos/LastDemo`` directory.

..

:Name: cl_autoDemoFormat
:Values: Format String
:Default: "%t_%m"
:Description:
   Filename format for demos saved with ``saveDemo`` command. Valid
   format tokens are:

   | %d: local date
   | %m: map
   | %n: custom name supplied as an argument to ``saveDemo`` command
   | %p: player name
   | %t: sequence of server timestamps when ``saveDemo`` was executed
   | %%: % character

..

:Name: cl_aviFrameRate
:Values: Integer from 1 to 1000
:Default: "30"
:Description:
   Frame rate for recording with ``video`` command.

..

:Name: cl_aviMotionJpeg
:Values: "0", "1"
:Default: "1"
:Description:
   Record AVI using Motion JPEG video compression format. Much smaller
   file size for little quality loss.

..

:Name: cl_aviMotionJpegQuality
:Values: Integer from 0 to 100
:Default: "90"
:Description:
   JPEG quality used by AVI Motion JPEG compression. Lower values result
   in worse quality and smaller file size.

..

:Name: cl_drawRecording
:Values: "0", "1", "2"
:Default: "1"
:Description:
   | 0: don't draw any demo recording indicator
   | 1: draw filename and demo size near the top of the screen
   | 2: draw red dot in the bottom left corner

..

:Name: con_height
:Values: Decimal > 0
:Default: "0.5"
:Description:
   Fraction of a screen which should be occupied by in-game console.

..

:Name: con_scale
:Values: Decimal > 0
:Default: "1"
:Description:
   Scale console font relative to it's original size.

..

:Name: con_timestamps
:Values: "0", "1"
:Default: "1"
:Description:
   Draw local timestamps in console and condump output.

..

:Name: mv_allowDownload
:Values: "0", "1"
:Default: "1"
:Description:
   Enable / Disable Downloads. If you turn this off, the download
   popup will not appear and you will not be asked wether a file
   should be downloaded.

..

:Name: mv_apienabled
:Values: "0", "1", "2", "3"
:Default: Max supported MVAPI level
:Description:
   Max MVAPI level modules can use. 0 disables MVAPI SysCalls
   completely.

..

:Name: mv_consoleShiftRequirement
:Values: "0", "1", 2
:Default: "1"
:Description:
   | 0: shift is not required to open/close the console.
   | 1: shift is required to open the console but not to close it.
   | 2: shift is required to both, open and to close the console.

..

:Name: mv_nameShadows
:Values: "0", "1", 2
:Default: "2"
:Description:
   | 0: no name shadows at all.
   | 1: name shadows enabled on every version.
   | 2: name shadows enabled in 1.02 mode.

..

:Name: mv_menuOverride
:Values: "0", "1"
:Default: "0"
:Description:
   Allow loading custom UI modules in the main menu. Beware! This
   gives full control over downloaded content to the mod, there will
   be no download popup. Use only for testing.

..

:Name: mv_slowrefresh
:Values: Integer >= 0
:Default: "3"
:Description:
   Number of requests on a serverlist refresh sent per second to
   servers in the list. Some providers filter packets on a high number
   of requests to a lot of different IP addresses in a short
   time. (e.g. two major ISPs in Germany: "Kabel Deutschland", "Kabel
   BW").

..

:Name: r_consoleFont
:Values: "0", "1", "2"
:Default: "1"
:Description:
   Font used in console, timer, message input field and other places:

   | 0: Original charsgrid_med
   | 1: Code New Roman
   | 2: M+ 1M

..

:Name: r_dynamicGlow
:Values: "0", "1"
:Default: "0"
:Description:
   Enable / Disable dynamic glow effect.

..

:Name: r_environmentMapping
:Values: "0", "1"
:Default: "1"
:Description:
   Disable environment mapping for better performance on low-end
   machines.

..

:Name: r_ext_multisample
:Values: "0", "2", "4", "8", "16"
:Default: "0"
:Description:
   Multisample anti-aliasing. May not work on all machines.

..

:Name: r_ext_texture_filter_anisotropic
:Values: "0", "2", "4", "8", "16"
:Default: "2"
:Description:
   Anisotropic filtering level. Higher values increase image quality
   with little performance loss.

..

:Name: r_fontSharpness
:Values: Decimal >= 0
:Default: "1"
:Description:
   Relative font sharpness (doesn't affect console font).

   | 0: Always use original low-res fonts
   | 1: Best quality (in fau's opinion)

..

:Name: r_gammamethod
:Values: "0", "1", "2"
:Default: "2"
:Description:
   Method for applying gamma correction. Keep in mind that using
   non-functional gamma method disables not only ``r_gamma``, but also
   ``r_overbrightbits``.

   | 0: Pre-processing. Causes washed out colors. Use as last resort.
   | 1: Hardware gamma. Works only in fullscreen.
   | 2: Post-processing. Works in both fullscreen and windowed.

..

:Name: r_saberGlow
:Values: "0", "1"
:Default: "1"
:Description:
   Enable / Disable dynamic glow on saber shaders. Turn off
   if it breaks your custom saber model.

..

:Name: r_textureLODBias
:Values: Decimal
:Default: "0"
:Description:
   Adjust OpenGL texture Level of Detail bias. Useful for some low
   quality video drivers. Small negative values (eg "-0.2") can help
   with distant textures appearing blurry.

-----------
Server-Side
-----------

:Name: mv_apiConnectionless
:Valid: "0", "1"
:Default: "1"
:Description:
   Controls if game module may use MVAPI 1 to receive and send
   connectionless packets with arbitrary source and destination. When
   disabled SysCalls always return qtrue as if error occured.

..

:Name: mv_serverversion
:Valid: "auto", "1.04", "1.03", "1.02"
:Default: "1.04"
:Description:
   Decides which gameversion the server will run on. "auto" will host
   a 1.04 server if assets5.pk3 is found, 1.03 if assets2.pk3
   is available and if only assets0.pk3 and assets1.pk3 can be found
   it will host a 1.02 server. *Make sure you have only mods
   compatible with the hosted gameversion in your base/mod directory.
   The dedicated server expects you to know what you are doing.*

..

:Name: mv_httpdownloads
:Valid: "0", "1"
:Default: "0"
:Description:
   Switches http downloads on and off.

..

:Name: mv_httpserverport
:Valid: 0-65535 (TCP Port), Any URL (http://...)
:Default: "0"
:Description:
   If a number is provided it decides on which TCP port the builtin
   HTTP-Server will listen on. If set to zero it will automatically
   choose a port between 18200 and 18215, trying every single one till
   it finds an unused port. Make sure that this port is opened in your
   Firewall / NAT. Since JK2MV 1.1 external HTTP Servers are
   supported. The URL should point to the GameData directory of your
   file server. Note that clients also need at least JK2MV 1.1 in case
   you are using a URL. Older JK2MV versions will not detect the
   availability of HTTP Downloads in this case.

..

:Name: mv_fixnamecrash
:Valid: "0", "1"
:Default: "1"
:Description:
   Blocks the use of chars from the extended ASCII table which can
   cause a crash if used correctly.

..

:Name: mv_fixforcecrash
:Valid: "0", "1"
:Default: "1"
:Description:
   Blocks the use of malformed forceconfig strings which can cause a
   crash if used correctly.

..

:Name: mv_fixgalaking
:Valid: "0", "1"
:Default: "1"
:Description:
   Blocks the use of "galak_mech" as a playermodel on the serverside
   so legacy clients will not crash. Only useful in 1.02 mode.

..

:Name: mv_fixbrokenmodels
:Valid: "0", "1"
:Default: "1"
:Description:
   Blocks the use of "kyle/fpls" and "morgan" as a playermodel. These
   models have invisible parts and thus are some kind of ghosting.
   Only useful in 1.02 mode.

..

:Name: mv_fixturretcrash
:Valid: "0", "1"
:Default: "1"
:Description:
   Removes all blaster missiles from the game before hitting the
   engine limit to prevent players from crashing a server with the
   turret/sentry.

..

:Name: mv_blockchargejump
:Valid: "0", "1"
:Default: "1"
:Description:
   Blocks a hack which can be used to jump higher then normally
   possible.

..

:Name: mv_blockspeedhack
:Valid: "0", "1"
:Default: "1"
:Description:
   Blocks the speedhack which can be used to run faster.

..

:Name: mv_fixsaberstealing
:Valid: "0", "1"
:Default: "1"
:Description:
   Prevents spectators from stealing saber.

..

:Name: mv_fixplayerghosting
:Valid: "0", "1"
:Default: "1"
:Description:
   Prevents "player ghosting" bug, where players can freely walk
   through affected player.

..

:Name: mv_resetServerTime
:Valid: "0", "1", "2"
:Default: "1"
:Description:
   Reset internal server time on map restart. Helps to avoid high
   server time bugs. Breaks queue in duel gametype on basejk. May
   cause issues with other mods.

   | 0: Never (compatible)
   | 1: Always except in Duel gametype
   | 2: Always

..

:Name: sv_autoWhitelist
:Values: "0", "1"
:Default: "1"
:Description:
   Automatically add IPs of players to a whitelist. Whitelisted IPs
   are can still access the server while it's under a DOS attack and
   they are stored in ipwhitelist.dat file. Collecting IP addresses
   without consent may be against European Union's General Data
   Protection Regulation.

..

:Name: sv_enforceSnaps
:Values: "0", "1"
:Default: "0"
:Description:
   Ignore the client preference for "snaps" and try to send a snapshot per
   server frame (sv_fps) if sv_maxSnaps and the client rate permit it.

..

:Name: sv_floodProtect
:Values: Integer >= 0
:Default: "3"
:Description:
   | 0: Disable flood protection.
   | 1: Original flood protection - 1 client command per second.
   | 2+: Relaxed flood protection - Allow sv_floodProtect commands
   at once (burst), after this 1 command per second (rate).

..

:Name: sv_hibernateTime
:Values: Integer >= 0
:Default: "0"
:Description:
   Switches the server to a hibernation mode in which it
   uses less CPU power when no player is connected.
   The value is the time in milliseconds after which it automatically
   switches to the said state when the last player disconnected from the server.
   The value zero disables hibernation mode.

..

:Name: sv_hibernateFps
:Values: Integer >= 1
:Default: "5"
:Description:
   The fps to use while the server is in hibernation mode.

..

:Name: sv_maxOOBRate
:Valid: 1-1000
:Default: "20"
:Description:
   Max out-of-bound requests handled per second. Increasing rate
   improves server responsiveness at the cost of higher CPU usage.

..

:Name: sv_maxRate
:Valid: "0", Integer >= 1000
:Default: "90000"
:Description:
   Maximum rate for each client. The client rate limits the maximum amount of
   snapshots sent to a client.

..

:Name: sv_maxSnaps
:Valid: Integer > 0
:Default: "30"
:Description:
   Maximum amount of snapshots each client should receive. This can also be
   limited by the client rate.

..

:Name: sv_minRate
:Valid: Integer >= 1000
:Default: "1000"
:Description:
   Minimum rate for each client. The client rate limits the maximum amount of
   snapshots sent to a client.

..

:Name: sv_minSnaps
:Valid: Integer > 0
:Default: "1"
:Description:
   Minimum amount of snapshots each client should receive. This can also be
   limited by the client rate.

..

:Name: sv_pingFix
:Values: "0", "1"
:Default: "1"
:Description:
   Enable more accurate and bug-free ping calculation.

..

:Name: sv_dynamicSnapshots
:Values: "0", "1"
:Default: "1"
:Description:
   Try to send partial snapshots if a snapshot message would otherwise overflow.
   This should help to avoid clients from dropping due to
   ``CL_ParseServerMessage: read past end of server message`` when maps or mods
   cause a lot of commands to be sent to a client in a short interval on a busy
   server.

==================
Undocumented Cvars
==================

* com_maxfpsMinimized
* com_maxfpsUnfocused
* in_nograb
* mv_coloredTextShadows
* net_dropsim (dev cvar)
* net_enabled
* r_allowsoftwaregl
* r_convertModelBones
* r_loadSkinsJKA
* r_noborder
* r_centerWindow
* s_sdlBits
* s_sdlSpeed
* s_sdlChannels
* s_sdlDevSamps
* s_sdlMixSamps

=============
Other Changes
=============

* cl_avidemo replaced by cl_aviFrameRate
* cl_conspeed renamed to con_speed
