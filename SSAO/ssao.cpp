#include "ssao.h"
#include <DirectXPackedVector.h>

///<summary>
/// Blurs the ambient map to smooth out the noise caused by limited number of samples [14].
/// The blur code is edge-preserving (do not blur across discontinuities)
///</summary>
static void
blur_ambient_map (SSAO * ssao, ID3D12GraphicsCommandList * cmdlist, bool horz_blur) {
    ID3D12Resource * output = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE input_srv;
    D3D12_CPU_DESCRIPTOR_HANDLE output_rtv;

    // ping ponging two ambient maps as we apply blur passes
    if (horz_blur) {
        output = ssao->ambient_map1;
        input_srv = ssao->ambient_map0_gpu_srv;
        output_rtv = ssao->ambient_map1_cpu_rtv;
        cmdlist->SetGraphicsRoot32BitConstant(1, 1, 0);
    } else {
        output = ssao->ambient_map0;
        input_srv = ssao->ambient_map1_gpu_srv;
        output_rtv = ssao->ambient_map0_cpu_rtv;
        cmdlist->SetGraphicsRoot32BitConstant(1, 0, 0);
    }

    resource_usage_transition(
        cmdlist, output,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );

    float clear_value [] = {1.0f, 1.0f, 1.0f, 1.0f};
    cmdlist->ClearRenderTargetView(output_rtv, clear_value, 0, nullptr);

    cmdlist->OMSetRenderTargets(1, &output_rtv, true, nullptr);

    // normal / depth map still bound

    // bind normal and depth maps
    cmdlist->SetGraphicsRootDescriptorTable(2, ssao->normal_map_gpu_srv);

    // bind input ambient map to second descriptor table
    cmdlist->SetGraphicsRootDescriptorTable(3, input_srv);

    // draw fullscreen quad
    cmdlist->IASetVertexBuffers(0, 0, nullptr);
    cmdlist->IASetIndexBuffer(nullptr);
    cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdlist->DrawInstanced(6, 1, 0, 0);

    resource_usage_transition(
        cmdlist, output,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_GENERIC_READ
    );
}
static void
blur_ambient_map (SSAO * ssao, ID3D12GraphicsCommandList * cmdlist, FrameResource * curr_frame, int blur_count) {
    cmdlist->SetPipelineState(ssao->blur_pso);

    D3D12_GPU_VIRTUAL_ADDRESS ssao_cb_address = curr_frame->ssao_cb->GetGPUVirtualAddress();
    cmdlist->SetGraphicsRootConstantBufferView(0, ssao_cb_address);

    for (int i = 0; i < blur_count; ++i) {
        blur_ambient_map(ssao, cmdlist, true);
        blur_ambient_map(ssao, cmdlist, false);
    }
}

