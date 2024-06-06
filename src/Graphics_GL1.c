/* Silence deprecation warnings on modern macOS/iOS */
#define GL_SILENCE_DEPRECATION
#define GLES_SILENCE_DEPRECATION

#include "Core.h"
#if CC_GFX_BACKEND == CC_GFX_BACKEND_GL1
#include "_GraphicsBase.h"
#include "Errors.h"
#include "Window.h"
/* The OpenGL backend is a bit of a mess, since it's really 2 backends in one:
 * - OpenGL 1.1 (completely lacking GPU, fallbacks to say Windows built-in software rasteriser)
 * - OpenGL 1.5 or OpenGL 1.2 + GL_ARB_vertex_buffer_object (default desktop backend)
*/

#if defined CC_BUILD_WIN
	/* Avoid pointless includes */
	#define WIN32_LEAN_AND_MEAN
	#define NOSERVICE
	#define NOMCX
	#define NOIME
	#include <windows.h>
	#define GLAPI WINGDIAPI
#else
	#define GLAPI extern
	#define APIENTRY
#endif
/* === BEGIN OPENGL HEADERS === */
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef signed char GLbyte;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef void GLvoid;

/* NOTE: With the OpenGL 1.1 backend "pointer" arguments are actual pointers, */
/* but with VBOs they are just offsets instead */
#ifdef CC_BUILD_GL11
typedef const void* GLpointer;
#else
typedef cc_uintptr GLpointer;
#endif

#define GL_LEQUAL                0x0203
#define GL_GREATER               0x0204

#define GL_DEPTH_BUFFER_BIT      0x00000100
#define GL_COLOR_BUFFER_BIT      0x00004000

#define GL_LINES                 0x0001
#define GL_TRIANGLES             0x0004
#define GL_QUADS                 0x0007

#define GL_BLEND                 0x0BE2
#define GL_SRC_ALPHA             0x0302
#define GL_ONE_MINUS_SRC_ALPHA   0x0303

#define GL_UNSIGNED_BYTE         0x1401
#define GL_UNSIGNED_SHORT        0x1403
#define GL_UNSIGNED_INT          0x1405
#define GL_FLOAT                 0x1406
#define GL_RGBA                  0x1908

#define GL_FOG                   0x0B60
#define GL_FOG_DENSITY           0x0B62
#define GL_FOG_END               0x0B64
#define GL_FOG_MODE              0x0B65
#define GL_FOG_COLOR             0x0B66
#define GL_LINEAR                0x2601
#define GL_EXP                   0x0800
#define GL_EXP2                  0x0801

#define GL_CULL_FACE             0x0B44
#define GL_DEPTH_TEST            0x0B71
#define GL_MATRIX_MODE           0x0BA0
#define GL_VIEWPORT              0x0BA2
#define GL_ALPHA_TEST            0x0BC0
#define GL_MAX_TEXTURE_SIZE      0x0D33
#define GL_DEPTH_BITS            0x0D56

#define GL_FOG_HINT              0x0C54
#define GL_NICEST                0x1102
#define GL_COMPILE               0x1300

#define GL_MODELVIEW             0x1700
#define GL_PROJECTION            0x1701
#define GL_TEXTURE               0x1702

#define GL_VENDOR                0x1F00
#define GL_RENDERER              0x1F01
#define GL_VERSION               0x1F02
#define GL_EXTENSIONS            0x1F03

#define GL_TEXTURE_2D            0x0DE1
#define GL_NEAREST               0x2600
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_TEXTURE_MAG_FILTER    0x2800
#define GL_TEXTURE_MIN_FILTER    0x2801

#define GL_VERTEX_ARRAY          0x8074
#define GL_COLOR_ARRAY           0x8076
#define GL_TEXTURE_COORD_ARRAY   0x8078

/* Not present in gl.h on Windows (only up to OpenGL 1.1) */
#define GL_ARRAY_BUFFER          0x8892
#define GL_ELEMENT_ARRAY_BUFFER  0x8893
#define GL_STATIC_DRAW           0x88E4
#define GL_DYNAMIC_DRAW          0x88E8

#define GL_FUNC(_retType, name) GLAPI _retType APIENTRY name
#include "_GL1Funcs.h"
/* === END OPENGL HEADERS === */

