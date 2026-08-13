// Copyright (c) 2024-2026 Upsoft sp. z o. o.
// Developer tool - not part of the shipped BetaHub Bug Reporter plugin.

using UnrealBuildTool;

public class BetaHubWidgetGen : ModuleRules
{
	public BetaHubWidgetGen(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG",
			"Slate",
			"SlateCore",
			"RenderCore",
			"Json"
		});
	}
}
