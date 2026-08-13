// Copyright (c) 2024-2026 Upsoft sp. z o. o.

using UnrealBuildTool;
using System;
using System.IO;

public class BetaHubBugReporter : ModuleRules
{
	public BetaHubBugReporter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"UMG",
				"InputCore",
				"HTTP",
				"Json",
				"JsonUtilities",
				"RenderCore",
				"RHI",
				// ... add private dependencies that you statically link with here ...
			}
			);

		// Optional hardware video encode via the engine's AVCodecs plugins (Experimental).
		// Default OFF, so the shipped plugin has NO dependency on Experimental engine plugins
		// and the ffmpeg path is the only backend. Opt in by setting the environment variable
		// BETAHUB_HWENCODE=1 at build time AND enabling the AVCodecs/NVCodecs plugins in your
		// .uproject. All hardware-encode code is compiled out unless WITH_BETAHUB_HWENCODE=1.
		bool bEnableHardwareEncode = Environment.GetEnvironmentVariable("BETAHUB_HWENCODE") == "1";
		if (bEnableHardwareEncode)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AVCodecsCore",
					"AVCodecsCoreRHI",
				}
				);
			PrivateDefinitions.Add("WITH_BETAHUB_HWENCODE=1");
		}
		else
		{
			PrivateDefinitions.Add("WITH_BETAHUB_HWENCODE=0");
		}
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

		
		string ffmpegPath = Path.Combine(PluginDirectory, "ThirdParty/FFmpeg/Windows/ffmpeg.exe");
		if (File.Exists(ffmpegPath))
		{
			RuntimeDependencies.Add("$(TargetOutputDir)/bh_ffmpeg.exe", ffmpegPath);
		}
	}
}
