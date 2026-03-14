#pragma once
#ifndef _H_SKELETON
#define _H_SKELETON

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include <stdint.h>

// プラグインのバージョン情報
#define MAJOR_VERSION	1
#define MINOR_VERSION	0
#define BUG_VERSION		0
#define STAGE_VERSION	PF_Stage_DEVELOP
#define BUILD_VERSION	1

// UIスライダーの定義（Twitchの根幹）
enum {
	TWITCH_INPUT = 0,
	TWITCH_AMOUNT, // カオスの強さ
	TWITCH_SPEED,  // カオスの頻度
	TWITCH_NUM_PARAMS
};

enum {
	AMOUNT_DISK_ID = 1,
	SPEED_DISK_ID
};

extern "C" {
	DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output, void *extra);
}

#endif // _H_SKELETON
