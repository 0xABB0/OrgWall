#include <barcode/code128.h>

#include "paint.h"

#include <collection.array/array.h>
#include <string.h>

#define MEL__C128_CODE_C 99
#define MEL__C128_CODE_B 100
#define MEL__C128_START_B 104
#define MEL__C128_START_C 105
#define MEL__C128_STOP 106

static const char* const MEL__C128[107] = {
    "212222", "222122", "222221", "121223", "121322", "131222", "122213",
    "122312", "132212", "221213", "221312", "231212", "112232", "122132",
    "122231", "113222", "123122", "123221", "223211", "221132", "221231",
    "213212", "223112", "312131", "311222", "321122", "321221", "312212",
    "322112", "322211", "212123", "212321", "232121", "111323", "131123",
    "131321", "112313", "132113", "132311", "211313", "231113", "231311",
    "112133", "112331", "132131", "113123", "113321", "133121", "313121",
    "211331", "231131", "213113", "213311", "213131", "311123", "311321",
    "331121", "312113", "312311", "332111", "314111", "221411", "431111",
    "111224", "111422", "121124", "121421", "141122", "141221", "112214",
    "112412", "122114", "122411", "142112", "142211", "241211", "221114",
    "413111", "241112", "134111", "111242", "121142", "121241", "114212",
    "124112", "124211", "411212", "421112", "421211", "212141", "214121",
    "412121", "111143", "111341", "131141", "114113", "114311", "411113",
    "411311", "113141", "114131", "311141", "411131", "211412", "211214",
    "211232", "2331112",
};

static void mel__c128_paint(mel__painter* p, i32 value) {
    const char* pat = MEL__C128[value];
    for (i32 k = 0; pat[k] != '\0'; ++k) {
        mel__paint_run(p, pat[k] - '0', (k % 2) == 0);
    }
}

static i32 mel__c128_digit_run(const char* s, usize i, usize len) {
    i32 n = 0;
    while (i + (usize)n < len && s[i + n] >= '0' && s[i + n] <= '9') {
        n += 1;
    }
    return n;
}

bool mel_code128_encode(mel_barcode_matrix* out, const char* data, i32 height,
                        const Mel_Alloc* allocator) {
    if (data == NULL || height <= 0) {
        return false;
    }
    usize len = strlen(data);
    if (len == 0) {
        return false;
    }
    for (usize i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (c < 32 || c > 126) {
            return false;
        }
    }

    Mel_Array(i32) syms;
    mel_array_init(&syms, allocator);

    i32 lead = mel__c128_digit_run(data, 0, len);
    bool set_c = ((usize)lead == len && (len % 2) == 0) || lead >= 4;
    mel_array_push(&syms, set_c ? MEL__C128_START_C : MEL__C128_START_B);

    usize i = 0;
    while (i < len) {
        if (set_c) {
            if (i + 1 < len && data[i] >= '0' && data[i] <= '9' &&
                data[i + 1] >= '0' && data[i + 1] <= '9') {
                mel_array_push(&syms, (data[i] - '0') * 10 + (data[i + 1] - '0'));
                i += 2;
            } else {
                mel_array_push(&syms, MEL__C128_CODE_B);
                set_c = false;
            }
        } else {
            i32 run = mel__c128_digit_run(data, i, len);
            bool to_c = run >= 4 ||
                        (run >= 2 && i + (usize)run == len && (run % 2) == 0);
            if (to_c) {
                mel_array_push(&syms, MEL__C128_CODE_C);
                set_c = true;
            } else {
                mel_array_push(&syms, (i32)((unsigned char)data[i] - 32));
                i += 1;
            }
        }
    }

    i64 sum = syms.items[0];
    for (usize k = 1; k < syms.count; ++k) {
        sum += (i64)k * syms.items[k];
    }
    mel_array_push(&syms, (i32)(sum % 103));

    i32 width = 11 * (i32)syms.count + 13;
    if (!mel_barcode_matrix_init(out, width, height, allocator)) {
        mel_array_free(&syms);
        return false;
    }
    out->quiet_zone = 10;

    mel__painter p = {out, 0};
    for (usize k = 0; k < syms.count; ++k) {
        mel__c128_paint(&p, syms.items[k]);
    }
    mel__c128_paint(&p, MEL__C128_STOP);

    mel_array_free(&syms);
    return true;
}
