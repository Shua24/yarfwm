#include "DecorationRenderer.hpp"
#include <gtest/gtest.h>
#include <vector>

// The renderer must paint the whole surface, leave the button squares a
// different colour from the bar, and never write outside the buffer.
//
// Note what these tests deliberately do NOT assert: an exact count of
// glyph-coloured pixels. cairo renders a 1px stroke with subpixel coverage, so
// a glyph's exact colour appears on only a handful of pixels (18 for this
// titlebar, against 512 in the button squares). Any test that pins an exact
// glyph pixel count is flaky. Shape differences are asserted as
// inequalities instead.
TEST(DecorationRenderer, FillsTheWholeSurfaceWithTheBackground)
{
	const int32_t width = 200;
	const int32_t height = 26;
	std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0u);
	const TitlebarMetrics metrics{width, height, 26, 26, 0, 0};
	const TitlebarColors colors{0xff203040u, 0xff404040u, 0xffffffffu,
				    0xffccccccu, 0xffffffffu};

	render_titlebar(pixels.data(), width, height, width * 4, metrics,
			colors, "foot", true, 3, "JetBrains Mono 10");

	// The far left of the bar is background, and it is opaque.
	EXPECT_EQ(pixels[0] >> 24, 0xffu);
	EXPECT_NE(pixels[0], 0u);
	// Nothing outside the buffer can be checked here, but every pixel must
	// have been written (the buffer started zeroed and alpha 0).
	for (int32_t y = 0; y < height; y++) {
		for (int32_t x = 0; x < width; x++) {
			EXPECT_NE(pixels[static_cast<size_t>(y) * width + x],
				  0u)
			    << "unpainted pixel at " << x << "," << y;
		}
	}
}

TEST(DecorationRenderer, AButtonIsNotPaintedWithTheBarColour)
{
	const int32_t width = 200;
	const int32_t height = 26;
	std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0u);
	const TitlebarMetrics metrics{width, height, 26, 26, 0, 0};
	const TitlebarColors colors{0xff203040u, 0xff404040u, 0xffffffffu,
				    0xffccccccu, 0xffff0000u};

	render_titlebar(pixels.data(), width, height, width * 4, metrics,
			colors, "foot", true, 3, "JetBrains Mono 10");

	// The close button's centre must not be the background colour: the
	// glyph is drawn there. This is the one case where an exact colour IS
	// reliable -- the glyph is a 1px line, but its crossing point paints
	// the centre pixel solidly.
	const int32_t close_left = width - 26;
	const uint32_t centre =
	    pixels[static_cast<size_t>(13) * width + close_left + 13];
	EXPECT_NE(centre, 0xff203040u);
}

// The three glyphs are different shapes, and the minimize bar is the
// smallest of them. Counting non-background pixels inside each square is the
// shape check; the counts must not be equal, or the glyph switch is broken.
TEST(DecorationRenderer, TheThreeGlyphsAreDifferentShapes)
{
	const int32_t width = 200;
	const int32_t height = 26;
	std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0u);
	const TitlebarMetrics metrics{width, height, 26, 26, 0, 0};
	const TitlebarColors colors{0xff203040u, 0xff404040u, 0xffffffffu,
				    0xffccccccu, 0xffff0000u};
	const uint32_t background = 0xff203040u;

	render_titlebar(pixels.data(), width, height, width * 4, metrics,
			colors, "foot", true, 3, "JetBrains Mono 10");

	uint32_t counts[3] = {0u, 0u, 0u};
	for (int index = 0; index < 3; index++) {
		const int32_t left = width - 26 * (3 - index);
		for (int32_t y = 0; y < height; y++) {
			for (int32_t x = left; x < left + 26; x++) {
				if (pixels[static_cast<size_t>(y) * width +
					   x] != background) {
					counts[index]++;
				}
			}
		}
	}

	// Each square carries some glyph.
	EXPECT_GT(counts[0], 0u);
	EXPECT_GT(counts[1], 0u);
	EXPECT_GT(counts[2], 0u);
	// The bar is thinner than the square outline and the cross.
	EXPECT_LT(counts[0], counts[1]);
	// The square outline and the cross differ too.
	EXPECT_NE(counts[1], counts[2]);
}

TEST(BorderColor, ScalesAByteToThePercentageTheProtocolWants)
{
	uint32_t r = 0;
	uint32_t g = 0;
	uint32_t b = 0;
	uint32_t a = 0;
	// 0x40ff0000 is AARRGGBB: alpha 0x40, full red, no green, no blue.
	//
	// The colour format is AARRGGBB everywhere in this feature (the
	// config's 8 hex digits, TitlebarColors, and this helper), so a test
	// written as "0xff0000ff is full red" would be reading it as RRGGBBAA
	// and is wrong: 0xff0000ff is opaque BLUE under AARRGGBB. This case is
	// chosen so the two readings cannot be confused.
	border_color_components(0x40ff0000u, &r, &g, &b, &a);
	EXPECT_EQ(r, 0xffffffffu);
	EXPECT_EQ(g, 0x00000000u);
	EXPECT_EQ(b, 0x00000000u);
	// One byte replicated across all four, so alpha 0x40 is 0x40404040.
	EXPECT_EQ(a, 0x40404040u);
}

TEST(BorderColor, ReplicatesTheByteRatherThanShiftingIt)
{
	uint32_t r = 0;
	uint32_t g = 0;
	uint32_t b = 0;
	uint32_t a = 0;
	// The sea-blue focus colour from the default config.
	border_color_components(0xff5c8fb0u, &r, &g, &b, &a);
	EXPECT_EQ(r, 0x5c5c5c5cu);
	EXPECT_EQ(g, 0x8f8f8f8fu);
	EXPECT_EQ(b, 0xb0b0b0b0u);
	EXPECT_EQ(a, 0xffffffffu);
}
