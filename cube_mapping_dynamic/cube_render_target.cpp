#include "cube_render_target.h"

static void
create_descriptors_internal (CubeRenderTarget * rt) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = rt->format;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srv_desc.TextureCube.MostDetailedMip = 0;
    srv_desc.TextureCube.MipLevels = 1;
    srv_desc.TextureCube.ResourceMinLODClamp = 0.0f;

    rt->device->CreateShaderResourceView(rt->cubemap, &srv_desc, rt->hcpu_srv);

    for (int i = 0; i < 6; ++i) {
        // NOTE(omid): use texture-2d-array for cubemap RTVs
        D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtv_desc.Format = rt->format;
        rtv_desc.Texture2DArray.MipSlice = 0;
        rtv_desc.Texture2DArray.PlaneSlice = 0;

        // render target to the ith element of texture-2d-array
        rtv_desc.Texture2DArray.FirstArraySlice = i;

        // only view one element of the array
        rtv_desc.Texture2DArray.ArraySize = 1;

        // create RTV to ith face
        rt->device->CreateRenderTargetView(rt->cubemap, &rtv_desc, rt->hcpu_rtv[i]);
    }
}
static void
create_resources_internal (CubeRenderTarget * rt) {
    // from http://www.d3dcoder.net/d3d12.htm
    // Note, compressed formats cannot be used for UAV.  We get error like:
    // ERROR: ID3D11Device::CreateTexture2D: The format (0x4d, BC3_UNORM) 
    // cannot be bound as an UnorderedAccessView, or cast to a format that
    // could be bound as an UnorderedAccessView.  Therefore this format 
    // does not support D3D11_BIND_UNORDERED_ACCESS.

    D3D12_RESOURCE_DESC tex_desc = {};
    tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Alignment = 0;
    tex_desc.Width = rt->width;
    tex_desc.Height = rt->height;
    tex_desc.DepthOrArraySize = 6;
    tex_desc.MipLevels = 1;
    tex_desc.Format = rt->format;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.SampleDesc.Quality = 0;
    tex_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    tex_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heap_def = {};
    heap_def.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_def.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_def.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_def.CreationNodeMask = 1;
    heap_def.VisibleNodeMask = 1;

    // -- specify clear color to suppress warning EXECUTION WARNING #820
    // nagging about ID3D12CommandList::ClearRenderTargetView: The application did not pass any clear value to resource creation.
    D3D12_CLEAR_VALUE clear_color = {};
    memcpy(clear_color.Color, rt->initial_clear_color, sizeof(rt->initial_clear_color));
    clear_color.Format = rt->format;
    rt->device->CreateCommittedResource(
        &heap_def, D3D12_HEAP_FLAG_NONE,
        &tex_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        &clear_color, IID_PPV_ARGS(&rt->cubemap)
    );
}
void
CubeRenderTarget_Init (CubeRenderTarget * rt, ID3D12Device * dev, UINT w, UINT h, DXGI_FORMAT format, float clear_color []) {
    _ASSERT_EXPR(rt, _T("Invalid Render target ptr"));
    rt->device = dev;
    rt->width = w;
    rt->height = h;
    rt->format = format;

    rt->viewport = {0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
    rt->scissor_rect = {0, 0, (int)w, (int)h};

    memcpy(rt->initial_clear_color, clear_color, sizeof(rt->initial_clear_color));
    rt->initialized = true;

    create_resources_internal(rt);
}
void
CubeRenderTarget_Deinit (CubeRenderTarget * rt) {
    rt->cubemap->Release();
}
void
CubeRenderTarget_CreateDescriptors (
    CubeRenderTarget * rt,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_srv,
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_srv,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_rtvs[6]
) {
    if (rt->initialized) {
        rt->hcpu_srv = hcpu_srv;
        rt->hgpu_srv = hgpu_srv;
        for (int i = 0; i < 6; ++i)
            rt->hcpu_rtv[i] = hcpu_rtvs[i];

        create_descriptors_internal(rt);
    }
}
void
CubeRenderTarget_Resize (CubeRenderTarget * rt, UINT w, UINT h) {
    if (rt->initialized) {
        if ((rt->width != w) || (rt->height != h)) {
            rt->width = w;
            rt->height = h;

            rt->cubemap->Release();
            create_resources_internal(rt);

            // -- new resource so create new descriptors
            create_descriptors_internal(rt);
        }
    }
}
D3D12_CPU_DESCRIPTOR_HANDLE
CubeRenderTarget_GetRtv (CubeRenderTarget * rt, int face_index) {
    return rt->hcpu_rtv[face_index];
}