static void
create_random_vector_texture (SSAO * ssao, ID3D12GraphicsCommandList * cmdlist) {
    D3D12_RESOURCE_DESC tex_desc = {};
    tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Alignment = 0;
    tex_desc.Width = 256;
    tex_desc.Height = 256;
    tex_desc.DepthOrArraySize = 1;
    tex_desc.MipLevels = 1;
    tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.SampleDesc.Quality = 0;
    tex_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    tex_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heap_def = {};
    heap_def.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_def.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_def.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_def.CreationNodeMask = 1;
    heap_def.VisibleNodeMask = 1;

    ssao->device->CreateCommittedResource(
        &heap_def, D3D12_HEAP_FLAG_NONE,
        &tex_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&ssao->random_vector_map)
    );

    // to copy cpu mem data into def buffer, create an upload buffer

    UINT n_subresources = tex_desc.DepthOrArraySize * tex_desc.MipLevels;
    UINT64 upload_buffer_size = get_required_intermediate_size(
        ssao->random_vector_map,
        0, n_subresources
    );

    D3D12_HEAP_PROPERTIES heap_upload = {};
    heap_upload.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap_upload.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_upload.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_upload.CreationNodeMask = 1;
    heap_upload.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC uploader_desc = {};
    uploader_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploader_desc.Alignment = 0;
    uploader_desc.Width = upload_buffer_size;
    uploader_desc.Height = 1;
    uploader_desc.DepthOrArraySize = 1;
    uploader_desc.MipLevels = 1;
    uploader_desc.Format = DXGI_FORMAT_UNKNOWN;
    uploader_desc.SampleDesc.Count = 1;
    uploader_desc.SampleDesc.Quality = 0;
    uploader_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploader_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ssao->device->CreateCommittedResource(
        &heap_upload,
        D3D12_HEAP_FLAG_NONE,
        &uploader_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&ssao->random_vector_map_uploader)
    );

    DirectX::PackedVector::XMCOLOR * init_data = (DirectX::PackedVector::XMCOLOR*)::calloc(256 * 256, sizeof(DirectX::PackedVector::XMCOLOR));
    for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 256; ++j) {
            // random vecs in [0,1].  We will uncompress them in shader code to [-1,1].
            XMFLOAT3 v(rand_float(), rand_float(), rand_float());
            init_data[i * 256 + j] = DirectX::PackedVector::XMCOLOR(v.x, v.y, v.z, 0.0f);
        }
    }

    D3D12_SUBRESOURCE_DATA subresource_data = {};
    subresource_data.pData = init_data;
    subresource_data.RowPitch = 256 * sizeof(DirectX::PackedVector::XMCOLOR);
    subresource_data.SlicePitch = subresource_data.RowPitch * 256;

    //
    // schedule a copy to "default resource"
    // remember to handle resource states changes
    resource_usage_transition(
        cmdlist, ssao->random_vector_map,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_COPY_DEST
    );

    update_subresources_heap(
        cmdlist, ssao->random_vector_map, ssao->random_vector_map_uploader,
        0, 0, n_subresources, &subresource_data
    );

    resource_usage_transition(
        cmdlist, ssao->random_vector_map,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_GENERIC_READ
    );

    ::free(init_data);
}
static void
create_offset_vectors (SSAO * ssao) {
    // 14 random vectors = 8 corners + 6 mid points

    // 8 cube corners
    ssao->offsets[0] = XMFLOAT4(+1.0f, +1.0f, +1.0f, 0.0f);
    ssao->offsets[1] = XMFLOAT4(-1.0f, -1.0f, -1.0f, 0.0f);

    ssao->offsets[2] = XMFLOAT4(-1.0f, +1.0f, +1.0f, 0.0f);
    ssao->offsets[3] = XMFLOAT4(+1.0f, -1.0f, -1.0f, 0.0f);

    ssao->offsets[4] = XMFLOAT4(+1.0f, +1.0f, -1.0f, 0.0f);
    ssao->offsets[5] = XMFLOAT4(-1.0f, -1.0f, +1.0f, 0.0f);

    ssao->offsets[6] = XMFLOAT4(-1.0f, +1.0f, -1.0f, 0.0f);
    ssao->offsets[7] = XMFLOAT4(+1.0f, -1.0f, +1.0f, 0.0f);

    // 6 centers of cube faces
    ssao->offsets[8] = XMFLOAT4(-1.0f, 0.0f, 0.0f, 0.0f);
    ssao->offsets[9] = XMFLOAT4(+1.0f, 0.0f, 0.0f, 0.0f);

    ssao->offsets[10] = XMFLOAT4(0.0f, -1.0f, 0.0f, 0.0f);
    ssao->offsets[11] = XMFLOAT4(0.0f, +1.0f, 0.0f, 0.0f);

    ssao->offsets[12] = XMFLOAT4(0.0f, 0.0f, -1.0f, 0.0f);
    ssao->offsets[13] = XMFLOAT4(0.0f, 0.0f, +1.0f, 0.0f);

    for (int i = 0; i < 14; ++i) {
        // random lengths in [0.25, 1.0] range.
        float s = rand_float(0.25f, 1.0f);
        XMVECTOR v = s * XMVector4Normalize(XMLoadFloat4(&ssao->offsets[i]));
        XMStoreFloat4(&ssao->offsets[i], v);
    }
}
static void
create_resources_internal (SSAO * ssao) {

    // free old resources if they exit
    if (ssao->ambient_map0) ssao->ambient_map0->Release();
    if (ssao->ambient_map1) ssao->ambient_map1->Release();
    if (ssao->normal_map) ssao->normal_map->Release();

    // we render to normal map at full resolution
    D3D12_RESOURCE_DESC tex_desc = {};
    tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Alignment = 0;
    tex_desc.Width = ssao->render_target_width;
    tex_desc.Height = ssao->render_target_height;
    tex_desc.DepthOrArraySize = 1;
    tex_desc.MipLevels = 1;
    tex_desc.Format = ssao->normal_map_format;
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

    float nmap_color [] = {0.0f, 0.0f, 1.0f, 0.0f};
    D3D12_CLEAR_VALUE opt_clear = {};
    opt_clear.Format = ssao->normal_map_format;
    memcpy(opt_clear.Color, nmap_color, sizeof(opt_clear.Color));

    ssao->device->CreateCommittedResource(
        &heap_def, D3D12_HEAP_FLAG_NONE,
        &tex_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        &opt_clear, IID_PPV_ARGS(&ssao->normal_map)
    );

    // we render to ambient map at half resolution
    tex_desc.Width = ssao->render_target_width / 2;
    tex_desc.Height = ssao->render_target_height / 2;
    tex_desc.Format = ssao->ambient_map_format;

    float ssaomap_color [] = {1.0f, 1.0f, 1.0f, 1.0f};
    opt_clear.Format = ssao->ambient_map_format;
    memcpy(opt_clear.Color, ssaomap_color, sizeof(opt_clear.Color));

    ssao->device->CreateCommittedResource(
        &heap_def, D3D12_HEAP_FLAG_NONE,
        &tex_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        &opt_clear, IID_PPV_ARGS(&ssao->ambient_map0)
    );
    ssao->device->CreateCommittedResource(
        &heap_def, D3D12_HEAP_FLAG_NONE,
        &tex_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        &opt_clear, IID_PPV_ARGS(&ssao->ambient_map1)
    );
}
void
SSAO_Init (SSAO * ssao, ID3D12Device * dev, ID3D12GraphicsCommandList * cmdlist, UINT w, UINT h) {
    _ASSERT_EXPR(ssao, _T("Invalid SSAO ptr"));
    memset(ssao, 0, sizeof(SSAO));

    ssao->device = dev;
    ssao->ambient_map_format = DXGI_FORMAT_R16_UNORM;
    ssao->normal_map_format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    ssao->max_blur_radius = 5;

    SSAO_Resize(ssao, w, h);
    create_offset_vectors(ssao);
    create_random_vector_texture(ssao, cmdlist);
    ssao->initialized = true;
}
void
SSAO_Deinit (SSAO * ssao) {
    ssao->ambient_map0->Release();
    ssao->ambient_map1->Release();
    ssao->normal_map->Release();
}

