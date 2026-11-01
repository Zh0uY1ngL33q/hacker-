//
// Created by vastrakai on 6/29/2024.
//

#include "bgfx_context.hpp"

#include <d3d12.h>
#include <libhat/Access.hpp>
#include <SDK/OffsetProvider.hpp>
#include <SDK/SigManager.hpp>

// Neither offset resolves on 1.21.120. At offset 0 hat::member_at() returns the
// object's vtable pointer, so a failed scan looks exactly like a valid object to
// the caller. Refuse instead of dressing the failure up.
ID3D12CommandQueue* bgfx_d3d12_RendererContextD3D12::getCommandQueue()
{
    const int offset = OffsetProvider::bgfx_d3d12_RendererContextD3D12_m_commandQueue;
    if (offset <= 0)
        return nullptr;
    return hat::member_at<ID3D12CommandQueue*>(this, offset);
}

bgfx_rendercontexti* bgfx_context::getRenderContext()
{
    const int offset = OffsetProvider::bgfx_context_m_renderCtx;
    if (offset <= 0)
        return nullptr;
    return hat::member_at<bgfx_rendercontexti*>(this, offset);
}

bgfx_context* bgfx_context::get()
{
    // Dereferencing the signature's target directly means an unresolved signature
    // is a null-page read, not a failed lookup. The 1.21.120 match also lands at
    // an odd address (0x665A211), which cannot be a pointer-aligned global, so a
    // "successful" scan is not proof either -- both cases have to be survivable.
    if (!SigManager::ClientInstance_mBgfx)
        return nullptr;
    return *reinterpret_cast<bgfx_context**>(SigManager::ClientInstance_mBgfx);
}
