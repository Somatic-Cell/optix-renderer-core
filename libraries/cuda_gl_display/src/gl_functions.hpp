#pragma once

#include <GLFW/glfw3.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

// Windows のシステムに標準搭載されている OpenGL のヘッダは，OpenGL 1.1 の API まで 
// 何らかの方法で呼び出す必要がある
// ここで，今回，必要な関数は 11 種類
// よって，GLAD / GLEW をわざわざ呼び出さず，必要な関数だけを手動でロードする
// ImGUI には OpenGL の関数をロードするための内部実装があるが，ImGUI が使うためのものなので，ビューワでは使わない
// GLFW には glfwGetProcAddress があるので，これを使って OpenGL の関数をロードする


// ABI と呼び出しの規約

// ポインタとして使う (opaque handle)
struct __GLsync;

#if defined(_WIN32)
#define RENDERER_GL_CALL __stdcall
#else
#define RENDERER_GL_CALL
#endif

// OpenGL の型を定義する
namespace renderer::cuda_gl_display::detail
{

using GLsyncHandle = ::__GLsync*;   // OpenGL の同期オブジェクトを識別するために使用する
using GLsize = std::ptrdiff_t;      // ポインタの幅 (signed int 型, OpenGL の GLSizeptr と互換性をもたせるため)
using GLtimeout = std::uint64_t;    // glClientWaitSync() はナノ秒 (10^-9 s) 単位

// OpenGL 定数の定義
// https://registry.khronos.org/OpenGL/api/GL/glcorearb.h (最終閲覧日: 2026/11/17)
// 定数が定義されているとは限らないので，定数を直接宣言する
using Sync = ::__GLsync*;
inline constexpr GLenum rgba8               = 0x8058;   // 各チャンネル 8-bit のテクスチャ方式
inline constexpr GLenum clamp_to_edge       = 0x812F;   // テクスチャ境界でのサンプリング方法 (clamp)
inline constexpr GLenum sync_complete       = 0x9117;   // GPU コマンドの完了を待つ同期条件
inline constexpr GLenum already_signaled    = 0x911A;   // 同期条件が既に成立しているかどうか
inline constexpr GLenum timeout_expired     = 0x911B;   // 指定時間内に同期条件が成立しなかった
inline constexpr GLenum condition_satisfied = 0x911C;   // 待機中に同期条件が成立した

struct GLFunctions
{

    template<class Function> static Function load(const char* name)
    {
        // OpenGL 関数のアドレスを取得
        const auto address = glfwGetProcAddress(name);
        
        if (!address)
        {
            throw std::runtime_error(std::string("Missing GL entry point: ") + name);
        }
        return reinterpret_cast<Function>(address);
    }
    using CreateTextures    = void(RENDERER_GL_CALL*)(GLenum, GLsizei, GLuint*);
    using Storage           = void(RENDERER_GL_CALL*)(GLuint, GLsizei, GLenum, GLsizei, GLsizei);
    using Parameter         = void(RENDERER_GL_CALL*)(GLuint, GLenum, GLint);
    using Fence             = GLsyncHandle(RENDERER_GL_CALL*)(GLenum, GLbitfield);
    using Poll              = GLenum(RENDERER_GL_CALL*)(GLsyncHandle, GLbitfield, std::uint64_t);
    using Delete            = void(RENDERER_GL_CALL*)(GLsyncHandle);

    CreateTextures create_textures  = load<CreateTextures>("glCreateTextures");
    Storage texture_storage         = load<Storage>("glTextureStorage2D");
    Parameter texture_parameter     = load<Parameter>("glTextureParameteri");
    Fence fence                     = load<Fence>("glFenceSync");
    Poll poll                       = load<Poll>("glClientWaitSync");
    Delete delete_sync              = load<Delete>("glDeleteSync");
};

inline void check_gl(const char* operation)
{
    const auto error = glGetError();
    if (error != GL_NO_ERROR)
    {
        throw std::runtime_error(std::string(operation) + ": GL error " + std::to_string(error));
    }
}

}  // namespace renderer::cuda_gl_display::detail

#undef RENDERER_GL_CALL