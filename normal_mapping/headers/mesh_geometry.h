/* ===========================================================
   #File: mesh_geometry.h #
   #Date: 31 Jan 2021 #
   #Revision: 1.0 #
   #Creator: Omid Miresmaeili #
   #Description: Mesh Geometry Stuff #
   #Notice: (C) Copyright 2021 by Omid. All Rights Reserved. #
   =========================================================== */
#pragma once

#include "headers/common.h"

#define MAX_SUBMESH_COUNT    50

// Defines a subrange of geometry in a MeshGeometry.  This is for when multiple
// geometries are stored in one vertex and index buffer.  It provides the offsets
// and data needed to draw a subset of geometry stores in the vertex and index 
// buffers 
struct SubmeshGeometry {
    UINT index_count;
    UINT start_index_location;
    INT base_vertex_location;

    // Bounding box of the geometry defined by this submesh. 
    // Not used for now
    DirectX::BoundingBox bounds;
};
struct MeshGeometry {
    // Give it a name so we can look it up by name.
    //char const * name;

    UINT vb_byte_stide;
    UINT vb_byte_size;
    UINT ib_byte_size;

    // System memory copies.  Use Blobs because the vertex/index format can be generic.
    // It is up to the client to cast appropriately.  
    ID3DBlob * vb_cpu;
    ID3DBlob * ib_cpu;

    ID3D12Resource * vb_gpu;
    ID3D12Resource * ib_gpu;

    ID3D12Resource * vb_uploader;
    ID3D12Resource * ib_uploader;

    DXGI_FORMAT index_format;

    // A MeshGeometry may store multiple geometries in one vertex/index buffer.
    // Use this container to define the Submesh geometries so we can draw
    // the Submeshes individually.
    char const *    submesh_names [MAX_SUBMESH_COUNT];
    SubmeshGeometry submesh_geoms [MAX_SUBMESH_COUNT];
};

inline D3D12_VERTEX_BUFFER_VIEW
Mesh_GetVertexBufferView (MeshGeometry * mesh) {
    D3D12_VERTEX_BUFFER_VIEW vbv;
    vbv.BufferLocation = mesh->vb_gpu->GetGPUVirtualAddress();
    vbv.StrideInBytes = mesh->vb_byte_stide;
    vbv.SizeInBytes = mesh->vb_byte_size;

    return vbv;
}

inline D3D12_INDEX_BUFFER_VIEW
Mesh_GetIndexBufferView (MeshGeometry * mesh) {
    D3D12_INDEX_BUFFER_VIEW ibv;
    ibv.BufferLocation = mesh->ib_gpu->GetGPUVirtualAddress();
    ibv.Format = mesh->index_format;
    ibv.SizeInBytes = mesh->ib_byte_size;

    return ibv;
}

// We can free this memory after we finish upload to the GPU.
inline void
Mesh_Dispose (MeshGeometry * mesh) {
    mesh->vb_cpu->Release();
    mesh->ib_cpu->Release();

    mesh->vb_gpu->Release();
    mesh->ib_gpu->Release();

    mesh->vb_uploader->Release();
    mesh->ib_uploader->Release();
}
