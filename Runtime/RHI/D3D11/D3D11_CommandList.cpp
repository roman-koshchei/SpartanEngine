/*
Copyright(c) 2016-2021 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ===============================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "../RHI_Pipeline.h"
#include "../RHI_Device.h"
#include "../RHI_Sampler.h"
#include "../RHI_Texture.h"
#include "../RHI_Shader.h"
#include "../RHI_ConstantBuffer.h"
#include "../RHI_VertexBuffer.h"
#include "../RHI_IndexBuffer.h"
#include "../RHI_StructuredBuffer.h"
#include "../RHI_BlendState.h"
#include "../RHI_DepthStencilState.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_InputLayout.h"
#include "../RHI_SwapChain.h"
#include "../RHI_PipelineState.h"
#include "../RHI_DescriptorSetLayoutCache.h"
#include "../../Profiling/Profiler.h"
#include "../../Rendering/Renderer.h"
//==========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    bool RHI_CommandList::m_memory_query_support = true;

    RHI_CommandList::RHI_CommandList(Context* context)
    {
        m_renderer                      = context->GetSubsystem<Renderer>();
        m_profiler                      = context->GetSubsystem<Profiler>();
        m_rhi_device                    = m_renderer->GetRhiDevice().get();
        m_pipeline_cache                = m_renderer->GetPipelineCache();
        m_descriptor_set_layout_cache   = m_renderer->GetDescriptorLayoutSetCache();
        m_timestamps.fill(0);
    }

    RHI_CommandList::~RHI_CommandList() = default;

    bool RHI_CommandList::Begin()
    {
        m_state = RHI_CommandListState::Recording;
        return true;
    }

    bool RHI_CommandList::End()
    {
        m_state = RHI_CommandListState::Ended;
        return true;
    }

    bool RHI_CommandList::Submit(RHI_Semaphore* wait_semaphore)
    {
        m_state = RHI_CommandListState::Submitted;
        return true;
    }

    bool RHI_CommandList::Reset()
    {
        m_state = RHI_CommandListState::Idle;
        return true;
    }

    bool RHI_CommandList::BeginRenderPass(RHI_PipelineState& pipeline_state)
    {
        if (!pipeline_state.IsValid())
        {
            LOG_ERROR("Invalid pipeline state");
            return false;
        }

        // Keep a local pointer for convenience 
        m_pipeline_state = &pipeline_state;

        // Start marker and profiler (if enabled)
        Timeblock_Start(m_pipeline_state);

        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Input layout
        {
            // New state
            ID3D11InputLayout* input_layout = static_cast<ID3D11InputLayout*>(pipeline_state.shader_vertex ? pipeline_state.shader_vertex->GetInputLayout()->GetResource() : nullptr);

            // Current state
            ID3D11InputLayout* input_layout_set = nullptr;
            device_context->IAGetInputLayout(&input_layout_set);

            // Set if dirty
            if (input_layout_set != input_layout)
            {
                device_context->IASetInputLayout(input_layout);
            }
        }

        // Vertex shader
        {
            // New state
            ID3D11VertexShader* shader = static_cast<ID3D11VertexShader*>(pipeline_state.shader_vertex ? pipeline_state.shader_vertex->GetResource() : nullptr);

            // Current state
            ID3D11VertexShader* set_shader = nullptr; UINT instance_count = 256; ID3D11ClassInstance* instances[256];
            device_context->VSGetShader(&set_shader, instances, &instance_count);

            // Set if dirty
            if (set_shader != shader)
            {
                device_context->VSSetShader(shader, nullptr, 0);

                m_profiler->m_rhi_bindings_shader_vertex++;
            }
        }

        // Pixel shader
        {
            // New state
            ID3D11PixelShader* shader = static_cast<ID3D11PixelShader*>(pipeline_state.shader_pixel ? pipeline_state.shader_pixel->GetResource() : nullptr);

            // Current state
            ID3D11PixelShader* set_shader = nullptr; UINT instance_count = 256; ID3D11ClassInstance* instances[256];
            device_context->PSGetShader(&set_shader, instances, &instance_count);

            // Set if dirty
            if (set_shader != shader)
            {
                device_context->PSSetShader(shader, nullptr, 0);

                m_profiler->m_rhi_bindings_shader_pixel++;
            }
        }

        // Compute shader
        {
            // New state
            ID3D11ComputeShader* shader = static_cast<ID3D11ComputeShader*>(pipeline_state.shader_compute ? pipeline_state.shader_compute->GetResource() : nullptr);

            // Current state
            ID3D11ComputeShader* set_shader = nullptr; UINT instance_count = 256; ID3D11ClassInstance* instances[256];
            device_context->CSGetShader(&set_shader, instances, &instance_count);

            // Set if dirty
            if (set_shader != shader)
            {
                device_context->CSSetShader(shader, nullptr, 0);

                m_profiler->m_rhi_bindings_shader_compute++;
            }
        }

        // Blend state
        {
            // New state
            ID3D11BlendState* blend_state_set       = nullptr;
            std::array<FLOAT, 4> blend_factor_set   = { 0.0f };
            UINT mask_set                           = 0;
            device_context->OMGetBlendState(&blend_state_set, blend_factor_set.data(), &mask_set);

            // Current state
            ID3D11BlendState* blend_state       = static_cast<ID3D11BlendState*>(pipeline_state.blend_state ? pipeline_state.blend_state->GetResource() : nullptr);
            const float blendFactor             = pipeline_state.blend_state ? pipeline_state.blend_state->GetBlendFactor() : 0.0f;
            std::array<FLOAT, 4> blend_factor   = { blendFactor, blendFactor, blendFactor, blendFactor };
            const UINT mask                           = 0;

            // Set if dirty
            if (blend_state_set != blend_state || blend_factor_set != blend_factor || mask_set != mask)
            {
                device_context->OMSetBlendState(blend_state, blend_factor.data(), 0xffffffff);
            }
        }

        // Depth stencil state
        {
            // New state
            ID3D11DepthStencilState* depth_stencil_state = static_cast<ID3D11DepthStencilState*>(pipeline_state.depth_stencil_state ? pipeline_state.depth_stencil_state->GetResource() : nullptr);

            // Current state
            ID3D11DepthStencilState* depth_stencil_state_set = nullptr;
            UINT stencil_ref = 0;
            device_context->OMGetDepthStencilState(&depth_stencil_state_set, &stencil_ref);

            // Set if dirty
            if (depth_stencil_state_set != depth_stencil_state)
            {
                device_context->OMSetDepthStencilState(depth_stencil_state, 1);
            }
        }

        // Rasterizer state
        {
            // New state
            ID3D11RasterizerState* rasterizer_state = static_cast<ID3D11RasterizerState*>(pipeline_state.rasterizer_state ? pipeline_state.rasterizer_state->GetResource() : nullptr);

            // Current state
            ID3D11RasterizerState* rasterizer_state_set = nullptr;

            // Set if dirty
            device_context->RSGetState(&rasterizer_state_set);
            if (rasterizer_state_set != rasterizer_state)
            {
                device_context->RSSetState(rasterizer_state);
            }
        }

        // Primitive topology
        if (pipeline_state.primitive_topology != RHI_PrimitiveTopology_Mode::RHI_PrimitiveTopology_Unknown)
        {
            // New state
            const D3D11_PRIMITIVE_TOPOLOGY topology = d3d11_primitive_topology[static_cast<uint32_t>(pipeline_state.primitive_topology)];

            // Current state
            D3D11_PRIMITIVE_TOPOLOGY topology_set;
            device_context->IAGetPrimitiveTopology(&topology_set);

            // Set if dirty
            if (topology_set != topology)
            {
                device_context->IASetPrimitiveTopology(d3d11_primitive_topology[static_cast<uint32_t>(pipeline_state.primitive_topology)]);
            }
        }

        // Render target(s)
        {
            // Detect depth stencil targets
            ID3D11DepthStencilView* depth_stencil = nullptr;
            if (pipeline_state.render_target_depth_texture)
            {
                if (pipeline_state.render_target_depth_texture_read_only)
                {
                    depth_stencil = static_cast<ID3D11DepthStencilView*>(pipeline_state.render_target_depth_texture->Get_Resource_View_DepthStencilReadOnly(pipeline_state.render_target_depth_stencil_texture_array_index));
                }
                else
                {
                    depth_stencil = static_cast<ID3D11DepthStencilView*>(pipeline_state.render_target_depth_texture->Get_Resource_View_DepthStencil(pipeline_state.render_target_depth_stencil_texture_array_index));
                }
            }

            // Detect color targets
            std::array<ID3D11RenderTargetView*, rhi_max_render_target_count> render_targets = { nullptr };
            {
                // Swapchain
                if (pipeline_state.render_target_swapchain)
                {
                    render_targets[0] = { static_cast<ID3D11RenderTargetView*>(pipeline_state.render_target_swapchain->Get_Resource_View_RenderTarget()) };
                }
                // Textures
                else
                {
                    for (uint8_t i = 0; i < rhi_max_render_target_count; i++)
                    {
                        if (pipeline_state.render_target_color_textures[i])
                        {
                            ID3D11RenderTargetView* rt = static_cast<ID3D11RenderTargetView*>(pipeline_state.render_target_color_textures[i]->Get_Resource_View_RenderTarget(pipeline_state.render_target_color_texture_array_index));
                            render_targets[i] = rt;
                        }
                    }
                }
            }

            // Set render targets
            {
                // Current state
                std::array<ID3D11RenderTargetView*, rhi_max_render_target_count> set_render_target_views = { nullptr };
                ID3D11DepthStencilView* set_depth_stencil_view = nullptr;
                device_context->OMGetRenderTargets(rhi_max_render_target_count, set_render_target_views.data(), &set_depth_stencil_view);

                // Set if dirty
                if (render_targets != set_render_target_views || depth_stencil != set_depth_stencil_view)
                {
                    UINT render_target_count = 0;
                    for (ID3D11RenderTargetView* rt : render_targets)
                    {
                        if (rt)
                        {
                            render_target_count++;
                        }
                    }

                    device_context->OMSetRenderTargets
                    (
                        render_target_count,
                        reinterpret_cast<ID3D11RenderTargetView* const*>(render_targets.data()),
                        depth_stencil
                    );

                    m_profiler->m_rhi_bindings_render_target++;
                }
            }
        }

        // Viewport
        if (pipeline_state.viewport.IsDefined())
        {
            SetViewport(pipeline_state.viewport);
        }

        // Clear render target(s)
        ClearPipelineStateRenderTargets(pipeline_state);

        m_renderer->SetGlobalShaderResources(this);
        m_profiler->m_rhi_bindings_pipeline++;

        return true;
    }

    bool RHI_CommandList::EndRenderPass()
    {
        // End marker and profiler (if enabled)
        Timeblock_End(m_pipeline_state);
        return true;
    }

    void RHI_CommandList::ClearPipelineStateRenderTargets(RHI_PipelineState& pipeline_state)
    {
        // Color
        for (uint8_t i = 0; i < rhi_max_render_target_count; i++)
        {
            if (pipeline_state.clear_color[i] != rhi_color_load && pipeline_state.clear_color[i] != rhi_color_dont_care)
            {
                if (pipeline_state.render_target_swapchain)
                {
                    m_rhi_device->GetContextRhi()->device_context->ClearRenderTargetView
                    (
                        static_cast<ID3D11RenderTargetView*>(const_cast<void*>(pipeline_state.render_target_swapchain->Get_Resource_View_RenderTarget())),
                        pipeline_state.clear_color[i].Data()
                    );
                }
                else if (pipeline_state.render_target_color_textures[i])
                {
                    m_rhi_device->GetContextRhi()->device_context->ClearRenderTargetView
                    (
                        static_cast<ID3D11RenderTargetView*>(const_cast<void*>(pipeline_state.render_target_color_textures[i]->Get_Resource_View_RenderTarget(pipeline_state.render_target_color_texture_array_index))),
                        pipeline_state.clear_color[i].Data()
                    );
                }
            }
        }

        // Depth-stencil
        if (pipeline_state.render_target_depth_texture)
        {
            UINT clear_flags = 0;
            clear_flags |= (pipeline_state.clear_depth      != rhi_depth_load     && pipeline_state.clear_depth   != rhi_depth_dont_care)   ? D3D11_CLEAR_DEPTH     : 0;
            clear_flags |= (pipeline_state.clear_stencil    != rhi_stencil_load   && pipeline_state.clear_stencil != rhi_stencil_dont_care) ? D3D11_CLEAR_STENCIL   : 0;
            if (clear_flags != 0)
            {
                m_rhi_device->GetContextRhi()->device_context->ClearDepthStencilView
                (
                    static_cast<ID3D11DepthStencilView*>(pipeline_state.render_target_depth_texture->Get_Resource_View_DepthStencil(pipeline_state.render_target_depth_stencil_texture_array_index)),
                    clear_flags,
                    static_cast<FLOAT>(pipeline_state.clear_depth),
                    static_cast<UINT8>(pipeline_state.clear_stencil)
                );
            }
        }
    }

    void RHI_CommandList::ClearRenderTarget(RHI_Texture* texture,
        const uint32_t color_index          /*= 0*/,
        const uint32_t depth_stencil_index  /*= 0*/,
        const bool storage                  /*= false*/,
        const Math::Vector4& clear_color    /*= rhi_color_load*/,
        const float clear_depth             /*= rhi_depth_load*/,
        const uint32_t clear_stencil        /*= rhi_stencil_load*/
    )
    {
        if (storage)
        {
            if (clear_color == rhi_color_load || clear_color == rhi_color_dont_care)
                return;

            // TODO: Assuming the UAV is a float, which almost always is, but I should fix it anyway
            m_rhi_device->GetContextRhi()->device_context->ClearUnorderedAccessViewFloat(static_cast<ID3D11UnorderedAccessView*>(texture->Get_Resource_View_Uav()), clear_color.Data());

            if (texture->HasPerMipView())
            {
                for (uint32_t i = 0; i < texture->GetMipCount(); i++)
                {
                    m_rhi_device->GetContextRhi()->device_context->ClearUnorderedAccessViewFloat(static_cast<ID3D11UnorderedAccessView*>(texture->Get_Resource_Views_Uav(i)), clear_color.Data());
                }
            }
        }
        else if (texture->IsRenderTarget())
        {
            if (texture->IsColorFormat())
            {
                if (clear_color == rhi_color_load || clear_color == rhi_color_dont_care)
                    return;

                m_rhi_device->GetContextRhi()->device_context->ClearRenderTargetView
                (
                    static_cast<ID3D11RenderTargetView*>(const_cast<void*>(texture->Get_Resource_View_RenderTarget(color_index))),
                    clear_color.Data()
                );
            }
            else if (texture->IsDepthStencilFormat())
            {
                if ((clear_depth == rhi_depth_load || clear_depth == rhi_depth_dont_care) && (clear_stencil == rhi_stencil_load || clear_stencil == rhi_stencil_dont_care))
                    return;

                UINT clear_flags = 0;
                clear_flags |= (clear_depth     != rhi_depth_load   && clear_depth   != rhi_depth_dont_care)     ? D3D11_CLEAR_DEPTH : 0;
                clear_flags |= (clear_stencil   != rhi_stencil_load && clear_stencil != rhi_stencil_dont_care)   ? D3D11_CLEAR_STENCIL : 0;
                if (clear_flags != 0)
                {
                    m_rhi_device->GetContextRhi()->device_context->ClearDepthStencilView
                    (
                        static_cast<ID3D11DepthStencilView*>(texture->Get_Resource_View_DepthStencil(depth_stencil_index)),
                        clear_flags,
                        static_cast<FLOAT>(clear_depth),
                        static_cast<UINT8>(clear_stencil)
                    );
                }
            }
        }
    }

    bool RHI_CommandList::Draw(const uint32_t vertex_count)
    {
        m_rhi_device->GetContextRhi()->device_context->Draw(static_cast<UINT>(vertex_count), 0);
        m_profiler->m_rhi_draw++;

        return true;
    }

    bool RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset)
    {
        m_rhi_device->GetContextRhi()->device_context->DrawIndexed
        (
            static_cast<UINT>(index_count),
            static_cast<UINT>(index_offset),
            static_cast<INT>(vertex_offset)
        );

        m_profiler->m_rhi_draw++;

        return true;
    }

    bool RHI_CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z, bool async /*= false*/)
    {
        ID3D11Device5* device = m_rhi_device->GetContextRhi()->device;
        ID3D11DeviceContext4* device_context = m_rhi_device->GetContextRhi()->device_context;

        device_context->Dispatch(x, y, z);
        m_profiler->m_rhi_dispatch++;

        // Make sure to clean the compute shader UAV slots after dispatching.
        // If we try to bind the resource but it's still bound as a computer shader output the runtime will automatically set the ID3D11ShaderResourceView to null.
        const void* resource_array[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        device_context->CSSetUnorderedAccessViews(0, 8, reinterpret_cast<ID3D11UnorderedAccessView* const*>(&resource_array), nullptr);

        return true;
    }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_Texture* destination)
    {
        // Ensure restrictions based on: https://docs.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource
        SP_ASSERT(source != nullptr);
        SP_ASSERT(destination != nullptr);
        SP_ASSERT(source->Get_Resource() != nullptr);
        SP_ASSERT(destination->Get_Resource() != nullptr);
        SP_ASSERT(source->GetObjectId() != destination->GetObjectId());
        SP_ASSERT(source->GetFormat() == destination->GetFormat());
        SP_ASSERT(source->GetWidth() == destination->GetWidth());
        SP_ASSERT(source->GetHeight() == destination->GetHeight());
        SP_ASSERT(source->GetArrayLength() == destination->GetArrayLength());
        SP_ASSERT(source->GetMipCount() == destination->GetMipCount());

        m_rhi_device->GetContextRhi()->device_context->CopyResource(static_cast<ID3D11Resource*>(destination->Get_Resource()), static_cast<ID3D11Resource*>(source->Get_Resource()));
    }

    void RHI_CommandList::SetViewport(const RHI_Viewport& viewport) const
    {
        D3D11_VIEWPORT d3d11_viewport   = {};
        d3d11_viewport.TopLeftX         = viewport.x;
        d3d11_viewport.TopLeftY         = viewport.y;
        d3d11_viewport.Width            = viewport.width;
        d3d11_viewport.Height           = viewport.height;
        d3d11_viewport.MinDepth         = viewport.depth_min;
        d3d11_viewport.MaxDepth         = viewport.depth_max;

        m_rhi_device->GetContextRhi()->device_context->RSSetViewports(1, &d3d11_viewport);
    }

    void RHI_CommandList::SetScissorRectangle(const Math::Rectangle& scissor_rectangle) const
    {
        const D3D11_RECT d3d11_rectangle = { static_cast<LONG>(scissor_rectangle.left), static_cast<LONG>(scissor_rectangle.top), static_cast<LONG>(scissor_rectangle.right), static_cast<LONG>(scissor_rectangle.bottom) };

        m_rhi_device->GetContextRhi()->device_context->RSSetScissorRects(1, &d3d11_rectangle);
    }

    void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer, const uint64_t offset /*= 0*/)
    {
        SP_ASSERT(buffer != nullptr);
        SP_ASSERT(buffer->GetResource() != nullptr);

        ID3D11Buffer* vertex_buffer         = static_cast<ID3D11Buffer*>(buffer->GetResource());
        UINT stride                         = buffer->GetStride();
        UINT offsets[]                      = { static_cast<UINT>(offset) };
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Get currently set buffer
        ID3D11Buffer* set_buffer    = nullptr;
        UINT set_stride             = buffer->GetStride();
        UINT set_offset             = 0;
        device_context->IAGetVertexBuffers(0, 1, &set_buffer, &set_stride, &set_offset);

        // Skip if already set
        if (set_buffer == vertex_buffer && set_offset == offset)
            return;

        // Set
        device_context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, offsets);
        m_profiler->m_rhi_bindings_buffer_vertex++;
    }

    void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer, const uint64_t offset /*= 0*/)
    {
        SP_ASSERT(buffer != nullptr);
        SP_ASSERT(buffer->GetResource() != nullptr);

        ID3D11Buffer* index_buffer          = static_cast<ID3D11Buffer*>(buffer->GetResource());
        const DXGI_FORMAT format            = buffer->Is16Bit() ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Get currently set buffer
        ID3D11Buffer* set_buffer    = nullptr;
        DXGI_FORMAT set_format      = DXGI_FORMAT_UNKNOWN;
        UINT set_offset             = 0;
        device_context->IAGetIndexBuffer(&set_buffer, &set_format, &set_offset);

        // Skip if already set
        if (set_buffer == index_buffer && set_offset == offset)
            return;

        // Set
        device_context->IASetIndexBuffer(index_buffer, format, static_cast<UINT>(offset));
        m_profiler->m_rhi_bindings_buffer_index++;
    }

    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, const uint8_t scope, RHI_ConstantBuffer* constant_buffer) const
    {
        void* buffer                        = static_cast<ID3D11Buffer*>(constant_buffer ? constant_buffer->GetResource() : nullptr);
        const void* buffer_array[1]         = { buffer };
        const UINT range                    = 1;
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        if (scope & RHI_Shader_Vertex)
        {
            // Set only if not set
            ID3D11Buffer* set_buffer = nullptr;
            device_context->VSGetConstantBuffers(slot, range, &set_buffer);
            if (set_buffer != buffer)
            {
                device_context->VSSetConstantBuffers(slot, range, reinterpret_cast<ID3D11Buffer* const*>(range > 1 ? buffer : &buffer_array));
                m_profiler->m_rhi_bindings_buffer_constant++;
            }
        }

        if (scope & RHI_Shader_Pixel)
        {
            // Set only if not set
            ID3D11Buffer* set_buffer = nullptr;
            device_context->PSGetConstantBuffers(slot, range, &set_buffer);
            if (set_buffer != buffer)
            {
                device_context->PSSetConstantBuffers(slot, range, reinterpret_cast<ID3D11Buffer* const*>(range > 1 ? buffer : &buffer_array));
                m_profiler->m_rhi_bindings_buffer_constant++;
            }
        }

        if (scope & RHI_Shader_Compute)
        {
            // Set only if not set
            ID3D11Buffer* set_buffer = nullptr;
            device_context->CSGetConstantBuffers(slot, range, &set_buffer);
            if (set_buffer != buffer)
            {
                device_context->CSSetConstantBuffers(slot, range, reinterpret_cast<ID3D11Buffer* const*>(range > 1 ? buffer : &buffer_array));
                m_profiler->m_rhi_bindings_buffer_constant++;
            }
        }
    }

    void RHI_CommandList::SetSampler(const uint32_t slot, RHI_Sampler* sampler) const
    {
        const UINT start_slot               = slot;
        const UINT range                    = 1;
        const void* sampler_array[1]        = { sampler ? sampler->GetResource() : nullptr };
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        if (m_pipeline_state->IsCompute())
        {
            // Set only if not already set
            ID3D11SamplerState* set_sampler = nullptr;
            device_context->CSGetSamplers(start_slot, range, &set_sampler);
            if (set_sampler != sampler_array[0])
            {
                device_context->CSSetSamplers(start_slot, range, reinterpret_cast<ID3D11SamplerState* const*>(&sampler_array));
                m_profiler->m_rhi_bindings_sampler++;
            }
        }
        else
        {
            // Set only if not already set
            ID3D11SamplerState* set_sampler = nullptr;
            device_context->PSGetSamplers(start_slot, range, &set_sampler);
            if (set_sampler != sampler_array[0])
            {
                device_context->PSSetSamplers(start_slot, range, reinterpret_cast<ID3D11SamplerState* const*>(&sampler_array));
                m_profiler->m_rhi_bindings_sampler++;
            }
        }
    }

    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture, const int mip /*= -1*/, const bool ranged /*= false*/, const bool uav /*= false*/)
    {
        bool mip_requested                  = mip != -1;
        const UINT range                    = ranged ? (texture->GetMipCount() - (mip_requested ? mip : 0)): 1;
        const uint8_t range_max             = 12;
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Build resource array
        array<void*, range_max> view_array;
        view_array.fill(nullptr);
        if (texture)
        {
            if (!ranged)
            {
                if (uav)
                {
                    view_array[0] = mip_requested ? texture->Get_Resource_Views_Uav(mip) : texture->Get_Resource_View_Uav();
                }
                else
                {
                    view_array[0] = mip_requested ? texture->Get_Resource_Views_Srv(mip) : texture->Get_Resource_View_Srv();
                }
            }
            else
            {
                for (uint32_t i = 0; i < range; i++)
                {
                    uint32_t mip_offset = mip + i;
                    view_array[i] = uav ? texture->Get_Resource_Views_Uav(mip_offset) : texture->Get_Resource_Views_Srv(mip_offset);
                }
            }
        }

        // UAV
        if (uav)
        {
            // Set if not already set
            array<ID3D11UnorderedAccessView*, range_max> set_uavs;
            set_uavs.fill(nullptr);
            device_context->CSGetUnorderedAccessViews(slot, range, &set_uavs[0]);
            if (set_uavs[0] != view_array[0])
            {
                device_context->CSSetUnorderedAccessViews(slot, range, reinterpret_cast<ID3D11UnorderedAccessView* const*>(view_array.data()), nullptr);
                m_profiler->m_rhi_bindings_texture_storage++;
            }
        }
        // SRV
        else
        {
            const uint8_t scope = m_pipeline_state->IsCompute() ? RHI_Shader_Compute : RHI_Shader_Pixel;

            array<ID3D11ShaderResourceView*, range_max> set_srvs;
            set_srvs.fill(nullptr);

            if (scope & RHI_Shader_Pixel)
            {
                // Set if not already set
                device_context->PSGetShaderResources(slot, range, &set_srvs[0]);
                if (set_srvs[0] != view_array[0])
                {
                    device_context->PSSetShaderResources(slot, range, reinterpret_cast<ID3D11ShaderResourceView* const*>(view_array.data()));
                    m_profiler->m_rhi_bindings_texture_sampled++;
                }
            }
            else if (scope & RHI_Shader_Compute)
            {
                // Set if not already set
                device_context->CSGetShaderResources(slot, range, &set_srvs[0]);
                if (set_srvs[0] != view_array[0])
                {
                    device_context->CSSetShaderResources(slot, range, reinterpret_cast<ID3D11ShaderResourceView* const*>(view_array.data()));
                    m_profiler->m_rhi_bindings_texture_sampled++;
                }
            }
        }
    }

    void RHI_CommandList::SetStructuredBuffer(const uint32_t slot, RHI_StructuredBuffer* structured_buffer) const
    {
        array<void*, 1> view_array          = { structured_buffer ? structured_buffer->GetResource() : nullptr };
        const UINT range                    = 1;
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Set only if not set
        ID3D11UnorderedAccessView* set_uav = nullptr;
        device_context->CSGetUnorderedAccessViews(slot, range, &set_uav);
        if (set_uav != view_array[0])
        {
            device_context->CSSetUnorderedAccessViews(slot, range, reinterpret_cast<ID3D11UnorderedAccessView* const*>(&view_array), nullptr);
            m_profiler->m_rhi_bindings_buffer_structured++;
        }
    }

    bool RHI_CommandList::Timestamp_Start(void* query_disjoint /*= nullptr*/, void* query_start /*= nullptr*/)
    {
        SP_ASSERT(query_disjoint != nullptr);
        SP_ASSERT(query_start != nullptr);
        SP_ASSERT(m_rhi_device != nullptr);

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        rhi_context->device_context->Begin(static_cast<ID3D11Query*>(query_disjoint));
        rhi_context->device_context->End(static_cast<ID3D11Query*>(query_start));

        return true;
    }

    bool RHI_CommandList::Timestamp_End(void* query_disjoint /*= nullptr*/, void* query_end /*= nullptr*/)
    {
        SP_ASSERT(query_disjoint != nullptr);
        SP_ASSERT(query_end != nullptr);
        SP_ASSERT(m_rhi_device != nullptr);

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        rhi_context->device_context->End(static_cast<ID3D11Query*>(query_end));
        rhi_context->device_context->End(static_cast<ID3D11Query*>(query_disjoint));

        return true;
    }

    float RHI_CommandList::Timestamp_GetDuration(void* query_disjoint, void* query_start, void* query_end, const uint32_t pass_index)
    {
        SP_ASSERT(query_disjoint != nullptr);
        SP_ASSERT(query_start != nullptr);
        SP_ASSERT(query_end != nullptr);
        SP_ASSERT(m_rhi_device != nullptr);

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Check whether timestamps were disjoint during the last frame
        D3D10_QUERY_DATA_TIMESTAMP_DISJOINT disjoint_data = {};
        while (rhi_context->device_context->GetData(static_cast<ID3D11Query*>(query_disjoint), &disjoint_data, sizeof(disjoint_data), 0) != S_OK);
        if (disjoint_data.Disjoint)
            return 0.0f;

        // Get start time
        uint64_t start_time = 0; 
        rhi_context->device_context->GetData(static_cast<ID3D11Query*>(query_start), &start_time, sizeof(start_time), 0);

        // Get end time
        uint64_t end_time = 0;
        rhi_context->device_context->GetData(static_cast<ID3D11Query*>(query_end), &end_time, sizeof(end_time), 0);

        // Compute duration in ms
        const uint64_t delta        = end_time - start_time;
        const double duration_ms    = (delta * 1000.0) / static_cast<double>(disjoint_data.Frequency);

        return static_cast<float>(duration_ms);
    }

    uint32_t RHI_CommandList::Gpu_GetMemoryUsed(RHI_Device* rhi_device)
    {
        if (!m_memory_query_support)
            return 0;

        if (const PhysicalDevice* physical_device = rhi_device->GetPrimaryPhysicalDevice())
        {
            if (IDXGIAdapter3* adapter = static_cast<IDXGIAdapter3*>(physical_device->GetData()))
            {
                // Try to query video memory info
                DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
                const HRESULT result = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);

                if (SUCCEEDED(result))
                {
                    return static_cast<uint32_t>(info.CurrentUsage / 1024 / 1024); // convert to MBs
                }
                else
                {
                    // Some integrated or older dedicated GPUs might not support video memory queries, log the error once and don't query again.
                    LOG_ERROR("Failed to get adapter memory info, %s", d3d11_utility::dxgi_error_to_string(result));
                    m_memory_query_support = false;
                }
            }
        }

        return 0;
    }

    bool RHI_CommandList::Gpu_QueryCreate(RHI_Device* rhi_device, void** query, const RHI_Query_Type type)
    {
        SP_ASSERT(rhi_device != nullptr);
        SP_ASSERT(rhi_device->GetContextRhi()->device != nullptr);

        D3D11_QUERY_DESC desc   = {};
        desc.Query              = (type == RHI_Query_Type::Timestamp_Disjoint) ? D3D11_QUERY_TIMESTAMP_DISJOINT : D3D11_QUERY_TIMESTAMP;
        desc.MiscFlags          = 0;
        const auto result = rhi_device->GetContextRhi()->device->CreateQuery(&desc, reinterpret_cast<ID3D11Query**>(query));
        if (FAILED(result))
        {
            LOG_ERROR("Failed to create ID3D11Query");
            return false;
        }

        return true;
    }

    void RHI_CommandList::Gpu_QueryRelease(void*& query_object)
    {
        if (!query_object)
            return;

        static_cast<ID3D11Query*>(query_object)->Release();
        query_object = nullptr;
    }

    void RHI_CommandList::Timeblock_Start(const RHI_PipelineState* pipeline_state)
    {
        if (!pipeline_state || !pipeline_state->pass_name)
            return;

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Allowed to profile ?
        if (rhi_context->profiler && pipeline_state->profile)
        {
            if (m_profiler)
            {
                m_profiler->TimeBlockStart(pipeline_state->pass_name, TimeBlockType::Cpu, this);
                m_profiler->TimeBlockStart(pipeline_state->pass_name, TimeBlockType::Gpu, this);
            }
        }

        // Allowed to mark ?
        if (rhi_context->markers && pipeline_state->mark)
        {
            m_rhi_device->GetContextRhi()->annotation->BeginEvent(FileSystem::StringToWstring(pipeline_state->pass_name).c_str());
        }
    }

    void RHI_CommandList::Timeblock_End(const RHI_PipelineState* pipeline_state)
    {
        if (!pipeline_state)
            return;

        // Allowed to mark ?
        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();
        if (rhi_context->markers && pipeline_state->mark)
        {
            rhi_context->annotation->EndEvent();
        }

        // Allowed to profile ?
        if (rhi_context->profiler && pipeline_state->profile)
        {
            if (m_profiler)
            {
                m_profiler->TimeBlockEnd(); // cpu
                m_profiler->TimeBlockEnd(); // gpu
            }
        }
    }

    bool RHI_CommandList::Deferred_BeginRenderPass()
    {
        return true;
    }

    bool RHI_CommandList::Deferred_BindPipeline()
    {
        return true;
    }

    bool RHI_CommandList::Deferred_BindDescriptorSet()
    {
        return true;
    }

    bool RHI_CommandList::OnDraw()
    {
        return true;
    }
}
