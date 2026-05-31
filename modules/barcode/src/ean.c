#include <barcode/ean.h>

#include <string.h>

static const u8 mel__ean_l[10] = {
    0x0D, 0x19, 0x13, 0x3D, 0x23, 0x31, 0x2F, 0x3B, 0x37, 0x0B,
};

static const u8 mel__ean_g[10] = {
    0x27, 0x33, 0x1B, 0x21, 0x1D, 0x39, 0x05, 0x11, 0x09, 0x17,
};

static const u8 mel__ean_first_parity[10] = {
    0x00, 0x0B, 0x0D, 0x0E, 0x13, 0x19, 0x1C, 0x15, 0x16, 0x1A,
};

static bool mel__all_digits(const char* s, i32 n) {
    for (i32 i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return true;
}

static i32 mel__checkdigit(const char* digits, i32 n) {
    i32 sum = 0;
    for (i32 i = 0; i < n; ++i) {
        i32 d = digits[i] - '0';
        i32 weight = (((n - 1 - i) & 1) == 0) ? 3 : 1;
        sum += d * weight;
    }
    return (10 - (sum % 10)) % 10;
}

i32 mel_ean13_checkdigit(const char* digits12) {
    return mel__checkdigit(digits12, 12);
}

i32 mel_ean8_checkdigit(const char* digits7) {
    return mel__checkdigit(digits7, 7);
}

typedef struct mel__painter {
    mel_barcode_matrix* m;
    i32 x;
} mel__painter;

static void mel__paint_bits(mel__painter* p, u8 value, i32 bit_count) {
    for (i32 i = 0; i < bit_count; ++i) {
        bool dark = ((value >> (bit_count - 1 - i)) & 1) != 0;
        if (dark) {
            mel_barcode_matrix_fill_column(p->m, p->x, true);
        }
        p->x += 1;
    }
}

static bool mel__emit_ean13(mel_barcode_matrix* out, const char* d13, i32 height,
                            const Mel_Alloc* allocator) {
    if (!mel_barcode_matrix_init(out, 95, height, allocator)) {
        return false;
    }
    out->quiet_zone = 11;

    i32 first = d13[0] - '0';
    u8 parity = mel__ean_first_parity[first];

    mel__painter p = {out, 0};
    mel__paint_bits(&p, 0x05, 3);
    for (i32 i = 0; i < 6; ++i) {
        i32 digit = d13[1 + i] - '0';
        bool use_g = ((parity >> (5 - i)) & 1) != 0;
        mel__paint_bits(&p, use_g ? mel__ean_g[digit] : mel__ean_l[digit], 7);
    }
    mel__paint_bits(&p, 0x0A, 5);
    for (i32 i = 0; i < 6; ++i) {
        i32 digit = d13[7 + i] - '0';
        mel__paint_bits(&p, (u8)(mel__ean_l[digit] ^ 0x7F), 7);
    }
    mel__paint_bits(&p, 0x05, 3);
    return true;
}

bool mel_ean13_encode(mel_barcode_matrix* out, const char* digits, i32 height,
                      const Mel_Alloc* allocator) {
    if (digits == NULL || height <= 0) {
        return false;
    }
    usize len = strlen(digits);
    char d13[14];
    if (len == 12) {
        if (!mel__all_digits(digits, 12)) {
            return false;
        }
        memcpy(d13, digits, 12);
        d13[12] = (char)('0' + mel_ean13_checkdigit(digits));
        d13[13] = '\0';
    } else if (len == 13) {
        if (!mel__all_digits(digits, 13)) {
            return false;
        }
        if (digits[12] - '0' != mel_ean13_checkdigit(digits)) {
            return false;
        }
        memcpy(d13, digits, 14);
    } else {
        return false;
    }
    return mel__emit_ean13(out, d13, height, allocator);
}

bool mel_ean8_encode(mel_barcode_matrix* out, const char* digits, i32 height,
                     const Mel_Alloc* allocator) {
    if (digits == NULL || height <= 0) {
        return false;
    }
    usize len = strlen(digits);
    char d8[9];
    if (len == 7) {
        if (!mel__all_digits(digits, 7)) {
            return false;
        }
        memcpy(d8, digits, 7);
        d8[7] = (char)('0' + mel_ean8_checkdigit(digits));
        d8[8] = '\0';
    } else if (len == 8) {
        if (!mel__all_digits(digits, 8)) {
            return false;
        }
        if (digits[7] - '0' != mel_ean8_checkdigit(digits)) {
            return false;
        }
        memcpy(d8, digits, 9);
    } else {
        return false;
    }

    if (!mel_barcode_matrix_init(out, 67, height, allocator)) {
        return false;
    }
    out->quiet_zone = 7;

    mel__painter p = {out, 0};
    mel__paint_bits(&p, 0x05, 3);
    for (i32 i = 0; i < 4; ++i) {
        mel__paint_bits(&p, mel__ean_l[d8[i] - '0'], 7);
    }
    mel__paint_bits(&p, 0x0A, 5);
    for (i32 i = 0; i < 4; ++i) {
        mel__paint_bits(&p, (u8)(mel__ean_l[d8[4 + i] - '0'] ^ 0x7F), 7);
    }
    mel__paint_bits(&p, 0x05, 3);
    return true;
}

bool mel_upca_encode(mel_barcode_matrix* out, const char* digits, i32 height,
                     const Mel_Alloc* allocator) {
    if (digits == NULL || height <= 0) {
        return false;
    }
    usize len = strlen(digits);
    if (len != 11 && len != 12) {
        return false;
    }
    if (!mel__all_digits(digits, (i32)len)) {
        return false;
    }

    char d13[14];
    d13[0] = '0';
    memcpy(d13 + 1, digits, len);
    if (len == 11) {
        d13[12] = (char)('0' + mel_ean13_checkdigit(d13));
        d13[13] = '\0';
    } else {
        if (digits[11] - '0' != mel_ean13_checkdigit(d13)) {
            return false;
        }
        d13[13] = '\0';
    }

    if (!mel__emit_ean13(out, d13, height, allocator)) {
        return false;
    }
    out->quiet_zone = 9;
    return true;
}
