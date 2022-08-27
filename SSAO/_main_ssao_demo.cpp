/* ===========================================================
   #File: _main_ssao_demo.cpp #
   #Date: 24 June 2021 #
   #Revision: 1.0 #
   #Creator: Omid Miresmaeili #
   #Description: Screen Space Ambient Occlusion Demo #
   # Reworking C21 Demo from http://www.d3dcoder.net/d3d12.htm #
   #Notice: (C) Copyright 2021 by Omid. All Rights Reserved. #
   =========================================================== */

#include "headers/common.h"

#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <dxgidebug.h>

#include <dxcapi.h>

#include "headers/utils.h"
#include "headers/game_timer.h"
#include "headers/dds_loader.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_dx12.h>

#include <time.h>

#include "camera.h"
#include "shadow_map.h"
#include "ssao.h"

#define ENABLE_DEARIMGUI

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

#define NUM_BACKBUFFERS         2
#define NUM_QUEUING_FRAMES      3

#if defined(ENABLE_DEARIMGUI)
bool g_imgui_enabled = true;
#else
bool g_imgui_enabled = false;
#endif // defined(ENABLE_DEARIMGUI)

enum RENDER_LAYER : int {
    LAYER_OPAQUE = 0,
    LAYER_DEBUG_SMAP = 1,
    LAYER_DEBUG_SSAO = 2,
    LAYER_SHADOW_OPAQUE,
    LAYER_SKY,
    LAYER_DRAW_NORMALS,
    LAYER_SSAO,
    LAYER_SSAO_BLUR,

    _COUNT_RENDERCOMPUTE_LAYER
};
enum ALL_RENDERITEMS {
    RITEM_GLOBE = 0,
    RITEM_SKY = 1,
    RITEM_BOX = 2,
    RITEM_GRID = 3,
    RITEM_SKULL = 4,
    RITEM_QUAD_SMAP = 5,
    RITEM_QUAD_SSAO = 6,

    // NOTE(omid): following indices are meaningless. DON'T use them! 
    RITEM_CYLENDER0 = 7,
    RITEM_CYLENDER1,
    RITEM_CYLENDER2,
    RITEM_CYLENDER3,
    RITEM_CYLENDER4,
    RITEM_CYLENDER5,
    RITEM_CYLENDER6,
    RITEM_CYLENDER7,
    RITEM_CYLENDER8,
    RITEM_CYLENDER9,
    RITEM_SPHERE0 = 17,
    RITEM_SPHERE1,
    RITEM_SPHERE2,
    RITEM_SPHERE3,
    RITEM_SPHERE4,
    RITEM_SPHERE5,
    RITEM_SPHERE6,
    RITEM_SPHERE7,
    RITEM_SPHERE8,
    RITEM_SPHERE9,

    _COUNT_RENDERITEM
};
static_assert(27 == _COUNT_RENDERITEM, _T("invalid render items count"));
enum SHADERS_CODE {
    SHADER_STANDARD_VS = 0,
    SHADER_OPAQUE_PS = 1,
    SHADER_SKY_VS = 2,
    SHADER_SKY_PS = 3,
    SHADER_SHADOW_VS = 4,
    SHADER_SHADOW_OPAQUE_PS = 5,
    SHADER_SHADOW_ALPHATESTED_PS = 6,
    SHADER_DEBUG_SMAP_VS,
    SHADER_DEBUG_SMAP_PS,
    SHADER_DEBUG_SSAO_VS,
    SHADER_DEBUG_SSAO_PS,
    SHADER_DRAW_NORMALS_VS,
    SHADER_DRAW_NORMALS_PS,
    SHADER_SSAO_VS,
    SHADER_SSAO_PS,
    SHADER_SSAO_BLUR_VS,
    SHADER_SSAO_BLUR_PS,

    _COUNT_SHADERS
};
enum GEOM_INDEX {
    GEOM_SKULL = 0,
    GEOM_SHAPES = 1,

    _COUNT_GEOM
};
enum SUBMESH_INDEX {
    _BOX_ID,
    _GRID_ID,
    _SPHERE_ID,
    _CYLINDER_ID,
    _QUAD_ID
};
enum MAT_INDEX {
    MAT_BRICK = 0,
    MAT_TILE = 1,
    MAT_MIRROR = 2,
    MAT_SKULL = 3,
    MAT_SKY = 4,

    _COUNT_MATERIAL
};
enum TEX_INDEX {
    BRICK_DIFFUSE_MAP = 0,
    BRICK_NORMAL_MAP,

    TILE_DIFFUSE_MAP,
    TILE_NORMAL_MAP,

    WHITE1x1_DIFFUSE_MAP,
    WHITE1x1_NORMAL_MAP,

    TEX_SKY_CUBEMAP0 = 6,
    TEX_SKY_CUBEMAP1,
    TEX_SKY_CUBEMAP2,
    TEX_SKY_CUBEMAP3,

    _COUNT_TEX
};
enum SAMPLER_INDEX {
    SAMPLER_POINT_WRAP = 0,
    SAMPLER_POINT_CLAMP = 1,
    SAMPLER_LINEAR_WRAP = 2,
    SAMPLER_LINEAR_CLAMP = 3,
    SAMPLER_ANISOTROPIC_WRAP = 4,
    SAMPLER_ANISOTROPIC_CLAMP = 5,
    SAMPLER_SHADOW,

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

    //
    // Light data for dynamic shadow
    float light_nearz;
    float light_farz;
    XMFLOAT3 light_pos_w;
    XMFLOAT4X4 light_view_mat;
    XMFLOAT4X4 light_proj_mat;
    XMFLOAT4X4 shadow_transform;

    float light_rotation_angle;
    XMFLOAT3 base_light_dirs[3];
    XMFLOAT3 rotated_light_dirs[3];

    DirectX::BoundingSphere scene_bounds;
};

Camera * g_camera;
ShadowMap * g_smap;
SSAO * g_ssao;
GameTimer g_timer;
bool g_paused;
bool g_resizing;
bool g_mouse_active;
SceneContext g_scene_ctx;

//
// global ui params
bool g_ssao_enabled = true;
bool g_dir_light_enabled = true;
float g_accessiblity_power = 4.0f;
float g_occlusion_addend = 0.1f;
bool g_show_smap_debug = false;
bool g_show_ssao_debug = false;

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
    ID3D12RootSignature *           root_signature_ssao;
    ID3D12PipelineState *           psos[_COUNT_RENDERCOMPUTE_LAYER];

    // Command objects
    ID3D12CommandQueue *            cmd_queue;
    ID3D12CommandAllocator *        direct_cmd_list_alloc;
    ID3D12GraphicsCommandList *     direct_cmd_list;

    UINT                            rtv_descriptor_size;
    UINT                            dsv_descriptor_size;
    UINT                            cbv_srv_uav_descriptor_size;

    ID3D12DescriptorHeap *          rtv_heap;
    ID3D12DescriptorHeap *          dsv_heap;
    ID3D12DescriptorHeap *          srv_heap;

    UINT                            sky_tex_heap_index;
    UINT                            shadow_map_heap_index;

    UINT                            ssao_heap_index_start;
    UINT                            ssao_ambient_map_index;

    UINT                            null_cube_srv_index;
    UINT                            null_tex_srv_index1;
    UINT                            null_tex_srv_index2;


    D3D12_GPU_DESCRIPTOR_HANDLE     null_srv;

    PassConstants                   main_pass_constants;
    PassConstants                   shadow_pass_constants;

    UINT                            pass_cbv_offset;

    // List of all the render items.
    RenderItemArray                 all_ritems;
    // Render items divided by PSO.
    RenderItemArray                 opaque_ritems;
    RenderItemArray                 environment_ritems;
    RenderItemArray                 debug_ritems_smap;
    RenderItemArray                 debug_ritems_ssao;

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
    strcpy_s(out_materials[MAT_BRICK].name, "bricks");
    out_materials[MAT_BRICK].mat_cbuffer_index = 0;
    out_materials[MAT_BRICK].diffuse_srvheap_index = BRICK_DIFFUSE_MAP;
    out_materials[MAT_BRICK].normal_srvheap_index = BRICK_NORMAL_MAP;
    out_materials[MAT_BRICK].diffuse_albedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    out_materials[MAT_BRICK].fresnel_r0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    out_materials[MAT_BRICK].roughness = 0.3f;
    out_materials[MAT_BRICK].mat_transform = Identity4x4();
    out_materials[MAT_BRICK].n_frames_dirty = NUM_QUEUING_FRAMES;

    strcpy_s(out_materials[MAT_TILE].name, "tile");
    out_materials[MAT_TILE].mat_cbuffer_index = 1;
    out_materials[MAT_TILE].diffuse_srvheap_index = TILE_DIFFUSE_MAP;
    out_materials[MAT_TILE].normal_srvheap_index = TILE_NORMAL_MAP;
    out_materials[MAT_TILE].diffuse_albedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
    out_materials[MAT_TILE].fresnel_r0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
    out_materials[MAT_TILE].roughness = 0.1f;
    out_materials[MAT_TILE].mat_transform = Identity4x4();
    out_materials[MAT_TILE].n_frames_dirty = NUM_QUEUING_FRAMES;

    strcpy_s(out_materials[MAT_MIRROR].name, "mirror");
    out_materials[MAT_MIRROR].mat_cbuffer_index = 2;
    out_materials[MAT_MIRROR].diffuse_srvheap_index = WHITE1x1_DIFFUSE_MAP;
    out_materials[MAT_MIRROR].normal_srvheap_index = WHITE1x1_NORMAL_MAP;
    out_materials[MAT_MIRROR].diffuse_albedo = XMFLOAT4(0.0f, 0.0f, 0.1f, 1.0f);
    out_materials[MAT_MIRROR].fresnel_r0 = XMFLOAT3(0.98f, 0.97f, 0.95f);
    out_materials[MAT_MIRROR].roughness = 0.1f;
    out_materials[MAT_MIRROR].mat_transform = Identity4x4();
    out_materials[MAT_MIRROR].n_frames_dirty = NUM_QUEUING_FRAMES;

    strcpy_s(out_materials[MAT_SKULL].name, "skull");
    out_materials[MAT_SKULL].mat_cbuffer_index = 3;
    out_materials[MAT_SKULL].diffuse_srvheap_index = WHITE1x1_DIFFUSE_MAP;
    out_materials[MAT_SKULL].normal_srvheap_index = WHITE1x1_NORMAL_MAP;
    out_materials[MAT_SKULL].diffuse_albedo = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
    out_materials[MAT_SKULL].fresnel_r0 = XMFLOAT3(0.6f, 0.6f, 0.6f);
    out_materials[MAT_SKULL].roughness = 0.2f;
    out_materials[MAT_SKULL].mat_transform = Identity4x4();
    out_materials[MAT_SKULL].n_frames_dirty = NUM_QUEUING_FRAMES;

    strcpy_s(out_materials[MAT_SKY].name, "sky");
    out_materials[MAT_SKY].mat_cbuffer_index = 4;
    out_materials[MAT_SKY].diffuse_srvheap_index = TEX_SKY_CUBEMAP0;
    out_materials[MAT_SKY].diffuse_albedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    out_materials[MAT_SKY].fresnel_r0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    out_materials[MAT_SKY].roughness = 1.0f;
    out_materials[MAT_SKY].mat_transform = Identity4x4();
    out_materials[MAT_SKY].n_frames_dirty = NUM_QUEUING_FRAMES;
}
static void
create_skull_geometry (D3DRenderContext * render_ctx) {
#pragma region Read_Data_File
    FILE * f = nullptr;
    errno_t err = fopen_s(&f, "./models/skull.txt", "r");
    if (0 == f || err != 0) {
        MessageBox(0, _T("Could not open file"), 0, 0);
        return;
    }
    char linebuf[100];
    int cnt = 0;
    unsigned vcount = 0;
    unsigned tcount = 0;
    // -- read 1st line
    if (fgets(linebuf, sizeof(linebuf), f))
        cnt = sscanf_s(linebuf, "%*s %d", &vcount);
    if (cnt != 1) {
        MessageBox(0, _T("Read error"), 0, 0);
        return;
    }
    // -- read 2nd line
    if (fgets(linebuf, sizeof(linebuf), f))
        cnt = sscanf_s(linebuf, "%*s %d", &tcount);
    if (cnt != 1) {
        MessageBox(0, _T("Read error"), 0, 0);
        return;
    }
    // -- skip two lines
    fgets(linebuf, sizeof(linebuf), f);
    fgets(linebuf, sizeof(linebuf), f);

    // -- vmin and vmax for AABB construction
    XMFLOAT3 vminf3(+FLT_MAX, +FLT_MAX, +FLT_MAX);
    XMFLOAT3 vmaxf3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    XMVECTOR vmin = XMLoadFloat3(&vminf3);
    XMVECTOR vmax = XMLoadFloat3(&vmaxf3);

    // -- read vertices
    Vertex * vertices = (Vertex *)calloc(vcount, sizeof(Vertex));
    for (unsigned i = 0; i < vcount; i++) {
        fgets(linebuf, sizeof(linebuf), f);
        cnt = sscanf_s(
            linebuf, "%f %f %f %f %f %f",
            &vertices[i].position.x, &vertices[i].position.y, &vertices[i].position.z,
            &vertices[i].normal.x, &vertices[i].normal.y, &vertices[i].normal.z
        );

        // generating tangent vector
        vertices[i].texc = {0.0f, 0.0f};
        XMVECTOR P = XMLoadFloat3(&vertices[i].position);
        XMVECTOR N = XMLoadFloat3(&vertices[i].normal);
        // NOTE(omid): We aren't applying a texture map to the skull,
        // so we just need any tangent vector
        // so that the math works out to give us the original interpolated vertex normal.
        XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        if (fabsf(XMVectorGetX(XMVector3Dot(N, up))) < 1.0f - 0.001f) {
            XMVECTOR T = XMVector3Normalize(XMVector3Cross(up, N));
            XMStoreFloat3(&vertices[i].tangent_u, T);
        } else {
            up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
            XMVECTOR T = XMVector3Normalize(XMVector3Cross(N, up));
            XMStoreFloat3(&vertices[i].tangent_u, T);
        }

        if (cnt != 6) {
            MessageBox(0, _T("Read error"), 0, 0);
            return;
        }

#pragma region skull texture coordinates calculations (Legacy Code)
        // Project point onto unit sphere and generate spherical texture coordinates.
        /*XMVECTOR P = XMLoadFloat3(&vertices[i].position);

        XMFLOAT3 shpere_pos;
        XMStoreFloat3(&shpere_pos, XMVector3Normalize(P));

        float theta = atan2f(shpere_pos.z, shpere_pos.x);

        if (theta < 0.0f)
            theta += XM_2PI;

        float phi = acosf(shpere_pos.y);

        float u = theta / (2.0f * XM_PI);
        float v = phi / XM_PI;

        vertices[i].texc = {u, v};*/
#pragma endregion

        // -- calculate vmin, vmax
        vmin = XMVectorMin(vmin, P);
        vmax = XMVectorMax(vmax, P);
    }

    // -- construct bounding box (AABB)
    BoundingBox bounds;
    XMStoreFloat3(&bounds.Center, 0.5f * (vmin + vmax));
    XMStoreFloat3(&bounds.Extents, 0.5f * (vmax - vmin));

    // -- skip three lines
    fgets(linebuf, sizeof(linebuf), f);
    fgets(linebuf, sizeof(linebuf), f);
    fgets(linebuf, sizeof(linebuf), f);
    // -- read indices
    uint32_t * indices = (uint32_t *)calloc(static_cast<size_t>(tcount) * 3, sizeof(uint32_t));
    for (unsigned i = 0; i < tcount; i++) {
        fgets(linebuf, sizeof(linebuf), f);
        cnt = sscanf_s(
            linebuf, "%d %d %d",
            &indices[i * 3 + 0], &indices[i * 3 + 1], &indices[i * 3 + 2]
        );
        if (cnt != 3) {
            MessageBox(0, _T("Read error"), 0, 0);
            return;
        }
    }

    // -- remember to free heap-allocated memory
    /*
    free(vertices);
    free(indices);
    */
    fclose(f);
#pragma endregion   Read_Data_File

    UINT vb_byte_size = vcount * sizeof(Vertex);
    UINT ib_byte_size = (tcount * 3) * sizeof(uint32_t);

    // -- Fill out render_ctx geom[GEOM_SKULL] (skull)
    D3DCreateBlob(vb_byte_size, &render_ctx->geom[GEOM_SKULL].vb_cpu);
    CopyMemory(render_ctx->geom[GEOM_SKULL].vb_cpu->GetBufferPointer(), vertices, vb_byte_size);

    D3DCreateBlob(ib_byte_size, &render_ctx->geom[GEOM_SKULL].ib_cpu);
    CopyMemory(render_ctx->geom[GEOM_SKULL].ib_cpu->GetBufferPointer(), indices, ib_byte_size);

    create_default_buffer(render_ctx->device, render_ctx->direct_cmd_list, vertices, vb_byte_size, &render_ctx->geom[GEOM_SKULL].vb_uploader, &render_ctx->geom[GEOM_SKULL].vb_gpu);
    create_default_buffer(render_ctx->device, render_ctx->direct_cmd_list, indices, ib_byte_size, &render_ctx->geom[GEOM_SKULL].ib_uploader, &render_ctx->geom[GEOM_SKULL].ib_gpu);

    render_ctx->geom[GEOM_SKULL].vb_byte_stide = sizeof(Vertex);
    render_ctx->geom[GEOM_SKULL].vb_byte_size = vb_byte_size;
    render_ctx->geom[GEOM_SKULL].ib_byte_size = ib_byte_size;
    render_ctx->geom[GEOM_SKULL].index_format = DXGI_FORMAT_R32_UINT;

    SubmeshGeometry submesh = {};
    submesh.index_count = tcount * 3;
    submesh.start_index_location = 0;
    submesh.base_vertex_location = 0;
    submesh.bounds = bounds;

    render_ctx->geom[GEOM_SKULL].submesh_names[0] = "skull";
    render_ctx->geom[GEOM_SKULL].submesh_geoms[0] = submesh;

    // -- cleanup
    free(vertices);
    free(indices);
}
#define _BOX_VTX_CNT   24
#define _BOX_IDX_CNT   36

