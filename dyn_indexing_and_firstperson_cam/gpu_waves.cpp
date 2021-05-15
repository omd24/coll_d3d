#include "gpu_waves.h"
#include "headers/utils.h"

using namespace DirectX;

GpuWaves *
GpuWaves_Init (BYTE * memory, ID3D12GraphicsCommandList* cmdlist, ID3D12Device * dev, int m, int n, float dx, float dt, float speed, float damping) {

    GpuWaves * ret = nullptr;
    ret = reinterpret_cast<GpuWaves *>(memory);
    ret->nrow = m;
    ret->ncol = n;
    ret->nvtx = m * n;
    ret->ntri = (m - 1) * (n - 1) * 2;

    ret->time_step = dt;
    ret->spatial_step = dx;

    float d = damping * dt + 2.0f;
    float e = (speed * speed) * (dt * dt) / (dx * dx);
    ret->k[0] = (damping * dt - 2.0f) / d;
    ret->k[1] = (4.0f - 8.0f * e) / d;
    ret->k[2] = (2.0f * e) / d;

    ret->device = dev;
    //
    // build resources
    //
#pragma region Build GpuWaves Resources
    D3D12_RESOURCE_DESC tex_desc = {};
    tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Alignment = 0;
    tex_desc.Width = ret->ncol;
    tex_desc.Height = ret->nrow;
    tex_desc.DepthOrArraySize = 1;
    tex_desc.MipLevels = 1;
    tex_desc.Format = DXGI_FORMAT_R32_FLOAT;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.SampleDesc.Quality = 0;
    tex_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    tex_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heap_props_default = {};
    heap_props_default.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_props_default.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props_default.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_props_default.CreationNodeMask = 1;
    heap_props_default.VisibleNodeMask = 1;

    ret->device->CreateCommittedResource(
        &heap_props_default, D3D12_HEAP_FLAG_NONE,
        &tex_desc, D3D12_RESOURCE_STATE_COMMON, NULL, IID_PPV_ARGS(&ret->prev_sol)
    );
    ret->device->CreateCommittedResource(
        &heap_props_default, D3D12_HEAP_FLAG_NONE,
        &tex_desc, D3D12_RESOURCE_STATE_COMMON, NULL, IID_PPV_ARGS(&ret->curr_sol)
    );
    ret->device->CreateCommittedResource(
        &heap_props_default, D3D12_HEAP_FLAG_NONE,
        &tex_desc, D3D12_RESOURCE_STATE_COMMON, NULL, IID_PPV_ARGS(&ret->next_sol)
    );

    //
    // In order to copy cpu memory data to default buffer we need an intermediate upload heap
    //
    UINT num_2dsubresources = tex_desc.DepthOrArraySize * tex_desc.MipLevels;
    UINT64 upload_buffer_size = get_required_intermediate_size(ret->curr_sol, 0, num_2dsubresources);
    D3D12_RESOURCE_DESC buf_desc = {};
    buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf_desc.Alignment = 0;
    buf_desc.Width = upload_buffer_size;
    buf_desc.Height = 1;
    buf_desc.DepthOrArraySize = 1;
    buf_desc.MipLevels = 1;
    buf_desc.Format = DXGI_FORMAT_UNKNOWN;
    buf_desc.SampleDesc.Count = 1;
    buf_desc.SampleDesc.Quality = 0;
    buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buf_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heap_props_upload = {};
    heap_props_upload.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap_props_upload.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props_upload.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_props_upload.CreationNodeMask = 1;
    heap_props_upload.VisibleNodeMask = 1;
    ret->device->CreateCommittedResource(
        &heap_props_upload, D3D12_HEAP_FLAG_NONE,
        &buf_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&ret->prev_upload_buffer)
    );
    ret->device->CreateCommittedResource(
        &heap_props_upload, D3D12_HEAP_FLAG_NONE,
        &buf_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&ret->curr_upload_buffer)
    );

    // Describe the data we want to copy to default buffer
    float * init_data = (float *)calloc(ret->nrow * ret->ncol, sizeof(float));
    D3D12_SUBRESOURCE_DATA subresource_data = {};
    subresource_data.pData = init_data;
    subresource_data.RowPitch = ret->ncol * sizeof(float);
    subresource_data.SlicePitch = ret->nrow * subresource_data.RowPitch;

    //
    // Schedule to copy data to the default resource and change states
    // curr_sol should be GENERIC_READ so it can be read by vertex shader
    //
    resource_usage_transition(cmdlist, ret->prev_sol, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    update_subresources_heap(cmdlist, ret->prev_sol, ret->prev_upload_buffer, 0, 0, num_2dsubresources, &subresource_data);
    resource_usage_transition(cmdlist, ret->prev_sol, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    resource_usage_transition(cmdlist, ret->curr_sol, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    update_subresources_heap(cmdlist, ret->curr_sol, ret->curr_upload_buffer, 0, 0, num_2dsubresources, &subresource_data);
    resource_usage_transition(cmdlist, ret->curr_sol, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);

    resource_usage_transition(cmdlist, ret->next_sol, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ::free(init_data);
#pragma endregion
    return ret;
}
void
GpuWaves_BuildDescriptors (
    GpuWaves * wave,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_descriptor,
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_descriptor,
    UINT descriptor_size
) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
    uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Texture2D.MipSlice = 0;

    wave->device->CreateShaderResourceView(wave->prev_sol, &srv_desc, hcpu_descriptor);
    hcpu_descriptor.ptr += descriptor_size;
    wave->device->CreateShaderResourceView(wave->curr_sol, &srv_desc, hcpu_descriptor);
    hcpu_descriptor.ptr += descriptor_size;
    wave->device->CreateShaderResourceView(wave->next_sol, &srv_desc, hcpu_descriptor);

    hcpu_descriptor.ptr += descriptor_size;
    wave->device->CreateUnorderedAccessView(wave->prev_sol, NULL, &uav_desc, hcpu_descriptor);
    hcpu_descriptor.ptr += descriptor_size;
    wave->device->CreateUnorderedAccessView(wave->curr_sol, NULL, &uav_desc, hcpu_descriptor);
    hcpu_descriptor.ptr += descriptor_size;
    wave->device->CreateUnorderedAccessView(wave->next_sol, NULL, &uav_desc, hcpu_descriptor);

    // save references to the gpu descriptors
    wave->prev_sol_hgpu_srv = hgpu_descriptor;
    hgpu_descriptor.ptr += descriptor_size;
    wave->curr_sol_hgpu_srv = hgpu_descriptor;
    hgpu_descriptor.ptr += descriptor_size;
    wave->next_sol_hgpu_srv = hgpu_descriptor;

    hgpu_descriptor.ptr += descriptor_size;
    wave->prev_sol_hgpu_uav = hgpu_descriptor;
    hgpu_descriptor.ptr += descriptor_size;
    wave->curr_sol_hgpu_uav = hgpu_descriptor;
    hgpu_descriptor.ptr += descriptor_size;
    wave->next_sol_hgpu_uav = hgpu_descriptor;
}
void
GpuWaves_Update (
    GpuWaves * wave,
    ID3D12GraphicsCommandList * cmdlist,
    ID3D12RootSignature * root_sig,
    ID3D12PipelineState * pso,
    float dt
) {
    static float t = 0.0f;

    // Accumulate time.
    t += dt;

    cmdlist->SetPipelineState(pso);
    cmdlist->SetComputeRootSignature(root_sig);

    // Only update the simulation at the specified time step.
    if (t >= wave->time_step) {
        // set the update constants
        cmdlist->SetComputeRoot32BitConstants(0, 3, wave->k, 0);

        cmdlist->SetComputeRootDescriptorTable(1, wave->prev_sol_hgpu_uav);
        cmdlist->SetComputeRootDescriptorTable(2, wave->curr_sol_hgpu_uav);
        cmdlist->SetComputeRootDescriptorTable(3, wave->next_sol_hgpu_uav);

        // how many groups do we need to dispatch
        // note that nrow and ncol should be divisible by 16
        // so there is no remainder
        UINT ngroup_x = wave->ncol / 16;
        UINT ngroup_y = wave->nrow / 16;

        resource_usage_transition(cmdlist, wave->curr_sol, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdlist->Dispatch(ngroup_x, ngroup_y, 1);

        //
        // Ping-pong buffers in preparation for next update
        // the prev sol is no longer needed and becomes the target of the next sol (in next update)
        // the curr sol becomes the prev sol and
        // the next sol becomes the curr sol
        //

        ID3D12Resource * resource_temp = wave->prev_sol;
        wave->prev_sol = wave->curr_sol;
        wave->curr_sol = wave->next_sol;
        wave->next_sol = resource_temp;

        D3D12_GPU_DESCRIPTOR_HANDLE srv_temp = wave->prev_sol_hgpu_srv;
        wave->prev_sol_hgpu_srv = wave->curr_sol_hgpu_srv;
        wave->curr_sol_hgpu_srv = wave->next_sol_hgpu_srv;
        wave->next_sol_hgpu_srv = srv_temp;

        D3D12_GPU_DESCRIPTOR_HANDLE uav_temp = wave->prev_sol_hgpu_uav;
        wave->prev_sol_hgpu_uav = wave->curr_sol_hgpu_uav;
        wave->curr_sol_hgpu_uav = wave->next_sol_hgpu_uav;
        wave->next_sol_hgpu_uav = uav_temp;

        t = 0.0f; // reset time

        // curr_sol should be GENERIC_READ so it can be read by vertex shader
        resource_usage_transition(cmdlist, wave->curr_sol, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
    }
}
void
GpuWaves_Disturb (
    GpuWaves * wave,
    ID3D12GraphicsCommandList * cmdlist,
    ID3D12RootSignature * root_sig,
    ID3D12PipelineState * pso,
    UINT i, UINT j, float magnitude
) {
    cmdlist->SetPipelineState(pso);
    cmdlist->SetComputeRootSignature(root_sig);

    // set distrub constants
    UINT disturb_index[2] = {j, i};
    cmdlist->SetComputeRoot32BitConstants(0, 1, &magnitude, 3);
    cmdlist->SetComputeRoot32BitConstants(0, 2, disturb_index, 4);
    cmdlist->SetComputeRootDescriptorTable(3, wave->curr_sol_hgpu_uav);

    // the curr sol is in GENERIC_READ state so it can be read by vertex shader
    // change it to UNORDERED_ACCESS for the compute shader.
    // Note that a uav can still be read in a compute shader
    resource_usage_transition(cmdlist, wave->curr_sol, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // One thread group kicks off one thread, which displaces the height of one
    // vertex and its neighbors.
    cmdlist->Dispatch(1, 1, 1);

    resource_usage_transition(cmdlist, wave->curr_sol, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
}
void
GpuWaves_Deinit (GpuWaves * wave) {
    wave->curr_upload_buffer->Release();
    wave->prev_upload_buffer->Release();
    wave->next_sol->Release();
    wave->curr_sol->Release();
    wave->prev_sol->Release();
}
