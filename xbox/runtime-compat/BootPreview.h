#pragma once
// R5F: optional local-only visual screenshot preview for the owner Xbox.
// Does not execute guest/game code or embed proprietary assets.
#include <windows.h>
#include <memory>

namespace nsmbuvisual {
using Logger = void(*)(const char*) noexcept;

class Preview final {
public:
    Preview(IUnknown* window, const wchar_t* localPngPath, Logger log);
    ~Preview();
    Preview(const Preview&) = delete;
    Preview& operator=(const Preview&) = delete;
    void present();
    bool loaded_local_image() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nsmbuvisual
