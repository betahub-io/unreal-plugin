// Copyright (c) 2024-2026 Upsoft sp. z o. o.

#pragma once

#include "CoreMinimal.h"

struct BH_FFmpegOptions
{
	FString Encoder;
	FString Options;
};

class BH_FFmpeg
{
public:
	static FString GetFFmpegPath();
	static BH_FFmpegOptions GetFFmpegPreferredOptions();

private:
	// the top one is the most preferred one, but needs to be tested
	static TArray<BH_FFmpegOptions> GetFFmpegAvailableOptions();

	
};