#if defined CC_BUILD_GL11
static GLuint activeList;
#define gl_DYNAMICLISTID 1234567891
static void* dynamicListData;
static cc_uint16 gl_indices[GFX_MAX_INDICES];
#else
/* OpenGL functions use stdcall instead of cdecl on Windows */
static void (APIENTRY *_glBindBuffer)(GLenum target, GfxResourceID buffer); /* NOTE: buffer is actually a GLuint in OpenGL */
static void (APIENTRY *_glDeleteBuffers)(GLsizei n, const GLuint *buffers);
static void (APIENTRY *_glGenBuffers)(GLsizei n, GLuint *buffers);
static void (APIENTRY *_glBufferData)(GLenum target, cc_uintptr size, const GLvoid* data, GLenum usage);
static void (APIENTRY *_glBufferSubData)(GLenum target, cc_uintptr offset, cc_uintptr size, const GLvoid* data);
#endif

static void GLContext_GetAll(const struct DynamicLibSym* syms, int count) {
	int i;
	for (i = 0; i < count; i++) 
	{
		*syms[i].symAddr = GLContext_GetAddress(syms[i].name);
	}
}


#if defined CC_BUILD_WIN && !defined CC_BUILD_GL11
/* Note the following about calling OpenGL functions on Windows */
/*  1) wglGetProcAddress returns a context specific address */
/*  2) dllimport functions are implemented using indirect function pointers */
/*     https://web.archive.org/web/20080321171626/http://blogs.msdn.com/oldnewthing/archive/2006/07/20/672695.aspx */
/*     https://web.archive.org/web/20071016185327/http://blogs.msdn.com/oldnewthing/archive/2006/07/27/680250.aspx */
/* Therefore one layer of indirection can be avoided by calling wglGetProcAddress functions instead */
/*  e.g. if _glDrawElements = wglGetProcAddress("glDrawElements") */
/*    call [glDrawElements]  --> opengl32.dll thunk--> GL driver thunk --> GL driver implementation */
/*    call [_glDrawElements] --> GL driver thunk --> GL driver implementation */

#undef GL_FUNC
#define GL_FUNC(_retType, name) typedef _retType (APIENTRY *FP_ ## name)
#include "_GL1Funcs.h"

static FP_glColorPointer    _glColorPointer;
static FP_glTexCoordPointer _glTexCoordPointer;
static FP_glVertexPointer   _glVertexPointer;

static FP_glDrawArrays   _glDrawArrays;
static FP_glDrawElements _glDrawElements;

static FP_glBindTexture    _glBindTexture;
static FP_glDeleteTextures _glDeleteTextures;
static FP_glGenTextures    _glGenTextures;
static FP_glTexImage2D     _glTexImage2D;
static FP_glTexSubImage2D  _glTexSubImage2D;

static const struct DynamicLibSym coreFuncs[] = {
	DynamicLib_Sym2("glColorPointer",    glColorPointer),
	DynamicLib_Sym2("glTexCoordPointer", glTexCoordPointer),
	DynamicLib_Sym2("glVertexPointer",   glVertexPointer),

	DynamicLib_Sym2("glDrawArrays",   glDrawArrays),
	DynamicLib_Sym2("glDrawElements", glDrawElements),

	DynamicLib_Sym2("glBindTexture",    glBindTexture),
	DynamicLib_Sym2("glDeleteTextures", glDeleteTextures),
	DynamicLib_Sym2("glGenTextures",    glGenTextures),
	DynamicLib_Sym2("glTexImage2D",     glTexImage2D),
	DynamicLib_Sym2("glTexSubImage2D",  glTexSubImage2D),
};

static void LoadCoreFuncs(void) {
	GLContext_GetAll(coreFuncs, Array_Elems(coreFuncs));
}
#else
#define _glColorPointer    glColorPointer
#define _glTexCoordPointer glTexCoordPointer
#define _glVertexPointer   glVertexPointer

#define _glDrawArrays      glDrawArrays
#define _glDrawElements    glDrawElements

#define _glBindTexture    glBindTexture
#define _glDeleteTextures glDeleteTextures
#define _glGenTextures    glGenTextures
#define _glTexImage2D     glTexImage2D
#define _glTexSubImage2D  glTexSubImage2D
#endif

typedef void (*GL_SetupVBFunc)(void);
typedef void (*GL_SetupVBRangeFunc)(int startVertex);
static GL_SetupVBFunc gfx_setupVBFunc;
static GL_SetupVBRangeFunc gfx_setupVBRangeFunc;
#include "_GLShared.h"

