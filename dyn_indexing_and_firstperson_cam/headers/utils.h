/* ===========================================================
   #File: utils.cpp #
   #Date: 25 Jan 2021 #
   #Revision: 1.0 #
   #Creator: Omid Miresmaeili #
   #Description: renderer utility tools #
   #Notice: (C) Copyright 2021 by Omid. All Rights Reserved. #
   =========================================================== */
#pragma once

#include "common.h"
#include "mesh_geometry.h"

#define ARRAY_COUNT(arr)                sizeof(arr)/sizeof(arr[0])
#define CLAMP_VALUE(val, lb, ub)        ((val) < (lb)) ? (lb) : ((val) > (ub) ? (ub) : (val))

using namespace DirectX;

struct Light {
    XMFLOAT3    strength;
    float       falloff_start;
    XMFLOAT3    direction;
    float       falloff_end;
    XMFLOAT3    position;
    float       spot_power;
};

#define MAX_LIGHTS  16

// -- per object constants
struct ObjectConstants {
    XMFLOAT4X4 world;
    XMFLOAT4X4 tex_transform;
    XMFLOAT2 displacement_texel_size;
    float grid_spatial_step;
    float pad;
    float padding[28];  // Padding so the constant buffer is 256-byte aligned
};
static_assert(256 == sizeof(ObjectConstants), "Constant buffer size must be 256b aligned");
// -- per pass constants
struct PassConstants {
    XMFLOAT4X4 view;
    XMFLOAT4X4 inverse_view;
    XMFLOAT4X4 proj;
    XMFLOAT4X4 inverse_proj;
    XMFLOAT4X4 view_proj;
    XMFLOAT4X4 inverse_view_proj;
    XMFLOAT3 eye_posw;
    float cbuffer_per_obj_pad1;
    XMFLOAT2 render_target_size;
    XMFLOAT2 inverse_render_target_size;
    float nearz;
    float farz;
    float total_time;
    float delta_time;

    XMFLOAT4 ambient_light;

    XMFLOAT4 fog_color;
    float fog_start;
    float fog_range;
    XMFLOAT2 cbuffer_per_obj_pad2;

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MAX_LIGHTS per object.
    Light lights[MAX_LIGHTS];

    float padding[8];  // Padding so the constant buffer is 256-byte aligned
};
static_assert(1280 == sizeof(PassConstants), "Constant buffer size must be 256b aligned");

struct Vertex {
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 texc;
};
struct GeomVertex {
    XMFLOAT3 Position;
    XMFLOAT3 Normal;
    XMFLOAT3 TangentU;
    XMFLOAT2 TexC;
};

// -- relevant material data in cbuffer
struct MaterialConstants {
    XMFLOAT4    diffuse_albedo;
    XMFLOAT3    fresnel_r0;
    float       roughness;

    // used in texture mapping
    XMFLOAT4X4  mat_transform;

    float padding[40];  // Padding so the constant buffer is 256-byte aligned
};
static_assert(256 == sizeof(MaterialConstants), "Constant buffer size must be 256b aligned");

// NOTE(omid): A production 3D engine would likely create a hierarchy of Materials.
// -- simple struct to represent a material. 
struct Material {
    char name[50];

    // Index into constant buffer corresponding to this material.
    int mat_cbuffer_index;

    // Index into SRV heap for diffuse texture.
    int diffuse_srvheap_index;

    // Index into SRV heap for normal texture.
    int normal_srvheap_index;

    // Dirty flag indicating the material has changed and we need to update the constant buffer.
    int n_frames_dirty;

    // Material constant buffer data used for shading.
    XMFLOAT4 diffuse_albedo;
    XMFLOAT3 fresnel_r0;
    float roughness;
    XMFLOAT4X4 mat_transform;
};

struct Texture {
    char name[50];
    wchar_t filename[250];

    ID3D12Resource * resource;
    ID3D12Resource * upload_heap;
};

// FrameResource stores the resources needed for the CPU to build the command lists for a frame.
struct FrameResource {
    // We cannot reset the allocator until the GPU is done processing the commands.
    // So each frame needs their own allocator.
    ID3D12CommandAllocator * cmd_list_alloc;

    // We cannot update a cbuffer until the GPU is done processing the commands
    // that reference it.  So each frame needs their own cbuffers.
    ID3D12Resource * pass_cb;
    PassConstants pass_cb_data;
    uint8_t * pass_cb_data_ptr;

    ID3D12Resource * mat_cb;
    MaterialConstants mat_cb_data;
    uint8_t * mat_cb_data_ptr;

    ID3D12Resource * obj_cb;
    ObjectConstants obj_cb_data;
    uint8_t * obj_cb_data_ptr;

    // Fence value to mark commands up to this fence point.  This lets us
    // check if these frame resources are still in use by the GPU.
    UINT64 fence;
};
struct RenderItem {
    bool initialized;

    // World matrix of the shape that describes the object's local space
    // relative to the world space, which defines the position, orientation,
    // and scale of the object in the world.
    XMFLOAT4X4 world;

    XMFLOAT4X4 tex_transform;

    // Dirty flag indicating the object data has changed and we need to update the constant buffer.
    // Because we have an object cbuffer for each FrameResource, we have to apply the
    // update to each FrameResource.  Thus, when we modify obect data we should set 
    // n_frames_dirty = NUM_QUEUING_FRAMES so that each frame resource gets the update.
    int n_frames_dirty;

