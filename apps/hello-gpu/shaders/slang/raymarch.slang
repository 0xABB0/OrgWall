struct Root
{
    float time;
    float aspect;
    float pad0;
    float pad1;
};

[[vk::push_constant]]
Root root;

struct VOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VOut vs_main(uint vid : SV_VertexID)
{
    VOut o;
    float2 p = float2(float((vid << 1) & 2), float(vid & 2));
    o.uv = p;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

float sd_sphere(float3 p, float r) { return length(p) - r; }

float sd_torus(float3 p, float2 t)
{
    float2 q = float2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

float sd_plane(float3 p) { return p.y + 1.0; }

float smin(float a, float b, float k)
{
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return lerp(b, a, h) - k * h * (1.0 - h);
}

float map(float3 p)
{
    float t = root.time;

    float3 q = p;
    q.y -= 0.15 * sin(t * 0.9);
    float d = sd_torus(q, float2(1.35, 0.32));

    for (int i = 0; i < 4; ++i)
    {
        float fi = float(i);
        float a = t * (0.6 + 0.18 * fi) + fi * 1.5707963;
        float3 c = float3(1.6 * cos(a), 0.4 + 0.35 * sin(t * 1.3 + fi), 1.6 * sin(a));
        float s = sd_sphere(p - c, 0.45 + 0.1 * sin(t + fi));
        d = smin(d, s, 0.55);
    }

    return min(d, sd_plane(p));
}

float3 calc_normal(float3 p)
{
    float2 e = float2(0.001, 0.0);
    return normalize(float3(
        map(p + e.xyy) - map(p - e.xyy),
        map(p + e.yxy) - map(p - e.yxy),
        map(p + e.yyx) - map(p - e.yyx)));
}

float soft_shadow(float3 ro, float3 rd, float mint, float maxt, float k)
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

float ambient_occlusion(float3 p, float3 n)
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

[shader("fragment")]
float4 fs_main(VOut input) : SV_Target
{
    float2 uv = input.uv * 2.0 - 1.0;
    uv.x *= root.aspect;
    uv.y = -uv.y;

    float ct = root.time * 0.25;
    float3 ro = float3(4.2 * cos(ct), 1.9 + 0.6 * sin(ct * 0.7), 4.2 * sin(ct));
    float3 ta = float3(0.0, 0.1, 0.0);
    float3 fwd = normalize(ta - ro);
    float3 right = normalize(cross(float3(0.0, 1.0, 0.0), fwd));
    float3 up = cross(fwd, right);
    float3 rd = normalize(uv.x * right + uv.y * up + 1.7 * fwd);

    float t = 0.0;
    bool hit = false;
    for (int i = 0; i < 96; ++i)
    {
        float3 p = ro + rd * t;
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

    float3 col = float3(0.0, 0.0, 0.0);
    if (hit)
    {
        float3 p = ro + rd * t;
        float3 n = calc_normal(p);
        float3 ldir = normalize(float3(0.7, 0.9, -0.5));
        float diff = clamp(dot(n, ldir), 0.0, 1.0);
        float sh = soft_shadow(p + n * 0.01, ldir, 0.02, 12.0, 12.0);
        float ao = ambient_occlusion(p, n);
        float fres = pow(clamp(1.0 + dot(rd, n), 0.0, 1.0), 3.0);

        float3 base;
        if (p.y < -0.99)
        {
            float chk = fmod(floor(p.x) + floor(p.z), 2.0);
            base = lerp(float3(0.16, 0.18, 0.22), float3(0.28, 0.30, 0.34), chk);
        }
        else
        {
            base = 0.5 + 0.5 * cos(float3(0.0, 2.0, 4.0) + 0.7 * p.y + root.time * 0.5 + length(p.xz));
            base = lerp(float3(0.3, 0.3, 0.3), base, 0.7);
        }

        float3 amb = float3(0.18, 0.21, 0.28) * ao;
        col = base * (amb + diff * sh * float3(1.05, 0.95, 0.8));
        col += fres * 0.35 * float3(0.6, 0.8, 1.0) * ao;
        float3 hvec = normalize(ldir - rd);
        col += sh * pow(clamp(dot(n, hvec), 0.0, 1.0), 48.0) * float3(1.0, 1.0, 1.0);
    }
    else
    {
        float g = 0.5 + 0.5 * rd.y;
        col = lerp(float3(0.02, 0.03, 0.05), float3(0.10, 0.14, 0.22), g);
        col += pow(clamp(dot(rd, normalize(float3(0.7, 0.9, -0.5))), 0.0, 1.0), 64.0) * float3(0.9, 0.85, 0.7);
    }

    col = 1.0 - exp(-col * 1.4);
    col = pow(col, float3(0.4545, 0.4545, 0.4545));
    return float4(col, 1.0);
}
