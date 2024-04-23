#include "Core.h"
#if defined CC_BUILD_GCWII
#include "Window.h"
#include "Platform.h"
#include "Input.h"
#include "Event.h"
#include "String.h"
#include "Funcs.h"
#include "Bitmap.h"
#include "Errors.h"
#include "ExtMath.h"
#include "Graphics.h"
#include "VirtualKeyboard.h"
#include <gccore.h>
#if defined HW_RVL
#include <wiiuse/wpad.h>
#include <wiikeyboard/keyboard.h>
#endif

static cc_bool needsFBUpdate;
static cc_bool launcherMode;
static void* xfb;
static GXRModeObj* rmode;
void* Window_XFB;
struct _DisplayData DisplayInfo;
struct _WindowData WindowInfo;


static void OnPowerOff(void) {
	Window_Main.Exists = false;
	Window_RequestClose();
}
static void InitVideo(void) {
	// Initialise the video system
	VIDEO_Init();

	// Obtain the preferred video mode from the system
	// This will correspond to the settings in the Wii menu
	rmode = VIDEO_GetPreferredMode(NULL);
	// Allocate memory for the display in the uncached region
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	
	Window_XFB = xfb;	
	//console_init(xfb,20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);

	// Set up the video registers with the chosen mode
	VIDEO_Configure(rmode);
	// Tell the video hardware where our display memory is
	VIDEO_SetNextFramebuffer(xfb);

	// Make the display visible
	VIDEO_SetBlack(FALSE);
	// Flush the video register changes to the hardware
	VIDEO_Flush();
	// Wait for Video setup to complete
	VIDEO_WaitVSync();
}

void Window_Init(void) {	
	// TODO: SYS_SetResetCallback(reload); too? not sure how reset differs on GC/WII
	#if defined HW_RVL
	SYS_SetPowerCallback(OnPowerOff);
	#endif
	InitVideo();
	
	DisplayInfo.Width  = rmode->fbWidth;
	DisplayInfo.Height = rmode->xfbHeight;
	DisplayInfo.ScaleX = 1;
	DisplayInfo.ScaleY = 1;
	
	Window_Main.Width   = rmode->fbWidth;
	Window_Main.Height  = rmode->xfbHeight;
	Window_Main.Focused = true;
	Window_Main.Exists  = true;

	Input.Sources = INPUT_SOURCE_GAMEPAD;
	DisplayInfo.ContentOffsetX = 10;
	DisplayInfo.ContentOffsetY = 10;

	#if defined HW_RVL
	WPAD_Init();
	WPAD_SetDataFormat(0, WPAD_FMT_BTNS_ACC_IR);
	KEYBOARD_Init(NULL);
	#endif
	PAD_Init();
}

void Window_Free(void) { }

void Window_Create2D(int width, int height) {
	needsFBUpdate = true;
	launcherMode  = true;  
}

void Window_Create3D(int width, int height) { 
	launcherMode = false; 
}

void Window_RequestClose(void) {
	Event_RaiseVoid(&WindowEvents.Closing);
}


/*########################################################################################################################*
*---------------------------------------------GameCube controller processing----------------------------------------------*
*#########################################################################################################################*/
static PADStatus gc_pad;

#define PAD_AXIS_SCALE 8.0f
static void ProcessPAD_Joystick(int axis, int x, int y, double delta) {
	// May not be exactly 0 on actual hardware
	if (Math_AbsI(x) <= 8) x = 0;
	if (Math_AbsI(y) <= 8) y = 0;
	
	Gamepad_SetAxis(axis, x / PAD_AXIS_SCALE, -y / PAD_AXIS_SCALE, delta);		
}

