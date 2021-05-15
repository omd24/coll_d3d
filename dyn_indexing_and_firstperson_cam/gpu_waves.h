#pragma once
#include "headers/common.h"
#include <DirectXMath.h>

struct GpuWaves {
    int nrow;
    int ncol;
    int nvtx;   // # of vertex
    int ntri;   // # of triangle

    float k[3];    // simulation constants

    float time_step, spatial_step;

    ID3D12Device * device;

    D3D12_GPU_DESCRIPTOR_HANDLE prev_sol_hgpu_srv;
    D3D12_GPU_DESCRIPTOR_HANDLE curr_sol_hgpu_srv;
    D3D12_GPU_DESCRIPTOR_HANDLE next_sol_hgpu_srv;

    D3D12_GPU_DESCRIPTOR_HANDLE prev_sol_hgpu_uav;
    D3D12_GPU_DESCRIPTOR_HANDLE curr_sol_hgpu_uav;
    D3D12_GPU_DESCRIPTOR_HANDLE next_sol_hgpu_uav;

    // extra resources for ping-ponging the textures
    ID3D12Resource * prev_sol;
    ID3D12Resource * curr_sol;
    ID3D12Resource * next_sol;

    ID3D12Resource * prev_upload_buffer;
    ID3D12Resource * curr_upload_buffer;
};
GpuWaves *
GpuWaves_Init (BYTE * memory, ID3D12GraphicsCommandList* cmdlist, ID3D12Device * dev, int m, int n, float dx, float dt, float speed, float damping);
void
GpuWaves_BuildDescriptors (
    GpuWaves * wave,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_descriptor,
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_descriptor,
    UINT descriptor_size
);
void
GpuWaves_Update (
    GpuWaves * wave,
    ID3D12GraphicsCommandList * cmdlist,
    ID3D12RootSignature * root_sig,
    ID3D12PipelineState * pso,
    float dt
);
void
GpuWaves_Disturb (
    GpuWaves * wave,
    ID3D12GraphicsCommandList * cmdlist,
    ID3D12RootSignature * root_sig,
    ID3D12PipelineState * pso,
    UINT i, UINT j, float magnitude
);
void
GpuWaves_Deinit (GpuWaves * wave);

