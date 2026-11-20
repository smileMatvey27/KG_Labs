#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(std140, set = 0, binding = 0) uniform SceneUniforms {
    mat4 view_projection;
    mat4 light_view_projection;
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

void main() {
    vec4 world_pos = model.model * vec4(in_position, 1.0);
    gl_Position = scene.light_view_projection * world_pos;
}