static void ProcessPAD_Buttons(void) {
	int mods = gc_pad.button;
	Gamepad_SetButton(CCPAD_L, mods & PAD_TRIGGER_L);
	Gamepad_SetButton(CCPAD_R, mods & PAD_TRIGGER_R);
	
	Gamepad_SetButton(CCPAD_A, mods & PAD_BUTTON_A);
	Gamepad_SetButton(CCPAD_B, mods & PAD_BUTTON_B);
	Gamepad_SetButton(CCPAD_X, mods & PAD_BUTTON_X);
	Gamepad_SetButton(CCPAD_Y, mods & PAD_BUTTON_Y);
	
	Gamepad_SetButton(CCPAD_START,  mods & PAD_BUTTON_START);
	Gamepad_SetButton(CCPAD_SELECT, mods & PAD_TRIGGER_Z);
	
	Gamepad_SetButton(CCPAD_LEFT,   mods & PAD_BUTTON_LEFT);
	Gamepad_SetButton(CCPAD_RIGHT,  mods & PAD_BUTTON_RIGHT);
	Gamepad_SetButton(CCPAD_UP,     mods & PAD_BUTTON_UP);
	Gamepad_SetButton(CCPAD_DOWN,   mods & PAD_BUTTON_DOWN);
}

static void ProcessPADInput(double delta) {
	PADStatus pads[4];
	PAD_Read(pads);
	int error = pads[0].err;

	if (error == 0) {
		gc_pad = pads[0]; // new state arrived
	} else if (error == PAD_ERR_TRANSFER) {
		// usually means still busy transferring state - use last state
	} else {
		return; // not connected, still busy, etc
	}
	
	ProcessPAD_Buttons();
	ProcessPAD_Joystick(PAD_AXIS_LEFT,  gc_pad.stickX,    gc_pad.stickY,    delta);
	ProcessPAD_Joystick(PAD_AXIS_RIGHT, gc_pad.substickX, gc_pad.substickY, delta);
}


/*########################################################################################################################*
*--------------------------------------------------Kebyaord processing----------------------------------------------------*
*#########################################################################################################################*/
#if defined HW_RVL
static const cc_uint8 key_map[] = {
/* 0x00 */ 0,0,0,0,         'A','B','C','D', 
/* 0x08 */ 'E','F','G','H', 'I','J','K','L',
/* 0x10 */ 'M','N','O','P', 'Q','R','S','T',
/* 0x18 */ 'U','V','W','X', 'Y','Z','1','2',
/* 0x20 */ '3','4','5','6', '7','8','9','0',
/* 0x28 */ CCKEY_ENTER,CCKEY_ESCAPE,CCKEY_BACKSPACE,CCKEY_TAB, CCKEY_SPACE,CCKEY_MINUS,CCKEY_EQUALS,CCKEY_LBRACKET,
/* 0x30 */ CCKEY_RBRACKET,CCKEY_BACKSLASH,0,CCKEY_SEMICOLON, CCKEY_QUOTE,CCKEY_TILDE,CCKEY_COMMA,CCKEY_PERIOD,
/* 0x38 */ CCKEY_SLASH,CCKEY_CAPSLOCK,CCKEY_F1,CCKEY_F2, CCKEY_F3,CCKEY_F4,CCKEY_F5,CCKEY_F6,
/* 0x40 */ CCKEY_F7,CCKEY_F8,CCKEY_F9,CCKEY_F10, CCKEY_F11,CCKEY_F12,CCKEY_PRINTSCREEN,CCKEY_SCROLLLOCK,
/* 0x48 */ CCKEY_PAUSE,CCKEY_INSERT,CCKEY_HOME,CCKEY_PAGEUP, CCKEY_DELETE,CCKEY_END,CCKEY_PAGEDOWN,CCKEY_RIGHT,
/* 0x50 */ CCKEY_LEFT,CCKEY_DOWN,CCKEY_UP,CCKEY_NUMLOCK, CCKEY_KP_DIVIDE,CCKEY_KP_MULTIPLY,CCKEY_KP_MINUS,CCKEY_KP_PLUS,
/* 0x58 */ CCKEY_KP_ENTER,CCKEY_KP1,CCKEY_KP2,CCKEY_KP3, CCKEY_KP4,CCKEY_KP5,CCKEY_KP6,CCKEY_KP7,
/* 0x60 */ CCKEY_KP8,CCKEY_KP9,CCKEY_KP0,CCKEY_KP_DECIMAL, 0,0,0,0,
/* 0x68 */ 0,0,0,0, 0,0,0,0,
/* 0x70 */ 0,0,0,0, 0,0,0,0,
/* 0x78 */ 0,0,0,0, 0,0,0,0,
/* 0x80 */ 0,0,0,0, 0,0,0,0,
/* 0x88 */ 0,0,0,0, 0,0,0,0,
/* 0x90 */ 0,0,0,0, 0,0,0,0,
/* 0x98 */ 0,0,0,0, 0,0,0,0,
/* 0xA0 */ 0,0,0,0, 0,0,0,0,
/* 0xA8 */ 0,0,0,0, 0,0,0,0,
/* 0xB0 */ 0,0,0,0, 0,0,0,0,
/* 0xB8 */ 0,0,0,0, 0,0,0,0,
/* 0xC0 */ 0,0,0,0, 0,0,0,0,
/* 0xC8 */ 0,0,0,0, 0,0,0,0,
/* 0xD0 */ 0,0,0,0, 0,0,0,0,
/* 0xD8 */ 0,0,0,0, 0,0,0,0,
/* 0xE0 */ CCKEY_LCTRL,CCKEY_LSHIFT,CCKEY_LALT,CCKEY_LWIN, CCKEY_RCTRL,CCKEY_RSHIFT,CCKEY_RALT,CCKEY_RWIN
};

