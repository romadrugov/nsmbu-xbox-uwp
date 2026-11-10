// Build/link only. Never run directly as an Xbox executable.
extern "C" int nsmbu_r2_uwp_link_probe();

namespace NSMBUR2UwpLinkProbe {
public ref class MetadataMarker sealed {
public:
    MetadataMarker() {}
};
}

int main() {
    return nsmbu_r2_uwp_link_probe();
}
