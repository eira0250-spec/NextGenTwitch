#include "Skeleton.h"
#include <math.h>

// =========================================================================
// 1. Math & Noise Engine (厳密なfloat計算でC4244警告を完全に排除)
// =========================================================================
inline float Hash1D(int n) {
    n = (n << 13) ^ n;
    // 整数からfloatへの変換を明示的に行い、警告を防ぐ
    return (1.0f - static_cast<float>((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
}

inline float ValueNoise(float x) {
    int i = static_cast<int>(floorf(x));
    float f = x - floorf(x);
    float u = f * f * (3.0f - 2.0f * f); // Smoothstep補間
    return Hash1D(i) * (1.0f - u) + Hash1D(i + 1) * u;
}

// =========================================================================
// 2. High-Quality Bilinear Sampler (高品質サブピクセル補間)
// =========================================================================
// 座標が小数点以下になった場合でも、ジャギーを出さずに滑らかにピクセルを取得するプロ仕様の関数
inline PF_Pixel SampleBilinear(PF_EffectWorld* world, float x, float y) {
    int w = world->width;
    int h = world->height;

    // 画面外の参照を安全にクランプ（端のピクセルを延長）
    if (x < 0.0f) x = 0.0f;
    if (x >= static_cast<float>(w - 1)) x = static_cast<float>(w - 1) - 0.001f;
    if (y < 0.0f) y = 0.0f;
    if (y >= static_cast<float>(h - 1)) y = static_cast<float>(h - 1) - 0.001f;

    int ix = static_cast<int>(x);
    int iy = static_cast<int>(y);
    float fx = x - static_cast<float>(ix);
    float fy = y - static_cast<float>(iy);

    // 周囲4ピクセルの取得
    PF_Pixel* row0 = reinterpret_cast<PF_Pixel*>(reinterpret_cast<char*>(world->data) + iy * world->rowbytes);
    PF_Pixel* row1 = reinterpret_cast<PF_Pixel*>(reinterpret_cast<char*>(world->data) + (iy + 1) * world->rowbytes);

    PF_Pixel p00 = *(row0 + ix);
    PF_Pixel p10 = *(row0 + ix + 1);
    PF_Pixel p01 = *(row1 + ix);
    PF_Pixel p11 = *(row1 + ix + 1);

    // 4点の重み付けブレンド
    PF_Pixel out;
    out.red   = static_cast<A_u_char>(p00.red * (1.0f - fx) * (1.0f - fy) + p10.red * fx * (1.0f - fy) + p01.red * (1.0f - fx) * fy + p11.red * fx * fy);
    out.green = static_cast<A_u_char>(p00.green * (1.0f - fx) * (1.0f - fy) + p10.green * fx * (1.0f - fy) + p01.green * (1.0f - fx) * fy + p11.green * fx * fy);
    out.blue  = static_cast<A_u_char>(p00.blue * (1.0f - fx) * (1.0f - fy) + p10.blue * fx * (1.0f - fy) + p01.blue * (1.0f - fx) * fy + p11.blue * fx * fy);
    out.alpha = static_cast<A_u_char>(p00.alpha * (1.0f - fx) * (1.0f - fy) + p10.alpha * fx * (1.0f - fy) + p01.alpha * (1.0f - fx) * fy + p11.alpha * fx * fy);

    return out;
}

// =========================================================================
// 3. Plugin Setup & Flags
// =========================================================================
static PF_Err About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    PF_SPRINTF(out_data->return_msg, "Next-Gen Twitch v1.1\nProfessional Studio Edition");
    return PF_Err_NONE;
}

static PF_Err GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
    // PF_OutFlag_NON_PARAM_VARY: キーフレームがなくても毎フレーム計算を強制する最強のフラグ
    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_WIDE_TIME_INPUT | PF_OutFlag_NON_PARAM_VARY;
    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Amount", 0, 100, 0, 100, 50, PF_Precision_INTEGER, 0, 0, AMOUNT_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Speed", 0, 100, 0, 100, 20, PF_Precision_INTEGER, 0, 0, SPEED_DISK_ID);

    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Blur", "On", 1, 0, EN_BLUR_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Color", "On", 1, 0, EN_COLOR_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Light", "On", 1, 0, EN_LIGHT_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Scale", "On", 1, 0, EN_SCALE_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Slide", "On", 1, 0, EN_SLIDE_DISK_ID);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Enable Time", "On", 1, 0, EN_TIME_DISK_ID);

    out_data->num_params = TWITCH_NUM_PARAMS;
    return err;
}

