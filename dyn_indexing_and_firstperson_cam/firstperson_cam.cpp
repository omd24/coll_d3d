#include "headers/common.h"

#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <dxgidebug.h>

#include <dxcapi.h>

#include "headers/utils.h"
#include "headers/game_timer.h"
#include "headers/dds_loader.h"

#include "offscreen_render_target.h"
#include "gpu_waves.h"
#include "blur_filter.h"
#include "sobel_filter.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_dx12.h>

#include <time.h>

#include "camera.h"

#if !defined(NDEBUG) && !defined(_DEBUG)
#error "Define at least one."
#elif defined(NDEBUG) && defined(_DEBUG)
#error "Define at most one."
#endif

#if defined(_DEBUG)
#define ENABLE_DEBUG_LAYER 1
#else
#define ENABLE_DEBUG_LAYER 0
#endif

#define ENABLE_DEARIMGUI

#pragma warning (disable: 28182)    // pointer can be NULL.
#pragma warning (disable: 6011)     // dereferencing a potentially null pointer
#pragma warning (disable: 26495)    // not initializing struct members

#define NUM_BACKBUFFERS         2
#define NUM_QUEUING_FRAMES      3

enum RENDER_LAYER : int {
    LAYER_OPAQUE = 0,
    LAYER_TRANSPARENT = 1,
    LAYER_ALPHATESTED = 2,
    LAYER_ALPHATESTED_TREESPRITES = 3,
    LAYER_GPU_WAVES_RENDER = 4,
    LAYER_GPU_WAVES_DISTURB = 5,
    LAYER_GPU_WAVES_UPDATE = 6,
    LAYER_HOR_BLUR = 7,
    LAYER_VER_BLUR = 8,
    LAYER_SOBEL = 9,
    LAYER_COMPISITE = 10,

    _COUNT_RENDERCOMPUTE_LAYER
};
enum ALL_RENDERITEMS {
    RITEM_WATER = 0,
    RITEM_GRID = 1,
    RITEM_BOX = 2,
    RITEM_TREESPRITE = 3,

    _COUNT_RENDERITEM
};
enum SHADERS_CODE {
    SHADER_DEFAULT_VS = 0,
    SHADER_WAVE_VS = 1,
    SHADER_OPAQUE_PS = 2,
    SHADER_ALPHATEST_PS = 3,
    SHADER_TREE_VS = 4,
    SHADER_TREE_GS = 5,
    SHADER_TREE_PS = 6,
    SHADER_UPDATE_WAVE_CS = 7,
    SHADER_DISTURB_WAVE_CS = 8,
    SHADER_HOR_BLUR_CS = 9,
    SHADER_VER_BLUR_CS = 10,
    SHADER_COMPOSITE_VS = 11,
    SHADER_COMPOSITE_PS = 12,
    SHADER_SOBEL_CS = 13,

    _COUNT_SHADERS
};
enum GEOM_INDEX {
    GEOM_BOX = 0,
    GEOM_WATER = 1,
    GEOM_GRID = 2,
    GEOM_TREESPRITE = 3,

    _COUNT_GEOM
};
enum MAT_INDEX {
    MAT_WOOD_CRATE = 0,
    MAT_GRASS = 1,
    MAT_WATER = 2,
    MAT_WIRED_CRATE = 3,
    MAT_TREE_SPRITE = 4,

    _COUNT_MATERIAL
};
enum TEX_INDEX {
    TEX_CRATE01 = 0,
    TEX_WATER = 1,
    TEX_GRASS = 2,
    TEX_WIREFENCE = 3,
    TEX_TREEARRAY = 4,

    _COUNT_TEX
};
enum SAMPLER_INDEX {
    SAMPLER_POINT_WRAP = 0,
    SAMPLER_POINT_CLAMP = 1,
    SAMPLER_LINEAR_WRAP = 2,
    SAMPLER_LINEAR_CLAMP = 3,
    SAMPLER_ANISOTROPIC_WRAP = 4,
    SAMPLER_ANISOTROPIC_CLAMP = 5,

    _COUNT_SAMPLER
};
struct SceneContext {

    // light (sun) settings
    float sun_theta;
    float sun_phi;

    // mouse position
    POINT mouse;

    // display-related data
    UINT width;
    UINT height;
    float aspect_ratio;
};

Camera * global_camera;
GameTimer global_timer;
bool global_paused;
bool global_resizing;
bool global_mouse_active;
SceneContext global_scene_ctx;
BlurFilter * global_blur_filter;

SobelFilter global_sobel_filter = {};
OffscreenRenderTarget global_offscreen_rendertarget = {};

#if defined(ENABLE_DEARIMGUI)
bool global_imgui_enabled = true;
#else
bool global_imgui_enabled = false;
#endif // defined(ENABLE_DEARIMGUI)


struct RenderItemArray {
    RenderItem  ritems[_COUNT_RENDERITEM];
    uint32_t    size;
};
struct D3DRenderContext {

    bool msaa4x_state;
    UINT msaa4x_quality;

    // Used formats
    struct {
        DXGI_FORMAT backbuffer_format;
        DXGI_FORMAT depthstencil_format;
    };

    // Pipeline stuff
    D3D12_VIEWPORT                  viewport;
    D3D12_RECT                      scissor_rect;
    //IDXGISwapChain3 *               swapchain3;
    IDXGISwapChain *                swapchain;
    ID3D12Device *                  device;
    ID3D12RootSignature *           root_signature;
    ID3D12RootSignature *           root_signature_waves;
    ID3D12RootSignature *           root_signature_postprocessing_blur;
    ID3D12RootSignature *           root_signature_postprocessing_sobel;
    ID3D12PipelineState *           psos[_COUNT_RENDERCOMPUTE_LAYER];

    // Command objects
    ID3D12CommandQueue *            cmd_queue;
    ID3D12CommandAllocator *        direct_cmd_list_alloc;
    ID3D12GraphicsCommandList *     direct_cmd_list;

    UINT                            rtv_descriptor_size;
    UINT                            cbv_srv_uav_descriptor_size;

    ID3D12DescriptorHeap *          rtv_heap;
    ID3D12DescriptorHeap *          dsv_heap;
    ID3D12DescriptorHeap *          srv_heap;

    PassConstants                   main_pass_constants;
    UINT                            pass_cbv_offset;

    // List of all the render items.
    RenderItemArray                 all_ritems;
    // Render items divided by PSO.
    RenderItemArray                 opaque_ritems;
    RenderItemArray                 transparent_ritems;
    RenderItemArray                 alphatested_ritems;
    RenderItemArray                 alphatested_treesprites_ritems;
    RenderItemArray                 gpu_waves_rtimes;

    MeshGeometry                    geom[_COUNT_GEOM];

    // Synchronization stuff
    UINT                            frame_index;
    HANDLE                          fence_event;
    ID3D12Fence *                   fence;
    FrameResource                   frame_resources[NUM_QUEUING_FRAMES];
    UINT64                          main_current_fence;

    // Each swapchain backbuffer needs a render target
    ID3D12Resource *                render_targets[NUM_BACKBUFFERS];
    UINT                            backbuffer_index;

    ID3D12Resource *                depth_stencil_buffer;

    Material                        materials[_COUNT_MATERIAL];
    Texture                         textures[_COUNT_TEX];
    IDxcBlob *                      shaders[_COUNT_SHADERS];
};
static void
load_texture (
    ID3D12Device * device,
    ID3D12GraphicsCommandList * cmd_list,
    wchar_t const * tex_path,
    Texture * out_texture
) {

    uint8_t * ddsData;
    D3D12_SUBRESOURCE_DATA * subresources;
    UINT n_subresources = 0;

    LoadDDSTextureFromFile(device, tex_path, &out_texture->resource, &ddsData, &subresources, &n_subresources);

    UINT64 upload_buffer_size = get_required_intermediate_size(out_texture->resource, 0,
                                                               n_subresources);

   // Create the GPU upload buffer.
    D3D12_HEAP_PROPERTIES heap_props = {};
    heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_props.CreationNodeMask = 1;
    heap_props.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = upload_buffer_size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    // TODO(omid): do we need to set 4x MSAA here? 
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    device->CreateCommittedResource(
        &heap_props,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&out_texture->upload_heap)
    );

    // Use Heap-allocating UpdateSubresources implementation for variable number of subresources (which is the case for textures).
    update_subresources_heap(
        cmd_list, out_texture->resource, out_texture->upload_heap,
        0, 0, n_subresources, subresources
    );

    resource_usage_transition(
        cmd_list, out_texture->resource,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );

    ::free(subresources);
    ::free(ddsData);
}
static void
create_materials (Material out_materials []) {
    strcpy_s(out_materials[MAT_GRASS].name, "grass");
    out_materials[MAT_GRASS].mat_cbuffer_index = 0;
    out_materials[MAT_GRASS].diffuse_srvheap_index = 0;
    out_materials[MAT_GRASS].diffuse_albedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    out_materials[MAT_GRASS].fresnel_r0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    out_materials[MAT_GRASS].roughness = 0.125f;
    out_materials[MAT_GRASS].mat_transform = Identity4x4();
    out_materials[MAT_GRASS].n_frames_dirty = NUM_QUEUING_FRAMES;

    strcpy_s(out_materials[MAT_WATER].name, "water");
    out_materials[MAT_WATER].mat_cbuffer_index = 1;
    out_materials[MAT_WATER].diffuse_srvheap_index = 1;
    out_materials[MAT_WATER].diffuse_albedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
    out_materials[MAT_WATER].fresnel_r0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    out_materials[MAT_WATER].roughness = 0.0f;
    out_materials[MAT_WATER].mat_transform = Identity4x4();
    out_materials[MAT_WATER].n_frames_dirty = NUM_QUEUING_FRAMES;

    strcpy_s(out_materials[MAT_WOOD_CRATE].name, "wood_crate");
    out_materials[MAT_WOOD_CRATE].mat_cbuffer_index = 2;
    out_materials[MAT_WOOD_CRATE].diffuse_srvheap_index = 2;
    out_materials[MAT_WOOD_CRATE].diffuse_albedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    out_materials[MAT_WOOD_CRATE].fresnel_r0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    out_materials[MAT_WOOD_CRATE].roughness = 0.2f;
    out_materials[MAT_WOOD_CRATE].mat_transform = Identity4x4();
    out_materials[MAT_WOOD_CRATE].n_frames_dirty = NUM_QUEUING_FRAMES;

    strcpy_s(out_materials[MAT_WIRED_CRATE].name, "wired_crate");
    out_materials[MAT_WIRED_CRATE].mat_cbuffer_index = 3;
    out_materials[MAT_WIRED_CRATE].diffuse_srvheap_index = 3;
    out_materials[MAT_WIRED_CRATE].diffuse_albedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    out_materials[MAT_WIRED_CRATE].fresnel_r0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    out_materials[MAT_WIRED_CRATE].roughness = 0.2f;
    out_materials[MAT_WIRED_CRATE].mat_transform = Identity4x4();
    out_materials[MAT_WIRED_CRATE].n_frames_dirty = NUM_QUEUING_FRAMES;

    strcpy_s(out_materials[MAT_TREE_SPRITE].name, "tree_sprites");
    out_materials[MAT_TREE_SPRITE].mat_cbuffer_index = 4;
    out_materials[MAT_TREE_SPRITE].diffuse_srvheap_index = 4;
    out_materials[MAT_TREE_SPRITE].diffuse_albedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    out_materials[MAT_TREE_SPRITE].fresnel_r0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    out_materials[MAT_TREE_SPRITE].roughness = 0.125f;
    out_materials[MAT_TREE_SPRITE].mat_transform = Identity4x4();
    out_materials[MAT_TREE_SPRITE].n_frames_dirty = NUM_QUEUING_FRAMES;
}
static float
calc_hill_height (float x, float z) {
    return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}