    // Index into GPU constant buffer corresponding to the ObjectCB for this render item.
    UINT obj_cbuffer_index;

    // TODO(omid): add mesh/geometry data 

    D3D12_PRIMITIVE_TOPOLOGY primitive_type;

    // DrawIndexedInstanced parameters.
    UINT index_count;
    UINT start_index_loc;
    int base_vertex_loc;

    Material * mat;
    MeshGeometry * geometry;

    // Stuff for GPU waves render_items
    XMFLOAT2 displacement_map_texel_size;
    float grid_spatial_step;
};

static XMFLOAT4X4
Identity4x4() {
    static XMFLOAT4X4 I(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);

    return I;
}

static int
rand_int (int a, int b) {
    return a + rand() % ((b - a) + 1);
}
// Returns random float in [0, 1).
static float
rand_float () {
    return (float)(rand()) / (float)RAND_MAX;
}
// Returns random float in [a, b).
static float
rand_float (float a, float b) {
    return a + rand_float() * (b - a);
}

static void
create_upload_buffer (ID3D12Device * device, UINT64 total_size, BYTE ** mapped_data, ID3D12Resource ** out_upload_buffer) {

    D3D12_HEAP_PROPERTIES heap_props = {};
    heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.CreationNodeMask = 1U;
    heap_props.VisibleNodeMask = 1U;

    D3D12_RESOURCE_DESC rsc_desc = {};
    rsc_desc.Dimension = D3D12_RESOURCE_DIMENSION::D3D12_RESOURCE_DIMENSION_BUFFER;
    rsc_desc.Alignment = 0;
    rsc_desc.Width = total_size;
    rsc_desc.Height = 1;
    rsc_desc.DepthOrArraySize = 1;
    rsc_desc.MipLevels = 1;
    rsc_desc.Format = DXGI_FORMAT_UNKNOWN;
    rsc_desc.SampleDesc.Count = 1;
    rsc_desc.SampleDesc.Quality = 0;
    rsc_desc.Layout = D3D12_TEXTURE_LAYOUT::D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rsc_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    CHECK_AND_FAIL(device->CreateCommittedResource(
        &heap_props,
        D3D12_HEAP_FLAG_NONE,
        &rsc_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(out_upload_buffer)));

    D3D12_RANGE mem_range = {};
    mem_range.Begin = 0;
    mem_range.End = 0;
    CHECK_AND_FAIL((*out_upload_buffer)->Map(0, &mem_range, reinterpret_cast<void**>(mapped_data)));

    // We do not need to unmap until we are done with the resource.  However, we must not write to
    // the resource while it is in use by the GPU (so we must use synchronization techniques).
    // We don't unmap this until the app closes. 

    // NOTE(omid): Keeping things mapped for the lifetime of the resource is okay.
    // (*out_upload_buffer)->Unmap(0, nullptr /*aka full-range*/);
}
// Returns required size of a buffer to be used for data upload
inline UINT64
get_required_intermediate_size (
    _In_ ID3D12Resource* dst_resource,
    _In_range_(0, D3D12_REQ_SUBRESOURCES) UINT first_subresource,
    _In_range_(0, D3D12_REQ_SUBRESOURCES - first_subresource) UINT n_subresources) {
    D3D12_RESOURCE_DESC desc = dst_resource->GetDesc();
    UINT64 required_size = 0;

    ID3D12Device * device;
    dst_resource->GetDevice(__uuidof(*device), reinterpret_cast<void**>(&device));
    device->GetCopyableFootprints(&desc, first_subresource, n_subresources, 0, nullptr, nullptr, nullptr, &required_size);
    device->Release();

    return required_size;
}
// Heap-allocating UpdateSubresources implementation
 /*refer to heap-allocating UpdateSubresources implementation in d3dx12.h (towards the end)*/
