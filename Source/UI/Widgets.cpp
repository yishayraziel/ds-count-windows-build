#include "Widgets.h"

namespace dsq
{
static void textEntry (juce::Component& owner, std::unique_ptr<juce::TextEditor>& editor, juce::Rectangle<int> area,
                       const juce::String& initial, std::function<void (const juce::String&)> commit)
{
    editor = std::make_unique<juce::TextEditor>();
    editor->setJustification (juce::Justification::centredRight);
    editor->setFont (theme::ui (18.0f, true));
    editor->setText (initial, false);
    editor->setBounds (area);
    editor->setIndents (6, 2);
    owner.addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
    editor->selectAll();

    auto* e = editor.get();
    auto safe = juce::Component::SafePointer<juce::Component> (&owner);
    auto close = [safe, &editor] { juce::MessageManager::callAsync ([safe, &editor] { if (safe != nullptr) editor.reset(); }); };
    e->onReturnKey = [e, commit, close] { commit (e->getText()); close(); };
    e->onEscapeKey = close;
    e->onFocusLost = close;
}

// ---- Knob (fader row) ------------------------------------------------------------------

Knob::Knob (juce::String l, juce::UndoManager* um) : label (std::move (l)), undo (um) {}

void Knob::bindTo (juce::RangedAudioParameter* p)
{
    if (p == param.get())
        return;
    param.bind (p, [this] (float v) { refresh (v); }, undo);
}

void Knob::refresh (float plain)
{
    auto* p = param.get();
    norm = p->convertTo0to1 (plain);
    valueText = p->getText (norm, 64);
    setTooltip (p->getName (64) + ": " + valueText
                + "\nArrows step, drag the box or the value, double-click to type, Alt-click for default");
    repaint();
}

Knob::Parts Knob::parts() const
{
    auto b = getLocalBounds().toFloat();
    const float h = 24.0f;
    auto row = b.withSizeKeepingCentre (b.getWidth(), std::min (h, b.getHeight()));
    Parts p;
    if (label.isNotEmpty())
        p.label = row.removeFromLeft (labelW);
    const bool spinArrows = ! theme::classic();
    if (spinArrows)
        p.spin = row.removeFromRight (17.0f);
    p.field = row.removeFromRight (fieldW).withSizeKeepingCentre (fieldW, spinArrows ? 24.0f : 26.0f);
    if (mode == Mode::row)
    {
        row.removeFromRight (8.0f);
        p.bar = row.withSizeKeepingCentre (row.getWidth(), theme::classic() ? 22.0f : 20.0f);
    }
    return p;
}

void Knob::setNorm (float n)
{
    n = juce::jlimit (0.0f, 1.0f, n);
    if (auto* p = param.get())
    {
        const float plain = p->convertFrom0to1 (n);
        param->setValueAsPartOfGesture (plain);
        if (onUserChange)
            onUserChange (plain);
    }
}

float Knob::shownNorm() const
{
    auto* p = param.get();
    if (p == nullptr || ! positionOverride)
        return norm;
    const float v = positionOverride();
    if (std::isnan (v))
        return norm;
    const auto& r = p->getNormalisableRange();
    return p->convertTo0to1 (juce::jlimit (r.start, r.end, v));
}

void Knob::step (float delta)
{
    dragNorm = juce::jlimit (0.0f, 1.0f, dragNorm + delta);
    setNorm (dragNorm);
}

void Knob::timerCallback()
{
    if (drag != Drag::repeat)
        return stopTimer();
    startTimer (40);
    step (repeatStep);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (! param || dimmed)
        return;
    if (e.mods.isAltDown() || e.mods.isCommandDown())
    {
        auto* p = param.get();
        const float plain = p->convertFrom0to1 (p->getDefaultValue());
        param->setValueAsCompleteGesture (plain);
        if (onUserChange)
            onUserChange (plain);
        return;
    }
    const auto pt = e.position;
    const auto pr = parts();
    const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
    param->beginGesture();
    dragNorm = shownNorm();
    lastY = pt.y;
    if (! pr.spin.isEmpty() && pr.spin.contains (pt))
    {
        repeatStep = (pt.y < pr.spin.getCentreY() ? 0.005f : -0.005f) * fine;
        drag = Drag::repeat;
        step (repeatStep);
        startTimer (350);
        return;
    }
    if (! pr.bar.isEmpty() && pr.bar.contains (pt))
    {
        const float a = pr.bar.getHeight();
        auto track = pr.bar.reduced (a, 0.0f);
        const float thumbX = track.getX() + shownNorm() * (track.getWidth() - a);
        if (pt.x < pr.bar.getX() + a || pt.x > pr.bar.getRight() - a)
        {
            repeatStep = (pt.x < pr.bar.getX() + a ? -0.005f : 0.005f) * fine;
            drag = Drag::repeat;
            step (repeatStep);
            startTimer (350);
        }
        else if (pt.x >= thumbX && pt.x <= thumbX + a)
            drag = Drag::thumb;
        else
        {
            repeatStep = (pt.x < thumbX ? -0.1f : 0.1f) * fine;
            drag = Drag::repeat;
            step (repeatStep);
            startTimer (350);
        }
        return;
    }
    drag = Drag::field;
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::thumb)
    {
        const auto pr = parts();
        const float a = pr.bar.getHeight();
        auto track = pr.bar.reduced (a, 0.0f);
        dragNorm = juce::jlimit (0.0f, 1.0f, (e.position.x - track.getX() - a * 0.5f) / std::max (1.0f, track.getWidth() - a));
        setNorm (dragNorm);
    }
    else if (drag == Drag::field)
    {
        const float pixels = e.mods.isShiftDown() ? 2400.0f : 320.0f;
        dragNorm = juce::jlimit (0.0f, 1.0f, dragNorm + (lastY - e.position.y) / pixels);
        lastY = e.position.y;
        setNorm (dragNorm);
    }
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    stopTimer();
    if (drag != Drag::none && param)
        param->endGesture();
    drag = Drag::none;
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto* p = param.get();
    const auto pr = parts();
    if (p == nullptr || dimmed || ! pr.field.contains (e.position))
        return;
    textEntry (*this, editor, pr.field.toNearestInt(), valueText, [this, p] (const juce::String& t)
    {
        const float n = p->getValueForText (t);
        param->setValueAsCompleteGesture (p->convertFrom0to1 (n));
        if (onUserChange)
            onUserChange (p->convertFrom0to1 (n));
    });
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! param || drag != Drag::none || dimmed)
        return;
    const float s = (e.mods.isShiftDown() ? 0.002f : 0.02f) * (w.deltaY > 0 ? 1.0f : -1.0f) * (w.isReversed ? -1.0f : 1.0f);
    param->beginGesture();
    dragNorm = shownNorm();
    step (s);
    param->endGesture();
}

