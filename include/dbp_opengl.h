/*
 *  Copyright (C) 2024-2025 Bernhard Schelling
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#define MYGL_FRAGMENT_SHADER                0x8B30
#define MYGL_VERTEX_SHADER                  0x8B31
#define MYGL_COLOR_BUFFER_BIT               0x00004000
#define MYGL_DEPTH_BUFFER_BIT               0x00000100
#define MYGL_STENCIL_BUFFER_BIT             0x00000400
#define MYGL_POINTS                         0x0000
#define MYGL_TRIANGLES                      0x0004
#define MYGL_TRIANGLE_STRIP                 0x0005
#define MYGL_COMPILE_STATUS                 0x8B81
#define MYGL_INFO_LOG_LENGTH                0x8B84
#define MYGL_LINK_STATUS                    0x8B82
#define MYGL_FRAMEBUFFER                    0x8D40
#define MYGL_FLOAT                          0x1406
#define MYGL_ARRAY_BUFFER                   0x8892
#define MYGL_STATIC_DRAW                    0x88E4
#define MYGL_DYNAMIC_DRAW                   0x88E8
#define MYGL_FALSE                          0
#define MYGL_TRUE                           1
#define MYGL_TEXTURE_2D                     0x0DE1
#define MYGL_TEXTURE0                       0x84C0
#define MYGL_TEXTURE1                       0x84C1
#define MYGL_TEXTURE_MIN_FILTER             0x2801
#define MYGL_TEXTURE_MAG_FILTER             0x2800
#define MYGL_TEXTURE_WRAP_S                 0x2802
#define MYGL_TEXTURE_WRAP_T                 0x2803
#define MYGL_NEAREST                        0x2600
#define MYGL_LINEAR                         0x2601
#define MYGL_LINEAR_MIPMAP_LINEAR           0x2703
#define MYGL_REPEAT                         0x2901
#define MYGL_CLAMP_TO_EDGE                  0x812F
#define MYGL_UNSIGNED_BYTE                  0x1401
#define MYGL_UNSIGNED_INT                   0x1405
#define MYGL_COLOR_ATTACHMENT0              0x8CE0
#define MYGL_DEPTH_ATTACHMENT               0x8D00
#define MYGL_STENCIL_ATTACHMENT             0x8D20
#define MYGL_RGBA                           0x1908
#define MYGL_DEPTH_COMPONENT                0x1902
#define MYGL_DEPTH24_STENCIL8               0x88F0
#define MYGL_DEPTH_STENCIL                  0x84F9
#define MYGL_UNSIGNED_INT_24_8              0x84FA
#define MYGL_DEPTH_TEST                     0x0B71
#define MYGL_SCISSOR_TEST                   0x0C11
#define MYGL_KEEP                           0x1E00
#define MYGL_PIXEL_PACK_BUFFER              0x88EB
#define MYGL_READ_FRAMEBUFFER               0x8CA8
#define MYGL_STREAM_READ                    0x88E1
#define MYGL_READ_ONLY                      0x88B8
#define MYGL_MAP_READ_BIT                   0x0001
#define MYGL_NEVER                          0x0200
#define MYGL_LESS                           0x0201
#define MYGL_EQUAL                          0x0202
#define MYGL_LEQUAL                         0x0203
#define MYGL_GREATER                        0x0204
#define MYGL_NOTEQUAL                       0x0205
#define MYGL_GEQUAL                         0x0206
#define MYGL_ALWAYS                         0x0207
#define MYGL_ZERO                           0
#define MYGL_ONE                            1
#define MYGL_SRC_COLOR                      0x0300
#define MYGL_ONE_MINUS_SRC_COLOR            0x0301
#define MYGL_SRC_ALPHA                      0x0302
#define MYGL_ONE_MINUS_SRC_ALPHA            0x0303
#define MYGL_DST_ALPHA                      0x0304
#define MYGL_ONE_MINUS_DST_ALPHA            0x0305
#define MYGL_DST_COLOR                      0x0306
#define MYGL_ONE_MINUS_DST_COLOR            0x0307
#define MYGL_SRC_ALPHA_SATURATE             0x0308
#define MYGL_BLEND                          0x0BE2
#define MYGL_STENCIL_TEST                   0x0B90
#define MYGL_PROGRAM_POINT_SIZE             0x8642

#define MYGL_FOR_EACH_PROC1(M) \
	M(1, int,           GetError,                (void)) \
	M(1, void,          Enable,                  (int cap)) \
	M(1, void,          Disable,                 (int cap)) \
	M(1, unsigned,      CreateProgram,           (void)) \
	M(1, unsigned,      CreateShader,            (unsigned type)) \
	M(1, void,          ShaderSource,            (unsigned shader, int count, const char** string, const int* length)) \
	M(1, void,          CompileShader,           (unsigned shader)) \
	M(1, void,          GetShaderiv,             (unsigned shader, unsigned pname, int *params)) \
	M(1, void,          AttachShader,            (unsigned program, unsigned shader)) \
	M(1, void,          BindAttribLocation,      (unsigned program, unsigned index, const char *name)) \
	M(1, int,           GetUniformLocation,      (unsigned program, const char* name)) \
	M(1, void,          LinkProgram,             (unsigned program)) \
	M(1, void,          GetProgramiv,            (unsigned program, unsigned pname, int *params)) \
	M(1, void,          GetShaderInfoLog,        (unsigned shader, int bufSize, int *length, char *infoLog)) \
	M(1, void,          GetProgramInfoLog,       (unsigned program, int bufSize, int *length, char *infoLog)) \
	M(1, void,          DetachShader,            (unsigned program, unsigned shader)) \
	M(1, void,          DeleteShader,            (unsigned shader)) \
	M(1, void,          DeleteProgram,           (unsigned program)) \
	M(1, void,          GenVertexArrays,         (int n, unsigned *arrays)) \
	M(1, void,          BindVertexArray,         (unsigned arr)) \
	M(1, void,          DeleteVertexArrays,      (int n, const unsigned* arrays)) \
	M(1, void,          GenTextures,             (int n, unsigned *textures)) \
	M(1, void,          BindTexture,             (int target, unsigned texture)) \
	M(1, void,          DeleteTextures,          (int n, const unsigned* textures)) \
	M(1, void,          ActiveTexture,           (int texture)) \
	M(1, void,          TexParameteri,           (int target, int pname, int param)) \
	M(1, void,          TexImage2D,              (int target, int level, int internalformat, int width, int height, int border, int format, int type, const void *pixels)) \
	M(1, void,          TexSubImage2D,           (int target, int level, int xoffset, int yoffset, int width, int height, int format, int type, const void *pixels)) \
	M(1, void,          GenBuffers,              (int n, unsigned *buffers)) \
	M(1, void,          BindBuffer,              (int target, unsigned buffer)) \
	M(1, void,          DeleteBuffers,           (int n, const unsigned* buffers)) \
	M(1, void,          BufferData,              (int target, ptrdiff_t size, const void *data, int usage)) \
	M(1, void,          GenFramebuffers,         (int n, unsigned *framebuffers)) \
	M(1, void,          BindFramebuffer,         (unsigned target, unsigned framebuffer)) \
	M(1, void,          DeleteFramebuffers,      (int n, const unsigned* framebuffers)) \
	M(1, void,          FramebufferTexture2D,    (int target, int attachment, int textarget, unsigned texture, int level)) \
	M(1, void,          ClearColor,              (float red, float green, float blue, float alpha)) \
	M(1, void,          Viewport,                (int x, int y, int width, int height)) \
	M(1, void,          Clear,                   (unsigned mask)) \
	M(1, void,          StencilFunc,             (int func, int ref, unsigned mask)) \
	M(1, void,          StencilOp,               (int fail, int zfail, int zpass)) \
	M(1, void,          DepthFunc,               (int func)) \
	M(1, void,          DepthMask,               (unsigned char flag)) \
	M(1, void,          ColorMask,               (unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha)) \
	M(1, void,          BlendFuncSeparate,       (int sfactorRGB, int dfactorRGB, int sfactorAlpha, int dfactorAlpha)) \
	M(1, void,          Scissor,                 (int x, int y, int width, int height)) \
	M(1, void,          UseProgram,              (unsigned program)) \
	M(1, void,          Uniform4f,               (int location, float v0, float v1, float v2, float v3)) \
	M(1, void,          Uniform3f,               (int location, float v0, float v1, float v2)) \
	M(1, void,          Uniform1i,               (int location, int v0)) \
	M(1, void,          DrawArrays,              (unsigned mode, int first, int count)) \
	M(1, void,          VertexAttribPointer,     (unsigned index, int size, int type, unsigned char normalized, int stride, const void *pointer)) \
	M(1, void,          EnableVertexAttribArray, (unsigned index)) \
	M(1, void,          ReadPixels,              (int x, int y, int width, int height, int format, int type, void* pixels)) \
	//------------------------------------------------------------------------------------

#define MYGL_FOR_EACH_PROC2(M) \
	M(0, void,          ClearDepth,              (double depth)) /* not available on OpenGL ES */ \
	M(0, void,          ClearDepthf,             (float depth)) /* only on OpenGL ES */ \
	M(0, void,          DepthRange,              (double nearVal, double farVal)) /* not available on OpenGL ES */ \
	M(0, void,          DepthRangef,             (float nearVal, float farVal)) /* only on OpenGL ES */ \
	M(0, void,          DrawBuffers,             (int n, const int *bufs)) /* optional PBO support */ \
	M(0, void*,         MapBufferRange,          (int target, void* offset, ptrdiff_t length, unsigned bufaccess)) /* optional PBO support */ \
	M(0, unsigned char, UnmapBuffer,             (int target)) /* optional PBO support */ \
	M(0, void,          ReadBuffer,              (int src)) /* optional PBO support */ \
	M(0, void,          GenerateMipmap,          (int target)) /* optional auto mipmapping */ \
	//------------------------------------------------------------------------------------