static XMFLOAT3
calc_hill_normal (float x, float z) {
    // n = (-df/dx, 1, -df/dz)
    XMFLOAT3 n(
        -0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
        1.0f,
        -0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z)
    );

    XMVECTOR unit_normal = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n, unit_normal);

    return n;
}
static void
create_shape_geometry (D3DRenderContext * render_ctx) {

    // required sizes
    int nvtx = 24;
    int nidx = 36;

    Vertex *    vertices = (Vertex *)::malloc(sizeof(Vertex) * nvtx);
    uint16_t *  indices = (uint16_t *)::malloc(sizeof(uint16_t) * nidx);
    BYTE *      scratch = (BYTE *)::malloc(sizeof(GeomVertex) * nvtx + sizeof(uint16_t) * nidx);

    // box
    UINT bsz = sizeof(GeomVertex) * nvtx;
    GeomVertex *    box_vertices = reinterpret_cast<GeomVertex *>(scratch);
    uint16_t *      box_indices = reinterpret_cast<uint16_t *>(scratch + bsz);

    create_box(8.0f, 8.0f, 8.0f, box_vertices, box_indices);

    SubmeshGeometry box_submesh = {};
    box_submesh.index_count = nidx;
    box_submesh.start_index_location = 0;
    box_submesh.base_vertex_location = 0;

    UINT k = 0;
    for (size_t i = 0; i < nvtx; ++i, ++k) {
        vertices[k].position = box_vertices[i].Position;
        vertices[k].normal = box_vertices[i].Normal;
        vertices[k].texc = box_vertices[i].TexC;
    }

    // -- pack indices
    k = 0;
    for (size_t i = 0; i < nidx; ++i, ++k) {
        indices[k] = box_indices[i];
    }

    UINT vb_byte_size = nvtx * sizeof(Vertex);
    UINT ib_byte_size = nidx * sizeof(uint16_t);

    // -- Fill out geom
    D3DCreateBlob(vb_byte_size, &render_ctx->geom[GEOM_BOX].vb_cpu);
    if (vertices)
        CopyMemory(render_ctx->geom[GEOM_BOX].vb_cpu->GetBufferPointer(), vertices, vb_byte_size);

    D3DCreateBlob(ib_byte_size, &render_ctx->geom[GEOM_BOX].ib_cpu);
    if (indices)
        CopyMemory(render_ctx->geom[GEOM_BOX].ib_cpu->GetBufferPointer(), indices, ib_byte_size);

    create_default_buffer(render_ctx->device, render_ctx->direct_cmd_list, vertices, vb_byte_size, &render_ctx->geom[GEOM_BOX].vb_gpu, &render_ctx->geom[GEOM_BOX].vb_uploader);
    create_default_buffer(render_ctx->device, render_ctx->direct_cmd_list, indices, ib_byte_size, &render_ctx->geom[GEOM_BOX].ib_gpu, &render_ctx->geom[GEOM_BOX].ib_uploader);

    render_ctx->geom[GEOM_BOX].vb_byte_stide = sizeof(Vertex);
    render_ctx->geom[GEOM_BOX].vb_byte_size = vb_byte_size;
    render_ctx->geom[GEOM_BOX].ib_byte_size = ib_byte_size;
    render_ctx->geom[GEOM_BOX].index_format = DXGI_FORMAT_R16_UINT;

    render_ctx->geom[GEOM_BOX].submesh_names[0] = "box";
    render_ctx->geom[GEOM_BOX].submesh_geoms[0] = box_submesh;

    // -- cleanup
    free(scratch);
    free(indices);
    free(vertices);
}
static void
create_land_geometry (D3DRenderContext * render_ctx) {

    // required sizes calculations
    int nrow = 50;
    int ncol = 50;
    int nvtx = nrow * ncol;
    int nidx = (nrow - 1) * (ncol - 1) * 6;     // every 6 vertices form a quad

    Vertex * vertices = (Vertex *)::malloc(sizeof(Vertex) * nvtx);
    uint16_t * indices = (uint16_t *)::malloc(sizeof(uint16_t) * nidx);
    GeomVertex * grid = (GeomVertex *)::malloc(sizeof(GeomVertex) * nvtx);

    create_grid16(320.0f, 320.0f, 50, 50, grid, indices);

    // Extract the vertex elements we are interested and apply the height function to
    // each vertex.  In addition, color the vertices based on their height so we have
    // sandy looking beaches, grassy low hills, and snow mountain peaks.

    for (int i = 0; i < nvtx; ++i) {
        auto & p = grid[i].Position;
        vertices[i].position = p;
        vertices[i].position.y = calc_hill_height(p.x, p.z);
        vertices[i].normal = calc_hill_normal(p.x, p.z);
        vertices[i].texc = grid[i].TexC;
    }

    UINT vb_byte_size = nvtx * sizeof(Vertex);
    UINT ib_byte_size = nidx * sizeof(uint16_t);

    // -- Fill out render_ctx geom (output)

    CHECK_AND_FAIL(D3DCreateBlob(vb_byte_size, &render_ctx->geom[GEOM_GRID].vb_cpu));
    if (vertices)
        CopyMemory(render_ctx->geom[GEOM_GRID].vb_cpu->GetBufferPointer(), vertices, vb_byte_size);

    D3DCreateBlob(ib_byte_size, &render_ctx->geom[GEOM_GRID].ib_cpu);
    if (indices)
        CopyMemory(render_ctx->geom[GEOM_GRID].ib_cpu->GetBufferPointer(), indices, ib_byte_size);

    create_default_buffer(render_ctx->device, render_ctx->direct_cmd_list, vertices, vb_byte_size, &render_ctx->geom[GEOM_GRID].vb_gpu, &render_ctx->geom[GEOM_GRID].vb_uploader);
    create_default_buffer(render_ctx->device, render_ctx->direct_cmd_list, indices, ib_byte_size, &render_ctx->geom[GEOM_GRID].ib_gpu, &render_ctx->geom[GEOM_GRID].ib_uploader);

    render_ctx->geom[GEOM_GRID].vb_byte_stide = sizeof(Vertex);
    render_ctx->geom[GEOM_GRID].vb_byte_size = vb_byte_size;
    render_ctx->geom[GEOM_GRID].ib_byte_size = ib_byte_size;
    render_ctx->geom[GEOM_GRID].index_format = DXGI_FORMAT_R16_UINT;

    SubmeshGeometry submesh;
    submesh.index_count = nidx;
    submesh.start_index_location = 0;
    submesh.base_vertex_location = 0;

    render_ctx->geom[GEOM_GRID].submesh_names[0] = "grid";
    render_ctx->geom[GEOM_GRID].submesh_geoms[0] = submesh;

    ::free(grid);
    ::free(indices);
    ::free(vertices);
}
static void
create_water_geometry (UINT nrow, UINT ncol, UINT ntri, D3DRenderContext * render_ctx) {
    _Unreferenced_parameter_(ntri);
    uint32_t _WAVE_VTX_CNT = ncol * nrow;
    uint32_t _idx_cnt = (nrow - 1) * (ncol - 1) * 6;    // every 6 vertices form a quad
    Vertex * vertices = (Vertex *)::calloc(_WAVE_VTX_CNT, sizeof(Vertex));
    uint32_t * indices = (uint32_t *)::calloc(_idx_cnt, sizeof(uint32_t));

    GeomVertex * grid = (GeomVertex *)::calloc(_WAVE_VTX_CNT, sizeof(GeomVertex));

    create_grid32(256.0f, 256.0f, nrow, ncol, grid, indices);

    for (uint32_t i = 0; i < _WAVE_VTX_CNT; i++) {
        vertices[i].position = grid[i].Position;
        vertices[i].normal = grid[i].Normal;
        vertices[i].texc = grid[i].TexC;
    }

    UINT vb_byte_size = _WAVE_VTX_CNT * sizeof(Vertex);
    UINT ib_byte_size = _idx_cnt * sizeof(uint32_t);

    // -- Fill out render_ctx geom (output)

    CHECK_AND_FAIL(D3DCreateBlob(vb_byte_size, &render_ctx->geom[GEOM_WATER].vb_cpu));
    if (vertices)
        CopyMemory(render_ctx->geom[GEOM_WATER].vb_cpu->GetBufferPointer(), vertices, vb_byte_size);
    CHECK_AND_FAIL(D3DCreateBlob(ib_byte_size, &render_ctx->geom[GEOM_WATER].ib_cpu));
    if (indices)
        CopyMemory(render_ctx->geom[GEOM_WATER].ib_cpu->GetBufferPointer(), indices, ib_byte_size);

    create_default_buffer(render_ctx->device, render_ctx->direct_cmd_list, vertices, vb_byte_size, &render_ctx->geom[GEOM_WATER].vb_gpu, &render_ctx->geom[GEOM_WATER].vb_uploader);
    create_default_buffer(render_ctx->device, render_ctx->direct_cmd_list, indices, ib_byte_size, &render_ctx->geom[GEOM_WATER].ib_gpu, &render_ctx->geom[GEOM_WATER].ib_uploader);

    render_ctx->geom[GEOM_WATER].vb_byte_stide = sizeof(Vertex);
    render_ctx->geom[GEOM_WATER].vb_byte_size = vb_byte_size;
    render_ctx->geom[GEOM_WATER].ib_byte_size = ib_byte_size;
    render_ctx->geom[GEOM_WATER].index_format = DXGI_FORMAT_R32_UINT;

    SubmeshGeometry submesh;
    submesh.index_count = _idx_cnt;
    submesh.start_index_location = 0;
    submesh.base_vertex_location = 0;

    render_ctx->geom[GEOM_WATER].submesh_names[0] = "water";
    render_ctx->geom[GEOM_WATER].submesh_geoms[0] = submesh;

    ::free(grid);
    ::free(indices);
    ::free(vertices);
}
static void
create_treesprites_geometry (D3DRenderContext * render_ctx) {
    struct TreeSpriteVertex {
        XMFLOAT3 pos;
        XMFLOAT2 size;
    };

    constexpr int tree_count = 10;
    TreeSpriteVertex vertices[tree_count];
    vertices[0].pos = {-40.00f , 8.5f + calc_hill_height(-40.00f, 60.00f) ,  60.00f}; vertices[0].size = XMFLOAT2(20.0f, 20.0f);
    vertices[1].pos = {20.00f  , 7.5f + calc_hill_height(20.00f, 58.10f)  ,  58.10f}; vertices[1].size = XMFLOAT2(20.0f, 20.0f);
    vertices[2].pos = {-50.00f , 8.5f + calc_hill_height(-50.00f, 30.00f) ,  30.00f}; vertices[2].size = XMFLOAT2(20.0f, 20.0f);
    vertices[3].pos = {-100.00f, 8.5f + calc_hill_height(-100.00, 30.00f) ,  30.00f}; vertices[3].size = XMFLOAT2(20.0f, 20.0f);
    vertices[4].pos = {35.00f  , 9.0f + calc_hill_height(35.00f, 0.00f)   ,   0.00f}; vertices[4].size = XMFLOAT2(20.0f, 20.0f);
    vertices[5].pos = {85.00f  , 9.0f + calc_hill_height(85.00f, 65.00f)  ,  65.00f}; vertices[5].size = XMFLOAT2(20.0f, 20.0f);
    vertices[6].pos = {-86.00f , 7.0f + calc_hill_height(-86.00f, -30.00f), -30.00f}; vertices[6].size = XMFLOAT2(20.0f, 20.0f);
    vertices[7].pos = {-15.00f , 9.0f + calc_hill_height(-15.00f, -30.00f), -30.00f}; vertices[7].size = XMFLOAT2(20.0f, 20.0f);
    vertices[8].pos = {14.00f  , 9.0f + calc_hill_height(14.00f, 38.00f)  ,  38.00f}; vertices[8].size = XMFLOAT2(20.0f, 20.0f);
    vertices[9].pos = {49.00f  , 8.0f + calc_hill_height(49.00f, -65.00f) , -65.00f}; vertices[9].size = XMFLOAT2(20.0f, 20.0f);

    /* Random Tree Positioning
    srand((unsigned int)time(NULL));
    for (UINT i = 0; i < tree_count; i++) {
        float x = rand_float(-105.0f, 105.0f);
        float z = 0;
        int rn = rand_int(0, 1);
        if (rn == 0)
            z = rand_float(30.0f, 60.0f);
        else
            z = rand_float(-30.0f, -60.0f);
        float y = calc_hill_height(x, z);

        // a bit of offset
        y += 9.0f;

        vertices[i].pos = XMFLOAT3(x, y, z);
        vertices[i].size = XMFLOAT2(20.0f, 20.0f);
    }
    */

    uint16_t indices[tree_count] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8,
        9/*, 10, 11, 12, 13, 14, 15*/
    };

    UINT vb_byte_size = tree_count * sizeof(TreeSpriteVertex);
    UINT ib_byte_size = tree_count * sizeof(uint16_t);

    D3DCreateBlob(vb_byte_size, &render_ctx->geom[GEOM_TREESPRITE].vb_cpu);
    if (vertices)
        CopyMemory(render_ctx->geom[GEOM_TREESPRITE].vb_cpu->GetBufferPointer(), vertices, vb_byte_size);

    D3DCreateBlob(ib_byte_size, &render_ctx->geom[GEOM_TREESPRITE].ib_cpu);
    if (indices)
        CopyMemory(render_ctx->geom[GEOM_TREESPRITE].ib_cpu->GetBufferPointer(), indices, ib_byte_size);

    create_default_buffer(render_ctx->device, render_ctx->direct_cmd_list, vertices, vb_byte_size, &render_ctx->geom[GEOM_TREESPRITE].vb_gpu, &render_ctx->geom[GEOM_TREESPRITE].vb_uploader);
    create_default_buffer(render_ctx->device, render_ctx->direct_cmd_list, indices, ib_byte_size, &render_ctx->geom[GEOM_TREESPRITE].ib_gpu, &render_ctx->geom[GEOM_TREESPRITE].ib_uploader);

    render_ctx->geom[GEOM_TREESPRITE].vb_byte_stide = sizeof(TreeSpriteVertex);
    render_ctx->geom[GEOM_TREESPRITE].vb_byte_size = vb_byte_size;
    render_ctx->geom[GEOM_TREESPRITE].ib_byte_size = ib_byte_size;
    render_ctx->geom[GEOM_TREESPRITE].index_format = DXGI_FORMAT_R16_UINT;

    SubmeshGeometry submesh;
    submesh.index_count = tree_count;
    submesh.start_index_location = 0;
    submesh.base_vertex_location = 0;

    render_ctx->geom[GEOM_TREESPRITE].submesh_names[0] = "points";
    render_ctx->geom[GEOM_TREESPRITE].submesh_geoms[0] = submesh;
}
static void
create_render_items (D3DRenderContext * render_ctx, GpuWaves * wave) {
    render_ctx->all_ritems.ritems[RITEM_WATER].world = Identity4x4();
    render_ctx->all_ritems.ritems[RITEM_WATER].tex_transform = Identity4x4();
    XMStoreFloat4x4(&render_ctx->all_ritems.ritems[RITEM_WATER].tex_transform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
    render_ctx->all_ritems.ritems[RITEM_WATER].obj_cbuffer_index = 0;
    render_ctx->all_ritems.ritems[RITEM_WATER].mat = &render_ctx->materials[MAT_WATER];
    render_ctx->all_ritems.ritems[RITEM_WATER].geometry = &render_ctx->geom[GEOM_WATER];
    render_ctx->all_ritems.ritems[RITEM_WATER].primitive_type = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    render_ctx->all_ritems.ritems[RITEM_WATER].index_count = render_ctx->geom[GEOM_WATER].submesh_geoms[0].index_count;
    render_ctx->all_ritems.ritems[RITEM_WATER].start_index_loc = render_ctx->geom[GEOM_WATER].submesh_geoms[0].start_index_location;
    render_ctx->all_ritems.ritems[RITEM_WATER].base_vertex_loc = render_ctx->geom[GEOM_WATER].submesh_geoms[0].base_vertex_location;
    render_ctx->all_ritems.ritems[RITEM_WATER].n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_WATER].mat->n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_WATER].grid_spatial_step = wave->spatial_step;
    render_ctx->all_ritems.ritems[RITEM_WATER].displacement_map_texel_size.x = 1.0f / wave->ncol;
    render_ctx->all_ritems.ritems[RITEM_WATER].displacement_map_texel_size.y = 1.0f / wave->nrow;
    render_ctx->all_ritems.ritems[RITEM_WATER].initialized = true;
    render_ctx->all_ritems.size++;
    render_ctx->transparent_ritems.ritems[0] = render_ctx->all_ritems.ritems[RITEM_WATER];
    render_ctx->transparent_ritems.size++;
    render_ctx->gpu_waves_rtimes.ritems[0] = render_ctx->all_ritems.ritems[RITEM_WATER];
    render_ctx->gpu_waves_rtimes.size++;

    render_ctx->all_ritems.ritems[RITEM_GRID].world = Identity4x4();
    render_ctx->all_ritems.ritems[RITEM_GRID].tex_transform = Identity4x4();
    XMStoreFloat4x4(&render_ctx->all_ritems.ritems[RITEM_GRID].tex_transform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
    render_ctx->all_ritems.ritems[RITEM_GRID].obj_cbuffer_index = 1;
    render_ctx->all_ritems.ritems[RITEM_GRID].mat = &render_ctx->materials[MAT_GRASS];
    render_ctx->all_ritems.ritems[RITEM_GRID].geometry = &render_ctx->geom[GEOM_GRID];
    render_ctx->all_ritems.ritems[RITEM_GRID].primitive_type = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    render_ctx->all_ritems.ritems[RITEM_GRID].index_count = render_ctx->geom[GEOM_GRID].submesh_geoms[0].index_count;
    render_ctx->all_ritems.ritems[RITEM_GRID].start_index_loc = render_ctx->geom[GEOM_GRID].submesh_geoms[0].start_index_location;
    render_ctx->all_ritems.ritems[RITEM_GRID].base_vertex_loc = render_ctx->geom[GEOM_GRID].submesh_geoms[0].base_vertex_location;
    render_ctx->all_ritems.ritems[RITEM_GRID].n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_GRID].mat->n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_GRID].grid_spatial_step = 1;
    render_ctx->all_ritems.ritems[RITEM_GRID].displacement_map_texel_size = {1.0f, 1.0f};
    render_ctx->all_ritems.ritems[RITEM_GRID].initialized = true;
    render_ctx->all_ritems.size++;
    render_ctx->opaque_ritems.ritems[0] = render_ctx->all_ritems.ritems[RITEM_GRID];
    render_ctx->opaque_ritems.size++;

    render_ctx->all_ritems.ritems[RITEM_BOX].world = Identity4x4();
    XMStoreFloat4x4(&render_ctx->all_ritems.ritems[RITEM_BOX].world, XMMatrixTranslation(3.0f, 2.0f, -9.0f));
    render_ctx->all_ritems.ritems[RITEM_BOX].tex_transform = Identity4x4();
    render_ctx->all_ritems.ritems[RITEM_BOX].obj_cbuffer_index = 2;
    render_ctx->all_ritems.ritems[RITEM_BOX].geometry = &render_ctx->geom[GEOM_BOX];
    render_ctx->all_ritems.ritems[RITEM_BOX].mat = &render_ctx->materials[MAT_WOOD_CRATE];
    render_ctx->all_ritems.ritems[RITEM_BOX].primitive_type = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    render_ctx->all_ritems.ritems[RITEM_BOX].index_count = render_ctx->geom[GEOM_BOX].submesh_geoms[0].index_count;
    render_ctx->all_ritems.ritems[RITEM_BOX].start_index_loc = render_ctx->geom[GEOM_BOX].submesh_geoms[0].start_index_location;
    render_ctx->all_ritems.ritems[RITEM_BOX].base_vertex_loc = render_ctx->geom[GEOM_BOX].submesh_geoms[0].base_vertex_location;
    render_ctx->all_ritems.ritems[RITEM_BOX].n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_BOX].mat->n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_BOX].grid_spatial_step = 1;
    render_ctx->all_ritems.ritems[RITEM_BOX].displacement_map_texel_size = {1.0f, 1.0f};
    render_ctx->all_ritems.ritems[RITEM_BOX].initialized = true;
    render_ctx->all_ritems.size++;
    render_ctx->alphatested_ritems.ritems[0] = render_ctx->all_ritems.ritems[RITEM_BOX];
    render_ctx->alphatested_ritems.size++;

    render_ctx->all_ritems.ritems[RITEM_TREESPRITE].world = Identity4x4();
    render_ctx->all_ritems.ritems[RITEM_TREESPRITE].tex_transform = Identity4x4();
    render_ctx->all_ritems.ritems[RITEM_TREESPRITE].obj_cbuffer_index = 3;
    render_ctx->all_ritems.ritems[RITEM_TREESPRITE].geometry = &render_ctx->geom[GEOM_TREESPRITE];
    render_ctx->all_ritems.ritems[RITEM_TREESPRITE].mat = &render_ctx->materials[MAT_TREE_SPRITE];
    render_ctx->all_ritems.ritems[RITEM_TREESPRITE].primitive_type = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    render_ctx->all_ritems.ritems[RITEM_TREESPRITE].index_count = render_ctx->geom[GEOM_TREESPRITE].submesh_geoms[0].index_count;
    render_ctx->all_ritems.ritems[RITEM_TREESPRITE].start_index_loc = render_ctx->geom[GEOM_TREESPRITE].submesh_geoms[0].start_index_location;
    render_ctx->all_ritems.ritems[RITEM_TREESPRITE].base_vertex_loc = render_ctx->geom[GEOM_TREESPRITE].submesh_geoms[0].base_vertex_location;
    render_ctx->all_ritems.ritems[RITEM_TREESPRITE].n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_TREESPRITE].mat->n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_TREESPRITE].grid_spatial_step = 1;
    render_ctx->all_ritems.ritems[RITEM_TREESPRITE].displacement_map_texel_size = {1.0f, 1.0f};
    render_ctx->all_ritems.ritems[RITEM_TREESPRITE].initialized = true;
    render_ctx->all_ritems.size++;
    render_ctx->alphatested_treesprites_ritems.ritems[0] = render_ctx->all_ritems.ritems[RITEM_TREESPRITE];
    render_ctx->alphatested_treesprites_ritems.size++;
}
// -- indexed drawing
static void
draw_render_items (
    ID3D12GraphicsCommandList * cmd_list,
    ID3D12Resource * object_cbuffer,
    UINT64 descriptor_increment_size,
    RenderItemArray * ritem_array,
    UINT current_frame_index
) {
    UINT objcb_byte_size = (UINT64)sizeof(ObjectConstants);
    for (size_t i = 0; i < ritem_array->size; ++i) {
        if (ritem_array->ritems[i].initialized) {
            D3D12_VERTEX_BUFFER_VIEW vbv = Mesh_GetVertexBufferView(ritem_array->ritems[i].geometry);
            D3D12_INDEX_BUFFER_VIEW ibv = Mesh_GetIndexBufferView(ritem_array->ritems[i].geometry);
            cmd_list->IASetVertexBuffers(0, 1, &vbv);
            cmd_list->IASetIndexBuffer(&ibv);
            cmd_list->IASetPrimitiveTopology(ritem_array->ritems[i].primitive_type);

            D3D12_GPU_VIRTUAL_ADDRESS objcb_address = object_cbuffer->GetGPUVirtualAddress();
            objcb_address += (UINT64)ritem_array->ritems[i].obj_cbuffer_index * objcb_byte_size;

            cmd_list->SetGraphicsRootConstantBufferView(0, objcb_address);

            cmd_list->DrawIndexedInstanced(ritem_array->ritems[i].index_count, 1, ritem_array->ritems[i].start_index_loc, ritem_array->ritems[i].base_vertex_loc, 0);
        }
    }
}
static void
create_descriptor_heaps (
    D3DRenderContext * render_ctx,
    GpuWaves * wave,
    BlurFilter * blur, SobelFilter * sobel,
    OffscreenRenderTarget * ort
) {
    // Create Shader Resource View descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
    srv_heap_desc.NumDescriptors = _COUNT_TEX +
        6 +     /* GpuWaves descriptors */
        4 +     /* Blur descriptors     */
        2 +     /* Sobel descriptors    */
        1 +     /* Offscreen RenderTarget descriptor */
        1;      /* imgui descriptor     */
    srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    render_ctx->device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&render_ctx->srv_heap));

    // Fill out the heap with actual descriptors
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_handle = render_ctx->srv_heap->GetCPUDescriptorHandleForHeapStart();

    // grass texture
    ID3D12Resource * grass_tex = render_ctx->textures[TEX_GRASS].resource;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = grass_tex->GetDesc().Format;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = grass_tex->GetDesc().MipLevels;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
    render_ctx->device->CreateShaderResourceView(grass_tex, &srv_desc, descriptor_cpu_handle);

    // water texture
    ID3D12Resource * water_tex = render_ctx->textures[TEX_WATER].resource;
    memset(&srv_desc, 0, sizeof(srv_desc));         // reset desc
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = water_tex->GetDesc().Format;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = water_tex->GetDesc().MipLevels;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
    descriptor_cpu_handle.ptr += render_ctx->cbv_srv_uav_descriptor_size;   // next descriptr
    render_ctx->device->CreateShaderResourceView(water_tex, &srv_desc, descriptor_cpu_handle);

    // crate texture
    ID3D12Resource * box_tex = render_ctx->textures[TEX_CRATE01].resource;
    memset(&srv_desc, 0, sizeof(srv_desc)); // reset desc
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = box_tex->GetDesc().Format;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = box_tex->GetDesc().MipLevels;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
    descriptor_cpu_handle.ptr += render_ctx->cbv_srv_uav_descriptor_size;   // next descriptor
    render_ctx->device->CreateShaderResourceView(box_tex, &srv_desc, descriptor_cpu_handle);

    // wire_fence texture
    ID3D12Resource * wire_tex = render_ctx->textures[TEX_WIREFENCE].resource;
    memset(&srv_desc, 0, sizeof(srv_desc)); // reset desc
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = wire_tex->GetDesc().Format;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = wire_tex->GetDesc().MipLevels;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
    descriptor_cpu_handle.ptr += render_ctx->cbv_srv_uav_descriptor_size;   // next descriptor
    render_ctx->device->CreateShaderResourceView(wire_tex, &srv_desc, descriptor_cpu_handle);

    // tree_array texture
    ID3D12Resource * tree_tex = render_ctx->textures[TEX_TREEARRAY].resource;
    memset(&srv_desc, 0, sizeof(srv_desc)); // reset desc
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = tree_tex->GetDesc().Format;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srv_desc.Texture2DArray.MostDetailedMip = 0;
    srv_desc.Texture2DArray.MipLevels = tree_tex->GetDesc().MipLevels;
    srv_desc.Texture2DArray.FirstArraySlice = 0;
    srv_desc.Texture2DArray.ArraySize = tree_tex->GetDesc().DepthOrArraySize;
    descriptor_cpu_handle.ptr += render_ctx->cbv_srv_uav_descriptor_size;   // next descriptor
    render_ctx->device->CreateShaderResourceView(tree_tex, &srv_desc, descriptor_cpu_handle);

    //
    // Create GpuWaves descriptors
    //
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_waves = render_ctx->srv_heap->GetCPUDescriptorHandleForHeapStart();
    hcpu_waves.ptr +=
        (_COUNT_TEX)*render_ctx->cbv_srv_uav_descriptor_size;
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_waves = render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
    hgpu_waves.ptr +=
        (_COUNT_TEX)*render_ctx->cbv_srv_uav_descriptor_size;
    GpuWaves_BuildDescriptors(wave, hcpu_waves, hgpu_waves, render_ctx->cbv_srv_uav_descriptor_size);

    //
    // Create Blur descriptors
    //
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_blur = render_ctx->srv_heap->GetCPUDescriptorHandleForHeapStart();
    hcpu_blur.ptr +=
        (_COUNT_TEX + 6 /* GpuWaves descriptors */) * render_ctx->cbv_srv_uav_descriptor_size;
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_blur = render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
    hgpu_blur.ptr +=
        (_COUNT_TEX + 6 /* GpuWaves descriptors */) * render_ctx->cbv_srv_uav_descriptor_size;
    BlurFilter_CreateDescriptors(blur, hcpu_blur, hgpu_blur, render_ctx->cbv_srv_uav_descriptor_size);


    //
    // Create Sobel Descriptors
    //
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_sobel = render_ctx->srv_heap->GetCPUDescriptorHandleForHeapStart();
    hcpu_sobel.ptr +=
        (_COUNT_TEX + 6 /* GpuWaves descriptors */ + 4 /* Blur descriptors */) * render_ctx->cbv_srv_uav_descriptor_size;
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_sobel = render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
    hgpu_sobel.ptr +=
        (_COUNT_TEX + 6 /* GpuWaves descriptors */ + 4 /* Blur descriptors */) * render_ctx->cbv_srv_uav_descriptor_size;
    SobelFilter_CreateDescriptors(sobel, hcpu_sobel, hgpu_sobel, render_ctx->cbv_srv_uav_descriptor_size);


    // Create Render Target View Descriptor Heap
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
    rtv_heap_desc.NumDescriptors = NUM_BACKBUFFERS + 1 /* offscreen render-target */;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAGS::D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    render_ctx->device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&render_ctx->rtv_heap));

    //
    // Create Offscreen RenderTarget descriptors
    //
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_offscreen_rt = render_ctx->srv_heap->GetCPUDescriptorHandleForHeapStart();
    hcpu_offscreen_rt.ptr +=
        (_COUNT_TEX + 6 /* GpuWaves descriptors */ + 4 /* Blur descriptors */ + 2 /* Sobel descriptors */) * render_ctx->cbv_srv_uav_descriptor_size;
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_offscreen_rt = render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
    hgpu_offscreen_rt.ptr +=
        (_COUNT_TEX + 6 /* GpuWaves descriptors */ + 4 /* Blur descriptors */ + 2 /* Sobel descriptors */) * render_ctx->cbv_srv_uav_descriptor_size;
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_rtv = render_ctx->rtv_heap->GetCPUDescriptorHandleForHeapStart();
    hcpu_rtv.ptr +=
        (NUM_BACKBUFFERS)*render_ctx->rtv_descriptor_size;
    OffscreenRenderTarget_CreateDescriptors(ort, hcpu_offscreen_rt, hgpu_offscreen_rt, hcpu_rtv);

    // Create Depth Stencil View Descriptor Heap
    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc;
    dsv_heap_desc.NumDescriptors = 1;
    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsv_heap_desc.NodeMask = 0;
    render_ctx->device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&render_ctx->dsv_heap));

}
static void
get_static_samplers (D3D12_STATIC_SAMPLER_DESC out_samplers []) {
    // 0: PointWrap
    out_samplers[SAMPLER_POINT_WRAP] = {};
    out_samplers[SAMPLER_POINT_WRAP].ShaderRegister = 0;
    out_samplers[SAMPLER_POINT_WRAP].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    out_samplers[SAMPLER_POINT_WRAP].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    out_samplers[SAMPLER_POINT_WRAP].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    out_samplers[SAMPLER_POINT_WRAP].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    out_samplers[SAMPLER_POINT_WRAP].MipLODBias = 0;
    out_samplers[SAMPLER_POINT_WRAP].MaxAnisotropy = 16;
    out_samplers[SAMPLER_POINT_WRAP].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    out_samplers[SAMPLER_POINT_WRAP].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    out_samplers[SAMPLER_POINT_WRAP].MinLOD = 0.f;
    out_samplers[SAMPLER_POINT_WRAP].MaxLOD = D3D12_FLOAT32_MAX;
    out_samplers[SAMPLER_POINT_WRAP].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    out_samplers[SAMPLER_POINT_WRAP].RegisterSpace = 0;

    // 1: PointClamp
    out_samplers[SAMPLER_POINT_CLAMP] = {};
    out_samplers[SAMPLER_POINT_CLAMP].ShaderRegister = 1;
    out_samplers[SAMPLER_POINT_CLAMP].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    out_samplers[SAMPLER_POINT_CLAMP].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    out_samplers[SAMPLER_POINT_CLAMP].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    out_samplers[SAMPLER_POINT_CLAMP].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    out_samplers[SAMPLER_POINT_CLAMP].MipLODBias = 0;
    out_samplers[SAMPLER_POINT_CLAMP].MaxAnisotropy = 16;
    out_samplers[SAMPLER_POINT_CLAMP].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    out_samplers[SAMPLER_POINT_CLAMP].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    out_samplers[SAMPLER_POINT_CLAMP].MinLOD = 0.f;
    out_samplers[SAMPLER_POINT_CLAMP].MaxLOD = D3D12_FLOAT32_MAX;
    out_samplers[SAMPLER_POINT_CLAMP].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    out_samplers[SAMPLER_POINT_CLAMP].RegisterSpace = 0;

    // 2: LinearWrap
    out_samplers[SAMPLER_LINEAR_WRAP] = {};
    out_samplers[SAMPLER_LINEAR_WRAP].ShaderRegister = 2;
    out_samplers[SAMPLER_LINEAR_WRAP].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    out_samplers[SAMPLER_LINEAR_WRAP].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    out_samplers[SAMPLER_LINEAR_WRAP].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    out_samplers[SAMPLER_LINEAR_WRAP].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    out_samplers[SAMPLER_LINEAR_WRAP].MipLODBias = 0;
    out_samplers[SAMPLER_LINEAR_WRAP].MaxAnisotropy = 16;
    out_samplers[SAMPLER_LINEAR_WRAP].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    out_samplers[SAMPLER_LINEAR_WRAP].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    out_samplers[SAMPLER_LINEAR_WRAP].MinLOD = 0.f;
    out_samplers[SAMPLER_LINEAR_WRAP].MaxLOD = D3D12_FLOAT32_MAX;
    out_samplers[SAMPLER_LINEAR_WRAP].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    out_samplers[SAMPLER_LINEAR_WRAP].RegisterSpace = 0;

    // 3: LinearClamp
    out_samplers[SAMPLER_LINEAR_CLAMP] = {};
    out_samplers[SAMPLER_LINEAR_CLAMP].ShaderRegister = 3;
    out_samplers[SAMPLER_LINEAR_CLAMP].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    out_samplers[SAMPLER_LINEAR_CLAMP].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    out_samplers[SAMPLER_LINEAR_CLAMP].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    out_samplers[SAMPLER_LINEAR_CLAMP].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    out_samplers[SAMPLER_LINEAR_CLAMP].MipLODBias = 0;
    out_samplers[SAMPLER_LINEAR_CLAMP].MaxAnisotropy = 16;
    out_samplers[SAMPLER_LINEAR_CLAMP].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    out_samplers[SAMPLER_LINEAR_CLAMP].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    out_samplers[SAMPLER_LINEAR_CLAMP].MinLOD = 0.f;
    out_samplers[SAMPLER_LINEAR_CLAMP].MaxLOD = D3D12_FLOAT32_MAX;
    out_samplers[SAMPLER_LINEAR_CLAMP].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    out_samplers[SAMPLER_LINEAR_CLAMP].RegisterSpace = 0;

    // 4: AnisotropicWrap
    out_samplers[SAMPLER_ANISOTROPIC_WRAP] = {};
    out_samplers[SAMPLER_ANISOTROPIC_WRAP].ShaderRegister = 4;
    out_samplers[SAMPLER_ANISOTROPIC_WRAP].Filter = D3D12_FILTER_ANISOTROPIC;
    out_samplers[SAMPLER_ANISOTROPIC_WRAP].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    out_samplers[SAMPLER_ANISOTROPIC_WRAP].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    out_samplers[SAMPLER_ANISOTROPIC_WRAP].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    out_samplers[SAMPLER_ANISOTROPIC_WRAP].MipLODBias = 0.0f;
    out_samplers[SAMPLER_ANISOTROPIC_WRAP].MaxAnisotropy = 8;
    out_samplers[SAMPLER_ANISOTROPIC_WRAP].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    out_samplers[SAMPLER_ANISOTROPIC_WRAP].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    out_samplers[SAMPLER_ANISOTROPIC_WRAP].MinLOD = 0.f;
    out_samplers[SAMPLER_ANISOTROPIC_WRAP].MaxLOD = D3D12_FLOAT32_MAX;
    out_samplers[SAMPLER_ANISOTROPIC_WRAP].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    out_samplers[SAMPLER_ANISOTROPIC_WRAP].RegisterSpace = 0;

    // 5: AnisotropicClamp
    out_samplers[SAMPLER_ANISOTROPIC_CLAMP] = {};
    out_samplers[SAMPLER_ANISOTROPIC_CLAMP].ShaderRegister = 5;
    out_samplers[SAMPLER_ANISOTROPIC_CLAMP].Filter = D3D12_FILTER_ANISOTROPIC;
    out_samplers[SAMPLER_ANISOTROPIC_CLAMP].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    out_samplers[SAMPLER_ANISOTROPIC_CLAMP].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    out_samplers[SAMPLER_ANISOTROPIC_CLAMP].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    out_samplers[SAMPLER_ANISOTROPIC_CLAMP].MipLODBias = 0.0f;
    out_samplers[SAMPLER_ANISOTROPIC_CLAMP].MaxAnisotropy = 8;
    out_samplers[SAMPLER_ANISOTROPIC_CLAMP].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    out_samplers[SAMPLER_ANISOTROPIC_CLAMP].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    out_samplers[SAMPLER_ANISOTROPIC_CLAMP].MinLOD = 0.f;
    out_samplers[SAMPLER_ANISOTROPIC_CLAMP].MaxLOD = D3D12_FLOAT32_MAX;
    out_samplers[SAMPLER_ANISOTROPIC_CLAMP].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    out_samplers[SAMPLER_ANISOTROPIC_CLAMP].RegisterSpace = 0;
}
static void
create_root_signature (ID3D12Device * device, ID3D12RootSignature ** root_signature) {
    D3D12_DESCRIPTOR_RANGE tex_table = {};
    tex_table.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    tex_table.NumDescriptors = _COUNT_TEX;
    tex_table.BaseShaderRegister = 0;
    tex_table.RegisterSpace = 0;
    tex_table.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // NOTE(omid): The 5 elements of texture array occupy registers t0, t1, t2, t3 and t4

    D3D12_DESCRIPTOR_RANGE displacement_map_table = {};
    displacement_map_table.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    displacement_map_table.NumDescriptors = 1;
    displacement_map_table.BaseShaderRegister = _COUNT_TEX; // t5
    displacement_map_table.RegisterSpace = 0;
    displacement_map_table.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER slot_root_params[5] = {};
    // NOTE(omid): Perfomance tip! Order from most frequent to least frequent.
    // -- obj cbuffer
    slot_root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    slot_root_params[0].Descriptor.ShaderRegister = 0;
    slot_root_params[0].Descriptor.RegisterSpace = 0;
    slot_root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // -- pass cbuffer
    slot_root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    slot_root_params[1].Descriptor.ShaderRegister = 1;
    slot_root_params[1].Descriptor.RegisterSpace = 0;
    slot_root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // -- structured buffer <material data>
    slot_root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    slot_root_params[2].Descriptor.ShaderRegister = 0;
    slot_root_params[2].Descriptor.RegisterSpace = 1;
    slot_root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;


    slot_root_params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[3].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[3].DescriptorTable.pDescriptorRanges = &tex_table;
    slot_root_params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    slot_root_params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[4].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[4].DescriptorTable.pDescriptorRanges = &displacement_map_table;
    slot_root_params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC samplers[_COUNT_SAMPLER] = {};
    get_static_samplers(samplers);

    // A root signature is an array of root parameters.
    D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {};
    root_sig_desc.NumParameters = _countof(slot_root_params);
    root_sig_desc.pParameters = slot_root_params;
    root_sig_desc.NumStaticSamplers = _COUNT_SAMPLER;
    root_sig_desc.pStaticSamplers = samplers;
    root_sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob * serialized_root_sig = nullptr;
    ID3DBlob * error_blob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized_root_sig, &error_blob);

    if (error_blob) {
        ::OutputDebugStringA((char*)error_blob->GetBufferPointer());
    }

    device->CreateRootSignature(0, serialized_root_sig->GetBufferPointer(), serialized_root_sig->GetBufferSize(), IID_PPV_ARGS(root_signature));
}
static void
create_root_signature_gpuwaves (ID3D12Device * device, ID3D12RootSignature ** wave_root_signature) {

    D3D12_DESCRIPTOR_RANGE uav_table0 = {};
    uav_table0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uav_table0.NumDescriptors = 1;
    uav_table0.BaseShaderRegister = 0;
    uav_table0.RegisterSpace = 0;
    uav_table0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uav_table1 = {};
    uav_table1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uav_table1.NumDescriptors = 1;
    uav_table1.BaseShaderRegister = 1;
    uav_table1.RegisterSpace = 0;
    uav_table1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uav_table2 = {};
    uav_table2.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uav_table2.NumDescriptors = 1;
    uav_table2.BaseShaderRegister = 2;
    uav_table2.RegisterSpace = 0;
    uav_table2.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER slot_root_params[4] = {};
    // NOTE(omid): Perfomance tip! Order from most frequent to least frequent.
    slot_root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    slot_root_params[0].Constants.Num32BitValues = 6; /* k1, k2, k3, magnitude, [i, j] */
    slot_root_params[0].Constants.ShaderRegister = 0;
    slot_root_params[0].Constants.RegisterSpace = 0;
    slot_root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    slot_root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[1].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[1].DescriptorTable.pDescriptorRanges = &uav_table0;
    slot_root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    slot_root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[2].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[2].DescriptorTable.pDescriptorRanges = &uav_table1;
    slot_root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    slot_root_params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[3].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[3].DescriptorTable.pDescriptorRanges = &uav_table2;
    slot_root_params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // A root signature is an array of root parameters.
    D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {};
    root_sig_desc.NumParameters = _countof(slot_root_params);
    root_sig_desc.pParameters = slot_root_params;
    root_sig_desc.NumStaticSamplers = 0;
    root_sig_desc.pStaticSamplers = NULL;
    root_sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob * serialized_root_sig = nullptr;
    ID3DBlob * error_blob = nullptr;
    D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized_root_sig, &error_blob);

    if (error_blob) {
        ::OutputDebugStringA((char*)error_blob->GetBufferPointer());
    }

    device->CreateRootSignature(0, serialized_root_sig->GetBufferPointer(), serialized_root_sig->GetBufferSize(), IID_PPV_ARGS(wave_root_signature));
}
static void
create_root_signature_postprocessing_blur (ID3D12Device * device, ID3D12RootSignature ** postprocessing_root_signature) {

    D3D12_DESCRIPTOR_RANGE srv_table = {};
    srv_table.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_table.NumDescriptors = 1;
    srv_table.BaseShaderRegister = 0;
    srv_table.RegisterSpace = 0;
    srv_table.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uav_table = {};
    uav_table.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uav_table.NumDescriptors = 1;
    uav_table.BaseShaderRegister = 0;
    uav_table.RegisterSpace = 0;
    uav_table.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER slot_root_params[3] = {};
    // NOTE(omid): Perfomance tip! Order from most frequent to least frequent.
    slot_root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    slot_root_params[0].Constants.Num32BitValues = 12; /* blur_radius, weights [11] */
    slot_root_params[0].Constants.ShaderRegister = 0;
    slot_root_params[0].Constants.RegisterSpace = 0;
    slot_root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    slot_root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[1].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[1].DescriptorTable.pDescriptorRanges = &srv_table;
    slot_root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    slot_root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[2].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[2].DescriptorTable.pDescriptorRanges = &uav_table;
    slot_root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // A root signature is an array of root parameters.
    D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {};
    root_sig_desc.NumParameters = _countof(slot_root_params);
    root_sig_desc.pParameters = slot_root_params;
    root_sig_desc.NumStaticSamplers = 0;
    root_sig_desc.pStaticSamplers = NULL;
    root_sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob * serialized_root_sig = nullptr;
    ID3DBlob * error_blob = nullptr;
    D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized_root_sig, &error_blob);

    if (error_blob) {
        ::OutputDebugStringA((char*)error_blob->GetBufferPointer());
    }

    device->CreateRootSignature(
        0, serialized_root_sig->GetBufferPointer(), serialized_root_sig->GetBufferSize(),
        IID_PPV_ARGS(postprocessing_root_signature)
    );
}
static void
create_root_signature_postprocessing_sobel (ID3D12Device * device, ID3D12RootSignature ** postprocessing_root_signature) {

    D3D12_DESCRIPTOR_RANGE srv_table0 = {};
    srv_table0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_table0.NumDescriptors = 1;
    srv_table0.BaseShaderRegister = 0;
    srv_table0.RegisterSpace = 0;
    srv_table0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE srv_table1 = {};
    srv_table1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_table1.NumDescriptors = 1;
    srv_table1.BaseShaderRegister = 1;
    srv_table1.RegisterSpace = 0;
    srv_table1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uav_table = {};
    uav_table.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uav_table.NumDescriptors = 1;
    uav_table.BaseShaderRegister = 0;
    uav_table.RegisterSpace = 0;
    uav_table.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER slot_root_params[3] = {};
    // NOTE(omid): Perfomance tip! Order from most frequent to least frequent.
    slot_root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[0].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[0].DescriptorTable.pDescriptorRanges = &srv_table0;
    slot_root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    slot_root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[1].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[1].DescriptorTable.pDescriptorRanges = &srv_table1;
    slot_root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    slot_root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[2].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[2].DescriptorTable.pDescriptorRanges = &uav_table;
    slot_root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC samplers[_COUNT_SAMPLER] = {};
    get_static_samplers(samplers);

    // A root signature is an array of root parameters.
    D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {};
    root_sig_desc.NumParameters = _countof(slot_root_params);
    root_sig_desc.pParameters = slot_root_params;
    root_sig_desc.NumStaticSamplers = _COUNT_SAMPLER;
    root_sig_desc.pStaticSamplers = samplers;
    root_sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob * serialized_root_sig = nullptr;
    ID3DBlob * error_blob = nullptr;
    D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized_root_sig, &error_blob);

    if (error_blob) {
        ::OutputDebugStringA((char*)error_blob->GetBufferPointer());
    }

    device->CreateRootSignature(
        0, serialized_root_sig->GetBufferPointer(), serialized_root_sig->GetBufferSize(),
        IID_PPV_ARGS(postprocessing_root_signature)
    );
}
static HRESULT
compile_shader (wchar_t * path, wchar_t const * entry_point, wchar_t const * shader_model, DxcDefine defines [], int n_defines, IDxcBlob ** out_shader_ptr) {
    // -- using DXC shader compiler [https://asawicki.info/news_1719_two_shader_compilers_of_direct3d_12]
    HRESULT ret = E_FAIL;

    IDxcLibrary * dxc_lib = nullptr;
    ret = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&dxc_lib));
    // if (FAILED(ret)) Handle error
    IDxcCompiler * dxc_compiler = nullptr;
    ret = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxc_compiler));
    uint32_t code_page = CP_UTF8;
    IDxcBlobEncoding * shader_blob_encoding = nullptr;
    IDxcOperationResult * dxc_res = nullptr;

    ret = dxc_lib->CreateBlobFromFile(path, &code_page, &shader_blob_encoding);
    if (shader_blob_encoding) {
        IDxcIncludeHandler * include_handler = nullptr;
        dxc_lib->CreateIncludeHandler(&include_handler);

        ret = dxc_compiler->Compile(shader_blob_encoding, path, entry_point, shader_model, nullptr, 0, defines, n_defines, include_handler, &dxc_res);
        dxc_res->GetStatus(&ret);
        dxc_res->GetResult(out_shader_ptr);
        if (FAILED(ret)) {
            if (dxc_res) {
                IDxcBlobEncoding * error_blob_encoding = nullptr;
                ret = dxc_res->GetErrorBuffer(&error_blob_encoding);
                if (SUCCEEDED(ret) && error_blob_encoding) {
                    OutputDebugStringA((const char*)error_blob_encoding->GetBufferPointer());
                    return(0);
                }
            }
            // Handle compilation error...
        }
        include_handler->Release();
    }
    shader_blob_encoding->Release();
    dxc_compiler->Release();
    dxc_lib->Release();

    _ASSERT_EXPR(*out_shader_ptr, _T("Shader Compilation Failed"));
    return ret;
}
static void
create_pso (D3DRenderContext * render_ctx) {
    // -- Create vertex-input-layout Elements

    D3D12_INPUT_ELEMENT_DESC std_input_desc[3];
    std_input_desc[0] = {};
    std_input_desc[0].SemanticName = "POSITION";
    std_input_desc[0].SemanticIndex = 0;
    std_input_desc[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    std_input_desc[0].InputSlot = 0;
    std_input_desc[0].AlignedByteOffset = 0;
    std_input_desc[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

    std_input_desc[1] = {};
    std_input_desc[1].SemanticName = "NORMAL";
    std_input_desc[1].SemanticIndex = 0;
    std_input_desc[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    std_input_desc[1].InputSlot= 0;
    std_input_desc[1].AlignedByteOffset = 12;
    std_input_desc[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

    std_input_desc[2] = {};
    std_input_desc[2].SemanticName = "TEXCOORD";
    std_input_desc[2].SemanticIndex = 0;
    std_input_desc[2].Format = DXGI_FORMAT_R32G32_FLOAT;
    std_input_desc[2].InputSlot = 0;
    std_input_desc[2].AlignedByteOffset = 24;
    std_input_desc[2].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

    D3D12_INPUT_ELEMENT_DESC treesprite_input_desc[2];
    treesprite_input_desc[0] = {};
    treesprite_input_desc[0].SemanticName = "POSITION";
    treesprite_input_desc[0].SemanticIndex = 0;
    treesprite_input_desc[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    treesprite_input_desc[0].InputSlot = 0;
    treesprite_input_desc[0].AlignedByteOffset = 0;
    treesprite_input_desc[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

    treesprite_input_desc[1] = {};
    treesprite_input_desc[1].SemanticName = "SIZE";
    treesprite_input_desc[1].SemanticIndex = 0;
    treesprite_input_desc[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    treesprite_input_desc[1].InputSlot = 0;
    treesprite_input_desc[1].AlignedByteOffset = 12;
    treesprite_input_desc[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    //
    // -- Create PSO for Opaque objs
    //
    D3D12_BLEND_DESC def_blend_desc = {};
    def_blend_desc.AlphaToCoverageEnable = FALSE;
    def_blend_desc.IndependentBlendEnable = FALSE;
    def_blend_desc.RenderTarget[0].BlendEnable = FALSE;
    def_blend_desc.RenderTarget[0].LogicOpEnable = FALSE;
    def_blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    def_blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    def_blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    def_blend_desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    def_blend_desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    def_blend_desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    def_blend_desc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    def_blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC def_rasterizer_desc = {};
    def_rasterizer_desc.FillMode = D3D12_FILL_MODE_SOLID;
    def_rasterizer_desc.CullMode = D3D12_CULL_MODE_BACK;
    def_rasterizer_desc.FrontCounterClockwise = false;
    def_rasterizer_desc.DepthBias = 0;
    def_rasterizer_desc.DepthBiasClamp = 0.0f;
    def_rasterizer_desc.SlopeScaledDepthBias = 0.0f;
    def_rasterizer_desc.DepthClipEnable = TRUE;
    def_rasterizer_desc.ForcedSampleCount = 0;
    def_rasterizer_desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    def_rasterizer_desc.MultisampleEnable = render_ctx->msaa4x_state;

    /* Depth Stencil Description */
    D3D12_DEPTH_STENCIL_DESC ds_desc = {};
    ds_desc.DepthEnable = TRUE;
    ds_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    ds_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    ds_desc.StencilEnable = FALSE;
    ds_desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    ds_desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    D3D12_DEPTH_STENCILOP_DESC def_stencil_op = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
    ds_desc.FrontFace = def_stencil_op;
    ds_desc.BackFace = def_stencil_op;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaque_pso_desc = {};
    opaque_pso_desc.pRootSignature = render_ctx->root_signature;
    opaque_pso_desc.VS.pShaderBytecode = render_ctx->shaders[SHADER_DEFAULT_VS]->GetBufferPointer();
    opaque_pso_desc.VS.BytecodeLength = render_ctx->shaders[SHADER_DEFAULT_VS]->GetBufferSize();
    opaque_pso_desc.PS.pShaderBytecode = render_ctx->shaders[SHADER_OPAQUE_PS]->GetBufferPointer();
    opaque_pso_desc.PS.BytecodeLength = render_ctx->shaders[SHADER_OPAQUE_PS]->GetBufferSize();
    opaque_pso_desc.BlendState = def_blend_desc;
    opaque_pso_desc.SampleMask = UINT_MAX;
    opaque_pso_desc.RasterizerState = def_rasterizer_desc;
    opaque_pso_desc.DepthStencilState = ds_desc;
    opaque_pso_desc.DSVFormat = render_ctx->depthstencil_format;
    opaque_pso_desc.InputLayout.pInputElementDescs = std_input_desc;
    opaque_pso_desc.InputLayout.NumElements = ARRAY_COUNT(std_input_desc);
    opaque_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaque_pso_desc.NumRenderTargets = 1;
    opaque_pso_desc.RTVFormats[0] = render_ctx->backbuffer_format;
    opaque_pso_desc.SampleDesc.Count = render_ctx->msaa4x_state ? 4 : 1;
    opaque_pso_desc.SampleDesc.Quality = render_ctx->msaa4x_state ? (render_ctx->msaa4x_quality - 1) : 0;

    render_ctx->device->CreateGraphicsPipelineState(&opaque_pso_desc, IID_PPV_ARGS(&render_ctx->psos[LAYER_OPAQUE]));
    //
    // -- Create PSO for Transparent objs
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC transparent_pso_desc = opaque_pso_desc;

    D3D12_RENDER_TARGET_BLEND_DESC transparency_blend_desc = {};
    transparency_blend_desc.BlendEnable = true;
    transparency_blend_desc.LogicOpEnable = false;
    transparency_blend_desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    transparency_blend_desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    transparency_blend_desc.BlendOp = D3D12_BLEND_OP_ADD;
    transparency_blend_desc.SrcBlendAlpha = D3D12_BLEND_ONE;
    transparency_blend_desc.DestBlendAlpha = D3D12_BLEND_ZERO;
    transparency_blend_desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    transparency_blend_desc.LogicOp = D3D12_LOGIC_OP_NOOP;
    transparency_blend_desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    transparent_pso_desc.BlendState.RenderTarget[0] = transparency_blend_desc;
    render_ctx->device->CreateGraphicsPipelineState(&transparent_pso_desc, IID_PPV_ARGS(&render_ctx->psos[LAYER_TRANSPARENT]));
    //
    // -- Create PSO for AlphaTested objs
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC alpha_pso_desc = opaque_pso_desc;
    alpha_pso_desc.PS.pShaderBytecode = render_ctx->shaders[SHADER_ALPHATEST_PS]->GetBufferPointer();
    alpha_pso_desc.PS.BytecodeLength = render_ctx->shaders[SHADER_ALPHATEST_PS]->GetBufferSize();
    alpha_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    render_ctx->device->CreateGraphicsPipelineState(&alpha_pso_desc, IID_PPV_ARGS(&render_ctx->psos[LAYER_ALPHATESTED]));

    //
    // -- Create PSO for Tree sprites
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC tree_pso_desc = opaque_pso_desc;
    tree_pso_desc.VS.pShaderBytecode = render_ctx->shaders[SHADER_TREE_VS]->GetBufferPointer();
    tree_pso_desc.VS.BytecodeLength = render_ctx->shaders[SHADER_TREE_VS]->GetBufferSize();
    tree_pso_desc.GS.pShaderBytecode = render_ctx->shaders[SHADER_TREE_GS]->GetBufferPointer();
    tree_pso_desc.GS.BytecodeLength = render_ctx->shaders[SHADER_TREE_GS]->GetBufferSize();
    tree_pso_desc.PS.pShaderBytecode = render_ctx->shaders[SHADER_TREE_PS]->GetBufferPointer();
    tree_pso_desc.PS.BytecodeLength = render_ctx->shaders[SHADER_TREE_PS]->GetBufferSize();
    tree_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    tree_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    tree_pso_desc.InputLayout.pInputElementDescs = treesprite_input_desc;
    tree_pso_desc.InputLayout.NumElements = ARRAY_COUNT(treesprite_input_desc);
    render_ctx->device->CreateGraphicsPipelineState(&tree_pso_desc, IID_PPV_ARGS(&render_ctx->psos[LAYER_ALPHATESTED_TREESPRITES]));

    //
    // -- Create PSO for drawing waves
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC waves_render_pso = transparent_pso_desc;
    waves_render_pso.VS.pShaderBytecode = render_ctx->shaders[SHADER_WAVE_VS]->GetBufferPointer();
    waves_render_pso.VS.BytecodeLength = render_ctx->shaders[SHADER_WAVE_VS]->GetBufferSize();
    render_ctx->device->CreateGraphicsPipelineState(&waves_render_pso, IID_PPV_ARGS(&render_ctx->psos[LAYER_GPU_WAVES_RENDER]));
    //
    // -- Create PSO for disturbing waves
    //
    D3D12_COMPUTE_PIPELINE_STATE_DESC waves_disturb_pso = {};
    waves_disturb_pso.pRootSignature = render_ctx->root_signature_waves;
    waves_disturb_pso.CS.pShaderBytecode = render_ctx->shaders[SHADER_DISTURB_WAVE_CS]->GetBufferPointer();
    waves_disturb_pso.CS.BytecodeLength = render_ctx->shaders[SHADER_DISTURB_WAVE_CS]->GetBufferSize();
    waves_disturb_pso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    render_ctx->device->CreateComputePipelineState(&waves_disturb_pso, IID_PPV_ARGS(&render_ctx->psos[LAYER_GPU_WAVES_DISTURB]));
    //
    // -- Create PSO for updating waves
    //
    D3D12_COMPUTE_PIPELINE_STATE_DESC waves_update_pso = {};
    waves_update_pso.pRootSignature = render_ctx->root_signature_waves;
    waves_update_pso.CS.pShaderBytecode = render_ctx->shaders[SHADER_UPDATE_WAVE_CS]->GetBufferPointer();
    waves_update_pso.CS.BytecodeLength = render_ctx->shaders[SHADER_UPDATE_WAVE_CS]->GetBufferSize();
    waves_update_pso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    render_ctx->device->CreateComputePipelineState(&waves_update_pso, IID_PPV_ARGS(&render_ctx->psos[LAYER_GPU_WAVES_UPDATE]));
    //
    // -- Create PSO for horizontal blur
    //
    D3D12_COMPUTE_PIPELINE_STATE_DESC hor_blur_pso = {};
    hor_blur_pso.pRootSignature = render_ctx->root_signature_postprocessing_blur;
    hor_blur_pso.CS.pShaderBytecode = render_ctx->shaders[SHADER_HOR_BLUR_CS]->GetBufferPointer();
    hor_blur_pso.CS.BytecodeLength = render_ctx->shaders[SHADER_HOR_BLUR_CS]->GetBufferSize();
    hor_blur_pso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    render_ctx->device->CreateComputePipelineState(&hor_blur_pso, IID_PPV_ARGS(&render_ctx->psos[LAYER_HOR_BLUR]));
    //
    // -- Create PSO for vertical blur
    //
    D3D12_COMPUTE_PIPELINE_STATE_DESC ver_blur_pso = {};
    ver_blur_pso.pRootSignature = render_ctx->root_signature_postprocessing_blur;
    ver_blur_pso.CS.pShaderBytecode = render_ctx->shaders[SHADER_VER_BLUR_CS]->GetBufferPointer();
    ver_blur_pso.CS.BytecodeLength = render_ctx->shaders[SHADER_VER_BLUR_CS]->GetBufferSize();
    ver_blur_pso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    render_ctx->device->CreateComputePipelineState(&ver_blur_pso, IID_PPV_ARGS(&render_ctx->psos[LAYER_VER_BLUR]));

    //
    // -- Create PSO for compositing post process
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC composite_pso = opaque_pso_desc;
    composite_pso.pRootSignature = render_ctx->root_signature_postprocessing_sobel;

    // disable depth test
    composite_pso.DepthStencilState.DepthEnable = false;
    composite_pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    composite_pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    composite_pso.VS.pShaderBytecode = render_ctx->shaders[SHADER_COMPOSITE_VS]->GetBufferPointer();
    composite_pso.VS.BytecodeLength = render_ctx->shaders[SHADER_COMPOSITE_VS]->GetBufferSize();

    composite_pso.PS.pShaderBytecode = render_ctx->shaders[SHADER_COMPOSITE_PS]->GetBufferPointer();
    composite_pso.PS.BytecodeLength = render_ctx->shaders[SHADER_COMPOSITE_PS]->GetBufferSize();

    render_ctx->device->CreateGraphicsPipelineState(&composite_pso, IID_PPV_ARGS(&render_ctx->psos[LAYER_COMPISITE]));

    //
    // -- Create PSO for sobel filter
    //
    D3D12_COMPUTE_PIPELINE_STATE_DESC sobel_pso = {};
    sobel_pso.pRootSignature = render_ctx->root_signature_postprocessing_sobel;
    sobel_pso.CS.pShaderBytecode = render_ctx->shaders[SHADER_SOBEL_CS]->GetBufferPointer();
    sobel_pso.CS.BytecodeLength = render_ctx->shaders[SHADER_SOBEL_CS]->GetBufferSize();
    sobel_pso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    render_ctx->device->CreateComputePipelineState(&sobel_pso, IID_PPV_ARGS(&render_ctx->psos[LAYER_SOBEL]));
}
static void
handle_keyboard_input (SceneContext * scene_ctx, GameTimer * gt) {
    float dt = gt->delta_time;

    if (GetAsyncKeyState('W') & 0x8000)
        Camera_Walk(global_camera, 10.0f * dt);

    if (GetAsyncKeyState('S') & 0x8000)
        Camera_Walk(global_camera, -10.0f * dt);

    if (GetAsyncKeyState('A') & 0x8000)
        Camera_Strafe(global_camera, -10.0f * dt);

    if (GetAsyncKeyState('D') & 0x8000)
        Camera_Strafe(global_camera, 10.0f * dt);

    Camera_UpdateViewMatrix(global_camera);
}
static void
handle_mouse_move (SceneContext * scene_ctx, WPARAM wParam, int x, int y) {
    if (global_mouse_active) {
        if ((wParam & MK_LBUTTON) != 0) {
            // make each pixel correspond to a quarter of a degree
            float dx = DirectX::XMConvertToRadians(0.25f * (float)(x - scene_ctx->mouse.x));
            float dy = DirectX::XMConvertToRadians(0.25f * (float)(y - scene_ctx->mouse.y));

            Camera_Pitch(global_camera, dy);
            Camera_RotateY(global_camera, dx);
        }
    }
    scene_ctx->mouse.x = x;
    scene_ctx->mouse.y = y;
}
static void
update_obj_cbuffers (D3DRenderContext * render_ctx) {
    UINT frame_index = render_ctx->frame_index;
    UINT cbuffer_size = sizeof(ObjectConstants);
    // Only update the cbuffer data if the constants have changed.  
    // This needs to be tracked per frame resource.
    for (unsigned i = 0; i < render_ctx->all_ritems.size; i++) {
        if (
            render_ctx->all_ritems.ritems[i].n_frames_dirty > 0 &&
            render_ctx->all_ritems.ritems[i].initialized
            ) {
            UINT obj_index = render_ctx->all_ritems.ritems[i].obj_cbuffer_index;
            XMMATRIX world = XMLoadFloat4x4(&render_ctx->all_ritems.ritems[i].world);
            XMMATRIX tex_transform = XMLoadFloat4x4(&render_ctx->all_ritems.ritems[i].tex_transform);

            ObjectConstants obj_cbuffer = {};
            XMStoreFloat4x4(&obj_cbuffer.world, XMMatrixTranspose(world));
            XMStoreFloat4x4(&obj_cbuffer.tex_transform, XMMatrixTranspose(tex_transform));
            obj_cbuffer.displacement_texel_size = render_ctx->all_ritems.ritems[i].displacement_map_texel_size;
            obj_cbuffer.grid_spatial_step = render_ctx->all_ritems.ritems[i].grid_spatial_step;

            // set mat index for dynamic indexing
            obj_cbuffer.mat_index = render_ctx->all_ritems.ritems[i].mat->mat_cbuffer_index;

            uint8_t * obj_ptr = render_ctx->frame_resources[frame_index].obj_cb_data_ptr + ((UINT64)obj_index * cbuffer_size);
            memcpy(obj_ptr, &obj_cbuffer, cbuffer_size);

            // Next FrameResource need to be updated too.
            render_ctx->all_ritems.ritems[i].n_frames_dirty--;
        }
    }
}
static void
update_mat_buffer (D3DRenderContext * render_ctx) {
    UINT frame_index = render_ctx->frame_index;
    size_t mat_data_size = sizeof(MaterialData);
    for (int i = 0; i < _COUNT_MATERIAL; ++i) {
        Material * mat = &render_ctx->materials[i];
        if (mat->n_frames_dirty > 0) {
            XMMATRIX mat_transform = XMLoadFloat4x4(&mat->mat_transform);

            MaterialData mat_data;
            mat_data.diffuse_albedo = render_ctx->materials[i].diffuse_albedo;
            mat_data.fresnel_r0 = render_ctx->materials[i].fresnel_r0;
            mat_data.roughness = render_ctx->materials[i].roughness;
            XMStoreFloat4x4(&mat_data.mat_transform, XMMatrixTranspose(mat_transform));
            mat_data.diffuse_map_index = mat->diffuse_srvheap_index;

            uint8_t * mat_ptr = render_ctx->frame_resources[frame_index].mat_data_buf_ptr + ((UINT64)mat->mat_cbuffer_index * mat_data_size);
            memcpy(mat_ptr, &mat_data, mat_data_size);

            // Next FrameResource need to be updated too.
            mat->n_frames_dirty--;
        }
    }
}
static void
update_pass_cbuffers (D3DRenderContext * render_ctx, GameTimer * timer) {

    XMMATRIX view = Camera_GetView(global_camera);
    XMMATRIX proj = Camera_GetProj(global_camera);

    XMMATRIX view_proj = XMMatrixMultiply(view, proj);
    XMVECTOR det_view = XMMatrixDeterminant(view);
    XMMATRIX inv_view = XMMatrixInverse(&det_view, view);
    XMVECTOR det_proj = XMMatrixDeterminant(proj);
    XMMATRIX inv_proj = XMMatrixInverse(&det_proj, proj);
    XMVECTOR det_view_proj = XMMatrixDeterminant(view_proj);
    XMMATRIX inv_view_proj = XMMatrixInverse(&det_view_proj, view_proj);

    XMStoreFloat4x4(&render_ctx->main_pass_constants.view, XMMatrixTranspose(view));
    XMStoreFloat4x4(&render_ctx->main_pass_constants.inverse_view, XMMatrixTranspose(inv_view));
    XMStoreFloat4x4(&render_ctx->main_pass_constants.proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&render_ctx->main_pass_constants.inverse_proj, XMMatrixTranspose(inv_proj));
    XMStoreFloat4x4(&render_ctx->main_pass_constants.view_proj, XMMatrixTranspose(view_proj));
    XMStoreFloat4x4(&render_ctx->main_pass_constants.inverse_view_proj, XMMatrixTranspose(inv_view_proj));
    render_ctx->main_pass_constants.eye_posw = Camera_GetPosition3f(global_camera);

    render_ctx->main_pass_constants.render_target_size = XMFLOAT2((float)global_scene_ctx.width, (float)global_scene_ctx.height);
    render_ctx->main_pass_constants.inverse_render_target_size = XMFLOAT2(1.0f / global_scene_ctx.width, 1.0f / global_scene_ctx.height);
    render_ctx->main_pass_constants.nearz = 1.0f;
    render_ctx->main_pass_constants.farz = 1000.0f;
    render_ctx->main_pass_constants.delta_time = timer->delta_time;
    render_ctx->main_pass_constants.total_time = Timer_GetTotalTime(timer);
    render_ctx->main_pass_constants.ambient_light = {.25f, .25f, .35f, 1.0f};

    render_ctx->main_pass_constants.lights[0].direction = {0.57735f, -0.57735f, 0.57735f};
    render_ctx->main_pass_constants.lights[0].strength = {0.6f, 0.6f, 0.6f};
    render_ctx->main_pass_constants.lights[1].direction = {-0.57735f, -0.57735f, 0.57735f};
    render_ctx->main_pass_constants.lights[1].strength = {0.3f, 0.3f, 0.3f};
    render_ctx->main_pass_constants.lights[2].direction = {0.0f, -0.707f, -0.707f};
    render_ctx->main_pass_constants.lights[2].strength = {0.15f, 0.15f, 0.15f};

    uint8_t * pass_ptr = render_ctx->frame_resources[render_ctx->frame_index].pass_cb_data_ptr;
    memcpy(pass_ptr, &render_ctx->main_pass_constants, sizeof(PassConstants));
}
static void
animate_material (Material * mat, GameTimer * timer) {
    // Scroll the water material texture coordinates.
    float& tu = mat->mat_transform(3, 0);
    float& tv = mat->mat_transform(3, 1);

    tu += 0.1f * timer->delta_time;
    tv += 0.02f * timer->delta_time;

    if (tu >= 1.0f)
        tu -= 1.0f;

    if (tv >= 1.0f)
        tv -= 1.0f;

    mat->mat_transform(3, 0) = tu;
    mat->mat_transform(3, 1) = tv;

    // Material has changed, so need to update cbuffer.
    mat->n_frames_dirty = NUM_QUEUING_FRAMES;
}
static void
update_waves_gpu (GpuWaves * waves, D3DRenderContext * render_ctx, GameTimer * timer) {
    float total_time = Timer_GetTotalTime(timer);
    float delta_time = timer->delta_time;

    // Every quarter second, generate a random wave.
    static float t_base = 0.0f;
    if ((total_time - t_base) >= 0.25f) {
        t_base += 0.25f;

        int i = rand_int(4, waves->nrow - 5);
        int j = rand_int(4, waves->ncol - 5);

        float r = rand_float(1.0f, 2.0f);

        GpuWaves_Disturb(
            waves, render_ctx->direct_cmd_list, render_ctx->root_signature_waves,
            render_ctx->psos[LAYER_GPU_WAVES_DISTURB], i, j, r
        );
    }

    // Update the wave simulation.
    GpuWaves_Update(
        waves, render_ctx->direct_cmd_list, render_ctx->root_signature_waves,
        render_ctx->psos[LAYER_GPU_WAVES_UPDATE], delta_time
    );
}
static HRESULT
move_to_next_frame (D3DRenderContext * render_ctx, UINT * out_frame_index, UINT * out_backbuffer_index) {

    HRESULT ret = E_FAIL;
    UINT frame_index = *out_frame_index;

    // -- 1. schedule a signal command in the queue
    UINT64 const current_fence_value = render_ctx->frame_resources[frame_index].fence;
    ret = render_ctx->cmd_queue->Signal(render_ctx->fence, current_fence_value);
    CHECK_AND_FAIL(ret);

    // -- 2. update frame index
    //*out_backbuffer_index = render_ctx->swapchain3->GetCurrentBackBufferIndex();
    *out_backbuffer_index = (*out_backbuffer_index + 1) % NUM_BACKBUFFERS;
    *out_frame_index = (render_ctx->frame_index + 1) % NUM_QUEUING_FRAMES;

    // -- 3. if the next frame is not ready to be rendered yet, wait until it is ready
    if (render_ctx->fence->GetCompletedValue() < render_ctx->frame_resources[frame_index].fence) {
        ret = render_ctx->fence->SetEventOnCompletion(render_ctx->frame_resources[frame_index].fence, render_ctx->fence_event);
        CHECK_AND_FAIL(ret);
        WaitForSingleObjectEx(render_ctx->fence_event, INFINITE /*return only when the object is signaled*/, false);
    }

    // -- 3. set the fence value for the next frame
    render_ctx->frame_resources[frame_index].fence = current_fence_value + 1;

    return ret;
}
static void
flush_command_queue (D3DRenderContext * render_ctx) {
    // Advance the fence value to mark commands up to this fence point.
    render_ctx->main_current_fence++;

    // Add an instruction to the command queue to set a new fence point.  Because we 
    // are on the GPU timeline, the new fence point won't be set until the GPU finishes
    // processing all the commands prior to this Signal().
    render_ctx->cmd_queue->Signal(render_ctx->fence, render_ctx->main_current_fence);

    // Wait until the GPU has completed commands up to this fence point.
    if (render_ctx->fence->GetCompletedValue() < render_ctx->main_current_fence) {
        HANDLE event_handle = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        // Fire event when GPU hits current fence.  
        render_ctx->fence->SetEventOnCompletion(render_ctx->main_current_fence, event_handle);

        // Wait until the GPU hits current fence event is fired.
        if (event_handle != 0) {
            WaitForSingleObject(event_handle, INFINITE);
            CloseHandle(event_handle);
        }
    }
}
static void
draw_fullscreen_quad (ID3D12GraphicsCommandList * cmdlist, RenderItem * dummy_ritem) {
    // using dummy buffer to suppress EXECUTION WARNING #202: COMMAND_LIST_DRAW_VERTEX_BUFFER_NOT_SET
    D3D12_VERTEX_BUFFER_VIEW vbv = Mesh_GetVertexBufferView(dummy_ritem->geometry);

    // -- null out IA stage since we build the vertex off the SV_VertexID in the shader
    cmdlist->IASetVertexBuffers(0, 1, &vbv);
    cmdlist->IASetIndexBuffer(nullptr);
    cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmdlist->DrawInstanced(6, 1, 0, 0);
}
static HRESULT
draw_main (D3DRenderContext * render_ctx, GpuWaves * waves, BlurFilter * blur_filter, UINT blur_count) {
    HRESULT ret = E_FAIL;
    UINT frame_index = render_ctx->frame_index;
    UINT backbuffer_index = render_ctx->backbuffer_index;
    ID3D12Resource * backbuffer = render_ctx->render_targets[backbuffer_index];
    ID3D12GraphicsCommandList * cmdlist = render_ctx->direct_cmd_list;

    // Populate command list

    // -- reset cmd_allocator and cmd_list
    render_ctx->frame_resources[frame_index].cmd_list_alloc->Reset();

    // When ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ret = cmdlist->Reset(render_ctx->frame_resources[frame_index].cmd_list_alloc, render_ctx->psos[LAYER_OPAQUE]);

    ID3D12DescriptorHeap * descriptor_heaps [] = {render_ctx->srv_heap};
    cmdlist->SetDescriptorHeaps(_countof(descriptor_heaps), descriptor_heaps);

    update_waves_gpu(waves, render_ctx, &global_timer);

    cmdlist->SetPipelineState(render_ctx->psos[LAYER_OPAQUE]);

    // -- set viewport and scissor
    cmdlist->RSSetViewports(1, &render_ctx->viewport);
    cmdlist->RSSetScissorRects(1, &render_ctx->scissor_rect);

    // -- indicate that the backbuffer will be used as the render target
    resource_usage_transition(
        cmdlist,
        backbuffer,
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );

    // -- get CPU descriptor handle that represents the start of the rtv heap
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = render_ctx->dsv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = render_ctx->rtv_heap->GetCPUDescriptorHandleForHeapStart();
    rtv_handle.ptr = SIZE_T(INT64(rtv_handle.ptr) + INT64(render_ctx->backbuffer_index) * INT64(render_ctx->rtv_descriptor_size));    // -- apply initial offset

    cmdlist->ClearRenderTargetView(rtv_handle, (float *)&render_ctx->main_pass_constants.fog_color, 0, nullptr);
    cmdlist->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    cmdlist->OMSetRenderTargets(1, &rtv_handle, true, &dsv_handle);

    cmdlist->SetGraphicsRootSignature(render_ctx->root_signature);

    // Bind per-pass constant buffer.  We only need to do this once per-pass.
    ID3D12Resource * pass_cb = render_ctx->frame_resources[frame_index].pass_cb;
    cmdlist->SetGraphicsRootConstantBufferView(1, pass_cb->GetGPUVirtualAddress());

    // Bind all materials. For structured buffers, we can bypass heap and set a root descriptor
    ID3D12Resource * mat_buf = render_ctx->frame_resources[frame_index].mat_data_buf;
    cmdlist->SetGraphicsRootShaderResourceView(2, mat_buf->GetGPUVirtualAddress());

    // Bind all textures. We only specify the first descriptor in the table
    // Root sig knows how many descriptors we have in the table
    cmdlist->SetGraphicsRootDescriptorTable(3, render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart());

     // set displacement map for waves
    cmdlist->SetGraphicsRootDescriptorTable(4, waves->curr_sol_hgpu_srv);

    // 1. draw opaque objs first (opaque pso is currently used)
    draw_render_items(
        cmdlist,
        render_ctx->frame_resources[frame_index].obj_cb,
        render_ctx->cbv_srv_uav_descriptor_size,
        &render_ctx->opaque_ritems, frame_index
    );
    // 2. draw alpha-tested box/crate
    cmdlist->SetPipelineState(render_ctx->psos[LAYER_ALPHATESTED]);
    draw_render_items(
        cmdlist,
        render_ctx->frame_resources[frame_index].obj_cb,
        render_ctx->cbv_srv_uav_descriptor_size,
        &render_ctx->alphatested_ritems, frame_index
    );
    // 3. draw tree-sprites
    cmdlist->SetPipelineState(render_ctx->psos[LAYER_ALPHATESTED_TREESPRITES]);
    draw_render_items(
        cmdlist,
        render_ctx->frame_resources[frame_index].obj_cb,
        render_ctx->cbv_srv_uav_descriptor_size,
        &render_ctx->alphatested_treesprites_ritems, frame_index
    );
    // 4. draw gpu waves
    cmdlist->SetPipelineState(render_ctx->psos[LAYER_GPU_WAVES_RENDER]);
    draw_render_items(
        cmdlist,
        render_ctx->frame_resources[frame_index].obj_cb,
        render_ctx->cbv_srv_uav_descriptor_size,
        &render_ctx->gpu_waves_rtimes, frame_index
    );

    if (blur_count > 0) {
        //
        // Blur compute work
        //
        BlurFilter_Execute(
            blur_filter, cmdlist,
            render_ctx->root_signature_postprocessing_blur,
            render_ctx->psos[LAYER_HOR_BLUR],
            render_ctx->psos[LAYER_VER_BLUR],
            backbuffer,
            blur_count
        );
        // -- prepare to copy blurred output to the backbuffer
        resource_usage_transition(cmdlist, backbuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

        cmdlist->CopyResource(
            backbuffer,
            blur_filter->blur_map0
        );

        if (global_imgui_enabled) {
            resource_usage_transition(cmdlist, backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdlist);
            resource_usage_transition(cmdlist, backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        } else {
            resource_usage_transition(cmdlist, backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
        }
    } else if (blur_count == 0) {
        if (global_imgui_enabled)
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdlist);

        // -- indicate that the backbuffer will now be used to present
        resource_usage_transition(cmdlist, backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    }

    // -- finish populating command list
    cmdlist->Close();

    ID3D12CommandList * cmd_lists [] = {cmdlist};
    render_ctx->cmd_queue->ExecuteCommandLists(ARRAY_COUNT(cmd_lists), cmd_lists);

    render_ctx->swapchain->Present(1 /*sync interval*/, 0 /*present flag*/);

    return ret;
}

static HRESULT
draw_stylized (
    D3DRenderContext * render_ctx,
    GpuWaves * waves,
    OffscreenRenderTarget * ort,
    BlurFilter * blur_filter, UINT blur_count,
    SobelFilter * sobel_filter
) {
    HRESULT ret = E_FAIL;

#if 0


    UINT frame_index = render_ctx->frame_index;
    UINT backbuffer_index = render_ctx->backbuffer_index;
    ID3D12Resource * backbuffer = render_ctx->render_targets[backbuffer_index];
    ID3D12GraphicsCommandList * cmdlist = render_ctx->direct_cmd_list;

    // Populate command list

    // -- reset cmd_allocator and cmd_list
    render_ctx->frame_resources[frame_index].cmd_list_alloc->Reset();

    ret = cmdlist->Reset(render_ctx->frame_resources[frame_index].cmd_list_alloc, render_ctx->psos[LAYER_OPAQUE]);

    ID3D12DescriptorHeap * descriptor_heaps [] = {render_ctx->srv_heap};
    cmdlist->SetDescriptorHeaps(_countof(descriptor_heaps), descriptor_heaps);

    update_waves_gpu(waves, render_ctx, &global_timer);

    cmdlist->SetPipelineState(render_ctx->psos[LAYER_OPAQUE]);

    // -- set viewport and scissor
    cmdlist->RSSetViewports(1, &render_ctx->viewport);
    cmdlist->RSSetScissorRects(1, &render_ctx->scissor_rect);

    // -- change offscreen texture to be used as a render-target output
    resource_usage_transition(
        cmdlist,
        ort->texture,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );

    // -- get CPU descriptor handle that represents the start of the rtv heap
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = render_ctx->dsv_heap->GetCPUDescriptorHandleForHeapStart();

    cmdlist->ClearRenderTargetView(ort->hcpu_rtv, (float *)&render_ctx->main_pass_constants.fog_color, 0, nullptr);
    cmdlist->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // -- specify the buffers we are going to render to
    cmdlist->OMSetRenderTargets(1, &ort->hcpu_rtv, true, &dsv_handle);

    cmdlist->SetGraphicsRootSignature(render_ctx->root_signature);

    // Bind per-pass constant buffer.  We only need to do this once per-pass.
    ID3D12Resource * pass_cb = render_ctx->frame_resources[frame_index].pass_cb;
    cmdlist->SetGraphicsRootConstantBufferView(2, pass_cb->GetGPUVirtualAddress());
    cmdlist->SetGraphicsRootDescriptorTable(4, waves->curr_sol_hgpu_srv); // set displacement map

    // 1. draw opaque objs first (opaque pso is currently used)
    draw_render_items(
        cmdlist,
        render_ctx->frame_resources[frame_index].obj_cb,
        render_ctx->frame_resources[frame_index].mat_cb,
        render_ctx->cbv_srv_uav_descriptor_size,
        render_ctx->srv_heap,
        &render_ctx->opaque_ritems, frame_index
    );
    // 2. draw alpha-tested box/crate
    cmdlist->SetPipelineState(render_ctx->psos[LAYER_ALPHATESTED]);
    draw_render_items(
        cmdlist,
        render_ctx->frame_resources[frame_index].obj_cb,
        render_ctx->frame_resources[frame_index].mat_cb,
        render_ctx->cbv_srv_uav_descriptor_size,
        render_ctx->srv_heap,
        &render_ctx->alphatested_ritems, frame_index
    );
    // 3. draw tree-sprites
    cmdlist->SetPipelineState(render_ctx->psos[LAYER_ALPHATESTED_TREESPRITES]);
    draw_render_items(
        cmdlist,
        render_ctx->frame_resources[frame_index].obj_cb,
        render_ctx->frame_resources[frame_index].mat_cb,
        render_ctx->cbv_srv_uav_descriptor_size,
        render_ctx->srv_heap,
        &render_ctx->alphatested_treesprites_ritems, frame_index
    );
    // 4. draw gpu waves
    cmdlist->SetPipelineState(render_ctx->psos[LAYER_GPU_WAVES_RENDER]);
    draw_render_items(
        cmdlist,
        render_ctx->frame_resources[frame_index].obj_cb,
        render_ctx->frame_resources[frame_index].mat_cb,
        render_ctx->cbv_srv_uav_descriptor_size,
        render_ctx->srv_heap,
        &render_ctx->gpu_waves_rtimes, frame_index
    );

    //
    // Sobel compute work
    //

    // -- change the offscreen rendertarget to be used as input
    resource_usage_transition(cmdlist, ort->texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);

    SobelFilter_Execute(sobel_filter, cmdlist, render_ctx->root_signature_postprocessing_sobel, render_ctx->psos[LAYER_SOBEL], ort->hgpu_srv);

    //
    // Switching back to backbuffer rendering
    //
    resource_usage_transition(cmdlist, backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = render_ctx->rtv_heap->GetCPUDescriptorHandleForHeapStart();
    rtv_handle.ptr = SIZE_T(INT64(rtv_handle.ptr) + INT64(render_ctx->backbuffer_index) * INT64(render_ctx->rtv_descriptor_size));    // -- apply offset
    cmdlist->OMSetRenderTargets(1, &rtv_handle, true, &dsv_handle);

    cmdlist->SetGraphicsRootSignature(render_ctx->root_signature_postprocessing_sobel);
    cmdlist->SetPipelineState(render_ctx->psos[LAYER_COMPISITE]);
    cmdlist->SetGraphicsRootDescriptorTable(0, ort->hgpu_srv);
    cmdlist->SetGraphicsRootDescriptorTable(1, sobel_filter->hgpu_srv);
    draw_fullscreen_quad(cmdlist, &render_ctx->opaque_ritems.ritems[0]);

    if (blur_count > 0) {
        //
        // Blur compute work
        //
        BlurFilter_Execute(
            blur_filter, cmdlist,
            render_ctx->root_signature_postprocessing_blur,
            render_ctx->psos[LAYER_HOR_BLUR],
            render_ctx->psos[LAYER_VER_BLUR],
            backbuffer,
            blur_count
        );
        // -- prepare to copy blurred output to the backbuffer
        resource_usage_transition(cmdlist, backbuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

        cmdlist->CopyResource(
            backbuffer,
            blur_filter->blur_map0
        );

        if (global_imgui_enabled) {
            resource_usage_transition(cmdlist, backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdlist);
            resource_usage_transition(cmdlist, backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        } else {
            resource_usage_transition(cmdlist, backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
        }
    } else if (blur_count == 0) {
        if (global_imgui_enabled)
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdlist);

        // -- indicate that the backbuffer will now be used to present
        resource_usage_transition(cmdlist, backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    }

    // -- finish populating command list
    cmdlist->Close();

    ID3D12CommandList * cmd_lists [] = {cmdlist};
    render_ctx->cmd_queue->ExecuteCommandLists(ARRAY_COUNT(cmd_lists), cmd_lists);

    render_ctx->swapchain->Present(1 /*sync interval*/, 0 /*present flag*/);

#endif // 0

    return ret;
}

static void
SceneContext_Init (SceneContext * scene_ctx, int w, int h) {
    _ASSERT_EXPR(scene_ctx, _T("scene_ctx not valid"));
    memset(scene_ctx, 0, sizeof(SceneContext));

    scene_ctx->width = w;
    scene_ctx->height = h;
    scene_ctx->sun_theta = 1.25f * XM_PI;
    scene_ctx->sun_phi = XM_PIDIV4;
    scene_ctx->aspect_ratio = (float)scene_ctx->width / (float)scene_ctx->height;
}
static void
RenderContext_Init (D3DRenderContext * render_ctx) {
    _ASSERT_EXPR(render_ctx, _T("render-ctx not valid"));
    memset(render_ctx, 0, sizeof(D3DRenderContext));

    render_ctx->viewport.TopLeftX = 0;
    render_ctx->viewport.TopLeftY = 0;
    render_ctx->viewport.Width = (float)global_scene_ctx.width;
    render_ctx->viewport.Height = (float)global_scene_ctx.height;
    render_ctx->viewport.MinDepth = 0.0f;
    render_ctx->viewport.MaxDepth = 1.0f;
    render_ctx->scissor_rect.left = 0;
    render_ctx->scissor_rect.top = 0;
    render_ctx->scissor_rect.right = global_scene_ctx.width;
    render_ctx->scissor_rect.bottom = global_scene_ctx.height;

    // -- initialize fog data
    render_ctx->main_pass_constants.fog_color = {0.7f, 0.7f, 0.7f, 1.0f};
    render_ctx->main_pass_constants.fog_start = 5.0f;
    render_ctx->main_pass_constants.fog_range = 150.0f;

    // -- initialize light data
    render_ctx->main_pass_constants.lights[0].strength = {.5f,.5f,.5f};
    render_ctx->main_pass_constants.lights[0].falloff_start = 1.0f;
    render_ctx->main_pass_constants.lights[0].direction = {0.0f, -1.0f, 0.0f};
    render_ctx->main_pass_constants.lights[0].falloff_end = 10.0f;
    render_ctx->main_pass_constants.lights[0].position = {0.0f, 0.0f, 0.0f};
    render_ctx->main_pass_constants.lights[0].spot_power = 64.0f;

    render_ctx->main_pass_constants.lights[1].strength = {.5f,.5f,.5f};
    render_ctx->main_pass_constants.lights[1].falloff_start = 1.0f;
    render_ctx->main_pass_constants.lights[1].direction = {0.0f, -1.0f, 0.0f};
    render_ctx->main_pass_constants.lights[1].falloff_end = 10.0f;
    render_ctx->main_pass_constants.lights[1].position = {0.0f, 0.0f, 0.0f};
    render_ctx->main_pass_constants.lights[1].spot_power = 64.0f;

    render_ctx->main_pass_constants.lights[2].strength = {.5f,.5f,.5f};
    render_ctx->main_pass_constants.lights[2].falloff_start = 1.0f;
    render_ctx->main_pass_constants.lights[2].direction = {0.0f, -1.0f, 0.0f};
    render_ctx->main_pass_constants.lights[2].falloff_end = 10.0f;
    render_ctx->main_pass_constants.lights[2].position = {0.0f, 0.0f, 0.0f};
    render_ctx->main_pass_constants.lights[2].spot_power = 64.0f;

    // -- specify formats
    render_ctx->backbuffer_format   = DXGI_FORMAT_R8G8B8A8_UNORM;
    render_ctx->depthstencil_format = DXGI_FORMAT_D24_UNORM_S8_UINT;

    // -- 4x MSAA enabled ?
    render_ctx->msaa4x_state = false;
    _ASSERT_EXPR(false == render_ctx->msaa4x_state, _T("Don't enable 4x MSAA for now"));
}
static void
d3d_resize (D3DRenderContext * render_ctx, BlurFilter * blur, SobelFilter * sobel, OffscreenRenderTarget * ort) {
    int w = global_scene_ctx.width;
    int h = global_scene_ctx.height;

    if (render_ctx &&
        render_ctx->device &&
        render_ctx->direct_cmd_list_alloc &&
        render_ctx->swapchain
        ) {
        // Flush before changing any resources.
        flush_command_queue(render_ctx);

        render_ctx->direct_cmd_list->Reset(render_ctx->direct_cmd_list_alloc, nullptr);

        // Release the previous resources we will be recreating.
        for (int i = 0; i < NUM_BACKBUFFERS; ++i)
            render_ctx->render_targets[i]->Release();
        render_ctx->depth_stencil_buffer->Release();

        // Resize the swap chain.
        render_ctx->swapchain->ResizeBuffers(
            NUM_BACKBUFFERS,
            w, h,
            render_ctx->backbuffer_format,
            DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
        );

        render_ctx->backbuffer_index = 0;

        D3D12_CPU_DESCRIPTOR_HANDLE rtv_heap_handle = render_ctx->rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < NUM_BACKBUFFERS; i++) {
            render_ctx->swapchain->GetBuffer(i, IID_PPV_ARGS(&render_ctx->render_targets[i]));
            render_ctx->device->CreateRenderTargetView(render_ctx->render_targets[i], nullptr, rtv_heap_handle);
            rtv_heap_handle.ptr += render_ctx->rtv_descriptor_size;
        }

        // Create the depth/stencil buffer and view.
        D3D12_RESOURCE_DESC depth_stencil_desc;
        depth_stencil_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depth_stencil_desc.Alignment = 0;
        depth_stencil_desc.Width = w;
        depth_stencil_desc.Height = h;
        depth_stencil_desc.DepthOrArraySize = 1;
        depth_stencil_desc.MipLevels = 1;

        // NOTE(omid): Note that we create the depth buffer resource with a typeless format.  
        depth_stencil_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
        depth_stencil_desc.SampleDesc.Count = render_ctx->msaa4x_state ? 4 : 1;
        depth_stencil_desc.SampleDesc.Quality = render_ctx->msaa4x_state ? (render_ctx->msaa4x_quality - 1) : 0;
        depth_stencil_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depth_stencil_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE opt_clear;
        opt_clear.Format = render_ctx->depthstencil_format;
        opt_clear.DepthStencil.Depth = 1.0f;
        opt_clear.DepthStencil.Stencil = 0;

        D3D12_HEAP_PROPERTIES def_heap = {};
        def_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        def_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        def_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        def_heap.CreationNodeMask = 1;
        def_heap.VisibleNodeMask = 1;
        render_ctx->device->CreateCommittedResource(
            &def_heap,
            D3D12_HEAP_FLAG_NONE,
            &depth_stencil_desc,
            D3D12_RESOURCE_STATE_COMMON,
            &opt_clear,
            IID_PPV_ARGS(&render_ctx->depth_stencil_buffer)
        );

        // Create descriptor to mip level 0 of entire resource using the format of the resource.
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
        dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsv_desc.Format = render_ctx->depthstencil_format;
        dsv_desc.Texture2D.MipSlice = 0;
        render_ctx->device->CreateDepthStencilView(render_ctx->depth_stencil_buffer, &dsv_desc, render_ctx->dsv_heap->GetCPUDescriptorHandleForHeapStart());

        // Transition the resource from its initial state to be used as a depth buffer.
        resource_usage_transition(render_ctx->direct_cmd_list, render_ctx->depth_stencil_buffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        // Execute the resize commands.
        render_ctx->direct_cmd_list->Close();
        ID3D12CommandList* cmds_list [] = {render_ctx->direct_cmd_list};
        render_ctx->cmd_queue->ExecuteCommandLists(_countof(cmds_list), cmds_list);

        // Wait until resize is complete.
        flush_command_queue(render_ctx);

        // Update the viewport transform to cover the client area.
        render_ctx->viewport.TopLeftX = 0;
        render_ctx->viewport.TopLeftY = 0;
        render_ctx->viewport.Width    = static_cast<float>(w);
        render_ctx->viewport.Height   = static_cast<float>(h);
        render_ctx->viewport.MinDepth = 0.0f;
        render_ctx->viewport.MaxDepth = 1.0f;

        render_ctx->scissor_rect = {0, 0, w, h};

        // The window resized, so update the aspect ratio
        global_scene_ctx.aspect_ratio = static_cast<float>(w) / h;

        // blur filter resize
        if (blur)
            BlurFilter_Resize(blur, w, h);

        // sobel filter resize
        if (sobel)
            SobelFilter_Resize(sobel, w, h);

        // ort filter resize
        if (ort) {
            OffscreenRenderTarget_Resize(ort, w, h);
        }
    }

    Camera_SetLens(global_camera, 0.25f * XM_PI, global_scene_ctx.aspect_ratio, 1.0f, 1000.0f);
}
static void
check_active_item () {
    if (ImGui::IsItemActive() || ImGui::IsItemHovered())
        global_mouse_active = false;
    else
        global_mouse_active = true;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT CALLBACK
main_win_cb (HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Handle imgui window
    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
        return true;

    // Handle passed user data (render_ctx)
    D3DRenderContext * _render_ctx = nullptr;
    if (uMsg == WM_CREATE) {
        CREATESTRUCT * ptr_create = reinterpret_cast<CREATESTRUCT *>(lParam);
        _render_ctx = reinterpret_cast<D3DRenderContext *>(ptr_create->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)_render_ctx);
    } else {
        LONG_PTR ptr = GetWindowLongPtr(hwnd, GWLP_USERDATA);
        _render_ctx = reinterpret_cast<D3DRenderContext *>(ptr);
    }

    LRESULT ret = 0;
    switch (uMsg) {

    case WM_ACTIVATE: {
        if (LOWORD(wParam) == WA_INACTIVE) {
            global_paused = true;
            Timer_Stop(&global_timer);
        } else {
            global_paused = false;
            Timer_Start(&global_timer);
        }
    } break;

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN: {
        global_scene_ctx.mouse.x = GET_X_LPARAM(lParam);
        global_scene_ctx.mouse.y = GET_Y_LPARAM(lParam);
        SetCapture(hwnd);
    } break;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP: {
        ReleaseCapture();
    } break;
    case WM_MOUSEMOVE: {
        handle_mouse_move(&global_scene_ctx, wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    } break;
    case WM_SIZE: {
        global_scene_ctx.width = LOWORD(lParam);
        global_scene_ctx.height = HIWORD(lParam);
        if (_render_ctx) {
            if (wParam == SIZE_MINIMIZED) {
                global_paused = true;
            } else if (wParam == SIZE_MAXIMIZED) {
                global_paused = false;
                d3d_resize(_render_ctx, global_blur_filter, &global_sobel_filter, &global_offscreen_rendertarget);
            } else if (wParam == SIZE_RESTORED) {
                // TODO(omid): handle restore from minimize/maximize 
                if (global_resizing) {
                    // don't do nothing until resizing finished
                } else {
                    d3d_resize(_render_ctx, global_blur_filter, &global_sobel_filter, &global_offscreen_rendertarget);
                }
            }
        }
    } break;
    // WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
    case WM_ENTERSIZEMOVE: {
        global_paused = true;
        global_resizing  = true;
        Timer_Stop(&global_timer);
    } break;
    // WM_EXITSIZEMOVE is sent when the user releases the resize bars.
    // Here we reset everything based on the new window dimensions.
    case WM_EXITSIZEMOVE: {
        global_paused = false;
        global_resizing  = false;
        Timer_Start(&global_timer);
        d3d_resize(_render_ctx, global_blur_filter, &global_sobel_filter, &global_offscreen_rendertarget);
    } break;
    case WM_DESTROY: {
        PostQuitMessage(0);
    } break;
    // Catch this message so to prevent the window from becoming too small.
    case WM_GETMINMAXINFO:
    {
        ((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
        ((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
    }
    break;
    default: {
        ret = DefWindowProc(hwnd, uMsg, wParam, lParam);
    } break;
    }
    return ret;
}
INT WINAPI
WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ INT) {

    SceneContext_Init(&global_scene_ctx, 1280, 720);
    D3DRenderContext * render_ctx = (D3DRenderContext *)::malloc(sizeof(D3DRenderContext));
    RenderContext_Init(render_ctx);

    // Camera Initial Setup
    size_t cam_size = Camera_CalculateRequiredSize();
    global_camera = (Camera *)malloc(cam_size);
    Camera_Init(global_camera);
    Camera_SetPosition(global_camera, 10.0f, 5.0f, -45.0f);

    // ========================================================================================================
#pragma region Windows_Setup
    WNDCLASS wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = main_win_cb;
    wc.hInstance = hInstance;
    wc.lpszClassName = _T("d3d12_win32");

    _ASSERT_EXPR(RegisterClass(&wc), _T("could not register window class"));

    // Compute window rectangle dimensions based on requested client area dimensions.
    RECT R = {0, 0, (long int)global_scene_ctx.width, (long int)global_scene_ctx.height};
    AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
    int width  = R.right - R.left;
    int height = R.bottom - R.top;

    HWND hwnd = CreateWindowEx(
        0,                                              // Optional window styles.
        wc.lpszClassName,                               // Window class
        _T("First-Person Camera & Dynamic Indexing app"),                    // Window title
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,               // Window style
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,    // Size and position settings
        0 /* Parent window */, 0 /* Menu */, hInstance  /* Instance handle */,
        render_ctx                                      /* Additional application data */
    );

    _ASSERT_EXPR(hwnd, _T("could not create window"));

#pragma endregion Windows_Setup

    // ========================================================================================================
#pragma region Enable_Debug_Layer
    UINT dxgiFactoryFlags = 0;
#if ENABLE_DEBUG_LAYER > 0
    ID3D12Debug * debug_interface_dx = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface_dx)))) {
        debug_interface_dx->EnableDebugLayer();
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif
#pragma endregion Enable_Debug_Layer

    // ========================================================================================================
#pragma region Initialization

    // Query Adapter (PhysicalDevice)
    IDXGIFactory4 * dxgi_factory = nullptr;
    CHECK_AND_FAIL(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgi_factory)));
    //CHECK_AND_FAIL(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi_factory)));

    uint32_t const MaxAdapters = 8;
    IDXGIAdapter * adapters[MaxAdapters] = {};
    IDXGIAdapter * pAdapter;
    for (UINT i = 0; dxgi_factory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        adapters[i] = pAdapter;
        DXGI_ADAPTER_DESC adapter_desc = {};
        ::printf("GPU Info [%d] :\n", i);
        if (SUCCEEDED(pAdapter->GetDesc(&adapter_desc))) {
            ::printf("\tDescription: %ls\n", adapter_desc.Description);
            ::printf("\tDedicatedVideoMemory: %zu\n", adapter_desc.DedicatedVideoMemory);
        }
    } // WARP -> Windows Advanced Rasterization ...

    // Create Logical Device
    auto res = D3D12CreateDevice(adapters[0], D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&render_ctx->device));
    CHECK_AND_FAIL(res);

    // Release adaptors
    for (unsigned i = 0; i < MaxAdapters; ++i) {
        if (adapters[i] != nullptr) {
            adapters[i]->Release();
        }
    }
    // store CBV_SRV_UAV descriptor increment size
    render_ctx->cbv_srv_uav_descriptor_size = render_ctx->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // store RTV descriptor increment size
    render_ctx->rtv_descriptor_size = render_ctx->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Check 4X MSAA quality support for our back buffer format.
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality_levels;
    quality_levels.Format = render_ctx->backbuffer_format;
    quality_levels.SampleCount = 4;
    quality_levels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    quality_levels.NumQualityLevels = 0;
    render_ctx->device->CheckFeatureSupport(
        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
        &quality_levels,
        sizeof(quality_levels)
    );
    render_ctx->msaa4x_quality = quality_levels.NumQualityLevels;
    _ASSERT_EXPR(render_ctx->msaa4x_quality > 0, _T("Unexpected MSAA quality level."));

#pragma region Create Command Objects
    // Create Command Queue
    D3D12_COMMAND_QUEUE_DESC cmd_q_desc = {};
    cmd_q_desc.Type = D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT;
    cmd_q_desc.Flags = D3D12_COMMAND_QUEUE_FLAGS::D3D12_COMMAND_QUEUE_FLAG_NONE;
    render_ctx->device->CreateCommandQueue(&cmd_q_desc, IID_PPV_ARGS(&render_ctx->cmd_queue));

    // Create Command Allocator
    render_ctx->device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&render_ctx->direct_cmd_list_alloc)
    );

    // Create Command List
    if (render_ctx->direct_cmd_list_alloc) {
        render_ctx->device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            render_ctx->direct_cmd_list_alloc,
            render_ctx->psos[LAYER_OPAQUE], IID_PPV_ARGS(&render_ctx->direct_cmd_list)
        );

        // Reset the command list to prep for initialization commands.
        // NOTE(omid): Command list needs to be closed before calling Reset.
        render_ctx->direct_cmd_list->Close();
        render_ctx->direct_cmd_list->Reset(render_ctx->direct_cmd_list_alloc, nullptr);
    }
#pragma endregion

    DXGI_MODE_DESC backbuffer_desc = {};
    backbuffer_desc.Width = global_scene_ctx.width;
    backbuffer_desc.Height = global_scene_ctx.height;
    backbuffer_desc.Format = render_ctx->backbuffer_format;
    backbuffer_desc.RefreshRate.Numerator = 60;
    backbuffer_desc.RefreshRate.Denominator = 1;
    backbuffer_desc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    backbuffer_desc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;

    DXGI_SAMPLE_DESC sampler_desc = {};
    if (render_ctx->msaa4x_state) {
        sampler_desc.Count = 1;
        sampler_desc.Quality = 0;
    } else {
        sampler_desc.Count = render_ctx->msaa4x_state ? 4 : 1;
        sampler_desc.Quality = render_ctx->msaa4x_state ? (render_ctx->msaa4x_quality - 1) : 0;
    }

    // Create Swapchain
    DXGI_SWAP_CHAIN_DESC swapchain_desc = {};
    swapchain_desc.BufferDesc = backbuffer_desc;
    swapchain_desc.SampleDesc = sampler_desc;
    swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain_desc.BufferCount = NUM_BACKBUFFERS;
    swapchain_desc.OutputWindow = hwnd;
    swapchain_desc.Windowed = TRUE;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    if (render_ctx->cmd_queue)
        CHECK_AND_FAIL(dxgi_factory->CreateSwapChain(render_ctx->cmd_queue, &swapchain_desc, &render_ctx->swapchain));

// ========================================================================================================
#pragma region Load Textures
    // crate
    strcpy_s(render_ctx->textures[TEX_CRATE01].name, "woodcrate01");
    wcscpy_s(render_ctx->textures[TEX_CRATE01].filename, L"../Textures/WoodCrate02.dds");
    load_texture(
        render_ctx->device, render_ctx->direct_cmd_list,
        render_ctx->textures[TEX_CRATE01].filename,
        &render_ctx->textures[TEX_CRATE01]
    );
    // water
    strcpy_s(render_ctx->textures[TEX_WATER].name, "watertex");
    wcscpy_s(render_ctx->textures[TEX_WATER].filename, L"../Textures/water1.dds");
    load_texture(
        render_ctx->device, render_ctx->direct_cmd_list,
        render_ctx->textures[TEX_WATER].filename,
        &render_ctx->textures[TEX_WATER]
    );
    // grass
    strcpy_s(render_ctx->textures[TEX_GRASS].name, "grasstex");
    wcscpy_s(render_ctx->textures[TEX_GRASS].filename, L"../Textures/grass.dds");
    load_texture(
        render_ctx->device, render_ctx->direct_cmd_list,
        render_ctx->textures[TEX_GRASS].filename,
        &render_ctx->textures[TEX_GRASS]
    );
    // wire_fence
    strcpy_s(render_ctx->textures[TEX_WIREFENCE].name, "wirefencetex");
    wcscpy_s(render_ctx->textures[TEX_WIREFENCE].filename, L"../Textures/WireFence.dds");
    load_texture(
        render_ctx->device, render_ctx->direct_cmd_list,
        render_ctx->textures[TEX_WIREFENCE].filename,
        &render_ctx->textures[TEX_WIREFENCE]
    );
    // tree_array
    strcpy_s(render_ctx->textures[TEX_TREEARRAY].name, "treearraytex");
    wcscpy_s(render_ctx->textures[TEX_TREEARRAY].filename, L"../Textures/treeArray2.dds");
    load_texture(
        render_ctx->device, render_ctx->direct_cmd_list,
        render_ctx->textures[TEX_TREEARRAY].filename,
        &render_ctx->textures[TEX_TREEARRAY]
    );
#pragma endregion

    // Waves Initial Setup
    uint32_t const nrow = 256;
    uint32_t const ncols = 256;
    uint32_t const N_VTX = nrow * ncols;
    BYTE * wave_memory = (BYTE *)::malloc(sizeof(GpuWaves));
    GpuWaves * waves = GpuWaves_Init(
        wave_memory,
        render_ctx->direct_cmd_list,
        render_ctx->device,
        nrow, ncols, 0.25f, 0.03f, 2.0f, 0.2f
    );

    // Blur Initial Setup
    size_t blur_size = BlurFilter_CalculateRequiredSize();
    BYTE * blur_memory = (BYTE *)::malloc(blur_size);
    global_blur_filter = BlurFilter_Init(
        blur_memory,
        render_ctx->device, global_scene_ctx.width, global_scene_ctx.height,
        DXGI_FORMAT_R8G8B8A8_UNORM
    );

    // Offscreen RenderTarget setup
    OffscreenRenderTarget_Init(
        &global_offscreen_rendertarget,
        render_ctx->device, global_scene_ctx.width, global_scene_ctx.height,
        render_ctx->backbuffer_format,
        (float *)&render_ctx->main_pass_constants.fog_color
    );

    // Sobel initial setup
    SobelFilter_Init(
        &global_sobel_filter,
        render_ctx->device, global_scene_ctx.width, global_scene_ctx.height,
        render_ctx->backbuffer_format
    );

    create_descriptor_heaps(render_ctx, waves, global_blur_filter, &global_sobel_filter, &global_offscreen_rendertarget);

#pragma region Dsv_Creation
// Create the depth/stencil buffer and view.
    D3D12_RESOURCE_DESC ds_desc;
    ds_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    ds_desc.Alignment = 0;
    ds_desc.Width = global_scene_ctx.width;
    ds_desc.Height = global_scene_ctx.height;
    ds_desc.DepthOrArraySize = 1;
    ds_desc.MipLevels = 1;

    // NOTE(omid): SSAO requires an SRV to the depth buffer to read from 
    // the depth buffer.  Therefore, because we need to create two views to the same resource:
    //   1. SRV format: DXGI_FORMAT_R24_UNORM_X8_TYPELESS
    //   2. DSV Format: DXGI_FORMAT_D24_UNORM_S8_UINT
    // we need to create the depth buffer resource with a typeless format.  
    ds_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    ds_desc.SampleDesc.Count = render_ctx->msaa4x_state ? 4 : 1;
    ds_desc.SampleDesc.Quality = render_ctx->msaa4x_state ? (render_ctx->msaa4x_quality - 1) : 0;
    ds_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ds_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES ds_heap_props = {};
    ds_heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    ds_heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    ds_heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    ds_heap_props.CreationNodeMask = 1;
    ds_heap_props.VisibleNodeMask = 1;

    D3D12_CLEAR_VALUE opt_clear;
    opt_clear.Format = render_ctx->depthstencil_format;
    opt_clear.DepthStencil.Depth = 1.0f;
    opt_clear.DepthStencil.Stencil = 0;
    render_ctx->device->CreateCommittedResource(
        &ds_heap_props,
        D3D12_HEAP_FLAG_NONE,
        &ds_desc,
        D3D12_RESOURCE_STATE_COMMON,
        &opt_clear,
        IID_PPV_ARGS(&render_ctx->depth_stencil_buffer)
    );

    // Create descriptor to mip level 0 of entire resource using the format of the resource.
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Format = render_ctx->depthstencil_format;
    dsv_desc.Texture2D.MipSlice = 0;
    render_ctx->device->CreateDepthStencilView(
        render_ctx->depth_stencil_buffer,
        &dsv_desc,
        render_ctx->dsv_heap->GetCPUDescriptorHandleForHeapStart()
    );
#pragma endregion Dsv_Creation

#pragma region Create RTV
    // -- create frame resources: rtv for each frame
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle_start = render_ctx->rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < NUM_BACKBUFFERS; ++i) {
        render_ctx->swapchain->GetBuffer(i, IID_PPV_ARGS(&render_ctx->render_targets[i]));
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = {};
        cpu_handle.ptr = rtv_handle_start.ptr + ((UINT64)i * render_ctx->rtv_descriptor_size);
        // -- create a rtv for each frame
        render_ctx->device->CreateRenderTargetView(render_ctx->render_targets[i], nullptr, cpu_handle);
    }
#pragma endregion

#pragma region Create CBuffers MaterialData Buffers
    UINT obj_cb_size = sizeof(ObjectConstants);
    UINT mat_data_size = sizeof(MaterialData);
    UINT pass_cb_size = sizeof(PassConstants);
    for (UINT i = 0; i < NUM_QUEUING_FRAMES; ++i) {
        // -- create a cmd-allocator for each frame
        res = render_ctx->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&render_ctx->frame_resources[i].cmd_list_alloc));

        // -- create cbuffers as upload_buffer
        create_upload_buffer(render_ctx->device, (UINT64)obj_cb_size * _COUNT_RENDERITEM, &render_ctx->frame_resources[i].obj_cb_data_ptr, &render_ctx->frame_resources[i].obj_cb);
        // Initialize cb data
        ::memcpy(render_ctx->frame_resources[i].obj_cb_data_ptr, &render_ctx->frame_resources[i].obj_cb_data, sizeof(render_ctx->frame_resources[i].obj_cb_data));

        create_upload_buffer(render_ctx->device, (UINT64)mat_data_size * _COUNT_MATERIAL, &render_ctx->frame_resources[i].mat_data_buf_ptr, &render_ctx->frame_resources[i].mat_data_buf);
        // Initialize cb data
        ::memcpy(render_ctx->frame_resources[i].mat_data_buf_ptr, &render_ctx->frame_resources[i].mat_data, sizeof(render_ctx->frame_resources[i].mat_data));

        create_upload_buffer(render_ctx->device, pass_cb_size * 1, &render_ctx->frame_resources[i].pass_cb_data_ptr, &render_ctx->frame_resources[i].pass_cb);
        // Initialize cb data
        ::memcpy(render_ctx->frame_resources[i].pass_cb_data_ptr, &render_ctx->frame_resources[i].pass_cb_data, sizeof(render_ctx->frame_resources[i].pass_cb_data));
    }
#pragma endregion

    // ========================================================================================================
#pragma region Root_Signature_Creation
    create_root_signature(render_ctx->device, &render_ctx->root_signature);
    create_root_signature_gpuwaves(render_ctx->device, &render_ctx->root_signature_waves);
    create_root_signature_postprocessing_blur(render_ctx->device, &render_ctx->root_signature_postprocessing_blur);
    create_root_signature_postprocessing_sobel(render_ctx->device, &render_ctx->root_signature_postprocessing_sobel);
#pragma endregion Root_Signature_Creation

    // Load and compile shaders

#pragma region Compile Shaders
    TCHAR shaders_path [] = _T("./shaders/default.hlsl");
    TCHAR tree_shader_path []= _T("./shaders/tree_sprite.hlsl");
    TCHAR wavesim_shader_path []= _T("./shaders/wave_sim.hlsl");
    TCHAR blur_shader_path [] = _T("./shaders/blur.hlsl");
    TCHAR sobel_shader_path []= _T("./shaders/sobel.hlsl");
    TCHAR composite_shader_path [] = _T("./shaders/composite.hlsl");

    {   // standard shaders
        compile_shader(shaders_path, _T("VertexShader_Main"), _T("vs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_DEFAULT_VS]);

        int const n_define_wave = 1;
        DxcDefine defines_wave[n_define_wave] = {};
        defines_wave[0] = {.Name = _T("DISPLACEMENT_MAP"), .Value = _T("1")};
        compile_shader(shaders_path, _T("VertexShader_Main"), _T("vs_6_0"), defines_wave, n_define_wave, &render_ctx->shaders[SHADER_WAVE_VS]);

        int const n_define_fog = 1;
        DxcDefine defines_fog[n_define_fog] = {};
        defines_fog[0] = {.Name = _T("FOG"), .Value = _T("1")};
        compile_shader(shaders_path, _T("PixelShader_Main"), _T("ps_6_0"), defines_fog, n_define_fog, &render_ctx->shaders[SHADER_OPAQUE_PS]);

        int const n_define_alphatest = 2;
        DxcDefine defines_alphatest[n_define_alphatest] = {};
        defines_alphatest[0] = {.Name = _T("FOG"), .Value = _T("1")};
        defines_alphatest[1] = {.Name = _T("ALPHA_TEST"), .Value = _T("1")};
        compile_shader(shaders_path, _T("PixelShader_Main"), _T("ps_6_0"), defines_alphatest, n_define_alphatest, &render_ctx->shaders[SHADER_ALPHATEST_PS]);
    }
    {   // tree-sprite shaders
        compile_shader(tree_shader_path, _T("VS_Main"), _T("vs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_TREE_VS]);
        compile_shader(tree_shader_path, _T("GS_Main"), _T("gs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_TREE_GS]);

        int const n_define_alphatest = 2;
        DxcDefine defines_alphatest[n_define_alphatest] = {};
        defines_alphatest[0] = {.Name = _T("FOG"), .Value = _T("1")};
        defines_alphatest[1] = {.Name = _T("ALPHA_TEST"), .Value = _T("1")};
        compile_shader(tree_shader_path, _T("PS_Main"), _T("ps_6_0"), defines_alphatest, n_define_alphatest, &render_ctx->shaders[SHADER_TREE_PS]);
    }
    {   // wave compute shaders
        compile_shader(wavesim_shader_path, _T("update_wave_cs"), _T("cs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_UPDATE_WAVE_CS]);
        compile_shader(wavesim_shader_path, _T("disturb_wave_cs"), _T("cs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_DISTURB_WAVE_CS]);
    }

    {   // blur compute-shaders
        compile_shader(blur_shader_path, _T("horizontal_blur_cs"), _T("cs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_HOR_BLUR_CS]);
        compile_shader(blur_shader_path, _T("vertical_blur_cs"), _T("cs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_VER_BLUR_CS]);
    }
    {   // composite shaders (vertex shader and pixel shader)
        compile_shader(composite_shader_path, _T("composite_vs"), _T("vs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_COMPOSITE_VS]);
        compile_shader(composite_shader_path, _T("composite_ps"), _T("ps_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_COMPOSITE_PS]);
    }
    {   // sobel compute-shader
        compile_shader(sobel_shader_path, _T("sobel_cs"), _T("cs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_SOBEL_CS]);
    }
#pragma endregion Compile Shaders

    create_pso(render_ctx);

#pragma region Shapes_And_Renderitem_Creation
    create_land_geometry(render_ctx);
    create_water_geometry(waves->nrow, waves->ncol, waves->ntri, render_ctx);
    create_treesprites_geometry(render_ctx);
    create_shape_geometry(render_ctx);
    create_materials(render_ctx->materials);
    create_render_items(render_ctx, waves);

#pragma endregion Shapes_And_Renderitem_Creation

    // NOTE(omid): Before closing/executing command list specify the depth-stencil-buffer transition from its initial state to be used as a depth buffer.
    resource_usage_transition(render_ctx->direct_cmd_list, render_ctx->depth_stencil_buffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    // -- close the command list and execute it to begin inital gpu setup
    CHECK_AND_FAIL(render_ctx->direct_cmd_list->Close());
    ID3D12CommandList * cmd_lists [] = {render_ctx->direct_cmd_list};
    render_ctx->cmd_queue->ExecuteCommandLists(ARRAY_COUNT(cmd_lists), cmd_lists);

    //----------------
    // Create fence
    // create synchronization objects and wait until assets have been uploaded to the GPU.

    UINT frame_index = render_ctx->frame_index;
    CHECK_AND_FAIL(render_ctx->device->CreateFence(render_ctx->frame_resources[frame_index].fence, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&render_ctx->fence)));

    ++render_ctx->frame_resources[frame_index].fence;

    // Create an event handle to use for frame synchronization.
    render_ctx->fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (nullptr == render_ctx->fence_event) {
        // map the error code to an HRESULT value.
        CHECK_AND_FAIL(HRESULT_FROM_WIN32(GetLastError()));
    }

    // Wait for the command list to execute; 
    // we just want to wait for setup to complete before continuing.
    flush_command_queue(render_ctx);

#pragma endregion

#pragma region Imgui Setup
    bool stylized_sobel = false;
    bool * ptr_open = nullptr;
    ImGuiWindowFlags window_flags = 0;
    bool beginwnd, sliderf, coloredit, sliderint;
    int i_box_mat = 0;
    int blur_count = 0;
    if (global_imgui_enabled) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.Fonts->AddFontDefault();
        ImGui::StyleColorsDark();

        // calculate imgui cpu & gpu handles on location on srv_heap
        D3D12_CPU_DESCRIPTOR_HANDLE imgui_cpu_handle = render_ctx->srv_heap->GetCPUDescriptorHandleForHeapStart();
        imgui_cpu_handle.ptr += (render_ctx->cbv_srv_uav_descriptor_size * (
            _COUNT_TEX +
            6 +     /* GpuWaves descriptors */
            4 +     /* Blur descriptors     */
            2 +     /* Sobel descriptors    */
            1       /* Offscreen RenderTarget descriptor */
            ));

        D3D12_GPU_DESCRIPTOR_HANDLE imgui_gpu_handle = render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
        imgui_gpu_handle.ptr += (render_ctx->cbv_srv_uav_descriptor_size * (
            _COUNT_TEX +
            6 +     /* GpuWaves descriptors */
            4 +     /* Blur descriptors     */
            2 +     /* Sobel descriptors    */
            1       /* Offscreen RenderTarget descriptor */
            ));

            // Setup Platform/Renderer backends
        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX12_Init(
            render_ctx->device, NUM_QUEUING_FRAMES,
            render_ctx->backbuffer_format, render_ctx->srv_heap,
            imgui_cpu_handle,
            imgui_gpu_handle
        );

        // Setup imgui window flags
        window_flags |= ImGuiWindowFlags_NoScrollbar;
        window_flags |= ImGuiWindowFlags_MenuBar;
        //window_flags |= ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoCollapse;
        window_flags |= ImGuiWindowFlags_NoNav;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
        //window_flags |= ImGuiWindowFlags_NoResize;

    }
#pragma endregion

        // ========================================================================================================
#pragma region Main_Loop
    global_paused = false;
    global_resizing = false;
    global_mouse_active = true;
    Timer_Init(&global_timer);
    Timer_Reset(&global_timer);

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        } else {
#pragma region Imgui window
            if (global_imgui_enabled) {
                ImGui_ImplDX12_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                ImGui::Begin("Settings", ptr_open, window_flags);
                beginwnd = ImGui::IsItemActive();

                ImGui::SliderFloat(
                    "Fog Distance",
                    &render_ctx->main_pass_constants.fog_start,
                    5.0f,
                    150.0f
                );
                sliderf = ImGui::IsItemActive();

                ImGui::ColorEdit3("Fog Color", (float*)&render_ctx->main_pass_constants.fog_color);
                coloredit = ImGui::IsItemActive();

                ImGui::Combo("Box Material", &i_box_mat, "   Wood\0   Wire-fenced\0\0");

                ImGui::SliderInt(
                    "Blur Filters",
                    &blur_count,
                    0,
                    10
                );
                sliderint = ImGui::IsItemActive();

                ImGui::Checkbox("Draw Stylized (Sobel Filter)", &stylized_sobel);

                ImGui::Text("\nUse \'W\' \'S\' for Walk, \'A\' \'D\' for Strafing");

                ImGui::Text("\n");
                ImGui::Separator();
                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

                ImGui::End();
                ImGui::Render();

                // choose box material
                if (0 == i_box_mat)
                    render_ctx->alphatested_ritems.ritems[0].mat = &render_ctx->materials[MAT_WOOD_CRATE];
                else if (1 == i_box_mat)
                    render_ctx->alphatested_ritems.ritems[0].mat = &render_ctx->materials[MAT_WIRED_CRATE];
                // control mouse activation
                global_mouse_active = !(beginwnd || sliderf || coloredit || sliderint);
            }
#pragma endregion
            Timer_Tick(&global_timer);

            if (!global_paused) {
                handle_keyboard_input(&global_scene_ctx, &global_timer);

                animate_material(&render_ctx->materials[MAT_WATER], &global_timer);
                update_obj_cbuffers(render_ctx);
                update_mat_buffer(render_ctx);
                update_pass_cbuffers(render_ctx, &global_timer);

                if (!stylized_sobel) {
                    CHECK_AND_FAIL(draw_main(render_ctx, waves, global_blur_filter, blur_count));
                } else if (false) {
                    CHECK_AND_FAIL(draw_stylized(render_ctx, waves, &global_offscreen_rendertarget, global_blur_filter, blur_count, &global_sobel_filter));
                }
                CHECK_AND_FAIL(move_to_next_frame(render_ctx, &render_ctx->frame_index, &render_ctx->backbuffer_index));
            } else {
                Sleep(100);
            }
        }
    }
#pragma endregion

    // ========================================================================================================
#pragma region Cleanup_And_Debug
    flush_command_queue(render_ctx);

    // Cleanup Imgui
    if (global_imgui_enabled) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    // release queuing frame resources
    for (size_t i = 0; i < NUM_QUEUING_FRAMES; i++) {
        flush_command_queue(render_ctx);    // TODO(omid): Address the cbuffers release issue 
        render_ctx->frame_resources[i].obj_cb->Unmap(0, nullptr);
        render_ctx->frame_resources[i].mat_data_buf->Unmap(0, nullptr);
        render_ctx->frame_resources[i].pass_cb->Unmap(0, nullptr);
        render_ctx->frame_resources[i].obj_cb->Release();
        render_ctx->frame_resources[i].mat_data_buf->Release();
        render_ctx->frame_resources[i].pass_cb->Release();

        render_ctx->frame_resources[i].cmd_list_alloc->Release();
    }
    CloseHandle(render_ctx->fence_event);
    render_ctx->fence->Release();

    for (unsigned i = 0; i < _COUNT_GEOM; i++) {
        render_ctx->geom[i].ib_uploader->Release();
        render_ctx->geom[i].vb_uploader->Release();
        render_ctx->geom[i].vb_gpu->Release();
        render_ctx->geom[i].ib_gpu->Release();
    }   // is this a bug in d3d12sdklayers.dll ?

    for (int i = 0; i < _COUNT_RENDERCOMPUTE_LAYER; ++i)
        render_ctx->psos[i]->Release();

    for (unsigned i = 0; i < _COUNT_SHADERS; ++i)
        render_ctx->shaders[i]->Release();

    render_ctx->root_signature_postprocessing_sobel->Release();
    render_ctx->root_signature_postprocessing_blur->Release();
    render_ctx->root_signature_waves->Release();
    render_ctx->root_signature->Release();

    SobelFilter_Deinit(&global_sobel_filter);
    OffscreenRenderTarget_Deinit(&global_offscreen_rendertarget);

    BlurFilter_Deinit(global_blur_filter);
    ::free(blur_memory);

    GpuWaves_Deinit(waves);
    ::free(wave_memory);

    // release swapchain backbuffers resources
    for (unsigned i = 0; i < NUM_BACKBUFFERS; ++i)
        render_ctx->render_targets[i]->Release();

    render_ctx->dsv_heap->Release();
    render_ctx->rtv_heap->Release();
    render_ctx->srv_heap->Release();

    render_ctx->depth_stencil_buffer->Release();

    for (unsigned i = 0; i < _COUNT_TEX; i++) {
        render_ctx->textures[i].upload_heap->Release();
        render_ctx->textures[i].resource->Release();
    }

    //render_ctx->swapchain3->Release();
    render_ctx->swapchain->Release();
    render_ctx->direct_cmd_list->Release();
    render_ctx->direct_cmd_list_alloc->Release();
    render_ctx->cmd_queue->Release();
    render_ctx->device->Release();
    dxgi_factory->Release();


#if (ENABLE_DEBUG_LAYER > 0)
    debug_interface_dx->Release();
#endif

// -- advanced debugging and reporting live objects [from https://walbourn.github.io/dxgi-debug-device/]

    typedef HRESULT (WINAPI * LPDXGIGETDEBUGINTERFACE)(REFIID, void **);

    //HMODULE dxgidebug_dll = LoadLibraryEx( L"dxgidebug_dll.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
    HMODULE dxgidebug_dll = LoadLibrary(L"DXGIDebug.dll");
    if (dxgidebug_dll) {
        auto dxgiGetDebugInterface = reinterpret_cast<LPDXGIGETDEBUGINTERFACE>(
            reinterpret_cast<void*>(GetProcAddress(dxgidebug_dll, "DXGIGetDebugInterface")));

        IDXGIDebug1 * dxgi_debugger = nullptr;
        DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debugger));
        dxgi_debugger->ReportLiveObjects(
            DXGI_DEBUG_ALL,
            DXGI_DEBUG_RLO_DETAIL
            /* DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL) */
        );
        dxgi_debugger->Release();
        FreeLibrary(dxgidebug_dll);

        // -- consume var to avoid warning
        dxgiGetDebugInterface = dxgiGetDebugInterface;
    }

    ::free(global_camera);

#pragma endregion Cleanup_And_Debug

    return 0;
}


