// Minimal GL 4.3 declarations + loader. We avoid glext.h/glcorearb.h to keep
// the build dependency-free; only what the FG pipeline uses is declared.
#pragma once

#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#else
// stop mesa's gl.h from dragging in glext.h; we declare what we need ourselves
#define GL_GLEXT_LEGACY
#include <GL/gl.h>
#ifndef APIENTRY
#define APIENTRY
#endif
#endif

#include <cstddef>
#include <cstdint>

using GLchar = char;
using GLsizeiptr = ptrdiff_t;
using GLintptr = ptrdiff_t;
using GLint64 = int64_t;
using GLuint64 = uint64_t;
using GLsync = struct __GLsync*;

// --- enums not in GL/gl.h (GL 1.1) ---
#define GL_TEXTURE0 0x84C0
#define GL_R16F 0x822D
#define GL_RG16F 0x8230
#define GL_RGBA8_ 0x8058 // GL_RGBA8 exists in gl.h; alias kept for clarity
#define GL_CLAMP_TO_EDGE 0x812F

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPUTE_SHADER 0x91B9
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82

#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_FRAMEBUFFER_BINDING 0x8CA6 // == GL_DRAW_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA

#define GL_READ_ONLY 0x88B8
#define GL_WRITE_ONLY 0x88B9
#define GL_READ_WRITE 0x88BA

#define GL_SHADER_STORAGE_BUFFER 0x90D2
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#define GL_TEXTURE_FETCH_BARRIER_BIT 0x00000008
#define GL_FRAMEBUFFER_BARRIER_BIT 0x00000400
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#define GL_ALL_BARRIER_BITS 0xFFFFFFFF

#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_READ 0x88E9

#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#define GL_TIMEOUT_IGNORED 0xFFFFFFFFFFFFFFFFull
#define GL_ALREADY_SIGNALED 0x911A
#define GL_TIMEOUT_EXPIRED 0x911B
#define GL_CONDITION_SATISFIED 0x911C
#define GL_WAIT_FAILED_ 0x911D

#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C

// X-macro list of dynamically loaded functions.
#define GL_FUNC_LIST(X) \
    X(GLuint, glCreateShader, (GLenum type)) \
    X(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths)) \
    X(void, glCompileShader, (GLuint shader)) \
    X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint* params)) \
    X(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void, glAttachShader, (GLuint program, GLuint shader)) \
    X(void, glLinkProgram, (GLuint program)) \
    X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint* params)) \
    X(void, glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog)) \
    X(void, glUseProgram, (GLuint program)) \
    X(void, glDeleteShader, (GLuint shader)) \
    X(void, glDeleteProgram, (GLuint program)) \
    X(GLint, glGetUniformLocation, (GLuint program, const GLchar* name)) \
    X(void, glUniform1i, (GLint location, GLint v0)) \
    X(void, glUniform2i, (GLint location, GLint v0, GLint v1)) \
    X(void, glUniform1f, (GLint location, GLfloat v0)) \
    X(void, glUniform2f, (GLint location, GLfloat v0, GLfloat v1)) \
    X(void, glActiveTexture, (GLenum texture)) \
    X(void, glTexStorage2D, (GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)) \
    X(void, glCopyImageSubData, (GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei width, GLsizei height, GLsizei depth)) \
    X(void, glGenFramebuffers, (GLsizei n, GLuint* framebuffers)) \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint* framebuffers)) \
    X(void, glBindFramebuffer, (GLenum target, GLuint framebuffer)) \
    X(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum target)) \
    X(void, glBlitFramebuffer, (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter)) \
    X(void, glDispatchCompute, (GLuint x, GLuint y, GLuint z)) \
    X(void, glMemoryBarrier, (GLbitfield barriers)) \
    X(void, glBindImageTexture, (GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLenum format)) \
    X(GLsync, glFenceSync, (GLenum condition, GLbitfield flags)) \
    X(GLenum, glClientWaitSync, (GLsync sync, GLbitfield flags, GLuint64 timeout)) \
    X(void, glWaitSync, (GLsync sync, GLbitfield flags, GLuint64 timeout)) \
    X(void, glDeleteSync, (GLsync sync)) \
    X(void, glGenBuffers, (GLsizei n, GLuint* buffers)) \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint* buffers)) \
    X(void, glBindBuffer, (GLenum target, GLuint buffer)) \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage)) \
    X(void, glBindBufferBase, (GLenum target, GLuint index, GLuint buffer)) \
    X(void, glGenVertexArrays, (GLsizei n, GLuint* arrays)) \
    X(void, glDeleteVertexArrays, (GLsizei n, const GLuint* arrays)) \
    X(void, glBindVertexArray, (GLuint array))

#define GL_DECLARE(ret, name, args) extern ret(APIENTRY* name##_)args;
GL_FUNC_LIST(GL_DECLARE)
#undef GL_DECLARE

// Loads all pointers via the platform's real (unhooked) GetProcAddress
// (a GL context must be current). Returns false if any function is missing.
bool GLLoad(void* (*getProc)(const char*));
