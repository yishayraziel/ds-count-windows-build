#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace dsq
{
/** Rebindable parameter attachment, so that one widget can follow channel 1 or channel 2. */
class Bound
{
public:
    void bind (juce::RangedAudioParameter* p, std::function<void (float)> onChange, juce::UndoManager* um)
    {
        attachment.reset();
        parameter = p;
        if (p != nullptr)
        {
            attachment = std::make_unique<juce::ParameterAttachment> (*p, std::move (onChange), um);
            attachment->sendInitialUpdate();
        }
    }
    juce::RangedAudioParameter* get() const { return parameter; }
    juce::ParameterAttachment* operator->() const { return attachment.get(); }
    explicit operator bool() const { return attachment != nullptr; }

private:
    juce::RangedAudioParameter* parameter = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

/** A continuous parameter as a dialog row: label, scroll bar used as a fader, and a value field
    (with spin arrows in the 3.11 skin). Arrows step, the track pages, the thumb drags, the field
    drags vertically, double-click types a value, Alt/Cmd-click resets, the wheel steps. */
class Knob : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    enum class Mode { row, field };
    Knob (juce::String label, juce::UndoManager* um);

    void bindTo (juce::RangedAudioParameter* p);
    juce::RangedAudioParameter* getParameter() const { return param.get(); }
    void setLabel (const juce::String& s) { if (s != label) { label = s; repaint(); } }
    void setDimmed (bool b) { if (dimmed != b) { dimmed = b; repaint(); } }
    void setMode (Mode m) { mode = m; repaint(); }
    void setLabelWidth (float w) { labelW = w; repaint(); }
    void setFieldWidth (float w) { fieldW = w; repaint(); }
    juce::String getValueText() const { return valueText; }

    std::function<juce::String()> valueOverride;
    std::function<float()> positionOverride;
    std::function<void (float plain)> onUserChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Parts { juce::Rectangle<float> label, bar, field, spin; };
    Parts parts() const;
    void refresh (float plain);
    void setNorm (float n);
    float shownNorm() const;
    void step (float delta);
    void timerCallback() override;

    juce::String label, valueText;
    juce::UndoManager* undo;
    Bound param;
    Mode mode = Mode::row;
    float labelW = 118.0f, fieldW = 104.0f;
    float norm = 0.5f, dragNorm = 0, lastY = 0, repeatStep = 0;
    enum class Drag { none, thumb, field, repeat } drag = Drag::none;
    bool dimmed = false;
    std::unique_ptr<juce::TextEditor> editor;
};

/** Checkbox (with its label) for a bool parameter, or a push button. Unbound it is a plain action button. */
class Toggle : public juce::Component, public juce::SettableTooltipClient
{
public:
    Toggle (juce::String text, juce::UndoManager* um);

    void bindTo (juce::RangedAudioParameter* p);
    void setText (const juce::String& s) { if (s != text) { text = s; repaint(); } }
    void setLit (bool b) { if (lit != b) { lit = b; repaint(); } }
    bool isLit() const { return lit; }
    void setDimmed (bool b) { if (dimmed != b) { dimmed = b; repaint(); } }
    /** true: checkbox with label. false: push button, drawn pressed while lit. */
    void setShowLamp (bool b) { checkbox = b; repaint(); }
    void setDefault (bool b) { isDefault = b; repaint(); }
    /** Drop-down look (for the preset name). */
    void setPopupLook (bool b) { popupLook = b; repaint(); }
    /** 3.11 toolbar look: icon above a small label. Ignored in Classic. */
    std::function<void (juce::Graphics&, juce::Rectangle<float>, bool disabled)> icon;

    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String text;
    juce::UndoManager* undo;
    Bound param;
    bool lit = false, dimmed = false, checkbox = false, isDefault = false, down = false, popupLook = false;
};

/** Choice parameter as radio buttons, a drop-down list or a row of push buttons. */
class Segment : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Style { radio, popup, buttons };
    Segment (juce::StringArray labels, juce::UndoManager* um, Style style = Style::radio);
    void bindTo (juce::RangedAudioParameter* p);
    void setLabels (juce::StringArray l) { if (l != labels) { labels = std::move (l); repaint(); } }
    void setStyle (Style s) { style = s; repaint(); }
    int getSelected() const { return selected; }
    void setDimmed (bool b) { if (dimmed != b) { dimmed = b; repaint(); } }
    std::function<void (int)> onUserChange;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> itemRect (int i) const;
    void choose (int i);

    juce::StringArray labels;
    juce::UndoManager* undo;
    Bound param;
    Style style;
    int selected = 0;
    bool dimmed = false;
};
} // namespace dsq
