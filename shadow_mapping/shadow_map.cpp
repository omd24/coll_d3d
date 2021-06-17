#include "shadow_map.h"

static void
create_descriptors_internal (ShadowMap * smap) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
    srv_desc.Texture2D.PlaneSlice = 0;

    smap->device->CreateShaderResourceView(smap->shadow_map, &srv_desc, smap->hcpu_srv);

	// Create DSV to resource so we can render to the shadow map.
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc; 
    dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsv_desc.Texture2D.MipSlice = 0;
    smap->device->CreateDepthStencilView(smap->shadow_map, &dsv_desc, smap->hcpu_dsv);
}
static void
create_resources_internal (ShadowMap * smap) {
    D3D12_RESOURCE_DESC tex_desc = {};
    tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Alignment = 0;
    tex_desc.Width = smap->width;
    tex_desc.Height = smap->height;
    tex_desc.DepthOrArraySize = 1;
    tex_desc.MipLevels = 1;
    tex_desc.Format = smap->format;
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

    D3D12_CLEAR_VALUE opt_clear = {};
    opt_clear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    opt_clear.DepthStencil.Depth = 1.0f;
    opt_clear.DepthStencil.Stencil = 0;

    smap->device->CreateCommittedResource(
        &heap_def, D3D12_HEAP_FLAG_NONE,
        &tex_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        &opt_clear, IID_PPV_ARGS(&smap->shadow_map)
    );
}
void
ShadowMap_Init (ShadowMap * smap, ID3D12Device * dev, UINT w, UINT h, DXGI_FORMAT format) {
    _ASSERT_EXPR(smap, _T("Invalid Render target ptr"));
    UNREFERENCED_PARAMETER(format);

    smap->device = dev;
    smap->width = w;
    smap->height = h;
    smap->format = DXGI_FORMAT_R24G8_TYPELESS;
    smap->viewport = {0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
    smap->scissor_rect = {0, 0, (int)w, (int)h};

    smap->initialized = true;
    create_resources_internal(smap);
}

void
ShadowMap_Deinit (ShadowMap * smap) {
    smap->shadow_map->Release();
}

void
ShadowMap_CreateDescriptors (
    ShadowMap * smap,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_srv,
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_srv,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_dsv
) {
    if (smap->initialized) {
        smap->hcpu_srv = hcpu_srv;
        smap->hgpu_srv = hgpu_srv;
        smap->hcpu_dsv = hcpu_dsv;

        create_descriptors_internal(smap);
    }
}

void
ShadowMap_Resize (ShadowMap * smap, UINT w, UINT h) {
    if (smap->initialized) {
        if ((smap->width != w) || (smap->height != h)) {
            smap->width = w;
            smap->height = h;

            smap->shadow_map->Release();
            create_resources_internal(smap);

            create_descriptors_internal(smap);
        }
    }
}

