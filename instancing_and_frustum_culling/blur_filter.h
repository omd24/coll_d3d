#pragma once

#include "headers/common.h"

struct BlurFilter {
    ID3D12Device * device;

    UINT width;
    UINT height;

    int     blur_radius_max;
    int     blur_radius;
    size_t  weight_count;
    float * weights;        // pointer to heap allocated array of float

    DXGI_FORMAT format;

    D3D12_CPU_DESCRIPTOR_HANDLE blur0_cpu_srv;
    D3D12_CPU_DESCRIPTOR_HANDLE blur0_cpu_uav;

    D3D12_CPU_DESCRIPTOR_HANDLE blur1_cpu_srv;
    D3D12_CPU_DESCRIPTOR_HANDLE blur1_cpu_uav;

    D3D12_GPU_DESCRIPTOR_HANDLE blur0_gpu_srv;
    D3D12_GPU_DESCRIPTOR_HANDLE blur0_gpu_uav;

    D3D12_GPU_DESCRIPTOR_HANDLE blur1_gpu_srv;
    D3D12_GPU_DESCRIPTOR_HANDLE blur1_gpu_uav;

    // two for ping-ponging the textures
    ID3D12Resource * blur_map0;
    ID3D12Resource * blur_map1;

    bool new_resources_flag;
};

size_t
BlurFilter_CalculateRequiredSize ();

BlurFilter *
BlurFilter_Init (BYTE * memory, ID3D12Device * dev, UINT w, UINT h, DXGI_FORMAT format);

void
BlurFilter_Deinit (BlurFilter * filter);

void
BlurFilter_CreateDescriptors (
    BlurFilter * filter,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_descriptor,
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_descriptor,
    UINT descriptor_size
);

bool
BlurFilter_Resize (BlurFilter * filter, UINT w, UINT h);

/*
    Blurs the input texture, blur_count times
*/
void
BlurFilter_Execute (
    BlurFilter * filter,
    ID3D12GraphicsCommandList * cmdlist,
    ID3D12RootSignature * root_sig,
    ID3D12PipelineState * hor_blur_pso,
    ID3D12PipelineState * ver_blur_pso,
    ID3D12Resource * input_tex,
    UINT blur_count
);
