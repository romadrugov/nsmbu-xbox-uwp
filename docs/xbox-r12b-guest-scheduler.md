# R12B — real Xbox game bootstrap: cooperative guest scheduling

## Evidence from first owner Xbox R12 real-game run

- Own USA_V0 `code/red-pro2.rpx` loaded at `02AA2E50`, matching
  generated recompiler code.
- Dozen x64 Vulkan 1.2.354 on Xbox's Direct3D12 SraKmd_arden,
  VK_KHR_dynamic_rendering, swapchain mailbox with two images, GX2 initialized.
- Render thread pool and audio guest threads created.
- After the first seconds: `[heap] frame allocator waits for its buffer`.
- After 10 seconds, `AudioDrcVsCtrlSubCoreThread` and
  `RenderObjParallelExecuterThread` waited for guest simulated core 0,
  held by `AudioLaunchThread` (last recorded **link register** 029931AC).
- **No real game-generated frame observed.** Reported `pc` in older scheduler
  message is actually `cpu.lr`, so do not infer an exact instruction from it.

## Test hypothesis and source change

In `runtime/include/ppc.h`, `PPC_ENTER` checks `g_core_preempt`,
but the emitted recompiler `PPC_LOOP()` macro was a no-op by default.
This means a guest thread in a tight internal back-edge loop may ignore
a pending simulated-core preemption request until it calls another
function. That's a candidate explanation for the starved render/audio
threads and resulting frame allocator stall.

R12B enables `NSMBU_R12B_COOPERATIVE_LOOPS` **only** on actual
owner-private generated game-code C translation units, via the
CMake `-DR12B_COOPERATIVE_GUEST_LOOPS=ON` switch.

`PPC_LOOP()` then tests the core's preemption flag and calls the
same existing `ppc_preempt(c)` path used by `PPC_ENTER`. It does not
change the guest registers, invent frames, disable sound or bypass
Cafe OS. The desktop and R7D builds do not enable this flag.

R12B also sets `NSMBU_THREAD_DUMP=5` and
`NSMBU_SCHED_STATS=2` before starting the guest scheduler to
provide per-thread diagnostics every 5 seconds if the black screen
continues.

## Preserving the owner's installed game

- Stable installed R12 package **identity and publisher stay identical**;
  app version increases from `0.1.0.0` to `0.1.0.1`.
- Update the R12 application in Xbox Device Portal. **Do not
  uninstall the existing package**: uninstalling may remove the
  complete game assets already uploaded into its `LocalState/game`.
- If Xbox refuses a certificate/upgrade, capture exact install error
  and don't delete the older package or its data.
- The owner game assets stay on the Xbox; no source files or
  proprietary game resources go to public CI. Game-derived binary is
  distributed privately to owner through a short-lived artifact.

## Hardware validation

Xbox Series S Developer Mode, already populated R12 LocalState/game:

1. Install the higher-version private signed AppX update.
2. Launch, watch for actual NSMBU visuals (not a synthetic clear).
3. Leave running 15–20 seconds if black, then grab the latest
   `LocalState/logs/nsmbu.log`.
4. Check `[threads]` and `[sched]` diagnostics. If still blocked,
   distinguish core ownership from a genuine wait on the game's
   ring buffer and investigate the next stuck guest function.

A clean CI link/package proves only that the scheduler change compiles.
Gameplay is **not** declared working until a real console frame.