static int MapNativeKey(unsigned key) {
	return key < Array_Elems(key_map) ? key_map[key] : 0;
}

static void ProcessKeyboardInput(void) {
	keyboard_event ke;
	int res = KEYBOARD_GetEvent(&ke);
	int key;
	
	if (!res) return;
	Input.Sources |= INPUT_SOURCE_NORMAL;
	
	if (res && ke.type == KEYBOARD_PRESSED)
	{
		key = MapNativeKey(ke.keycode);
		if (key) Input_SetPressed(key);
		//Platform_Log2("KEYCODE: %i (%i)", &ke.keycode, &ke.type);
		if (ke.symbol) Event_RaiseInt(&InputEvents.Press, ke.symbol);
	}
	if (res && ke.type == KEYBOARD_RELEASED)
	{
		key = MapNativeKey(ke.keycode);
		if (key) Input_SetReleased(key);
	}
}
#endif


/*########################################################################################################################*
*----------------------------------------------------Input processing-----------------------------------------------------*
*#########################################################################################################################*/
#if defined HW_RVL
static int dragCurX, dragCurY;
static int dragStartX, dragStartY;
static cc_bool dragActive;

static void ProcessWPAD_Buttons(int mods) {
	Gamepad_SetButton(CCPAD_L, mods & WPAD_BUTTON_1);
	Gamepad_SetButton(CCPAD_R, mods & WPAD_BUTTON_2);
      
	Gamepad_SetButton(CCPAD_A, mods & WPAD_BUTTON_A);
	Gamepad_SetButton(CCPAD_B, mods & WPAD_BUTTON_B);
	Gamepad_SetButton(CCPAD_X, mods & WPAD_BUTTON_PLUS);
      
	Gamepad_SetButton(CCPAD_START,  mods & WPAD_BUTTON_HOME);
	Gamepad_SetButton(CCPAD_SELECT, mods & WPAD_BUTTON_MINUS);

	Gamepad_SetButton(CCPAD_LEFT,   mods & WPAD_BUTTON_LEFT);
	Gamepad_SetButton(CCPAD_RIGHT,  mods & WPAD_BUTTON_RIGHT);
	Gamepad_SetButton(CCPAD_UP,     mods & WPAD_BUTTON_UP);
	Gamepad_SetButton(CCPAD_DOWN,   mods & WPAD_BUTTON_DOWN);
}

