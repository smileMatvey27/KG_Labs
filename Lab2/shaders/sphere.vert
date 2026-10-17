#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;

layout(location = 0) out vec3 out_color;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec3 out_world_pos;

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

void main() {
    vec4 world_pos = model.model * vec4(in_position, 1.0);
    gl_Position = scene.view_projection * world_pos;
    gl_Position.y = -gl_Position.y;
    out_world_pos = world_pos.xyz;
    out_color = in_color;
    out_normal = mat3(model.normal_matrix) * in_normal;
}