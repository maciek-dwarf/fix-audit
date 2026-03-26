using UnrealBuildTool;

public class LevelContextExporter : ModuleRules
{
	public LevelContextExporter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UnrealEd",
			"EditorSubsystem",
			"LevelEditor",
			"ToolMenus",
			"AssetRegistry",
			"Json",
			"JsonUtilities",
		});
	}
}
