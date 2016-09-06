.. Keep this file in sync with wiki entries

======================
New and Modified Cvars
======================

:Name: com_busyWait
:Values: "0", "1"
:Default: "0"
:Description:
   Enable / Disable old "busy" game loop using 100% CPU time.

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
   Enable / Disable dynamic glow effect know from JK:JA.

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

==================
Undocumented Cvars
==================

* com_maxfpsMinimized
* com_maxfpsUnfocused
* in_nograb
* mv_apiEnabled (ROM)
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
