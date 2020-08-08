/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// gl_vidnt.c -- NT GL vid component

#include "quakedef.h"
#include "winquake.h"
#include "resource.h"
#include <commctrl.h>

#define MAX_MODE_LIST	600	// Baker 3.80X
#define VID_ROW_SIZE	3
#define WARP_WIDTH		320
#define WARP_HEIGHT		200
#define MAXWIDTH		10000
#define MAXHEIGHT		10000
#define BASEWIDTH		320
#define BASEHEIGHT		200

#define MODE_WINDOWED			0
#define NO_MODE					(MODE_WINDOWED - 1)
#define MODE_FULLSCREEN_DEFAULT	(MODE_WINDOWED + 1)

typedef struct {
	modestate_t	type;
	int			width;
	int			height;
	int			modenum;
	int			dib;
	int			fullscreen;
	int			bpp;
	int			refreshrate; //johnfitz
	int			halfscreen;
	char		modedesc[17];
} vmode_t;

typedef struct {
	int			width;
	int			height;
} lmode_t;

lmode_t	lowresmodes[] = {
	{320, 200},
	{320, 240},
	{400, 300},
	{512, 384},
};

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions;

qboolean		DDActive;
qboolean		scr_skipupdate;

static vmode_t	modelist[MAX_MODE_LIST];
static int		nummodes;
static vmode_t	*pcurrentmode;
static vmode_t	badmode;

static DEVMODE	gdevmode;
static qboolean	vid_initialized = false;
static qboolean	windowed, leavecurrentmode;
static qboolean vid_canalttab = false;
static qboolean vid_wassuspended = false;
static int		windowed_mouse;
extern qboolean	flex_mouseactive;  // from in_win.c
static HICON	hIcon;

int			DIBWidth, DIBHeight;
RECT		WindowRect;
DWORD		WindowStyle, ExWindowStyle;

HWND		mainwindow;
HWND		dibwindow;

int			vid_modenum = NO_MODE;
int			vid_realmode;
int			vid_default = MODE_WINDOWED;
static int	windowed_default;
unsigned char	vid_curpal[256*3];
static qboolean fullsbardraw = false;

static float vid_gamma = 1.0;

//HGLRC	baseRC;
HDC		maindc;

glvert_t glv;

cvar_t	gl_ztrick = {"gl_ztrick","1"};

HWND WINAPI InitializeWindow (HINSTANCE hInstance, int nCmdShow);

modestate_t	modestate = MS_UNINIT;
viddef_t	vid;				// global video state

//unsigned short	d_8to16table[256];
unsigned	d_8to24table[256];
unsigned char d_15to8table[65536];

float		gldepthmin, gldepthmax;


void VID_Menu_Init (void); //johnfitz
void VID_Menu_f (void); //johnfitz
void VID_MenuDraw (void);
void VID_MenuKey (int key);

LONG WINAPI MainWndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void AppActivate(BOOL fActive, BOOL minimize);
void Key_ClearAllStates (void);
char *VID_GetModeDescription (int mode);
void VID_UpdateWindowStatus (void);
void GL_Init (void);

PROC glArrayElementEXT;
PROC glColorPointerEXT;
PROC glTexCoordPointerEXT;
PROC glVertexPointerEXT;

typedef void (APIENTRY *lp3DFXFUNC) (int, int, int, int, int, const void*);
lp3DFXFUNC glColorTableEXT;
qboolean is8bit = false;
qboolean isPermedia = false;
qboolean gl_mtexable = false;
int			vid_current_bpp;//R00k
qboolean	vid_locked = false; //johnfitz
void GL_SetupState (void); //johnfitz

//====================================

//johnfitz -- new cvars
//cvar_t		vid_fullscreen = {"vid_fullscreen", "1", true};
cvar_t		vid_width = {"vid_width", "640", true};
cvar_t		vid_height = {"vid_height", "480", true};
//cvar_t		vid_bpp = {"vid_bpp", "16", true};
cvar_t		vid_refreshrate = {"vid_refreshrate", "60", true};
//johnfitz


cvar_t		vid_mode = {"vid_mode","0", false};
// Note that 0 is MODE_WINDOWED
cvar_t		_vid_default_mode = {"_vid_default_mode","0", true};
// Note that 3 is MODE_FULLSCREEN_DEFAULT
cvar_t		_vid_default_mode_win = {"_vid_default_mode_win","3", true};
cvar_t		vid_wait = {"vid_wait","0"};
cvar_t		vid_nopageflip = {"vid_nopageflip","0", true};
cvar_t		_vid_wait_override = {"_vid_wait_override", "0", true};
cvar_t		vid_config_x = {"vid_config_x","800", true};
cvar_t		vid_config_y = {"vid_config_y","600", true};
cvar_t		vid_stretch_by_2 = {"vid_stretch_by_2","1", true};
cvar_t		_windowed_mouse = {"_windowed_mouse","1", true};
extern	cvar_t	cl_confirmquit; // Baker 3.60

int			window_center_x, window_center_y, window_x, window_y, window_width, window_height;
RECT		window_rect;

typedef BOOL (APIENTRY *SWAPINTERVALFUNCPTR)(int);
SWAPINTERVALFUNCPTR wglSwapIntervalEXT = NULL;
//qboolean OnChange_vid_vsync (cvar_t *var, char *string);
static qboolean update_vsync = false;
cvar_t		vid_vsync = {"vid_vsync", "1", true, 3};

//static void Check_Gamma (unsigned char *pal);
//qboolean OnChange_vid_gamma (cvar_t *var, char *string);
//cvar_t		vid_gamma = {"vid_gamma", "1", true, 4};



/******************************* GL EXTENSIONS *******************************/

qboolean CheckExtension (const char *extension) {
	char		*where, *terminator;
	const	char	*start;

	if (!gl_extensions && !(gl_extensions = glGetString(GL_EXTENSIONS)))
		return false;

	if (!extension || *extension == 0 || strchr(extension, ' '))
		return false;

	for (start = gl_extensions ; where = strstr(start, extension) ; start = terminator) {
		terminator = where + strlen(extension);
		if ((where == start || *(where - 1) == ' ') && (*terminator == 0 || *terminator == ' '))
			return true;
	}

	return false;
}

void VID_Vsync_f (void) {
	update_vsync = true;
	//return false;
}

void CheckVsyncControlExtensions (void) {
	if (!COM_CheckParm("-noswapctrl") && CheckExtension("WGL_EXT_swap_control")) {
		if ((wglSwapIntervalEXT = (void *)wglGetProcAddress("wglSwapIntervalEXT"))) {
			Con_Printf("Vsync control extensions found\n");
			Cvar_RegisterVariable (&vid_vsync, VID_Vsync_f);
		}
	}
}


// direct draw software compatability stuff

void VID_HandlePause (qboolean pause) {
	// Baker: Leave this comment here
}

void VID_ForceLockState (int lk) {
}

void VID_LockBuffer (void) {
	// Baker: Leave this comment here
}

void VID_UnlockBuffer (void) {
	// Baker: Leave this comment here
}

int VID_ForceUnlockedAndReturnState (void)
{
	return 0;
}

void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height) {
	// Baker: Leave this comment here
}

void D_EndDirectRect (int x, int y, int width, int height) {
	// Baker: Leave this comment here
}


void CenterWindow(HWND hWndCenter, int width, int height, BOOL lefttopjustify) {
    int     CenterX, CenterY;

	CenterX = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
	CenterY = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
	if (CenterX > CenterY*2)
		CenterX >>= 1;	// dual screens
	CenterX = (CenterX < 0) ? 0: CenterX;
	CenterY = (CenterY < 0) ? 0: CenterY;
	SetWindowPos (hWndCenter, NULL, CenterX, CenterY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_DRAWFRAME);
}

void VID_UpdateWindowStatus (void) {

	window_rect.left = window_x;
	window_rect.top = window_y;
	window_rect.right = window_x + window_width;
	window_rect.bottom = window_y + window_height;
	window_center_x = (window_rect.left + window_rect.right) / 2;
	window_center_y = (window_rect.top + window_rect.bottom) / 2;

	IN_UpdateClipCursor ();
}

/******************************** VID_SETMODE ********************************/

qboolean VID_SetWindowedMode (int modenum) {
	HDC				hdc;
	int				lastmodestate, width, height;
	RECT			rect;

	lastmodestate = modestate;

	WindowRect.top = WindowRect.left = 0;

	WindowRect.right = modelist[modenum].width;
	WindowRect.bottom = modelist[modenum].height;

	DIBWidth = modelist[modenum].width;
	DIBHeight = modelist[modenum].height;
	// Baker 3.60: full-screeen-windowed from aguirRe's engine
	if (DIBWidth == GetSystemMetrics(SM_CXSCREEN) && DIBHeight == GetSystemMetrics(SM_CYSCREEN))
		WindowStyle = WS_POPUP; // Window covers entire screen; no caption, borders etc
	else
	WindowStyle = WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_SYSMENU |
				  WS_MINIMIZEBOX;
	ExWindowStyle = 0;

	rect = WindowRect;
	AdjustWindowRectEx(&rect, WindowStyle, FALSE, 0);

	width = rect.right - rect.left;
	height = rect.bottom - rect.top;

	// Create the DIB window
	dibwindow = CreateWindowEx (
		 ExWindowStyle,
		 "WinQuake",
		 "GLQuake",
		 WindowStyle,
		 rect.left, rect.top,
		 width,
		 height,
		 NULL,
		 NULL,
		 global_hInstance,
		 NULL);

	if (!dibwindow)
		Sys_Error ("Couldn't create DIB window");

	// Center and show the DIB window
	CenterWindow(dibwindow, WindowRect.right - WindowRect.left, WindowRect.bottom - WindowRect.top, false);

	ShowWindow (dibwindow, SW_SHOWDEFAULT);
	UpdateWindow (dibwindow);

	modestate = MS_WINDOWED;

	// Because we have set the background brush for the window to NULL (to avoid flickering when re-sizing the window on the desktop),
	// we clear the window to black when created, otherwise it will be  empty while Quake starts up.
	hdc = GetDC(dibwindow);
	PatBlt(hdc,0,0,WindowRect.right,WindowRect.bottom,BLACKNESS);
	ReleaseDC(dibwindow, hdc);

/*	if (vid.conheight > modelist[modenum].height)
		vid.conheight = modelist[modenum].height;
	if (vid.conwidth > modelist[modenum].width)
		vid.conwidth = modelist[modenum].width; */

	vid.conwidth = vid.width & 0xFFFFFFF8;
	vid.conheight = vid.conwidth * vid.height / vid.width;

//	vid.width = vid.conwidth;
//  vid.height = vid.conheight;

	vid.numpages = 2;

	mainwindow = dibwindow;

	SendMessage (mainwindow, WM_SETICON, (WPARAM)TRUE, (LPARAM)hIcon);
	SendMessage (mainwindow, WM_SETICON, (WPARAM)FALSE, (LPARAM)hIcon);

	return true;
}

