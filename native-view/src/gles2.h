/*
 * The few OpenGL ES 2.0 entry points and constants RosellaSurface uses. The SDK
 * ships no GL headers; the functions are looked up at run time from the OS
 * (IGpuDevice::getExtensionAPI, else libGLESv2.so.2), so nothing is linked.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <cstdint>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLubyte;

#define GL_FALSE 0
#define GL_TRIANGLE_STRIP 0x0005
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_TEXTURE2 0x84C2
#define GL_BLEND 0x0BE2
#define GL_DEPTH_TEST 0x0B71
#define GL_SCISSOR_TEST 0x0C11

// X(return type, name, parameter list)
#define ROSELLA_GL_FUNCTIONS(X)                                                                                      \
  X(const GLubyte*, glGetString, (GLenum name))                                                                      \
  X(GLuint, glCreateShader, (GLenum type))                                                                           \
  X(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length))          \
  X(void, glCompileShader, (GLuint shader))                                                                          \
  X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint * params))                                              \
  X(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei * length, GLchar * infoLog))                  \
  X(GLuint, glCreateProgram, (void))                                                                                 \
  X(void, glAttachShader, (GLuint program, GLuint shader))                                                           \
  X(void, glBindAttribLocation, (GLuint program, GLuint index, const GLchar* name))                                  \
  X(void, glLinkProgram, (GLuint program))                                                                           \
  X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint * params))                                            \
  X(void, glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei * length, GLchar * infoLog))                \
  X(void, glUseProgram, (GLuint program))                                                                            \
  X(GLint, glGetUniformLocation, (GLuint program, const GLchar* name))                                               \
  X(void, glUniform1i, (GLint location, GLint v0))                                                                   \
  X(void, glUniform1f, (GLint location, GLfloat v0))                                                                 \
  X(void, glUniform2f, (GLint location, GLfloat v0, GLfloat v1))                                                     \
  X(void, glUniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3))                             \
  X(void, glGenTextures, (GLsizei n, GLuint * textures))                                                             \
  X(void, glBindTexture, (GLenum target, GLuint texture))                                                            \
  X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param))                                               \
  X(void, glTexImage2D,                                                                                              \
    (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format,   \
     GLenum type, const void* pixels))                                                                               \
  X(void, glTexSubImage2D,                                                                                           \
    (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format,         \
     GLenum type, const void* pixels))                                                                               \
  X(void, glPixelStorei, (GLenum pname, GLint param))                                                                \
  X(void, glActiveTexture, (GLenum texture))                                                                         \
  X(void, glViewport, (GLint x, GLint y, GLsizei width, GLsizei height))                                             \
  X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                                                \
  X(void, glClear, (GLbitfield mask))                                                                                \
  X(void, glDisable, (GLenum cap))                                                                                   \
  X(void, glVertexAttribPointer,                                                                                     \
    (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer))              \
  X(void, glEnableVertexAttribArray, (GLuint index))                                                                 \
  X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count))                                                   \
  X(void, glBindFramebuffer, (GLenum target, GLuint framebuffer))                                                    \
  X(GLenum, glCheckFramebufferStatus, (GLenum target))                                                               \
  X(void, glGenFramebuffers, (GLsizei n, GLuint * framebuffers))                                                     \
  X(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
  X(void, glFinish, (void))
