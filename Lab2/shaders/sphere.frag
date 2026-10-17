#version 450

layout(location = 0) in vec3 in_color;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_world_pos;

layout(location = 0) out vec4 out_color;

layout(std140, set = 0, binding = 0) uniform SceneUniforms {
    mat4 view_projection;
    vec4 camera_position;
    vec4 directional_light_direction;
    vec4 directional_light_color;
    vec4 ambient_light;
    uint point_light_count;
    uint spot_light_count;
} scene;

layout(std140, set = 1, binding = 0) uniform ModelUniforms {
    mat4 model;
    mat4 normal_matrix;
    vec4 albedo;
    vec4 specular;
    float shininess;
} model;

layout(push_constant) uniform PushConstants {
    vec4 tint;
} pc;

struct PointLight {
    vec4 position;
    vec4 color;
};

layout(std430, set = 0, binding = 1) buffer PointLights {
    PointLight point_lights[];
};

struct SpotLight {
    vec4 position;
    vec4 direction;
    vec4 color; 
    float inner_angle;
    float outer_angle;
};

layout(std430, set = 0, binding = 2) buffer Spotlights {
    SpotLight spot_lights[];
};

void main() {
    vec3 N = normalize(in_normal);
    vec3 V = normalize(scene.camera_position.xyz - in_world_pos);
    vec3 base_color = model.albedo.rgb * in_color;

    vec3 L_dir = normalize(-scene.directional_light_direction.xyz);
    vec3 H_dir = normalize(L_dir + V);
    float NdotL_dir = max(dot(N, L_dir), 0.0);
    float NdotH_dir = max(dot(N, H_dir), 0.0);
    float spec_dir = pow(NdotH_dir, model.shininess);

    vec3 light_color_dir =
        scene.directional_light_color.rgb * scene.directional_light_color.a;
    vec3 diffuse_dir = NdotL_dir * base_color;
    vec3 specular_dir = spec_dir * model.specular.rgb;

    vec3 result = light_color_dir * (diffuse_dir + specular_dir);

    for (uint i = 0u; i < scene.point_light_count; ++i) {
        PointLight pl = point_lights[i];

        vec3 to_light = pl.position.xyz - in_world_pos;
        float distance = length(to_light);

        if (distance < 0.0001) {
            continue;
        }

        vec3 L = to_light / distance;
        vec3 H = normalize(L + V);

        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float spec = pow(NdotH, model.shininess);

        float attenuation = 1.0 / (distance * distance);

        vec3 pl_color = pl.color.rgb * pl.color.a;
        vec3 diffuse = NdotL * base_color * pl_color * attenuation;
        vec3 specular = spec * model.specular.rgb * pl_color * attenuation;

        result += diffuse + specular;
    }

    for (uint i = 0u; i < scene.spot_light_count; ++i) {
        SpotLight sl = spot_lights[i];

        vec3 to_light = sl.position.xyz - in_world_pos;
        float distance = length(to_light);

        if (distance < 0.0001) {
            continue;
        }

        vec3 L = to_light / distance;
        vec3 H = normalize(L + V);

        vec3 spot_dir = normalize(sl.direction.xyz);


        float theta = dot(spot_dir, -L);

        float inner_cos = cos(radians(sl.inner_angle));
        float outer_cos = cos(radians(sl.outer_angle));

        float epsilon = inner_cos - outer_cos;
        float spot_factor = 1.0;

        if (epsilon > 0.0) {
            spot_factor = clamp((theta - outer_cos) / epsilon, 0.0, 1.0);
        } else {
            spot_factor = (theta > outer_cos) ? 1.0 : 0.0;
        }

        if (spot_factor <= 0.0) {
            continue;
        }

        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float spec = pow(NdotH, model.shininess);

        float attenuation = 1.0 / (distance * distance);

        vec3 sl_color = sl.color.rgb * sl.color.a;
        vec3 diffuse = NdotL * base_color * sl_color * attenuation * spot_factor;
        vec3 specular = spec * model.specular.rgb * sl_color * attenuation * spot_factor;

        result += diffuse + specular;
    }

    vec3 ambient = scene.ambient_light.rgb * scene.ambient_light.a * base_color;
    result += ambient;

    out_color = vec4(result * pc.tint.rgb, pc.tint.a);
}