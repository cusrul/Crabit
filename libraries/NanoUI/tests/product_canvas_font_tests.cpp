#include <hal/canvas.h>
#include <NanoUI/ports/legacy_canvas.h>
#include <NanoUI/ports/legacy_bitmap_font.h>
#include <NanoUI/widgets/text.h>
#include <NanoUI/framework/build_owner.h>
#include <windows/heap_allocator.h>
#include <windows/raster_canvas.h>
#include <cstdio>
#include <cstring>
#include <string>
namespace {
int checks = 0, failures = 0;
#define CHECK(v)                                   \
    do {                                           \
        ++checks;                                  \
        if (!(v)) {                                \
            ++failures;                            \
            std::printf("%d: %s\n", __LINE__, #v); \
        }                                          \
    } while (false)
class MemoryCanvas final : public Canvas {
   public:
    MemoryCanvas() { resetClipRect(); }
    void clear() override {
        ++clears;
        std::memset(pixels, 0, sizeof pixels);
    }
    void display() override { ++displays; }
    int16_t width() override { return 128; }
    int16_t height() override { return 96; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x >= 0 && x < 128 && y >= 0 && y < 96) {
            if (color == C_INVERT)
                pixels[y * 128 + x] ^= 1;
            else
                pixels[y * 128 + x] = color == C_WHITE;
        }
    }
    void resetPixels() { std::memset(pixels, 0, sizeof pixels); }
    std::uint8_t pixels[128 * 96]{};
    int clears = 0, displays = 0;
};
void encode(std::uint32_t u, char* text) {
    if (u < 128) {
        text[0] = static_cast<char>(u);
        text[1] = 0;
    } else if (u < 0x800) {
        text[0] = static_cast<char>(0xC0 | (u >> 6));
        text[1] = static_cast<char>(0x80 | (u & 63));
        text[2] = 0;
    } else if (u < 0x10000) {
        text[0] = static_cast<char>(0xE0 | (u >> 12));
        text[1] = static_cast<char>(0x80 | ((u >> 6) & 63));
        text[2] = static_cast<char>(0x80 | (u & 63));
        text[3] = 0;
    } else {
        text[0] = static_cast<char>(0xF0 | (u >> 18));
        text[1] = static_cast<char>(0x80 | ((u >> 12) & 63));
        text[2] = static_cast<char>(0x80 | ((u >> 6) & 63));
        text[3] = static_cast<char>(0x80 | (u & 63));
        text[4] = 0;
    }
}
void fontParity() {
    MemoryCanvas original, adapted;
    NanoUI::LegacyCanvasBridge<MemoryCanvas> bridge(adapted);
    unsigned mismatch = 0, widthMismatch = 0;
    for (unsigned i = 0; i < cn_glyph_count; ++i) {
        const auto& entry = cn_glyph_index[i];
        char text[5]{};
        encode(entry.unicode, text);
        NanoUI::LegacyBitmapFont font(font5x7, get_chinese_glyph, entry.font_id);
        original.resetPixels();
        adapted.resetPixels();
        original.drawText(2, 3, text, entry.size, false, entry.font_id);
        font.paint(bridge, NanoUI::Offset(2, 3), text, entry.size, 0xFFFFFFFFu);
        mismatch += std::memcmp(original.pixels, adapted.pixels, sizeof original.pixels) != 0;
        widthMismatch += font.measure(text, entry.size).width !=
                         get_utf8_string_width(text, entry.size, entry.font_id);
    }
    std::printf("%s actual glyphs=%u pixel_mismatch=%u width_mismatch=%u\n", NANOUI_LEGACY_NAME,
                cn_glyph_count, mismatch, widthMismatch);
    CHECK(mismatch == 0 && widthMismatch == 0);
    for (unsigned size : {12u, 16u, 20u}) {
        for (unsigned id : {0u, 1u}) {
            const char* texts[] = {"Settings", "BLE: READY", "A B!?", "\xF0\x9F\x98\x80"};
            NanoUI::LegacyBitmapFont font(font5x7, get_chinese_glyph,
                                          static_cast<std::uint8_t>(id));
            for (const char* text : texts) {
                original.resetPixels();
                adapted.resetPixels();
                original.drawText(1, 2, text, static_cast<std::uint8_t>(size), false,
                                  static_cast<std::uint8_t>(id));
                font.paint(bridge, NanoUI::Offset(1, 2), text, static_cast<float>(size),
                           0xFFFFFFFFu);
                CHECK(std::memcmp(original.pixels, adapted.pixels, sizeof original.pixels) == 0);
                CHECK(font.measure(text, static_cast<float>(size)).width ==
                      get_utf8_string_width(text, size, id));
            }
        }
    }
    CHECK(adapted.clears == 0 && adapted.displays == 0 && bridge.balanced() && !bridge.error());
    const std::uint32_t previewCodes[] = {0x5B57, 0x5E93, 0x8BBE, 0x7F6E, 0x8FD4, 0x56DE};
    for (auto cp : previewCodes) {
        std::uint8_t width = 0;
        if (!get_chinese_glyph(cp, 12, &width, 0))
            std::printf("%s existing resource missing U+%04x at12px; original fallback retained\n",
                        NANOUI_LEGACY_NAME, static_cast<unsigned>(cp));
    }
}
struct PageModel {
    const NanoUI::Font& font;
    float size;
    unsigned height;
};
class Page final : public NanoUI::StatelessWidget {
   public:
    explicit Page(PageModel& model) noexcept
        : StatelessWidget(NanoUI::typeId<Page>()), model(model) {}
    NanoUI::BuildResult build(NanoUI::BuildContext& c) const noexcept override {
        auto w =
            NanoUI::makeOwned<NanoUI::SizedBox>(c.allocator(), NanoUI::AllocationKind::widget,
                                                NanoUI::Size(128, static_cast<float>(model.height)),
                                                NanoUI::ChildBuilder(column, &model));
        const auto error = w ? NanoUI::UiError::none : NanoUI::UiError::outOfMemory;
        return NanoUI::BuildResult(error, std::move(w));
    }

