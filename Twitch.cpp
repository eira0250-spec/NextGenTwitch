#include "Skeleton.h"
#include <math.h>

// --- 超高速ノイズ生成器（本家の重い処理をカット！） ---
inline uint32_t FastHash(uint32_t seed) {
    seed ^= seed << 13; 
    seed ^= seed >> 17; 
    seed ^= seed << 5; 
    return seed;
}

static PF_Err About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    PF_SPRINTF(out_data->return_msg, "Next-Gen Twitch v1.0\nSuper Lightweight Edition");
    return PF_Err_NONE;
}

static PF_Err GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_I_DO_DIALOG;
    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    // Amount（強さ）スライダーの追加
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Amount", 0, 100, 0, 100, 50, PF_Precision_INTEGER, 0, 0, AMOUNT_DISK_ID);

    // Speed（頻度）スライダーの追加
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Speed", 0, 100, 0, 100, 20, PF_Precision_INTEGER, 0, 0, SPEED_DISK_ID);

    out_data->num_params = TWITCH_NUM_PARAMS;
    return err;
}

// --- レンダリング処理（ピクセルを直接操作して爆速化） ---
static PF_Err Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    PF_Err err = PF_Err_NONE;
    PF_EffectWorld *input = &params[TWITCH_INPUT]->u.ld;

    double amount = params[TWITCH_AMOUNT]->u.fs_d.value / 100.0;
    double speed = params[TWITCH_SPEED]->u.fs_d.value / 100.0;
    
    // 現在のフレーム時間からカオスなノイズ値を計算
    float time = (float)in_data->current_time / (float)in_data->time_step;
    uint32_t iTime = (uint32_t)(time * speed * 10.0f);
    float noise = (FastHash(iTime) % 1000) / 1000.0f;

    // 閾値を超えたら「痙攣（Twitch）」発動！
    int x_shift = 0;
    if (noise > (1.0f - amount) && amount > 0.0) {
        x_shift = (int)((noise - 0.5f) * 150.0f * amount); // 画面を左右にスライド
    }

    PF_Pixel *srcP, *dstP;
    for (int y = 0; y < output->height; ++y) {
        srcP = (PF_Pixel*)((char*)input->data + y * input->rowbytes);
        dstP = (PF_Pixel*)((char*)output->data + y * output->rowbytes);
        
        for (int x = 0; x < output->width; ++x) {
            int src_x = x + x_shift;
            
            if (src_x >= 0 && src_x < input->width) {
                PF_Pixel *sampleP = srcP + src_x;
                dstP->red = sampleP->red;
                
                // 【機能向上】スライド時に自動で色収差（RGBスプリット）を発生させる！
                int g_x = src_x + (x_shift != 0 ? (int)(x_shift * 0.2f) : 0);
                int b_x = src_x + (x_shift != 0 ? (int)(-x_shift * 0.2f) : 0);
                
                dstP->green = (g_x >= 0 && g_x < input->width) ? (srcP + g_x)->green : sampleP->green;
                dstP->blue = (b_x >= 0 && b_x < input->width) ? (srcP + b_x)->blue : sampleP->blue;
                dstP->alpha = sampleP->alpha;
            } else {
                dstP->red = dstP->green = dstP->blue = dstP->alpha = 0; // 画面外は黒
            }
            dstP++;
        }
    }
    return err;
}

// AEにこのプラグインの機能を登録する必須コード
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
