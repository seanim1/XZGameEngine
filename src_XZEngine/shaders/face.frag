#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = inUV;

    vec2 left_center  = vec2(0.35, 0.4);
    vec2 right_center = vec2(0.65, 0.4);

    float left_dist  = length(uv - left_center);
    float right_dist = length(uv - right_center);

    // mouth
    float mouth   = length(uv - vec2(0.5, 0.55)) - 0.18;
    bool in_smile = uv.y > 0.55;

    // tongue — ellipse inside the mouth, lower half only
    vec2 tongue_center = vec2(0.5, 0.68);
    vec2 tongue_scale  = vec2(0.09, 0.06); // wider than tall
    vec2 tongue_uv     = (uv - tongue_center) / tongue_scale;
    bool in_tongue     = dot(tongue_uv, tongue_uv) < 1.0 && uv.y > 0.62;

    // layered eye: outer black, white ring, inner black pupil
    float eye_radius_outer = 0.08f;
    float eye_radius_white = eye_radius_outer * 0.95f;
    float eye_radius_pupil = eye_radius_outer * 0.7f;

    bool in_left_eye    = left_dist  < eye_radius_outer;
    bool in_right_eye   = right_dist < eye_radius_outer;
    bool in_left_white  = left_dist  < eye_radius_white;
    bool in_right_white = right_dist < eye_radius_white;
    bool in_left_pupil  = left_dist  < eye_radius_pupil;
    bool in_right_pupil = right_dist < eye_radius_pupil;

    bool in_eye   = in_left_eye   || in_right_eye;
    bool in_white = in_left_white || in_right_white;
    bool in_pupil = in_left_pupil || in_right_pupil;

    if (in_pupil)
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
    else if (in_white)
        outColor = vec4(1.0, 1.0, 1.0, 1.0);
    else if (in_eye)
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
    else if (in_tongue && mouth < 0.0 && in_smile)
        outColor = vec4(0.85, 0.2, 0.2, 1.0);      // red tongue
    else if (mouth < 0.0 && in_smile)
        outColor = vec4(0.0, 0.0, 0.0, 1.0);       // mouth
    else
        outColor = vec4(0.0, 0.0, 0.0, 0.0);       // transparent
}