#include "UEFluid2DSampleModule.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

void FUEFluid2DSampleModule::StartupModule()
{
	// Map the plugin's Shaders directory to the virtual path used by IMPLEMENT_GLOBAL_SHADER.
	// This must happen before any shader compiles, which is why the module loads during
	// PostConfigInit.
	const FString ShaderDir = FPaths::Combine(
		IPluginManager::Get().FindPlugin(TEXT("UEFluid2DSample"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/UEFluid2DSample"), ShaderDir);
}

void FUEFluid2DSampleModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FUEFluid2DSampleModule, UEFluid2DSample)