/*
================
VID_SetFullDIBMode
================
*/
qboolean VID_SetFullDIBMode (int modenum) {
	HDC				hdc;
	int				lastmodestate;
	int				width, height;
	RECT			rect;

	if (!leavecurrentmode) {
		gdevmode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
		gdevmode.dmBitsPerPel = modelist[modenum].bpp;
		gdevmode.dmPelsWidth = modelist[modenum].width << modelist[modenum].halfscreen;
		gdevmode.dmPelsHeight = modelist[modenum].height;
		gdevmode.dmSize = sizeof (gdevmode);

		if (vid_refreshrate.value > 59) 
		{
			gdevmode.dmDisplayFrequency = modelist[modenum].refreshrate = vid_refreshrate.value;
			gdevmode.dmFields |= DM_DISPLAYFREQUENCY;			
		}
		else
			gdevmode.dmDisplayFrequency = modelist[modenum].refreshrate; 

		if (ChangeDisplaySettings (&gdevmode, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
			Sys_Error ("Couldn't set fullscreen DIB mode");
	}

	lastmodestate = modestate;
	modestate = MS_FULLDIB;

	WindowRect.top = WindowRect.left = 0;

	WindowRect.right = modelist[modenum].width;
	WindowRect.bottom = modelist[modenum].height;

	DIBWidth = modelist[modenum].width;
	DIBHeight = modelist[modenum].height;

	WindowStyle = WS_POPUP;
	ExWindowStyle = 0;

	rect = WindowRect;
	AdjustWindowRectEx(&rect, WindowStyle, FALSE, 0);

	width = rect.right - rect.left;
	height = rect.bottom - rect.top;

	// Create the DIB window
	dibwindow = CreateWindowEx (
		 ExWindowStyle,
		 "WinQuake",
		 "GLQuake",
		 WindowStyle,
		 rect.left, rect.top,
		 width,
		 height,
		 NULL,
		 NULL,
		 global_hInstance,
		 NULL);

	if (!dibwindow)
		Sys_Error ("Couldn't create DIB window");

	ShowWindow (dibwindow, SW_SHOWDEFAULT);
	UpdateWindow (dibwindow);

	// Because we have set the background brush for the window to NULL (to avoid flickering when re-sizing the window on the desktop),
	// we clear the window to black when created, otherwise it will be  empty while Quake starts up.
	hdc = GetDC(dibwindow);
	PatBlt(hdc,0,0,WindowRect.right,WindowRect.bottom,BLACKNESS);
	ReleaseDC(dibwindow, hdc);

/*	if (vid.conheight > modelist[modenum].height)
		vid.conheight = modelist[modenum].height;
	if (vid.conwidth > modelist[modenum].width)
		vid.conwidth = modelist[modenum].width; */
	vid.width = modelist[modenum].width;
	vid.height = modelist[modenum].height;
	vid.conwidth = vid.width & 0xFFFFFFF8;
	vid.conheight = vid.conwidth * vid.height / vid.width;

	vid.numpages = 2;

// needed because we're not getting WM_MOVE messages fullscreen on NT
	window_x = window_y = 0;

	mainwindow = dibwindow;

	SendMessage (mainwindow, WM_SETICON, (WPARAM)TRUE, (LPARAM)hIcon);
	SendMessage (mainwindow, WM_SETICON, (WPARAM)FALSE, (LPARAM)hIcon);

	return true;
}

extern void IN_StartupMouse (void);

int VID_SetMode (int modenum, unsigned char *palette) {
	int				original_mode, temp;
	qboolean		stat;
    MSG				msg;

	if ((windowed && (modenum != 0)) || (!windowed && (modenum < 1)) || (!windowed && (modenum >= nummodes))) 
		Sys_Error ("Bad video mode");

// so Con_Printfs don't mess us up by forcing vid and snd updates
	temp = scr_disabled_for_loading;
	scr_disabled_for_loading = true;

	CDAudio_Pause ();

	if (vid_modenum == NO_MODE)
		original_mode = windowed_default;
	else
		original_mode = vid_modenum;

	// Set either the fullscreen or windowed mode
	if (modelist[modenum].type == MS_WINDOWED) {
		if (_windowed_mouse.value && key_dest == key_game) {
			stat = VID_SetWindowedMode(modenum);
			IN_ActivateMouse ();
			IN_HideMouse ();
		} else {
			IN_DeactivateMouse ();
			IN_ShowMouse ();
			stat = VID_SetWindowedMode(modenum);
		}
	} else if (modelist[modenum].type == MS_FULLDIB) {
		stat = VID_SetFullDIBMode(modenum);
		IN_ActivateMouse ();
		IN_HideMouse ();
	} else {
		Sys_Error ("VID_SetMode: Bad mode type in modelist");
	}

	window_width = DIBWidth;
	window_height = DIBHeight;
	VID_UpdateWindowStatus ();

	CDAudio_Resume ();
	scr_disabled_for_loading = temp;

	if (!stat) {
		Sys_Error ("Couldn't set video mode");
	}


	// now we try to make sure we get the focus on the mode switch, because sometimes in some systems we don't.
	// We grab the foreground, then finish setting up, pump all our messages, and sleep for a little while
	// to let messages finish bouncing around the system, then we put ourselves at the top of the z order,
	// then grab the foreground again. Who knows if it helps, but it probably doesn't hurt

	SetForegroundWindow (mainwindow);
	VID_SetPalette (palette);
	vid_modenum = modenum;
//	Cvar_SetValue ("vid_mode", (float)vid_modenum);



	while (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE)) {
      	TranslateMessage (&msg);
      	DispatchMessage (&msg);
	}


	Sleep (100);

	SetWindowPos (mainwindow, HWND_TOP, 0, 0, 0, 0, SWP_DRAWFRAME | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOCOPYBITS);

	SetForegroundWindow (mainwindow);

// fix the leftover Alt from any Alt-Tab or the like that switched us away
	Key_ClearAllStates ();

	if (!msg_suppress_1)
		Con_SafePrintf ("Video mode %s initialized.\n", VID_GetModeDescription (vid_modenum));

	VID_SetPalette (palette);

	vid.recalc_refdef = 1;

	//R00k mouse died on mode change
	IN_StartupMouse ();

	return true;
}

/*
===================
VID_Restart -- johnfitz -- change video modes on the fly
===================
*/

void VID_Restart (void)
{
	HDC			hdc;
	HGLRC		hrc;
	int			i;
	qboolean	mode_changed = false;
	vmode_t		oldmode;
	qboolean	vidfullscreen = true;

	if (vid_locked)
		return;

//
// check cvars against current mode
//
	if (vidfullscreen)
	{
		if (modelist[vid_default].type == MS_WINDOWED)
			mode_changed = true;
/*		else if (modelist[vid_default].bpp != (int)vid_bpp.value)
			mode_changed = true; */
		else if (modelist[vid_default].refreshrate != (int)vid_refreshrate.value)
			mode_changed = true;
	}
	else
		if (modelist[vid_default].type != MS_WINDOWED)
			mode_changed = true;

	if (modelist[vid_default].width != (int)vid_width.value ||
		modelist[vid_default].height != (int)vid_height.value)
		mode_changed = true;

	if (mode_changed)
	{
//
// decide which mode to set
//
		oldmode = modelist[vid_default];

		if (vidfullscreen)
		{
			for (i=1; i<nummodes; i++)
			{
				if (modelist[i].width == (int)vid_width.value &&
					modelist[i].height == (int)vid_height.value &&
//					modelist[i].bpp == (int)vid_bpp.value &&
					modelist[i].refreshrate == (int)vid_refreshrate.value)
				{
					break;
				}
			}

			if (i == nummodes)
			{
				Con_Printf ("%dx%dx%d %dHz is not a valid fullscreen mode\n",
							(int)vid_width.value,
							(int)vid_height.value,
//							(int)vid_bpp.value,
							(int)vid_refreshrate.value);
				return;
			}

			windowed = false;
			vid_default = i;
		}
		else //not fullscreen
		{
			hdc = GetDC (NULL);
			if (GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE)
			{
				Con_Printf ("Can't run windowed on non-RGB desktop\n");
				ReleaseDC (NULL, hdc);
				return;
			}
			ReleaseDC (NULL, hdc);

			if (vid_width.value < 320)
			{
				Con_Printf ("Window width can't be less than 320\n");
				return;
			}

			if (vid_height.value < 200)
			{
				Con_Printf ("Window height can't be less than 200\n");
				return;
			}

			modelist[0].width = (int)vid_width.value;
			modelist[0].height = (int)vid_height.value;
			sprintf (modelist[0].modedesc, "%dx%dx%d %dHz",
					 modelist[0].width,
					 modelist[0].height,
					 modelist[0].bpp,
					 modelist[0].refreshrate);

			windowed = true;
			vid_default = 0;
		}
//
// destroy current window
//
		hrc = wglGetCurrentContext();
		hdc = wglGetCurrentDC();
		wglMakeCurrent(NULL, NULL);

		vid_canalttab = false;

		if (hdc && dibwindow)
			ReleaseDC (dibwindow, hdc);
		if (modestate == MS_FULLDIB)
			ChangeDisplaySettings (NULL, 0);
		if (maindc && dibwindow)
			ReleaseDC (dibwindow, maindc);
		maindc = NULL;
		if (dibwindow)
			DestroyWindow (dibwindow);
//
// set new mode
//
		VID_SetMode (vid_default, host_basepal);
		maindc = GetDC(mainwindow);
		bSetupPixelFormat(maindc);

		// if bpp changes, recreate render context and reload textures
		if (modelist[vid_default].bpp != oldmode.bpp)
		{
			wglDeleteContext (hrc);
			hrc = wglCreateContext (maindc);
			if (!wglMakeCurrent (maindc, hrc))
				Sys_Error ("VID_Restart: wglMakeCurrent failed");
			//Bakertest: TexMgr_ReloadImages ();
			GL_SetupState ();
		}
		else
			if (!wglMakeCurrent (maindc, hrc))
#if 1
			{
				char szBuf[80];
				LPVOID lpMsgBuf;
				DWORD dw = GetLastError();
				FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &lpMsgBuf, 0, NULL );
				sprintf(szBuf, "VID_Restart: wglMakeCurrent failed with error %d: %s", dw, lpMsgBuf);
 				Sys_Error (szBuf);
			}
#else
				Sys_Error ("VID_Restart: wglMakeCurrent failed");
#endif

		vid_canalttab = true;

		//swapcontrol settings were lost when previous window was destroyed
		if (vid_vsync.value) {
			Cvar_Set("vid_vsync", "0");
			Cvar_Set("vid_vsync", "1");
		} else {
			Cvar_Set("vid_vsync", "1");
			Cvar_Set("vid_vsync", "0");
		}

		//warpimages needs to be recalculated
		// Bakertest TexMgr_RecalcWarpImageSize ();

		//conwidth and conheight need to be recalculated
		
		//vid.conwidth = (vid_conwidth.value > 0) ? (int)vid_conwidth.value : vid.width;
		//vid.conwidth = CLAMP (320, vid.conwidth, vid.width);
		//vid.conwidth &= 0xFFFFFFF8;
		//vid.conheight = vid.conwidth * vid.height / vid.width;
		
		
	}
//
// keep cvars in line with actual mode
//
	Cvar_Set ("vid_width", va("%i", modelist[vid_default].width));
	Cvar_Set ("vid_height", va("%i", modelist[vid_default].height));
//	Cvar_Set ("vid_bpp", va("%i", modelist[vid_default].bpp));
	Cvar_Set ("vid_refreshrate", va("%i", modelist[vid_default].refreshrate));
//	Cvar_Set ("vid_fullscreen", (windowed) ? "0" : "1");

	VID_Conwidth_f(); // Force recalc
//	Cvar_SetValue("vid_conwidth", vid.width);
//	Cvar_SetValue("vid_conheight", vid.conheight);

}

