#pragma once

#include "headers/common.h"

struct SobelFilter {
    ID3D12Device * device;

    UINT width;
    UINT height;
    DXGI_FORMAT format;

    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_srv;
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_uav;

    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_srv;
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_uav;

    ID3D12Resource * output;
    bool new_resource_flag;
};

void
SobelFilter_Init (SobelFilter * filter, ID3D12Device * dev, UINT w, UINT h, DXGI_FORMAT format);

void
SobelFilter_Deinit (SobelFilter * filter);

void
SobelFilter_CreateDescriptors (
    SobelFilter * filter,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_descriptor,
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_descriptor,
    UINT descriptor_size
);

void
SobelFilter_Resize (SobelFilter * filter, UINT w, UINT h);

void
SobelFilter_Execute (
    SobelFilter * filter,
    ID3D12GraphicsCommandList * cmdlist,
    ID3D12RootSignature * root_sig,
    ID3D12PipelineState * pso,
    D3D12_GPU_DESCRIPTOR_HANDLE input
);
