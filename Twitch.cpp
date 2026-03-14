#include "Skeleton.h"
#include <math.h>

// --- 本格的な1Dパーリンノイズ（本家の滑らかで鋭いTwitchを再現） ---
inline float Hash1D(int n) {
    n = (n << 13) ^ n;
    return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
}

inline float ValueNoise(float x) {
    int i = (int)floor(x);
    float f = x - floor(x);
    float u = f * f * (3.0f - 2.0f * f); // Smoothstep
    return Hash1D(i) * (1.0f - u) + Hash1D(i + 1) * u;
}

// 画面外のピクセルを参照した時にエラーを出さないための安全装置
inline PF_Pixel GetPixel(PF_EffectWorld* world, int x, int y) {
    if (x < 0) x = 0; else if (x >= world->width) x = world->width - 1;
    if (y < 0) y = 0; else if (y >= world->height) y = world->height - 1;
    return *((PF_Pixel*)((char*)world->data + y * world->rowbytes) + x);
}

static PF_Err About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    PF_SPRINTF(out_data->return_msg, "Next-Gen Twitch v1.1\nUltimate Full Edition");
    return PF_Err_NONE;
}

static PF_Err GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
    // 【最重要修正】PF_OutFlag_NON_PARAM_VARY を付与！
    // これにより「キーフレームがなくても、毎フレーム必ずアニメーションを再計算しろ！」とAEに強制します！
    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_NON_PARAM_VARY;
    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Amount", 0, 100, 0, 100, 50, PF_Precision_INTEGER, 0, 0, AMOUNT_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Speed", 0, 100, 0, 100, 20, PF_Precision_INTEGER, 0, 0, SPEED_DISK_ID);

    // 独立した6つのスイッチ
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Blur", "On", 1, 0, EN_BLUR_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Color", "On", 1, 0, EN_COLOR_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Light", "On", 1, 0, EN_LIGHT_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Scale", "On", 1, 0, EN_SCALE_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Slide", "On", 1, 0, EN_SLIDE_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Time", "On", 1, 0, EN_TIME_DISK_ID);

    out_data->num_params = TWITCH_NUM_PARAMS;
    return err;
}