/*
================
VID_Test -- johnfitz -- like vid_restart, but asks for confirmation after switching modes
================
*/
void VID_Test (void)
{
	vmode_t oldmode;
	qboolean	mode_changed = false;
	qboolean	vidfullscreen=true;
	if (vid_locked)
		return;
//
// check cvars against current mode
//
	if (vidfullscreen)
	{
		if (modelist[vid_default].type == MS_WINDOWED)
			mode_changed = true;
/*		else if (modelist[vid_default].bpp != (int)vid_bpp.value)
			mode_changed = true; */
		else if (modelist[vid_default].refreshrate != (int)vid_refreshrate.value)
			mode_changed = true;
	}
	else
		if (modelist[vid_default].type != MS_WINDOWED)
			mode_changed = true;

	if (modelist[vid_default].width != (int)vid_width.value ||
		modelist[vid_default].height != (int)vid_height.value)
		mode_changed = true;

	if (!mode_changed)
		return;
//
// now try the switch
//
	oldmode = modelist[vid_default];

	VID_Restart ();

	//pop up confirmation dialoge
	if (!SCR_ModalMessage("Would you like to keep this\nvideo mode? (y/n)\n", 5.0f))
	{
		//revert cvars and mode
		Cvar_Set ("vid_width", va("%i", oldmode.width));
		Cvar_Set ("vid_height", va("%i", oldmode.height));
//		Cvar_Set ("vid_bpp", va("%i", oldmode.bpp));
		Cvar_Set ("vid_refreshrate", va("%i", oldmode.refreshrate));
//		Cvar_Set ("vid_fullscreen", (oldmode.type == MS_WINDOWED) ? "0" : "1");
		VID_Restart ();
	}
}

/*
================
VID_Unlock -- johnfitz
================
*/
void VID_Unlock (void)
{
	vid_locked = false;

	//sync up cvars in case they were changed during the lock
	Cvar_Set ("vid_width", va("%i", modelist[vid_default].width));
	Cvar_Set ("vid_height", va("%i", modelist[vid_default].height));
//	Cvar_Set ("vid_bpp", va("%i", modelist[vid_default].bpp));
	Cvar_Set ("vid_refreshrate", va("%i", modelist[vid_default].refreshrate));
//	Cvar_Set ("vid_fullscreen", (windowed) ? "0" : "1");
}





//====================================

BINDTEXFUNCPTR bindTexFunc;

#define TEXTURE_EXT_STRING "GL_EXT_texture_object"


void CheckTextureExtensions (void)
{
	char		*tmp;
	qboolean	texture_ext;
	HINSTANCE	hInstGL;

	texture_ext = FALSE;
	/* check for texture extension */
	tmp = (unsigned char *)glGetString(GL_EXTENSIONS);
	while (*tmp)
	{
		if (strncmp((const char*)tmp, TEXTURE_EXT_STRING, strlen(TEXTURE_EXT_STRING)) == 0)
			texture_ext = TRUE;
		tmp++;
	}

	if (!texture_ext || COM_CheckParm ("-gl11") )
	{
		hInstGL = LoadLibrary("opengl32.dll");
		
		if (hInstGL == NULL)
			Sys_Error ("Couldn't load opengl32.dll\n");

		bindTexFunc = (void *)GetProcAddress(hInstGL,"glBindTexture");

		if (!bindTexFunc)
			Sys_Error ("No texture objects!");
		return;
	}

/* load library and get procedure adresses for texture extension API */
	if ((bindTexFunc = (BINDTEXFUNCPTR)
		wglGetProcAddress((LPCSTR) "glBindTextureEXT")) == NULL)
	{
		Sys_Error ("GetProcAddress for BindTextureEXT failed");
		return;
	}
}

void CheckArrayExtensions (void)
{
	char		*tmp;

	/* check for texture extension */
	tmp = (unsigned char *)glGetString(GL_EXTENSIONS);
	while (*tmp)
	{
		if (strncmp((const char*)tmp, "GL_EXT_vertex_array", strlen("GL_EXT_vertex_array")) == 0)
		{
			if (
((glArrayElementEXT = wglGetProcAddress("glArrayElementEXT")) == NULL) ||
((glColorPointerEXT = wglGetProcAddress("glColorPointerEXT")) == NULL) ||
((glTexCoordPointerEXT = wglGetProcAddress("glTexCoordPointerEXT")) == NULL) ||
((glVertexPointerEXT = wglGetProcAddress("glVertexPointerEXT")) == NULL) )
			{
				Sys_Error ("GetProcAddress for vertex extension failed");
				return;
			}
			return;
		}
		tmp++;
	}

	Sys_Error ("Vertex array extension not present");
}

//int		texture_mode = GL_NEAREST;
//int		texture_mode = GL_NEAREST_MIPMAP_NEAREST;
//int		texture_mode = GL_NEAREST_MIPMAP_LINEAR;
int		texture_mode = GL_LINEAR;
//int		texture_mode = GL_LINEAR_MIPMAP_NEAREST;
//int		texture_mode = GL_LINEAR_MIPMAP_LINEAR;

int		texture_extension_number = 1;

#ifdef _WIN32
void CheckMultiTextureExtensions(void) 
{
	if (strstr(gl_extensions, "GL_SGIS_multitexture ") && !COM_CheckParm("-nomtex")) {
		Con_Printf("Multitexture extensions found.\n");
		qglMTexCoord2fSGIS = (void *) wglGetProcAddress("glMTexCoord2fSGIS");
		qglSelectTextureSGIS = (void *) wglGetProcAddress("glSelectTextureSGIS");
		gl_mtexable = true;
	}
}
#else
void CheckMultiTextureExtensions(void) 
{
		gl_mtexable = true;
}
#endif

/*
===============
GL_SetupState -- johnfitz

does all the stuff from GL_Init that needs to be done every time a new GL render context is created
GL_Init will still do the stuff that only needs to be done once
===============
*/
void GL_SetupState (void) {
	glClearColor (0.15,0.15,0.15,0);  // Baker 3.60 - set to same clear color as FitzQuake for void areas
	glCullFace(GL_FRONT);
	glEnable(GL_TEXTURE_2D);

	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.666);

	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel (GL_FLAT);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