int
SSAO_CalculateWeightsCount (SSAO * ssao, float sigma) {
    float two_sigma2 = 2.0f * sigma * sigma;

    // sigma controls the width of the bell curve of the gauss function
    int radius = (int)ceilf(2.0f * sigma);
    _ASSERT_EXPR(radius <= ssao->max_blur_radius, _T("Invalid blur radius"));

    int weights_count = 2 * radius + 1;
    return weights_count;
}
void
SSAO_CalculateGaussWeights (SSAO * ssao, float sigma, float out_weights []) {
    float two_sigma2 = 2.0f * sigma * sigma;

    // sigma controls the width of the bell curve of the gauss function
    int radius = (int)ceilf(2.0f * sigma);
    _ASSERT_EXPR(radius <= ssao->max_blur_radius, _T("Invalid blur radius"));
    int weights_count = 2 * radius + 1;

    float weight_sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        float x = (float)i;
        out_weights[i + radius] = expf(-(x * x) / two_sigma2);
        weight_sum += out_weights[i + radius];
    }
    // normalize (divide by sum)
    for (int i = 0; i < weights_count; ++i)
        out_weights[i] /= weight_sum;
}

void
SSAO_CreateDescriptors (
    SSAO * ssao,
    ID3D12Resource * depth_stencil_buffer,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_srv,
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_srv,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_rtv,
    UINT cbv_srv_uav_descriptor_size,
    UINT rtv_descriptor_size
) {
    if (ssao->initialized) {
        // srvs cpu handles
        ssao->ambient_map0_cpu_srv = hcpu_srv;

        hcpu_srv.ptr += cbv_srv_uav_descriptor_size;
        ssao->ambient_map1_cpu_srv = hcpu_srv;

        hcpu_srv.ptr += cbv_srv_uav_descriptor_size;
        ssao->normal_map_cpu_srv = hcpu_srv;

        hcpu_srv.ptr += cbv_srv_uav_descriptor_size;
        ssao->depth_map_cpu_srv = hcpu_srv;

        hcpu_srv.ptr += cbv_srv_uav_descriptor_size;
        ssao->random_vector_map_cpu_srv = hcpu_srv;

        // srvs gpu handles
        ssao->ambient_map0_gpu_srv = hgpu_srv;

        hgpu_srv.ptr += cbv_srv_uav_descriptor_size;
        ssao->ambient_map1_gpu_srv = hgpu_srv;

        hgpu_srv.ptr += cbv_srv_uav_descriptor_size;
        ssao->normal_map_gpu_srv = hgpu_srv;

        hgpu_srv.ptr += cbv_srv_uav_descriptor_size;
        ssao->depth_map_gpu_srv = hgpu_srv;

        hgpu_srv.ptr += cbv_srv_uav_descriptor_size;
        ssao->random_vector_map_gpu_srv = hgpu_srv;

        // rtv handles
        ssao->normal_map_cpu_rtv = hcpu_rtv;

        hcpu_rtv.ptr += rtv_descriptor_size;
        ssao->ambient_map0_cpu_rtv = hcpu_rtv;

        hcpu_rtv.ptr += rtv_descriptor_size;
        ssao->ambient_map1_cpu_rtv = hcpu_rtv;

        // create descruotirs
        SSAO_RecreateDescriptors(ssao, depth_stencil_buffer);
    }
}
void
SSAO_RecreateDescriptors (
    SSAO * ssao,
    ID3D12Resource * depth_stencil_buffer
) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = ssao->normal_map_format;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;

    ssao->device->CreateShaderResourceView(ssao->normal_map, &srv_desc, ssao->normal_map_cpu_srv);

    srv_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    ssao->device->CreateShaderResourceView(depth_stencil_buffer, &srv_desc, ssao->depth_map_cpu_srv);

    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ssao->device->CreateShaderResourceView(ssao->random_vector_map, &srv_desc, ssao->random_vector_map_cpu_srv);

    srv_desc.Format = ssao->ambient_map_format;
    ssao->device->CreateShaderResourceView(ssao->ambient_map0, &srv_desc, ssao->ambient_map0_cpu_srv);
    ssao->device->CreateShaderResourceView(ssao->ambient_map1, &srv_desc, ssao->ambient_map1_cpu_srv);

    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Format = ssao->normal_map_format;
    rtv_desc.Texture2D.MipSlice = 0;
    rtv_desc.Texture2D.PlaneSlice = 0;
    ssao->device->CreateRenderTargetView(ssao->normal_map, &rtv_desc, ssao->normal_map_cpu_rtv);

    rtv_desc.Format = ssao->ambient_map_format;
    ssao->device->CreateRenderTargetView(ssao->ambient_map0, &rtv_desc, ssao->ambient_map0_cpu_rtv);
    ssao->device->CreateRenderTargetView(ssao->ambient_map1, &rtv_desc, ssao->ambient_map1_cpu_rtv);
}

