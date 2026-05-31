#include <color/difference.h>

#include <math.h>

float mel_delta_e_76(mel_lab a, mel_lab b) {
    float dl = a.l - b.l;
    float da = a.a - b.a;
    float db = a.b - b.b;
    return sqrtf(dl * dl + da * da + db * db);
}

float mel_delta_e_94(mel_lab a, mel_lab b) {
    const float kl = 1.0f, k1 = 0.045f, k2 = 0.015f;
    float c1 = hypotf(a.a, a.b);
    float c2 = hypotf(b.a, b.b);
    float dl = a.l - b.l;
    float dc = c1 - c2;
    float da = a.a - b.a;
    float db = a.b - b.b;
    float dh2 = da * da + db * db - dc * dc;
    float dh = dh2 > 0.0f ? sqrtf(dh2) : 0.0f;
    float sc = 1.0f + k1 * c1;
    float sh = 1.0f + k2 * c1;
    float tl = dl / kl;
    float tc = dc / sc;
    float th = dh / sh;
    return sqrtf(tl * tl + tc * tc + th * th);
}

float mel_delta_e_2000(mel_lab lab1, mel_lab lab2) {
    const double deg = 180.0 / 3.14159265358979323846;
    const double rad = 3.14159265358979323846 / 180.0;
    const double pow25_7 = 6103515625.0;

    double l1 = lab1.l, a1 = lab1.a, b1 = lab1.b;
    double l2 = lab2.l, a2 = lab2.a, b2 = lab2.b;

    double c1 = sqrt(a1 * a1 + b1 * b1);
    double c2 = sqrt(a2 * a2 + b2 * b2);
    double cbar = (c1 + c2) / 2.0;
    double cbar7 = pow(cbar, 7.0);
    double g = 0.5 * (1.0 - sqrt(cbar7 / (cbar7 + pow25_7)));

    double a1p = (1.0 + g) * a1;
    double a2p = (1.0 + g) * a2;
    double c1p = sqrt(a1p * a1p + b1 * b1);
    double c2p = sqrt(a2p * a2p + b2 * b2);

    double h1p = (a1p == 0.0 && b1 == 0.0) ? 0.0 : atan2(b1, a1p) * deg;
    if (h1p < 0.0)
        h1p += 360.0;
    double h2p = (a2p == 0.0 && b2 == 0.0) ? 0.0 : atan2(b2, a2p) * deg;
    if (h2p < 0.0)
        h2p += 360.0;

    double dlp = l2 - l1;
    double dcp = c2p - c1p;

    double dhp;
    if (c1p * c2p == 0.0) {
        dhp = 0.0;
    } else {
        dhp = h2p - h1p;
        if (dhp > 180.0)
            dhp -= 360.0;
        else if (dhp < -180.0)
            dhp += 360.0;
    }
    double dHp = 2.0 * sqrt(c1p * c2p) * sin((dhp / 2.0) * rad);

    double lbarp = (l1 + l2) / 2.0;
    double cbarp = (c1p + c2p) / 2.0;

    double hbarp;
    if (c1p * c2p == 0.0) {
        hbarp = h1p + h2p;
    } else if (fabs(h1p - h2p) > 180.0) {
        hbarp = (h1p + h2p < 360.0) ? (h1p + h2p + 360.0) / 2.0 : (h1p + h2p - 360.0) / 2.0;
    } else {
        hbarp = (h1p + h2p) / 2.0;
    }

    double t = 1.0 - 0.17 * cos((hbarp - 30.0) * rad) + 0.24 * cos((2.0 * hbarp) * rad) +
               0.32 * cos((3.0 * hbarp + 6.0) * rad) - 0.20 * cos((4.0 * hbarp - 63.0) * rad);

    double dtheta = 30.0 * exp(-((hbarp - 275.0) / 25.0) * ((hbarp - 275.0) / 25.0));
    double cbarp7 = pow(cbarp, 7.0);
    double rc = 2.0 * sqrt(cbarp7 / (cbarp7 + pow25_7));
    double sl = 1.0 + (0.015 * (lbarp - 50.0) * (lbarp - 50.0)) /
                          sqrt(20.0 + (lbarp - 50.0) * (lbarp - 50.0));
    double sc = 1.0 + 0.045 * cbarp;
    double sh = 1.0 + 0.015 * cbarp * t;
    double rt = -sin((2.0 * dtheta) * rad) * rc;

    double tl = dlp / sl;
    double tc = dcp / sc;
    double th = dHp / sh;
    return (float)sqrt(tl * tl + tc * tc + th * th + rt * tc * th);
}

float mel_delta_e_ok(mel_oklab a, mel_oklab b) {
    float dl = a.l - b.l;
    float da = a.a - b.a;
    float db = a.b - b.b;
    return sqrtf(dl * dl + da * da + db * db);
}
