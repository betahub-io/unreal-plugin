#pragma once

#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("BetaHub"), STATGROUP_BetaHub, STATCAT_Advanced);

DECLARE_CYCLE_STAT(TEXT("Tick"), STAT_BetaHub_Tick, STATGROUP_BetaHub);
DECLARE_CYCLE_STAT(TEXT("OnBackBufferReady"), STAT_BetaHub_OnBackBufferReady, STATGROUP_BetaHub);
DECLARE_CYCLE_STAT(TEXT("ReadPixels"), STAT_BetaHub_ReadPixels, STATGROUP_BetaHub);
DECLARE_CYCLE_STAT(TEXT("CopyBackBuffer"), STAT_BetaHub_CopyBackBuffer, STATGROUP_BetaHub);
DECLARE_CYCLE_STAT(TEXT("ProcessFrame"), STAT_BetaHub_ProcessFrame, STATGROUP_BetaHub);
DECLARE_CYCLE_STAT(TEXT("SetFrameData"), STAT_BetaHub_SetFrameData, STATGROUP_BetaHub);