/*########################################################################################################################*
*-------------------------------------------------------Index buffers-----------------------------------------------------*
*#########################################################################################################################*/
#ifndef CC_BUILD_GL11
GfxResourceID Gfx_CreateIb2(int count, Gfx_FillIBFunc fillFunc, void* obj) {
	cc_uint16 indices[GFX_MAX_INDICES];
	GfxResourceID id = NULL;
	cc_uint32 size   = count * sizeof(cc_uint16);

	_glGenBuffers(1, (GLuint*)&id);
	fillFunc(indices, count, obj);
	_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, id);
	_glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, indices, GL_STATIC_DRAW);
	return id;
}

void Gfx_BindIb(GfxResourceID ib) { _glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib); }

void Gfx_DeleteIb(GfxResourceID* ib) {
	GfxResourceID id = *ib;
	if (!id) return;

	_glDeleteBuffers(1, (GLuint*)&id);
	*ib = 0;
}
#else
GfxResourceID Gfx_CreateIb2(int count, Gfx_FillIBFunc fillFunc, void* obj) { return 0; }
void Gfx_BindIb(GfxResourceID ib) { }
void Gfx_DeleteIb(GfxResourceID* ib) { }
#endif


/*########################################################################################################################*
*------------------------------------------------------Vertex buffers-----------------------------------------------------*
*#########################################################################################################################*/
#ifndef CC_BUILD_GL11
static GfxResourceID Gfx_AllocStaticVb(VertexFormat fmt, int count) {
	GfxResourceID id = NULL;
	_glGenBuffers(1, (GLuint*)&id);
	_glBindBuffer(GL_ARRAY_BUFFER, id);
	return id;
}

void Gfx_BindVb(GfxResourceID vb) { 
	_glBindBuffer(GL_ARRAY_BUFFER, vb); 
}

void Gfx_DeleteVb(GfxResourceID* vb) {
	GfxResourceID id = *vb;
	if (id) _glDeleteBuffers(1, (GLuint*)&id);
	*vb = 0;
}

void* Gfx_LockVb(GfxResourceID vb, VertexFormat fmt, int count) {
	return FastAllocTempMem(count * strideSizes[fmt]);
}

void Gfx_UnlockVb(GfxResourceID vb) {
	_glBufferData(GL_ARRAY_BUFFER, tmpSize, tmpData, GL_STATIC_DRAW);
}
#else
static GfxResourceID Gfx_AllocStaticVb(VertexFormat fmt, int count) { 
	return glGenLists(1); 
}
void Gfx_BindVb(GfxResourceID vb) { activeList = ptr_to_uint(vb); }

void Gfx_DeleteVb(GfxResourceID* vb) {
	GLuint id = ptr_to_uint(*vb);
	if (id) glDeleteLists(id, 1);
	*vb = 0;
}

static void UpdateDisplayList(GLuint list, void* vertices, VertexFormat fmt, int count) {
	/* We need to restore client state afer building the list */
	int realFormat = gfx_format;
	void* dyn_data = dynamicListData;
	Gfx_SetVertexFormat(fmt);
	dynamicListData = vertices;

	glNewList(list, GL_COMPILE);
	gfx_setupVBFunc();
	glDrawElements(GL_TRIANGLES, ICOUNT(count), GL_UNSIGNED_SHORT, gl_indices);
	glEndList();

	Gfx_SetVertexFormat(realFormat);
	dynamicListData = dyn_data;
}

/* NOTE! Building chunk in Builder.c relies on vb being ignored */
/* If that changes, you must fix Builder.c to properly call Gfx_LockVb */
static VertexFormat tmpFormat;
static int tmpCount;
void* Gfx_LockVb(GfxResourceID vb, VertexFormat fmt, int count) {
	tmpFormat = fmt;
	tmpCount  = count;
	return FastAllocTempMem(count * strideSizes[fmt]);
}

void Gfx_UnlockVb(GfxResourceID vb) {
	UpdateDisplayList((GLuint)vb, tmpData, tmpFormat, tmpCount);
}

GfxResourceID Gfx_CreateVb2(void* vertices, VertexFormat fmt, int count) {
	GLuint list = glGenLists(1);
	UpdateDisplayList(list, vertices, fmt, count);
	return list;
}
#endif


