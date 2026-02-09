#pragma once

#include "types.h"

#define mel_sort_reverse(arr, count) \
    do { \
        usize mel__rev_n = (count); \
        for (usize mel__rev_i = 0; mel__rev_i < mel__rev_n / 2; mel__rev_i++) { \
            usize mel__rev_j = mel__rev_n - 1 - mel__rev_i; \
            __auto_type mel__rev_tmp = (arr)[mel__rev_i]; \
            (arr)[mel__rev_i] = (arr)[mel__rev_j]; \
            (arr)[mel__rev_j] = mel__rev_tmp; \
        } \
    } while (0)

#define mel_sort_is_sorted(arr, count, lt_expr) \
    mel__sort_is_sorted_impl(arr, count, lt_expr, __COUNTER__)

#define mel__sort_is_sorted_impl(arr, count, lt_expr, uid) \
    mel__sort_is_sorted_expand(arr, count, lt_expr, uid)

#define mel__sort_is_sorted_expand(arr, count, lt_expr, uid) \
    ({ \
        __auto_type mel__iss_arr_##uid = (arr); \
        usize mel__iss_n_##uid = (count); \
        bool mel__iss_result_##uid = true; \
        for (usize mel__iss_i_##uid = 1; mel__iss_i_##uid < mel__iss_n_##uid; mel__iss_i_##uid++) { \
            __auto_type a = mel__iss_arr_##uid[mel__iss_i_##uid]; \
            __auto_type b = mel__iss_arr_##uid[mel__iss_i_##uid - 1]; \
            if (lt_expr) { \
                mel__iss_result_##uid = false; \
                break; \
            } \
        } \
        mel__iss_result_##uid; \
    })

#define mel_sort_insert(arr, count, lt_expr) \
    do { \
        __auto_type mel__ins_arr = (arr); \
        usize mel__ins_n = (count); \
        for (usize mel__ins_i = 1; mel__ins_i < mel__ins_n; mel__ins_i++) { \
            __auto_type mel__ins_key = mel__ins_arr[mel__ins_i]; \
            usize mel__ins_j = mel__ins_i; \
            while (mel__ins_j > 0) { \
                __auto_type a = mel__ins_key; \
                __auto_type b = mel__ins_arr[mel__ins_j - 1]; \
                if (!(lt_expr)) break; \
                mel__ins_arr[mel__ins_j] = mel__ins_arr[mel__ins_j - 1]; \
                mel__ins_j--; \
            } \
            mel__ins_arr[mel__ins_j] = mel__ins_key; \
        } \
    } while (0)

#define mel__sort_sift_down(arr, n, root, lt_expr) \
    do { \
        usize mel__sd_root = (root); \
        usize mel__sd_n = (n); \
        while (2 * mel__sd_root + 1 < mel__sd_n) { \
            usize mel__sd_child = 2 * mel__sd_root + 1; \
            if (mel__sd_child + 1 < mel__sd_n) { \
                __auto_type a = (arr)[mel__sd_child]; \
                __auto_type b = (arr)[mel__sd_child + 1]; \
                if (lt_expr) mel__sd_child++; \
            } \
            { \
                __auto_type a = (arr)[mel__sd_root]; \
                __auto_type b = (arr)[mel__sd_child]; \
                if (lt_expr) { \
                    __auto_type mel__sd_tmp = (arr)[mel__sd_root]; \
                    (arr)[mel__sd_root] = (arr)[mel__sd_child]; \
                    (arr)[mel__sd_child] = mel__sd_tmp; \
                    mel__sd_root = mel__sd_child; \
                } else { \
                    break; \
                } \
            } \
        } \
    } while (0)

#define mel_sort_heap(arr, count, lt_expr) \
    do { \
        __auto_type mel__hs_arr = (arr); \
        usize mel__hs_n = (count); \
        if (mel__hs_n < 2) break; \
        for (usize mel__hs_i = mel__hs_n / 2; mel__hs_i > 0; mel__hs_i--) { \
            mel__sort_sift_down(mel__hs_arr, mel__hs_n, mel__hs_i - 1, lt_expr); \
        } \
        for (usize mel__hs_end = mel__hs_n - 1; mel__hs_end > 0; mel__hs_end--) { \
            __auto_type mel__hs_tmp = mel__hs_arr[0]; \
            mel__hs_arr[0] = mel__hs_arr[mel__hs_end]; \
            mel__hs_arr[mel__hs_end] = mel__hs_tmp; \
            mel__sort_sift_down(mel__hs_arr, mel__hs_end, 0, lt_expr); \
        } \
    } while (0)

#define mel__sort_log2(n, out) \
    do { \
        usize mel__l2_v = (n); \
        (out) = 0; \
        while (mel__l2_v > 1) { mel__l2_v >>= 1; (out)++; } \
    } while (0)

#define mel__sort_med3(arr, i, j, k, lt_expr, out) \
    do { \
        usize mel__m3_a = (i), mel__m3_b = (j), mel__m3_c = (k); \
        bool mel__m3_ab, mel__m3_bc; \
        { __auto_type a = (arr)[mel__m3_a]; __auto_type b = (arr)[mel__m3_b]; mel__m3_ab = (lt_expr); } \
        { __auto_type a = (arr)[mel__m3_b]; __auto_type b = (arr)[mel__m3_c]; mel__m3_bc = (lt_expr); } \
        if (mel__m3_ab) { \
            if (mel__m3_bc) { (out) = mel__m3_b; } \
            else { \
                bool mel__m3_ac; \
                { __auto_type a = (arr)[mel__m3_a]; __auto_type b = (arr)[mel__m3_c]; mel__m3_ac = (lt_expr); } \
                (out) = mel__m3_ac ? mel__m3_c : mel__m3_a; \
            } \
        } else { \
            if (!mel__m3_bc) { (out) = mel__m3_b; } \
            else { \
                bool mel__m3_ac; \
                { __auto_type a = (arr)[mel__m3_a]; __auto_type b = (arr)[mel__m3_c]; mel__m3_ac = (lt_expr); } \
                (out) = mel__m3_ac ? mel__m3_a : mel__m3_c; \
            } \
        } \
    } while (0)