static void ProcessNunchuck_Game(int mods, double delta) {
	WPADData* wd = WPAD_Data(0);
	joystick_t analog = wd->exp.nunchuk.js;

	Gamepad_SetButton(CCPAD_L, mods & WPAD_NUNCHUK_BUTTON_C);
	Gamepad_SetButton(CCPAD_R, mods & WPAD_NUNCHUK_BUTTON_Z);
      
	Gamepad_SetButton(CCPAD_A, mods & WPAD_BUTTON_A);
	Gamepad_SetButton(CCPAD_Y, mods & WPAD_BUTTON_1);
	Gamepad_SetButton(CCPAD_X, mods & WPAD_BUTTON_2);

	Gamepad_SetButton(CCPAD_START,  mods & WPAD_BUTTON_HOME);
	Gamepad_SetButton(CCPAD_SELECT, mods & WPAD_BUTTON_MINUS);

	Gamepad_SetButton(KeyBinds_Normal[KEYBIND_FLY], mods & WPAD_BUTTON_LEFT);

	if (mods & WPAD_BUTTON_RIGHT) {
		Mouse_ScrollWheel(1.0*delta);
	}

	Gamepad_SetButton(KeyBinds_Normal[KEYBIND_THIRD_PERSON], mods & WPAD_BUTTON_UP);
	Gamepad_SetButton(KeyBinds_Normal[KEYBIND_FLY_DOWN],    mods & WPAD_BUTTON_DOWN);

	const float ANGLE_DELTA = 50;
	bool nunchuckUp    = (analog.ang > -ANGLE_DELTA)    && (analog.ang < ANGLE_DELTA)     && (analog.mag > 0.5);
	bool nunchuckDown  = (analog.ang > 180-ANGLE_DELTA) && (analog.ang < 180+ANGLE_DELTA) && (analog.mag > 0.5);
	bool nunchuckLeft  = (analog.ang > -90-ANGLE_DELTA) && (analog.ang < -90+ANGLE_DELTA) && (analog.mag > 0.5);
	bool nunchuckRight = (analog.ang > 90-ANGLE_DELTA)  && (analog.ang < 90+ANGLE_DELTA)  && (analog.mag > 0.5);

	Gamepad_SetButton(CCPAD_LEFT,  nunchuckLeft);
	Gamepad_SetButton(CCPAD_RIGHT, nunchuckRight);
	Gamepad_SetButton(CCPAD_UP,    nunchuckUp);
	Gamepad_SetButton(CCPAD_DOWN,  nunchuckDown);
}

#define CLASSIC_AXIS_SCALE 2.0f
static void ProcessClassic_Joystick(int axis, struct joystick_t* js, double delta) {
	// TODO: need to account for min/max?? see libogc
	int x = js->pos.x - js->center.x;
	int y = js->pos.y - js->center.y;
	
	if (Math_AbsI(x) <= 8) x = 0;
	if (Math_AbsI(y) <= 8) y = 0;
	
	Gamepad_SetAxis(axis, x / CLASSIC_AXIS_SCALE, -y / CLASSIC_AXIS_SCALE, delta);
}

static void ProcessClassicButtons(int mods) {
	Gamepad_SetButton(CCPAD_L, mods & CLASSIC_CTRL_BUTTON_FULL_L);
	Gamepad_SetButton(CCPAD_R, mods & CLASSIC_CTRL_BUTTON_FULL_R);
      
	Gamepad_SetButton(CCPAD_A, mods & CLASSIC_CTRL_BUTTON_A);
	Gamepad_SetButton(CCPAD_B, mods & CLASSIC_CTRL_BUTTON_B);
	Gamepad_SetButton(CCPAD_X, mods & CLASSIC_CTRL_BUTTON_X);
	Gamepad_SetButton(CCPAD_Y, mods & CLASSIC_CTRL_BUTTON_Y);
      
	Gamepad_SetButton(CCPAD_START,  mods & CLASSIC_CTRL_BUTTON_PLUS);
	Gamepad_SetButton(CCPAD_SELECT, mods & CLASSIC_CTRL_BUTTON_MINUS);

	Gamepad_SetButton(CCPAD_LEFT,   mods & CLASSIC_CTRL_BUTTON_LEFT);
	Gamepad_SetButton(CCPAD_RIGHT,  mods & CLASSIC_CTRL_BUTTON_RIGHT);
	Gamepad_SetButton(CCPAD_UP,     mods & CLASSIC_CTRL_BUTTON_UP);
	Gamepad_SetButton(CCPAD_DOWN,   mods & CLASSIC_CTRL_BUTTON_DOWN);
	
	Gamepad_SetButton(CCPAD_ZL, mods & CLASSIC_CTRL_BUTTON_ZL);
	Gamepad_SetButton(CCPAD_ZR, mods & CLASSIC_CTRL_BUTTON_ZR);
}

