using UnrealBuildTool;
using System.IO;

public class UEFluid2DSample : ModuleRules
{
	public UEFluid2DSample(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",     // IPluginManager, to locate the plugin's shader directory
			"RenderCore",   // FGlobalShader, RDG, FPixelShaderUtils
			"Renderer",     // FSceneViewExtensionBase, FPostProcessingInputs
			"RHI",
		});

		// FPostProcessingInputs and FViewInfo, both needed by PrePostProcessPass_RenderThread,
		// live in the Renderer module's private directory and have no public header yet.
		PrivateIncludePaths.Add(Path.Combine(Path.GetFullPath(Target.RelativeEnginePath), "Source/Runtime/Renderer/Private"));
	}
}
