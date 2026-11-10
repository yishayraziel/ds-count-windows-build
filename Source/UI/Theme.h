#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace dsq::theme
{
// Two skins share one layout: a 16-colour early-90s PC desktop ("3.11") and a 1-bit 1984 desktop ("Classic").
enum class Skin { win311 = 0, classic = 1 };
enum class Crt { off = 0, soft = 1, strong = 2 };

/** Global preferences, shared by every instance and saved in the user's settings file. */
Skin skin();
Crt crt();
void setSkin (Skin);
void setCrt (Crt);
inline bool classic() { return skin() == Skin::classic; }
/** The DS-COUNT mark (a stepped waveform) for the Classic menu bar. */
void mark (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
/** Changes with every setSkin / setCrt, so open editors can follow a change made in another one. */
int preferenceGeneration();
/** For tests: sets the skin without touching the settings file. */
void setSkinForTest (Skin);
void setCrtForTest (Crt);

// VGA palette.
namespace c
{
inline const juce::Colour black { 0xff000000 }, white { 0xffffffff }, silver { 0xffc0c0c0 }, gray { 0xff808080 },
                          navy { 0xff000080 }, blue { 0xff0000ff }, green { 0xff008000 }, lime { 0xff00ff00 },
                          teal { 0xff008080 }, cyan { 0xff00ffff }, maroon { 0xff800000 }, red { 0xffff0000 },
                          olive { 0xff808000 }, yellow { 0xffffff00 };
}

/** Interface text. The 3.11 skin uses DSC System (bold or regular), Classic uses ChicagoFLF for everything bold. */
juce::Font ui (float pointHeight = 18.0f, bool bold = true);
/** Small regular text for scales and notes. */
juce::Font small (float pointHeight = 12.0f);

/** Text, drawn dithered (Classic) or embossed gray (3.11) when disabled. */
void text (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification,
           bool bold = true, bool disabled = false, juce::Colour = c::black, float pointHeight = 18.0f);
float textWidth (const juce::String&, bool bold = true, float pointHeight = 18.0f);

/** Desktop behind the windows: white MDI client (3.11) or 50 % dither (Classic). */
void desktop (juce::Graphics&, juce::Rectangle<float>);
/** A document window with its title bar. Returns the client area. */
juce::Rectangle<float> window (juce::Graphics&, juce::Rectangle<float>, const juce::String& title, bool active,
                               bool clientGray = true, bool minMax = true);
/** Group box with a caption. */
void groupBox (juce::Graphics&, juce::Rectangle<float>, const juce::String& title);
/** Push button face. */
void button (juce::Graphics&, juce::Rectangle<float>, const juce::String& label, bool pressed, bool disabled = false, bool isDefault = false);
/** 3.11 toolbar button face. */
void tool (juce::Graphics&, juce::Rectangle<float>, bool pressed);
void checkbox (juce::Graphics&, juce::Rectangle<float> box, bool on, bool disabled = false);
void radio (juce::Graphics&, juce::Rectangle<float> box, bool on, bool disabled = false);
/** Horizontal scroll bar used as a fader. pos is 0...1, negative hides the thumb. */
void scrollbar (juce::Graphics&, juce::Rectangle<float>, float pos, bool disabled = false);
/** Edit field with right-aligned value. */
void field (juce::Graphics&, juce::Rectangle<float>, const juce::String&, bool disabled = false);
/** Spin arrows next to a field (3.11 only). */
void spin (juce::Graphics&, juce::Rectangle<float>, bool disabled = false);
/** Drop-down list / popup menu button. */
void popup (juce::Graphics&, juce::Rectangle<float>, const juce::String&, bool disabled = false);
/** Numeric readout: red digits on black (3.11) or inverted (Classic). */
void readout (juce::Graphics&, juce::Rectangle<float>, const juce::String&, bool dim = false);
/** Horizontal gain-reduction or detector bar, fraction 0...1, optional marker at 0...1. */
void hbar (juce::Graphics&, juce::Rectangle<float>, float fraction, float marker = -1.0f, bool disabled = false, bool level = false);
/** Vertical level meter, 0...1, with peak hold. */
void vmeter (juce::Graphics&, juce::Rectangle<float>, float level, float hold);
/** Small status lamp. */
void lamp (juce::Graphics&, juce::Rectangle<float>, bool on);
/** Etched separator. */
void vline (juce::Graphics&, float x, float y0, float y1);

/** Plot display inside a window: black (3.11) or white (Classic). Returns the drawing area. */
juce::Rectangle<float> plot (juce::Graphics&, juce::Rectangle<float>);
struct PlotInk { juce::Colour bg, grid, text, line, fill, fill2, accent, dim; bool dottedGrid; };
PlotInk ink();
/** Fills a path with the plot fill: solid (3.11) or dither pattern (Classic). */
void plotFill (juce::Graphics&, const juce::Path&, bool second = false);
/** Grid line, dotted in Classic. */
void gridLine (juce::Graphics&, juce::Point<float>, juce::Point<float>);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void refresh();
    juce::Font getPopupMenuFont() override { return ui (18.0f, false); }
    juce::Font getAlertWindowMessageFont() override { return ui (18.0f, false); }
    juce::Font getAlertWindowTitleFont() override { return ui (18.0f, true); }
    juce::Font getAlertWindowFont() override { return ui (18.0f, false); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return ui (18.0f, true); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
};
} // namespace dsq::theme