static void ProcessClassicInput(double delta) {
	WPADData* wd = WPAD_Data(0);
	classic_ctrl_t ctrls = wd->exp.classic;
	int mods = ctrls.btns | ctrls.btns_held;

	ProcessClassicButtons(mods);
	ProcessClassic_Joystick(PAD_AXIS_LEFT,  &ctrls.ljs, delta);
	ProcessClassic_Joystick(PAD_AXIS_RIGHT, &ctrls.rjs, delta);
}


static void GetIRPos(int res, int* x, int* y) {
	if (res == WPAD_ERR_NONE) {
		WPADData* wd = WPAD_Data(0);

		*x = wd->ir.x;
		*y = wd->ir.y;
	} else {
		*x = 0; 
		*y = 0;
	}
}

static void ProcessWPADInput(double delta) {	
	WPAD_ScanPads();
	u32 mods = WPAD_ButtonsDown(0) | WPAD_ButtonsHeld(0);
	u32 type;
	int res  = WPAD_Probe(0, &type);
	if (res) return;

	if (type == WPAD_EXP_CLASSIC) {
		ProcessClassicInput(delta);
	} else if (launcherMode) {
		ProcessWPAD_Buttons(mods);
	} else if (type == WPAD_EXP_NUNCHUK) {
		ProcessNunchuck_Game(mods, delta);
	} else {
		ProcessWPAD_Buttons(mods);
	}

	int x, y;
	GetIRPos(res, &x, &y);
	
	if (mods & WPAD_BUTTON_B) {
		if (!dragActive) {
			dragStartX = dragCurX = x;
			dragStartY = dragCurY = y;
		}
		dragActive = true;
	} else {
		dragActive = false;
	}
	Pointer_SetPosition(0, x, y);
}

void Window_ProcessEvents(double delta) {
	Input.JoystickMovement = false;
	Input.Sources = INPUT_SOURCE_GAMEPAD;

	ProcessWPADInput(delta);
	ProcessPADInput(delta);
	ProcessKeyboardInput();
}

static void ScanAndGetIRPos(int* x, int* y) {
	u32 type;
	WPAD_ScanPads();
	
	int res = WPAD_Probe(0, &type);
	GetIRPos(res, x, y);
}
#define FACTOR 2
void Window_UpdateRawMouse(void)  {
	if (!dragActive) return;
	int x, y;
	ScanAndGetIRPos(&x, &y);
   
	// TODO: Refactor the logic. is it 100% right too?
	dragCurX = dragStartX + (dragCurX - dragStartX) / FACTOR;
	dragCurY = dragStartY + (dragCurY - dragStartY) / FACTOR;
	
	int dx = x - dragCurX; Math_Clamp(dx, -40, 40);
	int dy = y - dragCurY; Math_Clamp(dy, -40, 40);
	Event_RaiseRawMove(&PointerEvents.RawMoved, dx, dy);
	
	dragCurX = x; dragCurY = y;
}
#else
void Window_ProcessEvents(double delta) {
	Input.JoystickMovement = false;
	
	ProcessPADInput(delta);
}

void Window_UpdateRawMouse(void) { }
#endif

void Cursor_SetPosition(int x, int y) { } // No point in GameCube/Wii
// TODO: Display cursor on Wii when not raw mode
void Window_EnableRawMouse(void)  { Input.RawMode = true;  }
void Window_DisableRawMouse(void) { Input.RawMode = false; }


/*########################################################################################################################*
*------------------------------------------------------Framebuffer--------------------------------------------------------*
*#########################################################################################################################*/
void Window_AllocFramebuffer(struct Bitmap* bmp) {
	bmp->scan0 = (BitmapCol*)Mem_Alloc(bmp->width * bmp->height, 4, "window pixels");
}