inline UINT64
update_subresources_heap (
    _In_ ID3D12GraphicsCommandList * cmd_list,
    _In_ ID3D12Resource * dest_resource,
    _In_ ID3D12Resource * intermediate,
    UINT64 intermediate_offset,
    _In_range_(0, D3D12_REQ_SUBRESOURCES) UINT first_subresource,
    _In_range_(0, D3D12_REQ_SUBRESOURCES - first_subresource) UINT n_subresources,
    _In_reads_(n_subresources) D3D12_SUBRESOURCE_DATA * src_data
) {
    UINT64 required_size = 0;
    UINT64 mem_to_alloc = static_cast<UINT64>(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(UINT) + sizeof(UINT64)) * n_subresources;
    if (mem_to_alloc > SIZE_MAX) {
        return 0;
    }
    void * mem_ptr = HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(mem_to_alloc));
    if (mem_ptr == NULL) {
        return 0;
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT* layouts = reinterpret_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(mem_ptr);
    UINT64* row_sizes_in_bytes = reinterpret_cast<UINT64*>(layouts + n_subresources);
    UINT* n_rows = reinterpret_cast<UINT*>(row_sizes_in_bytes + n_subresources);

    D3D12_RESOURCE_DESC desc = dest_resource->GetDesc();
    ID3D12Device* device;
    dest_resource->GetDevice(__uuidof(*device), reinterpret_cast<void**>(&device));
    device->GetCopyableFootprints(&desc, first_subresource, n_subresources, intermediate_offset, layouts, n_rows, row_sizes_in_bytes, &required_size);
    device->Release();

#pragma region UpdateSubresources (Heap & Stack)
    // Minor validation
    D3D12_RESOURCE_DESC intermediate_desc = intermediate->GetDesc();
    D3D12_RESOURCE_DESC destination_desc = dest_resource->GetDesc();
    _ASSERT_EXPR(!(intermediate_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
                         intermediate_desc.Width < required_size + layouts[0].Offset ||
                         required_size > (SIZE_T) - 1 ||
                         (destination_desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
                          (first_subresource != 0 || n_subresources != 1))), _T("validation failed!"));

    BYTE * data;
    /*_ASSERT_EXPR*/(intermediate->Map(0, NULL, reinterpret_cast<void**>(&data)), _T("Mapping intermediate resource failed"));

    for (UINT i = 0; i < n_subresources; ++i) {
        if (row_sizes_in_bytes[i] > (SIZE_T)-1) return 0;
        D3D12_MEMCPY_DEST dest_data = {data + layouts[i].Offset, layouts[i].Footprint.RowPitch, layouts[i].Footprint.RowPitch * (UINT64)n_rows[i]};
        // -- Row-by-row memcpy
        for (UINT z = 0; z < layouts[i].Footprint.Depth; ++z) {
            BYTE * dest_slice = reinterpret_cast<BYTE*>(dest_data.pData) + dest_data.SlicePitch * z;
            const BYTE * src_slice = reinterpret_cast<const BYTE*>(src_data[i].pData) + src_data[i].SlicePitch * z;
            for (UINT y = 0; y < n_rows[i]; ++y) {
                memcpy(dest_slice + dest_data.RowPitch * y,
                       src_slice + src_data[i].RowPitch * y,
                       row_sizes_in_bytes[i]);
            }
        }

    }
    intermediate->Unmap(0, NULL);

    if (destination_desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {

        // TODO(omid): Do we need D3D12_BOX? 
        D3D12_BOX src_box = {};
        src_box.left = UINT(layouts[0].Offset);
        src_box.top = 0;
        src_box.front = 0;
        src_box.right = UINT(layouts[0].Offset + layouts[0].Footprint.Width);
        src_box.bottom = 1;
        src_box.back = 1;

        cmd_list->CopyBufferRegion(
            dest_resource, 0, intermediate, layouts[0].Offset, layouts[0].Footprint.Width);
    } else {
        for (UINT i = 0; i < n_subresources; ++i) {
            D3D12_TEXTURE_COPY_LOCATION loc_dst = {};
            loc_dst.pResource = dest_resource;
            loc_dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            loc_dst.SubresourceIndex = i + first_subresource;

            D3D12_TEXTURE_COPY_LOCATION loc_src = {};
            loc_src.pResource = intermediate;
            loc_src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            loc_src.PlacedFootprint = layouts[i];

            cmd_list->CopyTextureRegion(&loc_dst, 0, 0, 0, &loc_src, nullptr);
        }
    }
#pragma endregion

    HeapFree(GetProcessHeap(), 0, mem_ptr);
    return required_size;
}

// Stack-allocating UpdateSubresources
 /*refer to stack-allocating UpdateSubresources implementation in d3dx12.h (towards the end)*/
template <UINT MAX_SUBRESOURCES>
inline UINT64
update_subresources_stack (
    ID3D12GraphicsCommandList * cmd_list,
    ID3D12Resource * dest_resource,
    ID3D12Resource * intermediate,
    UINT64 intermediate_offset,
    UINT first_subresource,
    UINT n_subresources,
    D3D12_SUBRESOURCE_DATA * src_data
) {
    _ASSERT_EXPR(first_subresource < MAX_SUBRESOURCES, "invalid first_subresource");
    _ASSERT_EXPR(0 < n_subresources && n_subresources <= (MAX_SUBRESOURCES - first_subresource), "invalid n_subresources");

    UINT64 required_size = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[MAX_SUBRESOURCES];
    UINT n_rows[MAX_SUBRESOURCES];
    UINT64 row_sizes_in_bytes[MAX_SUBRESOURCES];

    D3D12_RESOURCE_DESC desc = dest_resource->GetDesc();
    ID3D12Device * device;
    dest_resource->GetDevice(__uuidof(*device), reinterpret_cast<void**>(&device));
    device->GetCopyableFootprints(&desc, first_subresource, n_subresources, intermediate_offset, layouts, n_rows, row_sizes_in_bytes, &required_size);
    device->Release();

    // Minor validation
    D3D12_RESOURCE_DESC intermediate_desc = intermediate->GetDesc();
    D3D12_RESOURCE_DESC destination_desc = dest_resource->GetDesc();
    SIMPLE_ASSERT_FALSE((intermediate_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
                         intermediate_desc.Width < required_size + layouts[0].Offset ||
                         required_size > (SIZE_T) - 1 ||
                         (destination_desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
                          (first_subresource != 0 || n_subresources != 1))), "validation failed!");

    BYTE * data;
    CHECK_AND_FAIL(intermediate->Map(0, NULL, reinterpret_cast<void**>(&data)));

    for (UINT i = 0; i < n_subresources; ++i) {
        if (row_sizes_in_bytes[i] > (SIZE_T)-1) return 0;
        D3D12_MEMCPY_DEST dest_data = {data + layouts[i].Offset, layouts[i].Footprint.RowPitch, layouts[i].Footprint.RowPitch * (UINT64)n_rows[i]};
        // -- Row-by-row memcpy
        for (UINT z = 0; z < layouts[i].Footprint.Depth; ++z) {
            BYTE * dest_slice = reinterpret_cast<BYTE*>(dest_data.pData) + dest_data.SlicePitch * z;
            const BYTE * src_slice = reinterpret_cast<const BYTE*>(src_data[i].pData) + src_data[i].SlicePitch * z;
            for (UINT y = 0; y < n_rows[i]; ++y) {
                memcpy(dest_slice + dest_data.RowPitch * y,
                       src_slice + src_data[i].RowPitch * y,
                       row_sizes_in_bytes[i]);
            }
        }

    }
    intermediate->Unmap(0, NULL);

    if (destination_desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {

        // TODO(omid): Do we need D3D12_BOX? 
        D3D12_BOX src_box = {};
        src_box.left = UINT(layouts[0].Offset);
        src_box.top = 0;
        src_box.front = 0;
        src_box.right = UINT(layouts[0].Offset + layouts[0].Footprint.Width);
        src_box.bottom = 1;
        src_box.back = 1;

        cmd_list->CopyBufferRegion(
            dest_resource, 0, intermediate, layouts[0].Offset, layouts[0].Footprint.Width);
    } else {
        for (UINT i = 0; i < n_subresources; ++i) {
            D3D12_TEXTURE_COPY_LOCATION loc_dst = {};
            loc_dst.pResource = dest_resource;
            loc_dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            loc_dst.SubresourceIndex = i + first_subresource;

            D3D12_TEXTURE_COPY_LOCATION loc_src = {};
            loc_src.pResource = intermediate;
            loc_src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            loc_src.PlacedFootprint = layouts[i];

            cmd_list->CopyTextureRegion(&loc_dst, 0, 0, 0, &loc_src, nullptr);
        }
    }
    return required_size;
}
static void
create_default_buffer (
    ID3D12Device * device,
    ID3D12GraphicsCommandList * cmd_list,
    void * init_data,
    UINT64 byte_size,
    ID3D12Resource ** default_buffer,
    ID3D12Resource ** upload_buffer
) {
    D3D12_HEAP_PROPERTIES def_heap = {};
    def_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    def_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    def_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    def_heap.CreationNodeMask = 1;
    def_heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC buf_desc = {};
    buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf_desc.Alignment = 0;
    buf_desc.Width = byte_size;
    buf_desc.Height = 1;
    buf_desc.DepthOrArraySize = 1;
    buf_desc.MipLevels = 1;
    buf_desc.Format = DXGI_FORMAT_UNKNOWN;
    buf_desc.SampleDesc = {.Count = 1, .Quality = 0};
    buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buf_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES upload_heap = {};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    upload_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    upload_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    upload_heap.CreationNodeMask = 1;
    upload_heap.VisibleNodeMask = 1;

    // Create the actual default buffer resource.
    device->CreateCommittedResource(
        &def_heap,
        D3D12_HEAP_FLAG_NONE,
        &buf_desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(default_buffer));

    // In order to copy CPU memory data into our default buffer, we need to create
    // an intermediate upload heap. 
    device->CreateCommittedResource(
        &upload_heap,
        D3D12_HEAP_FLAG_NONE,
        &buf_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(upload_buffer));


    // Describe the data we want to copy into the default buffer.
    D3D12_SUBRESOURCE_DATA subresource_data = {};
    subresource_data.pData = init_data;
    subresource_data.RowPitch = byte_size;
    subresource_data.SlicePitch = subresource_data.RowPitch;

    // Schedule to copy the data to the default buffer resource.  At a high level, the helper function UpdateSubresources
    // will copy the CPU memory into the intermediate upload heap.  Then, using ID3D12CommandList::CopySubresourceRegion,
    // the intermediate upload heap data will be copied to mBuffer.
    D3D12_RESOURCE_BARRIER barrier1 = {};
    barrier1.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier1.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier1.Transition.pResource = *default_buffer;
    barrier1.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier1.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier1.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    D3D12_RESOURCE_BARRIER barrier2 = {};
    barrier2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier2.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier2.Transition.pResource = *default_buffer;
    barrier2.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier2.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    barrier2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmd_list->ResourceBarrier(1, &barrier1);
    update_subresources_stack<1>(cmd_list, *default_buffer, *upload_buffer, 0, 0, 1, &subresource_data);
    cmd_list->ResourceBarrier(1, &barrier2);

    // Note: upload_buffer has to be kept alive after the above function calls because
    // the command list has not been executed yet that performs the actual copy.
    // The caller can Release the upload_buffer after it knows the copy has been executed.

}
static D3D12_RESOURCE_BARRIER
create_barrier (ID3D12Resource * resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}
/*
    Notifies the driver that it needs to synchronize multiple accesses to resource
    i.e, specify transition between two usages through a transition barrier
*/
inline void
resource_usage_transition (
    ID3D12GraphicsCommandList * cmdlist,
    ID3D12Resource * resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after
) {
    D3D12_RESOURCE_BARRIER barrier = create_barrier(resource, before, after);
    cmdlist->ResourceBarrier(1, &barrier);
}

inline XMVECTOR
spherical_to_cartesian (float radius, float theta, float phi) {
    return XMVectorSet(
        radius * sinf(phi) * cosf(theta),
        radius * cosf(phi),
        radius * sinf(phi) * sinf(theta),
        1.0f
    );
}

static void
create_box (float width, float height, float depth, GeomVertex out_vtx [], uint16_t out_idx []) {

    // Creating Vertices

    float half_width = 0.5f * width;
    float half_height = 0.5f * height;
    float half_depth = 0.5f * depth;

    // Fill in the front face vertex data.
    out_vtx[0] = {.Position = {-half_width, -half_height, -half_depth}, .Normal = { 0.0f, 0.0f, -1.0f}, .TangentU = {1.0f, 0.0f, 0.0f}, .TexC = {0.0f, 1.0f}};
    out_vtx[1] = {.Position = {-half_width, +half_height, -half_depth}, .Normal = { 0.0f, 0.0f, -1.0f}, .TangentU = {1.0f, 0.0f, 0.0f}, .TexC = {0.0f, 0.0f}};
    out_vtx[2] = {.Position = {+half_width, +half_height, -half_depth}, .Normal = { 0.0f, 0.0f, -1.0f}, .TangentU = {1.0f, 0.0f, 0.0f}, .TexC = {1.0f, 0.0f}};
    out_vtx[3] = {.Position = {+half_width, -half_height, -half_depth}, .Normal = { 0.0f, 0.0f, -1.0f}, .TangentU = {1.0f, 0.0f, 0.0f}, .TexC = {1.0f, 1.0f}};

    // Fill in the back face vertex data.
    out_vtx[4] = {.Position = {-half_width, -half_height, +half_depth}, .Normal = { 0.0f, 0.0f, 1.0f}, .TangentU = {-1.0f, 0.0f, 0.0f}, .TexC = {1.0f, 1.0f}};
    out_vtx[5] = {.Position = {+half_width, -half_height, +half_depth}, .Normal = { 0.0f, 0.0f, 1.0f}, .TangentU = {-1.0f, 0.0f, 0.0f}, .TexC = {0.0f, 1.0f}};
    out_vtx[6] = {.Position = {+half_width, +half_height, +half_depth}, .Normal = { 0.0f, 0.0f, 1.0f}, .TangentU = {-1.0f, 0.0f, 0.0f}, .TexC = {0.0f, 0.0f}};
    out_vtx[7] = {.Position = {-half_width, +half_height, +half_depth}, .Normal = { 0.0f, 0.0f, 1.0f}, .TangentU = {-1.0f, 0.0f, 0.0f}, .TexC = {1.0f, 0.0f}};

    // Fill in the top face vertex data.
    out_vtx[8] = {.Position = {-half_width, +half_height, -half_depth}, .Normal = { 0.0f, 1.0f, 0.0f}, .TangentU = {1.0f, 0.0f, 0.0f}, .TexC = {0.0f, 1.0f}};
    out_vtx[9] = {.Position = {-half_width, +half_height, +half_depth}, .Normal = { 0.0f, 1.0f, 0.0f}, .TangentU = {1.0f, 0.0f, 0.0f}, .TexC = {0.0f, 0.0f}};
    out_vtx[10] = {.Position = {+half_width, +half_height, +half_depth}, .Normal = { 0.0f, 1.0f, 0.0f}, .TangentU = {1.0f, 0.0f, 0.0f}, .TexC = {1.0f, 0.0f}};
    out_vtx[11] = {.Position = {+half_width, +half_height, -half_depth}, .Normal = { 0.0f, 1.0f, 0.0f}, .TangentU = {1.0f, 0.0f, 0.0f}, .TexC = {1.0f, 1.0f}};

    // Fill in the bottom face vertex data.
    out_vtx[12] = {.Position = {-half_width, -half_height, -half_depth}, .Normal = { 0.0f, -1.0f, 0.0f}, .TangentU = {-1.0f, 0.0f, 0.0f}, .TexC = {1.0f, 1.0f}};
    out_vtx[13] = {.Position = {+half_width, -half_height, -half_depth}, .Normal = { 0.0f, -1.0f, 0.0f}, .TangentU = {-1.0f, 0.0f, 0.0f}, .TexC = {0.0f, 1.0f}};
    out_vtx[14] = {.Position = {+half_width, -half_height, +half_depth}, .Normal = { 0.0f, -1.0f, 0.0f}, .TangentU = {-1.0f, 0.0f, 0.0f}, .TexC = {0.0f, 0.0f}};
    out_vtx[15] = {.Position = {-half_width, -half_height, +half_depth}, .Normal = { 0.0f, -1.0f, 0.0f}, .TangentU = {-1.0f, 0.0f, 0.0f}, .TexC = {1.0f, 0.0f}};

    // Fill in the left face vertex data.
    out_vtx[16] = {.Position = {-half_width, -half_height, +half_depth}, .Normal = { -1.0f, 0.0f, 0.0f}, .TangentU = {0.0f, 0.0f, -1.0f}, .TexC = {0.0f, 1.0f}};
    out_vtx[17] = {.Position = {-half_width, +half_height, +half_depth}, .Normal = { -1.0f, 0.0f, 0.0f}, .TangentU = {0.0f, 0.0f, -1.0f}, .TexC = {0.0f, 0.0f}};
    out_vtx[18] = {.Position = {-half_width, +half_height, -half_depth}, .Normal = { -1.0f, 0.0f, 0.0f}, .TangentU = {0.0f, 0.0f, -1.0f}, .TexC = {1.0f, 0.0f}};
    out_vtx[19] = {.Position = {-half_width, -half_height, -half_depth}, .Normal = { -1.0f, 0.0f, 0.0f}, .TangentU = {0.0f, 0.0f, -1.0f}, .TexC = {1.0f, 1.0f}};

    // Fill in the right face vertex data.
    out_vtx[20] = {.Position = {+half_width, -half_height, -half_depth}, .Normal = { 1.0f, 0.0f, 0.0f}, .TangentU = {0.0f, 0.0f, 1.0f}, .TexC = {0.0f, 1.0f}};
    out_vtx[21] = {.Position = {+half_width, +half_height, -half_depth}, .Normal = { 1.0f, 0.0f, 0.0f}, .TangentU = {0.0f, 0.0f, 1.0f}, .TexC = {0.0f, 0.0f}};
    out_vtx[22] = {.Position = {+half_width, +half_height, +half_depth}, .Normal = { 1.0f, 0.0f, 0.0f}, .TangentU = {0.0f, 0.0f, 1.0f}, .TexC = {1.0f, 0.0f}};
    out_vtx[23] = {.Position = {+half_width, -half_height, +half_depth}, .Normal = { 1.0f, 0.0f, 0.0f}, .TangentU = {0.0f, 0.0f, 1.0f}, .TexC = {1.0f, 1.0f}};

    // -- Creating Indices 

    // Fill in the front face index data
    out_idx[0] = 0; out_idx[1] = 1; out_idx[2] = 2;
    out_idx[3] = 0; out_idx[4] = 2; out_idx[5] = 3;

    // Fill in the back face index data
    out_idx[6] = 4; out_idx[7] = 5; out_idx[8] = 6;
    out_idx[9] = 4; out_idx[10] = 6; out_idx[11] = 7;

    // Fill in the top face index data
    out_idx[12] = 8; out_idx[13] = 9; out_idx[14] = 10;
    out_idx[15] = 8; out_idx[16] = 10; out_idx[17] = 11;

    // Fill in the bottom face index data
    out_idx[18] = 12; out_idx[19] = 13; out_idx[20] = 14;
    out_idx[21] = 12; out_idx[22] = 14; out_idx[23] = 15;

    // Fill in the left face index data
    out_idx[24] = 16; out_idx[25] = 17; out_idx[26] = 18;
    out_idx[27] = 16; out_idx[28] = 18; out_idx[29] = 19;

    // Fill in the right face index data
    out_idx[30] = 20; out_idx[31] = 21; out_idx[32] = 22;
    out_idx[33] = 20; out_idx[34] = 22; out_idx[35] = 23;
}
static void
create_sphere (float radius, GeomVertex out_vtx [], uint16_t out_idx []) {

    // TODO(omid): add some validation for array sizes
    /* out_vtx [401], out_idx [2280] */

    // -- Compute the vertices stating at the top pole and moving down the stacks.
    UINT32 n_stack = 20;
    UINT32 n_slice = 20;
    UINT32 n_vtx = n_stack * n_slice;
    float phi_step = XM_PI / n_stack;
    float theta_step = 2.0f * XM_PI / n_slice;

    // Poles: note that there will be texture coordinate distortion as there is
    // not a unique point on the texture map to assign to the pole when mapping
    // a rectangular texture onto a sphere.
    GeomVertex top = {.Position = {0.0f, +radius, 0.0f}, .Normal = {0.0f, +1.0f, 0.0f}, .TangentU = {1.0f, 0.0f, 0.0f}, .TexC = {0.0f, 0.0f}};
    GeomVertex bottom = {.Position = {0.0f, -radius, 0.0f}, .Normal = {0.0f, -1.0f, 0.0f}, .TangentU = {1.0f, 0.0f, 0.0f}, .TexC = {0.0f, 1.0f}};

    out_vtx[0] = top;
    out_vtx[n_vtx + 1] = bottom;

    // -- Compute vertices for each stack ring (do not count the poles as rings).
    UINT32 _curr_idx = 1;
    for (UINT32 i = 1; i <= n_stack - 1; ++i) {
        float phi = i * phi_step;

        // Vertices of ring.
        for (UINT32 j = 0; j <= n_slice; ++j) {
            float theta = j * theta_step;

            GeomVertex v = {};

            // spherical to cartesian
            v.Position.x = radius * sinf(phi) * cosf(theta);
            v.Position.y = radius * cosf(phi);
            v.Position.z = radius * sinf(phi) * sinf(theta);

            // Partial derivative of P with respect to theta
            v.TangentU.x = -radius * sinf(phi) * sinf(theta);
            v.TangentU.y = 0.0f;
            v.TangentU.z = +radius * sinf(phi) * cosf(theta);

            XMVECTOR T = XMLoadFloat3(&v.TangentU);
            XMStoreFloat3(&v.TangentU, XMVector3Normalize(T));

            XMVECTOR p = XMLoadFloat3(&v.Position);
            XMStoreFloat3(&v.Normal, XMVector3Normalize(p));

            v.TexC.x = theta / XM_2PI;
            v.TexC.y = phi / XM_PI;

            out_vtx[_curr_idx++] = v;
        }
    }

    // -- Compute indices for top stack.  The top stack was written first to the vertex buffer and connects the top pole to the first ring.

    UINT32 _idx_cnt = 0;
    for (UINT16 i = 1; i <= n_slice; ++i) {
        out_idx[_idx_cnt++] = 0;
        out_idx[_idx_cnt++] = i + 1;
        out_idx[_idx_cnt++] = i;
    }

    // -- Compute indices for inner stacks (not connected to poles).

    // -- Offset the indices to the index of the first vertex in the first ring.
    // TODO(omid): fix this shenanigan 
    // This is just skipping the top pole vertex.
    UINT16 base_index = 1;
    UINT16 ring_vtx_cnt = (UINT16)n_slice + 1;
    for (UINT16 i = 0; i < n_stack - 2; ++i) {
        for (UINT16 j = 0; j < n_slice; ++j) {
            out_idx[_idx_cnt++] = base_index + i * ring_vtx_cnt + j;
            out_idx[_idx_cnt++] = base_index + i * ring_vtx_cnt + j + 1;
            out_idx[_idx_cnt++] = base_index + (i + 1) * ring_vtx_cnt + j;

            out_idx[_idx_cnt++] = base_index + (i + 1) * ring_vtx_cnt + j;
            out_idx[_idx_cnt++] = base_index + i * ring_vtx_cnt + j + 1;
            out_idx[_idx_cnt++] = base_index + (i + 1) * ring_vtx_cnt + j + 1;
        }
    }

    // -- Compute indices for bottom stack.  The bottom stack was written last to the vertex buffer and connects the bottom pole to the bottom ring.

    // South pole vertex was added last.
    UINT16 south_pole_index = (UINT16)n_vtx - 1;

    // offset the indices to the index of the first vertex in the last ring.
    base_index = south_pole_index - ring_vtx_cnt;

    for (UINT16 i = 0; i < n_slice; ++i) {
        out_idx[_idx_cnt++] = south_pole_index;
        out_idx[_idx_cnt++] = base_index + i;
        out_idx[_idx_cnt++] = base_index + i + 1;
    }
}
static void
create_cylinder (float bottom_radius, float top_radius, float height, GeomVertex out_vtx [], uint16_t out_idx []) {

    // TODO(omid): add some validation for array sizes
    /* out_vtx [485], out_idx [2520] */

    // -- Build Stacks.
    UINT32 n_stack = 20;
    UINT32 n_slice = 20;
    float stack_height = height / n_stack;

    // Amount to increment radius as we move up each stack level from bottom to top.
    float radius_step = (top_radius - bottom_radius) / n_stack;
    UINT32 ring_cnt = n_stack + 1;

    UINT32 _vtx_cnt = 0;
    UINT32 _idx_cnt = 0;

    // Compute vertices for each stack ring starting at the bottom and moving up.
    for (UINT32 i = 0; i < ring_cnt; ++i) {
        float y = -0.5f * height + i * stack_height;
        float r = bottom_radius + i * radius_step;

        // vertices of ring
        float dtheta = 2.0f * XM_PI / n_slice;
        for (UINT32 j = 0; j <= n_slice; ++j) {
            GeomVertex vertex = {};

            float c = cosf(j * dtheta);
            float s = sinf(j * dtheta);

            vertex.Position = XMFLOAT3(r * c, y, r * s);

            vertex.TexC.x = (float)j / n_slice;
            vertex.TexC.y = 1.0f - (float)i / n_stack;

            // This is unit length.
            vertex.TangentU = XMFLOAT3(-s, 0.0f, c);

            float dr = bottom_radius - top_radius;
            XMFLOAT3 bitangent(dr * c, -height, dr * s);

            XMVECTOR T = XMLoadFloat3(&vertex.TangentU);
            XMVECTOR B = XMLoadFloat3(&bitangent);
            XMVECTOR N = XMVector3Normalize(XMVector3Cross(T, B));
            XMStoreFloat3(&vertex.Normal, N);

            out_vtx[_vtx_cnt++] = vertex;
        }
    }

    // Add one because we duplicate the first and last vertex per ring
    // since the texture coordinates are different.
    UINT16 ring_vertex_count = (UINT16)n_slice + 1;

    // Compute indices for each stack.
    for (UINT16 i = 0; i < n_stack; ++i) {
        for (UINT16 j = 0; j < n_slice; ++j) {
            out_idx[_idx_cnt++] = i * ring_vertex_count + j;
            out_idx[_idx_cnt++] = (i + 1) * ring_vertex_count + j;
            out_idx[_idx_cnt++] = (i + 1) * ring_vertex_count + j + 1;

            out_idx[_idx_cnt++] = i * ring_vertex_count + j;
            out_idx[_idx_cnt++] = (i + 1) * ring_vertex_count + j + 1;
            out_idx[_idx_cnt++] = i * ring_vertex_count + j + 1;
        }
    }

#pragma region build cylinder top
    UINT16 base_index_top = (UINT16)_vtx_cnt;
    _ASSERT_EXPR(441 == base_index_top, "wrong vtx count");
    float y1 = 0.5f * height;
    float dtheta = 2.0f * XM_PI / n_slice;

    // Duplicate cap ring vertices because the texture coordinates and normals differ.
    for (UINT32 i = 0; i <= n_slice; ++i) {
        float x = top_radius * cosf(i * dtheta);
        float z = top_radius * sinf(i * dtheta);

        // Scale down by the height to try and make top cap texture coord area
        // proportional to base.
        float u = x / height + 0.5f;
        float v = z / height + 0.5f;

        out_vtx[_vtx_cnt++] = {.Position = {x, y1, z}, .Normal = {0.0f, 1.0f, 0.0f}, .TangentU = {1.0f, 0.0f, 0.0f}, .TexC = {u, v}};
    }

    // Cap center vertex.
    out_vtx[_vtx_cnt++] = {.Position = {0.0f, y1, 0.0f}, .Normal = {0.0f, 1.0f, 0.0f}, .TangentU = {1.0f, 0.0f, 0.0f}, .TexC = {0.5f, 0.5f}};

    // Index of center vertex.
    UINT16 center_index_top = (UINT16)_vtx_cnt - 1;
    _ASSERT_EXPR(462 == center_index_top, "wrong vtx count");

    for (UINT16 i = 0; i < n_slice; ++i) {
        out_idx[_idx_cnt++] = center_index_top;
        out_idx[_idx_cnt++] = base_index_top + i + 1;
        out_idx[_idx_cnt++] = base_index_top + i;
    }
#pragma endregion build cylinder top

#pragma region build cylinder bottom
    UINT16 base_index_bottom = (UINT16)_vtx_cnt;
    _ASSERT_EXPR(463 == base_index_bottom, "wrong vtx count");
    float y2 = -0.5f * height;

    // vertices of ring
    //float dTheta = 2.0f * XM_PI / n_slice; // not used
    for (UINT32 i = 0; i <= n_slice; ++i) {
        float x = bottom_radius * cosf(i * dtheta);
        float z = bottom_radius * sinf(i * dtheta);

        // Scale down by the height to try and make top cap texture coord area
        // proportional to base.
        float u = x / height + 0.5f;
        float v = z / height + 0.5f;
        out_vtx[_vtx_cnt++] = {.Position = {x, y2, z}, .Normal = {0.0f, -1.0f, 0.0f}, .TangentU = {1.0f, 0.0f, 0.0f}, .TexC = {u, v}};
    }

    // Cap center vertex.
    out_vtx[_vtx_cnt++] = {.Position = {0.0f, y2, 0.0f}, .Normal = {0.0f, -1.0f, 0.0f}, .TangentU = {1.0f, 0.0f, 0.0f}, .TexC = {0.5f, 0.5f}};

    // Cache the index of center vertex.
    UINT16 center_index_bottom = (UINT16)_vtx_cnt - 1;
    _ASSERT_EXPR(484 == center_index_bottom, "wrong vtx count");

    for (UINT16 i = 0; i < n_slice; ++i) {
        out_idx[_idx_cnt++] = center_index_bottom;
        out_idx[_idx_cnt++] = base_index_bottom + i;
        out_idx[_idx_cnt++] = base_index_bottom + i + 1;
    }
#pragma endregion build cylinder bottom

}
static void
create_grid16 (float width, float depth, UINT32 m, UINT32 n, GeomVertex out_vtx [], uint16_t out_idx []) {

    // -- Create the vertices.

    float half_width = 0.5f * width;
    float half_depth = 0.5f * depth;

    float dx = width / (n - 1);
    float dz = depth / (m - 1);

    float du = 1.0f / (n - 1);
    float dv = 1.0f / (m - 1);

    for (UINT32 i = 0; i < m; ++i) {
        float z = half_depth - i * dz;
        for (UINT32 j = 0; j < n; ++j) {
            float x = -half_width + j * dx;

            out_vtx[i * n + j].Position = XMFLOAT3(x, 0.0f, z);
            out_vtx[i * n + j].Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
            out_vtx[i * n + j].TangentU = XMFLOAT3(1.0f, 0.0f, 0.0f);

            // Stretch texture over grid.
            out_vtx[i * n + j].TexC.x = j * du;
            out_vtx[i * n + j].TexC.y = i * dv;
        }
    }

    // -- Create the indices.

    // Iterate over each quad and compute indices.
    UINT32 k = 0;
    UINT16 nn = (UINT16)n; // cast to avoid compiler warnings
    for (UINT16 i = 0; i < m - 1; ++i) {
        for (UINT16 j = 0; j < n - 1; ++j) {
            out_idx[k] = i * nn + j;
            out_idx[k + 1] = i * nn + j + 1;
            out_idx[k + 2] = (i + 1) * nn + j;

            out_idx[k + 3] = (i + 1) * nn + j;
            out_idx[k + 4] = i * nn + j + 1;
            out_idx[k + 5] = (i + 1) * nn + j + 1;

            k += 6; // next quad
        }
    }
}
static void
create_grid32 (float width, float depth, UINT32 m, UINT32 n, GeomVertex out_vtx [], uint32_t out_idx []) {

    // -- Create the vertices.

    float half_width = 0.5f * width;
    float half_depth = 0.5f * depth;

    float dx = width / (n - 1);
    float dz = depth / (m - 1);

    float du = 1.0f / (n - 1);
    float dv = 1.0f / (m - 1);

    for (UINT32 i = 0; i < m; ++i) {
        float z = half_depth - i * dz;
        for (UINT32 j = 0; j < n; ++j) {
            float x = -half_width + j * dx;

            out_vtx[i * n + j].Position = XMFLOAT3(x, 0.0f, z);
            out_vtx[i * n + j].Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
            out_vtx[i * n + j].TangentU = XMFLOAT3(1.0f, 0.0f, 0.0f);

            // Stretch texture over grid.
            out_vtx[i * n + j].TexC.x = j * du;
            out_vtx[i * n + j].TexC.y = i * dv;
        }
    }

    // -- Create the indices.

    // Iterate over each quad and compute indices.
    UINT32 k = 0;
    UINT32 nn = n;
    for (UINT i = 0; i < m - 1; ++i) {
        for (UINT j = 0; j < n - 1; ++j) {
            out_idx[k] = i * nn + j;
            out_idx[k + 1] = i * nn + j + 1;
            out_idx[k + 2] = (i + 1) * nn + j;

            out_idx[k + 3] = (i + 1) * nn + j;
            out_idx[k + 4] = i * nn + j + 1;
            out_idx[k + 5] = (i + 1) * nn + j + 1;

            k += 6; // next quad
        }
    }
}