/*########################################################################################################################*
*--------------------------------------------------Dynamic vertex buffers-------------------------------------------------*
*#########################################################################################################################*/
#ifndef CC_BUILD_GL11
static GfxResourceID Gfx_AllocDynamicVb(VertexFormat fmt, int maxVertices) {
	GfxResourceID id = NULL;
	cc_uint32 size   = maxVertices * strideSizes[fmt];

	_glGenBuffers(1, (GLuint*)&id);
	_glBindBuffer(GL_ARRAY_BUFFER, id);
	_glBufferData(GL_ARRAY_BUFFER, size, NULL, GL_DYNAMIC_DRAW);
	return id;
}

void Gfx_BindDynamicVb(GfxResourceID vb) {
	_glBindBuffer(GL_ARRAY_BUFFER, vb); 
}

void Gfx_DeleteDynamicVb(GfxResourceID* vb) {
	GfxResourceID id = *vb;
	if (id) _glDeleteBuffers(1, (GLuint*)&id);
	*vb = 0;
}

void* Gfx_LockDynamicVb(GfxResourceID vb, VertexFormat fmt, int count) {
	return FastAllocTempMem(count * strideSizes[fmt]);
}

void Gfx_UnlockDynamicVb(GfxResourceID vb) {
	_glBindBuffer(GL_ARRAY_BUFFER, vb);
	_glBufferSubData(GL_ARRAY_BUFFER, 0, tmpSize, tmpData);
}

void Gfx_SetDynamicVbData(GfxResourceID vb, void* vertices, int vCount) {
	cc_uint32 size = vCount * gfx_stride;
	_glBindBuffer(GL_ARRAY_BUFFER, vb);
	_glBufferSubData(GL_ARRAY_BUFFER, 0, size, vertices);
}
#else
static GfxResourceID Gfx_AllocDynamicVb(VertexFormat fmt, int maxVertices) {
	return (GfxResourceID)Mem_TryAlloc(maxVertices, strideSizes[fmt]);
}

void Gfx_BindDynamicVb(GfxResourceID vb) {
	activeList      = gl_DYNAMICLISTID;
	dynamicListData = vb;
}

void Gfx_DeleteDynamicVb(GfxResourceID* vb) {
	void* addr = *vb;
	if (addr) Mem_Free(addr);
	*vb = 0;
}

void* Gfx_LockDynamicVb(GfxResourceID vb, VertexFormat fmt, int count) { return vb; }
void  Gfx_UnlockDynamicVb(GfxResourceID vb) { Gfx_BindDynamicVb(vb); }

void Gfx_SetDynamicVbData(GfxResourceID vb, void* vertices, int vCount) {
	Gfx_BindDynamicVb(vb);
	Mem_Copy(vb, vertices, vCount * gfx_stride);
}
#endif


/*########################################################################################################################*
*----------------------------------------------------------Drawing--------------------------------------------------------*
*#########################################################################################################################*/
#ifdef CC_BUILD_GL11
	/* point to client side dynamic array */
	#define VB_PTR ((cc_uint8*)dynamicListData)
	#define IB_PTR gl_indices
#else
	/* no client side array, use vertex buffer object */
	#define VB_PTR 0
	#define IB_PTR NULL
#endif

static void GL_SetupVbColoured(void) {
	_glVertexPointer(3, GL_FLOAT,        SIZEOF_VERTEX_COLOURED, VB_PTR +  0);
	_glColorPointer(4, GL_UNSIGNED_BYTE, SIZEOF_VERTEX_COLOURED, VB_PTR + 12);
}

static void GL_SetupVbTextured(void) {
	_glVertexPointer(3, GL_FLOAT,        SIZEOF_VERTEX_TEXTURED, VB_PTR +  0);
	_glColorPointer(4, GL_UNSIGNED_BYTE, SIZEOF_VERTEX_TEXTURED, VB_PTR + 12);
	_glTexCoordPointer(2, GL_FLOAT,      SIZEOF_VERTEX_TEXTURED, VB_PTR + 16);
}

static void GL_SetupVbColoured_Range(int startVertex) {
	cc_uint32 offset = startVertex * SIZEOF_VERTEX_COLOURED;
	_glVertexPointer(3, GL_FLOAT,          SIZEOF_VERTEX_COLOURED, VB_PTR + offset +  0);
	_glColorPointer(4, GL_UNSIGNED_BYTE,   SIZEOF_VERTEX_COLOURED, VB_PTR + offset + 12);
}

