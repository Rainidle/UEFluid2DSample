#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "ShaderCompilerCore.h"
#include "DataDrivenShaderPlatformInfo.h"

// Per-frame injection budgets. Pushed to the shaders as defines so the HLSL array sizes
// and the C++ parameter arrays cannot drift apart.
inline constexpr int32 GFluid2DMaxImpulses = 16;
inline constexpr int32 GFluid2DMaxExplosions = 8;

// Tile geometry of the compute pressure solver. The halo ring count is also the number
// of Jacobi sweeps a single dispatch can complete in group shared memory, and the
// interior is what each group writes back.
inline constexpr int32 GFluid2DPressureTile = 32;
inline constexpr int32 GFluid2DPressureItersPerDispatch = 4;
inline constexpr int32 GFluid2DPressureTileInterior = GFluid2DPressureTile - 2 * GFluid2DPressureItersPerDispatch;

// Every shader here needs SM5 for dynamic loops
#define FLUID2D_SM5_ONLY() \
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) \
	{ \
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); \
	}

// const defs sync
#define FLUID2D_PIXEL_SHADER_BODY() \
	FLUID2D_SM5_ONLY() \
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment) \
	{ \
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment); \
		OutEnvironment.SetDefine(TEXT("FLUID2D_MAX_IMPULSES"), GFluid2DMaxImpulses); \
		OutEnvironment.SetDefine(TEXT("FLUID2D_MAX_EXPLOSIONS"), GFluid2DMaxExplosions); \
	}

#define FLUID2D_SOLVER_SHADER_BODY() \
	FLUID2D_SM5_ONLY() \
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment) \
	{ \
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment); \
		OutEnvironment.SetDefine(TEXT("FLUID2D_PRESSURE_TILE"), GFluid2DPressureTile); \
		OutEnvironment.SetDefine(TEXT("FLUID2D_PRESSURE_HALO"), GFluid2DPressureItersPerDispatch); \
	}

// Semi-Lagrangian advection, shared by the velocity and dye fields.
class FFluid2DAdvectPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluid2DAdvectPS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DAdvectPS, FGlobalShader);
	FLUID2D_PIXEL_SHADER_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocityTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampSampler)
		SHADER_PARAMETER(FVector2f, InvSimResolution)
		SHADER_PARAMETER(float, DeltaTime)
		SHADER_PARAMETER(float, Dissipation)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

// Segment impulses. The DYE_MODE permutation compiles one HLSL body into a variant that
// writes velocity and one that writes dye.
class FFluid2DSplatPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluid2DSplatPS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DSplatPS, FGlobalShader);
	FLUID2D_PIXEL_SHADER_BODY()

	class FDyeMode : SHADER_PERMUTATION_BOOL("DYE_MODE");
	using FPermutationDomain = TShaderPermutationDomain<FDyeMode>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampSampler)
		SHADER_PARAMETER(FVector2f, InvSimResolution)
		SHADER_PARAMETER(float, AspectRatio)
		SHADER_PARAMETER(float, SimWidth)
		SHADER_PARAMETER(int32, ImpulseCount)
		SHADER_PARAMETER_ARRAY(FVector4f, ImpulseSegment, [GFluid2DMaxImpulses])
		SHADER_PARAMETER_ARRAY(FVector4f, ImpulseVelocityAndRadius, [GFluid2DMaxImpulses])
		SHADER_PARAMETER_ARRAY(FVector4f, ImpulseDye, [GFluid2DMaxImpulses])
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