//	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

}


/*
===============
GL_Init
===============
*/
void GL_Init (void)
{
	gl_vendor = glGetString (GL_VENDOR);
	Con_Printf ("GL_VENDOR: %s\n", gl_vendor);
	gl_renderer = glGetString (GL_RENDERER);
	Con_Printf ("GL_RENDERER: %s\n", gl_renderer);

	gl_version = glGetString (GL_VERSION);
	Con_Printf ("GL_VERSION: %s\n", gl_version);
	gl_extensions = glGetString (GL_EXTENSIONS);
//	Con_Printf ("GL_EXTENSIONS: %s\n", gl_extensions); // Baker 3.62 commented out on advice of aguirRe

//	Con_Printf ("%s %s\n", gl_renderer, gl_version);

    if (strnicmp(gl_renderer,"PowerVR",7)==0)
         fullsbardraw = true;

    if (strnicmp(gl_renderer,"Permedia",8)==0)
         isPermedia = true;

	CheckTextureExtensions ();
	CheckMultiTextureExtensions ();
    // Register cvar vid_vsync


#if 0
	CheckArrayExtensions ();

	glEnable (GL_VERTEX_ARRAY_EXT);
	glEnable (GL_TEXTURE_COORD_ARRAY_EXT);
	glVertexPointerEXT (3, GL_FLOAT, 0, 0, &glv.x);
	glTexCoordPointerEXT (2, GL_FLOAT, 0, 0, &glv.s);
	glColorPointerEXT (3, GL_FLOAT, 0, 0, &glv.r);
#endif

	GL_SetupState (); //johnfitz
}


void GL_BeginRendering (int *x, int *y, int *width, int *height) {
	*x = *y = 0;
	*width = WindowRect.right - WindowRect.left;
	*height = WindowRect.bottom - WindowRect.top;
}


void GL_EndRendering (void) {
	if (!scr_skipupdate || block_drawing) {

		if (wglSwapIntervalEXT && update_vsync && vid_vsync.string[0])
			wglSwapIntervalEXT (vid_vsync.value ? 1 : 0);
		update_vsync = false;

		SwapBuffers (maindc);
	}

// handle the mouse state when windowed if that's changed
	if (modestate == MS_WINDOWED) {
		if (!_windowed_mouse.value) {
			if (windowed_mouse)	{
				IN_DeactivateMouse ();
				IN_ShowMouse ();
				windowed_mouse = false;
			}
		} else {
			windowed_mouse = true;
			if (key_dest == key_game && !flex_mouseactive && ActiveApp) {
				IN_ActivateMouse ();
				IN_HideMouse ();
			} else if (flex_mouseactive && key_dest != key_game) {
				IN_DeactivateMouse ();
				IN_ShowMouse ();
			}
		}
	}
	if (fullsbardraw)
		Sbar_Changed();
}

void VID_SetDefaultMode (void) 
{
	IN_DeactivateMouse ();
}

void	VID_SetPalette (unsigned char *palette)
{
	byte	*pal;
	unsigned r,g,b;
	unsigned v;
	int     r1,g1,b1;
	int		j,k,l;
	unsigned short i;
	unsigned	*table;

//
// 8 8 8 encoding
//
	pal = palette;
	table = d_8to24table;
	for (i=0 ; i<256 ; i++)
	{
		r = pal[0];
		g = pal[1];
		b = pal[2];
		pal += 3;
		
//		v = (255<<24) + (r<<16) + (g<<8) + (b<<0);
//		v = (255<<0) + (r<<8) + (g<<16) + (b<<24);
		v = (255<<24) + (r<<0) + (g<<8) + (b<<16);
		*table++ = v;
	}
	d_8to24table[255] &= 0xffffff;	// 255 is transparent

	// JACK: 3D distance calcs - k is last closest, l is the distance.
	// FIXME: Precalculate this and cache to disk.
	for (i=0; i < (1<<15); i++) {
		/* Maps
			000000000000000
			000000000011111 = Red  = 0x1F
			000001111100000 = Blue = 0x03E0
			111110000000000 = Grn  = 0x7C00
		*/
		r = ((i & 0x1F) << 3)+4;
		g = ((i & 0x03E0) >> 2)+4;
		b = ((i & 0x7C00) >> 7)+4;
		pal = (unsigned char *)d_8to24table;
		for (v=0,k=0,l=10000*10000; v<256; v++,pal+=4) {
			r1 = r-pal[0];
			g1 = g-pal[1];
			b1 = b-pal[2];
			j = (r1*r1)+(g1*g1)+(b1*b1);
			if (j<l) {
				k=v;
				l=j;
			}
		}
		d_15to8table[i]=k;
	}
}

BOOL	gammaworks;

void	VID_ShiftPalette (unsigned char *palette)
{
	extern	byte ramps[3][256];
	
//	VID_SetPalette (palette);

//	gammaworks = SetDeviceGammaRamp (maindc, ramps);
}


/******************************** VID SHUTDOWN ********************************/

void	VID_Shutdown (void) {
   	HGLRC hRC;
   	HDC	  hDC;

	if (vid_initialized) {
		vid_canalttab = false;
		hRC = wglGetCurrentContext();
    	hDC = wglGetCurrentDC();

    	wglMakeCurrent(NULL, NULL);

    	if (hRC)
    	    wglDeleteContext(hRC);

		if (hDC && dibwindow)
			ReleaseDC(dibwindow, hDC);

		if (modestate == MS_FULLDIB)
			ChangeDisplaySettings (NULL, 0);

		if (maindc && dibwindow)
			ReleaseDC (dibwindow, maindc);

		AppActivate(false, false);
	}
}


//==========================================================================


BOOL bSetupPixelFormat(HDC hDC)
{
    static PIXELFORMATDESCRIPTOR pfd = {
	sizeof(PIXELFORMATDESCRIPTOR),	// size of this pfd
	1,				// version number
	PFD_DRAW_TO_WINDOW |	// support window
	PFD_SUPPORT_OPENGL |	// support OpenGL
	PFD_DOUBLEBUFFER,		// double buffered
	PFD_TYPE_RGBA,			// RGBA type
	24,				// 24-bit color depth
	0, 0, 0, 0, 0, 0,		// color bits ignored
	0,				// no alpha buffer
	0,				// shift bit ignored
	0,				// no accumulation buffer
	0, 0, 0, 0, 			// accum bits ignored
	32,				// 32-bit z-buffer	
	0,				// no stencil buffer
	0,				// no auxiliary buffer
	PFD_MAIN_PLANE,			// main layer
	0,				// reserved
	0, 0, 0				// layer masks ignored
    };
    int pixelformat;

    if ( (pixelformat = ChoosePixelFormat(hDC, &pfd)) == 0 )
    {
        MessageBox(NULL, "ChoosePixelFormat failed", "Error", MB_OK);
        return FALSE;
    }

    if (SetPixelFormat(hDC, pixelformat, &pfd) == FALSE)
    {
        MessageBox(NULL, "SetPixelFormat failed", "Error", MB_OK);
        return FALSE;
    }

    return TRUE;
}


/*
===================================================================

MAIN WINDOW

===================================================================
*/

/************************ SIGNALS, MESSAGES, INPUT ETC ************************/

void AppActivate(BOOL fActive, BOOL minimize) {
/****************************************************************************
*
* Function:     AppActivate
* Parameters:   fActive - True if app is activating
*
* Description:  If the application is activating, then swap the system
*               into SYSPAL_NOSTATIC mode so that our palettes will display
*               correctly.
*
****************************************************************************/

	static BOOL	sound_active;

	ActiveApp = fActive;
	Minimized = minimize;

// enable/disable sound on focus gain/loss
	if (!ActiveApp && sound_active) {
		S_BlockSound ();
		sound_active = false;
	} else if (ActiveApp && !sound_active) {
		S_UnblockSound ();
		sound_active = true;
	}

	if (fActive) {
		if (modestate == MS_FULLDIB) {
			IN_ActivateMouse ();
			IN_HideMouse ();
			if (vid_canalttab && vid_wassuspended) {
				vid_wassuspended = false;
				ChangeDisplaySettings (&gdevmode, CDS_FULLSCREEN);
				ShowWindow(mainwindow, SW_SHOWNORMAL);
				
				// Baker 3.80x - Fix for alt-tab bug in NVidia drivers  from JoeQuake
                MoveWindow (mainwindow, 0, 0, gdevmode.dmPelsWidth, gdevmode.dmPelsHeight, false);
			}
		} else if ((modestate == MS_WINDOWED) && _windowed_mouse.value && key_dest == key_game) {
			IN_ActivateMouse ();
			IN_HideMouse ();
		}
	}

	if (!fActive) {
		if (modestate == MS_FULLDIB) {
			IN_DeactivateMouse ();
			IN_ShowMouse ();
			if (vid_canalttab) { 
				ChangeDisplaySettings (NULL, 0);
				vid_wassuspended = true;
			}
		} else if ((modestate == MS_WINDOWED) && _windowed_mouse.value) {
			IN_DeactivateMouse ();
			IN_ShowMouse ();
		}
	}
}

int IN_MapKey (int key);

