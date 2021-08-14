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

//= INCLUDES ============================
#include "Spartan.h"
#include "Renderer.h"
#include "ShaderGBuffer.h"
#include "ShaderLight.h"
#include "Font/Font.h"
#include "../Resource/ResourceCache.h"
#include "../RHI/RHI_Texture2D.h"
#include "../RHI/RHI_Texture2DArray.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_Sampler.h"
#include "../RHI/RHI_BlendState.h"
#include "../RHI/RHI_ConstantBuffer.h"
#include "../RHI/RHI_RasterizerState.h"
#include "../RHI/RHI_DepthStencilState.h"
#include "../RHI/RHI_SwapChain.h"
#include "../RHI/RHI_StructuredBuffer.h"
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    void Renderer::CreateConstantBuffers()
    {
        const bool is_dynamic       = true;
        const uint32_t offset_count = 1024; // should be big enough (buffers can dynamically reallocate anyway)

        m_cb_frame_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device, "frame", is_dynamic);
        m_cb_frame_gpu->Create<Cb_Frame>(offset_count);

        m_cb_uber_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device, "uber", is_dynamic);
        m_cb_uber_gpu->Create<Cb_Uber>(offset_count);

        m_cb_light_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device, "light", is_dynamic);
        m_cb_light_gpu->Create<Cb_Light>(offset_count);

        m_cb_material_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device, "material", is_dynamic);
        m_cb_material_gpu->Create<Cb_Material>(offset_count);
    }

    void Renderer::CreateStructuredBuffers()
    {
        static uint32_t counter = 0;
        uint32_t element_count = 1;
        m_sb_counter = make_shared<RHI_StructuredBuffer>(m_rhi_device, static_cast<uint32_t>(sizeof(uint32_t)), element_count, static_cast<void*>(&counter));
    }

    void Renderer::CreateDepthStencilStates()
    {
        RHI_Comparison_Function reverse_z_aware_comp_func = GetComparisonFunction();

        // arguments: depth_test, depth_write, depth_function, stencil_test, stencil_write, stencil_function
        m_depth_stencil_off_off = make_shared<RHI_DepthStencilState>(m_rhi_device, false, false, RHI_Comparison_Function::Never,    false, false, RHI_Comparison_Function::Never);  // no depth or stencil
        m_depth_stencil_rw_off  = make_shared<RHI_DepthStencilState>(m_rhi_device, true,  true,  reverse_z_aware_comp_func,         false, false, RHI_Comparison_Function::Never);  // depth
        m_depth_stencil_r_off   = make_shared<RHI_DepthStencilState>(m_rhi_device, true,  false, reverse_z_aware_comp_func,         false, false, RHI_Comparison_Function::Never);  // depth
        m_depth_stencil_off_r   = make_shared<RHI_DepthStencilState>(m_rhi_device, false, false, RHI_Comparison_Function::Never,    true,  false, RHI_Comparison_Function::Equal);  // depth + stencil
        m_depth_stencil_rw_w    = make_shared<RHI_DepthStencilState>(m_rhi_device, true,  true,  reverse_z_aware_comp_func,         false, true,  RHI_Comparison_Function::Always); // depth + stencil
    }

    void Renderer::CreateRasterizerStates()
    {
        float depth_bias              = GetOption(Render_ReverseZ) ? -m_depth_bias : m_depth_bias;
        float depth_bias_slope_scaled = GetOption(Render_ReverseZ) ? -m_depth_bias_slope_scaled : m_depth_bias_slope_scaled;

        m_rasterizer_cull_back_solid     = make_shared<RHI_RasterizerState>(m_rhi_device, RHI_Cull_Mode::Back, RHI_Fill_Mode::Solid,     true,  false, false);
        m_rasterizer_cull_back_wireframe = make_shared<RHI_RasterizerState>(m_rhi_device, RHI_Cull_Mode::Back, RHI_Fill_Mode::Wireframe, true,  false, true);
        m_rasterizer_light_point_spot    = make_shared<RHI_RasterizerState>(m_rhi_device, RHI_Cull_Mode::Back, RHI_Fill_Mode::Solid,     true,  false, false, depth_bias,        m_depth_bias_clamp, depth_bias_slope_scaled);
        m_rasterizer_light_directional   = make_shared<RHI_RasterizerState>(m_rhi_device, RHI_Cull_Mode::Back, RHI_Fill_Mode::Solid,     false, false, false, depth_bias * 0.1f, m_depth_bias_clamp, depth_bias_slope_scaled);
    }

    void Renderer::CreateBlendStates()
    {
        // blend_enabled, source_blend, dest_blend, blend_op, source_blend_alpha, dest_blend_alpha, blend_op_alpha, blend_factor
        m_blend_disabled = make_shared<RHI_BlendState>(m_rhi_device, false);
        m_blend_alpha    = make_shared<RHI_BlendState>(m_rhi_device, true, RHI_Blend::Src_Alpha, RHI_Blend::Inv_Src_Alpha, RHI_Blend_Operation::Add, RHI_Blend::One, RHI_Blend::One, RHI_Blend_Operation::Add, 0.0f);
        m_blend_additive = make_shared<RHI_BlendState>(m_rhi_device, true, RHI_Blend::One,       RHI_Blend::One,           RHI_Blend_Operation::Add, RHI_Blend::One, RHI_Blend::One, RHI_Blend_Operation::Add, 1.0f);
    }

    void Renderer::CreateSamplers(const bool create_only_anisotropic /*= false*/)
    {
        uint32_t anisotropy                      = GetOptionValue<uint32_t>(Renderer_Option_Value::Anisotropy);
        RHI_Comparison_Function depth_comparison = GetOption(Render_ReverseZ) ? RHI_Comparison_Function::Greater : RHI_Comparison_Function::Less;
        float mip_lod_bias                       = -log2(m_resolution_output.x / m_resolution_render.x); // negative mip bias when upscaling is active (helps bring in some texture detail)

        // minification, magnification, mip, sampler address mode, comparison, anisotropy, comparison, mip lod bias
        if (!create_only_anisotropic)
        {
            m_sampler_compare_depth   = make_shared<RHI_Sampler>(m_rhi_device, RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Sampler_Mipmap_Mode::Nearest, RHI_Sampler_Address_Mode::Clamp, depth_comparison, 0, true);
            m_sampler_point_clamp     = make_shared<RHI_Sampler>(m_rhi_device, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Mipmap_Mode::Nearest, RHI_Sampler_Address_Mode::Clamp, RHI_Comparison_Function::Always);
            m_sampler_point_wrap      = make_shared<RHI_Sampler>(m_rhi_device, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Mipmap_Mode::Nearest, RHI_Sampler_Address_Mode::Wrap,  RHI_Comparison_Function::Always);
            m_sampler_bilinear_clamp  = make_shared<RHI_Sampler>(m_rhi_device, RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Sampler_Mipmap_Mode::Nearest, RHI_Sampler_Address_Mode::Clamp, RHI_Comparison_Function::Always);
            m_sampler_bilinear_wrap   = make_shared<RHI_Sampler>(m_rhi_device, RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Sampler_Mipmap_Mode::Nearest, RHI_Sampler_Address_Mode::Wrap,  RHI_Comparison_Function::Always);
            m_sampler_trilinear_clamp = make_shared<RHI_Sampler>(m_rhi_device, RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Sampler_Mipmap_Mode::Linear,  RHI_Sampler_Address_Mode::Clamp, RHI_Comparison_Function::Always);
        }

        m_sampler_anisotropic_wrap = make_shared<RHI_Sampler>(m_rhi_device, RHI_Filter::Linear, RHI_Filter::Linear, RHI_Sampler_Mipmap_Mode::Linear, RHI_Sampler_Address_Mode::Wrap, RHI_Comparison_Function::Always, anisotropy, false, mip_lod_bias);

        LOG_INFO("Mip load bias set to %f", mip_lod_bias);
    }

    void Renderer::CreateRenderTextures(const bool create_render, const bool create_output, const bool create_fixed, const bool create_dynamic)
    {
        // Get render resolution
        uint32_t width_render  = static_cast<uint32_t>(m_resolution_render.x);
        uint32_t height_render = static_cast<uint32_t>(m_resolution_render.y);

        // Get output resolution
        uint32_t width_output  = static_cast<uint32_t>(m_resolution_output.x);
        uint32_t height_output = static_cast<uint32_t>(m_resolution_output.y);

        // Ensure none of the textures is being used by the GPU
        Flush();

        // rt_gbuffer_normal: From and below Texture_Format_R8G8B8A8_UNORM, normals have noticeable banding.
        // rt_hdr/rt_hdr_2/rt_dof_half/rt_dof_half_2/rt_post_process_hdr/rt_post_process_hdr_2/rt_post_process_ldr/rt_post_process_ldr_2: Investigate using less bits but have an alpha channel
        // rt_ssao/rt_ssao_blurred: If gi is disabled, the texture format could just be RHI_Format_R8_Unorm, but calling CreateRenderTextures() dynamically will re-create a lot of textures. Find an elegant solution to improve CreateRenderTextures().

        // Deduce how many mips are required to scale down any dimension close to 16px (or exactly)
        uint32_t mip_count_to_16px = 1;
        uint32_t width             = width_render;
        uint32_t height            = height_render;
        while ((width / 2) > 16 && (height / 2) > 16)
        {
            width /= 2;
            height /= 2;
            mip_count_to_16px++;
        }

        // Deduce how many mips are required to scale down any dimension close to 32px (or exactly)
        uint32_t mip_count_to_32px = mip_count_to_16px - 1;

        // Render resolution
        if (create_render)
        {
            // Frame (HDR)
            RENDER_TARGET(RendererRt::Frame_Render)   = make_unique<RHI_Texture2D>(m_context, width_render, height_render, mip_count_to_16px, RHI_Format_R11G11B10_Float, RHI_Texture_Rt_Color | RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipView, "rt_frame_render");
            RENDER_TARGET(RendererRt::Frame_Render_2) = make_unique<RHI_Texture2D>(m_context, width_render, height_render, mip_count_to_16px, RHI_Format_R11G11B10_Float, RHI_Texture_Rt_Color | RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipView, "rt_frame_render_2");

            // G-Buffer
            RENDER_TARGET(RendererRt::Gbuffer_Albedo)   = make_shared<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R8G8B8A8_Unorm,     RHI_Texture_Rt_Color        | RHI_Texture_Srv,                                       "rt_gbuffer_albedo");
            RENDER_TARGET(RendererRt::Gbuffer_Normal)   = make_shared<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R16G16B16A16_Float, RHI_Texture_Rt_Color        | RHI_Texture_Srv,                                       "rt_gbuffer_normal");
            RENDER_TARGET(RendererRt::Gbuffer_Material) = make_shared<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R8G8B8A8_Unorm,     RHI_Texture_Rt_Color        | RHI_Texture_Srv,                                       "rt_gbuffer_material");
            RENDER_TARGET(RendererRt::Gbuffer_Velocity) = make_shared<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R16G16_Float,       RHI_Texture_Rt_Color        | RHI_Texture_Srv,                                       "rt_gbuffer_velocity");
            RENDER_TARGET(RendererRt::Gbuffer_Depth)    = make_shared<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_D32_Float,          RHI_Texture_Rt_DepthStencil | RHI_Texture_Rt_DepthStencilReadOnly | RHI_Texture_Srv, "rt_gbuffer_depth");

            // Light
            RENDER_TARGET(RendererRt::Light_Diffuse)              = make_unique<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_CanBeCleared, "rt_light_diffuse");
            RENDER_TARGET(RendererRt::Light_Diffuse_Transparent)  = make_unique<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_CanBeCleared, "rt_light_diffuse_transparent");
            RENDER_TARGET(RendererRt::Light_Specular)             = make_unique<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_CanBeCleared, "rt_light_specular");
            RENDER_TARGET(RendererRt::Light_Specular_Transparent) = make_unique<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_CanBeCleared, "rt_light_specular_transparent");
            RENDER_TARGET(RendererRt::Light_Volumetric)           = make_unique<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_CanBeCleared, "rt_light_volumetric");

            // Misc
            RENDER_TARGET(RendererRt::Ssao)       = make_unique<RHI_Texture2D>(m_context, width_render,     height_render,     1,                 RHI_Format_R16G16B16A16_Snorm, RHI_Texture_Uav | RHI_Texture_Srv,                          "rt_ssao");
            RENDER_TARGET(RendererRt::Ssr)        = make_shared<RHI_Texture2D>(m_context, width_render,     height_render,     mip_count_to_32px, RHI_Format_R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipView, "rt_ssr");
            RENDER_TARGET(RendererRt::Dof_Half)   = make_unique<RHI_Texture2D>(m_context, width_render / 2, height_render / 2, 1,                 RHI_Format_R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv,                          "rt_dof_half");
            RENDER_TARGET(RendererRt::Dof_Half_2) = make_unique<RHI_Texture2D>(m_context, width_render / 2, height_render / 2, 1,                 RHI_Format_R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv,                          "rt_dof_half_2");
        }

        // Output resolution
        if (create_output)
        {
            // Frame (LDR)
            RENDER_TARGET(RendererRt::Frame_Output)   = make_unique<RHI_Texture2D>(m_context, width_output, height_output, 1, RHI_Format_R11G11B10_Float, RHI_Texture_Rt_Color | RHI_Texture_Uav | RHI_Texture_Srv, "rt_frame_output");
            RENDER_TARGET(RendererRt::Frame_Output_2) = make_unique<RHI_Texture2D>(m_context, width_output, height_output, 1, RHI_Format_R11G11B10_Float, RHI_Texture_Rt_Color | RHI_Texture_Uav | RHI_Texture_Srv, "rt_frame_output_2");

            // Misc
            RENDER_TARGET(RendererRt::Bloom) = make_shared<RHI_Texture2D>(m_context, width_output, height_output, mip_count_to_16px, RHI_Format_R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipView, "rt_bloom");
        }

        // Fixed resolution
        if (create_fixed)
        {
            RENDER_TARGET(RendererRt::Brdf_Specular_Lut) = make_unique<RHI_Texture2D>(m_context, 400, 400, 1, RHI_Format_R8G8_Unorm, RHI_Texture_Uav | RHI_Texture_Srv, "rt_brdf_specular_lut");
            m_brdf_specular_lut_rendered = false;
        }

        // Dynamic resolution
        if (create_dynamic)
        {
            // Blur
            bool is_output_larger = width_output > width_render && height_output > height_render;
            uint32_t width  = is_output_larger ? width_output : width_render;
            uint32_t height = is_output_larger ? height_output : height_render;
            RENDER_TARGET(RendererRt::Blur) = make_unique<RHI_Texture2D>(m_context, width, height, 1, RHI_Format_R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "rt_blur");

            // TAA History
            {
                RHI_Texture* rt_taa_history = RENDER_TARGET(RendererRt::Taa_History).get();
                bool upsampling_enabled     = GetOption(Render_Upsample_TAA);
                width                       = upsampling_enabled ? width_output  : width_render;
                height                      = upsampling_enabled ? height_output : height_render;

                if (!rt_taa_history || (rt_taa_history->GetWidth() != width || rt_taa_history->GetHeight() != height))
                {
                    RENDER_TARGET(RendererRt::Taa_History) = make_unique<RHI_Texture2D>(m_context, width, height, 1, RHI_Format_R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv, "rt_taa_history");
                    LOG_INFO("Taa history resolution has been set to %dx%d", width, height);
                }
            }
        }
    }

    void Renderer::CreateShaders()
    {
        // Compile asynchronously ?
        bool async = true;

        // Get standard shader directory
        const auto dir_shaders = m_resource_cache->GetResourceDirectory(ResourceDirectory::Shaders) + "\\";

        // Shader which compile different variations when needed
        m_shaders[RendererShader::Gbuffer_P] = make_shared<ShaderGBuffer>(m_context);
        m_shaders[RendererShader::Light_C]   = make_shared<ShaderLight>(m_context);

        // G-Buffer
        m_shaders[RendererShader::Gbuffer_V] = make_shared<RHI_Shader>(m_context, RHI_Vertex_Type::PosTexNorTan);
        m_shaders[RendererShader::Gbuffer_V]->Compile(RHI_Shader_Vertex, dir_shaders + "GBuffer.hlsl", async);

        // Quad
        m_shaders[RendererShader::Quad_V] = make_shared<RHI_Shader>(m_context, RHI_Vertex_Type::PosTex);
        m_shaders[RendererShader::Quad_V]->Compile(RHI_Shader_Vertex, dir_shaders + "quad.hlsl", async);

        // Depth prepass
        {
            m_shaders[RendererShader::Depth_Prepass_V] = make_shared<RHI_Shader>(m_context, RHI_Vertex_Type::PosTex);
            m_shaders[RendererShader::Depth_Prepass_V]->Compile(RHI_Shader_Vertex, dir_shaders + "depth_prepass.hlsl", async);

            m_shaders[RendererShader::Depth_Prepass_P] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Depth_Prepass_P]->Compile(RHI_Shader_Pixel, dir_shaders + "depth_prepass.hlsl", async);
        }

        // Depth light
        {
            m_shaders[RendererShader::Depth_Light_V] = make_shared<RHI_Shader>(m_context, RHI_Vertex_Type::PosTex);
            m_shaders[RendererShader::Depth_Light_V]->Compile(RHI_Shader_Vertex, dir_shaders + "depth_light.hlsl", async);

            m_shaders[RendererShader::Depth_Light_P] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Depth_Light_P]->Compile(RHI_Shader_Pixel, dir_shaders + "depth_light.hlsl", async);
        }

        // BRDF - Specular Lut
        m_shaders[RendererShader::BrdfSpecularLut_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::BrdfSpecularLut_C]->Compile(RHI_Shader_Compute, dir_shaders + "brdf_specular_lut.hlsl", async);

        // Copy
        {
            m_shaders[RendererShader::Copy_Point_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Copy_Point_C]->AddDefine("COMPUTE");
            m_shaders[RendererShader::Copy_Point_C]->Compile(RHI_Shader_Compute, dir_shaders + "copy.hlsl", async);

            m_shaders[RendererShader::Copy_Bilinear_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Copy_Bilinear_C]->AddDefine("COMPUTE");
            m_shaders[RendererShader::Copy_Bilinear_C]->AddDefine("BILINEAR");
            m_shaders[RendererShader::Copy_Bilinear_C]->Compile(RHI_Shader_Compute, dir_shaders + "copy.hlsl", async);

            m_shaders[RendererShader::Copy_Point_P] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Copy_Point_P]->AddDefine("PIXEL");
            m_shaders[RendererShader::Copy_Point_P]->AddDefine("BILINEAR");
            m_shaders[RendererShader::Copy_Point_P]->Compile(RHI_Shader_Pixel, dir_shaders + "copy.hlsl", async);

            m_shaders[RendererShader::Copy_Bilinear_P] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Copy_Bilinear_P]->AddDefine("PIXEL");
            m_shaders[RendererShader::Copy_Bilinear_P]->AddDefine("BILINEAR");
            m_shaders[RendererShader::Copy_Bilinear_P]->Compile(RHI_Shader_Pixel, dir_shaders + "copy.hlsl", async);
        }

        // Blur
        {
            // Gaussian
            m_shaders[RendererShader::BlurGaussian_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::BlurGaussian_C]->AddDefine("PASS_BLUR_GAUSSIAN");
            m_shaders[RendererShader::BlurGaussian_C]->Compile(RHI_Shader_Compute, dir_shaders + "blur.hlsl", async);

            // Gaussian bilateral 
            m_shaders[RendererShader::BlurGaussianBilateral_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::BlurGaussianBilateral_C]->AddDefine("PASS_BLUR_BILATERAL_GAUSSIAN");
            m_shaders[RendererShader::BlurGaussianBilateral_C]->Compile(RHI_Shader_Compute, dir_shaders + "blur.hlsl", async);
        }

        // Bloom
        {
            // Downsample luminance
            m_shaders[RendererShader::BloomLuminance_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::BloomLuminance_C]->AddDefine("LUMINANCE");
            m_shaders[RendererShader::BloomLuminance_C]->Compile(RHI_Shader_Compute, dir_shaders + "bloom.hlsl", async);

            // Upsample blend (with previous mip)
            m_shaders[RendererShader::BloomUpsampleBlendMip_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::BloomUpsampleBlendMip_C]->AddDefine("UPSAMPLE_BLEND_MIP");
            m_shaders[RendererShader::BloomUpsampleBlendMip_C]->Compile(RHI_Shader_Compute, dir_shaders + "bloom.hlsl", async);

            // Upsample blend (with frame)
            m_shaders[RendererShader::BloomBlendFrame_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::BloomBlendFrame_C]->AddDefine("BLEND_FRAME");
            m_shaders[RendererShader::BloomBlendFrame_C]->Compile(RHI_Shader_Compute, dir_shaders + "bloom.hlsl", async);
        }

        // Film grain
        m_shaders[RendererShader::FilmGrain_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::FilmGrain_C]->Compile(RHI_Shader_Compute, dir_shaders + "film_grain.hlsl", async);

        // Chromatic aberration
        m_shaders[RendererShader::ChromaticAberration_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::ChromaticAberration_C]->AddDefine("PASS_CHROMATIC_ABERRATION");
        m_shaders[RendererShader::ChromaticAberration_C]->Compile(RHI_Shader_Compute, dir_shaders + "chromatic_aberration.hlsl", async);

        // Tone-mapping
        m_shaders[RendererShader::ToneMapping_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::ToneMapping_C]->Compile(RHI_Shader_Compute, dir_shaders + "tone_mapping.hlsl", async);

        // Gamma correction
        m_shaders[RendererShader::GammaCorrection_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::GammaCorrection_C]->Compile(RHI_Shader_Compute, dir_shaders + "gamma_correction.hlsl", async);

        // Anti-aliasing
        {
            // TAA
            m_shaders[RendererShader::Taa_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Taa_C]->Compile(RHI_Shader_Compute, dir_shaders + "temporal_antialiasing.hlsl", async);

            // FXAA
            m_shaders[RendererShader::Fxaa_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Fxaa_C]->Compile(RHI_Shader_Compute, dir_shaders + "fxaa.hlsl", async);
        }

        // Depth of Field
        {
            m_shaders[RendererShader::Dof_DownsampleCoc_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Dof_DownsampleCoc_C]->AddDefine("DOWNSAMPLE_CIRCLE_OF_CONFUSION");
            m_shaders[RendererShader::Dof_DownsampleCoc_C]->Compile(RHI_Shader_Compute, dir_shaders + "depth_of_field.hlsl", async);

            m_shaders[RendererShader::Dof_Bokeh_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Dof_Bokeh_C]->AddDefine("BOKEH");
            m_shaders[RendererShader::Dof_Bokeh_C]->Compile(RHI_Shader_Compute, dir_shaders + "depth_of_field.hlsl", async);

            m_shaders[RendererShader::Dof_Tent_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Dof_Tent_C]->AddDefine("TENT");
            m_shaders[RendererShader::Dof_Tent_C]->Compile(RHI_Shader_Compute, dir_shaders + "depth_of_field.hlsl", async);

            m_shaders[RendererShader::Dof_UpscaleBlend_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Dof_UpscaleBlend_C]->AddDefine("UPSCALE_BLEND");
            m_shaders[RendererShader::Dof_UpscaleBlend_C]->Compile(RHI_Shader_Compute, dir_shaders + "depth_of_field.hlsl", async);
        }

        // Motion Blur
        m_shaders[RendererShader::MotionBlur_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::MotionBlur_C]->Compile(RHI_Shader_Compute, dir_shaders + "motion_blur.hlsl", async);

        // Dithering
        m_shaders[RendererShader::Dithering_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Dithering_C]->Compile(RHI_Shader_Compute, dir_shaders + "dithering.hlsl", async);

        // SSAO
        {
            m_shaders[RendererShader::Ssao_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Ssao_C]->Compile(RHI_Shader_Compute, dir_shaders + "ssao.hlsl", async);

            m_shaders[RendererShader::Ssao_Gi_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Ssao_Gi_C]->AddDefine("GI");
            m_shaders[RendererShader::Ssao_Gi_C]->Compile(RHI_Shader_Compute, dir_shaders + "ssao.hlsl", async);
        }

        // Light
        {
            m_shaders[RendererShader::Light_Composition_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Light_Composition_C]->Compile(RHI_Shader_Compute, dir_shaders + "light_composition.hlsl", async);

            m_shaders[RendererShader::Light_ImageBased_P] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Light_ImageBased_P]->Compile(RHI_Shader_Pixel, dir_shaders + "light_image_based.hlsl", async);
        }

        // SSR
        m_shaders[RendererShader::Ssr_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Ssr_C]->Compile(RHI_Shader_Compute, dir_shaders + "ssr.hlsl", async);

        // Entity
        m_shaders[RendererShader::Entity_V] = make_shared<RHI_Shader>(m_context, RHI_Vertex_Type::PosTexNorTan);
        m_shaders[RendererShader::Entity_V]->Compile(RHI_Shader_Vertex, dir_shaders + "entity.hlsl", async);

        // Entity - Transform
        m_shaders[RendererShader::Entity_Transform_P] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Entity_Transform_P]->AddDefine("TRANSFORM");
        m_shaders[RendererShader::Entity_Transform_P]->Compile(RHI_Shader_Pixel, dir_shaders + "entity.hlsl", async);

        // Entity - Outline
        m_shaders[RendererShader::Entity_Outline_P] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Entity_Outline_P]->AddDefine("OUTLINE");
        m_shaders[RendererShader::Entity_Outline_P]->Compile(RHI_Shader_Pixel, dir_shaders + "entity.hlsl", async);

        // Font
        m_shaders[RendererShader::Font_V] = make_shared<RHI_Shader>(m_context, RHI_Vertex_Type::PosTex);
        m_shaders[RendererShader::Font_V]->Compile(RHI_Shader_Vertex, dir_shaders + "font.hlsl", async);
        m_shaders[RendererShader::Font_P] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Font_P]->Compile(RHI_Shader_Pixel, dir_shaders + "font.hlsl", async);

        // Color
        m_shaders[RendererShader::Color_V] = make_shared<RHI_Shader>(m_context, RHI_Vertex_Type::PosCol);
        m_shaders[RendererShader::Color_V]->Compile(RHI_Shader_Vertex, dir_shaders + "color.hlsl", async);
        m_shaders[RendererShader::Color_P] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Color_P]->Compile(RHI_Shader_Pixel, dir_shaders + "color.hlsl", async);

        // AMD FidelityFX
        {
            // Sharpening
            m_shaders[RendererShader::AMD_FidelityFX_CAS_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::AMD_FidelityFX_CAS_C]->Compile(RHI_Shader_Compute, dir_shaders + "AMD_FidelityFX_CAS.hlsl", async);

            // Mip generation
            m_shaders[RendererShader::AMD_FidelityFX_SPD_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::AMD_FidelityFX_SPD_C]->Compile(RHI_Shader_Compute, dir_shaders + "AMD_FidelityFX_SPD.hlsl", async);
            m_shaders[RendererShader::AMD_FidelityFX_SPD_LuminanceAntiflicker_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::AMD_FidelityFX_SPD_LuminanceAntiflicker_C]->AddDefine("LUMINANCE_ANTIFLICKER");
            m_shaders[RendererShader::AMD_FidelityFX_SPD_LuminanceAntiflicker_C]->Compile(RHI_Shader_Compute, dir_shaders + "AMD_FidelityFX_SPD.hlsl", async);

            // Upsampling
            m_shaders[RendererShader::AMD_FidelityFX_FSR_Upsample_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::AMD_FidelityFX_FSR_Upsample_C]->AddDefine("UPSAMPLE");
            m_shaders[RendererShader::AMD_FidelityFX_FSR_Upsample_C]->Compile(RHI_Shader_Compute, dir_shaders + "AMD_FidelityFX_FSR.hlsl", async);
            m_shaders[RendererShader::AMD_FidelityFX_FSR_Sharpen_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::AMD_FidelityFX_FSR_Sharpen_C]->AddDefine("SHARPEN");
            m_shaders[RendererShader::AMD_FidelityFX_FSR_Sharpen_C]->Compile(RHI_Shader_Compute, dir_shaders + "AMD_FidelityFX_FSR.hlsl", async);
        }

        // Debug
        m_shaders[RendererShader::Debug_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Debug_C]->Compile(RHI_Shader_Compute, dir_shaders + "debug.hlsl", async);
    }

    void Renderer::CreateFonts()
    {
        // Get standard font directory
        const auto dir_font = m_resource_cache->GetResourceDirectory(ResourceDirectory::Fonts) + "/";

        // Load a font (used for performance metrics)
        m_font = make_unique<Font>(m_context, dir_font + "CalibriBold.ttf", 12, Vector4(0.8f, 0.8f, 0.8f, 1.0f));
    }

    void Renderer::CreateTextures()
    {
        // Get standard texture directory
        const string dir_texture = m_resource_cache->GetResourceDirectory(ResourceDirectory::Textures) + "/";

        m_tex_default_noise_normal = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Srv, "default_noise_normal");
        m_tex_default_noise_normal->LoadFromFile(dir_texture + "noise_normal.png");

        m_tex_default_noise_blue = static_pointer_cast<RHI_Texture>(make_shared<RHI_Texture2DArray>(m_context, RHI_Texture_Srv, "default_noise_blue"));
        m_tex_default_noise_blue->LoadFromFile(dir_texture + "noise_blue_0.png");

        m_tex_default_white = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Srv, "default_white");
        m_tex_default_white->LoadFromFile(dir_texture + "white.png");

        m_tex_default_black = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Srv, "default_black");
        m_tex_default_black->LoadFromFile(dir_texture + "black.png");

        m_tex_default_transparent = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Srv, "default_transparent");
        m_tex_default_transparent->LoadFromFile(dir_texture + "transparent.png");

        // Gizmo icons
        m_tex_gizmo_light_directional = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Srv, "default_icon_light_directional");
        m_tex_gizmo_light_directional->LoadFromFile(dir_texture + "sun.png");

        m_tex_gizmo_light_point = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Srv, "default_icon_light_point");
        m_tex_gizmo_light_point->LoadFromFile(dir_texture + "light_bulb.png");

        m_tex_gizmo_light_spot = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Srv, "default_icon_light_spot");
        m_tex_gizmo_light_spot->LoadFromFile(dir_texture + "flashlight.png");
    }
}