static void GL_SetupVbTextured_Range(int startVertex) {
	cc_uint32 offset = startVertex * SIZEOF_VERTEX_TEXTURED;
	_glVertexPointer(3,  GL_FLOAT,         SIZEOF_VERTEX_TEXTURED, VB_PTR + offset +  0);
	_glColorPointer(4, GL_UNSIGNED_BYTE,   SIZEOF_VERTEX_TEXTURED, VB_PTR + offset + 12);
	_glTexCoordPointer(2, GL_FLOAT,        SIZEOF_VERTEX_TEXTURED, VB_PTR + offset + 16);
}

void Gfx_SetVertexFormat(VertexFormat fmt) {
	if (fmt == gfx_format) return;
	gfx_format = fmt;
	gfx_stride = strideSizes[fmt];

	if (fmt == VERTEX_FORMAT_TEXTURED) {
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glEnable(GL_TEXTURE_2D);

		gfx_setupVBFunc      = GL_SetupVbTextured;
		gfx_setupVBRangeFunc = GL_SetupVbTextured_Range;
	} else {
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisable(GL_TEXTURE_2D);

		gfx_setupVBFunc      = GL_SetupVbColoured;
		gfx_setupVBRangeFunc = GL_SetupVbColoured_Range;
	}
}

void Gfx_DrawVb_Lines(int verticesCount) {
	gfx_setupVBFunc();
	_glDrawArrays(GL_LINES, 0, verticesCount);
}

void Gfx_DrawVb_IndexedTris_Range(int verticesCount, int startVertex) {
#ifdef CC_BUILD_GL11
	if (activeList != gl_DYNAMICLISTID) { glCallList(activeList); return; }
#endif
	gfx_setupVBRangeFunc(startVertex);
	_glDrawElements(GL_TRIANGLES, ICOUNT(verticesCount), GL_UNSIGNED_SHORT, IB_PTR);
}

void Gfx_DrawVb_IndexedTris(int verticesCount) {
#ifdef CC_BUILD_GL11
	if (activeList != gl_DYNAMICLISTID) { glCallList(activeList); return; }
#endif
	gfx_setupVBFunc();
	_glDrawElements(GL_TRIANGLES, ICOUNT(verticesCount), GL_UNSIGNED_SHORT, IB_PTR);
}

#ifdef CC_BUILD_GL11
void Gfx_DrawIndexedTris_T2fC4b(int verticesCount, int startVertex) { glCallList(activeList); }
#else
void Gfx_DrawIndexedTris_T2fC4b(int verticesCount, int startVertex) {
	cc_uint32 offset = startVertex * SIZEOF_VERTEX_TEXTURED;
	_glVertexPointer(3, GL_FLOAT,        SIZEOF_VERTEX_TEXTURED, VB_PTR + offset +  0);
	_glColorPointer(4, GL_UNSIGNED_BYTE, SIZEOF_VERTEX_TEXTURED, VB_PTR + offset + 12);
	_glTexCoordPointer(2, GL_FLOAT,      SIZEOF_VERTEX_TEXTURED, VB_PTR + offset + 16);
	_glDrawElements(GL_TRIANGLES,        ICOUNT(verticesCount),  GL_UNSIGNED_SHORT, IB_PTR);
}
#endif /* !CC_BUILD_GL11 */


/*########################################################################################################################*
*---------------------------------------------------------Textures--------------------------------------------------------*
*#########################################################################################################################*/
void Gfx_BindTexture(GfxResourceID texId) {
	_glBindTexture(GL_TEXTURE_2D, ptr_to_uint(texId));
}


/*########################################################################################################################*
*-----------------------------------------------------State management----------------------------------------------------*
*#########################################################################################################################*/
static PackedCol gfx_fogColor;
static float gfx_fogEnd = -1.0f, gfx_fogDensity = -1.0f;
static int gfx_fogMode  = -1;

void Gfx_SetFog(cc_bool enabled) {
	gfx_fogEnabled = enabled;
	if (enabled) { glEnable(GL_FOG); } else { glDisable(GL_FOG); }
}

void Gfx_SetFogCol(PackedCol color) {
	float rgba[4];
	if (color == gfx_fogColor) return;

	rgba[0] = PackedCol_R(color) / 255.0f; 
	rgba[1] = PackedCol_G(color) / 255.0f;
	rgba[2] = PackedCol_B(color) / 255.0f; 
	rgba[3] = PackedCol_A(color) / 255.0f;

	glFogfv(GL_FOG_COLOR, rgba);
	gfx_fogColor = color;
}

