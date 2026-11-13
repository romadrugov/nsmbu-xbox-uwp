#!/usr/bin/env python3
"""Add an owner-private Xbox LocalState path bridge to the pinned SDL3 WinRT fork.

This patches only an ephemeral open-source SDL3 checkout. The exported
plain-C function is linked by the real NSMBU runtime, so it can use the
actual package LocalState folder instead of guessing %LOCALAPPDATA%.
"""
from __future__ import annotations
import argparse
from pathlib import Path

ANCHOR = "SDL_WinRTApp ^ SDL_WinRTGlobalApp = nullptr;"
BRIDGE = r'''

// R12 AppContainer bridge: expose the actual WinRT sandbox path to the
// clang-cl game executable. The game stores logs/saves under LocalState and
// loads the owner's uploaded game/ directory from the same folder.
extern "C" void __cdecl NSMBU_R12_GetLocalStatePath(wchar_t* target, unsigned capacity)
{
    if (!target || capacity < 2) return;
    target[0] = 0;
    auto data = Windows::Storage::ApplicationData::Current;
    if (!data || !data->LocalFolder || !data->LocalFolder->Path) return;
    const wchar_t* src = data->LocalFolder->Path->Data();
    if (!src) return;
    unsigned i = 0;
    while (i + 1 < capacity && src[i]) {
        target[i] = src[i];
        ++i;
    }
    target[i] = 0;
}
'''

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--sdl-root", type=Path, required=True)
    args = ap.parse_args()
    p = args.sdl_root / "src/core/winrt/SDL_winrtapp_direct3d.cpp"
    source = p.read_text(encoding="utf-8")
    if source.count(ANCHOR) != 1 or "NSMBU_R12_GetLocalStatePath" in source:
        raise SystemExit("Pinned WinRT source changed or R12 bridge already applied")
    p.write_text(source.replace(ANCHOR, ANCHOR + BRIDGE, 1), encoding="utf-8")
    print("R12: real Windows.Storage.ApplicationData LocalState path bridge added")

if __name__ == "__main__":
    main()
