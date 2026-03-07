#version 310 es
precision highp float;
precision highp sampler2D;

uniform sampler2D u_image;
uniform float u_width;
uniform float u_height;
uniform int u_block_size;
uniform float u_C;

layout(location = 0) out float f_out_color;

void main() {
    vec2 texCoord = gl_FragCoord.xy / vec2(u_width, u_height);
    float sum = 0.0;
    int count = 0;
    
    // Calculate local mean
    for (int y = -u_block_size; y <= u_block_size; y++) {
        for (int x = -u_block_size; x <= u_block_size; x++) {
            vec2 samplePos = texCoord + vec2(float(x) / u_width, float(y) / u_height);
            sum += texture(u_image, samplePos).r;
            count++;
        }
    }
    float mean = sum / float(count);
    
    // Apply threshold
    float intensity = texture(u_image, texCoord).r;
    if (intensity > (mean - u_C)) {
        f_out_color = 1.0; // White
    } else {
        f_out_color = 0.0; // Black
    }
}
