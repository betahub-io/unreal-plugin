// Copyright Epic Games, Inc. All Rights Reserved.

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
                "EnhancedInput"
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

        RuntimeDependencies.Add("$(TargetOutputDir)/bh_ffmpeg.exe", Path.Combine(PluginDirectory, "ThirdParty/FFmpeg/Windows/ffmpeg.exe"));

		RuntimeDependencies.Add("$(TargetOutputDir)/save.bh", Path.Combine(PluginDirectory, "save.bh"));
	}
}
