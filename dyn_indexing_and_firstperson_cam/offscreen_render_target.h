#pragma once

#include "headers/common.h"

struct OffscreenRenderTarget {
    ID3D12Device * device;

    UINT width;
    UINT height;
    DXGI_FORMAT format;

    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_srv;
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_srv;
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_rtv;

    ID3D12Resource * texture;

    FLOAT initial_clear_color [4];

    bool initialized;
};

void
OffscreenRenderTarget_Init (OffscreenRenderTarget * out_ort, ID3D12Device * dev, UINT w, UINT h, DXGI_FORMAT format, float clear_color []);

void
OffscreenRenderTarget_Deinit (OffscreenRenderTarget * ort);

void
OffscreenRenderTarget_CreateDescriptors (
    OffscreenRenderTarget * rt,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_srv,
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_srv,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_rtv
);

void
OffscreenRenderTarget_Resize (OffscreenRenderTarget * ort, UINT w, UINT h);