// Currently unused functions:
	//M(1, void,          Uniform1f,               (int location, float v0))
	//M(1, void,          UniformMatrix4fv,        (int location, int count, unsigned char transpose, const float* value))
	//M(0, void,          PolygonOffset,           (float factor, float units)) /* avoid Z fighting */

#define MYGL_FOR_EACH_PROC(M) \
	MYGL_FOR_EACH_PROC1(M) \
	MYGL_FOR_EACH_PROC2(M) \
	//------------------------------------------------------------------------------------

#if (defined(__WIN32__) || defined(WIN32) || defined(_WIN32) || defined(_MSC_VER)) && !defined(_WIN64) && !defined(__MINGW64__) && !defined(_M_X64) && !defined(_LP64) && !defined(__LP64__) && !defined(__ia64__) && !defined(__ia64__) && !defined(__x86_64__) && !defined(__x86_64__) && !defined(__LLP64__) && !defined(__aarch64__) && !defined(_M_ARM64) && !defined(__arm__)
#define MYGLCALL __stdcall
#else 
#define MYGLCALL
#endif

#define MYGL_MAKEFUNCEXT(REQUIRE, RET, NAME, ARGS) extern RET (MYGLCALL* mygl ## NAME)ARGS;
MYGL_FOR_EACH_PROC(MYGL_MAKEFUNCEXT)

#define MYGL_MAKEFUNCPTR(REQUIRE, RET, NAME, ARGS) RET (MYGLCALL* mygl ## NAME)ARGS;
#define MYGL_MAKEPROCARRENTRY(REQUIRE, RET, NAME, ARGS) { (retro_proc_address_t*)&mygl ## NAME , "gl" #NAME, REQUIRE },

unsigned DBP_Build_GL_Program(int vertex_shader_srcs_count, const char** vertex_shader_srcs, int fragment_shader_srcs_count, const char** fragment_shader_srcs, int bind_attribs_count, const char** bind_attribs);

extern Bit8u voodoo_ogl_scale;
bool voodoo_is_active();
bool voodoo_ogl_is_showing();
bool voodoo_ogl_have_new_image();
bool voodoo_ogl_display();
bool voodoo_ogl_mainthread();
void voodoo_ogl_cleanup();
void voodoo_ogl_resetcontext();
void voodoo_ogl_initfailed();
