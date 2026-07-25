#include "Fluid2DViewExtension.h"
#include "Fluid2DShaders.h"

#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"

#include "Engine/World.h"
#include "SceneInterface.h"

#include "PostProcess/PostProcessInputs.h"
#include "SceneRendering.h"

// ---------------------------------------------------------------------------
// Console variables
// ---------------------------------------------------------------------------

static TAutoConsoleVariable<int32> CVarFluid2DEnable(
	TEXT("r.Fluid2DSample.Enable"), 1,
	TEXT("Enable the 2D fluid post process."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarFluid2DSimWidth(
	TEXT("r.Fluid2DSample.SimWidth"), 768,
	TEXT("Maximum width of the simulation grid; height follows the viewport aspect ratio."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarFluid2DPressureIterations(
	TEXT("r.Fluid2DSample.PressureIterations"), 8,
	TEXT("Jacobi iterations for the pressure solve. More is more accurate and more expensive."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarFluid2DComputeSolver(
	TEXT("r.Fluid2DSample.ComputeSolver"), 1,
	TEXT("Run the solver chain (divergence, Jacobi, projection) as compute shaders.\n")
	TEXT("The Jacobi kernel performs several sweeps per dispatch in group shared memory,\n")
	TEXT("avoiding one fullscreen render pass per iteration. 0 falls back to the pixel path."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarFluid2DDemoEmitter(
	TEXT("r.Fluid2DSample.DemoEmitter"), 0,
	TEXT("Built-in demo emitter: a colour-cycling plume swaying along the bottom of the screen."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarFluid2DBrightnessForce(
	TEXT("r.Fluid2DSample.BrightnessForce"), 0,
	TEXT("Drive the fluid from scene luminance treated as a height field."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFluid2DBrightnessStrength(
	TEXT("r.Fluid2DSample.BrightnessStrength"), 0.02f,
	TEXT("Strength of the screen brightness force."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarFluid2DNoiseForce(
	TEXT("r.Fluid2DSample.NoiseForce"), 1,
	TEXT("Enable the procedural curl noise force. Requires no texture assets."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFluid2DNoiseStrength(
	TEXT("r.Fluid2DSample.NoiseStrength"), 8.0f,
	TEXT("Curl noise strength, in simulation cells per second injected each frame."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFluid2DNoiseScale(
	TEXT("r.Fluid2DSample.NoiseScale"), 90.0f,
	TEXT("Noise repetitions across the screen. Higher values give smaller vortices."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFluid2DNoiseSpeed(
	TEXT("r.Fluid2DSample.NoiseSpeed"), 5.0f,
	TEXT("How fast the noise field evolves over time."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarFluid2DMotionForce(
	TEXT("r.Fluid2DSample.MotionForce"), 1,
	TEXT("Drive the fluid from the engine velocity buffer, so per-pixel screen motion of\n")
	TEXT("dynamic geometry (including skinned deformation) stirs the field."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFluid2DMotionForceBlend(
	TEXT("r.Fluid2DSample.MotionForceBlend"), 0.5f,
	TEXT("How far the fluid is blended towards the measured pixel motion, 0-1."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarFluid2DMotionForceMasked(
	TEXT("r.Fluid2DSample.MotionForceMasked"), 1,
	TEXT("1 = only actors with Render CustomDepth contribute motion, which is the precise\n")
	TEXT("way to choose what drives the fluid. 0 = every dynamic pixel on screen contributes."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFluid2DMotionInkStrength(
	TEXT("r.Fluid2DSample.MotionInkStrength"), 1.8f,
	TEXT("Dye bleed proportional to pixel speed inside the mask, so fast limbs spray ink.\n")
	TEXT("Requires r.Fluid2DSample.DepthMask 1. 0 disables it."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarFluid2DSceneAccum(
	TEXT("r.Fluid2DSample.SceneAccum"), 0,
	TEXT("Bleed scene highlights above a threshold into the dye field, leaving trails."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFluid2DSceneAccumStrength(
	TEXT("r.Fluid2DSample.SceneAccumStrength"), 0.03f,
	TEXT("Highlight bleed strength, per frame."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFluid2DSceneAccumThreshold(
	TEXT("r.Fluid2DSample.SceneAccumThreshold"), 1.0f,
	TEXT("Linear HDR luminance above which scene colour starts bleeding into the dye field."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarFluid2DDepthMask(
	TEXT("r.Fluid2DSample.DepthMask"), 1,
	TEXT("Restrict dye bleed to unoccluded actors that have Render CustomDepth enabled.\n")
	TEXT("Requires the project's Custom Depth-Stencil Pass to be enabled."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFluid2DMaskInkStrength(
	TEXT("r.Fluid2DSample.MaskInkStrength"), 0.5f,
	TEXT("Dye bleed strength for masked silhouettes, per frame. Ink takes the object's own\n")
	TEXT("scene colour, so use r.Fluid2DSample.Palette 0 to keep it."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFluid2DMaskDepthBias(
	TEXT("r.Fluid2DSample.MaskDepthBias"), 0.0005f,
	TEXT("Device-Z tolerance for the mask depth comparison, to avoid self-occlusion flicker."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFluid2DMaskShowModel(
	TEXT("r.Fluid2DSample.MaskShowModel"), 0.6f,
	TEXT("How strongly masked pixels are kept free of dye and refraction, 0-1. At 1 the\n")
	TEXT("object reads through cleanly and the fluid stays around it. Requires DepthMask 1."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarFluid2DPalette(
	TEXT("r.Fluid2DSample.Palette"), 0,
	TEXT("Composite mode: 0 = add dye RGB directly, 1 = map dye density through a cosine palette."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarFluid2DBlurIterations(
	TEXT("r.Fluid2DSample.BlurIterations"), 1,
	TEXT("Number of 3x3 Gaussian passes applied to the dye field. 0 disables the blur."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFluid2DVelocityDissipation(
	TEXT("r.Fluid2DSample.VelocityDissipation"), 0.999f,
	TEXT("Decay applied to the velocity field during advection."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFluid2DDyeDissipation(
	TEXT("r.Fluid2DSample.DyeDissipation"), 0.96f,
	TEXT("Decay applied to the dye field during advection. Lower values fade faster."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFluid2DDistortion(
	TEXT("r.Fluid2DSample.Distortion"), 0.04f,
	TEXT("Refraction of the image by the velocity field, weighted by dye density so it only\n")
	TEXT("affects inked areas. 0 disables it."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFluid2DDyeIntensity(
	TEXT("r.Fluid2DSample.DyeIntensity"), 0.8f,
	TEXT("Brightness of the dye overlay in the composite."),
	ECVF_RenderThreadSafe);

// ---------------------------------------------------------------------------

FFluid2DViewExtension::FFluid2DViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

bool FFluid2DViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return CVarFluid2DEnable.GetValueOnGameThread() != 0;
}

void FFluid2DViewExtension::EnqueueImpulse(const FFluid2DImpulse& Impulse)
{
	FScopeLock Lock(&ImpulseLock);
	if (PendingImpulses.Num() < 64)
	{
		PendingImpulses.Add(Impulse);
	}
}

void FFluid2DViewExtension::EnqueueWorldImpulse(const FFluid2DWorldImpulse& Impulse)
{
	FScopeLock Lock(&ImpulseLock);
	if (PendingWorldImpulses.Num() < 64)
	{
		PendingWorldImpulses.Add(Impulse);
	}
}

void FFluid2DViewExtension::EnqueueExplosion(const FFluid2DExplosion& Explosion)
{
	FScopeLock Lock(&ImpulseLock);
	if (PendingExplosions.Num() < 32)
	{
		PendingExplosions.Add(Explosion);
	}
}

void FFluid2DViewExtension::EnqueueWorldExplosion(const FFluid2DWorldExplosion& Explosion)
{
	FScopeLock Lock(&ImpulseLock);
	if (PendingWorldExplosions.Num() < 32)
	{
		PendingWorldExplosions.Add(Explosion);
	}
}

static bool IsPreviewScene(const FSceneInterface* Scene)
{
	const UWorld* World = Scene ? Scene->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	return World->WorldType == EWorldType::EditorPreview
		|| World->WorldType == EWorldType::GamePreview
		|| World->WorldType == EWorldType::Inactive;
}

void FFluid2DViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
	if (View.bIsSceneCapture || View.bIsReflectionCapture || View.bIsPlanarReflection)
	{
		return;
	}

	if (View.State == nullptr || IsPreviewScene(View.Family->Scene))
	{
		return;
	}

	Inputs.Validate();
	if (!Inputs.SceneTextures)
	{
		return;
	}

	// Scene colour can be larger than the rect this view occupies, either because editor
	// viewports share one texture or because of dynamic resolution, so every UV conversion
	// below goes through the view rect.
	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(View);
	const FIntRect ViewRect = ViewInfo.ViewRect;
	if (ViewRect.Width() <= 0 || ViewRect.Height() <= 0)
	{
		return;
	}

	FRDGTextureRef SceneColor = (*Inputs.SceneTextures)->SceneColorTexture;
	if (!SceneColor)
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "Fluid2DSample");

	// The composite writes scene colour while also reading it, and a texture cannot be an
	// SRV and a render target at once, hence the copy. Only the view rect is copied; the
	// rest of the texture is never sampled.
	const FRDGTextureDesc SceneCopyDesc = FRDGTextureDesc::Create2D(
		SceneColor->Desc.Extent,
		SceneColor->Desc.Format,
		FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_RenderTargetable);
	FRDGTextureRef SceneColorCopy = GraphBuilder.CreateTexture(SceneCopyDesc, TEXT("Fluid2D.SceneColorCopy"));
	{
		FRHICopyTextureInfo CopyInfo;
		CopyInfo.SourcePosition = FIntVector(ViewRect.Min.X, ViewRect.Min.Y, 0);
		CopyInfo.DestPosition = CopyInfo.SourcePosition;
		CopyInfo.Size = FIntVector(ViewRect.Width(), ViewRect.Height(), 1);
		AddCopyTexturePass(GraphBuilder, SceneColor, SceneColorCopy, CopyInfo);
	}

	// The depth mask needs scene and custom depth; the motion force needs the velocity buffer.
	FRDGTextureRef SceneDepth = (*Inputs.SceneTextures)->SceneDepthTexture;
	FRDGTextureRef CustomDepth = (*Inputs.SceneTextures)->CustomDepthTexture;
	FRDGTextureRef SceneVelocity = (*Inputs.SceneTextures)->GBufferVelocityTexture;

	// The first view rendered this frame advances the simulation.
	if (LastSimFrame != GFrameCounterRenderThread)
	{
		LastSimFrame = GFrameCounterRenderThread;
		StepSimulation(GraphBuilder, View, SceneColorCopy, SceneDepth, CustomDepth, SceneVelocity, ViewRect);
	}

	if (!VelocityRT[VelocityIdx].IsValid() || !DyeRT[DyeIdx].IsValid())
	{
		return;
	}

	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	const FVector2f SceneInvExtent(1.0f / SceneColor->Desc.Extent.X, 1.0f / SceneColor->Desc.Extent.Y);

	const bool bCompositeMask = CVarFluid2DDepthMask.GetValueOnRenderThread() != 0
		&& SceneDepth != nullptr && CustomDepth != nullptr;

	TShaderMapRef<FFluid2DCompositePS> CompositePS(ShaderMap);
	FFluid2DCompositePS::FParameters* Params = GraphBuilder.AllocParameters<FFluid2DCompositePS::FParameters>();
	Params->SceneColorTex = SceneColorCopy;
	Params->VelocityTex = GraphBuilder.RegisterExternalTexture(VelocityRT[VelocityIdx]);
	Params->DyeTex = GraphBuilder.RegisterExternalTexture(DyeRT[DyeIdx]);
	Params->SceneDepthTex = bCompositeMask ? SceneDepth : SceneColorCopy;
	Params->CustomDepthTex = bCompositeMask ? CustomDepth : SceneColorCopy;
	Params->BilinearClampSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Params->PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Params->InvSimResolution = FVector2f(1.0f / FMath::Max(SimSize.X, 1), 1.0f / FMath::Max(SimSize.Y, 1));
	Params->ViewportMin = FVector2f(ViewRect.Min.X, ViewRect.Min.Y);
	Params->InvViewportSize = FVector2f(1.0f / ViewRect.Width(), 1.0f / ViewRect.Height());
	Params->SceneUVMin = FVector2f(ViewRect.Min.X, ViewRect.Min.Y) * SceneInvExtent;
	Params->SceneUVScale = FVector2f(ViewRect.Width(), ViewRect.Height()) * SceneInvExtent;
	Params->SceneInvExtent = SceneInvExtent;
	Params->DistortionStrength = CVarFluid2DDistortion.GetValueOnRenderThread();
	Params->DyeIntensity = CVarFluid2DDyeIntensity.GetValueOnRenderThread();
	Params->PaletteMode = CVarFluid2DPalette.GetValueOnRenderThread();
	Params->Time = View.Family->Time.GetWorldTimeSeconds();
	Params->MaskEnabled = bCompositeMask ? 1 : 0;
	Params->MaskDepthBias = CVarFluid2DMaskDepthBias.GetValueOnRenderThread();
	Params->MaskShowModel = FMath::Clamp(CVarFluid2DMaskShowModel.GetValueOnRenderThread(), 0.0f, 1.0f);
	// ELoad because only the view rect is overwritten; other editor viewports sharing this
	// texture must survive.
	Params->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);

	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder, ShaderMap, RDG_EVENT_NAME("Fluid2D Composite"),
		CompositePS, Params, ViewRect);
}

void FFluid2DViewExtension::StepSimulation(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef SceneColorCopy,
	FRDGTextureRef SceneDepth, FRDGTextureRef CustomDepth, FRDGTextureRef SceneVelocity, const FIntRect& ViewRect)
{
	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	FRHISamplerState* Bilinear = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	// Cap the width and derive the height from the viewport aspect ratio, 
	// keeps the simulation cells square on screen.
	const int32 MaxWidth = FMath::Clamp(CVarFluid2DSimWidth.GetValueOnRenderThread(), 64, 2048);
	FIntPoint Desired;
	Desired.X = FMath::Min(MaxWidth, ViewRect.Width());
	Desired.Y = FMath::Max(1, FMath::FloorToInt(Desired.X * (ViewRect.Height() / (float)ViewRect.Width())));

	// The UAV flag is for the compute solver.
	const ETextureCreateFlags SolverFlags =
		TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV;
	const FRDGTextureDesc VelocityDesc = FRDGTextureDesc::Create2D(
		Desired, PF_G16R16F, FClearValueBinding::Black, SolverFlags);
	const FRDGTextureDesc PressureDesc = FRDGTextureDesc::Create2D(
		Desired, PF_R16F, FClearValueBinding::Black, SolverFlags);
	const FRDGTextureDesc DyeDesc = FRDGTextureDesc::Create2D(
		Desired, PF_FloatRGBA, FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_RenderTargetable);

	FRDGTextureRef Velocity[2];
	FRDGTextureRef Pressure[2];
	FRDGTextureRef Dye[2];

	const bool bReset = (Desired != SimSize)
		|| !VelocityRT[0] || !VelocityRT[1]
		|| !PressureRT[0] || !PressureRT[1]
		|| !DyeRT[0] || !DyeRT[1];

	if (bReset)
	{
		SimSize = Desired;
		VelocityIdx = PressureIdx = DyeIdx = 0;

		auto CreateField = [&](const FRDGTextureDesc& Desc, const TCHAR* Name)
		{
			FRDGTextureRef Tex = GraphBuilder.CreateTexture(Desc, Name);
			AddClearRenderTargetPass(GraphBuilder, Tex, FLinearColor::Transparent);
			return Tex;
		};

		Velocity[0] = CreateField(VelocityDesc, TEXT("Fluid2D.Velocity0"));
		Velocity[1] = CreateField(VelocityDesc, TEXT("Fluid2D.Velocity1"));
		Pressure[0] = CreateField(PressureDesc, TEXT("Fluid2D.Pressure0"));
		Pressure[1] = CreateField(PressureDesc, TEXT("Fluid2D.Pressure1"));
		Dye[0] = CreateField(DyeDesc, TEXT("Fluid2D.Dye0"));
		Dye[1] = CreateField(DyeDesc, TEXT("Fluid2D.Dye1"));

		// Promote to external so the contents outlive graph execution.
		VelocityRT[0] = GraphBuilder.ConvertToExternalTexture(Velocity[0]);
		VelocityRT[1] = GraphBuilder.ConvertToExternalTexture(Velocity[1]);
		PressureRT[0] = GraphBuilder.ConvertToExternalTexture(Pressure[0]);
		PressureRT[1] = GraphBuilder.ConvertToExternalTexture(Pressure[1]);
		DyeRT[0] = GraphBuilder.ConvertToExternalTexture(Dye[0]);
		DyeRT[1] = GraphBuilder.ConvertToExternalTexture(Dye[1]);
	}
	else
	{
		Velocity[0] = GraphBuilder.RegisterExternalTexture(VelocityRT[0]);
		Velocity[1] = GraphBuilder.RegisterExternalTexture(VelocityRT[1]);
		Pressure[0] = GraphBuilder.RegisterExternalTexture(PressureRT[0]);
		Pressure[1] = GraphBuilder.RegisterExternalTexture(PressureRT[1]);
		Dye[0] = GraphBuilder.RegisterExternalTexture(DyeRT[0]);
		Dye[1] = GraphBuilder.RegisterExternalTexture(DyeRT[1]);
	}

	float RawDt = View.Family->Time.GetDeltaWorldTimeSeconds();
	if (RawDt <= 0.0f)
	{
		RawDt = 1.0f / 60.0f;
	}
	RawDt = FMath::Min(RawDt, 1.0f / 30.0f);
	// Half steps keep advection stable at the grid resolutions used here. RawDt is kept
	// separately because motion vectors are a displacement over one full frame.
	const float Dt = RawDt * 0.5f;

	const float SimWidthF = (float)SimSize.X;
	const float HalfRDX = 0.5f / SimWidthF;
	const float PoissonAlpha = -(SimWidthF * SimWidthF);
	const float Aspect = SimSize.X / (float)SimSize.Y;
	const FVector2f InvSim(1.0f / SimSize.X, 1.0f / SimSize.Y);
	const FIntRect SimRect(0, 0, SimSize.X, SimSize.Y);

	TArray<FFluid2DImpulse> Impulses;
	TArray<FFluid2DWorldImpulse> WorldImpulses;
	TArray<FFluid2DExplosion> Explosions;
	TArray<FFluid2DWorldExplosion> WorldExplosions;
	{
		FScopeLock Lock(&ImpulseLock);
		Impulses = MoveTemp(PendingImpulses);
		PendingImpulses.Reset();
		WorldImpulses = MoveTemp(PendingWorldImpulses);
		PendingWorldImpulses.Reset();
		Explosions = MoveTemp(PendingExplosions);
		PendingExplosions.Reset();
		WorldExplosions = MoveTemp(PendingWorldExplosions);
		PendingWorldExplosions.Reset();
	}

	// Projecting here rather than on the game thread means using the view matrices that
	// are actually being rendered this frame, for whichever view that happens to be.
	const FMatrix ViewProj = View.ViewMatrices.GetViewProjectionMatrix();

	auto ProjectToUV = [&ViewProj](const FVector& WorldPos, FVector2f& OutUV) -> bool
	{
		const FVector4 Clip = ViewProj.TransformFVector4(FVector4(WorldPos, 1.0));
		if (Clip.W <= UE_SMALL_NUMBER)
		{
			return false; // behind the camera
		}
		// NDC (-1..1, Y up) to viewport UV (0..1, Y down)
		OutUV = FVector2f(
			(float)(Clip.X / Clip.W) * 0.5f + 0.5f,
			(float)(Clip.Y / Clip.W) * -0.5f + 0.5f);
		return true;
	};

	auto IsNearViewport = [](const FVector2f& UV)
	{
		return UV.X > -0.1f && UV.X < 1.1f && UV.Y > -0.1f && UV.Y < 1.1f;
	};

	for (const FFluid2DWorldImpulse& W : WorldImpulses)
	{
		FVector2f PrevUV, CurrUV;
		if (ProjectToUV(W.PrevPos, PrevUV) && ProjectToUV(W.CurrPos, CurrUV) &&
			(IsNearViewport(PrevUV) || IsNearViewport(CurrUV)))
		{
			FFluid2DImpulse Imp;
			Imp.PrevUV = PrevUV;
			Imp.CurrUV = CurrUV;
			Imp.Radius = W.Radius;
			Imp.Dye = W.Dye;
			Impulses.Add(Imp);
		}
	}

	for (const FFluid2DWorldExplosion& W : WorldExplosions)
	{
		FVector2f CenterUV;
		if (ProjectToUV(W.Pos, CenterUV) && IsNearViewport(CenterUV))
		{
			FFluid2DExplosion Exp;
			Exp.CenterUV = CenterUV;
			Exp.Radius = W.Radius;
			Exp.Strength = W.Strength;
			Exp.Irregularity = W.Irregularity;
			Exp.Seed = W.Seed;
			Exp.Dye = W.Dye;
			Explosions.Add(Exp);
		}
	}

	if (CVarFluid2DDemoEmitter.GetValueOnRenderThread() != 0)
	{
		const float T = View.Family->Time.GetWorldTimeSeconds();

		FFluid2DImpulse Plume;
		const FVector2f Pos(0.5f + 0.2f * FMath::Sin(T * 0.7f), 0.85f);
		FVector2f Dir(0.35f * FMath::Sin(T * 2.3f), -1.0f); // -Y points up the screen
		Dir.Normalize();

		Plume.PrevUV = Pos;
		Plume.CurrUV = Pos + Dir * 0.8f * Dt;
		Plume.Radius = 0.025f;
		Plume.Dye = FLinearColor::MakeFromHSV8((uint8)FMath::Fmod(T * 24.0f, 255.0f), 200, 255);
		Plume.Dye.A = 0.2f;
		Impulses.Add(Plume);
	}

	const int32 ImpulseCount = FMath::Min(Impulses.Num(), GFluid2DMaxImpulses);

	// Pack impulses for the shader. Velocity is derived from the segment the point covered
	// this frame, in aspect-corrected simulation space.
	auto FillSplatParams = [&](FFluid2DSplatPS::FParameters* P, FRDGTextureRef Source, FRDGTextureRef Target)
	{
		P->SourceTex = Source;
		P->BilinearClampSampler = Bilinear;
		P->InvSimResolution = InvSim;
		P->AspectRatio = Aspect;
		P->SimWidth = SimWidthF;
		P->ImpulseCount = ImpulseCount;
		for (int32 i = 0; i < ImpulseCount; ++i)
		{
			const FFluid2DImpulse& Imp = Impulses[i];
			const FVector2f PrevSim(Imp.PrevUV.X * Aspect, Imp.PrevUV.Y);
			const FVector2f CurrSim(Imp.CurrUV.X * Aspect, Imp.CurrUV.Y);
			const FVector2f Vel = (CurrSim - PrevSim) / FMath::Max(Dt, 0.0001f);

			P->ImpulseSegment[i] = FVector4f(CurrSim.X, CurrSim.Y, PrevSim.X, PrevSim.Y);
			P->ImpulseVelocityAndRadius[i] = FVector4f(Vel.X, Vel.Y, Imp.Radius, 0.0f);
			P->ImpulseDye[i] = FVector4f(Imp.Dye.R, Imp.Dye.G, Imp.Dye.B, Imp.Dye.A);
		}
		P->RenderTargets[0] = FRenderTargetBinding(Target, ERenderTargetLoadAction::ENoAction);
	};

	const int32 ExplosionCount = FMath::Min(Explosions.Num(), GFluid2DMaxExplosions);

	auto FillExplosionParams = [&](FFluid2DExplosionPS::FParameters* P, FRDGTextureRef Source, FRDGTextureRef Target)
	{
		P->SourceTex = Source;
		P->BilinearClampSampler = Bilinear;
		P->InvSimResolution = InvSim;
		P->AspectRatio = Aspect;
		P->ExplosionCount = ExplosionCount;
		for (int32 i = 0; i < ExplosionCount; ++i)
		{
			const FFluid2DExplosion& E = Explosions[i];
			P->ExplosionCenterRadius[i] = FVector4f(E.CenterUV.X * Aspect, E.CenterUV.Y, FMath::Max(E.Radius, 0.001f), E.Strength);
			P->ExplosionNoise[i] = FVector4f(E.Seed, FMath::Clamp(E.Irregularity, 0.0f, 1.0f), 0.0f, 0.0f);
			P->ExplosionDyeColor[i] = FVector4f(E.Dye.R, E.Dye.G, E.Dye.B, E.Dye.A);
		}
		P->RenderTargets[0] = FRenderTargetBinding(Target, ERenderTargetLoadAction::ENoAction);
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////

	// Advect the velocity field through itself.
	{
		TShaderMapRef<FFluid2DAdvectPS> PS(ShaderMap);
		FFluid2DAdvectPS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DAdvectPS::FParameters>();
		P->VelocityTex = Velocity[VelocityIdx];
		P->SourceTex = Velocity[VelocityIdx];
		P->BilinearClampSampler = Bilinear;
		P->InvSimResolution = InvSim;
		P->DeltaTime = Dt;
		// Idle frames skip the velocity splat pass
		P->Dissipation = CVarFluid2DVelocityDissipation.GetValueOnRenderThread()
			* (ImpulseCount > 0 ? 1.0f : 0.999f);
		P->RenderTargets[0] = FRenderTargetBinding(Velocity[1 - VelocityIdx], ERenderTargetLoadAction::ENoAction);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Fluid2D AdvectVelocity"), PS, P, SimRect);
		VelocityIdx = 1 - VelocityIdx;
	}

	// Advect the dye field through the velocity field.
	{
		TShaderMapRef<FFluid2DAdvectPS> PS(ShaderMap);
		FFluid2DAdvectPS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DAdvectPS::FParameters>();
		P->VelocityTex = Velocity[VelocityIdx];
		P->SourceTex = Dye[DyeIdx];
		P->BilinearClampSampler = Bilinear;
		P->InvSimResolution = InvSim;
		P->DeltaTime = Dt;
		P->Dissipation = CVarFluid2DDyeDissipation.GetValueOnRenderThread();
		P->RenderTargets[0] = FRenderTargetBinding(Dye[1 - DyeIdx], ERenderTargetLoadAction::ENoAction);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Fluid2D AdvectDye"), PS, P, SimRect);
		DyeIdx = 1 - DyeIdx;
	}

	if (ImpulseCount > 0)
	{
		FFluid2DSplatPS::FPermutationDomain Permutation;
		Permutation.Set<FFluid2DSplatPS::FDyeMode>(false);
		TShaderMapRef<FFluid2DSplatPS> PS(ShaderMap, Permutation);
		FFluid2DSplatPS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DSplatPS::FParameters>();
		FillSplatParams(P, Velocity[VelocityIdx], Velocity[1 - VelocityIdx]);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Fluid2D SplatVelocity"), PS, P, SimRect);
		VelocityIdx = 1 - VelocityIdx;
	}

	if (ImpulseCount > 0)
	{
		FFluid2DSplatPS::FPermutationDomain Permutation;
		Permutation.Set<FFluid2DSplatPS::FDyeMode>(true);
		TShaderMapRef<FFluid2DSplatPS> PS(ShaderMap, Permutation);
		FFluid2DSplatPS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DSplatPS::FParameters>();
		FillSplatParams(P, Dye[DyeIdx], Dye[1 - DyeIdx]);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Fluid2D SplatDye"), PS, P, SimRect);
		DyeIdx = 1 - DyeIdx;
	}

	if (ExplosionCount > 0)
	{
		FFluid2DExplosionPS::FPermutationDomain Permutation;
		Permutation.Set<FFluid2DExplosionPS::FDyeMode>(false);
		TShaderMapRef<FFluid2DExplosionPS> PS(ShaderMap, Permutation);
		FFluid2DExplosionPS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DExplosionPS::FParameters>();
		FillExplosionParams(P, Velocity[VelocityIdx], Velocity[1 - VelocityIdx]);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Fluid2D ExplosionVelocity"), PS, P, SimRect);
		VelocityIdx = 1 - VelocityIdx;
	}

	if (ExplosionCount > 0)
	{
		bool bAnyDye = false;
		for (int32 i = 0; i < ExplosionCount; ++i)
		{
			bAnyDye |= Explosions[i].Dye.A > 0.0f;
		}
		if (bAnyDye)
		{
			FFluid2DExplosionPS::FPermutationDomain Permutation;
			Permutation.Set<FFluid2DExplosionPS::FDyeMode>(true);
			TShaderMapRef<FFluid2DExplosionPS> PS(ShaderMap, Permutation);
			FFluid2DExplosionPS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DExplosionPS::FParameters>();
			FillExplosionParams(P, Dye[DyeIdx], Dye[1 - DyeIdx]);
			FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Fluid2D ExplosionDye"), PS, P, SimRect);
			DyeIdx = 1 - DyeIdx;
		}
	}

	if (CVarFluid2DNoiseForce.GetValueOnRenderThread() != 0)
	{
		TShaderMapRef<FFluid2DNoiseForcePS> PS(ShaderMap);
		FFluid2DNoiseForcePS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DNoiseForcePS::FParameters>();
		P->SourceTex = Velocity[VelocityIdx];
		P->BilinearClampSampler = Bilinear;
		P->InvSimResolution = InvSim;
		P->AspectRatio = Aspect;
		P->NoiseScale = CVarFluid2DNoiseScale.GetValueOnRenderThread();
		P->NoiseSpeed = CVarFluid2DNoiseSpeed.GetValueOnRenderThread();
		P->NoiseStrength = CVarFluid2DNoiseStrength.GetValueOnRenderThread();
		P->Time = View.Family->Time.GetWorldTimeSeconds();
		P->RenderTargets[0] = FRenderTargetBinding(Velocity[1 - VelocityIdx], ERenderTargetLoadAction::ENoAction);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Fluid2D NoiseForce"), PS, P, SimRect);
		VelocityIdx = 1 - VelocityIdx;
	}

	// Shared by the motion force and the dye source pass below.
	const bool bDepthAvailable = SceneDepth != nullptr && CustomDepth != nullptr;
	const bool bMask = CVarFluid2DDepthMask.GetValueOnRenderThread() != 0 && bDepthAvailable;
	FRHISamplerState* PointClamp = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	const FVector2f SceneInvExtent(1.0f / SceneColorCopy->Desc.Extent.X, 1.0f / SceneColorCopy->Desc.Extent.Y);
	const FVector2f SceneUVMin = FVector2f(ViewRect.Min.X, ViewRect.Min.Y) * SceneInvExtent;
	const FVector2f SceneUVScale = FVector2f(ViewRect.Width(), ViewRect.Height()) * SceneInvExtent;

	{
		const bool bMotionMasked = CVarFluid2DMotionForceMasked.GetValueOnRenderThread() != 0;
		const bool bMotion = CVarFluid2DMotionForce.GetValueOnRenderThread() != 0
			&& SceneVelocity != nullptr
			&& (!bMotionMasked || bDepthAvailable);
		if (bMotion)
		{
			TShaderMapRef<FFluid2DMotionForcePS> PS(ShaderMap);
			FFluid2DMotionForcePS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DMotionForcePS::FParameters>();
			P->SourceTex = Velocity[VelocityIdx];
			P->GBufferVelocityTex = SceneVelocity;
			// Depth is not sampled without the mask, but the bindings still have to resolve
			// to a live resource, so scene colour stands in.
			P->SceneDepthTex = bMotionMasked ? SceneDepth : SceneColorCopy;
			P->CustomDepthTex = bMotionMasked ? CustomDepth : SceneColorCopy;
			P->BilinearClampSampler = Bilinear;
			P->PointClampSampler = PointClamp;
			P->InvSimResolution = InvSim;
			P->SceneUVMin = SceneUVMin;
			P->SceneUVScale = SceneUVScale;
			P->MaskDepthBias = CVarFluid2DMaskDepthBias.GetValueOnRenderThread();
			P->MotionUseMask = bMotionMasked ? 1 : 0;
			P->MotionBlend = CVarFluid2DMotionForceBlend.GetValueOnRenderThread();
			P->RawDeltaTime = RawDt;
			P->RenderTargets[0] = FRenderTargetBinding(Velocity[1 - VelocityIdx], ERenderTargetLoadAction::ENoAction);
			FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Fluid2D MotionForce"), PS, P, SimRect);
			VelocityIdx = 1 - VelocityIdx;
		}
	}

	{
		const bool bSceneAccum = CVarFluid2DSceneAccum.GetValueOnRenderThread() != 0;
		// Motion bleed needs the mask, otherwise everything dynamic on screen sprays ink.
		const float MotionInk = (bMask && SceneVelocity != nullptr)
			? CVarFluid2DMotionInkStrength.GetValueOnRenderThread() : 0.0f;

		if (bSceneAccum || bMask)
		{
			TShaderMapRef<FFluid2DAccumulatePS> PS(ShaderMap);
			FFluid2DAccumulatePS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DAccumulatePS::FParameters>();
			P->SourceTex = Dye[DyeIdx];
			P->SceneColorTex = SceneColorCopy;
			P->SceneDepthTex = bMask ? SceneDepth : SceneColorCopy;
			P->CustomDepthTex = bMask ? CustomDepth : SceneColorCopy;
			P->GBufferVelocityTex = (MotionInk > 0.0f) ? SceneVelocity : SceneColorCopy;
			P->BilinearClampSampler = Bilinear;
			P->PointClampSampler = PointClamp;
			P->InvSimResolution = InvSim;
			P->SceneUVMin = SceneUVMin;
			P->SceneUVScale = SceneUVScale;
			P->AccumStrength = bSceneAccum ? CVarFluid2DSceneAccumStrength.GetValueOnRenderThread() : 0.0f;
			P->AccumThreshold = CVarFluid2DSceneAccumThreshold.GetValueOnRenderThread();
			P->MaskEnabled = bMask ? 1 : 0;
			P->MaskDepthBias = CVarFluid2DMaskDepthBias.GetValueOnRenderThread();
			P->MaskInkStrength = CVarFluid2DMaskInkStrength.GetValueOnRenderThread();
			P->MotionInkStrength = MotionInk;
			P->RawDeltaTime = RawDt;
			P->RenderTargets[0] = FRenderTargetBinding(Dye[1 - DyeIdx], ERenderTargetLoadAction::ENoAction);
			FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Fluid2D SceneAccum"), PS, P, SimRect);
			DyeIdx = 1 - DyeIdx;
		}
	}

	{
		const int32 BlurIterations = FMath::Clamp(CVarFluid2DBlurIterations.GetValueOnRenderThread(), 0, 8);
		for (int32 i = 0; i < BlurIterations; ++i)
		{
			TShaderMapRef<FFluid2DBlurPS> PS(ShaderMap);
			FFluid2DBlurPS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DBlurPS::FParameters>();
			P->SourceTex = Dye[DyeIdx];
			P->BilinearClampSampler = Bilinear;
			P->InvSimResolution = InvSim;
			P->RenderTargets[0] = FRenderTargetBinding(Dye[1 - DyeIdx], ERenderTargetLoadAction::ENoAction);
			FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Fluid2D BlurDye"), PS, P, SimRect);
			DyeIdx = 1 - DyeIdx;
		}
	}

	if (CVarFluid2DBrightnessForce.GetValueOnRenderThread() != 0)
	{
		TShaderMapRef<FFluid2DBrightnessForcePS> PS(ShaderMap);
		FFluid2DBrightnessForcePS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DBrightnessForcePS::FParameters>();
		P->SourceTex = Velocity[VelocityIdx];
		P->SceneColorTex = SceneColorCopy;
		P->BilinearClampSampler = Bilinear;
		P->InvSimResolution = InvSim;
		P->SceneUVMin = SceneUVMin;
		P->SceneUVScale = SceneUVScale;
		P->SceneInvExtent = SceneInvExtent;
		P->BrightnessTexelScale = 1.0f;
		P->BrightnessHeightScale = 0.1f;
		P->BrightnessStrength = CVarFluid2DBrightnessStrength.GetValueOnRenderThread();
		P->RenderTargets[0] = FRenderTargetBinding(Velocity[1 - VelocityIdx], ERenderTargetLoadAction::ENoAction);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Fluid2D BrightnessForce"), PS, P, SimRect);
		VelocityIdx = 1 - VelocityIdx;
	}

	// -----------------------------------------------------------------------
	// Pressure solve: divergence, N Jacobi sweeps, then subtract the gradient.
	// The compute path is the default; the pixel path is kept for comparison and makes a
	// straightforward A/B in a GPU capture.
	// -----------------------------------------------------------------------
	const int32 Iterations = FMath::Clamp(CVarFluid2DPressureIterations.GetValueOnRenderThread(), 1, 100);
	FRDGTextureRef Divergence = GraphBuilder.CreateTexture(PressureDesc, TEXT("Fluid2D.Divergence"));
	const FIntPoint SimBounds(SimSize.X - 1, SimSize.Y - 1);

	if (CVarFluid2DComputeSolver.GetValueOnRenderThread() != 0)
	{
		const FIntVector GroupCount8 = FComputeShaderUtils::GetGroupCount(SimSize, 8);

		{
			TShaderMapRef<FFluid2DDivergenceCS> CS(ShaderMap);
			FFluid2DDivergenceCS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DDivergenceCS::FParameters>();
			P->SrcVelocityTex = Velocity[VelocityIdx];
			P->OutDivergenceUAV = GraphBuilder.CreateUAV(Divergence);
			P->SimBounds = SimBounds;
			P->HalfRDX = HalfRDX;
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Fluid2D DivergenceCS"), CS, P, GroupCount8);
		}

		// Last frame's pressure is reused as the initial guess. Each group writes back only
		// its tile interior, so the dispatch grid is sized in interiors rather than tiles.
		const FIntVector PressureGroups(
			FMath::DivideAndRoundUp(SimSize.X, GFluid2DPressureTileInterior),
			FMath::DivideAndRoundUp(SimSize.Y, GFluid2DPressureTileInterior), 1);
		for (int32 Remaining = Iterations; Remaining > 0; Remaining -= GFluid2DPressureItersPerDispatch)
		{
			TShaderMapRef<FFluid2DPressureSolveCS> CS(ShaderMap);
			FFluid2DPressureSolveCS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DPressureSolveCS::FParameters>();
			P->SrcPressureTex = Pressure[PressureIdx];
			P->SrcDivergenceTex = Divergence;
			P->OutPressureUAV = GraphBuilder.CreateUAV(Pressure[1 - PressureIdx]);
			P->SimBounds = SimBounds;
			P->Alpha = PoissonAlpha;
			P->IterationsThisDispatch = FMath::Min(Remaining, GFluid2DPressureItersPerDispatch);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Fluid2D PressureSolveCS"), CS, P, PressureGroups);
			PressureIdx = 1 - PressureIdx;
		}

		{
			TShaderMapRef<FFluid2DGradientSubtractCS> CS(ShaderMap);
			FFluid2DGradientSubtractCS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DGradientSubtractCS::FParameters>();
			P->SrcVelocityTex = Velocity[VelocityIdx];
			P->SrcPressureTex = Pressure[PressureIdx];
			P->OutVelocityUAV = GraphBuilder.CreateUAV(Velocity[1 - VelocityIdx]);
			P->SimBounds = SimBounds;
			P->HalfRDX = HalfRDX;
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Fluid2D GradientSubtractCS"), CS, P, GroupCount8);
			VelocityIdx = 1 - VelocityIdx;
		}
		return;
	}

	// Pixel fallback path.
	{
		TShaderMapRef<FFluid2DDivergencePS> PS(ShaderMap);
		FFluid2DDivergencePS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DDivergencePS::FParameters>();
		P->VelocityTex = Velocity[VelocityIdx];
		P->BilinearClampSampler = Bilinear;
		P->InvSimResolution = InvSim;
		P->HalfRDX = HalfRDX;
		P->RenderTargets[0] = FRenderTargetBinding(Divergence, ERenderTargetLoadAction::ENoAction);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Fluid2D Divergence"), PS, P, SimRect);
	}

	for (int32 i = 0; i < Iterations; ++i)
	{
		TShaderMapRef<FFluid2DPressureSolvePS> PS(ShaderMap);
		FFluid2DPressureSolvePS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DPressureSolvePS::FParameters>();
		P->PressureTex = Pressure[PressureIdx];
		P->DivergenceTex = Divergence;
		P->BilinearClampSampler = Bilinear;
		P->InvSimResolution = InvSim;
		P->Alpha = PoissonAlpha;
		P->RenderTargets[0] = FRenderTargetBinding(Pressure[1 - PressureIdx], ERenderTargetLoadAction::ENoAction);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Fluid2D PressureSolve"), PS, P, SimRect);
		PressureIdx = 1 - PressureIdx;
	}

	{
		TShaderMapRef<FFluid2DGradientSubtractPS> PS(ShaderMap);
		FFluid2DGradientSubtractPS::FParameters* P = GraphBuilder.AllocParameters<FFluid2DGradientSubtractPS::FParameters>();
		P->PressureTex = Pressure[PressureIdx];
		P->VelocityTex = Velocity[VelocityIdx];
		P->BilinearClampSampler = Bilinear;
		P->InvSimResolution = InvSim;
		P->HalfRDX = HalfRDX;
		P->RenderTargets[0] = FRenderTargetBinding(Velocity[1 - VelocityIdx], ERenderTargetLoadAction::ENoAction);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Fluid2D GradientSubtract"), PS, P, SimRect);
		VelocityIdx = 1 - VelocityIdx;
	}
}