static PF_Err Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    PF_Err err = PF_Err_NONE;
    
    float amount = (float)(params[TWITCH_AMOUNT]->u.fs_d.value / 100.0);
    float speed  = (float)(params[TWITCH_SPEED]->u.fs_d.value / 100.0);
    
    // スイッチは完全に分離！1つ切っても他には一切影響しません。
    bool en_blur  = params[TWITCH_EN_BLUR]->u.bd.value != 0;
    bool en_color = params[TWITCH_EN_COLOR]->u.bd.value != 0;
    bool en_light = params[TWITCH_EN_LIGHT]->u.bd.value != 0;
    bool en_scale = params[TWITCH_EN_SCALE]->u.bd.value != 0;
    bool en_slide = params[TWITCH_EN_SLIDE]->u.bd.value != 0;
    bool en_time  = params[TWITCH_EN_TIME]->u.bd.value != 0;

    // 現在のフレーム（時間）を取得
    float t = (float)in_data->current_time / (float)in_data->time_step;

    // Twitch波形の生成（本家のような突発的な波形）
    float noise = fabs(ValueNoise(t * speed * 2.0f)); 
    float threshold = 1.0f - amount;
    float twitch = 0.0f;
    
    // Amountの値を超えた時だけ、鋭く痙攣（0.0〜1.0）
    if (noise > threshold && amount > 0.001f) {
        twitch = (noise - threshold) / (1.0f - threshold);
        twitch = pow(twitch, 0.5f); // 波形を鋭くする
    }

    // 各機能ごとにランダムなベクトル（方向や強さ）を生成
    float rnd_slide_x = ValueNoise(t * 12.3f + 100.0f);
    float rnd_slide_y = ValueNoise(t * 15.7f + 200.0f);
    float rnd_scale   = ValueNoise(t * 10.1f + 300.0f);
    float rnd_light   = fabs(ValueNoise(t * 18.5f + 400.0f));
    float rnd_time    = ValueNoise(t * 11.2f + 500.0f);

    // 各オペレーターの強度を完全に独立して計算
    float slide_val_x = en_slide ? twitch * rnd_slide_x * 200.0f * amount : 0.0f;
    float slide_val_y = en_slide ? twitch * rnd_slide_y * 100.0f * amount : 0.0f;
    float scale_val   = en_scale ? 1.0f + (twitch * rnd_scale * 0.5f * amount) : 1.0f;
    int light_val     = en_light ? (int)(twitch * rnd_light * 255.0f * amount) : 0;
    int blur_rad      = en_blur  ? (int)(twitch * 40.0f * amount) : 0;
    int color_spread  = en_color ? (int)(twitch * 40.0f * amount) : 0;

    // TIME機能の実装（過去や未来のフレームを引っ張ってくる）
    PF_EffectWorld* input_layer = &params[TWITCH_INPUT]->u.ld;
    PF_ParamDef checkout_param;
    bool time_checked_out = false;

    if (en_time && twitch > 0.0f) {
        int offset_frames = (int)(twitch * rnd_time * 10.0f); 
        int target_time = in_data->current_time + (offset_frames * in_data->time_step);
        if (target_time < 0) target_time = 0;
        
        if (PF_CHECKOUT_PARAM(in_data, TWITCH_INPUT, target_time, in_data->time_step, in_data->time_scale, &checkout_param) == PF_Err_NONE) {
            input_layer = &checkout_param.u.ld;
            time_checked_out = true;
        }
    }

    int cx = output->width / 2;
    int cy = output->height / 2;

    // 全ピクセルの処理ループ
    for (int y = 0; y < output->height; ++y) {
        for (int x = 0; x < output->width; ++x) {
            
            // 1. Scale と Slide による座標計算
            float src_x = cx + (x - cx) / scale_val + slide_val_x;
            float src_y = cy + (y - cy) / scale_val + slide_val_y;

            PF_Pixel result = {0,0,0,0};

            // 2. Blur と Color の適用（本家ばりの強烈なサンプリング）
            if (blur_rad > 0 && en_blur) {
                int r = 0, g = 0, b = 0, a = 0, count = 0;
                int step = (blur_rad / 4) + 1; // 軽量化しつつ激しいブラー
                for (int bx = -blur_rad; bx <= blur_rad; bx += step) {
                    for (int by = -blur_rad; by <= blur_rad; by += step) {
                        // ブラーの中でさらにColor(RGBスプリット)を計算！
                        int r_x = (int)src_x + bx + color_spread;
                        int g_x = (int)src_x + bx;
                        int b_x = (int)src_x + bx - color_spread;
                        int py  = (int)src_y + by;

                        r += GetPixel(input_layer, r_x, py).red;
                        g += GetPixel(input_layer, g_x, py).green;
                        b += GetPixel(input_layer, b_x, py).blue;
                        a += GetPixel(input_layer, g_x, py).alpha;
                        count++;
                    }
                }
                result.red = r/count; result.green = g/count; result.blue = b/count; result.alpha = a/count;
            } else {
                // Blurなし、Color（RGBスプリット）のみ
                int r_x = (int)src_x + color_spread;
                int g_x = (int)src_x;
                int b_x = (int)src_x - color_spread;
                int py  = (int)src_y;
                
                result.red   = GetPixel(input_layer, r_x, py).red;
                result.green = GetPixel(input_layer, g_x, py).green;
                result.blue  = GetPixel(input_layer, b_x, py).blue;
                result.alpha = GetPixel(input_layer, g_x, py).alpha;
            }

            // 3. Light（フラッシュ）の適用
            if (en_light && light_val > 0) {
                int lr = result.red + light_val;
                int lg = result.green + light_val;
                int lb = result.blue + light_val;
                result.red   = lr > 255 ? 255 : lr;
                result.green = lg > 255 ? 255 : lg;
                result.blue  = lb > 255 ? 255 : lb;
            }

            // 出力へ書き込み
            *((PF_Pixel*)((char*)output->data + y * output->rowbytes) + x) = result;
        }
    }
    
    if (time_checked_out) PF_CHECKIN_PARAM(in_data, &checkout_param);
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
