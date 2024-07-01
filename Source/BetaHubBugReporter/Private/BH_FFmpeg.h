// Fill out your copyright notice in the Description page of Project Settings.

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