/* main window procedure */
LONG WINAPI MainWndProc (
    HWND    hWnd,
    UINT    uMsg,
    WPARAM  wParam,
    LPARAM  lParam)
{
    LONG    lRet = 1;
	int		fActive, fMinimized, temp;
	extern unsigned int uiWheelMessage;
	char	state[256];
	char	asciichar[4];
	int		vkey;
	int		charlength;
	qboolean down = false;

	if ( uMsg == uiWheelMessage ) {
		uMsg = WM_MOUSEWHEEL;
		wParam <<= 16; // Baker 3.80x - Added this, purpose unknown
	}

    switch (uMsg) {
		// JACK: This is the mouse wheel with the Intellimouse
		// Its delta is either positive or neg, and we generate the proper Event.
		case WM_MOUSEWHEEL: 
			if ((short) HIWORD(wParam) > 0) {
				Key_Event(K_MWHEELUP, 0, true);
				Key_Event(K_MWHEELUP, 0, false);
			} else {
				Key_Event(K_MWHEELDOWN, 0, true);
				Key_Event(K_MWHEELDOWN, 0, false);
			}
			break;
		case WM_KILLFOCUS:
			if (modestate == MS_FULLDIB)
				ShowWindow(mainwindow, SW_SHOWMINNOACTIVE);
			break;

		case WM_CREATE:
			break;

		case WM_MOVE:
			window_x = (int) LOWORD(lParam);
			window_y = (int) HIWORD(lParam);
			VID_UpdateWindowStatus ();
			break;


		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			down=true;
			
		case WM_KEYUP:
		case WM_SYSKEYUP:
			// Baker 3.703 - old way
			// Key_Event (IN_MapKey(lParam), false);
			vkey = IN_MapKey(lParam);
				GetKeyboardState (state);
				// alt/ctrl/shift tend to produce funky ToAscii values,
				// and if it's not a single character we don't know care about it
				charlength = ToAscii (wParam, lParam >> 16, state, (unsigned short *)asciichar, 0);
				if (vkey == K_ALT || vkey == K_CTRL || vkey == K_SHIFT || charlength == 0)
					asciichar[0] = 0;
				else if( charlength == 2 ) {
					asciichar[0] = asciichar[1];
				}
				
				Key_Event (vkey, asciichar[0], down);
			break;
		
		case WM_SYSCHAR:
		// keep Alt-Space from happening
			break;

	// this is complicated because Win32 seems to pack multiple mouse events into
	// one update sometimes, so we always check all states and look for events
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MOUSEMOVE:
			temp = 0;

			if (wParam & MK_LBUTTON)
				temp |= 1;

			if (wParam & MK_RBUTTON)
				temp |= 2;

			if (wParam & MK_MBUTTON)
				temp |= 4;

				if (wParam & MK_XBUTTON1)
					temp |= 8;
	
				if (wParam & MK_XBUTTON2)
					temp |= 16;
	
			IN_MouseEvent (temp);

			break;


    	case WM_SIZE:
            break;

   	    case WM_CLOSE:

			// Baker 3.60 - cl_confirmquit support
			if (!cl_confirmquit.value || MessageBox(mainwindow, "Are you sure you want to quit?", "Confirm Exit", MB_YESNO | MB_SETFOREGROUND | MB_ICONQUESTION) == IDYES)
				Sys_Quit ();

	        break;

		case WM_ACTIVATE:
			fActive = LOWORD(wParam);
			fMinimized = (BOOL) HIWORD(wParam);
			AppActivate(!(fActive == WA_INACTIVE), fMinimized);

		// fix the leftover Alt from any Alt-Tab or the like that switched us away
			Key_ClearAllStates ();

			break;

   	    case WM_DESTROY:

			if (dibwindow)
				DestroyWindow (dibwindow);

            PostQuitMessage (0);

        break;

		case MM_MCINOTIFY:
            lRet = CDAudio_MessageHandler (hWnd, uMsg, wParam, lParam);
			break;

    	default:
            /* pass all unhandled messages to DefWindowProc */
            lRet = DefWindowProc (hWnd, uMsg, wParam, lParam);
        break;
    }

    /* return 1 if handled message, 0 if not */
    return lRet;
}

//==========================================================================
//
//  COMMANDS
//
//==========================================================================

/*
=================
VID_NumModes
=================
*/
int VID_NumModes (void) {
	return nummodes;
}

	

vmode_t *VID_GetModePtr (int modenum) {

	if ((modenum >= 0) && (modenum < nummodes))
		return &modelist[modenum];
	else
		return &badmode;
}

char *VID_GetModeDescription (int mode) {
	char		*pinfo;
	vmode_t		*pv;
	static char	temp[100];

	if ((mode < 0) || (mode >= nummodes))
		return NULL;

	if (!leavecurrentmode)
	{
		pv = VID_GetModePtr (mode);
		pinfo = pv->modedesc;
	}
	else
	{
		sprintf (temp, "Desktop resolution (%ix%ix%i)", //johnfitz -- added bpp
				 modelist[MODE_FULLSCREEN_DEFAULT].width,
				 modelist[MODE_FULLSCREEN_DEFAULT].height);
		pinfo = temp;
	}

	return pinfo;
}


// KJB: Added this to return the mode driver name in description for console

char *VID_GetExtModeDescription (int mode)
{
	static char	pinfo[40];
	vmode_t		*pv;

	if ((mode < 0) || (mode >= nummodes))
		return NULL;

	pv = VID_GetModePtr (mode);
	if (modelist[mode].type == MS_FULLDIB)
	{
		if (!leavecurrentmode) 
		{
			sprintf(pinfo,"%s fullscreen", pv->modedesc);
		}
		else
		{
			sprintf (pinfo, "Desktop resolution (%ix%ix%i)", //johnfitz -- added bpp
					 modelist[MODE_FULLSCREEN_DEFAULT].width,
					 modelist[MODE_FULLSCREEN_DEFAULT].height,
					 modelist[MODE_FULLSCREEN_DEFAULT].bpp); //johnfitz -- added bpp
		}
	}
		 else 
	{
		if (modestate == MS_WINDOWED)
			sprintf(pinfo, "%s windowed", pv->modedesc);
		else
			sprintf(pinfo, "windowed");
	}

	return pinfo;
}


/*
=================
VID_DescribeCurrentMode_f
=================
*/
void VID_DescribeCurrentMode_f (void) {
	Con_Printf ("%s\n", VID_GetExtModeDescription (vid_modenum));
}


/*
=================
VID_NumModes_f
=================
*/ /*
void VID_NumModes_f (void) {

	if (nummodes == 1)
		Con_Printf ("%d video mode is available\n", nummodes);
	else
		Con_Printf ("%d video modes are available\n", nummodes);
} */


/*
=================
VID_DescribeMode_f
=================
*/ /*
void VID_DescribeMode_f (void) {
	int		t, modenum;
	
	modenum = Q_atoi (Cmd_Argv(1));

	t = leavecurrentmode;
	leavecurrentmode = 0;

	Con_Printf ("%s\n", VID_GetExtModeDescription (modenum));

	leavecurrentmode = t;
} */


/*
=================
VID_DescribeModes_f -- johnfitz -- changed formatting, and added refresh rates after each mode.
=================
*/
void VID_DescribeModes_f (void) {
	int			i, lnummodes, t;
	char		*pinfo;
	vmode_t		*pv;
	int			lastwidth=0, lastheight=0, lastbpp=0, count=0;
	

	lnummodes = VID_NumModes ();

	t = leavecurrentmode;
	leavecurrentmode = 0;

	for (i=1 ; i<lnummodes ; i++)
	{
		pv = VID_GetModePtr (i);
		if (lastwidth == pv->width && lastheight == pv->height && lastbpp == pv->bpp)
		{
			Con_SafePrintf (",%i", pv->refreshrate);
		}
		else
		{
			if (count>0)
				Con_SafePrintf ("\n");
			Con_SafePrintf ("   %4i x %4i x %i : %i", pv->width, pv->height, pv->bpp, pv->refreshrate);
			lastwidth = pv->width;
			lastheight = pv->height;
			lastbpp = pv->bpp;
			count++;
	}
	}
	Con_Printf ("\n%i modes\n", count);

	leavecurrentmode = t;
}

//==========================================================================
//
//  INIT
//
//==========================================================================


void VID_InitDIB (HINSTANCE hInstance) {
	DEVMODE			devmode; //johnfitz
	WNDCLASS		wc;
	HDC				hdc;

	/* Register the frame class */
    wc.style         = 0;
    wc.lpfnWndProc   = (WNDPROC)MainWndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hIcon         = 0;
    wc.hCursor       = LoadCursor (NULL,IDC_ARROW);
	wc.hbrBackground = NULL;
    wc.lpszMenuName  = 0;
    wc.lpszClassName = "WinQuake";

    if (!RegisterClass (&wc) )
		Sys_Error ("Couldn't register window class");

	modelist[0].type = MS_WINDOWED;

	if (COM_CheckParm("-width"))
		modelist[0].width = Q_atoi(com_argv[COM_CheckParm("-width")+1]);
	else if (COM_CheckParm("-fullwindow"))
		modelist[0].width = GetSystemMetrics(SM_CXSCREEN); // Baker 3.60 - fullwindow mode
	else
		modelist[0].width = 640;

	if (modelist[0].width < 320)
		modelist[0].width = 320;

	if (COM_CheckParm("-height"))
		modelist[0].height= Q_atoi(com_argv[COM_CheckParm("-height")+1]);
	else if (COM_CheckParm("-fullwindow"))
		modelist[0].height = GetSystemMetrics(SM_CYSCREEN); // Baker 3.60 - fullwindow mode
	else
		modelist[0].height = modelist[0].width * 240/320;

	if (modelist[0].height < 200) //johnfitz -- was 240
		modelist[0].height = 200; //johnfitz -- was 240

	//johnfitz -- get desktop bit depth
	hdc = GetDC(NULL);
	modelist[0].bpp = GetDeviceCaps(hdc, BITSPIXEL);
	ReleaseDC(NULL, hdc);
	//johnfitz

	//johnfitz -- get refreshrate
	if (EnumDisplaySettings (NULL, ENUM_CURRENT_SETTINGS, &devmode))
		modelist[0].refreshrate = devmode.dmDisplayFrequency;
	//johnfitz

	sprintf (modelist[0].modedesc, "%dx%dx%d %dHz", //johnfitz -- added bpp, refreshrate
			 modelist[0].width,
			 modelist[0].height,
			 modelist[0].bpp, //johnfitz -- added bpp
			 modelist[0].refreshrate); //johnfitz -- added refreshrate

	modelist[0].modenum = MODE_WINDOWED;
	modelist[0].dib = 1;
	modelist[0].fullscreen = 0;
	modelist[0].halfscreen = 0;
	// modelist[0].bpp = 0;  // Baker: commented out in FitzQuake

	nummodes = 1;
}