void Knob::paint (juce::Graphics& g)
{
    const auto pr = parts();
    if (! pr.label.isEmpty())
        theme::text (g, label, pr.label, juce::Justification::centredLeft, true, dimmed);
    if (! pr.bar.isEmpty())
        theme::scrollbar (g, pr.bar, shownNorm(), dimmed);
    auto shown = valueOverride ? valueOverride() : juce::String();
    if (shown.isEmpty())
        shown = valueText;
    theme::field (g, pr.field, dimmed ? juce::String ("--") : shown, dimmed);
    if (! pr.spin.isEmpty())
        theme::spin (g, pr.spin, dimmed);
}

// ---- Toggle ----------------------------------------------------------------------------

Toggle::Toggle (juce::String t, juce::UndoManager* um) : text (std::move (t)), undo (um) {}

void Toggle::bindTo (juce::RangedAudioParameter* p)
{
    if (p == param.get())
        return;
    param.bind (p, [this] (float v) { setLit (v > 0.5f); }, undo);
    if (p != nullptr && getTooltip().isEmpty())
        setTooltip (p->getName (64));
}

void Toggle::mouseUp (const juce::MouseEvent& e)
{
    down = false;
    repaint();
    if (! getLocalBounds().contains (e.getPosition()))
        return;
    if (param)
        param->setValueAsCompleteGesture (lit ? 0.0f : 1.0f);
    if (onClick)
        onClick();
}