#define _GRID_VTX_CNT   2400
#define _GRID_IDX_CNT   13806

#define _SPHERE_VTX_CNT   401
#define _SPHERE_IDX_CNT   2280

#define _CYLINDER_VTX_CNT   485
#define _CYLINDER_IDX_CNT   2520

#define _QUAD_VTX_CNT   4
#define _QUAD_IDX_CNT   6

#define _TOTAL_VTX_CNT  (_BOX_VTX_CNT + _GRID_VTX_CNT + _SPHERE_VTX_CNT + _CYLINDER_VTX_CNT + _QUAD_VTX_CNT)
#define _TOTAL_IDX_CNT  (_BOX_IDX_CNT + _GRID_IDX_CNT + _SPHERE_IDX_CNT + _CYLINDER_IDX_CNT + _QUAD_IDX_CNT)

static void
create_shapes_geometry (D3DRenderContext * render_ctx) {

    Vertex *    vertices = (Vertex *)::malloc(sizeof(Vertex) * _TOTAL_VTX_CNT);
    uint16_t *  indices = (uint16_t *)::malloc(sizeof(uint16_t) * _TOTAL_IDX_CNT);
    BYTE *      scratch = (BYTE *)::malloc(sizeof(GeomVertex) * _TOTAL_VTX_CNT + sizeof(uint16_t) * _TOTAL_IDX_CNT);

    // box
    UINT bsz = sizeof(GeomVertex) * _BOX_VTX_CNT;
    UINT bsz_id = bsz + sizeof(uint16_t) * _BOX_IDX_CNT;
    // grid
    UINT gsz = bsz_id + sizeof(GeomVertex) * _GRID_VTX_CNT;
    UINT gsz_id = gsz + sizeof(uint16_t) * _GRID_IDX_CNT;
    // sphere
    UINT ssz = gsz_id + sizeof(GeomVertex) * _SPHERE_VTX_CNT;
    UINT ssz_id = ssz + sizeof(uint16_t) * _SPHERE_IDX_CNT;
    // cylinder
    UINT csz = ssz_id + sizeof(GeomVertex) * _CYLINDER_VTX_CNT;
    //UINT csz_id = csz + sizeof(uint16_t) * _CYLINDER_IDX_CNT; // not used

    GeomVertex *    box_vertices = reinterpret_cast<GeomVertex *>(scratch);
    uint16_t *      box_indices = reinterpret_cast<uint16_t *>(scratch + bsz);
    GeomVertex *    grid_vertices = reinterpret_cast<GeomVertex *>(scratch + bsz_id);
    uint16_t *      grid_indices = reinterpret_cast<uint16_t *>(scratch + gsz);
    GeomVertex *    sphere_vertices = reinterpret_cast<GeomVertex *>(scratch + gsz_id);
    uint16_t *      sphere_indices = reinterpret_cast<uint16_t *>(scratch + ssz);
    GeomVertex *    cylinder_vertices = reinterpret_cast<GeomVertex *>(scratch + ssz_id);
    uint16_t *      cylinder_indices = reinterpret_cast<uint16_t *>(scratch + csz);

    create_box(1.0f, 1.0f, 1.0f, box_vertices, box_indices);
    create_grid16(20.0f, 30.0f, 60, 40, grid_vertices, grid_indices);
    create_sphere(0.5f, sphere_vertices, sphere_indices);
    create_cylinder(0.5f, 0.3f, 3.0f, cylinder_vertices, cylinder_indices);

    GeomVertex quad_verts[_QUAD_VTX_CNT];
    uint16_t quad_indices[_QUAD_IDX_CNT];
    create_quad(0.5f, -0.5f, .5f, 0.5f, 0.0f, quad_verts, quad_indices);

    // We are concatenating all the geometry into one big vertex/index buffer.  So
    // define the regions in the buffer each submesh covers.

    // Cache the vertex offsets to each object in the concatenated vertex buffer.
    UINT box_vertex_offset = 0;
    UINT grid_vertex_offset = _BOX_VTX_CNT;
    UINT sphere_vertex_offset = grid_vertex_offset + _GRID_VTX_CNT;
    UINT cylinder_vertex_offset = sphere_vertex_offset + _SPHERE_VTX_CNT;
    UINT quad_vertex_offset = cylinder_vertex_offset + _CYLINDER_VTX_CNT;

    // Cache the starting index for each object in the concatenated index buffer.
    UINT box_index_offset = 0;
    UINT grid_index_offset = _BOX_IDX_CNT;
    UINT sphere_index_offset = grid_index_offset + _GRID_IDX_CNT;
    UINT cylinder_index_offsett = sphere_index_offset + _SPHERE_IDX_CNT;
    UINT quad_index_offset = cylinder_index_offsett + _CYLINDER_IDX_CNT;

    // Define the SubmeshGeometry that cover different 
    // regions of the vertex/index buffers.
    SubmeshGeometry box_submesh = {};
    box_submesh.index_count = _BOX_IDX_CNT;
    box_submesh.start_index_location = box_index_offset;
    box_submesh.base_vertex_location = box_vertex_offset;

    SubmeshGeometry grid_submesh = {};
    grid_submesh.index_count = _GRID_IDX_CNT;
    grid_submesh.start_index_location = grid_index_offset;
    grid_submesh.base_vertex_location = grid_vertex_offset;

    SubmeshGeometry sphere_submesh = {};
    sphere_submesh.index_count = _SPHERE_IDX_CNT;
    sphere_submesh.start_index_location = sphere_index_offset;
    sphere_submesh.base_vertex_location = sphere_vertex_offset;

    SubmeshGeometry cylinder_submesh = {};
    cylinder_submesh.index_count = _CYLINDER_IDX_CNT;
    cylinder_submesh.start_index_location = cylinder_index_offsett;
    cylinder_submesh.base_vertex_location = cylinder_vertex_offset;

    SubmeshGeometry quad_submesh = {};
    quad_submesh.index_count = _QUAD_IDX_CNT;
    quad_submesh.start_index_location = quad_index_offset;
    quad_submesh.base_vertex_location = quad_vertex_offset;

    // Extract the vertex elements we are interested in and pack the
    // vertices of all the meshes into one vertex buffer.

    UINT k = 0;
    for (size_t i = 0; i < _BOX_VTX_CNT; ++i, ++k) {
        vertices[k].position = box_vertices[i].Position;
        vertices[k].normal = box_vertices[i].Normal;
        vertices[k].texc = box_vertices[i].TexC;
        vertices[k].tangent_u = box_vertices[i].TangentU;
    }
    for (size_t i = 0; i < _GRID_VTX_CNT; ++i, ++k) {
        vertices[k].position = grid_vertices[i].Position;
        vertices[k].normal = grid_vertices[i].Normal;
        vertices[k].texc = grid_vertices[i].TexC;
        vertices[k].tangent_u = grid_vertices[i].TangentU;
    }
    for (size_t i = 0; i < _SPHERE_VTX_CNT; ++i, ++k) {
        vertices[k].position = sphere_vertices[i].Position;
        vertices[k].normal = sphere_vertices[i].Normal;
        vertices[k].texc = sphere_vertices[i].TexC;
        vertices[k].tangent_u = sphere_vertices[i].TangentU;
    }
    for (size_t i = 0; i < _CYLINDER_VTX_CNT; ++i, ++k) {
        vertices[k].position = cylinder_vertices[i].Position;
        vertices[k].normal = cylinder_vertices[i].Normal;
        vertices[k].texc = cylinder_vertices[i].TexC;
        vertices[k].tangent_u = cylinder_vertices[i].TangentU;
    }
    for (size_t i = 0; i < _QUAD_VTX_CNT; ++i, ++k) {
        vertices[k].position = quad_verts[i].Position;
        vertices[k].normal = quad_verts[i].Normal;
        vertices[k].texc = quad_verts[i].TexC;
        vertices[k].tangent_u = quad_verts[i].TangentU;
    }


    // -- pack indices
    k = 0;
    for (size_t i = 0; i < _BOX_IDX_CNT; ++i, ++k)
        indices[k] = box_indices[i];
    for (size_t i = 0; i < _GRID_IDX_CNT; ++i, ++k)
        indices[k] = grid_indices[i];
    for (size_t i = 0; i < _SPHERE_IDX_CNT; ++i, ++k)
        indices[k] = sphere_indices[i];
    for (size_t i = 0; i < _CYLINDER_IDX_CNT; ++i, ++k)
        indices[k] = cylinder_indices[i];
    for (size_t i = 0; i < _QUAD_IDX_CNT; ++i, ++k)
        indices[k] = quad_indices[i];

    UINT vb_byte_size = _TOTAL_VTX_CNT * sizeof(Vertex);
    UINT ib_byte_size = _TOTAL_IDX_CNT * sizeof(uint16_t);

    // -- Fill out render_ctx geom[0] (shapes)
    D3DCreateBlob(vb_byte_size, &render_ctx->geom[GEOM_SHAPES].vb_cpu);
    if (vertices)
        CopyMemory(render_ctx->geom[GEOM_SHAPES].vb_cpu->GetBufferPointer(), vertices, vb_byte_size);

    D3DCreateBlob(ib_byte_size, &render_ctx->geom[GEOM_SHAPES].ib_cpu);
    if (indices)
        CopyMemory(render_ctx->geom[GEOM_SHAPES].ib_cpu->GetBufferPointer(), indices, ib_byte_size);

    create_default_buffer(render_ctx->device, render_ctx->direct_cmd_list, vertices, vb_byte_size, &render_ctx->geom[GEOM_SHAPES].vb_uploader, &render_ctx->geom[GEOM_SHAPES].vb_gpu);
    create_default_buffer(render_ctx->device, render_ctx->direct_cmd_list, indices, ib_byte_size, &render_ctx->geom[GEOM_SHAPES].ib_uploader, &render_ctx->geom[GEOM_SHAPES].ib_gpu);

    render_ctx->geom[GEOM_SHAPES].vb_byte_stide = sizeof(Vertex);
    render_ctx->geom[GEOM_SHAPES].vb_byte_size = vb_byte_size;
    render_ctx->geom[GEOM_SHAPES].ib_byte_size = ib_byte_size;
    render_ctx->geom[GEOM_SHAPES].index_format = DXGI_FORMAT_R16_UINT;

    render_ctx->geom[GEOM_SHAPES].submesh_names[_BOX_ID] = "box";
    render_ctx->geom[GEOM_SHAPES].submesh_geoms[_BOX_ID] = box_submesh;
    render_ctx->geom[GEOM_SHAPES].submesh_names[_GRID_ID] = "grid";
    render_ctx->geom[GEOM_SHAPES].submesh_geoms[_GRID_ID] = grid_submesh;
    render_ctx->geom[GEOM_SHAPES].submesh_names[_SPHERE_ID] = "shpere";
    render_ctx->geom[GEOM_SHAPES].submesh_geoms[_SPHERE_ID] = sphere_submesh;
    render_ctx->geom[GEOM_SHAPES].submesh_names[_CYLINDER_ID] = "cylinder";
    render_ctx->geom[GEOM_SHAPES].submesh_geoms[_CYLINDER_ID] = cylinder_submesh;
    render_ctx->geom[GEOM_SHAPES].submesh_names[_QUAD_ID] = "quad";
    render_ctx->geom[GEOM_SHAPES].submesh_geoms[_QUAD_ID] = quad_submesh;

    // -- cleanup
    free(scratch);
    free(indices);
    free(vertices);
}
static void
create_render_items (D3DRenderContext * render_ctx) {
    // sky
    XMStoreFloat4x4(&render_ctx->all_ritems.ritems[RITEM_SKY].world, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
    render_ctx->all_ritems.ritems[RITEM_SKY].tex_transform = Identity4x4();
    render_ctx->all_ritems.ritems[RITEM_SKY].obj_cbuffer_index = 0;
    render_ctx->all_ritems.ritems[RITEM_SKY].mat = &render_ctx->materials[MAT_SKY];
    render_ctx->all_ritems.ritems[RITEM_SKY].geometry = &render_ctx->geom[GEOM_SHAPES];
    render_ctx->all_ritems.ritems[RITEM_SKY].primitive_type = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    render_ctx->all_ritems.ritems[RITEM_SKY].index_count = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_SPHERE_ID].index_count;
    render_ctx->all_ritems.ritems[RITEM_SKY].start_index_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_SPHERE_ID].start_index_location;
    render_ctx->all_ritems.ritems[RITEM_SKY].base_vertex_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_SPHERE_ID].base_vertex_location;
    render_ctx->all_ritems.ritems[RITEM_SKY].bounds = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_SPHERE_ID].bounds;
    render_ctx->all_ritems.ritems[RITEM_SKY].n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_SKY].mat->n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_SKY].initialized = true;
    render_ctx->all_ritems.size++;
    render_ctx->environment_ritems.ritems[0] = render_ctx->all_ritems.ritems[RITEM_SKY];
    render_ctx->environment_ritems.size++;

    // quad ssao
    render_ctx->all_ritems.ritems[RITEM_QUAD_SSAO].world = Identity4x4();
    render_ctx->all_ritems.ritems[RITEM_QUAD_SSAO].tex_transform = Identity4x4();
    render_ctx->all_ritems.ritems[RITEM_QUAD_SSAO].obj_cbuffer_index = 1;
    render_ctx->all_ritems.ritems[RITEM_QUAD_SSAO].geometry = &render_ctx->geom[GEOM_SHAPES];
    render_ctx->all_ritems.ritems[RITEM_QUAD_SSAO].mat = &render_ctx->materials[MAT_BRICK];
    render_ctx->all_ritems.ritems[RITEM_QUAD_SSAO].primitive_type = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    render_ctx->all_ritems.ritems[RITEM_QUAD_SSAO].index_count = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_QUAD_ID].index_count;
    render_ctx->all_ritems.ritems[RITEM_QUAD_SSAO].start_index_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_QUAD_ID].start_index_location;
    render_ctx->all_ritems.ritems[RITEM_QUAD_SSAO].base_vertex_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_QUAD_ID].base_vertex_location;
    render_ctx->all_ritems.ritems[RITEM_QUAD_SSAO].n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_QUAD_SSAO].mat->n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_QUAD_SSAO].initialized = true;
    render_ctx->all_ritems.size++;
    render_ctx->debug_ritems_ssao.ritems[render_ctx->debug_ritems_ssao.size++] = render_ctx->all_ritems.ritems[RITEM_QUAD_SSAO];

    // quad smap
    XMStoreFloat4x4(&render_ctx->all_ritems.ritems[RITEM_QUAD_SMAP].world, XMMatrixTranslation(0.0f, 0.75f, 0.0f));
    render_ctx->all_ritems.ritems[RITEM_QUAD_SMAP].tex_transform = Identity4x4();
    render_ctx->all_ritems.ritems[RITEM_QUAD_SMAP].obj_cbuffer_index = 2;
    render_ctx->all_ritems.ritems[RITEM_QUAD_SMAP].geometry = &render_ctx->geom[GEOM_SHAPES];
    render_ctx->all_ritems.ritems[RITEM_QUAD_SMAP].mat = &render_ctx->materials[MAT_BRICK];
    render_ctx->all_ritems.ritems[RITEM_QUAD_SMAP].primitive_type = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    render_ctx->all_ritems.ritems[RITEM_QUAD_SMAP].index_count = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_QUAD_ID].index_count;
    render_ctx->all_ritems.ritems[RITEM_QUAD_SMAP].start_index_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_QUAD_ID].start_index_location;
    render_ctx->all_ritems.ritems[RITEM_QUAD_SMAP].base_vertex_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_QUAD_ID].base_vertex_location;
    render_ctx->all_ritems.ritems[RITEM_QUAD_SMAP].n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_QUAD_SMAP].mat->n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_QUAD_SMAP].initialized = true;
    render_ctx->all_ritems.size++;
    render_ctx->debug_ritems_smap.ritems[render_ctx->debug_ritems_smap.size++] = render_ctx->all_ritems.ritems[RITEM_QUAD_SMAP];

    // box
    XMStoreFloat4x4(&render_ctx->all_ritems.ritems[RITEM_BOX].world, XMMatrixScaling(2.0f, 1.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
    XMStoreFloat4x4(&render_ctx->all_ritems.ritems[RITEM_BOX].tex_transform, XMMatrixScaling(1.0f, 0.5f, 1.0f));
    render_ctx->all_ritems.ritems[RITEM_BOX].obj_cbuffer_index = 3;
    render_ctx->all_ritems.ritems[RITEM_BOX].geometry = &render_ctx->geom[GEOM_SHAPES];
    render_ctx->all_ritems.ritems[RITEM_BOX].mat = &render_ctx->materials[MAT_BRICK];
    render_ctx->all_ritems.ritems[RITEM_BOX].primitive_type = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    render_ctx->all_ritems.ritems[RITEM_BOX].index_count = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_BOX_ID].index_count;
    render_ctx->all_ritems.ritems[RITEM_BOX].start_index_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_BOX_ID].start_index_location;
    render_ctx->all_ritems.ritems[RITEM_BOX].base_vertex_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_BOX_ID].base_vertex_location;
    render_ctx->all_ritems.ritems[RITEM_BOX].n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_BOX].mat->n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_BOX].initialized = true;
    render_ctx->all_ritems.size++;
    render_ctx->opaque_ritems.ritems[render_ctx->opaque_ritems.size++] = render_ctx->all_ritems.ritems[RITEM_BOX];

    // globe
    XMStoreFloat4x4(&render_ctx->all_ritems.ritems[RITEM_GLOBE].world, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 2.0f, 0.0f));
    XMStoreFloat4x4(&render_ctx->all_ritems.ritems[RITEM_GLOBE].tex_transform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
    render_ctx->all_ritems.ritems[RITEM_GLOBE].obj_cbuffer_index = 4;
    render_ctx->all_ritems.ritems[RITEM_GLOBE].geometry = &render_ctx->geom[GEOM_SHAPES];
    render_ctx->all_ritems.ritems[RITEM_GLOBE].mat = &render_ctx->materials[MAT_MIRROR];
    render_ctx->all_ritems.ritems[RITEM_GLOBE].primitive_type = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    render_ctx->all_ritems.ritems[RITEM_GLOBE].index_count = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_SPHERE_ID].index_count;
    render_ctx->all_ritems.ritems[RITEM_GLOBE].start_index_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_SPHERE_ID].start_index_location;
    render_ctx->all_ritems.ritems[RITEM_GLOBE].base_vertex_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_SPHERE_ID].base_vertex_location;
    render_ctx->all_ritems.ritems[RITEM_GLOBE].n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_GLOBE].mat->n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_GLOBE].initialized = false; // not using globe in this demp
    render_ctx->all_ritems.size++;
    render_ctx->opaque_ritems.ritems[render_ctx->opaque_ritems.size++] = render_ctx->all_ritems.ritems[RITEM_GLOBE];

    // skull
    XMStoreFloat4x4(&render_ctx->all_ritems.ritems[RITEM_SKULL].world, XMMatrixScaling(0.4f, 0.4f, 0.4f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f));
    render_ctx->all_ritems.ritems[RITEM_SKULL].tex_transform = Identity4x4();
    render_ctx->all_ritems.ritems[RITEM_SKULL].obj_cbuffer_index = 5;
    render_ctx->all_ritems.ritems[RITEM_SKULL].geometry = &render_ctx->geom[GEOM_SKULL];
    render_ctx->all_ritems.ritems[RITEM_SKULL].mat = &render_ctx->materials[MAT_SKULL];
    render_ctx->all_ritems.ritems[RITEM_SKULL].primitive_type = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    render_ctx->all_ritems.ritems[RITEM_SKULL].index_count = render_ctx->geom[GEOM_SKULL].submesh_geoms[0].index_count;
    render_ctx->all_ritems.ritems[RITEM_SKULL].start_index_loc = render_ctx->geom[GEOM_SKULL].submesh_geoms[0].start_index_location;
    render_ctx->all_ritems.ritems[RITEM_SKULL].base_vertex_loc = render_ctx->geom[GEOM_SKULL].submesh_geoms[0].base_vertex_location;
    render_ctx->all_ritems.ritems[RITEM_SKULL].n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_SKULL].mat->n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_SKULL].initialized = true;
    render_ctx->all_ritems.size++;
    render_ctx->opaque_ritems.ritems[render_ctx->opaque_ritems.size++] = render_ctx->all_ritems.ritems[RITEM_SKULL];

    // grid
    render_ctx->all_ritems.ritems[RITEM_GRID].world = Identity4x4();
    XMStoreFloat4x4(&render_ctx->all_ritems.ritems[RITEM_GRID].tex_transform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
    render_ctx->all_ritems.ritems[RITEM_GRID].obj_cbuffer_index = 6;
    render_ctx->all_ritems.ritems[RITEM_GRID].geometry = &render_ctx->geom[GEOM_SHAPES];
    render_ctx->all_ritems.ritems[RITEM_GRID].mat = &render_ctx->materials[MAT_TILE];
    render_ctx->all_ritems.ritems[RITEM_GRID].primitive_type = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    render_ctx->all_ritems.ritems[RITEM_GRID].index_count = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_GRID_ID].index_count;
    render_ctx->all_ritems.ritems[RITEM_GRID].start_index_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_GRID_ID].start_index_location;
    render_ctx->all_ritems.ritems[RITEM_GRID].base_vertex_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_GRID_ID].base_vertex_location;
    render_ctx->all_ritems.ritems[RITEM_GRID].n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_GRID].mat->n_frames_dirty = NUM_QUEUING_FRAMES;
    render_ctx->all_ritems.ritems[RITEM_GRID].initialized = true;
    render_ctx->all_ritems.size++;
    render_ctx->opaque_ritems.ritems[render_ctx->opaque_ritems.size++] = render_ctx->all_ritems.ritems[RITEM_GRID];

    // cylinders and spheres
    XMMATRIX brick_tex_transform = XMMatrixScaling(1.5f, 2.0f, 1.0f);
    UINT obj_cb_index = 7;
    int _curr = 7;
    for (int i = 0; i < 5; ++i) {
        XMMATRIX left_cylinder_world = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
        XMMATRIX right_cylinder_world = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

        XMMATRIX left_sphere_world = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
        XMMATRIX right_sphere_world = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

        XMStoreFloat4x4(&render_ctx->all_ritems.ritems[_curr].world, right_cylinder_world);
        XMStoreFloat4x4(&render_ctx->all_ritems.ritems[_curr].tex_transform, brick_tex_transform);
        render_ctx->all_ritems.ritems[_curr].obj_cbuffer_index = obj_cb_index++;
        render_ctx->all_ritems.ritems[_curr].geometry = &render_ctx->geom[GEOM_SHAPES];
        render_ctx->all_ritems.ritems[_curr].mat = &render_ctx->materials[MAT_BRICK];
        render_ctx->all_ritems.ritems[_curr].primitive_type = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        render_ctx->all_ritems.ritems[_curr].index_count = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_CYLINDER_ID].index_count;
        render_ctx->all_ritems.ritems[_curr].start_index_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_CYLINDER_ID].start_index_location;
        render_ctx->all_ritems.ritems[_curr].base_vertex_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_CYLINDER_ID].base_vertex_location;
        render_ctx->all_ritems.ritems[_curr].n_frames_dirty = NUM_QUEUING_FRAMES;
        render_ctx->all_ritems.ritems[_curr].mat->n_frames_dirty = NUM_QUEUING_FRAMES;
        render_ctx->all_ritems.ritems[_curr].initialized = true;
        render_ctx->all_ritems.size++;
        render_ctx->opaque_ritems.ritems[render_ctx->opaque_ritems.size++] = render_ctx->all_ritems.ritems[_curr];
        _curr++;

        XMStoreFloat4x4(&render_ctx->all_ritems.ritems[_curr].world, left_cylinder_world);
        XMStoreFloat4x4(&render_ctx->all_ritems.ritems[_curr].tex_transform, brick_tex_transform);
        render_ctx->all_ritems.ritems[_curr].obj_cbuffer_index = obj_cb_index++;
        render_ctx->all_ritems.ritems[_curr].geometry = &render_ctx->geom[GEOM_SHAPES];
        render_ctx->all_ritems.ritems[_curr].mat = &render_ctx->materials[MAT_BRICK];
        render_ctx->all_ritems.ritems[_curr].primitive_type = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        render_ctx->all_ritems.ritems[_curr].index_count = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_CYLINDER_ID].index_count;
        render_ctx->all_ritems.ritems[_curr].start_index_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_CYLINDER_ID].start_index_location;
        render_ctx->all_ritems.ritems[_curr].base_vertex_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_CYLINDER_ID].base_vertex_location;
        render_ctx->all_ritems.ritems[_curr].n_frames_dirty = NUM_QUEUING_FRAMES;
        render_ctx->all_ritems.ritems[_curr].mat->n_frames_dirty = NUM_QUEUING_FRAMES;
        render_ctx->all_ritems.ritems[_curr].initialized = true;
        render_ctx->all_ritems.size++;
        render_ctx->opaque_ritems.ritems[render_ctx->opaque_ritems.size++] = render_ctx->all_ritems.ritems[_curr];
        _curr++;

        XMStoreFloat4x4(&render_ctx->all_ritems.ritems[_curr].world, left_sphere_world);
        render_ctx->all_ritems.ritems[_curr].tex_transform = Identity4x4();
        render_ctx->all_ritems.ritems[_curr].obj_cbuffer_index = obj_cb_index++;
        render_ctx->all_ritems.ritems[_curr].geometry = &render_ctx->geom[GEOM_SHAPES];
        render_ctx->all_ritems.ritems[_curr].mat = &render_ctx->materials[MAT_MIRROR];
        render_ctx->all_ritems.ritems[_curr].primitive_type = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        render_ctx->all_ritems.ritems[_curr].index_count = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_SPHERE_ID].index_count;
        render_ctx->all_ritems.ritems[_curr].start_index_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_SPHERE_ID].start_index_location;
        render_ctx->all_ritems.ritems[_curr].base_vertex_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_SPHERE_ID].base_vertex_location;
        render_ctx->all_ritems.ritems[_curr].n_frames_dirty = NUM_QUEUING_FRAMES;
        render_ctx->all_ritems.ritems[_curr].mat->n_frames_dirty = NUM_QUEUING_FRAMES;
        render_ctx->all_ritems.ritems[_curr].initialized = true;
        render_ctx->all_ritems.size++;
        render_ctx->opaque_ritems.ritems[render_ctx->opaque_ritems.size++] = render_ctx->all_ritems.ritems[_curr];
        _curr++;

        XMStoreFloat4x4(&render_ctx->all_ritems.ritems[_curr].world, right_sphere_world);
        render_ctx->all_ritems.ritems[_curr].tex_transform = Identity4x4();
        render_ctx->all_ritems.ritems[_curr].obj_cbuffer_index = obj_cb_index++;
        render_ctx->all_ritems.ritems[_curr].geometry = &render_ctx->geom[GEOM_SHAPES];
        render_ctx->all_ritems.ritems[_curr].mat = &render_ctx->materials[MAT_MIRROR];
        render_ctx->all_ritems.ritems[_curr].primitive_type = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        render_ctx->all_ritems.ritems[_curr].index_count = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_SPHERE_ID].index_count;
        render_ctx->all_ritems.ritems[_curr].start_index_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_SPHERE_ID].start_index_location;
        render_ctx->all_ritems.ritems[_curr].base_vertex_loc = render_ctx->geom[GEOM_SHAPES].submesh_geoms[_SPHERE_ID].base_vertex_location;
        render_ctx->all_ritems.ritems[_curr].n_frames_dirty = NUM_QUEUING_FRAMES;
        render_ctx->all_ritems.ritems[_curr].mat->n_frames_dirty = NUM_QUEUING_FRAMES;
        render_ctx->all_ritems.ritems[_curr].initialized = true;
        render_ctx->all_ritems.size++;
        render_ctx->opaque_ritems.ritems[render_ctx->opaque_ritems.size++] = render_ctx->all_ritems.ritems[_curr];
        _curr++;
    }
    _ASSERT_EXPR(_curr == _COUNT_RENDERITEM, _T("Invalid render items creation"));
}
static void
draw_render_items (
    ID3D12GraphicsCommandList * cmd_list,
    ID3D12Resource * obj_cb,
    RenderItemArray * ritem_array
) {
    size_t obj_cbuffer_size = sizeof(ObjectConstants);
    for (size_t i = 0; i < ritem_array->size; ++i) {
        if (ritem_array->ritems[i].initialized) {
            D3D12_VERTEX_BUFFER_VIEW vbv = Mesh_GetVertexBufferView(ritem_array->ritems[i].geometry);
            D3D12_INDEX_BUFFER_VIEW ibv = Mesh_GetIndexBufferView(ritem_array->ritems[i].geometry);
            cmd_list->IASetVertexBuffers(0, 1, &vbv);
            cmd_list->IASetIndexBuffer(&ibv);
            cmd_list->IASetPrimitiveTopology(ritem_array->ritems[i].primitive_type);

            D3D12_GPU_VIRTUAL_ADDRESS obj_cb_address =
                obj_cb->GetGPUVirtualAddress() + ritem_array->ritems[i].obj_cbuffer_index * obj_cbuffer_size;

            cmd_list->SetGraphicsRootConstantBufferView(0, obj_cb_address);

            cmd_list->DrawIndexedInstanced(
                ritem_array->ritems[i].index_count,
                1,
                ritem_array->ritems[i].start_index_loc, ritem_array->ritems[i].base_vertex_loc, 0);
        }
    }
}
static void
create_descriptor_heaps (D3DRenderContext * render_ctx, ShadowMap * smap, SSAO * ssao) {
    _ASSERT_EXPR(render_ctx->cbv_srv_uav_descriptor_size > 0, _T("invalid descriptor size value"));

    // Create Shader Resource View descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
    srv_heap_desc.NumDescriptors =
        _COUNT_TEX
        + 3 /* one for ShadowMap srv, two other null srvs for shadow hlsl code (null cube and tex) */
        + 5 /* SSAO srvs */
        + 1 /* DearImGui */;
    srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    render_ctx->device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&render_ctx->srv_heap));

    // Fill out the heap with actual descriptors
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_handle = render_ctx->srv_heap->GetCPUDescriptorHandleForHeapStart();

    // brick texture
    ID3D12Resource * brick_tex = render_ctx->textures[BRICK_DIFFUSE_MAP].resource;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = brick_tex->GetDesc().Format;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = brick_tex->GetDesc().MipLevels;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
    render_ctx->device->CreateShaderResourceView(brick_tex, &srv_desc, descriptor_cpu_handle);

    // bricks nmap
    ID3D12Resource * bricks_nmap = render_ctx->textures[BRICK_NORMAL_MAP].resource;
    srv_desc.Format = bricks_nmap->GetDesc().Format;
    srv_desc.Texture2D.MipLevels = bricks_nmap->GetDesc().MipLevels;
    descriptor_cpu_handle.ptr += render_ctx->cbv_srv_uav_descriptor_size;
    render_ctx->device->CreateShaderResourceView(bricks_nmap, &srv_desc, descriptor_cpu_handle);

    // tile texture
    ID3D12Resource * tile_tex = render_ctx->textures[TILE_DIFFUSE_MAP].resource;
    srv_desc.Format = tile_tex->GetDesc().Format;
    srv_desc.Texture2D.MipLevels = tile_tex->GetDesc().MipLevels;
    descriptor_cpu_handle.ptr += render_ctx->cbv_srv_uav_descriptor_size;
    render_ctx->device->CreateShaderResourceView(tile_tex, &srv_desc, descriptor_cpu_handle);

    // tile nmap
    ID3D12Resource * tile_nmap = render_ctx->textures[TILE_NORMAL_MAP].resource;
    srv_desc.Format = tile_nmap->GetDesc().Format;
    srv_desc.Texture2D.MipLevels = tile_nmap->GetDesc().MipLevels;
    descriptor_cpu_handle.ptr += render_ctx->cbv_srv_uav_descriptor_size;
    render_ctx->device->CreateShaderResourceView(tile_nmap, &srv_desc, descriptor_cpu_handle);

    // default texture
    ID3D12Resource * default_tex = render_ctx->textures[WHITE1x1_DIFFUSE_MAP].resource;
    srv_desc.Format = default_tex->GetDesc().Format;
    srv_desc.Texture2D.MipLevels = default_tex->GetDesc().MipLevels;
    descriptor_cpu_handle.ptr += render_ctx->cbv_srv_uav_descriptor_size;
    render_ctx->device->CreateShaderResourceView(default_tex, &srv_desc, descriptor_cpu_handle);

    // default nmap
    ID3D12Resource * default_nmap = render_ctx->textures[WHITE1x1_NORMAL_MAP].resource;
    srv_desc.Format = default_nmap->GetDesc().Format;
    srv_desc.Texture2D.MipLevels = default_nmap->GetDesc().MipLevels;
    descriptor_cpu_handle.ptr += render_ctx->cbv_srv_uav_descriptor_size;
    render_ctx->device->CreateShaderResourceView(default_nmap, &srv_desc, descriptor_cpu_handle);

    // sky texture
    ID3D12Resource * sky_tex = render_ctx->textures[TEX_SKY_CUBEMAP0].resource;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srv_desc.TextureCube.MostDetailedMip = 0;
    srv_desc.TextureCube.MipLevels = sky_tex->GetDesc().MipLevels;
    srv_desc.TextureCube.ResourceMinLODClamp = 0.0f;
    srv_desc.Format = sky_tex->GetDesc().Format;
    descriptor_cpu_handle.ptr += render_ctx->cbv_srv_uav_descriptor_size;
    render_ctx->device->CreateShaderResourceView(sky_tex, &srv_desc, descriptor_cpu_handle);

    render_ctx->sky_tex_heap_index = TEX_SKY_CUBEMAP0;

    // sky texture2
    ID3D12Resource * sky_tex2 = render_ctx->textures[TEX_SKY_CUBEMAP1].resource;
    srv_desc.TextureCube.MipLevels = sky_tex2->GetDesc().MipLevels;
    srv_desc.Format = sky_tex2->GetDesc().Format;
    descriptor_cpu_handle.ptr += render_ctx->cbv_srv_uav_descriptor_size;
    render_ctx->device->CreateShaderResourceView(sky_tex2, &srv_desc, descriptor_cpu_handle);

    // sky texture3
    ID3D12Resource * sky_tex3 = render_ctx->textures[TEX_SKY_CUBEMAP2].resource;
    srv_desc.TextureCube.MipLevels = sky_tex3->GetDesc().MipLevels;
    srv_desc.Format = sky_tex3->GetDesc().Format;
    descriptor_cpu_handle.ptr += render_ctx->cbv_srv_uav_descriptor_size;
    render_ctx->device->CreateShaderResourceView(sky_tex3, &srv_desc, descriptor_cpu_handle);

    // sky texture4
    ID3D12Resource * sky_tex4 = render_ctx->textures[TEX_SKY_CUBEMAP3].resource;
    srv_desc.TextureCube.MipLevels = sky_tex4->GetDesc().MipLevels;
    srv_desc.Format = sky_tex4->GetDesc().Format;
    descriptor_cpu_handle.ptr += render_ctx->cbv_srv_uav_descriptor_size;
    render_ctx->device->CreateShaderResourceView(sky_tex4, &srv_desc, descriptor_cpu_handle);

