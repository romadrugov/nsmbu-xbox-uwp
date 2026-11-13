# R12 — real game AppX installation and Xbox Series S test

Status: R11 linked a genuine 53,103,616-byte WindowsStore x64 executable. R9 independently demonstrated a Vulkan/Dozen GPU frame on the owner's Series S. R12 combines both into a signed, PRIVATE AppX. **A real NSMBU game frame is not verified.**

## Sources and security

- Public port source: feat/r12-private-installable-game-appx-20261113
- Private CI workflow: romadrugov/mkwii-xbox-build/.github/workflows/nsmbu-r12-full-game-appx.yml
- Dozen/DXIL: pinned R9 run 43433286834, already tested on owner's Xbox.
- New package identity: Romadrugov.NSMBUFullRuntimeR12 (R7D and R9 unchanged).
- Nintendo RPX, code/content/meta and generated C files are NEVER uploaded publicly.
- Private release artifact contains only the signed AppX with privately recompiled game code, certificate, UWP VCLibs and a build report. Game assets are not included.

## Install from Mac without Windows

Needs macOS with Python 3.10+ and GitHub CLI, plus Xbox Series S in Developer Mode on the same trusted local network with Device Portal HTTPS enabled.

After R12 succeeds, download the private artifact (replace RUN_ID with the real number):

    gh run download RUN_ID --repo romadrugov/mkwii-xbox-build --name NSMBU_R12_REAL_GAME_INSTALL_PRIVATE --dir "$HOME/Downloads/nsmbu-r12"

From the public source checkout, install the three resulting files (replace the Xbox IP, portal username and paths):

    python3 tools/xbox/r12_portal_mac.py --host 192.168.1.100 --user DevPortal install --appx "$HOME/Downloads/nsmbu-r12/NSMBUFullRuntime_R12_0.1.0.0_x64.appx" --vclibs "$HOME/Downloads/nsmbu-r12/Microsoft.VCLibs.x64.14.00.appx" --cer "$HOME/Downloads/nsmbu-r12/NSMBUFullRuntimeR12.cer"

The script prompts for the portal password; nothing is stored. The self-signed HTTPS certificate is accepted for this explicitly chosen local console only.

Upload a legitimately obtained, unpacked game folder whose direct children are code, content and meta:

    python3 tools/xbox/r12_portal_mac.py --host 192.168.1.100 --user DevPortal upload-game --game "$HOME/Games/NSMBU"

The script creates multiple <=80 MiB uncompressed-input ZIP batches and uploads them using the same Device Portal LocalState extraction API as Smash4 Xbox. No game files go to GitHub. Individual files larger than the Device Portal upload limit may need separate handling.

Expected Xbox filesystem:

    LocalState/game/code/red-pro2.rpx
    LocalState/game/content/Common/course_res_pack/1-1.szs
    LocalState/game/meta/meta.xml
    LocalState/logs/nsmbu.log
    LocalState/nsmbu/save/

Inspect, launch, and fetch the log:

    python3 tools/xbox/r12_portal_mac.py --host 192.168.1.100 --user DevPortal list --path '\LocalState\game'
    python3 tools/xbox/r12_portal_mac.py --host 192.168.1.100 --user DevPortal launch
    python3 tools/xbox/r12_portal_mac.py --host 192.168.1.100 --user DevPortal download-log --out nsmbu_r12.log

The R12 WinRT LocalState C++/CX bridge supplies the sandbox path to the game, logs and save directories. The bridge is not console-verified. If activation fails before the game main(), the log may not exist; consult Device Portal diagnostics.

## Device acceptance

Host: Xbox Series S, Developer Mode.
Place: separate R12 UWP package with owner full game under LocalState/game.
Actions: launch, capture actual screen, read logs, check RPX entrypoint match and renderer output.
Pass: first genuine NSMBU-generated GX2 frame through real Vulkan/Dozen/D3D12. A green CI run, successful installation, synthetic clear or title picture file does NOT count.

Current risks: the full R11 runtime commits 4 GiB of virtual guest memory; UWP memory limits may reject it. The experimental sparse-memory branch uses an exception API not exposed by the Store compiler and must not be blindly applied. Instruction-site PPC hooks remain incomplete. Fix based on actual Xbox diagnostics.