void VID_InitFullDIB (HINSTANCE hInstance) {
	DEVMODE	devmode;
	int		i, modenum, originalnummodes, existingmode, numlowresmodes;
	int		j, bpp, done;
	BOOL	stat;

// enumerate >8 bpp modes
	originalnummodes = nummodes;
	modenum = 0;

	do {
		stat = EnumDisplaySettings (NULL, modenum, &devmode);

		if ((devmode.dmBitsPerPel >= 15) &&
			(devmode.dmPelsWidth <= MAXWIDTH) &&
			(devmode.dmPelsHeight <= MAXHEIGHT) &&
			(nummodes < MAX_MODE_LIST))
		{
			devmode.dmFields = DM_BITSPERPEL |
							   DM_PELSWIDTH |
							   DM_PELSHEIGHT |
							   DM_DISPLAYFREQUENCY; //johnfitz -- refreshrate

			if (ChangeDisplaySettings (&devmode, CDS_TEST | CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL) {
				modelist[nummodes].type = MS_FULLDIB;
				modelist[nummodes].width = devmode.dmPelsWidth;
				modelist[nummodes].height = devmode.dmPelsHeight;
				modelist[nummodes].modenum = 0;
				modelist[nummodes].halfscreen = 0;
				modelist[nummodes].dib = 1;
				modelist[nummodes].fullscreen = 1;
				modelist[nummodes].bpp = devmode.dmBitsPerPel;
				modelist[nummodes].refreshrate = devmode.dmDisplayFrequency; //johnfitz -- refreshrate
				sprintf (modelist[nummodes].modedesc, "%dx%dx%d %dHz", //johnfitz -- refreshrate
						 devmode.dmPelsWidth,
						 devmode.dmPelsHeight,
						 devmode.dmBitsPerPel,
						 devmode.dmDisplayFrequency); //johnfitz -- refreshrate

			// if the width is more than twice the height, reduce it by half because this
			// is probably a dual-screen monitor
				if (!COM_CheckParm("-noadjustaspect"))
				{
					if (modelist[nummodes].width > (modelist[nummodes].height << 1))
					{
						modelist[nummodes].width >>= 1;
						modelist[nummodes].halfscreen = 1;
						sprintf (modelist[nummodes].modedesc, "%dx%dx%d %dHz", //johnfitz -- refreshrate
								 modelist[nummodes].width,
								 modelist[nummodes].height,
								 modelist[nummodes].bpp,
								 modelist[nummodes].refreshrate); //johnfitz -- refreshrate
					}
				}

				for (i=originalnummodes, existingmode = 0 ; i<nummodes ; i++) {
					if ((modelist[nummodes].width == modelist[i].width)   &&
						(modelist[nummodes].height == modelist[i].height) &&
						(modelist[nummodes].bpp == modelist[i].bpp) &&
						(modelist[nummodes].refreshrate == modelist[i].refreshrate)) //johnfitz -- refreshrate
					{
						existingmode = 1;
						break;
					}
				}

				if (!existingmode)
					nummodes++;
			}
		}

		modenum++;
	} while (stat);

// see if there are any low-res modes that aren't being reported
	numlowresmodes = sizeof(lowresmodes) / sizeof(lowresmodes[0]);
	bpp = 16;
	done = 0;

	do
	{
		for (j=0 ; (j<numlowresmodes) && (nummodes < MAX_MODE_LIST) ; j++)
		{
			devmode.dmBitsPerPel = bpp;
			devmode.dmPelsWidth = lowresmodes[j].width;
			devmode.dmPelsHeight = lowresmodes[j].height;
			devmode.dmFields = DM_BITSPERPEL |
							   DM_PELSWIDTH |
							   DM_PELSHEIGHT |
							   DM_DISPLAYFREQUENCY; //johnfitz -- refreshrate;

			if (ChangeDisplaySettings (&devmode, CDS_TEST | CDS_FULLSCREEN) ==
					DISP_CHANGE_SUCCESSFUL)
			{
				modelist[nummodes].type = MS_FULLDIB;
				modelist[nummodes].width = devmode.dmPelsWidth;
				modelist[nummodes].height = devmode.dmPelsHeight;
				modelist[nummodes].modenum = 0;
				modelist[nummodes].halfscreen = 0;
				modelist[nummodes].dib = 1;
				modelist[nummodes].fullscreen = 1;
				modelist[nummodes].bpp = devmode.dmBitsPerPel;
				modelist[nummodes].refreshrate = devmode.dmDisplayFrequency; //johnfitz -- refreshrate
				sprintf (modelist[nummodes].modedesc, "%dx%dx%d %dHz", //johnfitz -- refreshrate
						 devmode.dmPelsWidth,
						 devmode.dmPelsHeight,
						 devmode.dmBitsPerPel,
						 devmode.dmDisplayFrequency); //johnfitz -- refreshrate

				for (i=originalnummodes, existingmode = 0 ; i<nummodes ; i++)
				{
					if ((modelist[nummodes].width == modelist[i].width)   &&
						(modelist[nummodes].height == modelist[i].height) &&
						(modelist[nummodes].bpp == modelist[i].bpp) &&
						(modelist[nummodes].refreshrate == modelist[i].refreshrate)) //johnfitz -- refreshrate
					{
						existingmode = 1;
						break;
					}
				}

				if (!existingmode)  {
					nummodes++;
				}
			}
		}
		switch (bpp) {
			case 16:
				bpp = 32;
				break;

			case 32:
				bpp = 24;
				break;

			case 24:
				done = 1;
				break;
		}
	} while (!done);

	if (nummodes == originalnummodes)
		Con_SafePrintf ("No fullscreen DIB modes found\n");
}

qboolean VID_Is8bit() {
	return is8bit;
}

#define GL_SHARED_TEXTURE_PALETTE_EXT 0x81FB

void VID_Init8bitPalette() 
{
	// Check for 8bit Extensions and initialize them.
	int i;
	char thePalette[256*3];
	char *oldPalette, *newPalette;

	glColorTableEXT = (void *)wglGetProcAddress("glColorTableEXT");
    if (!glColorTableEXT || strstr(gl_extensions, "GL_EXT_shared_texture_palette") ||
		COM_CheckParm("-no8bit"))
		return;

	Con_SafePrintf("8-bit GL extensions enabled.\n");
    glEnable( GL_SHARED_TEXTURE_PALETTE_EXT );
	oldPalette = (char *) d_8to24table; //d_8to24table3dfx;
	newPalette = thePalette;
	for (i=0;i<256;i++) {
		*newPalette++ = *oldPalette++;
		*newPalette++ = *oldPalette++;
		*newPalette++ = *oldPalette++;
		oldPalette++;
	}
	glColorTableEXT(GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGB, 256, GL_RGB, GL_UNSIGNED_BYTE,
		(void *) thePalette);
	is8bit = TRUE;
}

static void Check_Gamma (unsigned char *pal)
{
	float	f, inf;
	unsigned char	palette[768];
	int		i;

	if ((i = COM_CheckParm("-gamma")) == 0) {
		if ((gl_renderer && strstr(gl_renderer, "Voodoo")) ||
			(gl_vendor && strstr(gl_vendor, "3Dfx")))
			vid_gamma = 1;
		else
			vid_gamma = 0.7; // default to 0.7 on non-3dfx hardware
	} else
		vid_gamma = Q_atof(com_argv[i+1]);

	for (i=0 ; i<768 ; i++)
	{
		f = pow ( (pal[i]+1)/256.0 , vid_gamma );

		inf = f*255 + 0.5;
		if (inf < 0)
			inf = 0;
		if (inf > 255)
			inf = 255;
		palette[i] = inf;
	}

	memcpy (pal, palette, sizeof(palette));

	BuildGammaTable(vid_gamma);
}

/*
===================
VID_Init
===================
*/
void	VID_Init (unsigned char *palette)
{
	int		i, existingmode;
	int		basenummodes, width, height, bpp, findbpp, done;
	char	gldir[MAX_OSPATH];
	HGLRC	baseRC; //johnfitz -- moved here from global scope, since it was only used in this
	HDC		hdc;
	DEVMODE	devmode;

	memset(&devmode, 0, sizeof(devmode));

//	Cvar_RegisterVariable (&vid_fullscreen, NULL); //johnfitz
	Cvar_RegisterVariable (&vid_width, NULL); //johnfitz
	Cvar_RegisterVariable (&vid_height, NULL); //johnfitz
//	Cvar_RegisterVariable (&vid_bpp, NULL); //johnfitz
	Cvar_RegisterVariable (&vid_refreshrate, NULL); //johnfitz
	Cvar_RegisterVariable (&_windowed_mouse, NULL);
	Cvar_RegisterVariable (&gl_ztrick, NULL);

	Cmd_AddCommand ("vid_unlock", VID_Unlock); //johnfitz
	Cmd_AddCommand ("vid_restart", VID_Restart); //johnfitz
	Cmd_AddCommand ("vid_test", VID_Test); //johnfitz

	Cmd_AddCommand ("vid_describecurrentmode", VID_DescribeCurrentMode_f);
	Cmd_AddCommand ("vid_describemodes", VID_DescribeModes_f);

	hIcon = LoadIcon (global_hInstance, MAKEINTRESOURCE (IDI_ICON2));

	InitCommonControls();

	VID_InitDIB (global_hInstance);
	basenummodes = nummodes = 1;

	VID_InitFullDIB (global_hInstance);

	if (COM_CheckParm("-window") || COM_CheckParm("-fullwindow"))
	{
		hdc = GetDC (NULL);

		if (GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE)
			Sys_Error ("Can't run in non-RGB mode");


		ReleaseDC (NULL, hdc);

		windowed = true;

		vid_default = MODE_WINDOWED;
	} else {
		if (nummodes == 1)
			Sys_Error ("No RGB fullscreen modes available");

		windowed = false;

		if (COM_CheckParm("-mode"))
		{
			vid_default = Q_atoi(com_argv[COM_CheckParm("-mode")+1]);
		}
		else
		{
			if (COM_CheckParm("-current"))
			{
				modelist[MODE_FULLSCREEN_DEFAULT].width = GetSystemMetrics (SM_CXSCREEN);
				modelist[MODE_FULLSCREEN_DEFAULT].height = GetSystemMetrics (SM_CYSCREEN);
				vid_default = MODE_FULLSCREEN_DEFAULT;
				leavecurrentmode = 1;
			}
			else
			{
				if (COM_CheckParm("-width"))
				{
					width = Q_atoi(com_argv[COM_CheckParm("-width")+1]);
				}
				else
				{
					width = 640;
				}

				if (COM_CheckParm("-bpp"))
				{
					bpp = Q_atoi(com_argv[COM_CheckParm("-bpp")+1]);
					findbpp = 0;
				}
				else
				{
					bpp = 15;
					findbpp = 1;
				}

				if (COM_CheckParm("-height"))
					height = Q_atoi(com_argv[COM_CheckParm("-height")+1]);

			// if they want to force it, add the specified mode to the list
				if (COM_CheckParm("-force") && (nummodes < MAX_MODE_LIST))
				{
					modelist[nummodes].type = MS_FULLDIB;
					modelist[nummodes].width = width;
					modelist[nummodes].height = height;
					modelist[nummodes].modenum = 0;
					modelist[nummodes].halfscreen = 0;
					modelist[nummodes].dib = 1;
					modelist[nummodes].fullscreen = 1;
					modelist[nummodes].bpp = bpp;
					sprintf (modelist[nummodes].modedesc, "%dx%dx%d %dHz", //johnfitz -- refreshrate
							 devmode.dmPelsWidth,
							 devmode.dmPelsHeight,
							 devmode.dmBitsPerPel,
							 devmode.dmDisplayFrequency); //johnfitz -- refreshrate

					for (i=nummodes, existingmode = 0 ; i<nummodes ; i++)
					{
						if ((modelist[nummodes].width == modelist[i].width)   &&
							(modelist[nummodes].height == modelist[i].height) &&
							(modelist[nummodes].bpp == modelist[i].bpp) &&
							(modelist[nummodes].refreshrate == modelist[i].refreshrate)) //johnfitz -- refreshrate
						{
							existingmode = 1;
							break;
						}
					}

					if (!existingmode)
					{
						nummodes++;
					}
				}

				done = 0;

				do
				{
					if (COM_CheckParm("-height"))
					{
						height = Q_atoi(com_argv[COM_CheckParm("-height")+1]);

						for (i=1, vid_default=0 ; i<nummodes ; i++)
						{
							if ((modelist[i].width == width) &&
								(modelist[i].height == height) &&
								(modelist[i].bpp == bpp))
							{
								vid_default = i;
								done = 1;
								break;
							}
						}
					}
					else
					{
						for (i=1, vid_default=0 ; i<nummodes ; i++)
						{
							if ((modelist[i].width == width) && (modelist[i].bpp == bpp))
							{
								vid_default = i;
								done = 1;
								break;
							}
						}
					}

					if (!done)
					{
						if (findbpp)
						{
							switch (bpp)
							{
							case 15:
								bpp = 16;
								break;
							case 16:
								bpp = 32;
								break;
							case 32:
								bpp = 24;
								break;
							case 24:
								done = 1;
								break;
							}
						}
						else
						{
							done = 1;
						}
					}
				} while (!done);

				if (!vid_default)
				{
					Sys_Error ("Specified video mode not available");
				}
			}
		}
	}

	vid_initialized = true;

	if ((i = COM_CheckParm("-conwidth")) != 0) {
		// Conwidth specified, we check conheight too then
		vid.conwidth = Q_atoi(com_argv[i+1]);
		
		if ((i = COM_CheckParm("-conheight")) != 0)
			vid.conheight = Q_atoi(com_argv[i+1]);
	} else {
		// Conwidth not specified, default one
		if (modelist[i].width >= 1024) {
			vid.conwidth = modelist[vid_default].width / 2;
			vid.conheight = modelist[vid_default].height / 2;
		} else { 
			vid.conwidth = modelist[vid_default].width;
			vid.conheight = modelist[vid_default].height;
		}
	}

	if (vid.conwidth < 320)
		vid.conwidth = 320;

	if (vid.conheight < 200)
		vid.conheight = 200;

	vid.conwidth &= 0xfff8; // make it a multiple of eight
	vid.conheight &= 0xfff8; // make it a multiple of eight

	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));

	DestroyWindow (hwnd_dialog);

	Check_Gamma(palette);
	VID_SetPalette (palette);

	VID_SetMode (vid_default, palette);

    maindc = GetDC(mainwindow);
	bSetupPixelFormat(maindc);

    baseRC = wglCreateContext( maindc );
	if (!baseRC)
		Sys_Error ("Could not initialize GL (wglCreateContext failed).\n\nMake sure you are in 65535 color mode, and try running -window.");
    if (!wglMakeCurrent( maindc, baseRC ))
		Sys_Error ("VID_Init: wglMakeCurrent failed");

	GL_Init ();
	CheckVsyncControlExtensions ();
	
	// Baker 3.80x - We will want to remove the .ms2 stuff next time
	sprintf (gldir, "%s/glquake", com_gamedir);
	Sys_mkdir (gldir);

	vid_realmode = vid_modenum;

	// Check for 3DFX Extensions and initialize them.
	VID_Init8bitPalette();

	vid_menucmdfn = VID_Menu_f; //johnfitz
	vid_menudrawfn = VID_MenuDraw;
	vid_menukeyfn = VID_MenuKey;

	strcpy (badmode.modedesc, "Bad mode");
	vid_canalttab = true;

	if (COM_CheckParm("-fullsbar"))
		fullsbardraw = true;
		
	VID_Menu_Init(); //johnfitz

	//johnfitz -- command line vid settings should override config file settings.
	//so we have to lock the vid mode from now until after all config files are read.
	if (COM_CheckParm("-width") || COM_CheckParm("-height") || COM_CheckParm("-bpp") || COM_CheckParm("-window"))
	{
		vid_locked = true;
	}
	//johnfitz		
}