void Gfx_SetFogDensity(float value) {
	if (value == gfx_fogDensity) return;
	glFogf(GL_FOG_DENSITY, value);
	gfx_fogDensity = value;
}

void Gfx_SetFogEnd(float value) {
	if (value == gfx_fogEnd) return;
	glFogf(GL_FOG_END, value);
	gfx_fogEnd = value;
}

void Gfx_SetFogMode(FogFunc func) {
	static GLint modes[3] = { GL_LINEAR, GL_EXP, GL_EXP2 };
	if (func == gfx_fogMode) return;

#ifdef CC_BUILD_GLES
	/* OpenGL ES doesn't support glFogi, so use glFogf instead */
	/*  https://www.khronos.org/registry/OpenGL-Refpages/es1.1/xhtml/ */
	glFogf(GL_FOG_MODE, modes[func]);
#else
	glFogi(GL_FOG_MODE, modes[func]);
#endif
	gfx_fogMode = func;
}

static void SetAlphaTest(cc_bool enabled) {
	if (enabled) { glEnable(GL_ALPHA_TEST); } else { glDisable(GL_ALPHA_TEST); }
}

void Gfx_DepthOnlyRendering(cc_bool depthOnly) {
	cc_bool enabled = !depthOnly;
	SetColorWrite(enabled & gfx_colorMask[0], enabled & gfx_colorMask[1], 
				  enabled & gfx_colorMask[2], enabled & gfx_colorMask[3]);
	if (enabled) { glEnable(GL_TEXTURE_2D); } else { glDisable(GL_TEXTURE_2D); }
}


/*########################################################################################################################*
*---------------------------------------------------------Matrices--------------------------------------------------------*
*#########################################################################################################################*/
static GLenum matrix_modes[3] = { GL_PROJECTION, GL_MODELVIEW, GL_TEXTURE };
static int lastMatrix;

void Gfx_LoadMatrix(MatrixType type, const struct Matrix* matrix) {
	if (type != lastMatrix) { lastMatrix = type; glMatrixMode(matrix_modes[type]); }
	glLoadMatrixf((const float*)matrix);
}

void Gfx_LoadIdentityMatrix(MatrixType type) {
	if (type != lastMatrix) { lastMatrix = type; glMatrixMode(matrix_modes[type]); }
	glLoadIdentity();
}

static struct Matrix texMatrix = Matrix_IdentityValue;
void Gfx_EnableTextureOffset(float x, float y) {
	texMatrix.row4.x = x; texMatrix.row4.y = y;
	Gfx_LoadMatrix(2, &texMatrix);
}

void Gfx_DisableTextureOffset(void) { Gfx_LoadIdentityMatrix(2); }


/*########################################################################################################################*
*-------------------------------------------------------State setup-------------------------------------------------------*
*#########################################################################################################################*/
static void Gfx_FreeState(void) { FreeDefaultResources(); }
static void Gfx_RestoreState(void) {
	InitDefaultResources();
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	gfx_format = -1;

	glHint(GL_FOG_HINT, GL_NICEST);
	glAlphaFunc(GL_GREATER, 0.5f);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthFunc(GL_LEQUAL);
}

cc_bool Gfx_WarnIfNecessary(void) {
	cc_string renderer = String_FromReadonly((const char*)glGetString(GL_RENDERER));
	
#ifdef CC_BUILD_GL11
	Chat_AddRaw("&cYou are using the very outdated OpenGL backend.");
	Chat_AddRaw("&cAs such you may experience poor performance.");
	Chat_AddRaw("&cIt is likely you need to install video card drivers.");
#endif

	if (String_ContainsConst(&renderer, "llvmpipe")) {
		Chat_AddRaw("&cSoftware rendering is being used, performance will greatly suffer.");
		Chat_AddRaw("&cVSync may not work, and you may see disappearing clouds and map edges.");
		return true;
	}
	if (String_ContainsConst(&renderer, "Intel")) {
		Chat_AddRaw("&cIntel graphics cards are known to have issues with the OpenGL build.");
		Chat_AddRaw("&cVSync may not work, and you may see disappearing clouds and map edges.");
		#ifdef CC_BUILD_WIN
		Chat_AddRaw("&cTry downloading the Direct3D 9 build instead.");
		#endif
		return true;
	}
	return false;
}


