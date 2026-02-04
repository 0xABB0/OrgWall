#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <stdint.h>

int main(void)
{
    uint8_t pixels[64 * 64 * 4];

    for (int y = 0; y < 64; y++)
    {
        for (int x = 0; x < 64; x++)
        {
            int i = (y * 64 + x) * 4;
            int checker = ((x / 8) + (y / 8)) % 2;

            if (checker)
            {
                pixels[i + 0] = 255;
                pixels[i + 1] = 0;
                pixels[i + 2] = 255;
                pixels[i + 3] = 255;
            }
            else
            {
                pixels[i + 0] = 0;
                pixels[i + 1] = 255;
                pixels[i + 2] = 255;
                pixels[i + 3] = 255;
            }
        }
    }

    stbi_write_png("assets/test.png", 64, 64, 4, pixels, 64 * 4);
    return 0;
}
