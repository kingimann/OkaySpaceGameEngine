// okay-ms own <GL/gl.h>: the OpenGL 1.1 types, enumerants and core functions the engine's GL renderer uses, declared in
// our own words from the OpenGL 1.1 specification (Khronos, "The OpenGL Graphics System: A Specification", version
// 1.1: the type table of section 2.4 and the state and enumerant tables) and Microsoft's public documentation of
// opengl32.dll, which exports the 1.1 functions on Windows. Newer functions are not here: the engine loads them itself
// (wglGetProcAddress) and defines their enumerants. No text from the Windows SDK, MinGW, Mesa or Khronos headers.
// Add a name here when an engine file needs it. The functions live in opengl32.dll.
#pragma once
#include "../windows.h"

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;

// Boolean values.
#define GL_FALSE 0
#define GL_TRUE 1

// Primitives (Begin modes).
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006

// Clear masks.
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT 0x00004000

// Comparison functions (depth and alpha tests).
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207

// Blend factors.
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307

// Buffers and faces.
#define GL_NONE 0
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408

// Errors.
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_OUT_OF_MEMORY 0x0505

// Capabilities and state.
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_VIEWPORT 0x0BA2
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_TEXTURE_2D 0x0DE1

// Data types.
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406

// Pixel formats.
#define GL_DEPTH_COMPONENT 0x1902
#define GL_RED 0x1903
#define GL_ALPHA 0x1906
#define GL_RGB 0x1907
#define GL_RGBA 0x1908

// Strings (glGetString).
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03

// Texture filters and wrap modes.
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901

#define OKAY_GL_IMPORT extern "C" __declspec(dllimport)

OKAY_GL_IMPORT void WINAPI glBindTexture(GLenum target, GLuint texture);
OKAY_GL_IMPORT void WINAPI glBlendFunc(GLenum source, GLenum destination);
OKAY_GL_IMPORT void WINAPI glClear(GLbitfield mask);
OKAY_GL_IMPORT void WINAPI glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
OKAY_GL_IMPORT void WINAPI glClearDepth(GLclampd depth);
OKAY_GL_IMPORT void WINAPI glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
OKAY_GL_IMPORT void WINAPI glCullFace(GLenum face);
OKAY_GL_IMPORT void WINAPI glDeleteTextures(GLsizei count, const GLuint* textures);
OKAY_GL_IMPORT void WINAPI glDepthFunc(GLenum func);
OKAY_GL_IMPORT void WINAPI glDepthMask(GLboolean flag);
OKAY_GL_IMPORT void WINAPI glDisable(GLenum capability);
OKAY_GL_IMPORT void WINAPI glDrawArrays(GLenum mode, GLint first, GLsizei count);
OKAY_GL_IMPORT void WINAPI glDrawBuffer(GLenum buffer);
OKAY_GL_IMPORT void WINAPI glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices);
OKAY_GL_IMPORT void WINAPI glEnable(GLenum capability);
OKAY_GL_IMPORT void WINAPI glFinish(void);
OKAY_GL_IMPORT void WINAPI glFlush(void);
OKAY_GL_IMPORT void WINAPI glGenTextures(GLsizei count, GLuint* textures);
OKAY_GL_IMPORT GLenum WINAPI glGetError(void);
OKAY_GL_IMPORT void WINAPI glGetFloatv(GLenum name, GLfloat* values);
OKAY_GL_IMPORT void WINAPI glGetIntegerv(GLenum name, GLint* values);
OKAY_GL_IMPORT const GLubyte* WINAPI glGetString(GLenum name);
OKAY_GL_IMPORT void WINAPI glPixelStorei(GLenum name, GLint value);
OKAY_GL_IMPORT void WINAPI glReadBuffer(GLenum buffer);
OKAY_GL_IMPORT void WINAPI glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                                        GLvoid* pixels);
OKAY_GL_IMPORT void WINAPI glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
OKAY_GL_IMPORT void WINAPI glTexImage2D(GLenum target, GLint level, GLint internal_format, GLsizei width, GLsizei height,
                                        GLint border, GLenum format, GLenum type, const GLvoid* pixels);
OKAY_GL_IMPORT void WINAPI glTexParameteri(GLenum target, GLenum name, GLint value);
OKAY_GL_IMPORT void WINAPI glTexParameterf(GLenum target, GLenum name, GLfloat value);
OKAY_GL_IMPORT void WINAPI glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height,
                                           GLenum format, GLenum type, const GLvoid* pixels);
OKAY_GL_IMPORT void WINAPI glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
