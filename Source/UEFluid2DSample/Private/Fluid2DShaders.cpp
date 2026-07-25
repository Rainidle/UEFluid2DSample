#include "Fluid2DShaders.h"

IMPLEMENT_GLOBAL_SHADER(FFluid2DAdvectPS,           "/UEFluid2DSample/Fluid2D.usf", "AdvectPS",           SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FFluid2DSplatPS,            "/UEFluid2DSample/Fluid2D.usf", "SplatPS",            SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FFluid2DExplosionPS,        "/UEFluid2DSample/Fluid2D.usf", "ExplosionPS",        SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FFluid2DBrightnessForcePS,  "/UEFluid2DSample/Fluid2D.usf", "BrightnessForcePS",  SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FFluid2DNoiseForcePS,       "/UEFluid2DSample/Fluid2D.usf", "NoiseForcePS",       SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FFluid2DMotionForcePS,      "/UEFluid2DSample/Fluid2D.usf", "MotionForcePS",      SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FFluid2DAccumulatePS,       "/UEFluid2DSample/Fluid2D.usf", "AccumulatePS",       SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FFluid2DBlurPS,             "/UEFluid2DSample/Fluid2D.usf", "BlurPS",             SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FFluid2DDivergencePS,       "/UEFluid2DSample/Fluid2D.usf", "DivergencePS",       SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FFluid2DPressureSolvePS,    "/UEFluid2DSample/Fluid2D.usf", "PressureSolvePS",    SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FFluid2DGradientSubtractPS, "/UEFluid2DSample/Fluid2D.usf", "GradientSubtractPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FFluid2DCompositePS,        "/UEFluid2DSample/Fluid2D.usf", "CompositePS",        SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FFluid2DDivergenceCS,       "/UEFluid2DSample/Fluid2DSolver.usf", "DivergenceCS",       SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFluid2DPressureSolveCS,    "/UEFluid2DSample/Fluid2DSolver.usf", "PressureSolveCS",    SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFluid2DGradientSubtractCS, "/UEFluid2DSample/Fluid2DSolver.usf", "GradientSubtractCS", SF_Compute);