// Procedural radial bursts, no texture assets involved.
class FFluid2DExplosionPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluid2DExplosionPS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DExplosionPS, FGlobalShader);
	FLUID2D_PIXEL_SHADER_BODY()

	class FDyeMode : SHADER_PERMUTATION_BOOL("DYE_MODE");
	using FPermutationDomain = TShaderPermutationDomain<FDyeMode>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampSampler)
		SHADER_PARAMETER(FVector2f, InvSimResolution)
		SHADER_PARAMETER(float, AspectRatio)
		SHADER_PARAMETER(int32, ExplosionCount)
		SHADER_PARAMETER_ARRAY(FVector4f, ExplosionCenterRadius, [GFluid2DMaxExplosions])
		SHADER_PARAMETER_ARRAY(FVector4f, ExplosionNoise, [GFluid2DMaxExplosions])
		SHADER_PARAMETER_ARRAY(FVector4f, ExplosionDyeColor, [GFluid2DMaxExplosions])
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

// Scene luminance as a height field, pushing the fluid along its pseudo-normal.
class FFluid2DBrightnessForcePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluid2DBrightnessForcePS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DBrightnessForcePS, FGlobalShader);
	FLUID2D_PIXEL_SHADER_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampSampler)
		SHADER_PARAMETER(FVector2f, InvSimResolution)
		SHADER_PARAMETER(FVector2f, SceneUVMin)
		SHADER_PARAMETER(FVector2f, SceneUVScale)
		SHADER_PARAMETER(FVector2f, SceneInvExtent)
		SHADER_PARAMETER(float, BrightnessTexelScale)
		SHADER_PARAMETER(float, BrightnessHeightScale)
		SHADER_PARAMETER(float, BrightnessStrength)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

// Procedural curl noise, divergence-free by construction.
class FFluid2DNoiseForcePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluid2DNoiseForcePS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DNoiseForcePS, FGlobalShader);
	FLUID2D_PIXEL_SHADER_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampSampler)
		SHADER_PARAMETER(FVector2f, InvSimResolution)
		SHADER_PARAMETER(float, AspectRatio)
		SHADER_PARAMETER(float, NoiseScale)
		SHADER_PARAMETER(float, NoiseSpeed)
		SHADER_PARAMETER(float, NoiseStrength)
		SHADER_PARAMETER(float, Time)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

// Blends the fluid towards per-pixel screen motion read from the engine velocity buffer,
// which captures skinned deformation that a centroid impulse cannot express.
class FFluid2DMotionForcePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluid2DMotionForcePS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DMotionForcePS, FGlobalShader);
	FLUID2D_PIXEL_SHADER_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferVelocityTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CustomDepthTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, PointClampSampler)
		SHADER_PARAMETER(FVector2f, InvSimResolution)
		SHADER_PARAMETER(FVector2f, SceneUVMin)
		SHADER_PARAMETER(FVector2f, SceneUVScale)
		SHADER_PARAMETER(float, MaskDepthBias)
		SHADER_PARAMETER(int32, MotionUseMask)
		SHADER_PARAMETER(float, MotionBlend)
		SHADER_PARAMETER(float, RawDeltaTime)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

// Dye sources: scene highlights, masked silhouettes and motion-driven bleed. The mask
// comes from the engine's Custom Depth pass, so opting an actor in is a per-actor flag.
class FFluid2DAccumulatePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluid2DAccumulatePS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DAccumulatePS, FGlobalShader);
	FLUID2D_PIXEL_SHADER_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CustomDepthTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferVelocityTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, PointClampSampler)
		SHADER_PARAMETER(FVector2f, InvSimResolution)
		SHADER_PARAMETER(FVector2f, SceneUVMin)
		SHADER_PARAMETER(FVector2f, SceneUVScale)
		SHADER_PARAMETER(float, AccumStrength)
		SHADER_PARAMETER(float, AccumThreshold)
		SHADER_PARAMETER(int32, MaskEnabled)
		SHADER_PARAMETER(float, MaskDepthBias)
		SHADER_PARAMETER(float, MaskInkStrength)
		SHADER_PARAMETER(float, MotionInkStrength)
		SHADER_PARAMETER(float, RawDeltaTime)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