/*########################################################################################################################*
*-------------------------------------------------------Compatibility-----------------------------------------------------*
*#########################################################################################################################*/
#ifdef CC_BUILD_GL11
static void GLBackend_Init(void) { MakeIndices(gl_indices, GFX_MAX_INDICES, NULL); }
#else

#if defined CC_BUILD_WIN
static FP_glDrawElements    _realDrawElements;
static FP_glColorPointer    _realColorPointer;
static FP_glTexCoordPointer _realTexCoordPointer;
static FP_glVertexPointer   _realVertexPointer;

/* On Windows, can replace the GL function drawing with these 1.1 fallbacks */
/* fake vertex buffer objects by using client side pointers instead */
typedef struct legacy_buffer { cc_uint8* data; } legacy_buffer;
static legacy_buffer* cur_ib;
static legacy_buffer* cur_vb;
#define legacy_GetBuffer(target) (target == GL_ELEMENT_ARRAY_BUFFER ? &cur_ib : &cur_vb);

static void APIENTRY legacy_genBuffer(GLsizei n, GLuint* buffer) {
	GfxResourceID* dst = (GfxResourceID*)buffer;
	*dst = Mem_TryAllocCleared(1, sizeof(legacy_buffer));
}

static void APIENTRY legacy_deleteBuffer(GLsizei n, const GLuint* buffer) {
	GfxResourceID* dst = (GfxResourceID*)buffer;
	Mem_Free(*dst);
}

static void APIENTRY legacy_bindBuffer(GLenum target, GfxResourceID src) {
	legacy_buffer** buffer = legacy_GetBuffer(target);
	*buffer = (legacy_buffer*)src;
}

static void APIENTRY legacy_bufferData(GLenum target, cc_uintptr size, const GLvoid* data, GLenum usage) {
	legacy_buffer* buffer = *legacy_GetBuffer(target);
	Mem_Free(buffer->data);

	buffer->data = Mem_TryAlloc(size, 1);
	if (data) Mem_Copy(buffer->data, data, size);
}

static void APIENTRY legacy_bufferSubData(GLenum target, cc_uintptr offset, cc_uintptr size, const GLvoid* data) {
	legacy_buffer* buffer = *legacy_GetBuffer(target);
	Mem_Copy(buffer->data, data, size);
}


static void APIENTRY gl10_bindTexture(GLenum target, GLuint texture) {
	
}
static void APIENTRY gl10_deleteTexture(GLsizei n, const GLuint* textures) {

}
static void APIENTRY gl10_genTexture(GLsizei n, GLuint* textures) {

}
static void APIENTRY gl10_texImage(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* pixels) {
	
}
static void APIENTRY gl10_texSubImage(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid* pixels) {
	
}

static cc_uint8* gl10_vb;
static void APIENTRY gl10_drawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices) {
	/* TODO */
	int i;
	glBegin(GL_QUADS);
	count = (count * 4) / 6;

	if (gfx_format == VERTEX_FORMAT_TEXTURED) {
		struct VertexTextured* src = (struct VertexTextured*)gl10_vb;
		for (i = 0; i < count; i++, src++) 
		{
			glColor4ub(PackedCol_R(src->Col), PackedCol_G(src->Col), PackedCol_B(src->Col), PackedCol_A(src->Col));
			glTexCoord2f(src->U, src->V);
			glVertex3f(src->x, src->y, src->z);
		}
	} else {
		struct VertexColoured* src = (struct VertexColoured*)gl10_vb;
		for (i = 0; i < count; i++, src++) 
		{
			glColor4ub(PackedCol_R(src->Col), PackedCol_G(src->Col), PackedCol_B(src->Col), PackedCol_A(src->Col));
			glVertex3f(src->x, src->y, src->z);
		}
	}

	glEnd();
}
static void APIENTRY gl10_colorPointer(GLint size, GLenum type, GLsizei stride, GLpointer offset) {
}
static void APIENTRY gl10_texCoordPointer(GLint size, GLenum type, GLsizei stride, GLpointer offset) {
}
static void APIENTRY gl10_vertexPointer(GLint size, GLenum type, GLsizei stride, GLpointer offset) {
	gl10_vb = cur_vb->data + offset;
}


