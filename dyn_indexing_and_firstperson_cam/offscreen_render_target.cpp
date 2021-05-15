#include "offscreen_render_target.h"

static void
create_descriptors_internal (OffscreenRenderTarget * ort) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = ort->format;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;

    ort->device->CreateShaderResourceView(ort->texture, &srv_desc, ort->hcpu_srv);

    ort->device->CreateRenderTargetView(ort->texture, nullptr, ort->hcpu_rtv);
}
static void
create_resources_internal (OffscreenRenderTarget * ort) {
    D3D12_RESOURCE_DESC tex_desc = {};
    tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Alignment = 0;
    tex_desc.Width = ort->width;
    tex_desc.Height = ort->height;
    tex_desc.DepthOrArraySize = 1;
    tex_desc.MipLevels = 1;
    tex_desc.Format = ort->format;
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
    memcpy(clear_color.Color, ort->initial_clear_color, sizeof(ort->initial_clear_color));
    clear_color.Format = ort->format;
    ort->device->CreateCommittedResource(&heap_def, D3D12_HEAP_FLAG_NONE, &tex_desc, D3D12_RESOURCE_STATE_GENERIC_READ, &clear_color, IID_PPV_ARGS(&ort->texture));
}
void
OffscreenRenderTarget_Init (OffscreenRenderTarget * out_ort, ID3D12Device * dev, UINT w, UINT h, DXGI_FORMAT format, float clear_color []) {
    _ASSERT_EXPR(out_ort, _T("Invalid Render target ptr"));
    out_ort->device = dev;
    out_ort->width = w;
    out_ort->height = h;
    out_ort->format = format;
    memcpy(out_ort->initial_clear_color, clear_color, sizeof(out_ort->initial_clear_color));
    out_ort->initialized = true;
    create_resources_internal(out_ort);
}
void
OffscreenRenderTarget_Deinit (OffscreenRenderTarget * ort) {
    ort->texture->Release();
}
void
OffscreenRenderTarget_CreateDescriptors (
    OffscreenRenderTarget * ort,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_srv,
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_srv,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_rtv
) {
    if (ort->initialized) {
        ort->hcpu_srv = hcpu_srv;
        ort->hgpu_srv = hgpu_srv;
        ort->hcpu_rtv = hcpu_rtv;

        create_descriptors_internal(ort);
    }
}
void
OffscreenRenderTarget_Resize (OffscreenRenderTarget * ort, UINT w, UINT h) {
    if (ort->initialized) {
        if ((ort->width != w) || (ort->height != h)) {
            ort->width = w;
            ort->height = h;

            ort->texture->Release();
            create_resources_internal(ort);

            create_descriptors_internal(ort);
        }
    }
}
