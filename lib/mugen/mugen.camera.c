#include "mugen.camera.h"
#include "mugen.stage.h"
#include "math.scalar.h"

void mugen_camera_init(Mugen_Camera* cam, Mugen_Stage* stage)
{
    cam->x = stage->camera_startx;
    cam->y = stage->camera_starty;
    cam->bound_left = stage->left_bound;
    cam->bound_right = stage->right_bound;
    cam->bound_high = stage->top_bound;
    cam->bound_low = stage->bottom_bound;
    cam->tension = stage->tension;
    cam->verticalfollow = stage->verticalfollow;
    cam->floortension = stage->floortension;
    cam->start_x = stage->camera_startx;
    cam->start_y = stage->camera_starty;
}

void mugen_camera_update(Mugen_Camera* cam, f32 p1x, f32 p2x, f32 p1y, f32 p2y, f32 half_screen_w)
{
    f32 mid_x = (p1x + p2x) * 0.5f;

    f32 left_edge = cam->x - half_screen_w;
    f32 right_edge = cam->x + half_screen_w;

    f32 target_x = cam->x;
    if (mid_x < left_edge + cam->tension)
        target_x = mid_x - cam->tension + half_screen_w;
    else if (mid_x > right_edge - cam->tension)
        target_x = mid_x + cam->tension - half_screen_w;

    cam->x = mel_clampf(target_x, cam->bound_left, cam->bound_right);

    f32 highest_y = mel_minf(p1y, p2y);

    f32 target_y = cam->y;
    if (highest_y < -cam->floortension)
        target_y = highest_y * cam->verticalfollow;

    cam->y = mel_clampf(target_y, cam->bound_high, cam->bound_low);
}
