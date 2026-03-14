#include "Skeleton.h"
#include <math.h>

inline uint32_t FastHash(uint32_t seed) {
    seed ^= seed << 13; 
    seed ^= seed >> 17; 
    seed ^= seed << 5; 
    return seed;
}

static PF_Err About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    PF_SPRINTF(out_data->return_msg, "Next-Gen Twitch v1.1\nUltimate Glitch Edition");
    return PF_Err_NONE;
}

static PF_Err GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
    // エラーの原因だった「WIDE_TIME_INPUT」を削除し、IDカード（2000000）に完全に一致させました！
    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE;
    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Amount", 0, 100, 0, 100, 80, PF_Precision_INTEGER, 0, 0, AMOUNT_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Speed", 0, 100, 0, 100, 50, PF_Precision_INTEGER, 0, 0, SPEED_DISK_ID);

    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Blur", "On", 1, 0, EN_BLUR_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Color", "On", 1, 0, EN_COLOR_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Light", "On", 1, 0, EN_LIGHT_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Scale", "On", 1, 0, EN_SCALE_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Slide", "On", 1, 0, EN_SLIDE_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Glitch (Blocks)", "On", 1, 0, EN_GLITCH_DISK_ID);

    out_data->num_params = TWITCH_NUM_PARAMS;
    return err;
}

