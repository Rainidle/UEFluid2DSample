# UEFluid2DSample

A screen-space 2D fluid simulation for Unreal Engine, implemented as a post process. 
(Stam, "Stable Fluids"; GPU Gems ch. 38).

The plugin hooks the renderer through `FSceneViewExtension` and builds every pass with the
Render Dependency Graph, so it needs no post process material, no render target assets and
no scene setup beyond enabling the plugin.

## Features

- Full Stable Fluids pipeline: semi-Lagrangian advection, divergence, Jacobi pressure
  solve and gradient projection, with free-slip velocity and Neumann pressure boundaries.
- Compute pressure solver that performs several Jacobi sweeps per dispatch in group shared
  memory, with the equivalent pixel-shader path kept for comparison.
- Interaction from Blueprints or C++: drag impulses in viewport UV or world space, and
  procedural radial explosions.
- Automatic wakes from moving actors via a drop-in scene component.
- Per-pixel forcing from the engine velocity buffer, so skinned characters stir the fluid
  limb by limb even when their origin never moves.
- Custom Depth mask to choose exactly which actors bleed dye into the field, and to keep
  those actors readable in the final composite.
- Procedural curl noise ambience, scene highlight bleed, dye blur and a cosine colour
  palette, all runtime switchable.

## Requirements

- Unreal Engine 5.6 or newer. (test on UE5.8)
- A Shader Model 5 capable RHI. Lower feature levels are excluded at compile time.
- Optional: the project setting **Custom Depth-Stencil Pass** must be enabled for the
  depth mask and for masked motion forcing.

## Quick start

Run `r.Fluid2DSample.DemoEmitter 1` in the console. A colour-cycling plume rises from the
bottom of the screen and the velocity field starts refracting the image around it, which
confirms the whole chain is running.

For an interaction test, `Fluid2DSample.TestExplosion` detonates a burst at the centre of
the viewport. It accepts optional radius, strength and irregularity arguments.

Left-clicking detonates a burst at the clicked screen location.

`AddExplosion` takes viewport UV, `AddWorldExplosion` takes a world position and is the one
to hook up to impact or detonation events. The pattern is generated from a radial shockwave
profile modulated by value noise seeded per blast, so no texture assets are involved and
the result stays sharp at any simulation resolution.

Every position in this API is a viewport UV, never a pixel coordinate. Use
`Fluid2DSubsystem::GetMouseViewportUV` to obtain one, or divide the player controller's
`GetMousePosition` by the viewport size.

### Characters driving the fluid

Enable **Render CustomDepth** on the character's mesh. The motion vector force is on by
default and restricted to the mask, so those pixels immediately start stirring the field
using their real screen motion from the engine velocity buffer. Every limb contributes its
own direction, and rotations register even when the actor's origin is static.

A good starting combination is `r.Fluid2DSample.DepthMask 1` with
`r.Fluid2DSample.Palette 1`: the silhouette bleeds dye, fast-moving parts spray more of it
(`MotionInkStrength`), and limb motion drives the flow. Skeletal meshes write velocity by
default; if the character's material animates vertices with World Position Offset, confirm
`r.Velocity.EnableVertexDeformation` has not been disabled.

## Console variables

All variables are render-thread safe and can be changed while running. Type
`r.Fluid2DSample.` in the console to list them.