#if 0   // For SSAO, dsv and depth buffer should be created before SSAO descriptors setup
    //
    // Create Render Target View Descriptor Heap
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
    rtv_heap_desc.NumDescriptors = NUM_BACKBUFFERS + 1 /* SSAO normal map */ + 2 /* SSAO ambient maps */;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAGS::D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    render_ctx->device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&render_ctx->rtv_heap));

    //
    // Create Depth Stencil View Descriptor Heap
    // +1 DSV for shadow map
    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc;
    dsv_heap_desc.NumDescriptors = 2;
    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsv_heap_desc.NodeMask = 0;
    render_ctx->device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&render_ctx->dsv_heap));

#endif // 0

    //
    // shadow map and ssao heap indices setup
    //
    render_ctx->shadow_map_heap_index = render_ctx->sky_tex_heap_index + 4 /* offset for 4 skybox textures */;

    render_ctx->ssao_heap_index_start = render_ctx->shadow_map_heap_index + 1;
    render_ctx->ssao_ambient_map_index = render_ctx->ssao_heap_index_start + 3;

    render_ctx->null_cube_srv_index = render_ctx->ssao_heap_index_start + 5 /* 5 srvs for ssao */;
    render_ctx->null_tex_srv_index1 = render_ctx->null_cube_srv_index + 1;
    render_ctx->null_tex_srv_index2 = render_ctx->null_tex_srv_index1 + 1;

    D3D12_CPU_DESCRIPTOR_HANDLE null_srv_cpu = render_ctx->srv_heap->GetCPUDescriptorHandleForHeapStart();
    null_srv_cpu.ptr += ((SIZE_T)render_ctx->cbv_srv_uav_descriptor_size * render_ctx->null_cube_srv_index);

    D3D12_GPU_DESCRIPTOR_HANDLE null_srv_gpu = render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
    null_srv_gpu.ptr += ((UINT64)render_ctx->cbv_srv_uav_descriptor_size * render_ctx->null_cube_srv_index);

    render_ctx->null_srv = null_srv_gpu;

    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 0;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
    render_ctx->device->CreateShaderResourceView(nullptr, &srv_desc, null_srv_cpu);
    null_srv_cpu.ptr += render_ctx->cbv_srv_uav_descriptor_size;
    render_ctx->device->CreateShaderResourceView(nullptr, &srv_desc, null_srv_cpu);

    //
    // shadow map descriptors book-keeping
    //
    D3D12_CPU_DESCRIPTOR_HANDLE smap_srv_cpu = render_ctx->srv_heap->GetCPUDescriptorHandleForHeapStart();
    smap_srv_cpu.ptr += (SIZE_T)render_ctx->cbv_srv_uav_descriptor_size * render_ctx->shadow_map_heap_index;

    D3D12_GPU_DESCRIPTOR_HANDLE smap_srv_gpu = render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
    smap_srv_gpu.ptr += (UINT64)render_ctx->cbv_srv_uav_descriptor_size * render_ctx->shadow_map_heap_index;

    D3D12_CPU_DESCRIPTOR_HANDLE smap_dsv_cpu = render_ctx->dsv_heap->GetCPUDescriptorHandleForHeapStart();
    smap_dsv_cpu.ptr += (SIZE_T)render_ctx->dsv_descriptor_size;

    ShadowMap_CreateDescriptors(smap, smap_srv_cpu, smap_srv_gpu, smap_dsv_cpu);

    //
    // SSAO descriptors book-keeping
    //
    D3D12_CPU_DESCRIPTOR_HANDLE ssao_srv_cpu = render_ctx->srv_heap->GetCPUDescriptorHandleForHeapStart();
    ssao_srv_cpu.ptr += (SIZE_T)render_ctx->cbv_srv_uav_descriptor_size * render_ctx->ssao_heap_index_start;

    D3D12_GPU_DESCRIPTOR_HANDLE ssao_srv_gpu = render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
    ssao_srv_gpu.ptr += (UINT64)render_ctx->cbv_srv_uav_descriptor_size * render_ctx->ssao_heap_index_start;

    D3D12_CPU_DESCRIPTOR_HANDLE ssao_rtv_cpu = render_ctx->rtv_heap->GetCPUDescriptorHandleForHeapStart();
    ssao_rtv_cpu.ptr += (SIZE_T)render_ctx->rtv_descriptor_size * NUM_BACKBUFFERS;

    SSAO_CreateDescriptors(
        ssao,
        render_ctx->depth_stencil_buffer,
        ssao_srv_cpu, ssao_srv_gpu, ssao_rtv_cpu,
        render_ctx->cbv_srv_uav_descriptor_size,
        render_ctx->rtv_descriptor_size
    );
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

    // 6: SHADOW
    out_samplers[SAMPLER_SHADOW] = {};
    out_samplers[SAMPLER_SHADOW].ShaderRegister = 6;
    out_samplers[SAMPLER_SHADOW].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    out_samplers[SAMPLER_SHADOW].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    out_samplers[SAMPLER_SHADOW].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    out_samplers[SAMPLER_SHADOW].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    out_samplers[SAMPLER_SHADOW].MipLODBias = 0.0f;
    out_samplers[SAMPLER_SHADOW].MaxAnisotropy = 16;
    out_samplers[SAMPLER_SHADOW].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    out_samplers[SAMPLER_SHADOW].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    out_samplers[SAMPLER_SHADOW].MinLOD = 0.f;
    out_samplers[SAMPLER_SHADOW].MaxLOD = D3D12_FLOAT32_MAX;
    out_samplers[SAMPLER_SHADOW].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    out_samplers[SAMPLER_SHADOW].RegisterSpace = 0;
}
static void
create_root_signature (ID3D12Device * device, ID3D12RootSignature ** root_signature) {

#if 0   // fancy way of offsetting sky cubemap and shadow map descriptors
    // -- sky cube map (t0) and shdow map (t1)
    D3D12_DESCRIPTOR_RANGE tex_table0 = {};
    tex_table0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    tex_table0.NumDescriptors = 2;
    tex_table0.BaseShaderRegister = 0;  //t0 and t1 will be used
    tex_table0.RegisterSpace = 0;       //space0
    tex_table0.OffsetInDescriptorsFromTableStart = 3;
#endif // 0
    // -- sky cube map (t0)
    D3D12_DESCRIPTOR_RANGE tex_table0 = {};
    tex_table0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    tex_table0.NumDescriptors = 1;
    tex_table0.BaseShaderRegister = 0;  //t0 will be used
    tex_table0.RegisterSpace = 0;       //space0
    tex_table0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // -- shadow map (t1)
    D3D12_DESCRIPTOR_RANGE tex_table1 = {};
    tex_table1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    tex_table1.NumDescriptors = 1;
    tex_table1.BaseShaderRegister = 1;  //t1
    tex_table1.RegisterSpace = 0;       //space0
    tex_table1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // -- ssao map (t2)
    D3D12_DESCRIPTOR_RANGE tex_table2 = {};
    tex_table2.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    tex_table2.NumDescriptors = 1;
    tex_table2.BaseShaderRegister = 2;  //t2
    tex_table2.RegisterSpace = 0;       //space0
    tex_table2.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // -- rest of textures
    D3D12_DESCRIPTOR_RANGE tex_table3 = {};
    tex_table3.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    tex_table3.NumDescriptors = _COUNT_TEX;
    tex_table3.BaseShaderRegister = 3;  //t3
    tex_table3.RegisterSpace = 0;       //space0
    tex_table3.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER slot_root_params[7] = {};
    // NOTE(omid): Perfomance tip! Order from most frequent to least frequent.
    // -- obj cbuffer
    slot_root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    slot_root_params[0].Descriptor.ShaderRegister = 0;  //b0
    slot_root_params[0].Descriptor.RegisterSpace = 0;
    slot_root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // -- pass cbuffer
    slot_root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    slot_root_params[1].Descriptor.ShaderRegister = 1;  //b1
    slot_root_params[1].Descriptor.RegisterSpace = 0;
    slot_root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // -- material sbuffer
    slot_root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    slot_root_params[2].Descriptor.ShaderRegister = 0;  //t0
    slot_root_params[2].Descriptor.RegisterSpace = 1;   //space1
    slot_root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // -- sky cubemap texture
    slot_root_params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[3].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[3].DescriptorTable.pDescriptorRanges = &tex_table0;
    slot_root_params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // -- shadow map
    slot_root_params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[4].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[4].DescriptorTable.pDescriptorRanges = &tex_table1;
    slot_root_params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // -- ssao
    slot_root_params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[5].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[5].DescriptorTable.pDescriptorRanges = &tex_table2;
    slot_root_params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // -- textures
    slot_root_params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[6].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[6].DescriptorTable.pDescriptorRanges = &tex_table3;
    slot_root_params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

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
create_root_signature_ssao (ID3D12Device * device, ID3D12RootSignature ** root_signature) {
    // -- normal (t0) and depth (t1) maps
    D3D12_DESCRIPTOR_RANGE tex_table0 = {};
    tex_table0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    tex_table0.NumDescriptors = 2;
    tex_table0.BaseShaderRegister = 0;  //t0 and t1 will be used
    tex_table0.RegisterSpace = 0;       //space0
    tex_table0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // -- random vector map (t2)
    D3D12_DESCRIPTOR_RANGE tex_table1 = {};
    tex_table1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    tex_table1.NumDescriptors = 1;
    tex_table1.BaseShaderRegister = 2;  //t2 will be used
    tex_table1.RegisterSpace = 0;       //space0
    tex_table1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER slot_root_params[4] = {};
    // NOTE(omid): Perfomance tip! Order from most frequent to least frequent.
    // -- ssao cbuffer
    slot_root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    slot_root_params[0].Descriptor.ShaderRegister = 0;  //b0
    slot_root_params[0].Descriptor.RegisterSpace = 0;
    slot_root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // -- cb root constant (horz_blur)
    slot_root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    slot_root_params[1].Constants.ShaderRegister = 1;  //b1
    slot_root_params[1].Constants.Num32BitValues = 1;
    slot_root_params[1].Constants.RegisterSpace = 0;
    slot_root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // -- normal / depth maps
    slot_root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[2].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[2].DescriptorTable.pDescriptorRanges = &tex_table0;
    slot_root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // -- random vectors map
    slot_root_params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    slot_root_params[3].DescriptorTable.NumDescriptorRanges = 1;
    slot_root_params[3].DescriptorTable.pDescriptorRanges = &tex_table1;
    slot_root_params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;


    D3D12_STATIC_SAMPLER_DESC samplers[4] = {};
    // 0: PointClamp Sampler
    samplers[0] = {};
    samplers[0].ShaderRegister = 0;    //s0
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].MipLODBias = 0;
    samplers[0].MaxAnisotropy = 16;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[0].MinLOD = 0.f;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    samplers[0].RegisterSpace = 0;
    // 1: LinearClamp Sampler
    samplers[1] = {};
    samplers[1].ShaderRegister = 1;    //s1
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].MipLODBias = 0;
    samplers[1].MaxAnisotropy = 16;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[1].MinLOD = 0.f;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    samplers[1].RegisterSpace = 0;
    // 2: Depth Sampler
    samplers[2] = {};
    samplers[2].ShaderRegister = 2;    //s2
    samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[2].MipLODBias = 0;
    samplers[2].MaxAnisotropy = 0;
    samplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[2].MinLOD = 0.f;
    samplers[2].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    samplers[2].RegisterSpace = 0;
    // 3: Linear Sampler
    samplers[3] = {};
    samplers[3].ShaderRegister = 3;    //s3
    samplers[3].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[3].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[3].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[3].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[3].MipLODBias = 0;
    samplers[3].MaxAnisotropy = 16;
    samplers[3].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[3].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[3].MinLOD = 0.f;
    samplers[3].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    samplers[3].RegisterSpace = 0;

    // A root signature is an array of root parameters.
    D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {};
    root_sig_desc.NumParameters = _countof(slot_root_params);
    root_sig_desc.pParameters = slot_root_params;
    root_sig_desc.NumStaticSamplers = _countof(samplers);
    root_sig_desc.pStaticSamplers = samplers;
    root_sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob * serialized_root_sig = nullptr;
    ID3DBlob * error_blob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized_root_sig, &error_blob);

    if (error_blob)
        ::OutputDebugStringA((char*)error_blob->GetBufferPointer());

    device->CreateRootSignature(0, serialized_root_sig->GetBufferPointer(), serialized_root_sig->GetBufferSize(), IID_PPV_ARGS(root_signature));
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

        LPCWSTR args [] = {_T("-Zi"), _T("-Od")};

        ret = dxc_compiler->Compile(
            shader_blob_encoding, path, entry_point, shader_model,
            args, _countof(args),
            defines, n_defines, include_handler, &dxc_res
        );
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

    D3D12_INPUT_ELEMENT_DESC std_input_desc[4];
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

    std_input_desc[3] = {};
    std_input_desc[3].SemanticName = "TANGENT";
    std_input_desc[3].SemanticIndex = 0;
    std_input_desc[3].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    std_input_desc[3].InputSlot = 0;
    std_input_desc[3].AlignedByteOffset = 32;
    std_input_desc[3].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

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

    D3D12_GRAPHICS_PIPELINE_STATE_DESC base_pso_desc = {};
    base_pso_desc.pRootSignature = render_ctx->root_signature;
    base_pso_desc.VS.pShaderBytecode = render_ctx->shaders[SHADER_STANDARD_VS]->GetBufferPointer();
    base_pso_desc.VS.BytecodeLength = render_ctx->shaders[SHADER_STANDARD_VS]->GetBufferSize();
    base_pso_desc.PS.pShaderBytecode = render_ctx->shaders[SHADER_OPAQUE_PS]->GetBufferPointer();
    base_pso_desc.PS.BytecodeLength = render_ctx->shaders[SHADER_OPAQUE_PS]->GetBufferSize();
    base_pso_desc.BlendState = def_blend_desc;
    base_pso_desc.SampleMask = UINT_MAX;
    base_pso_desc.RasterizerState = def_rasterizer_desc;
    base_pso_desc.DepthStencilState = ds_desc;
    base_pso_desc.DSVFormat = render_ctx->depthstencil_format;
    base_pso_desc.InputLayout.pInputElementDescs = std_input_desc;
    base_pso_desc.InputLayout.NumElements = ARRAY_COUNT(std_input_desc);
    base_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    base_pso_desc.NumRenderTargets = 1;
    base_pso_desc.RTVFormats[0] = render_ctx->backbuffer_format;
    base_pso_desc.SampleDesc.Count = render_ctx->msaa4x_state ? 4 : 1;
    base_pso_desc.SampleDesc.Quality = render_ctx->msaa4x_state ? (render_ctx->msaa4x_quality - 1) : 0;

    //
    // -- PSO for opaque objects
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaque_pso_desc = base_pso_desc;
    opaque_pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
    opaque_pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    render_ctx->device->CreateGraphicsPipelineState(&opaque_pso_desc, IID_PPV_ARGS(&render_ctx->psos[LAYER_OPAQUE]));

    //
    // -- PSO for shadow map pass
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC smap_pso_desc = base_pso_desc;
    smap_pso_desc.RasterizerState.DepthBias = 100000;
    smap_pso_desc.RasterizerState.DepthBiasClamp = 0.0f;
    smap_pso_desc.RasterizerState.SlopeScaledDepthBias = 1.0f;
    smap_pso_desc.VS.pShaderBytecode = render_ctx->shaders[SHADER_SHADOW_VS]->GetBufferPointer();
    smap_pso_desc.VS.BytecodeLength = render_ctx->shaders[SHADER_SHADOW_VS]->GetBufferSize();
    smap_pso_desc.PS.pShaderBytecode = render_ctx->shaders[SHADER_SHADOW_OPAQUE_PS]->GetBufferPointer();
    smap_pso_desc.PS.BytecodeLength = render_ctx->shaders[SHADER_SHADOW_OPAQUE_PS]->GetBufferSize();
    //
    // shadow map pass does not have a render target
    smap_pso_desc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    smap_pso_desc.NumRenderTargets = 0;
    render_ctx->device->CreateGraphicsPipelineState(&smap_pso_desc, IID_PPV_ARGS(&render_ctx->psos[LAYER_SHADOW_OPAQUE]));

    // TODO(omid): Debug layers use the same VS, only changing PS is enough 
    //
    // -- PSO for shadow mapping debug layer
    D3D12_GRAPHICS_PIPELINE_STATE_DESC debug_pso_desc_samp = base_pso_desc;
    debug_pso_desc_samp.VS.pShaderBytecode = render_ctx->shaders[SHADER_DEBUG_SMAP_VS]->GetBufferPointer();
    debug_pso_desc_samp.VS.BytecodeLength = render_ctx->shaders[SHADER_DEBUG_SMAP_VS]->GetBufferSize();
    debug_pso_desc_samp.PS.pShaderBytecode = render_ctx->shaders[SHADER_DEBUG_SMAP_PS]->GetBufferPointer();
    debug_pso_desc_samp.PS.BytecodeLength = render_ctx->shaders[SHADER_DEBUG_SMAP_PS]->GetBufferSize();
    render_ctx->device->CreateGraphicsPipelineState(&debug_pso_desc_samp, IID_PPV_ARGS(&render_ctx->psos[LAYER_DEBUG_SMAP]));

    //
    // -- PSO for SSAO debug layer
    D3D12_GRAPHICS_PIPELINE_STATE_DESC debug_pso_desc_ssao = base_pso_desc;
    debug_pso_desc_ssao.VS.pShaderBytecode = render_ctx->shaders[SHADER_DEBUG_SSAO_VS]->GetBufferPointer();
    debug_pso_desc_ssao.VS.BytecodeLength = render_ctx->shaders[SHADER_DEBUG_SSAO_VS]->GetBufferSize();
    debug_pso_desc_ssao.PS.pShaderBytecode = render_ctx->shaders[SHADER_DEBUG_SSAO_PS]->GetBufferPointer();
    debug_pso_desc_ssao.PS.BytecodeLength = render_ctx->shaders[SHADER_DEBUG_SSAO_PS]->GetBufferSize();
    render_ctx->device->CreateGraphicsPipelineState(&debug_pso_desc_ssao, IID_PPV_ARGS(&render_ctx->psos[LAYER_DEBUG_SSAO]));

    //
    // -- PSO for sky
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC sky_pso = base_pso_desc;

    // -- camera is inside the sky sphere so just turn of culling
    sky_pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    // -- use LESS_EQUAL compare function to avoid depth test fail at z = 1 (NDC)
    // -- when depth buffer cleared to 1
    sky_pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    sky_pso.VS.pShaderBytecode = render_ctx->shaders[SHADER_SKY_VS]->GetBufferPointer();
    sky_pso.VS.BytecodeLength = render_ctx->shaders[SHADER_SKY_VS]->GetBufferSize();
    sky_pso.PS.pShaderBytecode = render_ctx->shaders[SHADER_SKY_PS]->GetBufferPointer();
    sky_pso.PS.BytecodeLength = render_ctx->shaders[SHADER_SKY_PS]->GetBufferSize();

    render_ctx->device->CreateGraphicsPipelineState(&sky_pso, IID_PPV_ARGS(&render_ctx->psos[LAYER_SKY]));

    //
    // PSO for draw normals
    D3D12_GRAPHICS_PIPELINE_STATE_DESC draw_normals_pso = base_pso_desc;
    draw_normals_pso.VS.pShaderBytecode = render_ctx->shaders[SHADER_DRAW_NORMALS_VS]->GetBufferPointer();
    draw_normals_pso.VS.BytecodeLength = render_ctx->shaders[SHADER_DRAW_NORMALS_VS]->GetBufferSize();
    draw_normals_pso.PS.pShaderBytecode = render_ctx->shaders[SHADER_DRAW_NORMALS_PS]->GetBufferPointer();
    draw_normals_pso.PS.BytecodeLength = render_ctx->shaders[SHADER_DRAW_NORMALS_PS]->GetBufferSize();
    draw_normals_pso.RTVFormats[0] = g_ssao->normal_map_format;
    draw_normals_pso.SampleDesc.Count = 1;
    draw_normals_pso.SampleDesc.Quality = 0;
    draw_normals_pso.DSVFormat = render_ctx->depthstencil_format;
    render_ctx->device->CreateGraphicsPipelineState(&draw_normals_pso, IID_PPV_ARGS(&render_ctx->psos[LAYER_DRAW_NORMALS]));

    //
    // PSO for SSAO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC ssao_pso_desc = base_pso_desc;
    ssao_pso_desc.InputLayout = {nullptr, 0};
    ssao_pso_desc.pRootSignature = render_ctx->root_signature_ssao;
    ssao_pso_desc.VS.pShaderBytecode = render_ctx->shaders[SHADER_SSAO_VS]->GetBufferPointer();
    ssao_pso_desc.VS.BytecodeLength = render_ctx->shaders[SHADER_SSAO_VS]->GetBufferSize();
    ssao_pso_desc.PS.pShaderBytecode = render_ctx->shaders[SHADER_SSAO_PS]->GetBufferPointer();
    ssao_pso_desc.PS.BytecodeLength = render_ctx->shaders[SHADER_SSAO_PS]->GetBufferSize();
    // ssao pass does not need depth buffer
    ssao_pso_desc.DepthStencilState.DepthEnable = false;
    ssao_pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    ssao_pso_desc.RTVFormats[0] = g_ssao->ambient_map_format;
    ssao_pso_desc.SampleDesc.Count = 1;
    ssao_pso_desc.SampleDesc.Quality = 0;
    ssao_pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    render_ctx->device->CreateGraphicsPipelineState(&ssao_pso_desc, IID_PPV_ARGS(&render_ctx->psos[LAYER_SSAO]));

    //
    // PSO for SSAO blur
    D3D12_GRAPHICS_PIPELINE_STATE_DESC ssao_blur_pso_desc = ssao_pso_desc;
    ssao_blur_pso_desc.VS.pShaderBytecode = render_ctx->shaders[SHADER_SSAO_BLUR_VS]->GetBufferPointer();
    ssao_blur_pso_desc.VS.BytecodeLength = render_ctx->shaders[SHADER_SSAO_BLUR_VS]->GetBufferSize();
    ssao_blur_pso_desc.PS.pShaderBytecode = render_ctx->shaders[SHADER_SSAO_BLUR_PS]->GetBufferPointer();
    ssao_blur_pso_desc.PS.BytecodeLength = render_ctx->shaders[SHADER_SSAO_BLUR_PS]->GetBufferSize();
    render_ctx->device->CreateGraphicsPipelineState(&ssao_blur_pso_desc, IID_PPV_ARGS(&render_ctx->psos[LAYER_SSAO_BLUR]));

}
static void
handle_keyboard_input (SceneContext * scene_ctx, GameTimer * gt) {
    float dt = gt->delta_time;

    if (GetAsyncKeyState('W') & 0x8000)
        Camera_Walk(g_camera, 10.0f * dt);

    if (GetAsyncKeyState('S') & 0x8000)
        Camera_Walk(g_camera, -10.0f * dt);

    if (GetAsyncKeyState('A') & 0x8000)
        Camera_Strafe(g_camera, -10.0f * dt);

    if (GetAsyncKeyState('D') & 0x8000)
        Camera_Strafe(g_camera, 10.0f * dt);

    Camera_UpdateViewMatrix(g_camera);
}
static void
handle_mouse_move (SceneContext * scene_ctx, WPARAM wParam, int x, int y) {
    if (g_mouse_active) {
        if ((wParam & MK_LBUTTON) != 0) {
            // make each pixel correspond to a quarter of a degree
            float dx = DirectX::XMConvertToRadians(0.25f * (float)(x - scene_ctx->mouse.x));
            float dy = DirectX::XMConvertToRadians(0.25f * (float)(y - scene_ctx->mouse.y));

            Camera_Pitch(g_camera, dy);
            Camera_RotateY(g_camera, dx);
        }
    }
    scene_ctx->mouse.x = x;
    scene_ctx->mouse.y = y;
}
static void
handle_mouse_down (
    SceneContext * scene_ctx,
    WPARAM wparam,
    int x, int y,
    HWND hwnd,
    int ritems_count,
    RenderItem ritems []
) {
    if ((wparam & MK_LBUTTON) != 0) {
        scene_ctx->mouse.x = x;
        scene_ctx->mouse.y = y;
        SetCapture(hwnd);
    }
}

