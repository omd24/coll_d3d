#include "sobel_filter.h"
#include "headers/utils.h"

static void
create_descriptors_internal (SobelFilter * filter) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = filter->format;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
    uav_desc.Format = filter->format;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Texture2D.MipSlice = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    filter->device->CreateShaderResourceView(filter->output, &srv_desc, filter->hcpu_srv);
    filter->device->CreateUnorderedAccessView(filter->output, NULL, &uav_desc, filter->hcpu_uav);
}
static void
create_resources_internal (SobelFilter * filter) {
    D3D12_RESOURCE_DESC tex_desc = {};
    tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Alignment = 0;
    tex_desc.Width = filter->width;
    tex_desc.Height = filter->height;
    tex_desc.DepthOrArraySize = 1;
    tex_desc.MipLevels = 1;
    tex_desc.Format = filter->format;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.SampleDesc.Quality = 0;
    tex_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    tex_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heap_def = {};
    heap_def.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_def.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_def.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_def.CreationNodeMask = 1;
    heap_def.VisibleNodeMask = 1;

    filter->device->CreateCommittedResource(
        &heap_def, D3D12_HEAP_FLAG_NONE, &tex_desc,
        D3D12_RESOURCE_STATE_COMMON, NULL, IID_PPV_ARGS(&filter->output)
    );
    filter->new_resource_flag = true;
}

void
SobelFilter_Init (SobelFilter * filter, ID3D12Device * dev, UINT w, UINT h, DXGI_FORMAT format) {
    _ASSERT_EXPR(filter, _T("Invalid Render target ptr"));
    filter->device = dev;
    filter->width = w;
    filter->height = h;
    filter->format = format;
    filter->new_resource_flag = false;

    create_resources_internal(filter);
}
void
SobelFilter_Deinit (SobelFilter * filter) {
    filter->output->Release();
}
void
SobelFilter_CreateDescriptors (
    SobelFilter * filter,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_descriptor,
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_descriptor,
    UINT descriptor_size
) {
    filter->hcpu_srv = hcpu_descriptor;
    filter->hcpu_uav = hcpu_descriptor;
    filter->hcpu_uav.ptr = hcpu_descriptor.ptr + descriptor_size;

    filter->hgpu_srv = hgpu_descriptor;
    filter->hgpu_uav = hgpu_descriptor;
    filter->hgpu_uav.ptr = hgpu_descriptor.ptr + descriptor_size;

    create_descriptors_internal(filter);
}
void
SobelFilter_Resize (SobelFilter * filter, UINT w, UINT h) {
    if ((filter->width != w) || (filter->height != h)) {
        filter->width = w;
        filter->height = h;

        filter->output->Release();
        create_resources_internal(filter);
        create_descriptors_internal(filter);
    }
}
void
SobelFilter_Execute (
    SobelFilter * filter,
    ID3D12GraphicsCommandList * cmdlist,
    ID3D12RootSignature * root_sig,
    ID3D12PipelineState * pso,
    D3D12_GPU_DESCRIPTOR_HANDLE input
) {
    cmdlist->SetComputeRootSignature(root_sig);
    cmdlist->SetPipelineState(pso);

    cmdlist->SetComputeRootDescriptorTable(0, input);
    cmdlist->SetComputeRootDescriptorTable(2, filter->hgpu_uav);

    if (filter->new_resource_flag)
        resource_usage_transition(cmdlist, filter->output, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    else
        resource_usage_transition(cmdlist, filter->output, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // how many groups do we need to dispatch to cover image where each group covers 16x16 pixels
    UINT num_groups_x = (UINT)ceilf(filter->width / 16.0f);
    UINT num_groups_y = (UINT)ceilf(filter->height / 16.0f);
    cmdlist->Dispatch(num_groups_x, num_groups_y, 1);

    resource_usage_transition(cmdlist, filter->output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);

    filter->new_resource_flag = false;
}
