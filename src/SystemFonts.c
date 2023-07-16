#include "SystemFonts.h"
#include "Drawer2D.h"
#include "String.h"
#include "Funcs.h"
#include "Platform.h"
#include "ExtMath.h"
#include "Logger.h"
#include "Game.h"
#include "Event.h"
#include "Stream.h"
#include "Utils.h"
#include "Errors.h"
#include "Window.h"
#include "Options.h"

static char defaultBuffer[STRING_SIZE];
static cc_string font_default = String_FromArray(defaultBuffer);

void SysFont_SetDefault(const cc_string* fontName) {
	String_Copy(&font_default, fontName);
	Event_RaiseVoid(&ChatEvents.FontChanged);
}

static void OnInit(void) {
	Options_Get(OPT_FONT_NAME, &font_default, "");
	if (Game_ClassicMode) font_default.length = 0;
}

struct IGameComponent SystemFonts_Component = {
	OnInit /* Init  */
};


void SysFonts_Register(const cc_string* path) { }

const cc_string* SysFonts_UNSAFE_GetDefault(void) { return &String_Empty; }

void SysFonts_GetNames(struct StringsBuffer* buffer) { }

cc_result SysFont_Make(struct FontDesc* desc, const cc_string* fontName, int size, int flags) {
	desc->size   = size;
	desc->flags  = flags;
	desc->height = Drawer2D_AdjHeight(size);

	desc->handle = (void*)1;
	return 0;
}

void SysFont_MakeDefault(struct FontDesc* desc, int size, int flags) {
	SysFont_Make(desc, NULL, size, flags);
}

void SysFont_Free(struct FontDesc* desc) {
}

int SysFont_TextWidth(struct DrawTextArgs* args) {
	return 10;
}

void SysFont_DrawText(struct DrawTextArgs* args, struct Bitmap* bmp, int x, int y, cc_bool shadow) {
}