static void
update_object_cbuffer (D3DRenderContext * render_ctx) {
    UINT frame_index = render_ctx->frame_index;
    size_t obj_cbuffer_size = sizeof(ObjectConstants);
    uint8_t * obj_begin_ptr = render_ctx->frame_resources[frame_index].obj_ptr;
    for (unsigned i = 0; i < render_ctx->all_ritems.size; i++) {
        if (render_ctx->all_ritems.ritems[i].n_frames_dirty > 0) {
            XMMATRIX world = XMLoadFloat4x4(&render_ctx->all_ritems.ritems[i].world);
            XMMATRIX tex_transform = XMLoadFloat4x4(&render_ctx->all_ritems.ritems[i].tex_transform);
            ObjectConstants data = {};

            XMStoreFloat4x4(&data.world, XMMatrixTranspose(world));
            XMStoreFloat4x4(&data.tex_transform, XMMatrixTranspose(tex_transform));
            data.mat_index = render_ctx->all_ritems.ritems[i].mat->mat_cbuffer_index;

            uint8_t * obj_ptr = obj_begin_ptr + (obj_cbuffer_size * render_ctx->all_ritems.ritems[i].obj_cbuffer_index);
            memcpy(obj_ptr, &data, obj_cbuffer_size);

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
            mat_data.normal_map_index = mat->normal_srvheap_index;

            uint8_t * mat_ptr = render_ctx->frame_resources[frame_index].material_ptr + ((UINT64)mat->mat_cbuffer_index * mat_data_size);
            memcpy(mat_ptr, &mat_data, mat_data_size);

            // Next FrameResource need to be updated too.
            mat->n_frames_dirty--;
        }
    }
}
static void
update_pass_cbuffers (D3DRenderContext * render_ctx, GameTimer * timer) {

    XMMATRIX view = Camera_GetView(g_camera);
    XMMATRIX proj = Camera_GetProj(g_camera);

    XMMATRIX view_proj = XMMatrixMultiply(view, proj);
    XMVECTOR det_view = XMMatrixDeterminant(view);
    XMMATRIX inv_view = XMMatrixInverse(&det_view, view);
    XMVECTOR det_proj = XMMatrixDeterminant(proj);
    XMMATRIX inv_proj = XMMatrixInverse(&det_proj, proj);
    XMVECTOR det_view_proj = XMMatrixDeterminant(view_proj);
    XMMATRIX inv_view_proj = XMMatrixInverse(&det_view_proj, view_proj);

    // transform NDC space to texture space
    // [-1,+1]^2   -->   [0,1]^2
    XMMATRIX T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    );
    XMMATRIX view_proj_tex = XMMatrixMultiply(view_proj, T);
    XMMATRIX shadow_transform = XMLoadFloat4x4(&g_scene_ctx.shadow_transform);

    XMStoreFloat4x4(&render_ctx->main_pass_constants.view, XMMatrixTranspose(view));
    XMStoreFloat4x4(&render_ctx->main_pass_constants.inv_view, XMMatrixTranspose(inv_view));
    XMStoreFloat4x4(&render_ctx->main_pass_constants.proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&render_ctx->main_pass_constants.inv_proj, XMMatrixTranspose(inv_proj));
    XMStoreFloat4x4(&render_ctx->main_pass_constants.view_proj, XMMatrixTranspose(view_proj));
    XMStoreFloat4x4(&render_ctx->main_pass_constants.inv_view_proj, XMMatrixTranspose(inv_view_proj));
    XMStoreFloat4x4(&render_ctx->main_pass_constants.view_proj_tex, XMMatrixTranspose(view_proj_tex));
    XMStoreFloat4x4(&render_ctx->main_pass_constants.shadow_transform, XMMatrixTranspose(shadow_transform));
    render_ctx->main_pass_constants.eye_posw = Camera_GetPosition3f(g_camera);
    render_ctx->main_pass_constants.render_target_size = XMFLOAT2((float)g_scene_ctx.width, (float)g_scene_ctx.height);
    render_ctx->main_pass_constants.inv_render_target_size = XMFLOAT2(1.0f / g_scene_ctx.width, 1.0f / g_scene_ctx.height);
    render_ctx->main_pass_constants.nearz = 1.0f;
    render_ctx->main_pass_constants.farz = 1000.0f;
    render_ctx->main_pass_constants.delta_time = timer->delta_time;
    render_ctx->main_pass_constants.total_time = Timer_GetTotalTime(timer);
    render_ctx->main_pass_constants.ambient_light = {.4f, .4f, .6f, 1.0f};

    render_ctx->main_pass_constants.lights[0].direction = g_scene_ctx.rotated_light_dirs[0];
    render_ctx->main_pass_constants.lights[0].strength = {0.9f, 0.8f, 0.7f};
    render_ctx->main_pass_constants.lights[1].direction = g_scene_ctx.rotated_light_dirs[1];
    render_ctx->main_pass_constants.lights[1].strength = {0.4f, 0.4f, 0.4f};
    render_ctx->main_pass_constants.lights[2].direction = g_scene_ctx.rotated_light_dirs[2];
    render_ctx->main_pass_constants.lights[2].strength = {0.2f, 0.2f, 0.2f};

    //
    // ui params
    if (g_dir_light_enabled)
        render_ctx->main_pass_constants.dir_light_flag = 1;
    else
        render_ctx->main_pass_constants.dir_light_flag = 0;

    uint8_t * pass_ptr = render_ctx->frame_resources[render_ctx->frame_index].pass_cb_ptr;
    memcpy(pass_ptr, &render_ctx->main_pass_constants, sizeof(PassConstants));
}
static void
update_shadow_transform (GameTimer * timer) {
    // Only the first "main" light casts a shadow.
    XMVECTOR light_dir = XMLoadFloat3(&g_scene_ctx.rotated_light_dirs[0]);
    XMVECTOR light_pos = -2.0f * g_scene_ctx.scene_bounds.Radius * light_dir;
    XMVECTOR target_pos = XMLoadFloat3(&g_scene_ctx.scene_bounds.Center);
    XMVECTOR light_up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX light_view = XMMatrixLookAtLH(light_pos, target_pos, light_up);

    XMStoreFloat3(&g_scene_ctx.light_pos_w, light_pos);

    // Transform bounding sphere to light space.
    XMFLOAT3 sphere_center_light_space;
    XMStoreFloat3(&sphere_center_light_space, XMVector3TransformCoord(target_pos, light_view));

    // Ortho frustum in light space encloses scene.
    float l = sphere_center_light_space.x - g_scene_ctx.scene_bounds.Radius;
    float b = sphere_center_light_space.y - g_scene_ctx.scene_bounds.Radius;
    float n = sphere_center_light_space.z - g_scene_ctx.scene_bounds.Radius;
    float r = sphere_center_light_space.x + g_scene_ctx.scene_bounds.Radius;
    float t = sphere_center_light_space.y + g_scene_ctx.scene_bounds.Radius;
    float f = sphere_center_light_space.z + g_scene_ctx.scene_bounds.Radius;

    g_scene_ctx.light_nearz = n;
    g_scene_ctx.light_farz = f;
    XMMATRIX light_proj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

    // Transform NDC space [-1,+1]^2 to texture space [0,1]^2
    XMMATRIX T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    );

    XMMATRIX S = light_view * light_proj * T;
    XMStoreFloat4x4(&g_scene_ctx.light_view_mat, light_view);
    XMStoreFloat4x4(&g_scene_ctx.light_proj_mat, light_proj);
    XMStoreFloat4x4(&g_scene_ctx.shadow_transform, S);
}
static void
update_shadow_pass_cb(ShadowMap * smap, D3DRenderContext * render_ctx, GameTimer * timer) {
    XMMATRIX view = XMLoadFloat4x4(&g_scene_ctx.light_view_mat);
    XMVECTOR det_view = XMMatrixDeterminant(view);
    XMMATRIX proj = XMLoadFloat4x4(&g_scene_ctx.light_proj_mat);
    XMVECTOR det_proj = XMMatrixDeterminant(proj);
    XMMATRIX view_proj = XMMatrixMultiply(view, proj);
    XMVECTOR det_view_proj = XMMatrixDeterminant(view_proj);

    XMMATRIX inv_view = XMMatrixInverse(&det_view, view);
    XMMATRIX inv_proj = XMMatrixInverse(&det_proj, proj);
    XMMATRIX inv_view_proj = XMMatrixInverse(&det_view_proj, view_proj);

    UINT w = smap->width;
    UINT h = smap->height;

    XMStoreFloat4x4(&render_ctx->shadow_pass_constants.view, XMMatrixTranspose(view));
    XMStoreFloat4x4(&render_ctx->shadow_pass_constants.inv_view, XMMatrixTranspose(inv_view));
    XMStoreFloat4x4(&render_ctx->shadow_pass_constants.proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&render_ctx->shadow_pass_constants.inv_proj, XMMatrixTranspose(inv_proj));
    XMStoreFloat4x4(&render_ctx->shadow_pass_constants.view_proj, XMMatrixTranspose(view_proj));
    XMStoreFloat4x4(&render_ctx->shadow_pass_constants.inv_view_proj, XMMatrixTranspose(inv_view_proj));

    render_ctx->shadow_pass_constants.eye_posw = g_scene_ctx.light_pos_w;
    render_ctx->shadow_pass_constants.render_target_size= XMFLOAT2((float)w, (float)h);
    render_ctx->shadow_pass_constants.inv_render_target_size= XMFLOAT2(1.0f / w, 1.0f / h);
    render_ctx->shadow_pass_constants.nearz= g_scene_ctx.light_nearz;
    render_ctx->shadow_pass_constants.farz= g_scene_ctx.light_farz;

    uint8_t * pass_ptr = render_ctx->frame_resources[render_ctx->frame_index].pass_cb_ptr + sizeof(PassConstants);
    memcpy(pass_ptr, &render_ctx->shadow_pass_constants, sizeof(PassConstants));

    //
    // messy way of handling ui param
    if (!g_dir_light_enabled)
        memset(pass_ptr, 0, sizeof(PassConstants));
}
static void
update_ssao_cb (SSAO * ssao, D3DRenderContext * render_ctx, GameTimer * timer) {
    SSAOConstants ssao_cb = {};

    XMMATRIX P = Camera_GetProj(g_camera);

    // transform NDC space [-1,+1]^2 to texture space [0,1]^2
    XMMATRIX T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    );

    ssao_cb.proj = render_ctx->main_pass_constants.proj;
    ssao_cb.inv_proj = render_ctx->main_pass_constants.inv_proj;
    XMStoreFloat4x4(&ssao_cb.proj_tex, XMMatrixTranspose(P * T));

    memcpy(ssao_cb.offset_vectors, ssao->offsets, _countof(ssao->offsets) * sizeof(ssao->offsets[0]));

    int wts_cnt = SSAO_CalculateWeightsCount(ssao, 2.5f);
    float * blur_wts = reinterpret_cast<float *>(calloc(wts_cnt, sizeof(float)));
    SSAO_CalculateGaussWeights(ssao, 2.5f, blur_wts);
    if (blur_wts) {
        ssao_cb.blur_weights[0] = XMFLOAT4(&blur_wts[0]);
        ssao_cb.blur_weights[1] = XMFLOAT4(&blur_wts[4]);
        ssao_cb.blur_weights[2] = XMFLOAT4(&blur_wts[8]);
    }

    UINT w = ssao->render_target_width / 2;
    UINT h = ssao->render_target_height / 2;
    ssao_cb.inv_render_target_size = XMFLOAT2(1.0f / w, 1.0f / h);

    // coords given in view space
    ssao_cb.occlusion_radius = 0.5f;
    ssao_cb.occlusion_fade_start = 0.2f;
    ssao_cb.occlusion_fade_end = 1.0f;
    ssao_cb.surface_epsilon = 0.05f;

    //
    // ui params
    if (g_ssao_enabled) {
        ssao_cb.accessiblity_power = g_accessiblity_power;
        ssao_cb.occlusion_addend = g_occlusion_addend;
    } else {
        ssao_cb.accessiblity_power = 0.0f;
        ssao_cb.occlusion_addend = 0.0f;
    }


    uint8_t * ssao_ptr = render_ctx->frame_resources[render_ctx->frame_index].ssao_ptr;
    memcpy(ssao_ptr, &ssao_cb, sizeof(SSAOConstants));

    free(blur_wts);
}
static void
move_to_next_frame (D3DRenderContext * render_ctx, UINT * frame_index) {

    // Cycle through the circular frame resource array.
    *frame_index = (render_ctx->frame_index + 1) % NUM_QUEUING_FRAMES;
    FrameResource * curr_frame_resource = &render_ctx->frame_resources[*frame_index];

    // -- 3. if the next frame is not ready to be rendered yet, wait until it is ready
    if (
        curr_frame_resource->fence != 0 &&
        render_ctx->fence->GetCompletedValue() < curr_frame_resource->fence
        ) {
        HANDLE event_handle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        if (0 != event_handle) {
            render_ctx->fence->SetEventOnCompletion(
                curr_frame_resource->fence,
                event_handle
            );
            WaitForSingleObject(event_handle, INFINITE);
            CloseHandle(event_handle);
        }
        //WaitForSingleObjectEx(render_ctx->fence_event, INFINITE /*return only when the object is signaled*/, false);
    }
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
draw_scene_to_shadow_map (ShadowMap * smap, D3DRenderContext * render_ctx) {
    UINT frame_index = render_ctx->frame_index;
    ID3D12GraphicsCommandList * cmdlist = render_ctx->direct_cmd_list;

    cmdlist->RSSetViewports(1, &smap->viewport);
    cmdlist->RSSetScissorRects(1, &smap->scissor_rect);

    // change to DEPTH_WRITE
    resource_usage_transition(
        cmdlist,
        smap->shadow_map,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_DEPTH_WRITE
    );

    cmdlist->ClearDepthStencilView(
        smap->hcpu_dsv,
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f, 0, 0, nullptr
    );

    // set null render target bc we only draw to depth buffer
    // active pso should also specify 0 render targets
    cmdlist->OMSetRenderTargets(0, nullptr, false, &smap->hcpu_dsv);

    //bind pass cbuffer for shadow map pass
    UINT pass_cb_byte_size = sizeof(PassConstants);
    ID3D12Resource * pass_cb = render_ctx->frame_resources[frame_index].pass_cb;
    D3D12_GPU_VIRTUAL_ADDRESS pass_cb_address = pass_cb->GetGPUVirtualAddress() + pass_cb_byte_size;
    cmdlist->SetGraphicsRootConstantBufferView(1, pass_cb_address);

    cmdlist->SetPipelineState(render_ctx->psos[LAYER_SHADOW_OPAQUE]);

    draw_render_items(cmdlist, render_ctx->frame_resources[frame_index].obj_cb, &render_ctx->opaque_ritems);

    // change back to generic read so texture can be read in shader
    resource_usage_transition(
        cmdlist,
        smap->shadow_map,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_GENERIC_READ
    );
}
static void
draw_normals_and_depth (SSAO * ssao, D3DRenderContext * render_ctx) {
    UINT frame_index = render_ctx->frame_index;
    ID3D12GraphicsCommandList * cmdlist = render_ctx->direct_cmd_list;

    cmdlist->RSSetViewports(1, &render_ctx->viewport);
    cmdlist->RSSetScissorRects(1, &render_ctx->scissor_rect);

    ID3D12Resource * normal_map = ssao->normal_map;
    D3D12_CPU_DESCRIPTOR_HANDLE normal_map_rtv = ssao->normal_map_cpu_rtv;

    resource_usage_transition(
        cmdlist,
        normal_map,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );

    D3D12_CPU_DESCRIPTOR_HANDLE depth_hcpu = render_ctx->dsv_heap->GetCPUDescriptorHandleForHeapStart();
    float clear_vals [] = {0.0f, 0.0f, 1.0f, 0.0f};
    cmdlist->ClearRenderTargetView(normal_map_rtv, clear_vals, 0, nullptr);
    cmdlist->ClearDepthStencilView(
        depth_hcpu,
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f, 0, 0, nullptr
    );

    cmdlist->OMSetRenderTargets(1, &normal_map_rtv, true, &depth_hcpu);

    ID3D12Resource * pass_cb = render_ctx->frame_resources[frame_index].pass_cb;
    cmdlist->SetGraphicsRootConstantBufferView(1, pass_cb->GetGPUVirtualAddress());

    cmdlist->SetPipelineState(render_ctx->psos[LAYER_DRAW_NORMALS]);
    draw_render_items(
        cmdlist,
        render_ctx->frame_resources[frame_index].obj_cb,
        &render_ctx->opaque_ritems
    );

    resource_usage_transition(
        cmdlist,
        normal_map,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_GENERIC_READ
    );
}
static HRESULT
draw_main (D3DRenderContext * render_ctx, ShadowMap * smap, SSAO * ssao) {
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

    cmdlist->SetGraphicsRootSignature(render_ctx->root_signature);

    //
    // SHADOW MAP pass

    // Bind all materials. For structured buffers, we can bypass heap and set a root descriptor
    ID3D12Resource * mat_buf = render_ctx->frame_resources[frame_index].material_sbuffer;
    cmdlist->SetGraphicsRootShaderResourceView(2, mat_buf->GetGPUVirtualAddress());

    // bind null srv for skymap in shadow map pass
    cmdlist->SetGraphicsRootDescriptorTable(3, render_ctx->null_srv);

    // bind smap srv
    D3D12_GPU_DESCRIPTOR_HANDLE smap_descriptor = render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
    smap_descriptor.ptr += static_cast<UINT64>(render_ctx->cbv_srv_uav_descriptor_size) * render_ctx->shadow_map_heap_index;
    cmdlist->SetGraphicsRootDescriptorTable(4, smap_descriptor);

    // TODO(omid): Bind ssao map later as it is not needed in shadow map pass 
    // bind ssao map srv
    D3D12_GPU_DESCRIPTOR_HANDLE ssao_descriptor = render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
    ssao_descriptor.ptr += static_cast<UINT64>(render_ctx->cbv_srv_uav_descriptor_size) * render_ctx->ssao_heap_index_start;
    cmdlist->SetGraphicsRootDescriptorTable(5, ssao_descriptor);

    // bind all [ordinary] textures.
    // (only specify the first descriptor in the table, root sig knows how many descriptors we have in the table)
    cmdlist->SetGraphicsRootDescriptorTable(6, render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart());

    draw_scene_to_shadow_map(smap, render_ctx);

    //
    // Normal / Depth pass (for SSAO)
    draw_normals_and_depth(ssao, render_ctx);

    //
    // Compute SSAO
    cmdlist->SetGraphicsRootSignature(render_ctx->root_signature_ssao);
    SSAO_ComputeSSAO(ssao, cmdlist, &render_ctx->frame_resources[frame_index], 3);

    //
    // Main Rendering Pass

    // change back to main root sig
    cmdlist->SetGraphicsRootSignature(render_ctx->root_signature);

    // NOTE(omid): REBIND RESOURCES WHENEVER GRAPHICS ROOT SIG CHANGES
    // Rebind state whenever graphics root signature changes...
    mat_buf = render_ctx->frame_resources[frame_index].material_sbuffer;
    cmdlist->SetGraphicsRootShaderResourceView(2, mat_buf->GetGPUVirtualAddress());

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
    rtv_handle.ptr += INT64(render_ctx->backbuffer_index) * INT64(render_ctx->rtv_descriptor_size);    // -- apply initial offset

    cmdlist->ClearRenderTargetView(rtv_handle, (float *)&render_ctx->main_pass_constants.fog_color, 0, nullptr);

    //
    // WE ALREADY WROTE THE DEPTH INFO TO THE DEPTH BUFFER IN "draw_normals_and_depth",
    // SO DO NOT CLEAR DEPTH.
    /*cmdlist->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);*/
    //

    cmdlist->OMSetRenderTargets(1, &rtv_handle, true, &dsv_handle);

    ID3D12Resource * pass_cb = render_ctx->frame_resources[frame_index].pass_cb;
    cmdlist->SetGraphicsRootConstantBufferView(1, pass_cb->GetGPUVirtualAddress());

    // Bind the sky cube map.  For our demos, we just use one "world" cube map representing the environment
    // from far away, so all objects will use the same cube map and we only need to set it once per-frame.  
    // If we wanted to use "local" cube maps, we would have to change them per-object, or dynamically
    // index into an array of cube maps.
    D3D12_GPU_DESCRIPTOR_HANDLE sky_tex_descriptor = render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
    sky_tex_descriptor.ptr += (UINT64)render_ctx->cbv_srv_uav_descriptor_size * render_ctx->sky_tex_heap_index;
    cmdlist->SetGraphicsRootDescriptorTable(3, sky_tex_descriptor);

    // rebind smap srv
    smap_descriptor = render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
    smap_descriptor.ptr += static_cast<UINT64>(render_ctx->cbv_srv_uav_descriptor_size) * render_ctx->shadow_map_heap_index;
    cmdlist->SetGraphicsRootDescriptorTable(4, smap_descriptor);

    // rebind ssao srv
    ssao_descriptor = render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
    ssao_descriptor.ptr += static_cast<UINT64>(render_ctx->cbv_srv_uav_descriptor_size) * render_ctx->ssao_heap_index_start;
    cmdlist->SetGraphicsRootDescriptorTable(5, ssao_descriptor);

    // rebind [ordinary] textures
    cmdlist->SetGraphicsRootDescriptorTable(6, render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart());

    // 1. draw opaque objs
    cmdlist->SetPipelineState(render_ctx->psos[LAYER_OPAQUE]);
    draw_render_items(
        cmdlist,
        render_ctx->frame_resources[frame_index].obj_cb,
        &render_ctx->opaque_ritems
    );

    // 2. draw debug quad for smap
    if (g_show_smap_debug) {
        cmdlist->SetPipelineState(render_ctx->psos[LAYER_DEBUG_SMAP]);
        draw_render_items(
            cmdlist,
            render_ctx->frame_resources[frame_index].obj_cb,
            &render_ctx->debug_ritems_smap
        );
    }

    // 3. draw debug quad for ssao
    if (g_show_ssao_debug) {
        cmdlist->SetPipelineState(render_ctx->psos[LAYER_DEBUG_SSAO]);
        draw_render_items(
            cmdlist,
            render_ctx->frame_resources[frame_index].obj_cb,
            &render_ctx->debug_ritems_ssao
        );
    }

    // 4. draw sky
    cmdlist->SetPipelineState(render_ctx->psos[LAYER_SKY]);
    draw_render_items(
        cmdlist,
        render_ctx->frame_resources[frame_index].obj_cb,
        &render_ctx->environment_ritems
    );

    if (g_imgui_enabled)
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdlist);

    // -- indicate that the backbuffer will now be used to present
    resource_usage_transition(cmdlist, backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    // -- finish populating command list
    cmdlist->Close();

    ID3D12CommandList * cmd_lists [] = {cmdlist};
    render_ctx->cmd_queue->ExecuteCommandLists(ARRAY_COUNT(cmd_lists), cmd_lists);

    render_ctx->swapchain->Present(1 /*sync interval*/, 0 /*present flag*/);
    render_ctx->backbuffer_index = (render_ctx->backbuffer_index + 1) % NUM_BACKBUFFERS;

    // Advance the fence value to mark commands up to this fence point.
    render_ctx->frame_resources[frame_index].fence = ++render_ctx->main_current_fence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    render_ctx->cmd_queue->Signal(render_ctx->fence, render_ctx->main_current_fence);

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

    // Estimate the scene bounding sphere manually since we know how the scene was constructed.
    // The grid is the "widest object" with a width of 20 and depth of 30.0f, and centered at
    // the world space origin.  In general, you need to loop over every world space vertex
    // position and compute the bounding sphere.
    scene_ctx->scene_bounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
    scene_ctx->scene_bounds.Radius = sqrtf(10.0f * 10.0f + 15.0f * 15.0f);

    //
    // Light data for dynamic shadow
    scene_ctx->light_nearz = 0.0f;
    scene_ctx->light_farz = 0.0f;
    scene_ctx->light_view_mat = Identity4x4();
    scene_ctx->light_proj_mat = Identity4x4();
    scene_ctx->shadow_transform = Identity4x4();

    scene_ctx->light_rotation_angle = 0.0f;
    scene_ctx->base_light_dirs[0] = XMFLOAT3(0.57735f, -0.57735f, 0.57735f);
    scene_ctx->base_light_dirs[1] = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);
    scene_ctx->base_light_dirs[2] = XMFLOAT3(0.0f, -0.707f, -0.707f);
}
static void
RenderContext_Init (D3DRenderContext * render_ctx) {
    _ASSERT_EXPR(render_ctx, _T("render-ctx not valid"));
    memset(render_ctx, 0, sizeof(D3DRenderContext));

    render_ctx->viewport.TopLeftX = 0;
    render_ctx->viewport.TopLeftY = 0;
    render_ctx->viewport.Width = (float)g_scene_ctx.width;
    render_ctx->viewport.Height = (float)g_scene_ctx.height;
    render_ctx->viewport.MinDepth = 0.0f;
    render_ctx->viewport.MaxDepth = 1.0f;
    render_ctx->scissor_rect.left = 0;
    render_ctx->scissor_rect.top = 0;
    render_ctx->scissor_rect.right = g_scene_ctx.width;
    render_ctx->scissor_rect.bottom = g_scene_ctx.height;

    // -- initialize fog data
    render_ctx->main_pass_constants.fog_color = {0.7f, 0.7f, 0.7f, 1.0f};
    render_ctx->main_pass_constants.fog_start = 5.0f;
    render_ctx->main_pass_constants.fog_range = 1000.0f;

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
calculate_frame_stats_str (GameTimer * timer, size_t str_count, TCHAR ** out_str) {
    memset(*out_str, '\0', str_count);
    // Code computes the average frames per second, and also the 
    // average time it takes to render one frame.  These stats 
    // are appended to the window caption bar.

    static int frame_count = 0;
    static float time_elapsed = 0.0f;

    frame_count++;

    // Compute averages over one second period.
    if ((Timer_GetTotalTime(timer) - time_elapsed) >= 1.0f) {
        float fps = (float)frame_count; // fps = frame_count / 1
        float mspf = 1000.0f / fps;

        TCHAR str[200];
        int j = _stprintf_s(str, 200, _T("   fps:    %.2f, "), fps);
        j +=    _stprintf_s(str + j, 200 - (size_t)j, _T("   mspf:    %.2f "), mspf);

        _tcscat_s(*out_str, str_count, str);

        // Reset for next average.
        frame_count = 0;
        time_elapsed += 1.0f;
    }
}
static void
calculate_frame_stats (GameTimer * timer, float * out_fps, float * out_mspf) {
    // Code computes the average frames per second, and also the 
    // average time it takes to render one frame.  These stats 
    // are appended to the window caption bar.

    static int frame_count = 0;
    static float time_elapsed = 0.0f;

    frame_count++;

    // Compute averages over one second period.
    if ((Timer_GetTotalTime(timer) - time_elapsed) >= 1.0f) {
        *out_fps = (float)frame_count; // fps = frame_count / 1
        *out_mspf = 1000.0f / (*out_fps);

        // Reset for next average.
        frame_count = 0;
        time_elapsed += 1.0f;
    }
}
static void
d3d_resize (D3DRenderContext * render_ctx) {
    int w = g_scene_ctx.width;
    int h = g_scene_ctx.height;

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
        g_scene_ctx.aspect_ratio = static_cast<float>(w) / h;
    }

    Camera_SetLens(g_camera, 0.25f * XM_PI, g_scene_ctx.aspect_ratio, 1.0f, 1000.0f);

    if (g_ssao && render_ctx) {
        SSAO_Resize(g_ssao, w, h);
        SSAO_RecreateDescriptors(g_ssao, render_ctx->depth_stencil_buffer);
    }
}
static void
check_active_item () {
    if (ImGui::IsItemActive() || ImGui::IsItemHovered())
        g_mouse_active = false;
    else
        g_mouse_active = true;
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
            g_paused = true;
            Timer_Stop(&g_timer);
        } else {
            g_paused = false;
            Timer_Start(&g_timer);
        }
    } break;

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN: {
        _ASSERT_EXPR(_render_ctx, _T("Uninitialized render context!"));
        handle_mouse_down(
            &g_scene_ctx,
            wParam,
            GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
            hwnd,
            _render_ctx->opaque_ritems.size,
            _render_ctx->opaque_ritems.ritems
        );
    } break;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP: {
        ReleaseCapture();
    } break;
    case WM_MOUSEMOVE: {
        handle_mouse_move(&g_scene_ctx, wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    } break;
    case WM_SIZE: {
        g_scene_ctx.width = LOWORD(lParam);
        g_scene_ctx.height = HIWORD(lParam);
        if (_render_ctx) {
            if (wParam == SIZE_MINIMIZED) {
                g_paused = true;
            } else if (wParam == SIZE_MAXIMIZED) {
                g_paused = false;
                d3d_resize(_render_ctx);
            } else if (wParam == SIZE_RESTORED) {
                // TODO(omid): handle restore from minimize/maximize 
                if (g_resizing) {
                    // don't do nothing until resizing finished
                } else {
                    d3d_resize(_render_ctx);
                }
            }
        }
    } break;
    // WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
    case WM_ENTERSIZEMOVE: {
        g_paused = true;
        g_resizing  = true;
        Timer_Stop(&g_timer);
    } break;
    // WM_EXITSIZEMOVE is sent when the user releases the resize bars.
    // Here we reset everything based on the new window dimensions.
    case WM_EXITSIZEMOVE: {
        g_paused = false;
        g_resizing  = false;
        Timer_Start(&g_timer);
        d3d_resize(_render_ctx);
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

    SceneContext_Init(&g_scene_ctx, 1280, 720);
    D3DRenderContext * render_ctx = (D3DRenderContext *)::malloc(sizeof(D3DRenderContext));
    RenderContext_Init(render_ctx);

    // Camera Initial Setup
    size_t cam_size = Camera_CalculateRequiredSize();
    g_camera = (Camera *)malloc(cam_size);
    Camera_Init(g_camera);
    Camera_SetPosition(g_camera, 0.0f, 2.0f, -15.0f);

    // ========================================================================================================
#pragma region Windows_Setup
    WNDCLASS wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = main_win_cb;
    wc.hInstance = hInstance;
    wc.lpszClassName = _T("d3d12_win32");

    _ASSERT_EXPR(RegisterClass(&wc), _T("could not register window class"));

    // Compute window rectangle dimensions based on requested client area dimensions.
    RECT R = {0, 0, (long int)g_scene_ctx.width, (long int)g_scene_ctx.height};
    AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
    int width  = R.right - R.left;
    int height = R.bottom - R.top;

    HWND hwnd = CreateWindowEx(
        0,                                              // Optional window styles.
        wc.lpszClassName,                               // Window class
        _T("SSAO Demo"),                      // Window title
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
        DBG_PRINT(_T("GPU Info [%d] :\n"), i);
        if (SUCCEEDED(pAdapter->GetDesc(&adapter_desc))) {
            DBG_PRINT(_T("\tDescription: %ls\n"), adapter_desc.Description);
            DBG_PRINT(_T("\tDedicatedVideoMemory: %zu\n"), adapter_desc.DedicatedVideoMemory);
        }
    } // WARP -> Windows Advanced Rasterization ...

    // HOTFIX for D3D12 expecting only signed shaders at PSO create
    // To see shader validation errors copy DXIL.lib into executable directory
    // Also using -Vd flag with dxc disables validations
    // https://github.com/microsoft/DirectXShaderCompiler/issues/2550
    // https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12enableexperimentalfeatures
    static const UUID D3D12ExperimentalShaderModels = { /* 76f5573e-f13a-40f5-b297-81ce9e18933f */
      0x76f5573e,
      0xf13a,
      0x40f5,
      { 0xb2, 0x97, 0x81, 0xce, 0x9e, 0x18, 0x93, 0x3f }
    };
    D3D12EnableExperimentalFeatures(
      1,
      &D3D12ExperimentalShaderModels,
      NULL,
      NULL
    );

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

    // store DSV descriptor increment size
    render_ctx->dsv_descriptor_size = render_ctx->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_DSV);


    //
    // Shadow Map Setup
    g_smap = (ShadowMap *)malloc(sizeof(ShadowMap));
    ShadowMap_Init(g_smap, render_ctx->device, 2048, 2048, DXGI_FORMAT_R24G8_TYPELESS);


    //
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


    //
    // SSAO setup
    g_ssao = (SSAO *)malloc(sizeof(SSAO));
    SSAO_Init(g_ssao, render_ctx->device, render_ctx->direct_cmd_list, g_scene_ctx.width, g_scene_ctx.height);




    DXGI_MODE_DESC backbuffer_desc = {};
    backbuffer_desc.Width = g_scene_ctx.width;
    backbuffer_desc.Height = g_scene_ctx.height;
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
    // brick tex
    strcpy_s(render_ctx->textures[BRICK_DIFFUSE_MAP].name, "tex_brick");
    wcscpy_s(render_ctx->textures[BRICK_DIFFUSE_MAP].filename, L"../Textures/bricks2.dds");
    load_texture(
        render_ctx->device, render_ctx->direct_cmd_list,
        render_ctx->textures[BRICK_DIFFUSE_MAP].filename,
        &render_ctx->textures[BRICK_DIFFUSE_MAP]
    );
    // tile tex
    strcpy_s(render_ctx->textures[TILE_DIFFUSE_MAP].name, "tex_tile");
    wcscpy_s(render_ctx->textures[TILE_DIFFUSE_MAP].filename, L"../Textures/tile.dds");
    load_texture(
        render_ctx->device, render_ctx->direct_cmd_list,
        render_ctx->textures[TILE_DIFFUSE_MAP].filename,
        &render_ctx->textures[TILE_DIFFUSE_MAP]
    );
    // default tex
    strcpy_s(render_ctx->textures[WHITE1x1_DIFFUSE_MAP].name, "tex_default");
    wcscpy_s(render_ctx->textures[WHITE1x1_DIFFUSE_MAP].filename, L"../Textures/white1x1.dds");
    load_texture(
        render_ctx->device, render_ctx->direct_cmd_list,
        render_ctx->textures[WHITE1x1_DIFFUSE_MAP].filename,
        &render_ctx->textures[WHITE1x1_DIFFUSE_MAP]
    );
    // bricks normal map
    strcpy_s(render_ctx->textures[BRICK_NORMAL_MAP].name, "bricks nmap");
    wcscpy_s(render_ctx->textures[BRICK_NORMAL_MAP].filename, L"../Textures/bricks2_nmap.dds");
    load_texture(
        render_ctx->device, render_ctx->direct_cmd_list,
        render_ctx->textures[BRICK_NORMAL_MAP].filename,
        &render_ctx->textures[BRICK_NORMAL_MAP]
    );
    // tile normal map
    strcpy_s(render_ctx->textures[TILE_NORMAL_MAP].name, "tile nmap");
    wcscpy_s(render_ctx->textures[TILE_NORMAL_MAP].filename, L"../Textures/tile_nmap.dds");
    load_texture(
        render_ctx->device, render_ctx->direct_cmd_list,
        render_ctx->textures[TILE_NORMAL_MAP].filename,
        &render_ctx->textures[TILE_NORMAL_MAP]
    );
    // default normal map
    strcpy_s(render_ctx->textures[WHITE1x1_NORMAL_MAP].name, "default nmap");
    wcscpy_s(render_ctx->textures[WHITE1x1_NORMAL_MAP].filename, L"../Textures/default_nmap.dds");
    load_texture(
        render_ctx->device, render_ctx->direct_cmd_list,
        render_ctx->textures[WHITE1x1_NORMAL_MAP].filename,
        &render_ctx->textures[WHITE1x1_NORMAL_MAP]
    );
    // sky cube-map tex0
    strcpy_s(render_ctx->textures[TEX_SKY_CUBEMAP0].name, "tex_sky_cubemap");
    wcscpy_s(render_ctx->textures[TEX_SKY_CUBEMAP0].filename, L"../Textures/grasscube1024.dds");
    load_texture(
        render_ctx->device, render_ctx->direct_cmd_list,
        render_ctx->textures[TEX_SKY_CUBEMAP0].filename,
        &render_ctx->textures[TEX_SKY_CUBEMAP0]
    );
    // sky cube-map tex1
    strcpy_s(render_ctx->textures[TEX_SKY_CUBEMAP1].name, "tex_sky_cubemap1");
    wcscpy_s(render_ctx->textures[TEX_SKY_CUBEMAP1].filename, L"../Textures/desertcube1024.dds");
    load_texture(
        render_ctx->device, render_ctx->direct_cmd_list,
        render_ctx->textures[TEX_SKY_CUBEMAP1].filename,
        &render_ctx->textures[TEX_SKY_CUBEMAP1]
    );
    // sky cube-map tex2
    strcpy_s(render_ctx->textures[TEX_SKY_CUBEMAP2].name, "tex_sky_cubemap2");
    wcscpy_s(render_ctx->textures[TEX_SKY_CUBEMAP2].filename, L"../Textures/snowcube1024.dds");
    load_texture(
        render_ctx->device, render_ctx->direct_cmd_list,
        render_ctx->textures[TEX_SKY_CUBEMAP2].filename,
        &render_ctx->textures[TEX_SKY_CUBEMAP2]
    );
    // sky cube-map tex3
    strcpy_s(render_ctx->textures[TEX_SKY_CUBEMAP3].name, "tex_sky_cubemap3");
    wcscpy_s(render_ctx->textures[TEX_SKY_CUBEMAP3].filename, L"../Textures/sunsetcube1024.dds");
    load_texture(
        render_ctx->device, render_ctx->direct_cmd_list,
        render_ctx->textures[TEX_SKY_CUBEMAP3].filename,
        &render_ctx->textures[TEX_SKY_CUBEMAP3]
    );
#pragma endregion

#if 1   // For SSAO, dsv and depth buffer should be created before SSAO descriptors setup
    //
    // Create Render Target View Descriptor Heap
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
    rtv_heap_desc.NumDescriptors = NUM_BACKBUFFERS + 1 /* SSAO normal map */ + 2 /* SSAO ambient maps */;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAGS::D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    render_ctx->device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&render_ctx->rtv_heap));

    //
    // Create Depth Stencil View Descriptor Heap
    // +1 DSV for shadow map
    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc;
    dsv_heap_desc.NumDescriptors = 2;
    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsv_heap_desc.NodeMask = 0;
    render_ctx->device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&render_ctx->dsv_heap));