#define mel_sort(arr, count, lt_expr) \
    do { \
        __auto_type mel__is_arr = (arr); \
        usize mel__is_n = (count); \
        if (mel__is_n < 2) break; \
        usize mel__is_depth_limit; \
        mel__sort_log2(mel__is_n, mel__is_depth_limit); \
        mel__is_depth_limit *= 2; \
        struct { usize lo; usize hi; usize depth; } mel__is_stack[128]; \
        usize mel__is_sp = 0; \
        mel__is_stack[mel__is_sp++] = (typeof(mel__is_stack[0])){0, mel__is_n - 1, 0}; \
        while (mel__is_sp > 0) { \
            usize mel__is_lo = mel__is_stack[mel__is_sp - 1].lo; \
            usize mel__is_hi = mel__is_stack[mel__is_sp - 1].hi; \
            usize mel__is_depth = mel__is_stack[mel__is_sp - 1].depth; \
            mel__is_sp--; \
            usize mel__is_len = mel__is_hi - mel__is_lo + 1; \
            if (mel__is_len <= 1) continue; \
            if (mel__is_len <= 16) { \
                for (usize mel__is_ii = mel__is_lo + 1; mel__is_ii <= mel__is_hi; mel__is_ii++) { \
                    __auto_type mel__is_key = mel__is_arr[mel__is_ii]; \
                    usize mel__is_jj = mel__is_ii; \
                    while (mel__is_jj > mel__is_lo) { \
                        __auto_type a = mel__is_key; \
                        __auto_type b = mel__is_arr[mel__is_jj - 1]; \
                        if (!(lt_expr)) break; \
                        mel__is_arr[mel__is_jj] = mel__is_arr[mel__is_jj - 1]; \
                        mel__is_jj--; \
                    } \
                    mel__is_arr[mel__is_jj] = mel__is_key; \
                } \
                continue; \
            } \
            if (mel__is_depth >= mel__is_depth_limit) { \
                usize mel__is_hs_n = mel__is_len; \
                __auto_type mel__is_hs_base = mel__is_arr + mel__is_lo; \
                for (usize mel__is_hs_i = mel__is_hs_n / 2; mel__is_hs_i > 0; mel__is_hs_i--) { \
                    mel__sort_sift_down(mel__is_hs_base, mel__is_hs_n, mel__is_hs_i - 1, lt_expr); \
                } \
                for (usize mel__is_hs_end = mel__is_hs_n - 1; mel__is_hs_end > 0; mel__is_hs_end--) { \
                    __auto_type mel__is_hs_tmp = mel__is_hs_base[0]; \
                    mel__is_hs_base[0] = mel__is_hs_base[mel__is_hs_end]; \
                    mel__is_hs_base[mel__is_hs_end] = mel__is_hs_tmp; \
                    mel__sort_sift_down(mel__is_hs_base, mel__is_hs_end, 0, lt_expr); \
                } \
                continue; \
            } \
            usize mel__is_pivot_idx; \
            mel__sort_med3(mel__is_arr, mel__is_lo, mel__is_lo + mel__is_len / 2, mel__is_hi, lt_expr, mel__is_pivot_idx); \
            { \
                __auto_type mel__is_tmp = mel__is_arr[mel__is_pivot_idx]; \
                mel__is_arr[mel__is_pivot_idx] = mel__is_arr[mel__is_hi]; \
                mel__is_arr[mel__is_hi] = mel__is_tmp; \
            } \
            usize mel__is_store = mel__is_lo; \
            for (usize mel__is_k = mel__is_lo; mel__is_k < mel__is_hi; mel__is_k++) { \
                __auto_type a = mel__is_arr[mel__is_k]; \
                __auto_type b = mel__is_arr[mel__is_hi]; \
                if (lt_expr) { \
                    __auto_type mel__is_tmp2 = mel__is_arr[mel__is_k]; \
                    mel__is_arr[mel__is_k] = mel__is_arr[mel__is_store]; \
                    mel__is_arr[mel__is_store] = mel__is_tmp2; \
                    mel__is_store++; \
                } \
            } \
            { \
                __auto_type mel__is_tmp3 = mel__is_arr[mel__is_store]; \
                mel__is_arr[mel__is_store] = mel__is_arr[mel__is_hi]; \
                mel__is_arr[mel__is_hi] = mel__is_tmp3; \
            } \
            if (mel__is_store > 0 && mel__is_store - 1 > mel__is_lo) { \
                mel__is_stack[mel__is_sp++] = (typeof(mel__is_stack[0])){mel__is_lo, mel__is_store - 1, mel__is_depth + 1}; \
            } \
            if (mel__is_store + 1 < mel__is_hi) { \
                mel__is_stack[mel__is_sp++] = (typeof(mel__is_stack[0])){mel__is_store + 1, mel__is_hi, mel__is_depth + 1}; \
            } \
        } \
    } while (0)
