# FatTracker — Sample Editor Plan
> Executed inline (executing-plans), TDD per task. Spec: docs/superpowers/specs/2026-11-05-fattracker-sample-editor-design.md
## Global Constraints
- Files only inside C:\development\FatTracker; no exceptions / RTTI; core without Windows headers.
## Review Focus
- Empty sample / no selection / selection past the end for every tool.
- 8-bit vs 16-bit samples (clipping, bit depth kept).
- Loops after resample / mix / echo stay inside the sample.
- Recording with no input device or a device that disappears.
- Name editing while playing; keys that also mean notes must not leak into the pattern.
### Task 1: core/sample_edit tools (resample, tune/detect pitch, mix, low/high pass, echo, boost, draw) + tests/test_sample_tools.cpp
### Task 2: sampler UI: third button row, DRAW mode
### Task 3: audio/capture (WASAPI) + SAMPLE button; recording -> Sample helper tested
### Task 4: name editing (core/text_edit helper tested) + DISK OP. RENAME
### Task 5: review, fixes, merge