#endif // 1

#pragma region Dsv_Creation
// Create the depth/stencil buffer and view.
    D3D12_RESOURCE_DESC ds_desc;
    ds_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    ds_desc.Alignment = 0;
    ds_desc.Width = g_scene_ctx.width;
    ds_desc.Height = g_scene_ctx.height;
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

    // depth buffer should be created before SSAO descriptors setup
    create_descriptor_heaps(render_ctx, g_smap, g_ssao);

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

#pragma region Shapes_And_Renderitem_Creation
    create_skull_geometry(render_ctx);
    create_shapes_geometry(render_ctx);
    create_materials(render_ctx->materials);
    create_render_items(render_ctx);

#pragma endregion 

#pragma region Create CBuffers, MaterialData and InstanceData Buffers
    UINT obj_cb_size = sizeof(ObjectConstants);
    UINT mat_data_size = sizeof(MaterialData);
    UINT pass_cb_size = sizeof(PassConstants);
    UINT ssao_cb_size = sizeof(SSAOConstants);
    for (UINT i = 0; i < NUM_QUEUING_FRAMES; ++i) {
        // -- create a cmd-allocator for each frame
        res = render_ctx->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&render_ctx->frame_resources[i].cmd_list_alloc));

        create_upload_buffer(render_ctx->device, (UINT64)obj_cb_size * _COUNT_RENDERITEM, &render_ctx->frame_resources[i].obj_ptr, &render_ctx->frame_resources[i].obj_cb);

        create_upload_buffer(render_ctx->device, (UINT64)mat_data_size * _COUNT_MATERIAL, &render_ctx->frame_resources[i].material_ptr, &render_ctx->frame_resources[i].material_sbuffer);

        create_upload_buffer(render_ctx->device, (UINT64)pass_cb_size * 2, &render_ctx->frame_resources[i].pass_cb_ptr, &render_ctx->frame_resources[i].pass_cb);

        create_upload_buffer(render_ctx->device, (UINT64)ssao_cb_size * 1, &render_ctx->frame_resources[i].ssao_ptr, &render_ctx->frame_resources[i].ssao_cb);
    }
