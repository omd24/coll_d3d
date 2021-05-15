#include "blur_filter.h"
#include "headers/utils.h"

static void
calc_gauss_weights (BlurFilter * filter, float sigma, float out_weights [], size_t weight_count) {
    int r = (int)ceilf(2.0f * sigma);
    _ASSERT_EXPR(weight_count == (2 * r + 1), _T("Invalid weight counts"));
    _ASSERT_EXPR(r <= filter->blur_radius_max, _T("Invalid blur radius"));

    float two_sigma2 = 2.0f * sigma * sigma;

    float weight_sum = 0.0f;
    for (int i = -r; i <= r; ++i) {
        float x = (float)i;
        out_weights[i + r] = expf(-(x * x) / two_sigma2);
        weight_sum += out_weights[i + r];
    }
    // normalize (divide by sum)
    for (int i = 0; i < weight_count; ++i) {
        out_weights[i] /= weight_sum;
    }
}
static void
create_descriptors_internal (BlurFilter * filter) {
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

    filter->device->CreateShaderResourceView(filter->blur_map0, &srv_desc, filter->blur0_cpu_srv);
    filter->device->CreateUnorderedAccessView(filter->blur_map0, NULL, &uav_desc, filter->blur0_cpu_uav);

    filter->device->CreateShaderResourceView(filter->blur_map1, &srv_desc, filter->blur1_cpu_srv);
    filter->device->CreateUnorderedAccessView(filter->blur_map1, NULL, &uav_desc, filter->blur1_cpu_uav);
}
static void
create_resources_internal (BlurFilter * filter) {
    // NOTE(omid): compressed formats cannot be used for UAV. 
    // Therefore this format does not support D3D11_BIND_UNORDERED_ACCESS ?

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
        D3D12_RESOURCE_STATE_COMMON, NULL, IID_PPV_ARGS(&filter->blur_map0)
    );
    filter->device->CreateCommittedResource(
        &heap_def, D3D12_HEAP_FLAG_NONE, &tex_desc,
        D3D12_RESOURCE_STATE_COMMON, NULL, IID_PPV_ARGS(&filter->blur_map1)
    );

    filter->new_resources_flag = true;
}
size_t
BlurFilter_CalculateRequiredSize () {
    // calculate weights array size
    float sigma = 2.5f;
    int r = (int)ceilf(2.0f * sigma);
    int weight_count = (2 * r + 1);
    return sizeof(BlurFilter) + (weight_count * sizeof(float));
}
BlurFilter *
BlurFilter_Init (BYTE * memory, ID3D12Device * dev, UINT w, UINT h, DXGI_FORMAT format) {
    BlurFilter * ret = nullptr;
    if (memory) {
        ret = reinterpret_cast<BlurFilter *>(memory);
        ret->weights = reinterpret_cast<float *>(memory + sizeof(BlurFilter));

        // calculate weights
        float sigma = 2.5f;
        int r = (int)ceilf(2.0f * sigma);
        ret->weight_count = (2 * r + 1);
        ret->blur_radius = (int)ret->weight_count / 2;
        ret->blur_radius_max = 5;
        calc_gauss_weights(ret, sigma, ret->weights, ret->weight_count);

        ret->device = dev;
        ret->width = w;
        ret->height = h;
        ret->format = format;
        ret->new_resources_flag = false;

        create_resources_internal(ret);
    }
    return ret;
}
void
BlurFilter_Deinit (BlurFilter * filter) {
    filter->blur_map1->Release();
    filter->blur_map0->Release();
}
void
BlurFilter_CreateDescriptors (
    BlurFilter * filter,
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_descriptor,
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_descriptor,
    UINT descriptor_size
) {
    // save references to the descriptors
    filter->blur0_cpu_srv = hcpu_descriptor;

    hcpu_descriptor.ptr += descriptor_size;
    filter->blur0_cpu_uav = hcpu_descriptor;

    hcpu_descriptor.ptr += descriptor_size;
    filter->blur1_cpu_srv = hcpu_descriptor;

    hcpu_descriptor.ptr += descriptor_size;
    filter->blur1_cpu_uav = hcpu_descriptor;

    filter->blur0_gpu_srv = hgpu_descriptor;

    hgpu_descriptor.ptr += descriptor_size;
    filter->blur0_gpu_uav = hgpu_descriptor;

    hgpu_descriptor.ptr += descriptor_size;
    filter->blur1_gpu_srv = hgpu_descriptor;

    hgpu_descriptor.ptr += descriptor_size;
    filter->blur1_gpu_uav = hgpu_descriptor;

    create_descriptors_internal(filter);
}
bool
BlurFilter_Resize (BlurFilter * filter, UINT w, UINT h) {
    bool result = false;
    if (filter) {
        if ((filter->width != w) || (filter->height != h)) {
            filter->width = w;
            filter->height = h;

            // prepare for resource RE-creation
            filter->blur_map1->Release();
            filter->blur_map0->Release();

            create_resources_internal(filter);   // RE-new resources
            create_descriptors_internal(filter); // new resources so need new descriptors

            result = true;
        }
    }
    return result;
}
void
BlurFilter_Execute (
    BlurFilter * filter,
    ID3D12GraphicsCommandList * cmdlist,
    ID3D12RootSignature * root_sig,
    ID3D12PipelineState * hor_blur_pso,
    ID3D12PipelineState * ver_blur_pso,
    ID3D12Resource * input_tex,
    UINT blur_count
) {
    if (blur_count > 0) {
        cmdlist->SetComputeRootSignature(root_sig);

        cmdlist->SetComputeRoot32BitConstants(0, 1, &filter->blur_radius, 0);
        cmdlist->SetComputeRoot32BitConstants(0, (UINT)filter->weight_count, filter->weights, 1);

        resource_usage_transition(cmdlist, input_tex, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        if (filter->new_resources_flag)
            resource_usage_transition(cmdlist, filter->blur_map0, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        else
            resource_usage_transition(cmdlist, filter->blur_map0, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);

        // copy input (here backbuffer) to blur_map0
        cmdlist->CopyResource(filter->blur_map0, input_tex);

        resource_usage_transition(cmdlist, filter->blur_map0, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);

        if (filter->new_resources_flag)
            resource_usage_transition(cmdlist, filter->blur_map1, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        for (UINT i = 0; i < blur_count; ++i) {
            //
            // Horizontal Blur Pass
            //
            cmdlist->SetPipelineState(hor_blur_pso);

            cmdlist->SetComputeRootDescriptorTable(1, filter->blur0_gpu_srv);
            cmdlist->SetComputeRootDescriptorTable(2, filter->blur1_gpu_uav);

            // how many groups to dispatch
            UINT ngroup_x = (UINT)ceilf(filter->width / 256.0f);
            cmdlist->Dispatch(ngroup_x, filter->height, 1);

            resource_usage_transition(cmdlist, filter->blur_map0, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            resource_usage_transition(cmdlist, filter->blur_map1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);

            //
            // Vertical Blur Pass
            //
            cmdlist->SetPipelineState(ver_blur_pso);

            cmdlist->SetComputeRootDescriptorTable(1, filter->blur1_gpu_srv);
            cmdlist->SetComputeRootDescriptorTable(2, filter->blur0_gpu_uav);

            // how many groups to dispatch
            UINT ngroup_y = (UINT)ceilf(filter->height / 256.0f);
            cmdlist->Dispatch(filter->width, ngroup_y, 1);

            resource_usage_transition(cmdlist, filter->blur_map0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);

            resource_usage_transition(cmdlist, filter->blur_map1, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            filter->new_resources_flag = false;
        }
    }
}
