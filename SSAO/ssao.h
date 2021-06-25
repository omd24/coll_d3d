#pragma once

#include "headers/common.h"
#include "headers/utils.h"

struct SSAO {
    ID3D12Device * device;

    DXGI_FORMAT ambient_map_format;
    DXGI_FORMAT normal_map_format;

    int max_blur_radius;
    size_t  weight_count;

    UINT render_target_width;
    UINT render_target_height;

    DirectX::XMFLOAT4 offsets[14];

    D3D12_VIEWPORT viewport;
    D3D12_RECT scissor_rect;

    D3D12_CPU_DESCRIPTOR_HANDLE normal_map_cpu_srv;
    D3D12_GPU_DESCRIPTOR_HANDLE normal_map_gpu_srv;
    D3D12_CPU_DESCRIPTOR_HANDLE normal_map_cpu_rtv;

    D3D12_CPU_DESCRIPTOR_HANDLE depth_map_cpu_srv;
    D3D12_GPU_DESCRIPTOR_HANDLE depth_map_gpu_srv;

    D3D12_CPU_DESCRIPTOR_HANDLE random_vector_map_cpu_srv;
    D3D12_GPU_DESCRIPTOR_HANDLE random_vector_map_gpu_srv;

    // need two sets of cpu/gpu handles for ping-ponging during blur
    D3D12_CPU_DESCRIPTOR_HANDLE ambient_map0_cpu_srv;
    D3D12_GPU_DESCRIPTOR_HANDLE ambient_map0_gpu_srv;
    D3D12_CPU_DESCRIPTOR_HANDLE ambient_map0_cpu_rtv;

    D3D12_CPU_DESCRIPTOR_HANDLE ambient_map1_cpu_srv;
    D3D12_GPU_DESCRIPTOR_HANDLE ambient_map1_gpu_srv;
    D3D12_CPU_DESCRIPTOR_HANDLE ambient_map1_cpu_rtv;

    ID3D12Resource * random_vector_map;
    ID3D12Resource * random_vector_map_uploader;
    ID3D12Resource * normal_map;
    ID3D12Resource * ambient_map0;
    ID3D12Resource * ambient_map1;

    ID3D12PipelineState * ssao_pso;
    ID3D12PipelineState * blur_pso;

    ID3D12RootSignature * ssao_root_sig;

    bool initialized;
};

void
SSAO_Init (SSAO * ssao, ID3D12Device * dev, ID3D12GraphicsCommandList * cmdlist, UINT w, UINT h);

void
SSAO_Deinit (SSAO * ssao);

int
SSAO_CalculateWeightsCount (SSAO * ssao, float sigma);

void
SSAO_CalculateGaussWeights (SSAO * ssao, float sigma, float out_weights []);

void
SSAO_CreateDescriptors (
    SSAO * ssao,
    ID3D12Resource * depth_stencil_buffer,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_srv,
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_srv,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_rtv,
    UINT cbv_srv_uav_descriptor_size,
    UINT rtv_descriptor_size
);
void
SSAO_RecreateDescriptors (
    SSAO * ssao,
    ID3D12Resource * depth_stencil_buffer
);

void
SSAO_SetPSOs (SSAO * ssao, ID3D12PipelineState * ssao_pso, ID3D12PipelineState * blur_pso);

void
SSAO_Resize (SSAO * ssao, UINT w, UINT h);

///<summary>
/// changes render target to the ambient render target and draws a fullscreen
/// quad to kick off the pixel shader to compute the ambient map.
/// Still keep the main depth buffer binded to the pipeline,
/// but depth buffer read/writes are disabled,
/// as we do not need the depth buffer computing the ambient map.
///</summary>
void
SSAO_ComputeSSAO (
    ID3D12GraphicsCommandList * cmdlist, 
    FrameResource * curr_frame, 
    int blur_count
);