#pragma endregion

    // ========================================================================================================
#pragma region Root_Signature_Creation
    create_root_signature(render_ctx->device, &render_ctx->root_signature);

    create_root_signature_ssao(render_ctx->device, &render_ctx->root_signature_ssao);

#pragma endregion Root_Signature_Creation

    // Load and compile shaders

#pragma region Compile Shaders
    TCHAR standard_shader_path [] = _T("./shaders/default.hlsl");
    TCHAR sky_shader_path [] = _T("./shaders/sky.hlsl");
    TCHAR shadow_shader_path [] = _T("./shaders/shadows.hlsl");
    TCHAR debug_smap_shader_path [] = _T("./shaders/shadow_debug.hlsl");
    TCHAR debug_ssao_shader_path [] = _T("./shaders/ssao_debug.hlsl");
    TCHAR draw_normals_shader_path [] = _T("./shaders/draw_normals.hlsl");
    TCHAR ssao_shader_path [] = _T("./shaders/ssao.hlsl");
    TCHAR ssao_blur_shader_path [] = _T("./shaders/ssao_blur.hlsl");

    {   // standard shaders
        compile_shader(standard_shader_path, _T("VertexShader_Main"), _T("vs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_STANDARD_VS]);

        int const n_define_fog = 1;
        DxcDefine defines_fog[n_define_fog] = {};
        defines_fog[0] = {.Name = _T("FOG"), .Value = _T("1")};
        compile_shader(standard_shader_path, _T("PixelShader_Main"), _T("ps_6_0"), defines_fog, n_define_fog, &render_ctx->shaders[SHADER_OPAQUE_PS]);
    }
    {   // shadow shaders
        compile_shader(shadow_shader_path, _T("VS"), _T("vs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_SHADOW_VS]);
        compile_shader(shadow_shader_path, _T("PS"), _T("ps_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_SHADOW_OPAQUE_PS]);

        int const n_define_alphatest = 1;
        DxcDefine defines_alphatest[n_define_alphatest] = {};
        defines_alphatest[0] = {.Name = _T("ALPHA_TEST"), .Value = _T("1")};
        compile_shader(shadow_shader_path, _T("PS"), _T("ps_6_0"), defines_alphatest, n_define_alphatest, &render_ctx->shaders[SHADER_SHADOW_ALPHATESTED_PS]);
    }
    {   // shadow debug shaders
        compile_shader(debug_smap_shader_path, _T("VS"), _T("vs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_DEBUG_SMAP_VS]);
        compile_shader(debug_smap_shader_path, _T("PS"), _T("ps_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_DEBUG_SMAP_PS]);
    }
    {   // ssao debug shaders
        compile_shader(debug_ssao_shader_path, _T("VS"), _T("vs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_DEBUG_SSAO_VS]);
        compile_shader(debug_ssao_shader_path, _T("PS"), _T("ps_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_DEBUG_SSAO_PS]);
    }
    {   // sky shaders
        compile_shader(sky_shader_path, _T("VS"), _T("vs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_SKY_VS]);

        compile_shader(sky_shader_path, _T("PS"), _T("ps_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_SKY_PS]);
    }
    {   // draw normals shaders
        compile_shader(draw_normals_shader_path, _T("VS"), _T("vs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_DRAW_NORMALS_VS]);

        compile_shader(draw_normals_shader_path, _T("PS"), _T("ps_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_DRAW_NORMALS_PS]);
    }
    {   // ssao shaders
        compile_shader(ssao_shader_path, _T("VS"), _T("vs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_SSAO_VS]);

        compile_shader(ssao_shader_path, _T("PS"), _T("ps_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_SSAO_PS]);
    }
    {   // ssao blur shaders
        compile_shader(ssao_blur_shader_path, _T("VS"), _T("vs_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_SSAO_BLUR_VS]);

        compile_shader(ssao_blur_shader_path, _T("PS"), _T("ps_6_0"), nullptr, 0, &render_ctx->shaders[SHADER_SSAO_BLUR_PS]);
    }
#pragma endregion


    create_pso(render_ctx);
    SSAO_SetPSOs(g_ssao, render_ctx->psos[LAYER_SSAO], render_ctx->psos[LAYER_SSAO_BLUR]);


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
    bool * ptr_open = nullptr;
    ImGuiWindowFlags window_flags = 0;
    bool beginwnd, slider1, slider2;
    int selected_mat = 0;
    if (g_imgui_enabled) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.Fonts->AddFontDefault();
        ImGui::StyleColorsDark();

        // calculate imgui cpu & gpu handles on location on srv_heap
        D3D12_CPU_DESCRIPTOR_HANDLE imgui_cpu_handle = render_ctx->srv_heap->GetCPUDescriptorHandleForHeapStart();
        imgui_cpu_handle.ptr += ((size_t)render_ctx->cbv_srv_uav_descriptor_size * (
            _COUNT_TEX
            + 3 /* one for ShadowMap srv, two other null srvs for shadow hlsl code (null cube and tex) */
            + 5 /* SSAO srvs */
            ));

        D3D12_GPU_DESCRIPTOR_HANDLE imgui_gpu_handle = render_ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
        imgui_gpu_handle.ptr += ((size_t)render_ctx->cbv_srv_uav_descriptor_size * (
            _COUNT_TEX
            + 3 /* one for ShadowMap srv, two other null srvs for shadow hlsl code (null cube and tex) */
            + 5 /* SSAO srvs */
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
    g_paused = false;
    g_resizing = false;
    g_mouse_active = true;
    Timer_Init(&g_timer);
    Timer_Reset(&g_timer);

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        } else {
#pragma region Imgui window
            if (g_imgui_enabled) {
                ImGui_ImplDX12_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                ImGui::Begin("Settings", ptr_open, window_flags);
                beginwnd = ImGui::IsItemActive();

                ImGui::Combo(
                    "Skybox Texture", &selected_mat,
                    "   Grass Cube\0   Desert Cube\0   Snow Cube\0   Sunset Cube\0\0");

                ImGui::Separator();
                ImGui::Checkbox("Enable SSAO", &g_ssao_enabled);

                ImGui::Separator();
                ImGui::Checkbox("Enable Directional Lights", &g_dir_light_enabled);

                ImGui::Separator();

                ImGui::Text("Ambient Accessiblity Power:");
                ImGui::SliderFloat("Ambient Intensity", &g_accessiblity_power, 4.0f, 40.0f);
                slider1 = ImGui::IsItemActive();

                ImGui::Separator();

                ImGui::Text("Ambient Factor Addend Value:");
                ImGui::SliderFloat("Indirect Factor", &g_occlusion_addend, 0.0f, 0.4f);
                slider2 = ImGui::IsItemActive();

                ImGui::Separator();
                ImGui::Checkbox("Show Shadow Mapping Debug Window", &g_show_smap_debug);
                ImGui::Checkbox("Show SSAO Debug Window", &g_show_ssao_debug);

                ImGui::Text("\n\n");
                ImGui::Separator();
                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

                ImGui::End();
                ImGui::Render();

                // choose skybox texture
                if (0 == selected_mat)
                    render_ctx->sky_tex_heap_index = TEX_SKY_CUBEMAP0;
                else if (1 == selected_mat)
                    render_ctx->sky_tex_heap_index = TEX_SKY_CUBEMAP1;
                else if (2 == selected_mat)
                    render_ctx->sky_tex_heap_index = TEX_SKY_CUBEMAP2;
                else if (3 == selected_mat)
                    render_ctx->sky_tex_heap_index = TEX_SKY_CUBEMAP3;

                // control mouse activation
                g_mouse_active = !(beginwnd || slider1 || slider2);
            }
#pragma endregion
            Timer_Tick(&g_timer);

            if (!g_paused) {
                move_to_next_frame(render_ctx, &render_ctx->frame_index);

                //
                // Animate the lights (and hence shadows).
                g_scene_ctx.light_rotation_angle += 0.1f * g_timer.delta_time;
                XMMATRIX R = XMMatrixRotationY(g_scene_ctx.light_rotation_angle);
                for (int i = 0; i < 3; ++i) {
                    XMVECTOR light_dir = XMLoadFloat3(&g_scene_ctx.base_light_dirs[i]);
                    light_dir = XMVector3TransformNormal(light_dir, R);
                    XMStoreFloat3(&g_scene_ctx.rotated_light_dirs[i], light_dir);
                }

                handle_keyboard_input(&g_scene_ctx, &g_timer);
                update_mat_buffer(render_ctx);

                update_pass_cbuffers(render_ctx, &g_timer);

                update_shadow_transform(&g_timer);
                update_shadow_pass_cb(g_smap, render_ctx, &g_timer);
                update_ssao_cb(g_ssao, render_ctx, &g_timer);

                update_object_cbuffer(render_ctx);

                draw_main(render_ctx, g_smap, g_ssao);

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
    if (g_imgui_enabled) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    // release queuing frame resources
    for (size_t i = 0; i < NUM_QUEUING_FRAMES; i++) {
        flush_command_queue(render_ctx);    // TODO(omid): Address the cbuffers release issue 
        render_ctx->frame_resources[i].obj_cb->Unmap(0, nullptr);
        render_ctx->frame_resources[i].material_sbuffer->Unmap(0, nullptr);
        render_ctx->frame_resources[i].pass_cb->Unmap(0, nullptr);
        render_ctx->frame_resources[i].ssao_cb->Unmap(0, nullptr);
        render_ctx->frame_resources[i].obj_cb->Release();
        render_ctx->frame_resources[i].material_sbuffer->Release();
        render_ctx->frame_resources[i].pass_cb->Release();
        render_ctx->frame_resources[i].ssao_cb->Release();

        render_ctx->frame_resources[i].cmd_list_alloc->Release();
    }
    CloseHandle(render_ctx->fence_event);
    render_ctx->fence->Release();

    for (unsigned i = 0; i < _COUNT_GEOM; i++) {
        render_ctx->geom[i].ib_uploader->Release();
        render_ctx->geom[i].vb_uploader->Release();
        render_ctx->geom[i].vb_gpu->Release();
        render_ctx->geom[i].ib_gpu->Release();
    }

    for (int i = 0; i < _COUNT_RENDERCOMPUTE_LAYER; ++i)
        render_ctx->psos[i]->Release();

    for (unsigned i = 0; i < _COUNT_SHADERS; ++i)
        render_ctx->shaders[i]->Release();

    render_ctx->root_signature->Release();
    render_ctx->root_signature_ssao->Release();

    // release swapchain backbuffers resources
    for (unsigned i = 0; i < NUM_BACKBUFFERS; ++i)
        render_ctx->render_targets[i]->Release();

    render_ctx->dsv_heap->Release();
    render_ctx->rtv_heap->Release();
    render_ctx->srv_heap->Release();

    render_ctx->depth_stencil_buffer->Release();

    for (unsigned i = 0; i < (_COUNT_TEX); i++) {
        render_ctx->textures[i].upload_heap->Release();
        render_ctx->textures[i].resource->Release();
    }

    ShadowMap_Deinit(g_smap);
    ::free(g_smap);

    SSAO_Deinit(g_ssao);
    ::free(g_ssao);

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

    ::free(g_camera);

#pragma endregion Cleanup_And_Debug

    return 0;
}


