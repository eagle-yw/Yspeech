# Pipeline Runtime Refactor

## Scope

This document tracks the runtime refactor that unified streaming execution on the Taskflow pipeline model.

## Current Status

### Phase 1: Data Model
- Added `PipelineToken`
- Added `SegmentState`
- Added `SegmentRegistry`
- Added `RuntimeContext`
- Added `RuntimeEventRecord`

### Phase 2: Taskflow Skeleton
- Added `PipelineExecutor`
- Added `PipelineBuilderConfig`
- Added `PipelineRuntimeRecipe`
- Added a three-pipe skeleton:
  - `SourcePipe`
  - `VadPipe`
  - `FeaturePipe`
  - `AsrPipe`
  - `EventPipe`
- Wired the new modules into the build and public umbrella module

### Config-Driven Design Kept
- `tf::Pipeline` remains a fixed execution kernel
- configuration is preserved through `PipelineRuntimeRecipe`
- `PipelineBuilderConfig` now derives a recipe from `PipelineConfig`
- the builder decides which stage roles are active from config
- stage callbacks are bound by role instead of hardcoded directly into runtime logic
- stage-level DAG dependencies are now preserved in the recipe through `depends_on`, `downstream_ids`, `node_kind`, and `join_policy`

### Phase 3: First Adapter Boundary
- Added `VadStage` adapter skeleton
- Established the runtime-facing contract:
  - input: `PipelineToken`
  - shared state: `RuntimeContext`, `SegmentRegistry`
  - output: updated `segment_id` and `SegmentState`

## Implemented

1. `VadStage` now uses `SileroVadCore`.
2. `FeatureStage` now consumes `SegmentState.audio_accumulated`.
3. `AsrStage` now consumes `SegmentState.features_accumulated`.
4. `EngineRuntime` now uses the Taskflow runtime as the default streaming path.
5. Runtime recipe tests and real-engine taskflow smoke tests are in place for both the short sample and a longer mixed-language sample.
6. Partial-result triggering is no longer segment-final-only:
   - the first partial is allowed as soon as the first usable feature window is available
   - later partials still respect `min_new_feature_frames`
   - EOS now force-closes any active segment so the final segment is not dropped
7. Stage/core timing from the Taskflow path is now reported into core-level performance stats.
8. Taskflow stage-owned cores are protected against concurrent access to keep multi-run benchmarks stable.
9. Recipe/build time preserves stage-level DAG metadata so the runtime can execute a static configurable DAG without losing configuration-driven construction.
10. `RuntimeDagExecutor` now supports `Branch`, `Join`, `join_policy`, and lightweight `join_timeout_ms`.

## Validation Snapshot

### Short Audio Benchmark
- Audio: `<短音频样本>`
- Runs: `5`
- This sample remains part of the verification set for the current Taskflow streaming baseline.

### Longer Audio Benchmark
- Audio: `<长音频样本>`
- Runs: `3`
- The longer mixed-language sample remains part of the smoke/benchmark set used to verify the current Taskflow runtime.

## Current Focus

1. Keep the single-line Taskflow ASR path stable and easy to verify from shipped example configs.
2. Keep the static DAG layer minimal so Taskflow continues to own linear execution.
3. Use tests and example configs to validate design assumptions before expanding semantics further.
