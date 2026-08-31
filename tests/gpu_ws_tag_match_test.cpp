#include "gpu_ws_tag_match.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

int main() {
    constexpr std::uint32_t kStamp = 100u;
    const std::array<std::uint32_t, 9> original{{
        0x2C808080u, 0x00120012u, 0x00400020u,
        0x00120052u, 0x00800060u, 0x00520012u,
        0x00C00020u, 0x00520052u, 0x00FF0060u,
    }};
    const std::uint32_t signature =
        psx_ws_ft4_signature_words(original.data());

    expect(psx_ws_tag_match_result(
               kStamp, kStamp, 1, signature, original.data()) ==
               PSX_WS_TAG_MATCH,
           "strict tag matches in its construction frame");
    expect(psx_ws_tag_match_result(
               kStamp + 2u, kStamp, 1, signature, original.data()) ==
               PSX_WS_TAG_MATCH,
           "strict tag survives the reviewed two-VBlank DMA window");
    expect(psx_ws_tag_match_result(
               kStamp + 3u, kStamp, 1, signature, original.data()) ==
               PSX_WS_TAG_EXPIRED,
           "matching content cannot extend a tag beyond its TTL");

    for (std::size_t i = 0; i < original.size(); ++i) {
        auto reused = original;
        reused[i] ^= 0x01010101u;
        expect(psx_ws_tag_match_result(
                   kStamp + 1u, kStamp, 1, signature, reused.data()) ==
                   PSX_WS_TAG_CONTENT_MISMATCH,
               "changing any completed FT4 word rejects packet reuse");
    }

    auto reused = original;
    reused[7] ^= 1u;
    expect(psx_ws_tag_match_result(
               kStamp + 2u, kStamp, 0, 0u, reused.data()) ==
               PSX_WS_TAG_MATCH,
           "legacy provenance retains address-and-age behavior");

    if (g_failures != 0) return 1;
    std::cout << "GPU widescreen tag matching tests passed\n";
    return 0;
}
