#version 310 es
precision highp float;
precision highp sampler2D;

uniform sampler2D u_camera;
uniform vec2 u_frame_size;
uniform float u_center_x;
uniform float u_center_y;
uniform float u_max_radius;

layout(location = 0) out vec4 f_target_result;

const float PI = 3.14159265359;
const int NUM_DIRECTIONS = 8;
const int SAMPLES_PER_DIRECTION = 64;
const int MIN_RINGS = 3;

int count_rings_along_direction(vec2 direction) {
    int rings = 0;
    float prev_intensity = -1.0;
    int transitions = 0;
    bool found_first = false;

    for (int i = 0; i < SAMPLES_PER_DIRECTION; i++) {
        float t = float(i + 1) * u_max_radius / float(SAMPLES_PER_DIRECTION);
        vec2 sample_pos = vec2(u_center_x, u_center_y) + direction * t;

        if (sample_pos.x < 0.0 || sample_pos.x >= u_frame_size.x ||
            sample_pos.y < 0.0 || sample_pos.y >= u_frame_size.y) {
            break;
        }

        vec2 texcoord = sample_pos / u_frame_size;
        float intensity = texture(u_camera, texcoord).r;

        if (!found_first) {
            prev_intensity = intensity;
            found_first = true;
            continue;
        }

        float diff = intensity - prev_intensity;
        if (abs(diff) > 0.15) {
            transitions++;
            prev_intensity = intensity;
        }
    }

    return transitions / 2;
}

float compute_confidence(vec2 directions[8]) {
    int rings_sum = 0;
    for (int i = 0; i < NUM_DIRECTIONS; i++) {
        rings_sum += count_rings_along_direction(directions[i]);
    }
    return float(rings_sum) / float(NUM_DIRECTIONS);
}

vec4 find_bbox_along_direction(vec2 direction) {
    float t_min = u_max_radius;
    float t_max = 0.0;

    for (int i = 0; i < SAMPLES_PER_DIRECTION; i++) {
        float t = float(i + 1) * u_max_radius / float(SAMPLES_PER_DIRECTION);
        vec2 sample_pos = vec2(u_center_x, u_center_y) + direction * t;

        if (sample_pos.x < 0.0 || sample_pos.x >= u_frame_size.x ||
            sample_pos.y < 0.0 || sample_pos.y >= u_frame_size.y) {
            break;
        }

        vec2 texcoord = sample_pos / u_frame_size;
        float intensity = texture(u_camera, texcoord).r;

        if (intensity > 0.5) {
            t_min = min(t_min, t);
            t_max = max(t_max, t);
        }
    }

    return vec4(t_min, t_max, 0.0, 0.0);
}

vec2 compute_bbox_radius() {
    vec2 directions[8];
    directions[0] = vec2(1.0, 0.0);
    directions[1] = vec2(0.0, 1.0);
    directions[2] = vec2(-1.0, 0.0);
    directions[3] = vec2(0.0, -1.0);
    directions[4] = vec2(0.707, 0.707);
    directions[5] = vec2(-0.707, 0.707);
    directions[6] = vec2(-0.707, -0.707);
    directions[7] = vec2(0.707, -0.707);

    float min_r = u_max_radius;
    float max_r = 0.0;

    for (int i = 0; i < 8; i++) {
        vec4 result = find_bbox_along_direction(directions[i]);
        min_r = min(min_r, result.x);
        max_r = max(max_r, result.y);
    }

    return vec2(min_r, max_r);
}

void main() {
    vec2 directions[8];
    directions[0] = vec2(1.0, 0.0);
    directions[1] = vec2(0.0, 1.0);
    directions[2] = vec2(-1.0, 0.0);
    directions[3] = vec2(0.0, -1.0);
    directions[4] = vec2(0.707, 0.707);
    directions[5] = vec2(-0.707, 0.707);
    directions[6] = vec2(-0.707, -0.707);
    directions[7] = vec2(0.707, -0.707);

    float avg_rings = compute_confidence(directions);
    int rings = int(avg_rings + 0.5);

    if (rings >= MIN_RINGS) {
        vec2 bbox_r = compute_bbox_radius();

        float x_min = u_center_x - bbox_r.y;
        float y_min = u_center_y - bbox_r.y;
        float x_max = u_center_x + bbox_r.y;
        float y_max = u_center_y + bbox_r.y;

        x_min = max(0.0, x_min);
        y_min = max(0.0, y_min);
        x_max = min(u_frame_size.x - 1.0, x_max);
        y_max = min(u_frame_size.y - 1.0, y_max);

        float confidence = min(1.0, avg_rings / 6.0);

        f_target_result = vec4(
            x_min / u_frame_size.x,
            y_min / u_frame_size.y,
            x_max / u_frame_size.x,
            y_max / u_frame_size.y
        );
    } else {
        f_target_result = vec4(-1.0, -1.0, -1.0, float(rings) / 10.0);
    }
}