/*
================
VID_SyncCvars -- johnfitz -- set vid cvars to match current video mode
================
*/
void VID_SyncCvars (void)
{
	Cvar_Set ("vid_width", va("%i", modelist[vid_default].width));
	Cvar_Set ("vid_height", va("%i", modelist[vid_default].height));
//	Cvar_Set ("vid_bpp", va("%i", modelist[vid_default].bpp));
	Cvar_Set ("vid_refreshrate", va("%i", modelist[vid_default].refreshrate));
//	Cvar_Set ("vid_fullscreen", (windowed) ? "0" : "1");
}

//========================================================
// Video menu stuff
//========================================================

extern void M_Menu_Options_f (void);
extern void M_Print (int cx, int cy, char *str);
extern void M_PrintWhite (int cx, int cy, char *str);
extern void M_DrawCharacter (int cx, int line, int num);
extern void M_DrawTransPic (int x, int y, qpic_t *pic);
extern void M_DrawPic (int x, int y, qpic_t *pic);
extern void M_DrawCheckbox (int x, int y, int on);

extern qboolean	m_entersound;

// Baker - mirror of menu
enum {
	m_none, 
	m_main, 
	m_singleplayer, 
	m_load, 
	m_save, 
	m_multiplayer, 
	m_setup, 
	m_net, 
	m_options, 
	m_video, 
	m_keys, 
	m_help, 
	m_quit, 
	m_serialconfig, 
	m_modemconfig, 
	m_lanconfig, 
	m_gameoptions, 
	m_search, 
	m_slist, 
	m_preferences
} m_state;

// video options was 6
#define VIDEO_OPTIONS_ITEMS 4
int		video_cursor_table[] = {48, 56, 72, 80}; //{48, 56, 64, 72, 88, 96};
int		video_options_cursor = 0;

typedef struct {int width,height;} vid_menu_mode;

//TODO: replace these fixed-length arrays with hunk_allocated buffers

vid_menu_mode vid_menu_modes[MAX_MODE_LIST];
int vid_menu_nummodes=0;

//int vid_menu_bpps[4];
//int vid_menu_numbpps=0;

int vid_menu_rates[20];
int vid_menu_numrates=0;

