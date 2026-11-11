// Link-time UWP x64 ABI check: calls the clang-cl bridge to genuine
// upstream Cafe OS coreinit_mem.cpp HLE functions. No game binary is used.
extern "C" int nsmbu_runtime_hle_link_probe();
extern "C" int nsmbu_public_guest_hle_link_probe();

namespace NSMBURuntimeHleLinkProbe {
public ref class MetadataMarker sealed {
public:
    MetadataMarker() {}
};
}

int main() {
    const int hle = nsmbu_runtime_hle_link_probe();
    return hle ? hle : nsmbu_public_guest_hle_link_probe();
}
