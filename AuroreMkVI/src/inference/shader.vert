#version 310 es

precision highp float;
precision highp int;

layout(location = 0) in vec2 a_position;

uniform vec2 u_frame_size;
uniform float u_candidate_spacing;

out vec2 v_candidate_texcoord;
out float v_local_x;
out float v_local_y;

void main() {
    int width = int(u_candidate_spacing);
    int height = int(u_candidate_spacing);

    int col = gl_VertexID % width;
    int row = gl_VertexID / width;

    float spacing = u_candidate_spacing;
    vec2 candidate_pos = vec2(float(col) + 0.5, float(row) + 0.5) * spacing;

    candidate_pos = clamp(candidate_pos, vec2(0.0), u_frame_size - vec2(1.0));

    vec2 ndc = (candidate_pos / u_frame_size) * 2.0 - 1.0;
    ndc.y = -ndc.y;

    gl_Position = vec4(ndc, 0.0, 1.0);

    v_candidate_texcoord = candidate_pos / u_frame_size;
    v_local_x = float(col) / float(width);
    v_local_y = float(row) / float(height);
}