// TODO: Get rid of this complexity and use the 3D API instead..
// https://github.com/devkitPro/gamecube-examples/blob/master/graphics/fb/pageflip/source/flip.c
static u32 CvtRGB (u8 r1, u8 g1, u8 b1, u8 r2, u8 g2, u8 b2)
{
  int y1, cb1, cr1, y2, cb2, cr2, cb, cr;
 
  y1  = (299    * r1 +   587 * g1 +   114 * b1) / 1000;
  cb1 = (-16874 * r1 - 33126 * g1 + 50000 * b1 + 12800000) / 100000;
  cr1 = (50000  * r1 - 41869 * g1 -  8131 * b1 + 12800000) / 100000;
 
  y2  = (299    * r2 +   587 * g2 +   114 * b2) / 1000;
  cb2 = (-16874 * r2 - 33126 * g2 + 50000 * b2 + 12800000) / 100000;
  cr2 = (50000  * r2 - 41869 * g2 -  8131 * b2 + 12800000) / 100000;
 
  cb = (cb1 + cb2) >> 1;
  cr = (cr1 + cr2) >> 1;
 
  return (y1 << 24) | (cb << 16) | (y2 << 8) | cr;
}

void Window_DrawFramebuffer(Rect2D r, struct Bitmap* bmp) {
	// When coming back from the 3D game, framebuffer might have changed
	if (needsFBUpdate) {
		VIDEO_SetNextFramebuffer(xfb);
		VIDEO_Flush();
		needsFBUpdate = false;
	}
	
	VIDEO_WaitVSync();
	r.x &= ~0x01; // round down to nearest even horizontal index
	
	// TODO XFB is raw yuv, but is absolutely a pain to work with..
	for (int y = r.y; y < r.y + r.height; y++) 
	{
		cc_uint32* src = bmp->scan0 + y * bmp->width     + r.x;
		u16* dst       = (u16*)xfb  + y * rmode->fbWidth + r.x;
		
		for (int x = 0; x < r.width / 2; x++) {
			cc_uint32 rgb0 = src[(x<<1) + 0];
			cc_uint32 rgb1 = src[(x<<1) + 1];
			
			((u32*)dst)[x] = CvtRGB(BitmapCol_R(rgb0), BitmapCol_G(rgb0), BitmapCol_B(rgb0),
					BitmapCol_R(rgb1),  BitmapCol_G(rgb1), BitmapCol_B(rgb1));
		}
	}
}

void Window_FreeFramebuffer(struct Bitmap* bmp) {
	Mem_Free(bmp->scan0);
}


/*########################################################################################################################*
*------------------------------------------------------Soft keyboard------------------------------------------------------*
*#########################################################################################################################*/
void OnscreenKeyboard_Open(struct OpenKeyboardArgs* args) {
	if (Input.Sources & INPUT_SOURCE_NORMAL) return;
	VirtualKeyboard_Open(args, launcherMode);
}

void OnscreenKeyboard_SetText(const cc_string* text) {
	VirtualKeyboard_SetText(text);
}

void OnscreenKeyboard_Draw2D(Rect2D* r, struct Bitmap* bmp) {
	VirtualKeyboard_Display2D(r, bmp);
}

void OnscreenKeyboard_Draw3D(void) {
	VirtualKeyboard_Display3D();
}

void OnscreenKeyboard_Close(void) {
	VirtualKeyboard_Close();
}


/*########################################################################################################################*
*-------------------------------------------------------Misc/Other--------------------------------------------------------*
*#########################################################################################################################*/
void Window_SetTitle(const cc_string* title)   { }
void Clipboard_GetText(cc_string* value)       { }
void Clipboard_SetText(const cc_string* value) { }

int Window_GetWindowState(void) { return WINDOW_STATE_FULLSCREEN; }
cc_result Window_EnterFullscreen(void) { return 0; }
cc_result Window_ExitFullscreen(void)  { return 0; }
int Window_IsObscured(void)            { return 0; }

void Window_Show(void) { }
void Window_SetSize(int width, int height) { }


void Window_ShowDialog(const char* title, const char* msg) {
	/* TODO implement */
	Platform_LogConst(title);
	Platform_LogConst(msg);
}

cc_result Window_OpenFileDialog(const struct OpenFileDialogArgs* args) {
	return ERR_NOT_SUPPORTED;
}

cc_result Window_SaveFileDialog(const struct SaveFileDialogArgs* args) {
	return ERR_NOT_SUPPORTED;
}
#endif
