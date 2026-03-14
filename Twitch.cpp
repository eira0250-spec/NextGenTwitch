#include "Skeleton.h"
#include <math.h>

inline uint32_t FastHash(uint32_t seed) {
    seed ^= seed << 13; 
    seed ^= seed >> 17; 
    seed ^= seed << 5; 
    return seed;
}

static PF_Err About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    PF_SPRINTF(out_data->return_msg, "Next-Gen Twitch v1.1\nFull 6-Operator Edition");
    return PF_Err_NONE;
}

static PF_Err GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
    // Time（時間軸ジャンプ）機能を使うための特別な許可証（WIDE_TIME_INPUT）を追加！
    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_WIDE_TIME_INPUT;
    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Amount", 0, 100, 0, 100, 50, PF_Precision_INTEGER, 0, 0, AMOUNT_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Speed", 0, 100, 0, 100, 20, PF_Precision_INTEGER, 0, 0, SPEED_DISK_ID);

    // 6つのスイッチ（デフォルトは全開！）
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Blur", "On", TRUE, 0, EN_BLUR_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Color", "On", TRUE, 0, EN_COLOR_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Light", "On", TRUE, 0, EN_LIGHT_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Scale", "On", TRUE, 0, EN_SCALE_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Slide", "On", TRUE, 0, EN_SLIDE_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Time", "On", TRUE, 0, EN_TIME_DISK_ID);

    out_data->num_params = TWITCH_NUM_PARAMS;
    return err;
}

static PF_Err Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    PF_Err err = PF_Err_NONE;
    
    double amount = params[TWITCH_AMOUNT]->u.fs_d.value / 100.0;
    double speed = params[TWITCH_SPEED]->u.fs_d.value / 100.0;
    
    bool en_blur  = params[TWITCH_EN_BLUR]->u.bd.value;
    bool en_color = params[TWITCH_EN_COLOR]->u.bd.value;
    bool en_light = params[TWITCH_EN_LIGHT]->u.bd.value;
    bool en_scale = params[TWITCH_EN_SCALE]->u.bd.value;
    bool en_slide = params[TWITCH_EN_SLIDE]->u.bd.value;
    bool en_time  = params[TWITCH_EN_TIME]->u.bd.value;

    float time = (float)in_data->current_time / (float)in_data->time_step;
    uint32_t iTime = (uint32_t)(time * speed * 10.0f);
    float noise = (FastHash(iTime) % 1000) / 1000.0f;
    float noise2 = (FastHash(iTime + 123) % 1000) / 1000.0f;

    bool is_twitching = (noise > (1.0f - amount) && amount > 0.0);
    
    // 1. TIME (時間軸ジャンプ処理)
    PF_EffectWorld *current_input = &params[TWITCH_INPUT]->u.ld;
    PF_ParamDef checkout_input;
    bool checked_out = false;
    
    if (is_twitching && en_time) {
        int time_offset = (int)((noise2 - 0.5f) * 20.0f * amount * in_data->time_step);
        int target_time = in_data->current_time + time_offset;
        if (target_time < 0) target_time = 0;
        
        // 指定した時間のフレームをAEシステムから引っ張ってくる
        if (PF_CHECKOUT_PARAM(in_data, TWITCH_INPUT, target_time, in_data->time_step, in_data->time_scale, &checkout_input) == PF_Err_NONE) {
            current_input = &checkout_input.u.ld;
            checked_out = true;
        }
    }

    // 各種計算
    int x_shift = 0, y_shift = 0, blur_radius = 0;
    float flash = 0.0f, scale = 1.0f;

    if (is_twitching) {
        if (en_slide) {
            x_shift = (int)((noise - 0.5f) * 200.0f * amount);
            y_shift = (int)((noise2 - 0.5f) * 200.0f * amount);
        }
        if (en_light) flash = (noise - 0.5f) * 300.0f * amount;
        if (en_scale) scale = 1.0f + (noise * 1.5f * amount); // 最大2.5倍拡大
        if (en_blur)  blur_radius = (int)(noise * 30.0f * amount); // ブラーの強さ
    }

    int cx = output->width / 2;
    int cy = output->height / 2;
    int in_w = current_input->width;
    int in_h = current_input->height;

    PF_Pixel *dstP;
    for (int y = 0; y < output->height; ++y) {
        dstP = (PF_Pixel*)((char*)output->data + y * output->rowbytes);
        
        for (int x = 0; x < output->width; ++x) {
            // 2. SCALE & SLIDE
            int s_x = cx + (int)((x - cx) / scale) + x_shift;
            int s_y = cy + (int)((y - cy) / scale) + y_shift;
            
            PF_Pixel c = {0, 0, 0, 0};
            
            if (s_x >= 0 && s_x < in_w && s_y >= 0 && s_y < in_h) {
                // 3. BLUR (超高速化サンプリング)
                if (blur_radius > 0) {
                    int r_sum = 0, g_sum = 0, b_sum = 0, a_sum = 0, count = 0;
                    int step = blur_radius / 3 + 1;
                    for (int by = -blur_radius; by <= blur_radius; by += step) {
                        for (int bx = -blur_radius; bx <= blur_radius; bx += step) {
                            int bs_x = s_x + bx, bs_y = s_y + by;
                            if (bs_x >= 0 && bs_x < in_w && bs_y >= 0 && bs_y < in_h) {
                                PF_Pixel *p = (PF_Pixel*)((char*)current_input->data + bs_y * current_input->rowbytes) + bs_x;
                                r_sum += p->red; g_sum += p->green; b_sum += p->blue; a_sum += p->alpha;
                                count++;
                            }
                        }
                    }
                    if (count > 0) { c.red = r_sum / count; c.green = g_sum / count; c.blue = b_sum / count; c.alpha = a_sum / count; }
                } else {
                    c = *((PF_Pixel*)((char*)current_input->data + s_y * current_input->rowbytes) + s_x);
                }
            }
            
            // 4. COLOR (RGBスプリット)
            if (is_twitching && en_color) {
                int g_x = s_x + (int)(x_shift * 0.4f) + 5, g_y = s_y + (int)(y_shift * 0.4f) + 5;
                int b_x = s_x - (int)(x_shift * 0.4f) - 5, b_y = s_y - (int)(y_shift * 0.4f) - 5;
                if (g_x >= 0 && g_x < in_w && g_y >= 0 && g_y < in_h) c.green = ((PF_Pixel*)((char*)current_input->data + g_y * current_input->rowbytes) + g_x)->green;
                if (b_x >= 0 && b_x < in_w && b_y >= 0 && b_y < in_h) c.blue = ((PF_Pixel*)((char*)current_input->data + b_y * current_input->rowbytes) + b_x)->blue;
            }
            
            // 5. LIGHT (フラッシュ)
            if (is_twitching && en_light) {
                int r = c.red + (int)flash, g = c.green + (int)flash, b = c.blue + (int)flash;
                c.red   = (r > 255) ? 255 : (r < 0 ? 0 : r);
                c.green = (g > 255) ? 255 : (g < 0 ? 0 : g);
                c.blue  = (b > 255) ? 255 : (b < 0 ? 0 : b);
            }
            
            dstP->red = c.red; dstP->green = c.green; dstP->blue = c.blue; dstP->alpha = c.alpha;
            dstP++;
        }
    }
    
    if (checked_out) PF_CHECKIN_PARAM(in_data, &checkout_input);
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
