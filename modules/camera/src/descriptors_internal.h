#pragma once

#include <camera/camera.h>

struct mel_camera_facing
{
    const char* name;
};

struct mel_camera_auth
{
    const char* name;
    bool        granted;
};
