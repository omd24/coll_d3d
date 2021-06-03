#pragma once

#include "headers/common.h"

enum CUBEMAP_FACE {
    CUBE_FACE_POSITIVE_X = 0,
    CUBE_FACE_NEGATIVE_X = 1,
    CUBE_FACE_POSITIVE_Y = 2,
    CUBE_FACE_NEGATIVE_Y = 3,
    CUBE_FACE_POSITIVE_Z = 4,
    CUBE_FACE_NEGATIVE_Z = 5,
};
struct CubeRenderTarget {
    ID3D12Device * device;

    D3D12_VIEWPORT viewport;
    D3D12_RECT scissor_rect;

    UINT width;
    UINT height;
    DXGI_FORMAT format;

    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_srv;
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_srv;
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_rtv [6];

    ID3D12Resource * cupemap;

    FLOAT initial_clear_color [4];

    bool initialized;
};
void
CubeRenderTarget_Init (CubeRenderTarget * rt, ID3D12Device * dev, UINT w, UINT h, DXGI_FORMAT format, float clear_color []);

void
CubeRenderTarget_Deinit (CubeRenderTarget * rt);

void
CubeRenderTarget_CreateDescriptors (
    CubeRenderTarget * rt,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_srv,
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_srv,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_rtvs[6]
);

void
CubeRenderTarget_Resize (CubeRenderTarget * rt, UINT w, UINT h);

D3D12_CPU_DESCRIPTOR_HANDLE
CubeRenderTarget_GetRtv (CubeRenderTarget * rt, int face_index);