// Fixed 3x3 Gaussian on the dye field, run at simulation resolution.
class FFluid2DBlurPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluid2DBlurPS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DBlurPS, FGlobalShader);
	FLUID2D_PIXEL_SHADER_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampSampler)
		SHADER_PARAMETER(FVector2f, InvSimResolution)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

class FFluid2DDivergencePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluid2DDivergencePS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DDivergencePS, FGlobalShader);
	FLUID2D_PIXEL_SHADER_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocityTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampSampler)
		SHADER_PARAMETER(FVector2f, InvSimResolution)
		SHADER_PARAMETER(float, HalfRDX)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

class FFluid2DPressureSolvePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluid2DPressureSolvePS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DPressureSolvePS, FGlobalShader);
	FLUID2D_PIXEL_SHADER_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PressureTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DivergenceTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampSampler)
		SHADER_PARAMETER(FVector2f, InvSimResolution)
		SHADER_PARAMETER(float, Alpha)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

class FFluid2DGradientSubtractPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluid2DGradientSubtractPS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DGradientSubtractPS, FGlobalShader);
	FLUID2D_PIXEL_SHADER_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PressureTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocityTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampSampler)
		SHADER_PARAMETER(FVector2f, InvSimResolution)
		SHADER_PARAMETER(float, HalfRDX)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

// Compute counterparts of the three solver passes, selected by r.Fluid2DSample.ComputeSolver.
// The Jacobi kernel iterates in group shared memory, collapsing several sweeps into one
// dispatch instead of one fullscreen render pass each.

class FFluid2DDivergenceCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluid2DDivergenceCS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DDivergenceCS, FGlobalShader);
	FLUID2D_SOLVER_SHADER_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SrcVelocityTex)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutDivergenceUAV)
		SHADER_PARAMETER(FIntPoint, SimBounds)
		SHADER_PARAMETER(float, HalfRDX)
	END_SHADER_PARAMETER_STRUCT()
};

class FFluid2DPressureSolveCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluid2DPressureSolveCS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DPressureSolveCS, FGlobalShader);
	FLUID2D_SOLVER_SHADER_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SrcPressureTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SrcDivergenceTex)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutPressureUAV)
		SHADER_PARAMETER(FIntPoint, SimBounds)
		SHADER_PARAMETER(float, Alpha)
		SHADER_PARAMETER(int32, IterationsThisDispatch)
	END_SHADER_PARAMETER_STRUCT()
};

class FFluid2DGradientSubtractCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluid2DGradientSubtractCS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DGradientSubtractCS, FGlobalShader);
	FLUID2D_SOLVER_SHADER_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SrcVelocityTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SrcPressureTex)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutVelocityUAV)
		SHADER_PARAMETER(FIntPoint, SimBounds)
		SHADER_PARAMETER(float, HalfRDX)
	END_SHADER_PARAMETER_STRUCT()
};

// Final pass: refract scene colour through the velocity field and present the dye.
class FFluid2DCompositePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluid2DCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DCompositePS, FGlobalShader);
	FLUID2D_PIXEL_SHADER_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocityTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DyeTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CustomDepthTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, PointClampSampler)
		SHADER_PARAMETER(FVector2f, InvSimResolution)
		SHADER_PARAMETER(FVector2f, ViewportMin)
		SHADER_PARAMETER(FVector2f, InvViewportSize)
		SHADER_PARAMETER(FVector2f, SceneUVMin)
		SHADER_PARAMETER(FVector2f, SceneUVScale)
		SHADER_PARAMETER(FVector2f, SceneInvExtent)
		SHADER_PARAMETER(float, DistortionStrength)
		SHADER_PARAMETER(float, DyeIntensity)
		SHADER_PARAMETER(int32, PaletteMode)
		SHADER_PARAMETER(float, Time)
		SHADER_PARAMETER(int32, MaskEnabled)
		SHADER_PARAMETER(float, MaskDepthBias)
		SHADER_PARAMETER(float, MaskShowModel)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