// =========================================================================
// 4. Core Render Pipeline (完全独立の6レイヤー処理エンジン)
// =========================================================================
static PF_Err Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output) {
    PF_Err err = PF_Err_NONE;
    
    // パラメータ取得時の警告を防ぐ厳密なキャスト
    float amount = static_cast<float>(params[TWITCH_AMOUNT]->u.fs_d.value) / 100.0f;
    float speed  = static_cast<float>(params[TWITCH_SPEED]->u.fs_d.value) / 100.0f;
    
    bool en_blur  = params[TWITCH_EN_BLUR]->u.bd.value != 0;
    bool en_color = params[TWITCH_EN_COLOR]->u.bd.value != 0;
    bool en_light = params[TWITCH_EN_LIGHT]->u.bd.value != 0;
    bool en_scale = params[TWITCH_EN_SCALE]->u.bd.value != 0;
    bool en_slide = params[TWITCH_EN_SLIDE]->u.bd.value != 0;
    bool en_time  = params[TWITCH_EN_TIME]->u.bd.value != 0;

    // 時間ベースのマスター・ノイズ生成
    float t = static_cast<float>(in_data->current_time) / static_cast<float>(in_data->time_step);
    float noise = fabsf(ValueNoise(t * speed * 2.0f)); 
    float threshold = 1.0f - amount;
    
    float twitch_val = 0.0f;
    if (noise > threshold && amount > 0.001f) {
        twitch_val = (noise - threshold) / (1.0f - threshold);
        twitch_val = powf(twitch_val, 0.5f); // 鋭い波形に変換
    }

    // 各オペレーターごとのランダム値を独立生成
    float rnd_slide_x = ValueNoise(t * 12.3f + 100.0f);
    float rnd_slide_y = ValueNoise(t * 15.7f + 200.0f);
    float rnd_scale   = ValueNoise(t * 10.1f + 300.0f);
    float rnd_light   = fabsf(ValueNoise(t * 18.5f + 400.0f));
    float rnd_time    = ValueNoise(t * 11.2f + 500.0f);

    // 各エフェクトの強度を計算
    float slide_x    = en_slide ? (rnd_slide_x - 0.5f) * 300.0f * twitch_val * amount : 0.0f;
    float slide_y    = en_slide ? (rnd_slide_y - 0.5f) * 150.0f * twitch_val * amount : 0.0f;
    float scale      = en_scale ? 1.0f + (rnd_scale * 1.5f * twitch_val * amount) : 1.0f;
    float color_dist = en_color ? twitch_val * 50.0f * amount : 0.0f;
    int blur_rad     = en_blur  ? static_cast<int>(twitch_val * 40.0f * amount) : 0;
    int flash        = en_light ? static_cast<int>(twitch_val * rnd_light * 255.0f * amount) : 0;

    // Timeオペレーター: 過去や未来のフレームをシステムから引っ張り出す
    PF_EffectWorld* input_layer = &params[TWITCH_INPUT]->u.ld;
    PF_ParamDef checkout_param;
    bool time_checked_out = false;

    if (en_time && twitch_val > 0.0f) {
        int offset_frames = static_cast<int>((rnd_time - 0.5f) * 20.0f * amount); 
        int target_time = in_data->current_time + (offset_frames * in_data->time_step);
        if (target_time < 0) target_time = 0;
        
        if (PF_CHECKOUT_PARAM(in_data, TWITCH_INPUT, target_time, in_data->time_step, in_data->time_scale, &checkout_param) == PF_Err_NONE) {
            input_layer = &checkout_param.u.ld;
            time_checked_out = true;
        }
    }

    float cx = static_cast<float>(output->width) / 2.0f;
    float cy = static_cast<float>(output->height) / 2.0f;

    // =====================================================================
    // メイン・ピクセルループ (完全なパイプライン構造)
    // =====================================================================
    for (int y = 0; y < output->height; ++y) {
        PF_Pixel* dstP = reinterpret_cast<PF_Pixel*>(reinterpret_cast<char*>(output->data) + y * output->rowbytes);
        
        for (int x = 0; x < output->width; ++x) {
            
            // 1. 座標変形 (Scale & Slide)
            float px = cx + (static_cast<float>(x) - cx) / scale + slide_x;
            float py = cy + (static_cast<float>(y) - cy) / scale + slide_y;

            int r = 0, g = 0, b = 0, a = 0;

            // 2. ピクセル取得 (Blur & Color の統合処理)
            if (blur_rad > 0) {
                int count = 0;
                int step = (blur_rad / 3) + 1; // 軽量化のための間引き
                
                // ブラーの範囲内で、Colorスプリットも同時に計算する最強のループ
                for (int by = -blur_rad; by <= blur_rad; by += step) {
                    for (int bx = -blur_rad; bx <= blur_rad; bx += step) {
                        PF_Pixel smp_r = SampleBilinear(input_layer, px + static_cast<float>(bx) + color_dist, py + static_cast<float>(by));
                        PF_Pixel smp_g = SampleBilinear(input_layer, px + static_cast<float>(bx),              py + static_cast<float>(by));
                        PF_Pixel smp_b = SampleBilinear(input_layer, px + static_cast<float>(bx) - color_dist, py + static_cast<float>(by));

                        r += smp_r.red;
                        g += smp_g.green;
                        b += smp_b.blue;
                        a += smp_g.alpha;
                        count++;
                    }
                }
                r /= count; g /= count; b /= count; a /= count;
            } else {
                // Blurなし、Colorスプリットのみ（または両方なし）
                PF_Pixel smp_r = SampleBilinear(input_layer, px + color_dist, py);
                PF_Pixel smp_g = SampleBilinear(input_layer, px,              py);
                PF_Pixel smp_b = SampleBilinear(input_layer, px - color_dist, py);
                
                r = smp_r.red;
                g = smp_g.green;
                b = smp_b.blue;
                a = smp_g.alpha;
            }

            // 3. 露出加算 (Light)
            if (flash > 0) {
                r += flash; 
                g += flash; 
                b += flash;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
            }

            // 最終出力への書き込み
            dstP->red   = static_cast<A_u_char>(r);
            dstP->green = static_cast<A_u_char>(g);
            dstP->blue  = static_cast<A_u_char>(b);
            dstP->alpha = static_cast<A_u_char>(a);
            dstP++;
        }
    }
    
    // Timeチェックアウトの返却（メモリリーク防止）
    if (time_checked_out) PF_CHECKIN_PARAM(in_data, &checkout_param);
    return err;
}

// =========================================================================
// 5. Entry Point
// =========================================================================
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