static void APIENTRY gl11_drawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices) {
	_realDrawElements(mode, count, type, (cc_uintptr)indices + cur_ib->data);
}
static void APIENTRY gl11_colorPointer(GLint size, GLenum type, GLsizei stride, GLpointer offset) {
	_realColorPointer(size,    type, stride, (cc_uintptr)cur_vb->data + offset);
}
static void APIENTRY gl11_texCoordPointer(GLint size, GLenum type, GLsizei stride, GLpointer offset) {
	_realTexCoordPointer(size, type, stride, (cc_uintptr)cur_vb->data + offset);
}
static void APIENTRY gl11_vertexPointer(GLint size, GLenum type, GLsizei stride, GLpointer offset) {
	_realVertexPointer(size,   type, stride, (cc_uintptr)cur_vb->data + offset);
}


static void FallbackOpenGL(void) {
	Window_ShowDialog("Performance warning",
		"Your system only supports only OpenGL 1.1\n" \
		"This is usually caused by graphics drivers not being installed\n\n" \
		"As such you will likely experience very poor performance");
	customMipmapsLevels = false;
		
	_glGenBuffers    = legacy_genBuffer;
	_glDeleteBuffers = legacy_deleteBuffer;
	_glBindBuffer    = legacy_bindBuffer;
	_glBufferData    = legacy_bufferData;
	_glBufferSubData = legacy_bufferSubData;

	_realDrawElements    = _glDrawElements;    _realColorPointer  = _glColorPointer;
	_realTexCoordPointer = _glTexCoordPointer; _realVertexPointer = _glVertexPointer;

	_glDrawElements    = gl11_drawElements;    _glColorPointer  = gl11_colorPointer;
	_glTexCoordPointer = gl11_texCoordPointer; _glVertexPointer = gl11_vertexPointer;

	/* OpenGL 1.0 fallback support */
	if (_realDrawElements) return;
	Window_ShowDialog("Performance warning", "OpenGL 1.0 only support, expect awful performance");

	_glDrawElements    = gl10_drawElements;    _glColorPointer  = gl10_colorPointer;
	_glTexCoordPointer = gl10_texCoordPointer; _glVertexPointer = gl10_vertexPointer;

	_glBindTexture    = gl10_bindTexture;
	_glGenTextures    = gl10_genTexture;
	_glDeleteTextures = gl10_deleteTexture;
	_glTexImage2D     = gl10_texImage;
	_glTexSubImage2D  = gl10_texSubImage;
}
#else
/* No point in even trying for other systems */
static void FallbackOpenGL(void) {
	Logger_FailToStart("Only OpenGL 1.1 supported.\n\n" \
		"Compile the game with CC_BUILD_GL11, or ask on the ClassiCube forums for it");
}
#endif

static void GLBackend_Init(void) {
	static const struct DynamicLibSym coreVboFuncs[] = {
		DynamicLib_Sym2("glBindBuffer",    glBindBuffer), DynamicLib_Sym2("glDeleteBuffers", glDeleteBuffers),
		DynamicLib_Sym2("glGenBuffers",    glGenBuffers), DynamicLib_Sym2("glBufferData",    glBufferData),
		DynamicLib_Sym2("glBufferSubData", glBufferSubData)
	};
	static const struct DynamicLibSym arbVboFuncs[] = {
		DynamicLib_Sym2("glBindBufferARB",    glBindBuffer), DynamicLib_Sym2("glDeleteBuffersARB", glDeleteBuffers),
		DynamicLib_Sym2("glGenBuffersARB",    glGenBuffers), DynamicLib_Sym2("glBufferDataARB",    glBufferData),
		DynamicLib_Sym2("glBufferSubDataARB", glBufferSubData)
	};
	static const cc_string vboExt = String_FromConst("GL_ARB_vertex_buffer_object");
	cc_string extensions = String_FromReadonly((const char*)glGetString(GL_EXTENSIONS));
	const GLubyte* ver   = glGetString(GL_VERSION);

	/* Version string is always: x.y. (and whatever afterwards) */
	int major = ver[0] - '0', minor = ver[2] - '0';
#ifdef CC_BUILD_WIN
	LoadCoreFuncs();
#endif
	customMipmapsLevels = true;

	/* Supported in core since 1.5 */
	if (major > 1 || (major == 1 && minor >= 5)) {
		GLContext_GetAll(coreVboFuncs, Array_Elems(coreVboFuncs));
	} else if (String_CaselessContains(&extensions, &vboExt)) {
		GLContext_GetAll(arbVboFuncs,  Array_Elems(arbVboFuncs));
	} else {
		FallbackOpenGL();
	}
}
#endif
#endif
