#version 460

layout(push_constant, std430) uniform Root
{
    float time;
    float aspect;
    float pad0;
    float pad1;
} root;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

float sd_sphere(vec3 p, float r) { return length(p) - r; }

float sd_torus(vec3 p, vec2 t)
{
    vec2 q = vec2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

float sd_plane(vec3 p) { return p.y + 1.0; }

float smin(float a, float b, float k)
{
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float map(vec3 p)
{
    float t = root.time;

    vec3 q = p;
    q.y -= 0.15 * sin(t * 0.9);
    float d = sd_torus(q, vec2(1.35, 0.32));

    for (int i = 0; i < 4; ++i)
    {
        float fi = float(i);
        float a = t * (0.6 + 0.18 * fi) + fi * 1.5707963;
        vec3  c = vec3(1.6 * cos(a), 0.4 + 0.35 * sin(t * 1.3 + fi), 1.6 * sin(a));
        float s = sd_sphere(p - c, 0.45 + 0.1 * sin(t + fi));
        d = smin(d, s, 0.55);
    }

    return min(d, sd_plane(p));
}

vec3 calc_normal(vec3 p)
{
    vec2 e = vec2(0.001, 0.0);
    return normalize(vec3(
        map(p + e.xyy) - map(p - e.xyy),
        map(p + e.yxy) - map(p - e.yxy),
        map(p + e.yyx) - map(p - e.yyx)));
}

float soft_shadow(vec3 ro, vec3 rd, float mint, float maxt, float k)
{
    float res = 1.0;
    float t = mint;
    for (int i = 0; i < 48; ++i)
    {
        float h = map(ro + rd * t);
        if (h < 0.001)
            return 0.0;
        res = min(res, k * h / t);
        t += clamp(h, 0.02, 0.3);
        if (t > maxt)
            break;
    }
    return clamp(res, 0.0, 1.0);
}

float ambient_occlusion(vec3 p, vec3 n)
{
    float occ = 0.0;
    float sca = 1.0;
    for (int i = 0; i < 5; ++i)
    {
        float hr = 0.02 + 0.12 * float(i);
        float dd = map(p + n * hr);
        occ += (hr - dd) * sca;
        sca *= 0.7;
    }
    return clamp(1.0 - 3.0 * occ, 0.0, 1.0);
}

void main()
{
    vec2 uv = (v_uv * 2.0 - 1.0);
    uv.x *= root.aspect;
    uv.y = -uv.y;

    float ct = root.time * 0.25;
    vec3  ro = vec3(4.2 * cos(ct), 1.9 + 0.6 * sin(ct * 0.7), 4.2 * sin(ct));
    vec3  ta = vec3(0.0, 0.1, 0.0);
    vec3  fwd = normalize(ta - ro);
    vec3  right = normalize(cross(vec3(0.0, 1.0, 0.0), fwd));
    vec3  up = cross(fwd, right);
    vec3  rd = normalize(uv.x * right + uv.y * up + 1.7 * fwd);

    float t = 0.0;
    bool  hit = false;
    for (int i = 0; i < 96; ++i)
    {
        vec3  p = ro + rd * t;
        float d = map(p);
        if (d < 0.0015)
        {
            hit = true;
            break;
        }
        t += d;
        if (t > 30.0)
            break;
    }

    vec3 col = vec3(0.0);
    if (hit)
    {
        vec3  p = ro + rd * t;
        vec3  n = calc_normal(p);
        vec3  ldir = normalize(vec3(0.7, 0.9, -0.5));
        float diff = clamp(dot(n, ldir), 0.0, 1.0);
        float sh = soft_shadow(p + n * 0.01, ldir, 0.02, 12.0, 12.0);
        float ao = ambient_occlusion(p, n);
        float fres = pow(clamp(1.0 + dot(rd, n), 0.0, 1.0), 3.0);

        vec3 base;
        if (p.y < -0.99)
        {
            float chk = mod(floor(p.x) + floor(p.z), 2.0);
            base = mix(vec3(0.16, 0.18, 0.22), vec3(0.28, 0.30, 0.34), chk);
        }
        else
        {
            base = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + 0.7 * p.y + root.time * 0.5 + length(p.xz));
            base = mix(vec3(0.3), base, 0.7);
        }

        vec3 amb = vec3(0.18, 0.21, 0.28) * ao;
        col = base * (amb + diff * sh * vec3(1.05, 0.95, 0.8));
        col += fres * 0.35 * vec3(0.6, 0.8, 1.0) * ao;
        vec3 hvec = normalize(ldir - rd);
        col += sh * pow(clamp(dot(n, hvec), 0.0, 1.0), 48.0) * vec3(1.0);
    }
    else
    {
        float g = 0.5 + 0.5 * rd.y;
        col = mix(vec3(0.02, 0.03, 0.05), vec3(0.10, 0.14, 0.22), g);
        col += pow(clamp(dot(rd, normalize(vec3(0.7, 0.9, -0.5))), 0.0, 1.0), 64.0) * vec3(0.9, 0.85, 0.7);
    }

    col = 1.0 - exp(-col * 1.4);
    col = pow(col, vec3(0.4545));
    o_color = vec4(col, 1.0);
}
