#include <barcode/code39.h>

#include "paint.h"

#include <string.h>

static const char MEL__C39_SET[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%";

static const char* const MEL__C39_PAT[43] = {
    "000110100", "100100001", "001100001", "101100000", "000110001",
    "100110000", "001110000", "000100101", "100100100", "001100100",
    "100001001", "001001001", "101001000", "000011001", "100011000",
    "001011000", "000001101", "100001100", "001001100", "000011100",
    "100000011", "001000011", "101000010", "000010011", "100010010",
    "001010010", "000000111", "100000110", "001000110", "000010110",
    "110000001", "011000001", "111000000", "010010001", "110010000",
    "011010000", "010000101", "110000100", "011000100", "010101000",
    "010100010", "010001010", "000101010",
};

static const char* const MEL__C39_STAR = "010010100";

static i32 mel__c39_index(char c) {
    if (c == '\0') {
        return -1;
    }
    const char* p = strchr(MEL__C39_SET, c);
    return p == NULL ? -1 : (i32)(p - MEL__C39_SET);
}

static void mel__c39_glyph(mel__painter* p, const char* pat) {
    for (i32 k = 0; k < 9; ++k) {
        i32 w = (pat[k] == '1') ? 3 : 1;
        mel__paint_run(p, w, (k % 2) == 0);
    }
}

bool mel_code39_encode(mel_barcode_matrix* out, const char* data, i32 height,
                       mel_code39_opt opt, const Mel_Alloc* allocator) {
    if (data == NULL || height <= 0) {
        return false;
    }
    usize len = strlen(data);
    if (len == 0) {
        return false;
    }
    for (usize i = 0; i < len; ++i) {
        if (mel__c39_index(data[i]) < 0) {
            return false;
        }
    }

    i32 check = -1;
    if (opt.checksum) {
        i32 sum = 0;
        for (usize i = 0; i < len; ++i) {
            sum += mel__c39_index(data[i]);
        }
        check = sum % 43;
    }

    i32 glyphs = (i32)len + 2 + (opt.checksum ? 1 : 0);
    i32 width = 15 * glyphs + (glyphs - 1);
    if (!mel_barcode_matrix_init(out, width, height, allocator)) {
        return false;
    }
    out->quiet_zone = 10;

    mel__painter p = {out, 0};
    mel__c39_glyph(&p, MEL__C39_STAR);
    mel__paint_run(&p, 1, false);
    for (usize i = 0; i < len; ++i) {
        mel__c39_glyph(&p, MEL__C39_PAT[mel__c39_index(data[i])]);
        mel__paint_run(&p, 1, false);
    }
    if (opt.checksum) {
        mel__c39_glyph(&p, MEL__C39_PAT[check]);
        mel__paint_run(&p, 1, false);
    }
    mel__c39_glyph(&p, MEL__C39_STAR);
    return true;
}
