#pragma once

#include "headers/common.h"

struct ShadowMap {
    ID3D12Device * device;

    D3D12_VIEWPORT viewport;
    D3D12_RECT scissor_rect;

    UINT width;
    UINT height;
    DXGI_FORMAT format;

    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_srv;
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_srv;
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_dsv;

    ID3D12Resource * shadow_map;

    bool initialized;
};

void
ShadowMap_Init (ShadowMap * smap, ID3D12Device * dev, UINT w, UINT h, DXGI_FORMAT format);

void
ShadowMap_Deinit (ShadowMap * smap);

void
ShadowMap_CreateDescriptors (
    ShadowMap * smap,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_srv,
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_srv,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_rtv
);

void
ShadowMap_Resize (ShadowMap * smap, UINT w, UINT h);