void Toggle::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    if (checkbox)
    {
        auto box = b.removeFromLeft (17.0f).withSizeKeepingCentre (17.0f, 17.0f);
        theme::checkbox (g, box, lit, dimmed);
        b.removeFromLeft (8.0f);
        theme::text (g, text, b, juce::Justification::centredLeft, true, dimmed);
        return;
    }
    if (popupLook)
    {
        theme::popup (g, b.withSizeKeepingCentre (b.getWidth(), std::min (b.getHeight(), 26.0f)), text, dimmed);
        return;
    }
    if (icon && ! theme::classic())
    {
        theme::tool (g, b, lit || down);
        auto in = b.reduced (2.0f);
        icon (g, in.removeFromTop (in.getHeight() * 0.55f).withTrimmedTop (4.0f), dimmed);
        theme::text (g, text, in.withTrimmedBottom (2.0f), juce::Justification::centred, false, dimmed);
        return;
    }
    theme::button (g, b.reduced (isDefault ? 4.0f : 1.0f), text, lit || down, dimmed, isDefault);
}

// ---- Segment ---------------------------------------------------------------------------

Segment::Segment (juce::StringArray l, juce::UndoManager* um, Style s) : labels (std::move (l)), undo (um), style (s) {}

void Segment::bindTo (juce::RangedAudioParameter* p)
{
    if (p == param.get())
        return;
    param.bind (p, [this] (float v) { selected = juce::roundToInt (v); repaint(); }, undo);
    if (p != nullptr && getTooltip().isEmpty())
        setTooltip (p->getName (64));
}

juce::Rectangle<float> Segment::itemRect (int i) const
{
    auto b = getLocalBounds().toFloat();
    if (style == Style::buttons)
    {
        const float w = b.getWidth() / (float) std::max (1, labels.size());
        return { b.getX() + i * w, b.getY(), w, b.getHeight() };
    }
    // Radio: each option is sized to its label, with even spacing.
    float x = 0.0f;
    for (int k = 0; k < labels.size(); ++k)
    {
        const float w = 17.0f + 7.0f + theme::textWidth (labels[k]);
        if (k == i)
            return { x, b.getY(), w, b.getHeight() };
        x += w + 18.0f;
    }
    return {};
}

void Segment::choose (int i)
{
    if (param)
        param->setValueAsCompleteGesture ((float) i);
    else
    {
        selected = i;
        repaint();
    }
    if (onUserChange)
        onUserChange (i);
}

void Segment::mouseUp (const juce::MouseEvent& e)
{
    if (labels.isEmpty() || dimmed || ! getLocalBounds().contains (e.getPosition()))
        return;
    if (style == Style::popup)
    {
        juce::PopupMenu m;
        for (int i = 0; i < labels.size(); ++i)
            m.addItem (i + 1, labels[i], true, i == selected);
        juce::Component::SafePointer<Segment> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                         [safe] (int r) { if (safe != nullptr && r > 0) safe->choose (r - 1); });
        return;
    }
    for (int i = 0; i < labels.size(); ++i)
        if (itemRect (i).contains (e.position))
            return choose (i);
}

void Segment::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    if (style == Style::popup)
    {
        theme::popup (g, b.withSizeKeepingCentre (b.getWidth(), std::min (b.getHeight(), 26.0f)),
                      labels[juce::jlimit (0, labels.size() - 1, selected)], dimmed);
        return;
    }
    for (int i = 0; i < labels.size(); ++i)
    {
        auto r = itemRect (i);
        if (style == Style::buttons)
        {
            theme::button (g, r.reduced (2.0f, 1.0f), labels[i], i == selected, dimmed);
            continue;
        }
        theme::radio (g, r.withWidth (17.0f).withSizeKeepingCentre (17.0f, 17.0f), i == selected, dimmed);
        theme::text (g, labels[i], r.withTrimmedLeft (24.0f), juce::Justification::centredLeft, true, dimmed);
    }
}
} // namespace dsq
