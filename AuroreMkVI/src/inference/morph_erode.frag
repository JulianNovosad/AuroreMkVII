#version 310 es
precision highp float;
precision highp sampler2D;

uniform sampler2D u_image;
uniform vec2 u_image_size;
uniform int u_kernel_size; // Assuming square kernel for now

layout(location = 0) out float f_out_color;

void main() {
    vec2 texCoord = gl_FragCoord.xy / u_image_size;
    
    float min_val = 1.0;
    
    for (int y = -u_kernel_size; y <= u_kernel_size; y++) {
        for (int x = -u_kernel_size; x <= u_kernel_size; x++) {
            vec2 sample_offset = vec2(float(x), float(y)) / u_image_size;
            min_val = min(min_val, texture(u_image, texCoord + sample_offset).r);
        }
    }
    
    f_out_color = min_val; // For erosion (used in open)
}