/*
================
VID_Menu_Init
================
*/
void VID_Menu_Init (void)
{
	int i,j,h,w;

	for (i=1;i<nummodes;i++) //start i at mode 1 because 0 is windowed mode
	{
		w = modelist[i].width;
		h = modelist[i].height;

		for (j=0;j<vid_menu_nummodes;j++)
{
			if (vid_menu_modes[j].width == w &&
				vid_menu_modes[j].height == h)
				break;
		}

		if (j==vid_menu_nummodes)
		{
			vid_menu_modes[j].width = w;
			vid_menu_modes[j].height = h;
			vid_menu_nummodes++;
		}
	}
}

/*
================
VID_Menu_RebuildBppList

regenerates bpp list based on current vid_width and vid_height
================
*/ /*
void VID_Menu_RebuildBppList (void)
{
	int i,j,b;

	vid_menu_numbpps=0;

	for (i=1;i<nummodes;i++) //start i at mode 1 because 0 is windowed mode
	{
		//bpp list is limited to bpps available with current width/height
		if (modelist[i].width != vid_width.value ||
			modelist[i].height != vid_height.value)
			continue;

		b = modelist[i].bpp;

		for (j=0;j<vid_menu_numbpps;j++)
		{
			if (vid_menu_bpps[j] == b)
				break;
		}

		if (j==vid_menu_numbpps)
{
			vid_menu_bpps[j] = b;
			vid_menu_numbpps++;
		}
	}

	//if vid_bpp is not in the new list, change vid_bpp
	for (i=0;i<vid_menu_numbpps;i++)
		if (vid_menu_bpps[i] == (int)(vid_bpp.value))
			break;

	if (i==vid_menu_numbpps)
		Cvar_Set ("vid_bpp",va("%i",vid_menu_bpps[0]));
} */

/*
================
VID_Menu_RebuildRateList

regenerates rate list based on current vid_width, vid_height and vid_bpp
================
*/
void VID_Menu_RebuildRateList (void)
{
	int i,j,r;

	vid_menu_numrates=0;
	
	for (i=1;i<nummodes;i++) //start i at mode 1 because 0 is windowed mode
	{
		//rate list is limited to rates available with current width/height/bpp
		if (modelist[i].width != vid_width.value ||
			modelist[i].height != vid_height.value /*||
			modelist[i].bpp != vid_bpp.value */)
			continue;

		r = modelist[i].refreshrate;

		for (j=0;j<vid_menu_numrates;j++)
		{
			if (vid_menu_rates[j] == r)
				break;
		}

		if (j==vid_menu_numrates)
		{
			vid_menu_rates[j] = r;
			vid_menu_numrates++;
		}
	}

	//if vid_refreshrate is not in the new list, change vid_refreshrate
	for (i=0;i<vid_menu_numrates;i++)
		if (vid_menu_rates[i] == (int)(vid_refreshrate.value))
			break;

	if (i==vid_menu_numrates)
		Cvar_Set ("vid_refreshrate",va("%i",vid_menu_rates[0]));
	}

/*
================
VID_Menu_ChooseNextMode

chooses next resolution in order, then updates vid_width and
vid_height cvars, then updates bpp and refreshrate lists
================
*/
void VID_Menu_ChooseNextMode (int dir)
{
	int i;

	for (i=0;i<vid_menu_nummodes;i++)
	{
		if (vid_menu_modes[i].width == vid_width.value &&
			vid_menu_modes[i].height == vid_height.value)
			break;
	}

	if (i==vid_menu_nummodes) //can't find it in list, so it must be a custom windowed res
	{
		i = 0;
	}
			else
	{
		i+=dir;
		if (i>=vid_menu_nummodes)
			i = 0;
		else if (i<0)
			i = vid_menu_nummodes-1;
	}

	Cvar_Set ("vid_width",va("%i",vid_menu_modes[i].width));
	Cvar_Set ("vid_height",va("%i",vid_menu_modes[i].height));
	//VID_Menu_RebuildBppList ();
	VID_Menu_RebuildRateList ();
}

/*
================
VID_Menu_ChooseNextBpp

chooses next bpp in order, then updates vid_bpp cvar, then updates refreshrate list
================
*/ /*
void VID_Menu_ChooseNextBpp (int dir)
{
	int i;

	for (i=0;i<vid_menu_numbpps;i++)
	{
		if (vid_menu_bpps[i] == vid_bpp.value)
			break;
	}

	if (i==vid_menu_numbpps) //can't find it in list
	{
		i = 0;
			}
	else
	{
		i+=dir;
		if (i>=vid_menu_numbpps)
			i = 0;
		else if (i<0)
			i = vid_menu_numbpps-1;
		}

	Cvar_Set ("vid_bpp",va("%i",vid_menu_bpps[i]));
	VID_Menu_RebuildRateList ();
} */

/*
================
VID_Menu_ChooseNextRate

chooses next refresh rate in order, then updates vid_refreshrate cvar
================
*/
void VID_Menu_ChooseNextRate (int dir)
{
	int i;

	for (i=0;i<vid_menu_numrates;i++)
	{
		if (vid_menu_rates[i] == vid_refreshrate.value)
			break;
	}

	if (i==vid_menu_numrates) //can't find it in list
	{
		i = 0;
	}
	else
	{
		i+=dir;
		if (i>=vid_menu_numrates)
			i = 0;
		else if (i<0)
			i = vid_menu_numrates-1;
}

	Cvar_Set ("vid_refreshrate",va("%i",vid_menu_rates[i]));
}

/*
================
VID_MenuKey
================
*/
void VID_MenuKey (int key)
{
	switch (key)
	{
	case K_ESCAPE:
		VID_SyncCvars (); //sync cvars before leaving menu. FIXME: there are other ways to leave menu
		S_LocalSound ("misc/menu1.wav");
		M_Menu_Options_f ();
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		video_options_cursor--;
		if (video_options_cursor < 0)
			video_options_cursor = VIDEO_OPTIONS_ITEMS-1;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		video_options_cursor++;
		if (video_options_cursor >= VIDEO_OPTIONS_ITEMS)
			video_options_cursor = 0;
		break;

	case K_LEFTARROW:
		S_LocalSound ("misc/menu3.wav");
		switch (video_options_cursor)
		{
		case 0:
			VID_Menu_ChooseNextMode (-1);
			break;
/*		case 1:
			VID_Menu_ChooseNextBpp (-1);
			break; */
		case 1:
			VID_Menu_ChooseNextRate (-1);
			break;
/*		case 3:
			Cbuf_AddText ("toggle vid_fullscreen\n");
			break; */
		case 2:
		case 3:
		case 4:
		case 5:
	default:
		break;
	}
		break;

	case K_RIGHTARROW:
		S_LocalSound ("misc/menu3.wav");
		switch (video_options_cursor)
		{
		case 0:
			VID_Menu_ChooseNextMode (1);
			break;
/*		case 1:
			VID_Menu_ChooseNextBpp (1);
			break; */
		case 1:
			VID_Menu_ChooseNextRate (1);
			break;
/*		case 3:
			Cbuf_AddText ("toggle vid_fullscreen\n");
			break; */
		case 2:
		case 3:
		case 4:
		case 5:
		default:
			break;
		}
		break;

	case K_ENTER:
		m_entersound = true;
		switch (video_options_cursor)
		{
		case 0:
			VID_Menu_ChooseNextMode (1);
			break;
/*		case 1:
			VID_Menu_ChooseNextBpp (1);
			break; */
		case 1:
			VID_Menu_ChooseNextRate (1);
			break;
/*		case 3:
			Cbuf_AddText ("toggle vid_fullscreen\n");
			break; */
		case 2:
			Cbuf_AddText ("vid_test\n");
			break;
		case 3:
			Cbuf_AddText ("vid_restart\n");
			break;
		default:
			break;
		}
		break;

	default:
		break;
	}
}

/*
================
VID_MenuDraw
================
*/
void VID_MenuDraw (void)
{
	int i = 0;
	qpic_t *p;
	char *title;

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp"));

	//p = Draw_CachePic ("gfx/vidmodes.lmp");
	p = Draw_CachePic ("gfx/p_option.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);

	// title
	title = "Video Options";
	M_PrintWhite ((320-8*strlen(title))/2, 32, title);

	// options
	M_Print (16, video_cursor_table[i], "            Video mode");
	M_Print (216, video_cursor_table[i], va("%ix%i", (int)vid_width.value, (int)vid_height.value));
	i++;

/*	M_Print (16, video_cursor_table[i], "           Color depth");
	M_Print (216, video_cursor_table[i], va("%i", (int)vid_bpp.value));
	i++; */

	M_Print (16, video_cursor_table[i], "          Refresh rate");
	M_Print (216, video_cursor_table[i], va("%i Hz", (int)vid_refreshrate.value));
	i++;

/*	M_Print (16, video_cursor_table[i], "            Fullscreen");
	M_DrawCheckbox (216, video_cursor_table[i], (int)vid_fullscreen.value);
	i++; */

	M_Print (16, video_cursor_table[i], "          Test changes");
	i++;

	M_Print (16, video_cursor_table[i], "         Apply changes");

	// cursor
	M_DrawCharacter (200, video_cursor_table[video_options_cursor], 12+((int)(realtime*4)&1));

	// notes          "345678901234567890123456789012345678"
//	M_Print (16, 172, "Windowed modes always use the desk- ");
//	M_Print (16, 180, "top color depth, and can never be   ");
//	M_Print (16, 188, "larger than the desktop resolution. ");
}

/*
================
VID_Menu_f
================
*/
void VID_Menu_f (void)
{
	key_dest = key_menu;
	m_state = m_video;
	m_entersound = true;

	//set all the cvars to match the current mode when entering the menu
	VID_SyncCvars ();

	//set up bpp and rate lists based on current cvars
//	VID_Menu_RebuildBppList ();
	VID_Menu_RebuildRateList ();
}