| Variable | Default | Description |
|---|---|---|
| `r.Fluid2DSample.Enable` | 1 | Master switch |
| `r.Fluid2DSample.SimWidth` | 768 | Maximum simulation grid width; height follows the viewport aspect |
| `r.Fluid2DSample.PressureIterations` | 8 | Jacobi iterations per frame |
| `r.Fluid2DSample.ComputeSolver` | 1 | Solver chain as compute shaders; 0 uses the pixel path |
| `r.Fluid2DSample.DemoEmitter` | 0 | Built-in demo plume |
| `r.Fluid2DSample.NoiseForce` | 1 | Procedural curl noise force |
| `r.Fluid2DSample.NoiseStrength` | 8.0 | Curl noise strength, cells per second |
| `r.Fluid2DSample.NoiseScale` | 90.0 | Noise repetitions across the screen; higher means smaller vortices |
| `r.Fluid2DSample.NoiseSpeed` | 5.0 | How fast the noise field evolves |
| `r.Fluid2DSample.MotionForce` | 1 | Per-pixel forcing from the engine velocity buffer |
| `r.Fluid2DSample.MotionForceBlend` | 0.5 | How far the fluid is blended towards measured pixel motion |
| `r.Fluid2DSample.MotionForceMasked` | 1 | Restrict motion forcing to Render CustomDepth actors |
| `r.Fluid2DSample.MotionInkStrength` | 1.5 | Dye bleed proportional to pixel speed; needs `DepthMask 1` |
| `r.Fluid2DSample.DepthMask` | 1 | Restrict dye bleed to unoccluded Render CustomDepth actors |
| `r.Fluid2DSample.MaskInkStrength` | 0.5 | Dye bleed strength for masked silhouettes |
| `r.Fluid2DSample.MaskDepthBias` | 0.0005 | Device-Z tolerance for the mask comparison |
| `r.Fluid2DSample.MaskShowModel` | 0.6 | Keep masked pixels free of dye and refraction, 0-1 |
| `r.Fluid2DSample.SceneAccum` | 0 | Bleed scene highlights into the dye field |
| `r.Fluid2DSample.SceneAccumStrength` | 0.03 | Highlight bleed strength per frame |
| `r.Fluid2DSample.SceneAccumThreshold` | 1.0 | Linear HDR luminance above which highlights bleed |
| `r.Fluid2DSample.BrightnessForce` | 0 | Force from scene luminance treated as a height field |
| `r.Fluid2DSample.BrightnessStrength` | 0.02 | Screen brightness force strength |
| `r.Fluid2DSample.Palette` | 0 | Composite mode: 0 adds dye RGB, 1 maps density through a cosine palette |
| `r.Fluid2DSample.BlurIterations` | 1 | 3x3 Gaussian passes over the dye field |
| `r.Fluid2DSample.VelocityDissipation` | 0.999 | Velocity decay during advection |
| `r.Fluid2DSample.DyeDissipation` | 0.96 | Dye decay during advection; lower fades faster |
| `r.Fluid2DSample.Distortion` | 0.04 | Refraction strength, weighted by dye density |
| `r.Fluid2DSample.DyeIntensity` | 0.7 | Brightness of the dye overlay |

Every pass is named, so `stat GPU` and GPU captures both group the work under
`Fluid2DSample`.

## How it works

The game thread only queues intents. `UFluid2DSubsystem` pushes impulses and explosions
onto lock-protected queues, and `UFluid2DVelocityComponent` feeds the same queues from its
tick. Everything else happens on the render thread inside
`FFluid2DViewExtension::PrePostProcessPass_RenderThread`.

```
Copy the view rect out of scene colour        (the composite reads and writes scene colour)
Advance the simulation, once per frame:
  AdvectVelocity      semi-Lagrangian self-advection, with dissipation
  AdvectDye           dye carried by the velocity field
  SplatVelocity       segment impulses, if any were queued
  SplatDye
  ExplosionVelocity   radial bursts, if any were queued
  ExplosionDye
  NoiseForce          curl noise                                          (optional)
  MotionForce         engine velocity buffer                              (optional)
  SceneAccum          highlight, silhouette and motion dye bleed          (optional)
  BlurDye x N         3x3 Gaussian                                        (optional)
  BrightnessForce     luminance height field                              (optional)
  Divergence          free-slip boundaries
  PressureSolve       Jacobi, ceil(N/4) dispatches on the compute path
  GradientSubtract    projection to a divergence-free field
Composite                                     refract scene colour, overlay dye, write back
```

Velocity, pressure and dye are ping-pong pairs. Graph resources only live for one frame, so
the pairs are held as pooled render targets and re-registered every frame. Resizing the
viewport reallocates and clears them.

Velocity is stored as `PF_G16R16F`, pressure and divergence as `PF_R16F`, dye as
`PF_FloatRGBA`. Velocity is expressed in simulation cells per second, and the grid height
is derived from the viewport aspect ratio so cells stay square on screen.

**Compute pressure solver.** On the pixel path every Jacobi sweep is a fullscreen render
pass, paying a render target switch and a rasterised triangle each time. The compute path
loads a 32x32 tile with four halo rings into group shared memory and iterates four times
before writing back its 24x24 interior, so eight iterations cost two dispatches instead of
eight passes. Divergence and projection are compute kernels too. The tradeoff is that halo
values come from the previous dispatch, so information crosses tile borders one dispatch
later than it would per-pass; both paths converge on the same fixed point and are visually
indistinguishable. `r.Fluid2DSample.ComputeSolver 0` switches back for comparison.

## Limitations

- The dye field is not reprojected when the camera moves, so ink stays put in screen space
  rather than sticking to the world.
- The depth mask treats any pixel with custom depth as included. If Custom Depth is already
  used by another effect in your project, extend the mask to filter on Custom Stencil.
- Both the mask and the motion vectors are point-sampled at simulation resolution, so thin
  fast-moving geometry can be missed at low `SimWidth`.
- The composite runs before tonemapping, on linear HDR scene colour, which is worth keeping
  in mind when tuning the strength parameters.