static PF_Err Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    PF_Err err = PF_Err_NONE;
    PF_EffectWorld *input = &params[TWITCH_INPUT]->u.ld;
    
    float amount = (float)(params[TWITCH_AMOUNT]->u.fs_d.value / 100.0);
    float speed  = (float)(params[TWITCH_SPEED]->u.fs_d.value / 100.0);
    
    bool en_blur   = params[TWITCH_EN_BLUR]->u.bd.value != 0;
    bool en_color  = params[TWITCH_EN_COLOR]->u.bd.value != 0;
    bool en_light  = params[TWITCH_EN_LIGHT]->u.bd.value != 0;
    bool en_scale  = params[TWITCH_EN_SCALE]->u.bd.value != 0;
    bool en_slide  = params[TWITCH_EN_SLIDE]->u.bd.value != 0;
    bool en_glitch = params[TWITCH_EN_GLITCH]->u.bd.value != 0;

    // フレーム単位で確実に切り替わるようにシードを修正！
    int frame = in_data->current_time / in_data->time_step;
    float trig = (FastHash(frame * 13) % 1000) / 1000.0f;

    // Speedの値（確率）に応じて痙攣をトリガー
    bool is_twitching = (trig < speed) && (amount > 0.0f);

    float n1 = (FastHash(frame * 27) % 1000) / 1000.0f;
    float n2 = (FastHash(frame * 31) % 1000) / 1000.0f;
    float n3 = (FastHash(frame * 47) % 1000) / 1000.0f;

    int x_shift = 0, y_shift = 0;
    float scale_x = 1.0f, scale_y = 1.0f;
    int flash_r = 0, flash_g = 0, flash_b = 0;
    int glitch_blocks = 0;
    int b_rad = 0;

    if (is_twitching) {
        if (en_slide) {
            x_shift = (int)((n1 - 0.5f) * 600.0f * amount);
            y_shift = (int)((n2 - 0.5f) * 100.0f * amount);
        }
        if (en_scale) {
            scale_x = 1.0f + (n1 * 1.5f * amount);
            scale_y = 1.0f + (n2 * 0.5f * amount);
        }
        if (en_light) {
            flash_r = (int)(n1 * 255.0f * amount);
            flash_g = (int)(n2 * 255.0f * amount);
            flash_b = (int)(n3 * 255.0f * amount);
        }
        if (en_glitch) {
            glitch_blocks = (int)(1.0f + n1 * 30.0f * amount);
        }
        if (en_blur) {
            b_rad = (int)(n2 * 60.0f * amount);
        }
    }

    int cx = output->width / 2;
    int cy = output->height / 2;
    int in_w = input->width;
    int in_h = input->height;

    for (int y = 0; y < output->height; ++y) {
        PF_Pixel *dstP = (PF_Pixel*)((char*)output->data + y * output->rowbytes);

        // Glitch: 行ごとにランダムな横ズレを発生させる
        int row_shift = 0;
        if (is_twitching && en_glitch && glitch_blocks > 0) {
            int block_h = in_h / glitch_blocks;
            if (block_h < 1) block_h = 1;
            int block_id = y / block_h;
            float block_n = (FastHash(frame + block_id * 99) % 1000) / 1000.0f;
            if (block_n > 0.6f) { // ブロックがズレる確率
                row_shift = (int)((block_n - 0.8f) * 500.0f * amount);
            }
        }

        for (int x = 0; x < output->width; ++x) {
            int s_x = cx + (int)((x - cx) / scale_x) + x_shift + row_shift;
            int s_y = cy + (int)((y - cy) / scale_y) + y_shift;

            // 画面外は端のピクセルを延長して埋める（黒帯が出ないようにする処理）
            int clamp_x = s_x < 0 ? 0 : (s_x >= in_w ? in_w - 1 : s_x);
            int clamp_y = s_y < 0 ? 0 : (s_y >= in_h ? in_h - 1 : s_y);

            PF_Pixel *srcP = (PF_Pixel*)((char*)input->data + clamp_y * input->rowbytes) + clamp_x;
            PF_Pixel c = *srcP;

            // Color (RGBハイパースプリット)
            if (is_twitching && en_color) {
                int r_x = clamp_x + (int)(x_shift * 0.8f) + row_shift + 15;
                int b_x = clamp_x - (int)(x_shift * 0.8f) - row_shift - 15;
                r_x = r_x < 0 ? 0 : (r_x >= in_w ? in_w - 1 : r_x);
                b_x = b_x < 0 ? 0 : (b_x >= in_w ? in_w - 1 : b_x);

                c.red = ((PF_Pixel*)((char*)input->data + clamp_y * input->rowbytes) + r_x)->red;
                c.blue = ((PF_Pixel*)((char*)input->data + clamp_y * input->rowbytes) + b_x)->blue;
            }

            // Blur (超高速な横方向モーションブラー)
            if (is_twitching && en_blur && b_rad > 0) {
                int sum_r = c.red, sum_g = c.green, sum_b = c.blue;
                int taps = 1;
                for(int tap = 1; tap <= 4; ++tap) {
                    int bx1 = clamp_x + tap * b_rad / 4;
                    bx1 = bx1 >= in_w ? in_w - 1 : bx1;
                    PF_Pixel *bp1 = (PF_Pixel*)((char*)input->data + clamp_y * input->rowbytes) + bx1;
                    sum_r += bp1->red; sum_g += bp1->green; sum_b += bp1->blue; taps++;

                    int bx2 = clamp_x - tap * b_rad / 4;
                    bx2 = bx2 < 0 ? 0 : bx2;
                    PF_Pixel *bp2 = (PF_Pixel*)((char*)input->data + clamp_y * input->rowbytes) + bx2;
                    sum_r += bp2->red; sum_g += bp2->green; sum_b += bp2->blue; taps++;
                }
                c.red = sum_r / taps; c.green = sum_g / taps; c.blue = sum_b / taps;
            }

            // Light (ネオンフラッシュ)
            if (is_twitching && en_light) {
                int r = c.red + flash_r; int g = c.green + flash_g; int b = c.blue + flash_b;
                c.red = r > 255 ? 255 : r; c.green = g > 255 ? 255 : g; c.blue = b > 255 ? 255 : b;
            }

            *dstP++ = c;
        }
    }
    return err;
}

extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output, void *extra) {
    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
            case PF_Cmd_ABOUT: err = About(in_data, out_data, params, output); break;
            case PF_Cmd_GLOBAL_SETUP: err = GlobalSetup(in_data, out_data, params, output); break;
            case PF_Cmd_PARAMS_SETUP: err = ParamsSetup(in_data, out_data, params, output); break;
            case PF_Cmd_RENDER: err = Render(in_data, out_data, params, output); break;
        }
    } catch (...) {}
    return err;
}
