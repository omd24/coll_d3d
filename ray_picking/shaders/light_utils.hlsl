#define MAX_LIGHTS 16

struct Light {
    float3 strength;
    float falloff_start;
    float3 direction;
    float falloff_end;
    float3 position;
    float spot_power;
};
struct Material {
    float4 diffuse_albedo;
    float3 fresnel_r0;
    float shininess;
};
//
// helper functions
//
float
calc_attenuation (float d, float falloff_start, float falloff_end) {
    // linear falloff
    return saturate((falloff_end - d) / (falloff_end - falloff_start));
}
// schlick apporximation
// r0 = ((n-1)/(n+1))^2, where n is refraction index
float3
schlick_fresnel (float3 r0, float3 normal, float3 light_vec) {
    float cos_incident = saturate(dot(normal, light_vec));
    float f0 = 1.0f - cos_incident;
    float3 reflect_percent = r0 + (1.0f - r0) * (f0 * f0 * f0 * f0 * f0);

    return reflect_percent;
}
float3
blinn_phong (float3 light_strength, float3 light_vec, float3 normal, float3 to_eye, Material mat) {
    float m = mat.shininess * 256.0f;
    float3 half_vec = normalize(to_eye + light_vec);
    
    float roughness_coef = (m + 8.0f) * pow(max(dot(half_vec, normal), 0.0f), m) / 8.0f;
    float3 fresnel_factor = schlick_fresnel(mat.fresnel_r0, half_vec, light_vec);
    
    float3 spec_albedo = fresnel_factor * roughness_coef;
    
    // our spec formula may go outside [0,1] but we're doing LDR so scaling is needed
    spec_albedo = spec_albedo / (spec_albedo + 1.0f);
    
    return (mat.diffuse_albedo.rgb + spec_albedo) * light_strength;
}
//
//  light equation for directional lights
//
float3
compute_directional_light (Light l, Material mat, float3 normal, float3 to_eye) {
    float3 light_vec = -l.direction;
    
    // lambert cosine law
    float ndotl = max(dot(light_vec, normal), 0.0f);
    float3 light_strength = l.strength * ndotl;
    
    return blinn_phong(light_strength, light_vec, normal, to_eye, mat);
}
//
//  light equation for point lights
//
float3
compute_point_light (Light l, Material mat, float3 pos, float3 normal, float3 to_eye) {
    float3 light_vec = l.position - pos;
    
    float d = length(light_vec);
    
    if (d > l.falloff_end) {
        return 0.0f;
    }
    // normalize light vector
    light_vec /= d;
    
    // lambert cosine law
    float ndotl = max(dot(light_vec, normal), 0.0f);
    float3 light_strength = l.strength * ndotl;
    
    // attenuate light by distance
    float att = calc_attenuation(d, l.falloff_start, l.falloff_end);
    light_strength *= att;
    
    return blinn_phong(light_strength, light_vec, normal, to_eye, mat);
}
//
//  light equation for spot lights
//
float3
compute_spot_light (Light l, Material mat, float3 pos, float3 normal, float3 to_eye) {
    float3 light_vec = l.position - pos;
    
    float d = length(light_vec);
    
    if (d > l.falloff_end) {
        return 0.0f;
    }
    // normalize light vector
    light_vec /= d;
    
    // lambert cosine law
    float ndotl = max(dot(light_vec, normal), 0.0f);
    float3 light_strength = l.strength * ndotl;
    
    // attenuate light by distance
    float att = calc_attenuation(d, l.falloff_start, l.falloff_end);
    light_strength *= att;
    
    // scale by spotlight
    float spot_factor = pow(max(dot(-light_vec, l.direction), 0.0f), l.spot_power);
    light_strength *= spot_factor;
    
    return blinn_phong(light_strength, light_vec, normal, to_eye, mat);
}
//
//  accumulating multiple lights
//
float4
compute_lighting (Light g_light[MAX_LIGHTS], Material mat, float3 pos, float3 normal, float3 to_eye, float3 shadow_factor) {
    float3 res = 0.0f;
    int i = 0;
    
#if (NUM_DIR_LIGHTS > 0)
    for (i = 0; i < NUM_DIR_LIGHTS; ++i) {
        res += shadow_factor[i] * compute_directional_light(g_light[i], mat, normal, to_eye);
    }
#endif
#if (NUM_POINT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; ++i) {
        res += compute_point_light(g_light[i], mat, pos, normal, to_eye);
    }
#endif
#if (NUM_SPOT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i) {
        res += compute_spot_light(g_light[i], mat, pos, normal, to_eye);
    }
#endif

    return float4(res, 0.0f);
}