void
SSAO_SetPSOs (SSAO * ssao, ID3D12PipelineState * ssao_pso, ID3D12PipelineState * blur_pso) {
    ssao->ssao_pso = ssao_pso;
    ssao->blur_pso = blur_pso;
}

void
SSAO_Resize (SSAO * ssao, UINT w, UINT h) {
    if ((ssao->render_target_width != w) || (ssao->render_target_height != h)) {
        ssao->render_target_width = w;
        ssao->render_target_height = h;

        // we render to ambient map at half resolution
        ssao->viewport.TopLeftX = 0.0f;
        ssao->viewport.TopLeftY = 0.0f;
        ssao->viewport.Width = ssao->render_target_width / 2.0f;
        ssao->viewport.Height = ssao->render_target_height / 2.0f;
        ssao->viewport.MinDepth = 0.0f;
        ssao->viewport.MaxDepth = 1.0f;

        ssao->scissor_rect = {0, 0, (int)ssao->render_target_width / 2, (int)ssao->render_target_height / 2};

        create_resources_internal(ssao);
    }
}

void
SSAO_ComputeSSAO (
    SSAO * ssao,
    ID3D12GraphicsCommandList * cmdlist,
    FrameResource * curr_frame,
    int blur_count
) {
    cmdlist->RSSetViewports(1, &ssao->viewport);
    cmdlist->RSSetScissorRects(1, &ssao->scissor_rect);

    // we compute initial ssao to ambient_map0
    resource_usage_transition(
        cmdlist, ssao->ambient_map0,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );

    float clear_vals[] = {1.0f, 1.0f, 1.0f, 1.0f};
    cmdlist->ClearRenderTargetView(ssao->ambient_map0_cpu_rtv, clear_vals, 0, nullptr);

    cmdlist->OMSetRenderTargets(1, &ssao->ambient_map0_cpu_rtv, true, nullptr);

    // bind cbuffer
    D3D12_GPU_VIRTUAL_ADDRESS ssao_cb_address = curr_frame->ssao_cb->GetGPUVirtualAddress();
    cmdlist->SetGraphicsRootConstantBufferView(0, ssao_cb_address);
    cmdlist->SetGraphicsRoot32BitConstant(1, 0, 0);

    // bind normal and depth maps
    cmdlist->SetGraphicsRootDescriptorTable(2, ssao->normal_map_gpu_srv);

    // bind random vector map
    cmdlist->SetGraphicsRootDescriptorTable(3, ssao->random_vector_map_gpu_srv);

    cmdlist->SetPipelineState(ssao->ssao_pso);

    // draw fullscreen quad
    cmdlist->IASetVertexBuffers(0, 0, nullptr);
    cmdlist->IASetIndexBuffer(nullptr);
    cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdlist->DrawInstanced(6, 1, 0, 0);

    // change back ambient_map0 to be used as shader input
    resource_usage_transition(
        cmdlist, ssao->ambient_map0,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_GENERIC_READ
    );

    blur_ambient_map(ssao, cmdlist, curr_frame, blur_count);
}