   private:
    static NanoUI::BuildResult column(NanoUI::BuildContext& c, void* payload) noexcept {
        auto w = NanoUI::makeOwned<NanoUI::Linear>(
            c.allocator(), NanoUI::AllocationKind::widget, NanoUI::ChildBuilder(lines, payload),
            NanoUI::Axis::vertical, 2, NanoUI::CrossAlign::stretch);
        const auto error = w ? NanoUI::UiError::none : NanoUI::UiError::outOfMemory;
        return NanoUI::BuildResult(error, std::move(w));
    }
    static NanoUI::BuildResult lines(NanoUI::BuildContext& c, void* payload) noexcept {
        const auto& page = *static_cast<PageModel*>(payload);
        NanoUI::BuildResult result;
        const char* labels[] = {"字库 / Font", "设置 / Settings", "BLE: READY", "返回 / Back"};
        for (const auto* label : labels) {
            auto w = NanoUI::makeText(c.allocator(), label, page.font, page.size, 0xFFFFFFFFu);
            if (!w) {
                result.error = NanoUI::UiError::outOfMemory;
                return result;
            }
            result.error = result.children.append(c.allocator(), w);
            if (result.error != NanoUI::UiError::none)
                return result;
        }
        return result;
    }
    PageModel& model;
};
void renderPage(const char* output, unsigned id, unsigned size, unsigned height) {
    MemoryCanvas memory;
    NanoUI::LegacyCanvasBridge<MemoryCanvas> bridge(memory);
    NanoUI::LegacyBitmapFont font(font5x7, get_chinese_glyph, static_cast<std::uint8_t>(id));
    PageModel model{font, static_cast<float>(size), height};  // Outlives every borrowing config.
    NanoUI::Windows::HeapAllocator heap;
    NanoUI::TrackingAllocator a(heap);
    {
        NanoUI::BuildOwner owner(a, NanoUI::OwnerOptions(63, 512, 64));
        CHECK(owner.setRoot(NanoUI::makeOwned<Page>(a, NanoUI::AllocationKind::widget, model)) ==
              NanoUI::UiError::none);
        CHECK(bridge.beginFrame());
        const auto frame = owner.frame(bridge, NanoUI::Size(128, static_cast<float>(height)));
        CHECK(frame.error == NanoUI::UiError::none && frame.painted && bridge.balanced() &&
              !bridge.error());
        NanoUI::RasterCanvas exported(128, height);
        exported.clear(0x000000FFu);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < 128; ++x)
                if (memory.pixels[y * 128 + x])
                    exported.fillRect(NanoUI::Rect(x, y, x + 1, y + 1), 0xFFFFFFFFu);
        const std::string path = std::string(output) + "/" + NANOUI_LEGACY_NAME + "-font" +
                                 std::to_string(id) + "-size" + std::to_string(size) + "-128x" +
                                 std::to_string(height) + ".ppm";
        CHECK(exported.writePpm(path.c_str()));
        CHECK(!owner.frame(bridge, NanoUI::Size(128, static_cast<float>(height))).painted);
    }
    CHECK(a.statistics().currentBlocks() == 0);
    CHECK(memory.clears == 0 && memory.displays == 0);
}
}  // namespace
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    fontParity();
    for (unsigned id : {0u, 1u}) {
        renderPage(argv[1], id, 12, 64);
        renderPage(argv[1], id, 16, 96);
    }
    std::printf("%s product port: %d checks, %d failures\n", NANOUI_LEGACY_NAME, checks, failures);
    return failures ? 1 : 0;
}
