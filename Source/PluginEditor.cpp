#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "WavetableDocument.h"
#include "PresetLibrary.h"
#include "ModulationView.h"
#include "EnvelopeModel.h"
#include "LfoModel.h"
#include "LfoCurveDocument.h"
#include <cmath>
#include <limits>

namespace
{
constexpr int designWidth = 1120, designHeight = 760;
constexpr int matrixRouteCount = static_cast<int> (anyvoc::modulationRouteCount);
const juce::Colour background { 0xff10131e }, cardColour { 0xff1a1e2d }, insetColour { 0xff121622 };
const juce::Colour borderColour { 0xff2c3345 }, primaryText { 0xffedf1fa }, secondaryText { 0xff8993ac };
const juce::Colour mint { 0xff80e5c3 }, lilac { 0xffb4a1ff }, blue { 0xff80baff }, orange { 0xffffc18b };

juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

float value (AnyvocAudioProcessor& processor, const juce::String& id)
{
    if (auto* parameter = processor.apvts.getRawParameterValue (id))
        return parameter->load();
    return 0.0f;
}

void drawText (juce::Graphics& g, juce::String text, juce::Rectangle<int> bounds,
               float size = 12.0f, juce::Colour colour = secondaryText,
               bool bold = false, juce::Justification justification = juce::Justification::centredLeft)
{
    g.setColour (colour);
    g.setFont (font (size, bold));
    g.drawFittedText (text, bounds, justification, 1);
}

void drawCard (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title,
               const juce::String& subtitle, juce::Colour accent)
{
    const auto r = bounds.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.13f));
    g.fillRoundedRectangle (r.translated (0, 3), 12);
    g.setColour (cardColour);
    g.fillRoundedRectangle (r, 12);
    g.setColour (borderColour);
    g.drawRoundedRectangle (r.reduced (0.5f), 12, 1);
    g.setColour (accent);
    g.fillRoundedRectangle (r.getX() + 17, r.getY() + 17, 3, 14, 1.5f);
    drawText (g, title, bounds.withTrimmedLeft (29).withY (bounds.getY() + 11).withHeight (24),
              14, primaryText, true);
    if (subtitle.isNotEmpty())
        drawText (g, subtitle, { bounds.getX() + 18, bounds.getY() + 41, bounds.getWidth() - 36, 19 }, 11);
}

class InstrumentLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    InstrumentLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, primaryText);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::rotarySliderFillColourId, mint);
        setColour (juce::Slider::trackColourId, mint);
        setColour (juce::ComboBox::backgroundColourId, insetColour);
        setColour (juce::ComboBox::textColourId, primaryText);
        setColour (juce::ComboBox::outlineColourId, borderColour);
        setColour (juce::ComboBox::arrowColourId, secondaryText);
        setColour (juce::PopupMenu::backgroundColourId, cardColour);
        setColour (juce::PopupMenu::textColourId, primaryText);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, mint.withAlpha (0.14f));
        setColour (juce::PopupMenu::highlightedTextColourId, mint);
        setColour (juce::TextButton::buttonColourId, insetColour);
        setColour (juce::TextButton::buttonOnColourId, mint.withAlpha (0.16f));
        setColour (juce::TextButton::textColourOffId, secondaryText);
        setColour (juce::TextButton::textColourOnId, mint);
        setColour (juce::ToggleButton::textColourId, primaryText);
        setColour (juce::ToggleButton::tickColourId, mint);
        setColour (juce::ToggleButton::tickDisabledColourId, secondaryText);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff30384d));
        setColour (juce::TooltipWindow::textColourId, primaryText);
        setColour (juce::TooltipWindow::outlineColourId, borderColour);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (12, true); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12); }
    juce::Font getLabelFont (juce::Label&) override { return font (11); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float position, float start, float end, juce::Slider& slider) override
    {
        const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                           .reduced (2.0f);
        const auto radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 1;
        const auto centre = r.getCentre();
        const auto angle = start + position * (end - start);
        const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0, start, end, true);
        g.setColour (juce::Colour (0xff343c51));
        g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
        juce::Path progress;
        progress.addCentredArc (centre.x, centre.y, radius, radius, 0, start, angle, true);
        g.setColour (accent.withAlpha (slider.isEnabled() ? 1.0f : 0.3f));
        g.strokePath (progress, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        const auto body = juce::Rectangle<float> (radius * 1.59f, radius * 1.59f).withCentre (centre);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillEllipse (body.translated (0, 2));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff343c50), body.getTopLeft(),
                                               juce::Colour (0xff212637), body.getBottomRight(), false));
        g.fillEllipse (body);
        g.setColour (juce::Colour (0xff454e63));
        g.drawEllipse (body.reduced (0.5f), 1.0f);
        const auto tip = centre.getPointOnCircumference (radius * 0.61f, angle);
        const auto tail = centre.getPointOnCircumference (radius * 0.31f, angle);
        g.setColour (accent);
        g.drawLine ({ tail, tip }, 2.4f);
        if (slider.isMouseOverOrDragging())
        {
            g.setColour (accent.withAlpha (0.13f));
            g.drawEllipse (body.expanded (2), 2);
        }
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float, juce::Slider::SliderStyle,
                           juce::Slider& slider) override
    {
        const float cy = (float) y + (float) height * 0.5f;
        const auto accent = slider.findColour (juce::Slider::trackColourId);
        g.setColour (borderColour);
        g.fillRoundedRectangle ((float) x, cy - 2, (float) width, 4, 2);
        const auto zero = (float) slider.getPositionOfValue (0.0);
        g.setColour (accent.withAlpha (0.65f));
        g.fillRoundedRectangle (juce::jmin (zero, sliderPos), cy - 2,
                                juce::jmax (1.0f, std::abs (sliderPos - zero)), 4, 2);
        g.setColour (accent);
        g.fillEllipse (sliderPos - 4, cy - 4, 8, 8);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& colour, bool hovered, bool down) override
    {
        auto r = button.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (colour.brighter (down ? 0.12f : hovered ? 0.06f : 0.0f));
        g.fillRoundedRectangle (r, 6);
        g.setColour (button.getToggleState() ? mint.withAlpha (0.35f) : borderColour);
        g.drawRoundedRectangle (r, 6, 1);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                       juce::ComboBox&) override
    {
        const juce::Rectangle<float> r (0.5f, 0.5f, (float) width - 1, (float) height - 1);
        g.setColour (insetColour);
        g.fillRoundedRectangle (r, 5);
        g.setColour (borderColour);
        g.drawRoundedRectangle (r, 5, 1);
        juce::Path arrow;
        const float ax = (float) width - 16, ay = (float) height * 0.5f;
        arrow.startNewSubPath (ax - 3, ay - 2);
        arrow.lineTo (ax, ay + 1);
        arrow.lineTo (ax + 3, ay - 2);
        g.setColour (secondaryText);
        g.strokePath (arrow, juce::PathStrokeType (1.5f));
    }
};

enum class Unit { percent, signedPercent, hertz, seconds, integer, semitones, cents, pan, note, density, degrees };

void styleSliderText (juce::Slider& slider)
{
    // Sliders create their value labels before they join the editor hierarchy.
    // Explicit colours also keep those early labels consistent with our theme.
    slider.setColour (juce::Slider::textBoxTextColourId, primaryText);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

juce::String formatted (double n, Unit unit)
{
    switch (unit)
    {
        case Unit::percent: return juce::String (juce::roundToInt (n * 100)) + "%";
        case Unit::signedPercent: return (n > 0 ? "+" : "") + juce::String (juce::roundToInt (n * 100)) + "%";
        case Unit::hertz: return n >= 1000 ? juce::String (n / 1000, 2) + " kHz" : (n < 30 ? juce::String (n, 2) : juce::String (juce::roundToInt (n))) + " Hz";
        case Unit::seconds: return n >= 1 ? juce::String (n, 2) + " s" : juce::String (juce::roundToInt (n * 1000)) + " ms";
        case Unit::integer: return juce::String (juce::roundToInt (n));
        case Unit::semitones: return (n > 0 ? "+" : "") + juce::String (juce::roundToInt (n)) + " st";
        case Unit::cents: return juce::String (juce::roundToInt (n)) + " ct";
        case Unit::pan: return std::abs (n) < 0.005 ? "Centre" : juce::String (juce::roundToInt (std::abs (n) * 100)) + (n < 0 ? " L" : " R");
        case Unit::note: return juce::MidiMessage::getMidiNoteName (juce::roundToInt (n), true, true, 4);
        case Unit::density: return juce::String (juce::roundToInt (n)) + " /s";
        case Unit::degrees: return juce::String (juce::roundToInt (n * 360.0)) + juce::String::charToString (0x00b0);
    }
    return {};
}

// User provenance is explicit: parameter notifications only update controls. A
// control opens the sound transaction before its first write, independently of
// the host automation gesture that accompanies it.
class ScopedSoundEdit final
{
public:
    ScopedSoundEdit (AnyvocAudioProcessor& p, const juce::String& name) : processor (p) { processor.beginUserEdit (name); }
    ~ScopedSoundEdit() { processor.endUserEdit(); }
private:
    AnyvocAudioProcessor& processor;
    JUCE_DECLARE_NON_COPYABLE (ScopedSoundEdit)
};

class SoundSlider final : public juce::Slider
{
public:
    std::function<void()> onEditCancelled;
    std::function<bool()> userEditAllowed;
    void focusLost (FocusChangeType cause) override
    {
        juce::Slider::focusLost (cause);
        if (! hasKeyboardFocus (true) && onEditCancelled) onEditCancelled();
    }
    void visibilityChanged() override
    {
        juce::Slider::visibilityChanged();
        if (! isShowing() && onEditCancelled) onEditCancelled();
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey && onEditCancelled) { onEditCancelled(); return true; }
        return juce::Slider::keyPressed (key);
    }
};

class SoundSliderAttachment final : private juce::Slider::Listener
{
public:
    SoundSliderAttachment (AnyvocAudioProcessor& p, const juce::String& parameterId, SoundSlider& s)
        : processor (p), parameter (*p.apvts.getParameter (parameterId)), slider (s), id (parameterId),
          attachment (parameter, [this] (float n)
          {
              const std::weak_ptr<int> alive (lifetime);
              const bool previous = ignoreCallbacks; ignoreCallbacks = true;
              slider.setValue (n, juce::sendNotificationSync);
              if (! alive.expired()) ignoreCallbacks = previous;
          })
    {
        if (slider.getComponentID().isEmpty()) slider.setComponentID (id);
        slider.valueFromTextFunction = [this] (const juce::String& text) { return (double) parameter.convertFrom0to1 (parameter.getValueForText (text)); };
        slider.textFromValueFunction = [this] (double n) { return parameter.getText (parameter.convertTo0to1 ((float) n), 0); };
        slider.setDoubleClickReturnValue (true, parameter.convertFrom0to1 (parameter.getDefaultValue()));
        auto range = parameter.getNormalisableRange();
        auto from = [range] (double start, double end, double n) mutable
        { range.start = (float) start; range.end = (float) end; return (double) range.convertFrom0to1 ((float) n); };
        auto to = [range] (double start, double end, double n) mutable
        { range.start = (float) start; range.end = (float) end; return (double) range.convertTo0to1 ((float) n); };
        auto snap = [range] (double start, double end, double n) mutable
        { range.start = (float) start; range.end = (float) end; return (double) range.snapToLegalValue ((float) n); };
        juce::NormalisableRange<double> sliderRange { (double) range.start, (double) range.end, std::move (from), std::move (to), std::move (snap) };
        sliderRange.interval = range.interval; sliderRange.skew = range.skew; sliderRange.symmetricSkew = range.symmetricSkew;
        slider.setNormalisableRange (sliderRange);
        attachment.sendInitialUpdate();
        slider.addListener (this);
        slider.onDragStart = [this]
        {
            if (gesturing) return;
            if (slider.userEditAllowed && ! slider.userEditAllowed()) { attachment.sendInitialUpdate(); return; }
            const std::weak_ptr<int> alive (lifetime);
            cancelled = false;
            gestureRevision = processor.getHistoryRevision(); gestureExpected = parameter.getValue();
            processor.beginUserEdit (parameter.getName (128));
            gesturing = true;
            parameter.beginChangeGesture();
            if (! alive.expired() && ! validGesture()) cancelGesture();
        };
        slider.onDragEnd = [this]
        {
            const std::weak_ptr<int> alive (lifetime);
            finishGesture();
            if (! alive.expired()) { cancelled = false; attachment.sendInitialUpdate(); }
        };
        slider.onEditCancelled = [this] { if (gesturing) cancelGesture(); };
    }
    ~SoundSliderAttachment() override
    {
        lifetime.reset();
        slider.onDragStart = nullptr; slider.onDragEnd = nullptr; slider.onEditCancelled = nullptr;
        slider.removeListener (this); finishGesture();
    }
private:
    void finishGesture()
    {
        if (! gesturing) return;
        gesturing = false;
        auto* owner = &processor; auto* target = &parameter;
        target->endChangeGesture(); owner->endUserEdit();
    }
    bool validGesture() const
    {
        return gesturing && processor.getHistoryRevision() == gestureRevision && parameter.getValue() == gestureExpected;
    }
    void cancelGesture()
    {
        const std::weak_ptr<int> alive (lifetime);
        cancelled = true;
        finishGesture();
        if (! alive.expired()) attachment.sendInitialUpdate();
    }
    void sliderValueChanged (juce::Slider*) override
    {
        if (ignoreCallbacks) return;
        if (slider.userEditAllowed && ! slider.userEditAllowed())
        {
            if (gesturing) cancelGesture(); else attachment.sendInitialUpdate();
            return;
        }
        if (cancelled) { attachment.sendInitialUpdate(); return; }
        const std::weak_ptr<int> alive (lifetime);
        auto* owner = &processor; auto* target = &parameter;
        const auto parameterId = id;
        const auto plain = (float) slider.getValue();
        if (gesturing)
        {
            if (! validGesture()) { cancelGesture(); return; }
            const auto expected = target->convertTo0to1 (target->getNormalisableRange().snapToLegalValue (plain));
            owner->setUserParameter (parameterId, plain);
            if (alive.expired()) return;
            if (processor.getHistoryRevision() != gestureRevision || std::abs (target->getValue() - expected) > 1.0e-6f)
            { cancelGesture(); return; }
            gestureExpected = target->getValue();
        }
        else
        {
            const auto revision = owner->getHistoryRevision();
            const auto expected = target->getValue();
            ScopedSoundEdit edit (*owner, target->getName (128));
            target->beginChangeGesture();
            if (! alive.expired() && owner->getHistoryRevision() == revision && target->getValue() == expected
                && (! slider.userEditAllowed || slider.userEditAllowed()))
                owner->setUserParameter (parameterId, plain);
            target->endChangeGesture();
            if (! alive.expired()) attachment.sendInitialUpdate();
        }
    }
    AnyvocAudioProcessor& processor;
    juce::RangedAudioParameter& parameter;
    SoundSlider& slider;
    juce::String id;
    bool ignoreCallbacks = false, gesturing = false, cancelled = false;
    uint64_t gestureRevision = 0;
    float gestureExpected = 0;
    std::shared_ptr<int> lifetime = std::make_shared<int> (0);
    juce::ParameterAttachment attachment;
};

class SoundChoiceAttachment final : private juce::ComboBox::Listener
{
public:
    SoundChoiceAttachment (AnyvocAudioProcessor& p, const juce::String& parameterId, juce::ComboBox& b, std::function<bool()> allowed = {})
        : processor (p), parameter (*p.apvts.getParameter (parameterId)), box (b), id (parameterId), userEditAllowed (std::move (allowed)),
          attachment (parameter, [this] (float n)
          {
              const std::weak_ptr<int> alive (lifetime);
              const bool previous = ignoreCallbacks; ignoreCallbacks = true;
              box.setSelectedItemIndex (juce::roundToInt (parameter.convertTo0to1 (n) * (float) (box.getNumItems() - 1)), juce::sendNotificationSync);
              if (! alive.expired()) ignoreCallbacks = previous;
          })
    {
        if (box.getComponentID().isEmpty()) box.setComponentID (id);
        attachment.sendInitialUpdate(); box.addListener (this);
    }
    ~SoundChoiceAttachment() override { lifetime.reset(); box.removeListener (this); }
private:
    void comboBoxChanged (juce::ComboBox*) override
    {
        if (ignoreCallbacks || box.getSelectedItemIndex() < 0) return;
        if (userEditAllowed && ! userEditAllowed()) { attachment.sendInitialUpdate(); return; }
        const std::weak_ptr<int> alive (lifetime);
        auto* owner = &processor; auto* target = &parameter;
        const auto parameterId = id;
        const auto revision = owner->getHistoryRevision();
        const auto expected = target->getValue();
        const auto normalised = box.getNumItems() > 1 ? (float) box.getSelectedItemIndex() / (float) (box.getNumItems() - 1) : 0.0f;
        const auto plain = target->convertFrom0to1 (normalised);
        ScopedSoundEdit edit (*owner, target->getName (128));
        target->beginChangeGesture();
        if (! alive.expired() && owner->getHistoryRevision() == revision && target->getValue() == expected
            && (! userEditAllowed || userEditAllowed()))
            owner->setUserParameter (parameterId, plain);
        target->endChangeGesture();
        if (! alive.expired()) attachment.sendInitialUpdate();
    }
    AnyvocAudioProcessor& processor;
    juce::RangedAudioParameter& parameter;
    juce::ComboBox& box;
    juce::String id;
    std::function<bool()> userEditAllowed;
    bool ignoreCallbacks = false;
    std::shared_ptr<int> lifetime = std::make_shared<int> (0);
    juce::ParameterAttachment attachment;
};

class SoundButtonAttachment final : private juce::Button::Listener
{
public:
    SoundButtonAttachment (AnyvocAudioProcessor& p, const juce::String& parameterId, juce::Button& b, std::function<bool()> allowed = {})
        : processor (p), parameter (*p.apvts.getParameter (parameterId)), button (b), id (parameterId), userEditAllowed (std::move (allowed)),
          attachment (parameter, [this] (float n)
          {
              const std::weak_ptr<int> alive (lifetime);
              const bool previous = ignoreCallbacks; ignoreCallbacks = true;
              // JUCE ignores setToggleState on disabled buttons. Reconcile their
              // display without turning a host/preset update into a user edit.
              const bool enabled = button.isEnabled();
              button.setEnabled (true); button.setToggleState (n >= 0.5f, juce::sendNotificationSync);
              if (! alive.expired()) { button.setEnabled (enabled); ignoreCallbacks = previous; }
          })
    {
        if (button.getComponentID().isEmpty()) button.setComponentID (id);
        attachment.sendInitialUpdate(); button.addListener (this);
    }
    ~SoundButtonAttachment() override { lifetime.reset(); button.removeListener (this); }
private:
    void buttonClicked (juce::Button*) override
    {
        if (ignoreCallbacks) return;
        if (userEditAllowed && ! userEditAllowed()) { attachment.sendInitialUpdate(); return; }
        const std::weak_ptr<int> alive (lifetime);
        auto* owner = &processor; auto* target = &parameter;
        const auto parameterId = id;
        const auto revision = owner->getHistoryRevision();
        const auto expected = target->getValue();
        const float plain = button.getToggleState() ? 1.0f : 0.0f;
        ScopedSoundEdit edit (*owner, target->getName (128));
        target->beginChangeGesture();
        if (! alive.expired() && owner->getHistoryRevision() == revision && target->getValue() == expected
            && (! userEditAllowed || userEditAllowed()))
            owner->setUserParameter (parameterId, plain);
        target->endChangeGesture();
        if (! alive.expired()) attachment.sendInitialUpdate();
    }
    AnyvocAudioProcessor& processor;
    juce::RangedAudioParameter& parameter;
    juce::Button& button;
    juce::String id;
    std::function<bool()> userEditAllowed;
    bool ignoreCallbacks = false;
    std::shared_ptr<int> lifetime = std::make_shared<int> (0);
    juce::ParameterAttachment attachment;
};

juce::String modulationSourceName (int source)
{
    const juce::StringArray names { "Off", "LFO 1", "LFO 2", "Filter Env", "Velocity", "Mod Wheel", "Macro 1", "Macro 2", "Macro 3", "Macro 4",
        "LFO 3", "LFO 4", "LFO 5", "LFO 6", "LFO 7", "LFO 8", "LFO 9", "LFO 10", "Amp Env", "Env 3", "Env 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8" };
    return names[juce::jlimit (0, 24, source)];
}

juce::Colour modulationSourceColour (int source)
{
    if (source >= 10 && source <= 17) return source % 2 == 0 ? mint : lilac;
    if (source >= 21 && source <= 24) return source % 2 == 0 ? lilac : mint;
    if (source == 1 || source == 3 || source == 7 || source == 9) return mint;
    if (source == 2 || source == 6 || source == 8 || source == 18) return lilac;
    return blue;
}

bool matrixHierarchyVisible (const juce::Component* component);

constexpr size_t matrixRouteFieldCount = 19;
using MatrixRouteSnapshot = std::array<float, matrixRouteFieldCount>;
MatrixRouteSnapshot matrixRouteSnapshot (AnyvocAudioProcessor& processor, int route)
{
    const std::array<const char*, matrixRouteFieldCount> suffixes { "Source", "Dest", "Amount", "DestBank", "FilterTarget", "WarpEnabled", "WarpTarget",
        "Enabled", "Polarity", "Curve", "AuxSource", "AuxPolarity", "AuxCurve", "SourceExtra", "AuxSourceExtra", "LfoPhaseEnabled", "LfoPhaseTarget", "MacroEnabled", "MacroTarget" };
    MatrixRouteSnapshot snapshot{};
    const auto prefix = "mod" + juce::String (route + 1);
    for (size_t i = 0; i < snapshot.size(); ++i) snapshot[i] = processor.apvts.getParameter (prefix + suffixes[i])->getValue();
    return snapshot;
}

juce::String modulationCopyStatusName (AnyvocAudioProcessor::ModulationCopyStatus status)
{
    using Status = AnyvocAudioProcessor::ModulationCopyStatus;
    switch (status)
    {
        case Status::copied: return "copied";
        case Status::emptySource: return "emptySource";
        case Status::noEmptySlot: return "noEmptySlot";
        case Status::busy: return "busy";
        case Status::cancelled: return "cancelled";
        case Status::invalidRoute: return "invalidRoute";
    }
    return "invalidRoute";
}

juce::String modulationCopyMessage (int source, const AnyvocAudioProcessor::ModulationCopyResult& result)
{
    using Status = AnyvocAudioProcessor::ModulationCopyStatus;
    switch (result.status)
    {
        case Status::copied: return "Copied route " + juce::String (source + 1) + " to route " + juce::String (result.destination + 1)
                                   + ". Both routes keep their settings; Undo removes the copy.";
        case Status::emptySource: return "This route is completely empty. Configure it before copying.";
        case Status::noEmptySlot: return "No completely empty route. Clear a row with X, then copy again.";
        case Status::busy: return "Finish the current edit before copying.";
        case Status::cancelled: return "Copy interrupted; review current routes";
        case Status::invalidRoute: return "Copy unavailable. Choose a modulation route and try again.";
    }
    return {};
}

const juce::String modulationCopyTooltip = "Copy every setting to the first completely empty row without replacing another route. "
    "Bypassed routes and unfinished Off/None drafts are kept. An enabled copy adds its modulation immediately. Undo removes the copy.";

class ModulationRing final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModulationRing (AnyvocAudioProcessor& p, juce::Slider& baseIn, const juce::String& id)
        : processor (p), base (baseIn)
    {
        setComponentID (id + "ModulationRing");
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setWantsKeyboardFocus (true);
    }
    ~ModulationRing() override { finish(); }

    void select (int routeIn, int sourceIn, int destinationIn)
    {
        const juce::Component::SafePointer<ModulationRing> safe (this);
        if (gesturing && (routeIn != route || sourceIn != source || destinationIn != destination || ! validGesture())) finish();
        if (safe == nullptr) return;
        route = routeIn; source = sourceIn; destination = destinationIn;
        setVisible (route >= 0);
        if (route >= 0)
        {
            setTitle (modulationSourceName (source) + " / Route " + juce::String (route + 1) + " modulation depth");
            setTooltip (getTitle() + ": " + formatted (amount(), Unit::signedPercent)
                + ". Drag the outer ring up/down; Shift is fine adjustment. Double-click sets depth to zero; right-click chooses a route."
                  " Arc = this route's nominal range including polarity and AUX, clipped to the base knob. A grey arc is bypassed and has no effect. Other routes, filter envelope, tracking and live voices are not shown."
                + (anyvoc::lfoIndexForPhaseDestination (destination) >= 0 ? " LFO phase wraps at 360 degrees; this clipped arc does not show that wrap." : "")
                + (anyvoc::macroIndexForDestination (destination) >= 0 ? " The knob edits the macro's base value. EV shows the newest active voice's actual value, or the smoothed base while idle. Macro input reaches its source on the next audio sample; the arc does not predict feedback." : ""));
        }
        repaint();
    }
    bool hitTest (int x, int y) override
    {
        if (route < 0) return false;
        const auto geometry = ringGeometry();
        const auto point = juce::Point<float> ((float) x, (float) y) - geometry.first;
        const float distance = point.getDistanceFromOrigin();
        float angle = std::atan2 (point.x, -point.y);
        if (angle < startAngle) angle += juce::MathConstants<float>::twoPi;
        return std::abs (distance - geometry.second) <= 3.0f && angle >= startAngle && angle <= endAngle;
    }
    void paint (juce::Graphics& g) override
    {
        if (route < 0) return;
        const auto geometry = ringGeometry();
        const auto centre = geometry.first;
        const float radius = geometry.second;
        auto routeSettings = processor.getModulationRoute (route);
        const bool enabled = routeSettings.enabled;
        const auto colour = enabled ? modulationSourceColour (source) : secondaryText;
        const auto arc = [&] (float lo, float hi, juce::Colour c, float thickness)
        {
            juce::Path path;
            path.addCentredArc (centre.x, centre.y, radius, radius, 0,
                               startAngle + lo * (endAngle - startAngle), startAngle + hi * (endAngle - startAngle), true);
            g.setColour (c); g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        arc (0, 1, colour.withAlpha (0.16f), 1.3f);
        // A bypassed route keeps a faint outline of its stored excursion, so
        // users can compare it without losing the route's settings.
        routeSettings.enabled = true;
        const auto range = anyvoc::modulationRange (destination, (float) base.getValue(), routeSettings);
        const auto normalise = [this] (float n) { return (float) base.valueToProportionOfLength (juce::jlimit (base.getMinimum(), base.getMaximum(), (double) n)); };
        arc (normalise (range.first), normalise (range.second), colour.withAlpha (enabled ? (isMouseOverOrDragging() ? 1.0f : 0.8f) : 0.28f), enabled ? 2.2f : 1.3f);
        const float end = normalise (amount() >= 0 ? range.second : range.first);
        const auto handle = centre.getPointOnCircumference (radius, startAngle + end * (endAngle - startAngle));
        g.setColour (colour.withAlpha (enabled ? 1.0f : 0.5f));
        if (enabled) g.fillEllipse (juce::Rectangle<float> (5, 5).withCentre (handle));
        else g.drawEllipse (juce::Rectangle<float> (5, 5).withCentre (handle), 1.0f);
    }
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu()) { if (onMenu) onMenu(); return; }
        if (! event.mods.isLeftButtonDown() || route < 0 || ! macroTargetEditable() || processor.isUserEditActive()) return;
        if (processor.getModulationSource (route) != source || processor.getModulationDestination (route) != destination) return;
        parameter = processor.apvts.getParameter (prefix() + "Amount");
        if (parameter == nullptr) return;
        lastValue = parameter->getValue(); dragValue = amount(); lastY = event.position.y;
        gestureRoute = processor.getModulationRoute (route);
        gestureParameters = matrixRouteSnapshot (processor, route);
        revision = processor.getHistoryRevision();
        processor.beginUserEdit (getTitle());
        gesturing = true;
        const juce::Component::SafePointer<ModulationRing> safe (this);
        parameter->beginChangeGesture();
        if (safe == nullptr) return;
        if (! validGesture()) finish();
    }
    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! gesturing) return;
        if (! validGesture()) { finish(); return; }
        dragValue = juce::jlimit (-1.0f, 1.0f, dragValue + (lastY - event.position.y) * (event.mods.isShiftDown() ? 0.001f : 0.005f));
        lastY = event.position.y;
        const auto expected = parameter->convertTo0to1 (parameter->getNormalisableRange().snapToLegalValue (dragValue));
        const juce::Component::SafePointer<ModulationRing> safe (this);
        processor.setUserParameter (prefix() + "Amount", dragValue);
        if (safe == nullptr) return;
        // A host can replace the sound synchronously from the parameter callback.
        if (processor.getHistoryRevision() != revision || std::abs (parameter->getValue() - expected) > 1.0e-6f)
        { finish(); return; }
        lastValue = parameter->getValue();
        if (! validGesture()) { finish(); return; }
        if (onDepthChanged) onDepthChanged (route, amount());
        if (safe == nullptr) return;
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { finish(); }
    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        const juce::Component::SafePointer<ModulationRing> safe (this);
        finish();
        if (safe == nullptr) return;
        if (! event.mods.isLeftButtonDown() || route < 0) return;
        // Re-use the guarded gesture path so a host callback cannot redirect this edit.
        mouseDown (event);
        if (safe == nullptr) return;
        if (! gesturing) return;
        dragValue = 0; lastY = event.position.y;
        mouseDrag (event);
        if (safe != nullptr) finish();
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey) { finish(); return true; }
        return false;
    }
    void focusLost (FocusChangeType) override { finish(); }
    void visibilityChanged() override { if (! isShowing()) finish(); }
    std::function<void()> onMenu;
    std::function<void (int, float)> onDepthChanged;
private:
    std::pair<juce::Point<float>, float> ringGeometry() const
    {
        const auto bounds = base.getLookAndFeel().getSliderLayout (base).sliderBounds.toFloat().translated ((float) base.getX(), (float) base.getY()).reduced (2);
        return { bounds.getCentre(), juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f + 4.0f };
    }
    juce::String prefix() const { return "mod" + juce::String (route + 1); }
    float amount() const { return route >= 0 ? value (processor, prefix() + "Amount") : 0; }
    bool macroTargetEditable() const
    {
        return anyvoc::macroIndexForDestination (destination) < 0 || (matrixHierarchyVisible (this) && isEnabled());
    }
    bool validGesture() const
    {
        auto current = processor.getModulationRoute (route);
        current.amount = gestureRoute.amount;
        auto currentParameters = matrixRouteSnapshot (processor, route);
        currentParameters[2] = gestureParameters[2]; // Amount is the one value owned by this gesture.
        return gesturing && parameter != nullptr && macroTargetEditable() && processor.getHistoryRevision() == revision
            && currentParameters == gestureParameters
            && processor.getModulationSource (route) == source
            && processor.getModulationDestination (route) == destination
            && current.enabled == gestureRoute.enabled && current.polarity == gestureRoute.polarity
            && current.curve == gestureRoute.curve && current.auxSource == gestureRoute.auxSource
            && current.auxPolarity == gestureRoute.auxPolarity && current.auxCurve == gestureRoute.auxCurve
            && std::abs (parameter->getValue() - lastValue) <= 1.0e-6f;
    }
    void finish()
    {
        if (! gesturing) return;
        gesturing = false;
        auto* owner = &processor;
        const juce::Component::SafePointer<ModulationRing> safe (this);
        parameter->endChangeGesture(); owner->endUserEdit();
        if (safe != nullptr) repaint();
    }
    AnyvocAudioProcessor& processor;
    juce::Slider& base;
    juce::RangedAudioParameter* parameter = nullptr;
    int route = -1, source = 0, destination = 0;
    float lastY = 0, dragValue = 0, lastValue = 0;
    std::uint64_t revision = 0;
    anyvoc::ModRoute gestureRoute;
    MatrixRouteSnapshot gestureParameters{};
    bool gesturing = false;
    static constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
    static constexpr float endAngle = juce::MathConstants<float>::pi * 2.75f;
};

class Knob final : public juce::Component, public juce::DragAndDropTarget
{
public:
    Knob (AnyvocAudioProcessor& processor, const juce::String& parameterId,
          juce::String captionIn, juce::String tip, Unit unit, juce::Colour accent)
        : ring (processor, slider, parameterId), caption (std::move (captionIn))
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 17);
        styleSliderText (slider);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setMouseDragSensitivity (180);
        slider.setVelocityModeParameters (0.25, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        slider.setScrollWheelEnabled (false);
        slider.setTooltip (tip + " Double-click to reset. Shift-drag for fine adjustment.");
        slider.setTitle (caption);
        slider.setDescription (tip);
        slider.addMouseListener (this, true);
        addAndMakeVisible (slider);
        attachment = std::make_unique<SoundSliderAttachment>
                         (processor, parameterId, slider);
        slider.textFromValueFunction = [unit] (double n) { return formatted (n, unit); };
        slider.valueFromTextFunction = [unit] (const juce::String& text)
        {
            auto n = text.getDoubleValue();
            if (unit == Unit::percent || unit == Unit::signedPercent) n /= 100.0;
            if (unit == Unit::degrees) n /= 360.0;
            if (unit == Unit::hertz && text.containsIgnoreCase ("k")) n *= 1000.0;
            if (unit == Unit::seconds && text.containsIgnoreCase ("ms")) n /= 1000.0;
            if (unit == Unit::pan && text.containsIgnoreCase ("L")) n = -std::abs (n) / 100;
            else if (unit == Unit::pan && text.containsIgnoreCase ("R")) n = std::abs (n) / 100;
            if (unit == Unit::note)
                for (int note = 0; note <= 127; ++note)
                    if (text.trim().equalsIgnoreCase (juce::MidiMessage::getMidiNoteName (note, true, true, 4)))
                        return (double) note;
            return n;
        };
        if (auto* p = processor.apvts.getParameter (parameterId))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.updateText();
        addChildComponent (ring);
    }
    void paint (juce::Graphics& g) override
    {
        if (targetHighlighted || dragHovered)
        {
            g.setColour (mint.withAlpha (dragHovered ? 0.13f : 0.045f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1), 8);
            g.setColour (mint.withAlpha (dragHovered ? 0.9f : 0.4f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1), 8, dragHovered ? 1.6f : 1.0f);
        }
        drawText (g, caption, { 0, 0, getWidth(), 17 }, 10.5f, secondaryText, false,
                  juce::Justification::centred);
        if (routeCount > 0)
        {
            g.setColour (mint);
            g.fillEllipse ((float) getWidth() - 8, 6, 4, 4);
        }
    }
    void resized() override
    {
        const int top = modulationDestination > 0 ? 21 : 15;
        slider.setBounds (0, top, getWidth(), getHeight() - top);
        ring.setBounds (getLocalBounds());
    }
    void setCaption (const juce::String& text)
    {
        if (caption != text) { caption = text; slider.setTitle (text); repaint(); }
    }
    void setModulationTarget (int destination)
    {
        modulationDestination = destination;
        resized();
        slider.setTooltip (slider.getTooltip() + " Drag a source chip here to add modulation. Right-click to edit routes.");
    }
    int getModulationDestination() const { return modulationDestination; }
    void setTargetHighlighted (bool enabled) { targetHighlighted = enabled; repaint(); }
    void setRouteCount (int count)
    {
        if (routeCount != count) { routeCount = count; repaint(); }
    }
    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        return isEnabled() && modulationDestination > 0
            && (anyvoc::macroIndexForDestination (modulationDestination) < 0 || matrixHierarchyVisible (this))
            && (! slider.userEditAllowed || slider.userEditAllowed())
            && details.description.toString().startsWith ("anyvoc.mod:");
    }
    void itemDragEnter (const SourceDetails& details) override { dragHovered = isInterestedInDragSource (details); repaint(); }
    void itemDragExit (const SourceDetails&) override { dragHovered = false; repaint(); }
    void itemDropped (const SourceDetails& details) override
    {
        dragHovered = false;
        // A host can change the LFO type after drag enter, before this callback.
        if (! isInterestedInDragSource (details)) { repaint(); return; }
        const int source = details.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
        const int destination = modulationDestination;
        const auto dropped = onModulationDrop;
        const juce::Component::SafePointer<Knob> safe (this);
        if (dropped) dropped (source, destination);
        if (safe != nullptr) repaint();
    }
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu() && modulationDestination > 0 && onModulationMenu)
            onModulationMenu (modulationDestination, this);
    }
    std::function<void (int, int)> onModulationDrop;
    std::function<void (int, juce::Component*)> onModulationMenu;
    SoundSlider slider;
    ModulationRing ring;
private:
    juce::String caption;
    std::unique_ptr<SoundSliderAttachment> attachment;
    int modulationDestination = 0, routeCount = 0;
    bool targetHighlighted = false, dragHovered = false;
};

class ModulationChip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModulationChip (int sourceIn, juce::String nameIn, juce::Colour accentIn)
        : source (sourceIn), name (std::move (nameIn)), accent (accentIn)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Click to show " + name + " modulation rings. Drag this chip onto a highlighted knob to add a route; drag the outer ring up/down to change depth. Right-click a knob to choose between routes.");
        setTitle (name + " modulation source");
        setComponentID ("modSource" + juce::String (source));
        setWantsKeyboardFocus (true);
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (selected ? accent.withAlpha (0.2f) : isMouseOverOrDragging() ? accent.withAlpha (0.12f) : insetColour);
        g.fillRoundedRectangle (r, 5);
        g.setColour (accent.withAlpha (selected ? 0.95f : isMouseOverOrDragging() ? 0.65f : 0.25f));
        g.drawRoundedRectangle (r, 5, 1);
        g.setColour (accent);
        g.fillEllipse (8, r.getCentreY() - 2, 4, 4);
        drawText (g, name, getLocalBounds().withTrimmedLeft (16).withTrimmedRight (5), 9.5f,
                  primaryText, true, juce::Justification::centred);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent& event) override { if (event.mods.isLeftButtonDown() && onSelect) onSelect (source); }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
        { if (onSelect) onSelect (source); return true; }
        return false;
    }
    void selectSource (int selectedSource) { selected = selectedSource == source; repaint(); }
    std::function<void (int)> onSelect;
    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (event.getDistanceFromDragStart() < 4) return;
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! container->isDragAndDropActive())
                container->startDragging ("anyvoc.mod:" + juce::String (source), this,
                                          juce::ScaledImage(), false, nullptr, &event.source);
    }
private:
    int source;
    juce::String name;
    juce::Colour accent;
    bool selected = false;
};

class ModulationSourcePalette final : public juce::Component
{
public:
    ModulationSourcePalette()
    {
        setComponentID ("ModulationSourcePalette"); setBounds (0, 0, designWidth, designHeight);
        addAndMakeVisible (close); close.setButtonText ("DONE"); close.setComponentID ("SourcePaletteClose"); close.setBounds (854, 134, 80, 29);
        close.onClick = [this] { setVisible (false); };
        const auto add = [this] (int source, int x, int y, int width)
        {
            auto chip = std::make_unique<ModulationChip> (source, modulationSourceName (source).toUpperCase(), modulationSourceColour (source));
            chip->setComponentID ("paletteModSource" + juce::String (source)); chip->setBounds (x, y, width, 27);
            chip->onSelect = [this] (int selected) { if (onSelect) onSelect (selected); };
            addAndMakeVisible (*chip); chips.push_back (std::move (chip));
        };
        for (int i = 0; i < 10; ++i) add (i < 2 ? i + 1 : i + 8, 184 + (i % 5) * 150, 197 + (i / 5) * 34, 138);
        const std::array<int, 4> envelopeSources { 18, 3, 19, 20 };
        for (int i = 0; i < 4; ++i) add (envelopeSources[(size_t) i], 184 + i * 188, 296, 174);
        for (int i = 0; i < 8; ++i) add (i < 4 ? i + 6 : i + 17, 184 + (i % 4) * 188, 361 + (i / 4) * 34, 174);
        add (4, 184, 460, 174); add (5, 372, 460, 174);
    }
    void select (int source) { for (auto& chip : chips) chip->selectSource (source); }
    bool hitTest (int x, int y) override { return juce::Rectangle<int> (24, 117, 1076, 461).contains (x, y); }
    void mouseDown (const juce::MouseEvent& e) override { if (! juce::Rectangle<int> (154, 117, 812, 461).contains (e.getPosition())) setVisible (false); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (background.withAlpha (0.82f)); g.fillRoundedRectangle (24, 117, 1076, 461, 12);
        drawCard (g, { 154, 117, 812, 461 }, "MODULATION SOURCES", "", mint);
        drawText (g, "10 LFOs  /  4 envelopes  /  8 macros  /  performance", { 430, 132, 410, 25 }, 10, secondaryText);
        drawText (g, "LFOS", { 184, 174, 748, 17 }, 10, mint, true);
        drawText (g, "ENVELOPES", { 184, 273, 748, 17 }, 10, blue, true);
        drawText (g, "MACROS", { 184, 338, 748, 17 }, 10, lilac, true);
        drawText (g, "PERFORMANCE", { 184, 437, 748, 17 }, 10, orange, true);
        drawText (g, "Click a source to select its modulation rings. Drag a chip onto a target knob to add a route.", { 184, 517, 748, 22 }, 11, primaryText);
        drawText (g, "The keyboard remains available below. DONE returns to your sound.", { 184, 546, 748, 18 }, 10.5f, secondaryText);
    }
    std::function<void (int)> onSelect;
private:
    juce::TextButton close;
    std::vector<std::unique_ptr<ModulationChip>> chips;
};

class Choice final : public juce::Component
{
public:
    Choice (AnyvocAudioProcessor& p, const juce::String& id, juce::String captionIn,
            const juce::StringArray& options, juce::String tip, std::function<bool()> allowed = {})
        : caption (std::move (captionIn))
    {
        box.addItemList (options, 1);
        box.setTooltip (tip);
        box.setTitle (caption);
        addAndMakeVisible (box);
        attachment = std::make_unique<SoundChoiceAttachment> (p, id, box, std::move (allowed));
    }
    void paint (juce::Graphics& g) override
    {
        if (caption.isNotEmpty()) drawText (g, caption, { 0, 0, getWidth(), 16 }, 10.5f);
    }
    void resized() override
    {
        const int labelHeight = caption.isEmpty() ? 0 : 19;
        box.setBounds (0, labelHeight, getWidth(), getHeight() - labelHeight);
    }
    juce::ComboBox box;
private:
    juce::String caption;
    std::unique_ptr<SoundChoiceAttachment> attachment;
};

const juce::StringArray modulationDestinationNames { "Off", "Filter A Cutoff", "Osc A Position", "Osc B Position", "Osc C Position",
                                                    "Pitch", "Level", "Pan", "Filter B Cutoff", "Filter A Resonance", "Filter B Resonance", "Parallel Balance",
                                                    "Osc A Warp 1", "Osc A Warp 2", "Osc B Warp 1", "Osc B Warp 2", "Osc C Warp 1", "Osc C Warp 2",
                                                    "LFO 1 Phase", "LFO 2 Phase", "LFO 3 Phase", "LFO 4 Phase", "LFO 5 Phase", "LFO 6 Phase", "LFO 7 Phase", "LFO 8 Phase", "LFO 9 Phase", "LFO 10 Phase",
                                                    "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8" };

class ModulationDestinationChoice final : public juce::Component
{
public:
    ModulationDestinationChoice (AnyvocAudioProcessor& p, int routeIn, const juce::String& suffix = {}) : processor (p), route (routeIn)
    {
        addAndMakeVisible (box); box.addItemList (modulationDestinationNames, 1);
        box.setTitle ("Modulation " + juce::String (route + 1) + " destination");
        box.setComponentID ("mod" + juce::String (route + 1) + "Destination" + suffix);
        box.setTooltip ("Choose what this source moves. LFO Phase shifts the curve's reading position; it has no effect on Random, Lorenz or Rossler. Warp targets require an active warp mode. Macro inputs add to the base separately for each voice, clamp to 0-100%, and feed the next audio sample, including self or mutual modulation.");
        box.onChange = [this]
        {
            const auto selected = box.getSelectedItemIndex();
            if (selected < 0) return;
            const bool macroEdit = anyvoc::macroIndexForDestination (selected) >= 0
                                || anyvoc::macroIndexForDestination (processor.getModulationDestination (route)) >= 0;
            if (macroEdit && ! matrixHierarchyVisible (this)) { refresh(); return; }
            const juce::Component::SafePointer<ModulationDestinationChoice> safe (this);
            processor.setModulationDestination (route, selected, [safe, macroEdit]
            { return safe != nullptr && (! macroEdit || matrixHierarchyVisible (safe.getComponent())); });
            if (safe != nullptr) refresh();
        };
        const auto prefix = "mod" + juce::String (route + 1);
        const std::array<const char*, 9> suffixes { "Dest", "DestBank", "FilterTarget", "WarpEnabled", "WarpTarget", "LfoPhaseEnabled", "LfoPhaseTarget", "MacroEnabled", "MacroTarget" };
        for (size_t i = 0; i < parameters.size(); ++i) parameters[i] = processor.apvts.getParameter (prefix + suffixes[i]);
        const juce::Component::SafePointer<ModulationDestinationChoice> safe (this);
        for (size_t i = 0; i < attachments.size(); ++i)
            attachments[i] = std::make_unique<juce::ParameterAttachment> (*parameters[i], [safe] (float) { if (safe != nullptr) safe->refresh(); });
        refresh();
    }
    void refresh()
    {
        const auto plain = [this] (size_t i) { return juce::roundToInt (parameters[i]->convertFrom0to1 (parameters[i]->getValue())); };
        const int destination = plain (7) != 0 ? anyvoc::macroDestinationId (plain (8))
                              : plain (5) != 0 ? 18 + plain (6) : plain (3) != 0 ? 12 + plain (4)
                              : plain (1) != 0 ? 8 + plain (2) : plain (0);
        box.setSelectedItemIndex (destination, juce::dontSendNotification);
    }
    void resized() override { box.setBounds (getLocalBounds()); }
    void focusControl() { if (box.isShowing()) box.grabKeyboardFocus(); }
private:
    AnyvocAudioProcessor& processor;
    int route;
    juce::ComboBox box;
    std::array<juce::RangedAudioParameter*, 9> parameters{};
    std::array<std::unique_ptr<juce::ParameterAttachment>, 9> attachments;
};

class ModulationSourceChoice final : public juce::Component
{
public:
    ModulationSourceChoice (AnyvocAudioProcessor& p, int routeIn, bool auxiliaryIn,
                            juce::String captionIn, const juce::String& tip, const juce::String& suffix = {})
        : processor (p), route (routeIn), auxiliary (auxiliaryIn), caption (std::move (captionIn))
    {
        const auto id = "mod" + juce::String (route + 1) + (auxiliary ? "AuxSource" : "Source");
        for (int source = 0; source <= 24; ++source)
            box.addItem (auxiliary && source == 0 ? "None" : modulationSourceName (source), source + 1);
        box.setComponentID (id + suffix); box.setTitle (caption);
        box.setTooltip (tip + " Macro sources use the voice's effective value, including incoming modulation from the previous audio sample. Different notes may have different values.");
        addAndMakeVisible (box);
        box.onChange = [this]
        {
            const int selected = box.getSelectedItemIndex();
            if (selected < 0) return;
            const juce::Component::SafePointer<ModulationSourceChoice> safe (this);
            processor.setModulationSource (route, selected, auxiliary, [safe] { return safe != nullptr; });
            if (safe != nullptr) refresh();
        };
        const juce::Component::SafePointer<ModulationSourceChoice> safe (this);
        const auto changed = [safe] (float) { if (safe != nullptr) safe->refresh(); };
        legacyParameter = processor.apvts.getParameter (id);
        extraParameter = processor.apvts.getParameter (id + "Extra");
        legacyAttachment = std::make_unique<juce::ParameterAttachment> (*legacyParameter, changed);
        extraAttachment = std::make_unique<juce::ParameterAttachment> (*extraParameter, changed);
        refresh();
    }
    void refresh()
    {
        // A parameter listener may run before APVTS publishes its raw value.
        // Read both selectors from the parameters whose values are already set.
        const int extra = juce::roundToInt (extraParameter->convertFrom0to1 (extraParameter->getValue()));
        const int legacy = juce::roundToInt (legacyParameter->convertFrom0to1 (legacyParameter->getValue()));
        box.setSelectedItemIndex (extra > 0 ? extra + 9 : legacy, juce::dontSendNotification);
    }
    void paint (juce::Graphics& g) override { if (caption.isNotEmpty()) drawText (g, caption, { 0, 0, getWidth(), 16 }, 10.5f); }
    void resized() override { const int top = caption.isEmpty() ? 0 : 19; box.setBounds (0, top, getWidth(), getHeight() - top); }
    juce::ComboBox box;
private:
    AnyvocAudioProcessor& processor;
    int route;
    bool auxiliary;
    juce::String caption;
    juce::RangedAudioParameter* legacyParameter = nullptr;
    juce::RangedAudioParameter* extraParameter = nullptr;
    std::unique_ptr<juce::ParameterAttachment> legacyAttachment, extraAttachment;
};

const juce::StringArray matrixPolarities { "Original", "Unipolar", "Bipolar" };

void configureMatrixAmount (SoundSlider& slider)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 22);
    styleSliderText (slider);
    slider.setScrollWheelEnabled (false);
    slider.setTooltip ("Strength and direction of this route. A negative amount reverses the movement. Double-click resets the amount.");
}

void formatMatrixAmount (SoundSlider& slider)
{
    slider.textFromValueFunction = [] (double n) { return formatted (n, Unit::signedPercent); };
    slider.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue() / 100.0; };
    slider.updateText();
}

bool matrixHierarchyVisible (const juce::Component* component)
{
    for (auto* current = component; current != nullptr; current = current->getParentComponent())
        if (! current->isVisible()) return false;
    return true;
}

class ModulationRouteResponse final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModulationRouteResponse (AnyvocAudioProcessor& p, int routeIn, bool auxiliaryIn)
        : processor (p), route (routeIn), auxiliary (auxiliaryIn)
    {
        setComponentID ("mod" + juce::String (route + 1) + (auxiliary ? "AuxResponse" : "SourceResponse"));
        setTooltip ("Response of this source shaping stage. The horizontal axis is the incoming source value; the vertical axis is the shaped value. This is a transfer curve, not a live signal display.");
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = processor.getModulationRoute (route);
        const int source = auxiliary ? r.auxSource : r.source;
        const int polarity = auxiliary ? r.auxPolarity : r.polarity;
        const float curve = auxiliary ? r.auxCurve : r.curve;
        const auto plot = getLocalBounds().withTrimmedBottom (20).reduced (1).toFloat();
        g.setColour (insetColour); g.fillRoundedRectangle (plot, 6);
        g.setColour (borderColour);
        for (int i = 1; i < 4; ++i)
        {
            const float t = (float) i / 4.0f;
            g.drawVerticalLine (juce::roundToInt (plot.getX() + t * plot.getWidth()), plot.getY(), plot.getBottom());
            g.drawHorizontalLine (juce::roundToInt (plot.getY() + t * plot.getHeight()), plot.getX(), plot.getRight());
        }
        const bool rawBipolar = anyvoc::modulationSourceIsBipolar (source, 0);
        const bool outputBipolar = anyvoc::modulationSourceIsBipolar (source, polarity);
        juce::Path path;
        for (int i = 0; i <= 256; ++i)
        {
            const float t = (float) i / 256.0f;
            const float raw = rawBipolar ? 2.0f * t - 1.0f : t;
            const float shaped = auxiliary && source == 0 ? 1.0f : anyvoc::transformModulationSource (raw, source, polarity, curve);
            const float normalised = outputBipolar && source != 0 ? 0.5f + shaped * 0.5f : shaped;
            const juce::Point<float> point { plot.getX() + t * plot.getWidth(), plot.getBottom() - 3.0f - normalised * (plot.getHeight() - 6.0f) };
            if (i == 0) path.startNewSubPath (point); else path.lineTo (point);
        }
        g.setColour ((auxiliary ? lilac : mint).withAlpha (r.enabled ? 1.0f : 0.35f));
        g.strokePath (path, juce::PathStrokeType (2.0f));
        const auto caption = auxiliary && source == 0 ? "NO AUX  /  MULTIPLIER = 1"
            : source == 0 ? "SOURCE OFF  /  NO MOVEMENT"
            : juce::String (rawBipolar ? "INPUT  -1 ... +1" : "INPUT  0 ... 1")
                + "     >     OUTPUT  " + (outputBipolar ? "-1 ... +1" : "0 ... 1");
        drawText (g, caption, getLocalBounds().withTrimmedTop (getHeight() - 18), 9, secondaryText, false, juce::Justification::centred);
    }
private:
    AnyvocAudioProcessor& processor;
    int route;
    bool auxiliary;
};

class ModulationRouteDetail final : public juce::Component
{
public:
    ModulationRouteDetail (AnyvocAudioProcessor& p, int routeIn)
        : processor (p), route (routeIn), prefix ("mod" + juce::String (route + 1)),
          source (p, route, false, "SOURCE", "The source that moves the destination.", "Detail"),
          destination (p, route, "Detail"),
          polarity (p, prefix + "Polarity", "POLARITY", matrixPolarities, "Original keeps the source's native range. Unipolar moves from 0 to 1; Bipolar moves from -1 to +1."),
          auxSource (p, route, true, "AUX SOURCE", "The shaped auxiliary source multiplies the main source. None means full depth, with no auxiliary scaling."),
          auxPolarity (p, prefix + "AuxPolarity", "POLARITY", matrixPolarities, "Choose the auxiliary range before its curve. A bipolar auxiliary source can reverse the modulation."),
          curve (p, prefix + "Curve", "CURVE", "Bend the main source response. Zero is linear; negative values rise earlier, positive values rise later.", Unit::signedPercent, mint),
          auxCurve (p, prefix + "AuxCurve", "CURVE", "Bend the auxiliary response before it multiplies the main source.", Unit::signedPercent, lilac),
          sourceResponse (p, route, false), auxResponse (p, route, true)
    {
        setComponentID ("MatrixRouteDetail");
        for (auto* component : std::initializer_list<juce::Component*> { &source, &destination, &polarity, &auxSource, &auxPolarity,
                                  &curve, &auxCurve, &sourceResponse, &auxResponse, &amount, &enabled, &copy, &close, &previous, &next })
            addAndMakeVisible (component);
        source.box.setComponentID (prefix + "SourceDetail");
        source.box.setTitle ("Modulation " + juce::String (route + 1) + " source");
        configureMatrixAmount (amount);
        amount.setTitle ("Modulation " + juce::String (route + 1) + " amount");
        amountAttachment = std::make_unique<SoundSliderAttachment> (processor, prefix + "Amount", amount);
        amount.setComponentID (prefix + "AmountDetail"); formatMatrixAmount (amount);
        enabled.setButtonText ("ROUTE ON"); enabled.setClickingTogglesState (true);
        enabled.setTitle ("Enable modulation route " + juce::String (route + 1));
        enabled.setTooltip ("Bypass keeps every setting so you can compare this route with and without its movement.");
        enabledAttachment = std::make_unique<SoundButtonAttachment> (processor, prefix + "Enabled", enabled);
        enabled.setComponentID (prefix + "EnabledDetail");
        curve.slider.setTitle ("Modulation " + juce::String (route + 1) + " source curve");
        auxCurve.slider.setTitle ("Modulation " + juce::String (route + 1) + " auxiliary curve");
        copy.setButtonText ("COPY"); copy.setComponentID (prefix + "CopyDetail");
        copy.setTitle ("Copy modulation route " + juce::String (route + 1)); copy.setTooltip (modulationCopyTooltip);
        copy.onClick = [this] { const auto callback = onCopy; if (callback) callback (route, &copy); };
        close.setButtonText ("BACK"); close.setComponentID ("MatrixDetailClose");
        close.onClick = [this] { if (onClose) onClose(); };
        previous.setButtonText ("<"); previous.setComponentID ("MatrixRouteDetailPrev"); previous.setTooltip ("Edit the previous modulation route.");
        next.setButtonText (">"); next.setComponentID ("MatrixRouteDetailNext"); next.setTooltip ("Edit the next modulation route.");
        previous.setEnabled (route > 0); next.setEnabled (route + 1 < matrixRouteCount);
        previous.onClick = [this] { if (onNavigate) onNavigate (route - 1); };
        next.onClick = [this] { if (onNavigate) onNavigate (route + 1); };
        refresh();
    }
    void refresh()
    {
        source.refresh(); auxSource.refresh(); destination.refresh();
        const bool auxiliary = processor.getModulationSource (route, true) > 0;
        auxPolarity.setEnabled (auxiliary); auxCurve.setEnabled (auxiliary);
        auxPolarity.setAlpha (auxiliary ? 1.0f : 0.4f); auxCurve.setAlpha (auxiliary ? 1.0f : 0.4f);
        enabled.setButtonText (value (processor, prefix + "Enabled") > 0.5f ? "ROUTE ON" : "BYPASSED");
        repaint(); sourceResponse.repaint(); auxResponse.repaint();
    }
    void resized() override
    {
        copy.setBounds (getWidth() - 306, 15, 98, 28);
        close.setBounds (getWidth() - 112, 15, 92, 28);
        previous.setBounds (getWidth() - 200, 15, 32, 28); next.setBounds (getWidth() - 160, 15, 32, 28);
        source.setBounds (22, 67, 202, 44); destination.setBounds (244, 86, 258, 25);
        amount.setBounds (545, 86, 291, 25); enabled.setBounds (870, 86, 184, 25);
        polarity.setBounds (38, 173, 172, 46); curve.setBounds (404, 157, 94, 80);
        auxSource.setBounds (568, 173, 178, 46); auxPolarity.setBounds (760, 173, 160, 46); auxCurve.setBounds (942, 157, 94, 80);
        sourceResponse.setBounds (38, 245, 474, 139); auxResponse.setBounds (568, 245, 474, 139);
    }
    void paint (juce::Graphics& g) override
    {
        drawCard (g, getLocalBounds(), "ROUTE " + juce::String (route + 1).paddedLeft ('0', 2) + "  /  SHAPE & AUX", "", mint);
        drawText (g, "Shaped source  x  shaped aux  x  amount", { 300, 16, 450, 26 }, 11, secondaryText);
        drawText (g, "DESTINATION", { 244, 67, 250, 16 }, 10.5f);
        drawText (g, "AMOUNT", { 545, 67, 270, 16 }, 10.5f);
        drawCard (g, { 18, 127, 512, 274 }, "SOURCE RESPONSE", "", mint);
        drawCard (g, { 548, 127, 510, 274 }, "AUX RESPONSE", "", lilac);
        drawText (g, anyvoc::macroIndexForDestination (processor.getModulationDestination (route)) >= 0
                         ? "Macro input adds to its base per voice, clips to 0-100%, and reaches its source on the next audio sample."
                         : "Choose a curve to change how the input responds; the line shows the result before AMOUNT.",
                  { 24, 412, 1020, 18 }, 11, secondaryText);
        drawText (g, "AUX None keeps full depth. An AUX value of zero stops this route; a negative AUX reverses its direction.", { 24, 435, 1020, 17 }, 10.5f, primaryText);
    }
    std::function<void()> onClose;
    std::function<void (int)> onNavigate;
    std::function<void (int, juce::Component*)> onCopy;
private:
    AnyvocAudioProcessor& processor;
    int route;
    juce::String prefix;
    ModulationSourceChoice source;
    ModulationDestinationChoice destination;
    Choice polarity;
    ModulationSourceChoice auxSource;
    Choice auxPolarity;
    Knob curve, auxCurve;
    ModulationRouteResponse sourceResponse, auxResponse;
    SoundSlider amount;
    juce::TextButton enabled, copy, close, previous, next;
    std::unique_ptr<SoundSliderAttachment> amountAttachment;
    std::unique_ptr<SoundButtonAttachment> enabledAttachment;
};

class ModulationRouteHandle final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ModulationRouteHandle (int routeIn) : route (routeIn)
    {
        setComponentID ("ModulationRouteHandle" + juce::String (route + 1));
        setTitle ("Move modulation route " + juce::String (route + 1));
        setTooltip ("Drag to move this complete route. Expanded view scrolls at the edges. You can also focus this handle and press Up or Down to move between rows.");
        setMouseCursor (juce::MouseCursor::DraggingHandCursor); setWantsKeyboardFocus (true);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (isMouseOverOrDragging() || hasKeyboardFocus (false) ? mint : secondaryText);
        for (int y : { 8, 12, 16 }) for (int x : { 6, 10 }) g.fillEllipse ((float) x, (float) y, 2.0f, 2.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isLeftButtonDown() && onStart) onStart (route); }
    void mouseDrag (const juce::MouseEvent& e) override { if (e.getDistanceFromDragStart() >= 4 && onDrag) onDrag (route, e.getScreenPosition()); }
    void mouseUp (const juce::MouseEvent& e) override { if (onDrop) onDrop (route, e.getScreenPosition(), e.getDistanceFromDragStart() >= 4); }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        { if (onStep) onStep (route, key == juce::KeyPress::upKey ? -1 : 1); return true; }
        return false;
    }
    std::function<void (int)> onStart;
    std::function<void (int, juce::Point<int>)> onDrag;
    std::function<void (int, juce::Point<int>, bool)> onDrop;
    std::function<void (int, int)> onStep;
private:
    int route;
};

class ModulationMatrixRow final : public juce::Component
{
public:
    ModulationMatrixRow (AnyvocAudioProcessor& p, int index)
        : handle (index), processor (p), route (index), prefix ("mod" + juce::String (route + 1)),
          source (p, route, false, "", "What moves the sound. Off leaves this route inactive."), destination (p, route)
    {
        for (auto* component : std::initializer_list<juce::Component*> { &handle, &source, &destination, &amount, &enabled, &copy, &edit, &clear }) addAndMakeVisible (component);
        source.box.setTitle ("Modulation " + juce::String (route + 1) + " source");
        configureMatrixAmount (amount);
        amount.setTitle ("Modulation " + juce::String (route + 1) + " amount");
        amountAttachment = std::make_unique<SoundSliderAttachment> (processor, prefix + "Amount", amount);
        formatMatrixAmount (amount);
        enabled.setButtonText ("ON"); enabled.setClickingTogglesState (true);
        enabled.setTitle ("Enable modulation route " + juce::String (route + 1));
        enabled.setTooltip ("Enable or bypass this route. Bypass keeps the source, destination, curve and auxiliary settings.");
        enabledAttachment = std::make_unique<SoundButtonAttachment> (processor, prefix + "Enabled", enabled);
        edit.setButtonText ("EDIT"); edit.setComponentID (prefix + "Edit"); edit.setTooltip ("Edit polarity, response curves and an auxiliary source for this route.");
        edit.setTitle ("Edit modulation route " + juce::String (route + 1) + " curve and auxiliary source");
        edit.onClick = [this] { if (onEdit) onEdit (route); };
        copy.setButtonText ("COPY"); copy.setComponentID (prefix + "Copy");
        copy.setTitle ("Copy modulation route " + juce::String (route + 1)); copy.setTooltip (modulationCopyTooltip);
        copy.onClick = [this] { const auto callback = onCopy; if (callback) callback (route, &copy); };
        clear.setButtonText ("X"); clear.setComponentID (prefix + "Clear"); clear.setTitle ("Clear route " + juce::String (route + 1));
        clear.setTooltip ("Clear this complete route. Undo restores all of its settings.");
        clear.onClick = [this]
        {
            const juce::Component::SafePointer<ModulationMatrixRow> safe (this);
            processor.resetModulationRoute (route);
            if (safe != nullptr) refresh();
        };
        refresh();
    }
    void refresh()
    {
        source.refresh(); destination.refresh();
        const auto r = processor.getModulationRoute (route);
        enabled.setButtonText (r.enabled ? "ON" : "OFF");
        const float alpha = r.enabled ? 1.0f : 0.44f;
        source.setAlpha (alpha); destination.setAlpha (alpha); amount.setAlpha (alpha);
        edit.setButtonText (r.auxSource != 0 || r.polarity != 0 || r.curve != 0 || r.auxPolarity != 0 || r.auxCurve != 0 ? "EDIT *" : "EDIT");
        repaint();
    }
    void resized() override
    {
        const int y = (getHeight() - 22) / 2;
        handle.setBounds (0, y - 1, 18, 25); enabled.setBounds (48, y, 44, 22);
        source.setBounds (104, y, 190, 22); destination.setBounds (316, y, 243, 22);
        amount.setBounds (579, y, 249, 22); copy.setBounds (842, y, 54, 22);
        edit.setBounds (906, y, 64, 22); clear.setBounds (980, y, 32, 22);
    }
    void paint (juce::Graphics& g) override
    {
        drawText (g, juce::String (route + 1).paddedLeft ('0', 2), { 20, 0, 28, getHeight() }, 9.5f, secondaryText);
        drawText (g, ">", { 296, 0, 20, getHeight() }, 11, mint, false, juce::Justification::centred);
        g.setColour (borderColour.withAlpha (0.45f)); g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
    }
    ModulationRouteHandle handle;
    std::function<void (int)> onEdit;
    std::function<void (int, juce::Component*)> onCopy;
    void focusDestination() { destination.focusControl(); }
private:
    AnyvocAudioProcessor& processor;
    int route;
    juce::String prefix;
    ModulationSourceChoice source;
    ModulationDestinationChoice destination;
    SoundSlider amount;
    juce::TextButton enabled, copy, edit, clear;
    std::unique_ptr<SoundSliderAttachment> amountAttachment;
    std::unique_ptr<SoundButtonAttachment> enabledAttachment;
};

class MatrixViewport final : public juce::Viewport
{
public:
    bool expanded = false;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    { if (expanded) juce::Viewport::mouseWheelMove (e, wheel); }
};

class ModulationMatrix final : public juce::Component
{
public:
    explicit ModulationMatrix (AnyvocAudioProcessor& p) : processor (p)
    {
        setComponentID ("ModulationMatrix"); setInterceptsMouseClicks (true, true);
        addAndMakeVisible (viewport); viewport.setViewedComponent (&content, false); viewport.setComponentID ("MatrixViewport");
        for (auto* button : { &previous, &next, &expand, &compact }) addAndMakeVisible (button);
        previous.setButtonText ("<"); previous.setComponentID ("MatrixPreviousPage"); previous.setTooltip ("Show the previous eight modulation routes.");
        next.setButtonText (">"); next.setComponentID ("MatrixNextPage"); next.setTooltip ("Show the next eight modulation routes.");
        expand.setButtonText ("EXPAND"); expand.setComponentID ("MatrixExpand"); expand.setTooltip ("Show a scrollable view of all 32 routes, with drag handles to move them.");
        compact.setButtonText ("COMPACT"); compact.setComponentID ("MatrixCompact"); compact.setTooltip ("Show eight routes at a time, alongside the LFO and envelope controls.");
        previous.onClick = [this] { setPage (page - 1); }; next.onClick = [this] { setPage (page + 1); };
        expand.onClick = [this] { setExpanded (true); }; compact.onClick = [this] { setExpanded (false); };
        for (int i = 0; i < matrixRouteCount; ++i)
        {
            auto row = std::make_unique<ModulationMatrixRow> (processor, i);
            row->onEdit = [this] (int index) { showDetail (index); };
            row->onCopy = [this] (int index, juce::Component* origin) { copyRoute (index, origin, false); };
            row->handle.onStart = [this] (int index) { startMove (index); };
            row->handle.onDrag = [this] (int, juce::Point<int> point) { dragMove (point); };
            row->handle.onDrop = [this] (int index, juce::Point<int> point, bool moved) { finishMove (index, point, moved); };
            row->handle.onStep = [this] (int index, int delta) { stepMove (index, delta); };
            content.addAndMakeVisible (*row); rows[(size_t) i] = std::move (row);
        }
    }
    void refresh()
    {
        if (! matrixHierarchyVisible (this)) return;
        if (dragActive) dragMove (dragPoint);
        for (auto& row : rows) row->refresh();
        if (detail != nullptr && detail->isVisible()) detail->refresh();
    }
    void showExpanded() { setExpanded (true); }
    bool hitTest (int x, int y) override
    {
        return getLocalBounds().contains (x, y)
            && (expanded || (detail != nullptr && detail->isVisible()) || y >= 200);
    }
    void visibilityChanged() override
    {
        if (! matrixHierarchyVisible (this)) { cancelPendingNavigation(); dragSource = -1; dragTarget = -1; dragActive = false; }
    }
    void cancelPendingNavigation() { ++detailRequest; }
    void dismissDetail() { cancelPendingNavigation(); if (detail != nullptr) detail->setVisible (false); resized(); repaint(); }
    void showDetail (int route)
    {
        if (route < 0 || route >= matrixRouteCount) return;
        // Navigation can destroy the button that invoked us, so replace the
        // outgoing panel only after its click callback has returned.
        const juce::Component::SafePointer<ModulationMatrix> safe (this);
        const auto request = ++detailRequest;
        const auto revision = processor.getHistoryRevision();
        const auto snapshot = matrixRouteSnapshot (processor, route);
        juce::MessageManager::callAsync ([safe, route, request, revision, snapshot]
        {
            const auto isCurrent = [safe, route, request, revision, snapshot]
            {
                return safe != nullptr && safe->detailRequest == request && matrixHierarchyVisible (safe.getComponent())
                    && safe->processor.getHistoryRevision() == revision && ! safe->processor.isUserEditActive()
                    && matrixRouteSnapshot (safe->processor, route) == snapshot;
            };
            if (! isCurrent()) return;
            // Detach ownership before destruction: ending a control's gesture
            // may close the editor, so do not assign through its unique_ptr.
            auto outgoing = std::move (safe->detail);
            outgoing.reset();
            if (! isCurrent()) return;
            auto replacement = std::make_unique<ModulationRouteDetail> (safe->processor, route);
            if (! isCurrent()) return;
            safe->detail = std::move (replacement);
            safe->detail->onClose = [safe] { if (safe != nullptr) safe->dismissDetail(); };
            safe->detail->onNavigate = [safe] (int index) { if (safe != nullptr) safe->showDetail (index); };
            safe->detail->onCopy = [safe] (int index, juce::Component* origin) { if (safe != nullptr) safe->copyRoute (index, origin, true); };
            safe->addAndMakeVisible (*safe->detail); safe->detail->toFront (false);
            safe->resized(); safe->repaint();
        });
    }
    void reportCopyResult (int source, const AnyvocAudioProcessor::ModulationCopyResult& result, bool editCopy)
    {
        getProperties().set ("MatrixCopyStatus", modulationCopyStatusName (result.status));
        getProperties().set ("MatrixCopyDestination", result.destination);
        const juce::Component::SafePointer<ModulationMatrix> safe (this);
        const auto notify = onStatus;
        if (notify) notify (modulationCopyMessage (source, result));
        if (safe == nullptr || result.status != AnyvocAudioProcessor::ModulationCopyStatus::copied
            || result.destination < 0 || result.destination >= matrixRouteCount || ! matrixHierarchyVisible (safe.getComponent())) return;
        copiedRoute = result.destination;
        copiedRevision = processor.getHistoryRevision();
        copiedSnapshot = matrixRouteSnapshot (processor, copiedRoute);
        if (editCopy) { showDetail (copiedRoute); return; }

        // Page/scroll/focus changes may finish another control's callback. Defer
        // navigation until the originating COPY button has returned, and discard
        // it if either the view or copied route changed in the meantime.
        const auto request = ++detailRequest;
        const auto revision = copiedRevision;
        const auto snapshot = copiedSnapshot;
        const int route = copiedRoute;
        juce::MessageManager::callAsync ([safe, route, request, revision, snapshot]
        {
            if (safe == nullptr || safe->detailRequest != request || ! matrixHierarchyVisible (safe.getComponent())
                || safe->processor.getHistoryRevision() != revision || safe->processor.isUserEditActive()
                || matrixRouteSnapshot (safe->processor, route) != snapshot) return;
            if (! safe->expanded) safe->setPage (route / 8);
            else safe->viewport.setViewPosition (0, juce::jlimit (0, safe->content.getHeight() - safe->viewport.getHeight(),
                                                                route * 30 - safe->viewport.getHeight() / 2));
            if (safe == nullptr) return;
            safe->refresh();
            if (safe == nullptr) return;
            safe->repaint();
            safe->rows[(size_t) route]->focusDestination();
        });
    }
    std::function<void (const juce::String&)> onStatus;
    void resized() override
    {
        const bool editing = detail != nullptr && detail->isVisible();
        const int top = expanded ? 0 : 200;
        for (auto* button : { &previous, &next, &expand, &compact }) button->setVisible (! editing);
        expand.setVisible (! editing && ! expanded); compact.setVisible (! editing && expanded);
        previous.setVisible (! editing && ! expanded); next.setVisible (! editing && ! expanded);
        previous.setEnabled (page > 0); next.setEnabled (page < 3);
        previous.setBounds (778, top + 12, 28, 24); next.setBounds (864, top + 12, 28, 24);
        expand.setBounds (914, top + 12, 142, 24); compact.setBounds (914, top + 12, 142, 24);
        viewport.expanded = expanded; viewport.setScrollBarsShown (expanded, false);
        viewport.setVisible (! editing); viewport.setBounds (18, top + 49, 1040, expanded ? 390 : 208);
        const int height = expanded ? 30 : 26;
        content.setSize (1020, matrixRouteCount * height);
        for (int i = 0; i < matrixRouteCount; ++i) rows[(size_t) i]->setBounds (0, i * height, 1020, height);
        if (! expanded) viewport.setViewPosition (0, page * 8 * height);
        if (editing) detail->setBounds (getLocalBounds());
    }
    void paint (juce::Graphics& g) override
    {
        if (detail != nullptr && detail->isVisible()) return;
        const int top = expanded ? 0 : 200;
        drawCard (g, { 0, top, getWidth(), getHeight() - top }, "", "", mint);
        drawText (g, "#", { 38, top + 13, 24, 22 }, 9, secondaryText, true);
        drawText (g, "ON", { 66, top + 13, 44, 22 }, 9, secondaryText, true, juce::Justification::centred);
        drawText (g, "SOURCE", { 122, top + 13, 190, 22 }, 9, secondaryText, true);
        drawText (g, "DESTINATION", { 334, top + 13, 243, 22 }, 9, secondaryText, true);
        drawText (g, "AMOUNT", { 597, top + 13, 160, 22 }, 9, secondaryText, true);
        drawText (g, expanded ? "32 ROUTES" : juce::String (page + 1) + " / 4", { expanded ? 778 : 807, top + 13, expanded ? 113 : 55, 22 }, 10, primaryText, true, juce::Justification::centred);
        if (expanded) drawText (g, "Drag to move. COPY keeps every setting in the first empty row; EDIT opens shaping and AUX; X clears a row.", { 20, 441, 1030, 16 }, 9.5f);
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (! viewport.isVisible()) return;
        const bool copyCurrent = copiedRoute >= 0 && processor.getHistoryRevision() == copiedRevision
                                 && matrixRouteSnapshot (processor, copiedRoute) == copiedSnapshot;
        const int highlighted = dragTarget >= 0 ? dragTarget : copyCurrent ? copiedRoute : -1;
        if (highlighted < 0) return;
        const auto bounds = getLocalArea (&content, rows[(size_t) highlighted]->getBounds());
        g.reduceClipRegion (viewport.getBounds()); g.setColour ((dragTarget >= 0 ? mint : lilac).withAlpha (0.7f));
        g.drawRoundedRectangle (bounds.toFloat().reduced (1), 4, 2);
    }
private:
    void copyRoute (int route, juce::Component* origin, bool fromDetail)
    {
        if (route < 0 || route >= matrixRouteCount || origin == nullptr) return;
        const juce::Component::SafePointer<ModulationMatrix> safe (this);
        const juce::Component::SafePointer<juce::Component> safeOrigin (origin);
        const auto request = detailRequest;
        const auto viewPosition = viewport.getViewPosition();
        const auto isCurrent = [safe, safeOrigin, request, viewPosition, fromDetail, route]
        {
            if (safe == nullptr || safeOrigin == nullptr || safe->detailRequest != request
                || ! matrixHierarchyVisible (safeOrigin.getComponent()) || ! safeOrigin->isEnabled()) return false;
            if (fromDetail) return safe->detail != nullptr && safe->detail->isParentOf (safeOrigin.getComponent());
            return safe->viewport.isVisible() && safe->viewport.getViewPosition() == viewPosition
                && safe->viewport.getViewArea().intersects (safe->rows[(size_t) route]->getBounds());
        };
        if (! isCurrent()) return;
        auto result = processor.copyModulationRoute (route, isCurrent);
        if (safe == nullptr) return;
        if (result.status == AnyvocAudioProcessor::ModulationCopyStatus::copied && ! isCurrent())
            result = { AnyvocAudioProcessor::ModulationCopyStatus::cancelled, -1 };
        // The processor reports cancellations even if some destination fields
        // were written before a host callback; never describe that as rollback.
        safe->reportCopyResult (route, result, fromDetail);
    }
    void setPage (int index) { cancelPendingNavigation(); page = juce::jlimit (0, 3, index); resized(); repaint(); }
    void setExpanded (bool shouldExpand)
    {
        if (expanded == shouldExpand) return;
        cancelPendingNavigation();
        if (! shouldExpand) page = juce::jlimit (0, 3, viewport.getViewPositionY() / (30 * 8));
        expanded = shouldExpand; resized();
        if (expanded) viewport.setViewPosition (0, page * 8 * 30);
        repaint();
    }
    void startMove (int index)
    {
        dragSource = -1; dragTarget = -1; dragActive = false;
        if (processor.isUserEditActive()) return;
        dragRevision = processor.getHistoryRevision(); dragSource = index;
        for (int i = 0; i < matrixRouteCount; ++i) dragSnapshot[(size_t) i] = matrixRouteSnapshot (processor, i);
    }
    bool moveStillValid() const
    {
        if (dragSource < 0 || processor.isUserEditActive() || processor.getHistoryRevision() != dragRevision) return false;
        for (int i = 0; i < matrixRouteCount; ++i)
            if (dragSnapshot[(size_t) i] != matrixRouteSnapshot (processor, i)) return false;
        return true;
    }
    int rowAt (juce::Point<int> screenPoint) const
    {
        if (! viewport.getLocalBounds().contains (viewport.getLocalPoint (nullptr, screenPoint))) return -1;
        return juce::jlimit (0, matrixRouteCount - 1, content.getLocalPoint (nullptr, screenPoint).y / (expanded ? 30 : 26));
    }
    void dragMove (juce::Point<int> screenPoint)
    {
        if (! moveStillValid()) { dragSource = -1; dragTarget = -1; dragActive = false; repaint(); return; }
        dragPoint = screenPoint; dragActive = true;
        const auto point = viewport.getLocalPoint (nullptr, screenPoint);
        if (expanded && point.x >= 0 && point.x < viewport.getWidth())
        {
            if (point.y < 24) viewport.setViewPosition (0, juce::jmax (0, viewport.getViewPositionY() - 20));
            else if (point.y > viewport.getHeight() - 24) viewport.setViewPosition (0, viewport.getViewPositionY() + 20);
        }
        dragTarget = rowAt (screenPoint); repaint();
    }
    void finishMove (int index, juce::Point<int> screenPoint, bool moved)
    {
        const int target = rowAt (screenPoint);
        const bool valid = moved && index == dragSource && moveStillValid() && target >= 0 && target != index;
        dragSource = -1; dragTarget = -1; dragActive = false;
        const juce::Component::SafePointer<ModulationMatrix> safe (this);
        if (valid) processor.moveModulationRoute (index, target);
        if (safe != nullptr) { refresh(); repaint(); }
    }
    void stepMove (int index, int delta)
    {
        const int target = index + delta;
        if (processor.isUserEditActive() || target < 0 || target >= matrixRouteCount) return;
        const juce::Component::SafePointer<ModulationMatrix> safe (this);
        processor.moveModulationRoute (index, target);
        if (safe == nullptr) return;
        if (! expanded) setPage (target / 8);
        else viewport.setViewPosition (0, juce::jlimit (0, content.getHeight() - viewport.getHeight(), target * 30 - viewport.getHeight() / 2));
        rows[(size_t) target]->handle.grabKeyboardFocus(); refresh(); repaint();
    }
    AnyvocAudioProcessor& processor;
    juce::Component content;
    MatrixViewport viewport;
    std::array<std::unique_ptr<ModulationMatrixRow>, matrixRouteCount> rows;
    std::unique_ptr<ModulationRouteDetail> detail;
    juce::TextButton previous, next, expand, compact;
    std::array<MatrixRouteSnapshot, matrixRouteCount> dragSnapshot;
    uint64_t dragRevision = 0, detailRequest = 0, copiedRevision = 0;
    MatrixRouteSnapshot copiedSnapshot{};
    int page = 0, dragSource = -1, dragTarget = -1, copiedRoute = -1;
    juce::Point<int> dragPoint;
    bool expanded = false, dragActive = false;
};

class FilterResponse final : public juce::Component, public juce::SettableTooltipClient
{
public:
    FilterResponse (AnyvocAudioProcessor& p, int filterIndex, juce::Colour colour)
        : processor (p), index (filterIndex), accent (colour),
          cutoffId (index == 0 ? "cutoff" : "filter2Cutoff"), resonanceId (index == 0 ? "resonance" : "filter2Resonance"),
          enabledId (index == 0 ? "filterEnabled" : "filter2Enabled"), typeId (index == 0 ? "filterType" : "filter2Type"),
          slopeId (index == 0 ? "filterSlope" : "filter2Slope")
    {
        setTitle (index == 0 ? "Filter A response" : "Filter B response");
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        setTooltip ("Nominal response at the base settings, not an audio analyzer. Drag left/right for cutoff and up/down for resonance. Shift-drag for precision. Envelopes, modulation and key tracking move each voice independently.");
    }
    ~FilterResponse() override { endGesture(); }
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu()) return;
        endGesture(); grabKeyboardFocus();
        processor.beginUserEdit (index == 0 ? "Filter A response" : "Filter B response");
        dragging = true;
        initialCutoff = value (processor, cutoffId); initialResonance = value (processor, resonanceId);
        startPosition = event.position;
        for (const auto& id : { cutoffId, resonanceId })
            if (auto* parameter = processor.apvts.getParameter (id)) parameter->beginChangeGesture();
        if (! event.mods.isShiftDown()) updateValue (cutoffId, frequencyAt (event.position.x));
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! dragging) return;
        const float fine = event.mods.isShiftDown() ? 0.15f : 1.0f;
        const float cutoff = event.mods.isShiftDown()
            ? initialCutoff * std::pow (1000.0f, (event.position.x - startPosition.x) / plotBounds().getWidth() * fine)
            : frequencyAt (event.position.x);
        updateValue (cutoffId, juce::jlimit (20.0f, 20000.0f, cutoff));
        updateValue (resonanceId, juce::jlimit (0.0f, 0.95f, initialResonance + (startPosition.y - event.position.y) / plotBounds().getHeight() * 0.95f * fine));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { endGesture(); }
    void focusLost (FocusChangeType) override { endGesture(); }
    void visibilityChanged() override { if (! isShowing()) endGesture(); }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey) { endGesture(); return true; }
        return false;
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        endGesture();
        ScopedSoundEdit edit (processor, index == 0 ? "Reset Filter A response" : "Reset Filter B response");
        for (const auto& id : { cutoffId, resonanceId })
            if (auto* parameter = processor.apvts.getParameter (id))
            {
                parameter->beginChangeGesture(); processor.setUserParameter (id, parameter->convertFrom0to1 (parameter->getDefaultValue())); parameter->endChangeGesture();
            }
        repaint();
    }
    void mouseMove (const juce::MouseEvent& event) override { hoverX = event.position.x; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hoverX = -1; repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto r = plotBounds();
        const bool enabled = value (processor, enabledId) > 0.5f;
        const auto lineColour = enabled ? accent : secondaryText;
        g.setColour (insetColour); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8);
        g.setColour (borderColour); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8, 1);
        for (float frequency : { 20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f })
        {
            const float x = xAt (frequency);
            g.setColour (borderColour.withAlpha (0.65f)); g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
            drawText (g, frequency >= 1000 ? juce::String (juce::roundToInt (frequency / 1000)) + "k" : juce::String (juce::roundToInt (frequency)),
                      { juce::roundToInt (x) - 16, getHeight() - 17, 32, 14 }, 8, secondaryText, false, juce::Justification::centred);
        }
        for (float db : { 12.0f, 0.0f, -24.0f, -48.0f })
        {
            const float y = yAt (db);
            g.setColour (borderColour.withAlpha (db == 0 ? 1.0f : 0.55f)); g.drawHorizontalLine (juce::roundToInt (y), r.getX(), r.getRight());
            drawText (g, juce::String (juce::roundToInt (db)), { 3, juce::roundToInt (y) - 7, 24, 14 }, 8, secondaryText, false, juce::Justification::centredRight);
        }
        juce::Path path;
        for (int point = 0; point <= 256; ++point)
        {
            const float x = r.getX() + r.getWidth() * (float) point / 256;
            const float y = yAt (enabled ? responseDb (frequencyAt (x)) : 0);
            if (point == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        auto fill = path; fill.lineTo (r.getRight(), r.getBottom()); fill.lineTo (r.getX(), r.getBottom()); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (lineColour.withAlpha (0.18f), 0, r.getY(), lineColour.withAlpha (0.01f), 0, r.getBottom(), false));
        g.fillPath (fill);
        g.setColour (lineColour); g.strokePath (path, juce::PathStrokeType (1.8f));
        const float cutoff = value (processor, cutoffId), x = xAt (cutoff);
        g.setColour (lineColour.withAlpha (0.4f)); g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
        const float y = yAt (enabled ? responseDb (cutoff) : 0);
        g.setColour (insetColour); g.fillEllipse (x - 5, y - 5, 10, 10);
        g.setColour (lineColour); g.drawEllipse (x - 5, y - 5, 10, 10, 2);
        drawText (g, enabled ? "NOMINAL / BASE SETTINGS" : "BYPASSED / AUDIO PASSES THROUGH", { 36, 4, 240, 14 }, 8.5f, lineColour, true);
        const auto readout = hoverX >= 0 && ! dragging ? formatted (frequencyAt (hoverX), Unit::hertz)
                                                    : formatted (cutoff, Unit::hertz) + "  /  RES " + formatted (value (processor, resonanceId), Unit::percent);
        drawText (g, readout, { getWidth() - 190, 4, 177, 14 }, 9, primaryText, false, juce::Justification::centredRight);
    }
private:
    void updateValue (const juce::String& id, float newValue)
    {
        processor.setUserParameter (id, newValue);
    }
    void endGesture()
    {
        if (! dragging) return;
        dragging = false;
        for (const auto& id : { cutoffId, resonanceId })
            if (auto* parameter = processor.apvts.getParameter (id)) parameter->endChangeGesture();
        processor.endUserEdit();
    }
    juce::Rectangle<float> plotBounds() const { return getLocalBounds().toFloat().withTrimmedLeft (33).withTrimmedRight (14).withTrimmedTop (25).withTrimmedBottom (22); }
    float xAt (float frequency) const { return plotBounds().getX() + std::log (juce::jlimit (20.0f, 20000.0f, frequency) / 20.0f) / std::log (1000.0f) * plotBounds().getWidth(); }
    float frequencyAt (float x) const { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - plotBounds().getX()) / plotBounds().getWidth())); }
    float yAt (float db) const { return plotBounds().getY() + (18.0f - juce::jlimit (-48.0f, 18.0f, db)) / 66.0f * plotBounds().getHeight(); }
    float responseDb (float frequency) const
    {
        const double sampleRate = processor.getSampleRate() > 0 ? processor.getSampleRate() : 48000.0;
        const double cutoff = juce::jlimit (20.0, sampleRate * 0.45, (double) value (processor, cutoffId));
        const double ratio = std::tan (juce::MathConstants<double>::pi * juce::jmin ((double) frequency, sampleRate * 0.49) / sampleRate)
                           / std::tan (juce::MathConstants<double>::pi * cutoff / sampleRate);
        const double damping = 2.0 - 1.94 * value (processor, resonanceId);
        const double denominator = std::sqrt ((1.0 - ratio * ratio) * (1.0 - ratio * ratio) + damping * damping * ratio * ratio);
        const int type = (int) value (processor, typeId);
        const double numerator = type == 1 ? ratio * ratio : type == 2 ? ratio : 1.0;
        const double magnitude = numerator / juce::jmax (1.0e-12, denominator);
        return (float) (20.0 * std::log10 (juce::jmax (1.0e-6, magnitude))) * (value (processor, slopeId) > 0.5f ? 2.0f : 1.0f);
    }
    AnyvocAudioProcessor& processor;
    int index;
    juce::Colour accent;
    juce::String cutoffId, resonanceId, enabledId, typeId, slopeId;
    bool dragging = false;
    float initialCutoff = 1000, initialResonance = 0, hoverX = -1;
    juce::Point<float> startPosition;
};

class FilterRoutingDiagram final : public juce::Component
{
public:
    explicit FilterRoutingDiagram (AnyvocAudioProcessor& p) : processor (p) { setTitle ("Filter signal flow for Both sends"); }
    void paint (juce::Graphics& g) override
    {
        const int mode = (int) value (processor, "filterRouting");
        const bool a = value (processor, "filterEnabled") > 0.5f, b = value (processor, "filter2Enabled") > 0.5f;
        drawText (g, "BOTH", { 0, 15, 45, 20 }, 9, secondaryText, true);
        drawText (g, "OUT", { 486, 15, 45, 20 }, 9, secondaryText, true);
        auto arrow = [&g] (float x1, float y1, float x2, float y2)
        {
            g.setColour (secondaryText.withAlpha (0.7f)); g.drawArrow ({ x1, y1, x2, y2 }, 1.1f, 5, 5);
        };
        auto box = [&g] (int x, int y, const juce::String& name, bool enabled, juce::Colour colour)
        {
            g.setColour ((enabled ? colour : secondaryText).withAlpha (0.1f)); g.fillRoundedRectangle ((float) x, (float) y, 116, 23, 5);
            g.setColour ((enabled ? colour : secondaryText).withAlpha (0.5f)); g.drawRoundedRectangle ((float) x, (float) y, 116, 23, 5, 1);
            drawText (g, name + (enabled ? "" : " / BYPASS"), { x, y, 116, 23 }, 9, enabled ? colour : secondaryText, true, juce::Justification::centred);
        };
        if (mode == 2)
        {
            box (157, 1, "A", a, mint); box (157, 29, "B", b, lilac);
            arrow (52, 25, 109, 25); arrow (109, 25, 150, 12); arrow (109, 25, 150, 40);
            arrow (280, 12, 382, 25); arrow (280, 40, 382, 25); arrow (382, 25, 477, 25);
            const float balance = value (processor, "filterBalance");
            drawText (g, formatted (1.0f - balance, Unit::percent), { 282, 0, 53, 18 }, 8, mint);
            drawText (g, formatted (balance, Unit::percent), { 282, 34, 53, 18 }, 8, lilac);
        }
        else
        {
            const bool aFirst = mode == 0;
            box (112, 14, aFirst ? "A" : "B", aFirst ? a : b, aFirst ? mint : lilac);
            box (314, 14, aFirst ? "B" : "A", aFirst ? b : a, aFirst ? lilac : mint);
            arrow (52, 25, 104, 25); arrow (237, 25, 306, 25); arrow (439, 25, 477, 25);
        }
    }
private:
    AnyvocAudioProcessor& processor;
};

class WaveDisplay final : public juce::Component, public juce::FileDragAndDropTarget,
                          public juce::SettableTooltipClient
{
public:
    WaveDisplay (AnyvocAudioProcessor& p, int osc, juce::Colour colour)
        : processor (p), oscillator (osc), accent (colour)
    {
        setTooltip ("Drop a WAV here to import it into the selected oscillator mode. Wavetable uses cycles; Sample and Granular use audio up to 20 seconds.");
    }
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() == 1 && juce::File (files[0]).hasFileExtension ("wav");
    }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        hovered = false;
        if (files.size() == 1 && onImport) onImport (juce::File (files[0]));
        repaint();
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { hovered = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hovered = false; repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (insetColour);
        g.fillRoundedRectangle (r, 6);
        g.setColour (borderColour.withAlpha (0.5f));
        for (int i = 1; i < 8; ++i)
            g.drawVerticalLine (juce::roundToInt (r.getWidth() * (float) i / 8), 8, r.getHeight() - 8);
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine (juce::roundToInt (r.getHeight() * (float) i / 4), 8, r.getWidth() - 8);
        const auto prefix = "osc" + juce::String (oscillator + 1);
        const auto position = value (processor, prefix + "Position");
        const auto mode = (int) value (processor, prefix + "Mode");
        const auto plot = r.reduced (8, 9);
        juce::Path wave;
        for (int i = 0; i <= 192; ++i)
        {
            const auto phase = (float) i / 192.0f;
            const auto sample = mode == 0 ? processor.getWarpedWavetableSample (oscillator, phase, position)
                                          : processor.getSamplePreview (oscillator, phase);
            const auto x = plot.getX() + phase * plot.getWidth();
            const auto y = plot.getCentreY() - sample * plot.getHeight() * 0.43f;
            if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
        }
        g.setColour (accent.withAlpha (0.09f));
        g.strokePath (wave, juce::PathStrokeType (6));
        g.setColour (accent);
        g.strokePath (wave, juce::PathStrokeType (1.6f));
        if (mode == 0 && (processor.getWarpMode (oscillator, 0) >= 7 || processor.getWarpMode (oscillator, 1) >= 7))
        {
            g.setColour (insetColour.withAlpha (0.9f)); g.fillRect (5, 1, getWidth() - 10, 12);
            drawText (g, "BASE WAVE / LIVE CROSS MOD NOT SHOWN", { 7, 1, getWidth() - 14, 12 }, 7.5f,
                      secondaryText, true, juce::Justification::centredRight);
        }
        if (mode != 0)
        {
            const bool reverse = mode == 1 && value (processor, prefix + "Reverse") > 0.5f;
            const auto x = plot.getX() + (reverse ? 1.0f - position : position) * plot.getWidth();
            g.setColour (orange.withAlpha (0.8f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            juce::Path marker;
            marker.addTriangle (x - 4, plot.getY() - 4, x + 4, plot.getY() - 4, x, plot.getY() + 2);
            g.fillPath (marker);
        }
        if (hovered)
        {
            g.setColour (accent.withAlpha (0.12f)); g.fillRoundedRectangle (r, 6);
            g.setColour (accent); g.drawRoundedRectangle (r.reduced (1), 6, 1.5f);
            drawText (g, "DROP WAV TO IMPORT", getLocalBounds(), 12, primaryText, true, juce::Justification::centred);
        }
    }
    std::function<void (const juce::File&)> onImport;
private:
    AnyvocAudioProcessor& processor;
    int oscillator;
    juce::Colour accent;
    bool hovered = false;
};

const juce::StringArray envelopeNames { "Amp Envelope", "Filter Envelope", "Envelope 3", "Envelope 4" };
const juce::StringArray envelopePrefixes { "amp", "env", "env3", "env4" };
const juce::StringArray envelopeStages { "Delay", "Attack", "Hold", "Decay", "Sustain", "Release" };
const juce::StringArray envelopeDivisions { "Instant", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "1/8 T", "1/16 T", "1/8 D", "1/16 D" };
const std::array<const char*, 16> envelopeFields { "Delay", "Attack", "Hold", "Decay", "Sustain", "Release",
    "AttackCurve", "DecayCurve", "ReleaseCurve", "TempoSync", "DelayDivision", "AttackDivision", "HoldDivision", "DecayDivision", "ReleaseDivision", "RetriggerLegato" };

anyvoc::EnvelopeSettings envelopeSettingsForUi (AnyvocAudioProcessor& processor, const juce::String& prefix)
{
    anyvoc::EnvelopeSettings settings;
    settings.attack = value (processor, prefix + "Attack"); settings.decay = value (processor, prefix + "Decay");
    settings.sustain = value (processor, prefix + "Sustain"); settings.release = value (processor, prefix + "Release");
    settings.delay = value (processor, prefix + "Delay"); settings.hold = value (processor, prefix + "Hold");
    settings.attackCurve = value (processor, prefix + "AttackCurve"); settings.decayCurve = value (processor, prefix + "DecayCurve");
    settings.releaseCurve = value (processor, prefix + "ReleaseCurve"); settings.tempoSync = value (processor, prefix + "TempoSync") > 0.5f;
    for (int i = 0; i < 5; ++i) settings.divisions[(size_t) i] = (int) value (processor, prefix + envelopeFields[(size_t) i + 10]);
    settings.retriggerLegato = value (processor, prefix + "RetriggerLegato") > 0.5f;
    return settings;
}

int envelopeDurationIndex (int stage) { return stage == 5 ? 4 : stage; }
juce::String envelopeDurationText (double seconds)
{
    return seconds >= 1.0 ? juce::String (seconds, seconds < 10 ? 3 : 2) + " s"
                         : juce::String (seconds * 1000.0, seconds < .1 ? 2 : 1) + " ms";
}

struct EnvelopePlot
{
    juce::Rectangle<float> bounds;
    std::array<juce::Point<float>, 6> nodes{};
    std::array<double, 5> durations{};
    float secondsPerPixel = 1;
    juce::Path path;
    EnvelopePlot (const anyvoc::EnvelopeSettings& settings, double tempo, juce::Rectangle<float> rectangle, float frozenScale = 0)
        : bounds (rectangle)
    {
        double total = 0;
        for (int i = 0; i < 5; ++i) { durations[(size_t) i] = anyvoc::envelopeStageSeconds (settings, i, tempo); total += durations[(size_t) i]; }
        secondsPerPixel = frozenScale > 0 ? frozenScale : (float) juce::jmax (.05, total) / (bounds.getWidth() * .82f);
        const auto y = [&] (float level) { return bounds.getBottom() - level * bounds.getHeight(); };
        float x = bounds.getX();
        path.startNewSubPath (x, y (0));
        const auto segment = [&] (int stage, double duration, float start, float end, float curve)
        {
            const float width = (float) duration / secondsPerPixel;
            for (int i = 1; i <= 48; ++i)
            {
                const float t = (float) i / 48.0f;
                path.lineTo (x + width * t, y (anyvoc::envelopeSegmentValue (start, end, t, curve)));
            }
            x += width; nodes[(size_t) stage] = { x, y (end) };
        };
        segment (0, durations[0], 0, 0, 0);
        segment (1, durations[1], 0, 1, settings.attackCurve);
        segment (2, durations[2], 1, 1, 0);
        segment (3, durations[3], 1, settings.sustain, settings.decayCurve);
        x += bounds.getWidth() * .18f; nodes[4] = { x, y (settings.sustain) }; path.lineTo (nodes[4]);
        segment (5, durations[4], settings.sustain, 0, settings.releaseCurve);
    }
    juce::Point<float> curvePoint (int stage, const anyvoc::EnvelopeSettings& settings) const
    {
        const auto start = stage == 1 ? nodes[0] : stage == 3 ? nodes[2] : nodes[4];
        const auto end = nodes[(size_t) stage];
        const float curve = stage == 1 ? settings.attackCurve : stage == 3 ? settings.decayCurve : settings.releaseCurve;
        return { (start.x + end.x) * .5f, start.y + (end.y - start.y) * anyvoc::envelopeCurveProgress (.5f, curve) };
    }
};

class EnvelopeDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    EnvelopeDisplay (AnyvocAudioProcessor& p, juce::String prefixIn, juce::Colour colour)
        : processor (p), prefix (std::move (prefixIn)), accent (colour)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Open Envelope Lab to drag stages, shape curves, synchronize to BPM and control retriggering in Legato mode.");
    }
    void mouseUp (const juce::MouseEvent& e) override { const auto open = onOpen; if (! e.mouseWasDraggedSinceMouseDown() && open) open(); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (5, 4);
        const EnvelopePlot plot (envelopeSettingsForUi (processor, prefix), processor.getTempo(), r);
        auto fill = plot.path; fill.lineTo (plot.nodes[5].x, r.getBottom()); fill.lineTo (r.getX(), r.getBottom()); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (.18f), 0, r.getY(), accent.withAlpha (.01f), 0, r.getBottom(), false));
        g.fillPath (fill); g.setColour (accent); g.strokePath (plot.path, juce::PathStrokeType (1.7f));
        g.setColour (borderColour); g.drawHorizontalLine ((int) r.getBottom(), r.getX(), r.getRight());
    }
    std::function<void()> onOpen;
private:
    AnyvocAudioProcessor& processor;
    juce::String prefix;
    juce::Colour accent;
};

class EnvelopeGraph final : public juce::Component, public juce::SettableTooltipClient
{
    class Handle final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        bool curve = false, selected = false;
        juce::Colour accent = mint;
        std::function<void (const juce::MouseEvent&)> onDown, onDrag, onDouble;
        std::function<void()> onEnd;
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (selected ? 1.5f : 3.0f);
            g.setColour (selected ? primaryText : accent);
            if (curve)
            {
                juce::Path diamond; diamond.addQuadrilateral (r.getCentreX(), r.getY(), r.getRight(), r.getCentreY(), r.getCentreX(), r.getBottom(), r.getX(), r.getCentreY());
                g.fillPath (diamond);
            }
            else { g.fillEllipse (r); g.setColour (insetColour); g.fillEllipse (r.reduced (2.5f)); }
        }
        void mouseDown (const juce::MouseEvent& e) override { const auto fn = onDown; if (e.mods.isLeftButtonDown() && fn) fn (e); }
        void mouseDrag (const juce::MouseEvent& e) override { const auto fn = onDrag; if (fn) fn (e); }
        void mouseUp (const juce::MouseEvent&) override { const auto fn = onEnd; if (fn) fn(); }
        void mouseDoubleClick (const juce::MouseEvent& e) override { const auto fn = onDouble; if (e.mods.isLeftButtonDown() && fn) fn (e); }
        void focusLost (FocusChangeType) override { const auto fn = onEnd; if (fn) fn(); }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key != juce::KeyPress::escapeKey) return false;
            const auto fn = onEnd; if (fn) fn(); return true;
        }
    };
    struct Edit
    {
        AnyvocAudioProcessor& processor;
        std::weak_ptr<int> lifetime;
        std::array<juce::RangedAudioParameter*, 16> parameters{};
        std::array<float, 16> expected{};
        std::array<bool, 16> gesturing{};
        uint64_t revision;
        bool active = true;
        Edit (AnyvocAudioProcessor& p, const juce::String& prefix, std::weak_ptr<int> alive)
            : processor (p), lifetime (std::move (alive)), revision (p.getHistoryRevision())
        {
            for (size_t i = 0; i < parameters.size(); ++i)
            { parameters[i] = p.apvts.getParameter (prefix + envelopeFields[i]); expected[i] = parameters[i]->getValue(); }
        }
        bool valid() const
        {
            if (! active || lifetime.expired() || processor.getHistoryRevision() != revision) return false;
            for (size_t i = 0; i < parameters.size(); ++i)
                if (parameters[i]->getValue() != expected[i]) return false;
            return true;
        }
        bool begin (int slot)
        {
            if (! valid()) { finish(); return false; }
            if (! gesturing[(size_t) slot])
            { gesturing[(size_t) slot] = true; parameters[(size_t) slot]->beginChangeGesture(); }
            if (! valid()) { finish(); return false; }
            return true;
        }
        bool write (int slot, float plain)
        {
            if (! begin (slot)) return false;
            auto* parameter = parameters[(size_t) slot];
            // Float parameters store the denormalized value. Predict their exact
            // round trip so even a one-ULP host edit cancels this gesture.
            expected[(size_t) slot] = parameter->convertTo0to1 (parameter->convertFrom0to1 (parameter->convertTo0to1 (plain)));
            processor.setUserParameter (parameter->paramID, plain);
            if (! valid()) { finish(); return false; }
            return true;
        }
        float plain (int slot) const { return parameters[(size_t) slot]->convertFrom0to1 (expected[(size_t) slot]); }
        void finish()
        {
            if (! active) return;
            active = false;
            const auto pending = gesturing; gesturing.fill (false);
            for (size_t i = 0; i < parameters.size(); ++i) if (pending[i]) parameters[i]->endChangeGesture();
            processor.endUserEdit();
        }
    };
public:
    EnvelopeGraph (AnyvocAudioProcessor& p, juce::String prefixIn, juce::Colour colour)
        : processor (p), prefix (std::move (prefixIn)), accent (colour)
    {
        setComponentID (prefix + "EnvelopeGraph");
        setTooltip ("Drag circles to change stage time or sustain level. Diamonds change attack, decay and release curvature. Select a stage name when zero-length handles overlap.");
        for (int i = 0; i < 9; ++i)
        {
            auto& handle = handles[(size_t) i]; addAndMakeVisible (handle);
            const bool curve = i >= 6; const int stage = curve ? (i - 6) * 2 + 1 : i;
            handle.curve = curve; handle.accent = accent; handle.setWantsKeyboardFocus (true);
            handle.setComponentID (prefix + (curve ? "EnvelopeCurve" : "EnvelopeNode") + envelopeStages[stage]);
            handle.setTitle (envelopeStages[stage] + (curve ? " curve" : " envelope node"));
            handle.setTooltip (curve ? "Drag vertically to bend this stage. Double-click for a linear curve. Shift is fine adjustment."
                                    : stage == 4 ? "Drag vertically to change the held sustain level. Double-click to reset."
                                    : stage == 3 ? "Drag horizontally for decay time and vertically for sustain. Shift is fine adjustment."
                                                 : "Drag horizontally to change this stage's duration. Double-click to reset. Shift is fine adjustment.");
            handle.setMouseCursor (curve || stage == 4 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::DraggingHandCursor);
            handle.onDown = [this, i] (const juce::MouseEvent& e) { beginGesture (i, e); };
            handle.onDrag = [this] (const juce::MouseEvent& e) { dragGesture (e); };
            handle.onEnd = [this]
            {
                const juce::Component::SafePointer<EnvelopeGraph> safe (this);
                endGesture(); if (safe != nullptr) refresh();
            };
            handle.onDouble = [this, i] (const juce::MouseEvent& e) { resetGesture (i, e); };
        }
    }
    ~EnvelopeGraph() override { lifetime.reset(); endGesture(); }
    void endGesture()
    {
        auto edit = std::move (gesture);
        if (edit != nullptr) edit->finish();
    }
    void selectStage (int stage)
    {
        const juce::Component::SafePointer<EnvelopeGraph> safe (this);
        endGesture();
        if (safe == nullptr) return;
        selected = juce::jlimit (0, 5, stage); handles[(size_t) selected].toFront (false); refresh();
    }
    bool refresh()
    {
        const juce::Component::SafePointer<EnvelopeGraph> safe (this);
        if (gesture != nullptr && ! gesture->valid()) endGesture();
        if (safe == nullptr) return false;
        const auto settings = envelopeSettingsForUi (processor, prefix);
        const EnvelopePlot plot (settings, processor.getTempo(), plotBounds(), gesture != nullptr ? secondsPerPixel : 0);
        for (int i = 0; i < 9; ++i)
        {
            auto point = i < 6 ? plot.nodes[(size_t) i] : plot.curvePoint ((i - 6) * 2 + 1, settings);
            point.x = juce::jlimit (plot.bounds.getX(), plot.bounds.getRight(), point.x);
            handles[(size_t) i].setBounds (juce::Rectangle<int> (16, 16).withCentre (point.roundToInt()));
            handles[(size_t) i].selected = i == selected;
            handles[(size_t) i].repaint();
        }
        repaint(); return true;
    }
    void resized() override { refresh(); }
    void visibilityChanged() override { if (! matrixHierarchyVisible (this)) endGesture(); }
    juce::String feedback() const
    {
        const int stage = selected >= 6 ? (selected - 6) * 2 + 1 : selected;
        const auto settings = envelopeSettingsForUi (processor, prefix);
        if (selected >= 6)
            return envelopeStages[stage].toUpperCase() + " CURVE  " + formatted (value (processor, prefix + envelopeStages[stage] + "Curve"), Unit::signedPercent);
        if (stage == 4) return "SUSTAIN  " + formatted (settings.sustain, Unit::percent) + "  /  held until the key is released";
        const int durationIndex = envelopeDurationIndex (stage);
        juce::String result = envelopeStages[stage].toUpperCase() + "  ";
        if (settings.tempoSync) result += envelopeDivisions[juce::jlimit (0, 13, settings.divisions[(size_t) durationIndex])] + "  /  ";
        result += envelopeDurationText (anyvoc::envelopeStageSeconds (settings, durationIndex, processor.getTempo()));
        if (stage == 3) result += "   SUSTAIN  " + formatted (settings.sustain, Unit::percent);
        return result;
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (insetColour); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8);
        g.setColour (borderColour); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (.5f), 8, 1);
        const auto settings = envelopeSettingsForUi (processor, prefix);
        const EnvelopePlot plot (settings, processor.getTempo(), plotBounds(), gesture != nullptr ? secondsPerPixel : 0);
        const auto r = plot.bounds;
        for (int i = 0; i <= 4; ++i)
        {
            g.setColour (borderColour.withAlpha (i == 0 || i == 4 ? .8f : .45f));
            g.drawHorizontalLine (juce::roundToInt (r.getY() + r.getHeight() * (float) i / 4), r.getX(), r.getRight());
        }
        const float heldStart = juce::jlimit (r.getX(), r.getRight(), plot.nodes[3].x);
        const float heldEnd = juce::jlimit (r.getX(), r.getRight(), plot.nodes[4].x);
        g.setColour (accent.withAlpha (.045f)); g.fillRect (heldStart, r.getY(), heldEnd - heldStart, r.getHeight());
        g.setColour (accent.withAlpha (.32f));
        const float dash[] { 3, 3 }; g.drawDashedLine ({ heldEnd, r.getY(), heldEnd, r.getBottom() }, dash, 2);
        drawText (g, "KEY HELD", { (int) heldStart, 3, juce::jmax (0, (int) (heldEnd - heldStart)), 18 }, 8.5f, secondaryText, true, juce::Justification::centred);
        drawText (g, "NOTE OFF", { juce::jlimit (0, getWidth() - 70, (int) heldEnd - 35), getHeight() - 20, 70, 16 }, 8, secondaryText, false, juce::Justification::centred);
        drawText (g, "1", { 5, (int) r.getY() - 7, 15, 14 }, 8);
        drawText (g, "0", { 5, (int) r.getBottom() - 7, 15, 14 }, 8);
        g.saveState(); g.reduceClipRegion (r.expanded (1).toNearestInt());
        auto fill = plot.path; fill.lineTo (plot.nodes[5].x, r.getBottom()); fill.lineTo (r.getX(), r.getBottom()); fill.closeSubPath();
        g.setColour (accent.withAlpha (.07f)); g.fillPath (fill);
        g.setColour (accent.withAlpha (.15f)); g.strokePath (plot.path, juce::PathStrokeType (5));
        g.setColour (accent); g.strokePath (plot.path, juce::PathStrokeType (1.8f)); g.restoreState();
    }
private:
    juce::Rectangle<float> plotBounds() const { return getLocalBounds().toFloat().withTrimmedLeft (26).withTrimmedRight (18).reduced (0, 26); }
    juce::Point<float> eventPoint (const juce::MouseEvent& e) const { return getLocalPoint (e.eventComponent, e.position); }
    std::vector<int> slotsFor (int handle) const
    {
        if (handle >= 6) return { handle };
        if (handle == 4) return { 4 };
        const int slot = value (processor, prefix + "TempoSync") > .5f ? 10 + envelopeDurationIndex (handle) : handle;
        return handle == 3 ? std::vector<int> { slot, 4 } : std::vector<int> { slot };
    }
    void beginGesture (int handle, const juce::MouseEvent& e)
    {
        const juce::Component::SafePointer<EnvelopeGraph> safe (this);
        endGesture();
        if (safe == nullptr || processor.isUserEditActive()) return;
        selected = handle; initialPosition = eventPoint (e); slots = slotsFor (handle);
        const auto settings = envelopeSettingsForUi (processor, prefix);
        secondsPerPixel = EnvelopePlot (settings, processor.getTempo(), plotBounds()).secondsPerPixel;
        auto edit = std::make_shared<Edit> (processor, prefix, lifetime); gesture = edit;
        for (size_t i = 0; i < initialValues.size(); ++i) initialValues[i] = edit->plain ((int) i);
        processor.beginUserEdit (prefix.toUpperCase() + " envelope " + (handle >= 6 ? "curve" : "node"));
        for (const int slot : slots) if (! edit->begin (slot)) return;
        handles[(size_t) handle].toFront (false);
        refresh();
    }
    void dragGesture (const juce::MouseEvent& e)
    {
        auto edit = gesture;
        if (edit == nullptr) return;
        if (! edit->valid()) { endGesture(); return; }
        const auto delta = eventPoint (e) - initialPosition;
        const float fine = e.mods.isShiftDown() ? .1f : 1.0f;
        const int target = selected;
        if (target >= 6)
        {
            const float direction = target == 6 ? 1.0f : -1.0f;
            if (! edit->write (target, juce::jlimit (-1.0f, 1.0f, initialValues[(size_t) target] + delta.y * direction * .01f * fine))) return;
        }
        else
        {
            if (target != 4)
            {
                const int slot = slots.front();
                float next = initialValues[(size_t) slot];
                if (slot >= 10)
                {
                    const std::array<int, 14> order { 0, 1, 2, 11, 3, 10, 13, 4, 12, 5, 6, 7, 8, 9 };
                    int initial = 0; for (int i = 0; i < 14; ++i) if (order[(size_t) i] == (int) next) initial = i;
                    next = (float) order[(size_t) juce::jlimit (0, 13, initial + juce::roundToInt (delta.x / 18.0f * fine))];
                }
                else next = juce::jlimit (target == 0 || target == 2 ? 0.0f : .001f, 8.0f, next + delta.x * secondsPerPixel * fine);
                if (! edit->write (slot, next)) return;
            }
            if (target == 3 || target == 4)
                if (! edit->write (4, juce::jlimit (0.0f, 1.0f, initialValues[4] - delta.y / plotBounds().getHeight() * fine))) return;
        }
        refresh();
    }
    void resetGesture (int handle, const juce::MouseEvent& e)
    {
        const juce::Component::SafePointer<EnvelopeGraph> safe (this);
        beginGesture (handle, e);
        if (safe == nullptr) return;
        auto edit = gesture;
        if (edit == nullptr) return;
        const auto targets = slots;
        for (const int slot : targets)
        {
            const auto* parameter = edit->parameters[(size_t) slot];
            if (! edit->write (slot, parameter->convertFrom0to1 (parameter->getDefaultValue()))) return;
        }
        endGesture();
        if (safe != nullptr) refresh();
    }
    AnyvocAudioProcessor& processor;
    juce::String prefix;
    juce::Colour accent;
    std::array<Handle, 9> handles;
    std::shared_ptr<int> lifetime = std::make_shared<int> (0);
    std::shared_ptr<Edit> gesture;
    std::array<float, 16> initialValues{};
    std::vector<int> slots;
    juce::Point<float> initialPosition;
    float secondsPerPixel = 1;
    int selected = 1;
};

class EnvelopeLabPage final : public juce::Component
{
public:
    EnvelopeLabPage (AnyvocAudioProcessor& p, int index)
        : processor (p), prefix (envelopePrefixes[index]), accent (index == 0 ? lilac : index == 1 ? mint : blue), graph (p, prefix, accent)
    {
        setComponentID (prefix + "EnvelopeLabPage"); setInterceptsMouseClicks (false, true);
        addAndMakeVisible (graph); graph.setBounds (52, 169, 1016, 175);
        for (int i = 0; i < 6; ++i)
        {
            const int x = 52 + i * 170;
            auto& stage = stageButtons[(size_t) i]; addAndMakeVisible (stage); stage.setButtonText (envelopeStages[i].toUpperCase());
            stage.setBounds (x + 8, 356, 142, 22); stage.setComponentID (prefix + "EnvelopeSelect" + envelopeStages[i]);
            stage.setTooltip ("Select this stage's graph handle, including when zero-length stages overlap.");
            stage.onClick = [this, i] { graph.selectStage (i); };
            auto& slider = times[(size_t) i]; addAndMakeVisible (slider);
            slider.setSliderStyle (juce::Slider::LinearHorizontal); slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 24);
            styleSliderText (slider); slider.setScrollWheelEnabled (false); slider.setBounds (x + 8, 386, 142, 24);
            slider.setTitle (envelopeStages[i] + (i == 4 ? " level" : " time"));
            slider.setTooltip (i == 4 ? "Level held until the key is released." : "Stage duration in seconds. BPM mode uses the beat division instead.");
            timeAttachments[(size_t) i] = std::make_unique<SoundSliderAttachment> (processor, prefix + envelopeStages[i], slider);
            slider.setComponentID (prefix + envelopeStages[i] + "Lab");
            slider.textFromValueFunction = [i] (double n) { return i == 4 ? formatted (n, Unit::percent) : envelopeDurationText (n); };
            slider.valueFromTextFunction = [i] (const juce::String& text) { return text.getDoubleValue() / (i == 4 ? 100 : text.containsIgnoreCase ("ms") ? 1000 : 1); };
            if (i != 4) slider.userEditAllowed = [owner = &processor, id = prefix + "TempoSync"] { return value (*owner, id) < .5f; };
            slider.updateText();
            if (i != 4)
            {
                auto& choice = divisions[(size_t) envelopeDurationIndex (i)]; addAndMakeVisible (choice);
                choice.addItemList (envelopeDivisions, 1); choice.setBounds (x + 8, 386, 142, 24); choice.setTitle (envelopeStages[i] + " beat division");
                choice.setTooltip ("Duration in beats of the host or local tempo. T is triplet; D is dotted. Instant skips the stage.");
                divisionAttachments[(size_t) envelopeDurationIndex (i)] = std::make_unique<SoundChoiceAttachment> (processor, prefix + envelopeStages[i] + "Division", choice);
                choice.setComponentID (prefix + envelopeStages[i] + "DivisionLab");
            }
        }
        for (int i = 0; i < 3; ++i)
        {
            const int stage = i * 2 + 1, x = 52 + stage * 170;
            auto& slider = curves[(size_t) i]; addAndMakeVisible (slider); slider.setBounds (x + 8, 456, 142, 24);
            slider.setSliderStyle (juce::Slider::LinearHorizontal); slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 47, 24);
            styleSliderText (slider); slider.setScrollWheelEnabled (false);
            slider.setTitle (envelopeStages[stage] + " curve"); slider.setTooltip ("Bend this stage. Zero is linear. Drag the diamond on the graph for the same control.");
            curveAttachments[(size_t) i] = std::make_unique<SoundSliderAttachment> (processor, prefix + envelopeStages[stage] + "Curve", slider);
            slider.setComponentID (prefix + envelopeStages[stage] + "CurveLab");
            slider.textFromValueFunction = [] (double n) { return formatted (n, Unit::signedPercent); };
            slider.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue() / 100.0; }; slider.updateText();
        }
        addAndMakeVisible (sync); sync.setButtonText ("BPM"); sync.setBounds (704, 131, 72, 26);
        sync.setTooltip ("Synchronize all five timed stages to host or local tempo. Each stage has its own beat division; stored seconds are kept for when BPM is off.");
        syncAttachment = std::make_unique<SoundButtonAttachment> (processor, prefix + "TempoSync", sync); sync.setComponentID (prefix + "TempoSyncLab");
        addAndMakeVisible (legato); legato.setButtonText ("RETRIGGER IN LEGATO"); legato.setBounds (52, 508, 224, 28);
        legato.setTooltip ("In Legato voice mode, restart this envelope when an overlapping new note is pressed. Returning to an older held note keeps its current phase. Poly and Mono retain their normal triggering.");
        legatoAttachment = std::make_unique<SoundButtonAttachment> (processor, prefix + "RetriggerLegato", legato); legato.setComponentID (prefix + "RetriggerLegatoLab");
        refresh();
    }
    void endGesture() { graph.endGesture(); }
    bool refresh()
    {
        const juce::Component::SafePointer<EnvelopeLabPage> safe (this);
        if (! graph.refresh() || safe == nullptr) return false;
        const bool synced = value (processor, prefix + "TempoSync") > .5f;
        for (int i = 0; i < 6; ++i) if (i != 4)
        {
            times[(size_t) i].setVisible (! synced);
            if (safe == nullptr) return false;
            divisions[(size_t) envelopeDurationIndex (i)].setVisible (synced);
            if (safe == nullptr) return false;
        }
        repaint(); return true;
    }
    void paint (juce::Graphics& g) override
    {
        const auto settings = envelopeSettingsForUi (processor, prefix);
        for (int i = 0; i < 6; ++i)
        {
            const int x = 52 + i * 170;
            g.setColour (insetColour); g.fillRoundedRectangle ((float) x, 348, 158, 148, 6);
            g.setColour (borderColour); g.drawRoundedRectangle ((float) x + .5f, 348.5f, 157, 147, 6, 1);
            const auto label = i == 4 ? juce::String ("Held level") : envelopeDurationText (anyvoc::envelopeStageSeconds (settings, envelopeDurationIndex (i), processor.getTempo()));
            drawText (g, label, { x + 8, 414, 142, 19 }, 10, accent, false, juce::Justification::centred);
            if (i == 1 || i == 3 || i == 5) drawText (g, "CURVE", { x + 8, 437, 142, 14 }, 9, secondaryText, true);
            else drawText (g, i == 0 ? "WAIT BEFORE ATTACK" : i == 2 ? "STAY AT THE PEAK" : "WHILE THE KEY IS HELD", { x + 8, 444, 142, 24 }, 8.5f, secondaryText, false, juce::Justification::centred);
        }
        drawText (g, (processor.isTempoFromHost() ? "HOST " : "LOCAL ") + juce::String (processor.getTempo(), 1) + " BPM", { 788, 132, 174, 23 }, 10, accent, true, juce::Justification::centredRight);
        drawText (g, "Circles: time / level. Diamonds: curve. Shift: fine. Double-click: reset.", { 304, 510, 764, 24 }, 10, secondaryText);
        drawText (g, graph.feedback(), { 52, 544, 1016, 20 }, 11, primaryText);
    }
private:
    AnyvocAudioProcessor& processor;
    juce::String prefix;
    juce::Colour accent;
    EnvelopeGraph graph;
    std::array<juce::TextButton, 6> stageButtons;
    std::array<SoundSlider, 6> times;
    std::array<SoundSlider, 3> curves;
    std::array<juce::ComboBox, 5> divisions;
    juce::ToggleButton sync, legato;
    std::array<std::unique_ptr<SoundSliderAttachment>, 6> timeAttachments;
    std::array<std::unique_ptr<SoundSliderAttachment>, 3> curveAttachments;
    std::array<std::unique_ptr<SoundChoiceAttachment>, 5> divisionAttachments;
    std::unique_ptr<SoundButtonAttachment> syncAttachment, legatoAttachment;
};

class EnvelopeLab final : public juce::Component
{
public:
    explicit EnvelopeLab (AnyvocAudioProcessor& processor)
    {
        setComponentID ("EnvelopeLab"); setBounds (0, 0, designWidth, designHeight);
        for (int i = 0; i < 4; ++i)
        {
            pages[(size_t) i] = std::make_unique<EnvelopeLabPage> (processor, i);
            addChildComponent (*pages[(size_t) i]); pages[(size_t) i]->setBounds (getLocalBounds());
        }
        addAndMakeVisible (selector); selector.addItemList (envelopeNames, 1); selector.setBounds (344, 131, 246, 26);
        selector.setComponentID ("EnvelopeLabSelect"); selector.setTitle ("Choose an envelope to edit");
        selector.onChange = [this] { select (selector.getSelectedItemIndex()); };
        addAndMakeVisible (closeButton); closeButton.setButtonText ("DONE"); closeButton.setBounds (982, 131, 94, 28); closeButton.setComponentID ("EnvelopeLabClose");
        closeButton.onClick = [this] { close(); };
        select (0);
    }
    bool close()
    {
        const juce::Component::SafePointer<EnvelopeLab> safe (this);
        for (auto& page : pages) { page->endGesture(); if (safe == nullptr) return false; }
        setVisible (false); return safe != nullptr;
    }
    bool open (int index)
    {
        const juce::Component::SafePointer<EnvelopeLab> safe (this);
        if (! select (index) || safe == nullptr) return false;
        setVisible (true); if (safe == nullptr) return false; toFront (false); return true;
    }
    bool refresh() { return pages[(size_t) selected]->refresh(); }
    bool hitTest (int x, int y) override { return juce::Rectangle<int> (24, 117, 1076, 461).contains (x, y); }
    void paint (juce::Graphics& g) override { drawCard (g, { 24, 117, 1076, 461 }, "ENVELOPE LAB", "", mint); }
private:
    bool select (int index)
    {
        if (index < 0 || index >= 4) return true;
        const juce::Component::SafePointer<EnvelopeLab> safe (this);
        for (int i = 0; i < 4; ++i)
        {
            if (i != index) pages[(size_t) i]->endGesture();
            if (safe == nullptr) return false;
            pages[(size_t) i]->setVisible (i == index);
            if (safe == nullptr) return false;
        }
        selected = index; selector.setSelectedItemIndex (index, juce::dontSendNotification);
        return pages[(size_t) index]->refresh();
    }
    int selected = 0;
    std::array<std::unique_ptr<EnvelopeLabPage>, 4> pages;
    juce::ComboBox selector;
    juce::TextButton closeButton;
};

const juce::StringArray lfoDivisions { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/8D", "1/16D" };

void setValueWithGesture (AnyvocAudioProcessor& processor, const juce::String& id, float newValue)
{
    if (auto* parameter = processor.apvts.getParameter (id))
    {
        ScopedSoundEdit edit (processor, parameter->getName (128));
        parameter->beginChangeGesture();
        processor.setUserParameter (id, newValue);
        parameter->endChangeGesture();
    }
}

bool lfoHasPhase (int shape) { return shape >= 0 && (shape < 4 || shape == 5); }
bool lfoIsChaos (int shape) { return shape == 6 || shape == 7; }

float lfoSample (AnyvocAudioProcessor& processor, int index, float phase)
{
    const auto prefix = "lfo" + juce::String (index + 1);
    const int shape = processor.getLfoShape (index);
    if (shape == 0) return std::sin (phase * juce::MathConstants<float>::twoPi);
    if (shape == 1) return 1.0f - 4.0f * std::abs (phase - 0.5f);
    if (shape == 2) return 2.0f * phase - 1.0f;
    if (shape == 3) return phase < 0.5f ? 1.0f : -1.0f;
    if (shape == 4 || lfoIsChaos (shape)) return 0.0f; // Non-periodic sources have no drawable cycle.
    const float position = juce::jlimit (0.0f, 1.0f, phase) * 32.0f;
    const int first = juce::jmin (31, (int) position);
    const auto a = value (processor, prefix + "Point" + juce::String (first));
    const auto b = value (processor, prefix + "Point" + juce::String (first + 1));
    if (position >= 32.0f) return b;
    return value (processor, prefix + "Interpolation") > 0.5f ? a : a + (b - a) * (position - (float) first);
}

class LfoShapeChoice final : public juce::Component
{
public:
    LfoShapeChoice (AnyvocAudioProcessor& p, int indexIn) : processor (p), index (indexIn)
    {
        addAndMakeVisible (box);
        box.addItemList ({ "Sine", "Triangle", "Saw", "Square", "Random", "Custom", "Lorenz", "Rossler" }, 1);
        box.setTitle ("LFO " + juce::String (index + 1) + " shape");
        box.setComponentID ("lfo" + juce::String (index + 1) + "ShapeChoice");
        box.setTooltip ("Choose a repeating shape, Random, Custom, or evolving Lorenz / Rossler chaos. EDIT shows a drawing canvas or the real chaos trajectory. Existing custom points are kept when changing shape.");
        box.onChange = [this]
        {
            if (! matrixHierarchyVisible (this)) return;
            const auto selected = box.getSelectedItemIndex();
            if (selected < 0) return;
            const juce::Component::SafePointer<LfoShapeChoice> safe (this);
            const int lfoIndex = index;
            processor.setLfoShape (lfoIndex, selected, [safe, lfoIndex]
            {
                return safe != nullptr && safe->index == lfoIndex && matrixHierarchyVisible (safe.getComponent());
            });
            if (safe != nullptr) refresh();
        };
        const auto prefix = "lfo" + juce::String (index + 1);
        const std::array<const char*, 3> suffixes { "Shape", "Kind", "Chaos" };
        const juce::Component::SafePointer<LfoShapeChoice> safe (this);
        for (size_t i = 0; i < suffixes.size(); ++i) parameters[i] = processor.apvts.getParameter (prefix + suffixes[i]);
        for (size_t i = 0; i < parameters.size(); ++i)
            attachments[i] = std::make_unique<juce::ParameterAttachment> (*parameters[i], [safe] (float) { if (safe != nullptr) safe->refresh(); });
        refresh();
    }
    void refresh()
    {
        // Parameter listeners may run before APVTS updates its raw-value cache.
        const auto read = [this] (size_t slot) { return juce::roundToInt (parameters[slot]->convertFrom0to1 (parameters[slot]->getValue())); };
        const int chaos = juce::jlimit (0, 2, read (2)), kind = juce::jlimit (0, 2, read (1));
        box.setSelectedItemIndex (chaos != 0 ? chaos + 5 : kind != 0 ? kind + 3 : juce::jlimit (0, 3, read (0)), juce::dontSendNotification);
    }
    void paint (juce::Graphics& g) override { drawText (g, "SHAPE", { 0, 0, getWidth(), 16 }, 10.5f); }
    void resized() override { box.setBounds (0, 19, getWidth(), getHeight() - 19); }
private:
    AnyvocAudioProcessor& processor;
    int index;
    juce::ComboBox box;
    std::array<juce::RangedAudioParameter*, 3> parameters{};
    std::array<std::unique_ptr<juce::ParameterAttachment>, 3> attachments;
};

class LfoRateModeChoice final : public juce::Component
{
public:
    LfoRateModeChoice (AnyvocAudioProcessor& p, int indexIn) : processor (p), index (indexIn)
    {
        const auto prefix = "lfo" + juce::String (index + 1);
        addAndMakeVisible (box); box.addItemList ({ "Free Hz", "Tempo", "Audio Rate" }, 1);
        box.setComponentID (prefix + "RateMode"); box.setTitle ("LFO rate mode");
        box.setTooltip ("Free Hz keeps the original slow rate. Tempo uses a beat division. Audio Rate uses its separate 20-1000 speed. For chaos these scale evolution, not a repeating frequency.");
        box.onChange = [this]
        {
            if (! matrixHierarchyVisible (this)) return;
            const int selected = box.getSelectedItemIndex(); if (selected < 0) return;
            const juce::Component::SafePointer<LfoRateModeChoice> safe (this);
            processor.setLfoRateMode (index, selected, [safe] { return safe != nullptr && matrixHierarchyVisible (safe.getComponent()); });
            if (safe != nullptr) refresh();
        };
        sync = processor.apvts.getParameter (prefix + "Sync"); audio = processor.apvts.getParameter (prefix + "AudioRateEnabled");
        const juce::Component::SafePointer<LfoRateModeChoice> safe (this);
        const auto changed = [safe] (float) { if (safe != nullptr) safe->refresh(); };
        syncAttachment = std::make_unique<juce::ParameterAttachment> (*sync, changed);
        audioAttachment = std::make_unique<juce::ParameterAttachment> (*audio, changed);
        refresh();
    }
    void refresh()
    {
        // Read published parameter values, not APVTS's later listener cache.
        box.setSelectedItemIndex (sync->getValue() > .5f ? 1 : audio->getValue() > .5f ? 2 : 0, juce::dontSendNotification);
    }
    void paint (juce::Graphics& g) override { drawText (g, "RATE MODE", { 0, 0, getWidth(), 16 }, 10.5f); }
    void resized() override { box.setBounds (0, 19, getWidth(), getHeight() - 19); }
private:
    AnyvocAudioProcessor& processor;
    int index;
    juce::ComboBox box;
    juce::RangedAudioParameter* sync = nullptr;
    juce::RangedAudioParameter* audio = nullptr;
    std::unique_ptr<juce::ParameterAttachment> syncAttachment, audioAttachment;
};

class TempoDisplay final : public juce::Component
{
public:
    explicit TempoDisplay (AnyvocAudioProcessor& p) : processor (p)
    {
        addAndMakeVisible (slider);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 72, 23);
        styleSliderText (slider);
        slider.setScrollWheelEnabled (false);
        slider.setDoubleClickReturnValue (true, 120);
        slider.setTooltip ("Local tempo for synchronized LFOs and the arpeggiator. Your DAW's tempo takes over when a host clock is available.");
        slider.setTitle ("Local tempo");
        attachment = std::make_unique<SoundSliderAttachment> (processor, "tempo", slider);
        slider.textFromValueFunction = [] (double n) { return juce::String (juce::roundToInt (n)) + " BPM"; };
        slider.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
        slider.updateText(); refresh();
    }
    void refresh()
    {
        slider.setVisible (! processor.isTempoFromHost());
        repaint();
    }
    void resized() override { slider.setBounds (0, 15, getWidth(), 24); }
    void paint (juce::Graphics& g) override
    {
        const bool host = processor.isTempoFromHost();
        drawText (g, host ? "HOST CLOCK" : "LOCAL TEMPO", { 0, 0, getWidth(), 12 }, 8, mint, true, juce::Justification::centred);
        g.setColour (insetColour); g.fillRoundedRectangle (0, 15, (float) getWidth(), 24, 5);
        g.setColour (borderColour); g.drawRoundedRectangle (0.5f, 15.5f, (float) getWidth() - 1, 23, 5, 1);
        if (host) drawText (g, juce::String (processor.getTempo(), 1) + " BPM", { 0, 16, getWidth(), 22 }, 11, primaryText, true, juce::Justification::centred);
    }
private:
    AnyvocAudioProcessor& processor;
    SoundSlider slider;
    std::unique_ptr<SoundSliderAttachment> attachment;
};

// A bounded history of actual DSP snapshots, sampled by the existing UI timer.
// No oscillator, integration, phase cursor or invented continuation runs here.
class LfoChaosTrace
{
public:
    void refresh (AnyvocAudioProcessor& processor, int index, juce::Component& component)
    {
        const int currentShape = processor.getLfoShape (index);
        const int currentMode = juce::jlimit (0, 2, (int) value (processor, "lfo" + juce::String (index + 1) + "Mode"));
        const auto currentRevision = processor.getHistoryRevision();
        if (shape != currentShape || mode != currentMode || revision != currentRevision)
        {
            count = 0; next = 0; total = 0;
            shape = currentShape; mode = currentMode; revision = currentRevision;
        }
        if (lfoIsChaos (shape))
        {
            auto point = processor.getLfoChaosPoint (index, currentShape, currentMode);
            if (point == std::array<float, 3>{})
            {
                // API sentinel: reset or no matching shape/mode has been read.
                // Break the trail instead of drawing a fake origin or joining epochs.
                count = 0; next = 0; total = 0;
            }
            else if (std::all_of (point.begin(), point.end(), [] (float n) { return std::isfinite (n); }))
            {
                for (auto& n : point) n = juce::jlimit (-1.0f, 1.0f, n);
                // A held one-shot or idle engine must not acquire fake motion.
                if (count == 0 || point != at (count - 1))
                {
                    points[next] = point; next = (next + 1) % points.size();
                    count = juce::jmin (count + 1, points.size()); ++total;
                }
            }
        }
        const auto latest = count == 0 ? std::array<float, 3>{} : at (count - 1);
        // Read-only display state also makes real telemetry observable to UI tests.
        auto& properties = component.getProperties();
        properties.set ("chaosTraceSamples", (int) count);
        properties.set ("chaosTraceTotalSamples", (juce::int64) total);
        properties.set ("chaosTraceShape", shape);
        properties.set ("chaosTraceMode", mode);
        properties.set ("chaosTraceX", latest[0]); properties.set ("chaosTraceY", latest[1]); properties.set ("chaosTraceZ", latest[2]);
    }

    void paint (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour accent, int currentShape, int currentMode, bool compact) const
    {
        const auto r = bounds.toFloat();
        g.setColour (insetColour); g.fillRoundedRectangle (r, compact ? 5.0f : 8.0f);
        g.setColour (borderColour); g.drawRoundedRectangle (r.reduced (.5f), compact ? 5.0f : 8.0f, 1);
        const auto name = currentShape == 6 ? juce::String ("LORENZ") : juce::String ("ROSSLER");
        const bool available = currentShape == shape && currentMode == mode && count != 0;
        const auto latest = available ? at (count - 1) : std::array<float, 3>{};
        const float size = juce::jmax (1.0f, r.getHeight() - (compact ? 12.0f : 44.0f));
        const float divider = compact ? 66.0f : 260.0f;
        const auto xy = juce::Rectangle<float> (size, size).withCentre ({ compact ? 29.0f : divider * .5f, r.getCentreY() });
        const auto history = juce::Rectangle<float> (divider + 16, compact ? 23.0f : 27.0f,
                                                    r.getWidth() - divider - 32, r.getHeight() - (compact ? 32.0f : 54.0f));
        g.setColour (borderColour.withAlpha (.65f));
        g.drawRect (xy, .7f);
        g.drawHorizontalLine ((int) xy.getCentreY(), xy.getX(), xy.getRight());
        g.drawVerticalLine ((int) xy.getCentreX(), xy.getY(), xy.getBottom());
        g.drawHorizontalLine ((int) history.getCentreY(), history.getX(), history.getRight());
        if (! compact)
        {
            g.drawHorizontalLine ((int) history.getY(), history.getX(), history.getRight());
            g.drawHorizontalLine ((int) history.getBottom(), history.getX(), history.getRight());
            g.drawVerticalLine ((int) divider, 12, r.getBottom() - 12);
        }
        const auto xyPoint = [&xy] (const std::array<float, 3>& point)
        {
            return juce::Point<float> (xy.getCentreX() + point[0] * xy.getWidth() * .46f,
                                       xy.getCentreY() - point[1] * xy.getHeight() * .46f);
        };
        if (available)
        {
            for (size_t i = 1; i < count; ++i)
            {
                const auto opacity = .12f + .78f * (float) i / (float) juce::jmax ((size_t) 1, count - 1);
                g.setColour (accent.withAlpha (opacity));
                g.drawLine ({ xyPoint (at (i - 1)), xyPoint (at (i)) }, compact ? 1.1f : 1.5f);
            }
            const auto head = xyPoint (latest);
            g.setColour (accent.withAlpha (.2f)); g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre (head));
            g.setColour (primaryText); g.fillEllipse (juce::Rectangle<float> (4, 4).withCentre (head));
            juce::Path output;
            for (size_t i = 0; i < count; ++i)
            {
                const float x = history.getRight() - (float) (count - 1 - i) / (float) (points.size() - 1) * history.getWidth();
                const float y = history.getCentreY() - at (i)[0] * history.getHeight() * .46f;
                if (i == 0) output.startNewSubPath (x, y); else output.lineTo (x, y);
            }
            g.setColour (accent); g.strokePath (output, juce::PathStrokeType (compact ? 1.3f : 1.8f));
            g.setColour (primaryText);
            g.fillEllipse (juce::Rectangle<float> (4, 4).withCentre ({ history.getRight(), history.getCentreY() - latest[0] * history.getHeight() * .46f }));
        }
        if (compact)
            drawText (g, name + " / LIVE X-Y + X OUTPUT", { (int) divider + 16, 3, bounds.getWidth() - (int) divider - 24, 16 }, 8, accent, true);
        else
        {
            drawText (g, name + " / X-Y TRAJECTORY", { 14, 3, 232, 17 }, 9, accent, true);
            drawText (g, "X OUTPUT / RECENT SNAPSHOTS", { 278, 3, bounds.getWidth() - 294, 17 }, 9, accent, true);
            drawText (g, "REAL ENGINE STATE", { 14, bounds.getHeight() - 20, 232, 16 }, 8, secondaryText, false, juce::Justification::centred);
            drawText (g, "X  " + juce::String (latest[0], 3) + "     Y  " + juce::String (latest[1], 3) + "     Z  " + juce::String (latest[2], 3),
                      { 278, bounds.getHeight() - 20, bounds.getWidth() - 294, 16 }, 9, primaryText);
            if (! available || count < 2)
                drawText (g, "PLAY A NOTE TO BUILD THE TRAIL", history.toNearestInt(), 10, secondaryText, false, juce::Justification::centred);
        }
    }

private:
    const std::array<float, 3>& at (size_t i) const { return points[(next + points.size() - count + i) % points.size()]; }
    std::array<std::array<float, 3>, 256> points{};
    size_t count = 0, next = 0;
    std::uint64_t total = 0, revision = 0;
    int shape = -1, mode = -1;
};

class LfoDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    LfoDisplay (AnyvocAudioProcessor& p, int indexIn, juce::Colour colour)
        : processor (p), index (indexIn - 1), accent (colour)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Click to open the LFO Lab. Draw a curve or view a real Lorenz / Rossler trajectory; synchronize movement to tempo or restart it from each note.");
    }
    void refresh() { chaosTrace.refresh (processor, index, *this); repaint(); }
    void mouseUp (const juce::MouseEvent& event) override { if (! event.mouseWasDraggedSinceMouseDown() && onOpen) onOpen(); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (2);
        const auto shape = processor.getLfoShape (index);
        if (lfoIsChaos (shape))
        {
            const int mode = juce::jlimit (0, 2, (int) value (processor, "lfo" + juce::String (index + 1) + "Mode"));
            chaosTrace.paint (g, getLocalBounds(), accent, shape, mode, true); return;
        }
        g.setColour (insetColour); g.fillRoundedRectangle (r, 5);
        g.setColour (borderColour); g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        juce::Path path;
        for (int i = 0; i <= 192; ++i)
        {
            const float phase = (float) i / 192.0f;
            const float sample = lfoSample (processor, index, phase);
            const auto x = r.getX() + phase * r.getWidth(), y = r.getCentreY() - sample * (r.getHeight() * 0.39f);
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        g.setColour (accent); g.strokePath (path, juce::PathStrokeType (1.6f));
        const float phase = juce::jlimit (0.0f, 1.0f, processor.getLfoPhase (index));
        g.setColour (accent.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (r.getX() + phase * r.getWidth()), r.getY(), r.getBottom());
        if (shape == 4) drawText (g, "RANDOM / VARIABLE OUTPUT", getLocalBounds().reduced (8), 9, primaryText, true, juce::Justification::centred);
    }
    std::function<void()> onOpen;
private:
    AnyvocAudioProcessor& processor;
    int index;
    juce::Colour accent;
    LfoChaosTrace chaosTrace;
};

struct LfoCurveSnapshot
{
    AnyvocAudioProcessor& processor;
    std::array<juce::RangedAudioParameter*, 46> parameters{};
    std::array<float, 46> expected{};
    std::uint64_t revision;

    LfoCurveSnapshot (AnyvocAudioProcessor& p, const juce::String& prefix)
        : processor (p), revision (p.getHistoryRevision())
    {
        for (int i = 0; i < 33; ++i) parameters[(size_t) i] = p.apvts.getParameter (prefix + "Point" + juce::String (i));
        parameters[33] = p.apvts.getParameter (prefix + "Interpolation");
        parameters[34] = p.apvts.getParameter (prefix + "Shape");
        parameters[35] = p.apvts.getParameter (prefix + "Kind");
        const std::array<const char*, 10> additional { "Rate", "Sync", "Division", "Mode", "Phase", "Direction", "FollowSwing", "AudioRateEnabled", "AudioRateHz", "Chaos" };
        for (size_t i = 0; i < additional.size(); ++i) parameters[i + 36] = p.apvts.getParameter (prefix + additional[i]);
        for (size_t i = 0; i < parameters.size(); ++i) expected[i] = parameters[i]->getValue();
    }
    bool valid() const
    {
        if (processor.getHistoryRevision() != revision) return false;
        for (size_t i = 0; i < parameters.size(); ++i)
            if (parameters[i]->getValue() != expected[i]) return false;
        return true;
    }
};

class LfoCurveEditor final : public juce::Component, public juce::SettableTooltipClient
{
public:
    LfoCurveEditor (AnyvocAudioProcessor& p, int indexIn, juce::Colour colour)
        : processor (p), index (indexIn), prefix ("lfo" + juce::String (indexIn + 1)), accent (colour)
    {
        setTitle ("LFO " + juce::String (index + 1) + " drawable curve");
        setComponentID (prefix + "Curve");
        setWantsKeyboardFocus (true);
        setTooltip ("Draw across the graph to shape 33 evenly spaced points. Drag between points to paint a continuous stroke. Drawing switches the shape to Custom.");
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        updateViewProperties();
    }
    ~LfoCurveEditor() override { lifetime.reset(); endStroke(); }

    void refresh()
    {
        chaosTrace.refresh (processor, index, *this);
        setMouseCursor (lfoIsChaos (processor.getLfoShape (index)) ? juce::MouseCursor::NormalCursor : juce::MouseCursor::CrosshairCursor);
        setTooltip (lfoIsChaos (processor.getLfoShape (index))
                    ? "Real normalized DSP snapshots: X-Y trajectory and recent X output. Repeated held values are omitted; spacing is not an audio time axis. Choose Custom to draw the saved curve."
                    : editMode == 1 ? "Drag a closed range of points to select it. Selection changes only the view; FLIP and APPLY affect this range."
                    : "Draw across the graph to shape 33 evenly spaced points. Grid snapping is optional. Drawing switches the shape to Custom.");
        updateViewProperties();
        repaint();
    }

    bool canEdit() const { return matrixHierarchyVisible (this) && isEnabled() && ! lfoIsChaos (processor.getLfoShape (index)); }
    void setToolsVisible (bool visible) { toolView = visible; }
    int firstSelected() const { return selectionFirst; }
    int lastSelected() const { return selectionLast; }
    void setViewTools (int modeIn, int gridXIn, int gridYIn, bool snapXIn, bool snapYIn)
    {
        if (! canEdit()) return;
        const auto validGrid = [] (int n) { return n == 1 || n == 2 || n == 4 || n == 8 || n == 16 || n == 32; };
        if (! validGrid (gridXIn) || ! validGrid (gridYIn)) return;
        const juce::Component::SafePointer<LfoCurveEditor> safe (this);
        endStroke();
        if (safe == nullptr || ! canEdit()) return;
        editMode = juce::jlimit (0, 1, modeIn); gridX = gridXIn; gridY = gridYIn;
        snapX = snapXIn; snapY = snapYIn; selecting = false;
        refresh();
    }
    void selectAll()
    {
        if (! canEdit()) return;
        selectionFirst = 0; selectionLast = 32; updateViewProperties(); repaint();
    }
    anyvoc::LfoCurveData readCurve() const
    {
        anyvoc::LfoCurveData data;
        const int shape = processor.getLfoShape (index);
        for (int i = 0; i < 33; ++i)
        {
            auto* parameter = processor.apvts.getParameter (prefix + "Point" + juce::String (i));
            data.points[(size_t) i] = shape == 4 || shape == 5 || lfoIsChaos (shape)
                ? parameter->convertFrom0to1 (parameter->getValue()) : lfoSample (processor, index, (float) i / 32.0f);
        }
        auto* interpolation = processor.apvts.getParameter (prefix + "Interpolation");
        data.interpolation = (int) interpolation->convertFrom0to1 (interpolation->getValue());
        return data;
    }
    bool dataWouldChange (const anyvoc::LfoCurveData& data, bool replaceAsset = false) const
    {
        // Importing an asset explicitly selects Custom, even when its sampled
        // points happen to equal the currently displayed basic/saved curve.
        if (replaceAsset && processor.getLfoShape (index) != 5) return true;
        const auto before = readCurve();
        if (before.points == data.points && before.interpolation == data.interpolation) return false;
        bool changed = before.interpolation != data.interpolation;
        for (int i = 0; i < 33 && ! changed; ++i)
        {
            const auto* parameter = processor.apvts.getParameter (prefix + "Point" + juce::String (i));
            const auto canonical = [parameter] (float n) { return parameter->convertTo0to1 (parameter->convertFrom0to1 (parameter->convertTo0to1 (n))); };
            changed = canonical (before.points[(size_t) i]) != canonical (data.points[(size_t) i]);
        }
        return changed;
    }
    bool applyData (const anyvoc::LfoCurveData& data, const juce::String& label, bool replaceAsset = false,
                    std::function<bool()> isCurrent = {})
    {
        if (! canEdit() || ! anyvoc::LfoCurveDocument::valid (data) || (isCurrent && ! isCurrent())) return false;
        if (! dataWouldChange (data, replaceAsset)) return true;
        const juce::Component::SafePointer<LfoCurveEditor> safe (this);
        auto edit = beginStroke (label, std::move (isCurrent));
        if (edit == nullptr) return false;
        for (int i = 0; i < 33; ++i) if (! edit->write (i, data.points[(size_t) i])) return false;
        if (! edit->write (33, (float) data.interpolation) || ! edit->write (35, 2.0f)) return false;
        edit->finish();
        if (safe == nullptr) return false;
        if (stroke == edit) stroke.reset();
        const auto edited = onEdited;
        if (edited) edited();
        if (safe != nullptr) repaint();
        return safe != nullptr;
    }

    void endStroke()
    {
        drawing = false;
        selecting = false;
        auto edit = std::move (stroke);
        if (edit != nullptr) edit->finish();
    }

    void applyCurve (int curve)
    {
        if (! canEdit()) return;
        const juce::Component::SafePointer<LfoCurveEditor> safe (this);
        auto edit = beginStroke ("LFO " + juce::String (index + 1) + " curve preset");
        if (edit == nullptr) return;
        for (int i = 0; i < 33; ++i)
        {
            const float phase = (float) i / 32.0f;
            const float n = curve == 0 ? std::sin (phase * juce::MathConstants<float>::twoPi)
                          : curve == 1 ? phase * 2.0f - 1.0f
                          : curve == 2 ? 1.0f - std::abs (phase - 0.5f) * 4.0f
                          : curve == 3 ? ((i / 4) % 2 == 0 ? 1.0f : -1.0f) : 0.0f;
            if (! edit->write (i, n)) return;
        }
        if (! edit->write (33, curve == 3 ? 1.0f : 0.0f) || ! edit->write (35, 2.0f)) return;
        edit->finish();
        if (safe == nullptr) return;
        if (stroke == edit) stroke.reset();
        const auto edited = onEdited;
        if (edited) edited();
        if (safe != nullptr) repaint();
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu() || ! canEdit()) return;
        const juce::Component::SafePointer<LfoCurveEditor> safe (this);
        if (editMode == 1)
        {
            endStroke();
            if (safe == nullptr || ! canEdit()) return;
            selectionAnchor = rawPointAt (event.position.x);
            selectionFirst = selectionLast = selectionAnchor; selecting = true;
            updateViewProperties(); repaint(); return;
        }
        auto edit = beginStroke ("Draw LFO " + juce::String (index + 1));
        if (edit == nullptr) return;
        grabKeyboardFocus();
        if (safe == nullptr || ! edit->valid()) { edit->finish(); return; }
        if (processor.getLfoShape (index) != 5)
        {
            const auto initial = readCurve().points;
            for (int i = 0; i < 33; ++i) if (! edit->write (i, initial[(size_t) i])) return;
            if (! edit->write (35, 2.0f)) return;
        }
        drawing = true;
        lastPoint = pointAt (event.position.x);
        lastValue = valueAt (event.position.y);
        if (! edit->write (lastPoint, lastValue)) return;
        hoveredPoint = lastPoint;
        const auto edited = onEdited;
        if (edited) edited();
        if (safe != nullptr) repaint();
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (selecting)
        {
            if (! canEdit()) { selecting = false; return; }
            const int point = rawPointAt (event.position.x);
            selectionFirst = juce::jmin (selectionAnchor, point); selectionLast = juce::jmax (selectionAnchor, point);
            updateViewProperties(); repaint(); return;
        }
        if (! drawing || stroke == nullptr) return;
        auto edit = stroke;
        if (! edit->valid()) { endStroke(); return; }
        const int nextPoint = pointAt (event.position.x);
        const float nextValue = valueAt (event.position.y);
        if (nextPoint == lastPoint)
        {
            if (! edit->write (nextPoint, nextValue)) return;
        }
        else
        {
            const int low = juce::jmin (lastPoint, nextPoint), high = juce::jmax (lastPoint, nextPoint);
            for (int i = low; i <= high; ++i)
            {
                const float blend = (float) (i - lastPoint) / (float) (nextPoint - lastPoint);
                const float n = lastValue + (nextValue - lastValue) * blend;
                if (! edit->write (i, snapY ? anyvoc::LfoCurveDocument::snapValue (n, gridY) : n)) return;
            }
        }
        lastPoint = nextPoint; lastValue = nextValue; hoveredPoint = nextPoint;
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { endStroke(); }
    void focusLost (FocusChangeType) override { endStroke(); }
    void visibilityChanged() override { if (! isShowing()) endStroke(); }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey) { endStroke(); return true; }
        return false;
    }
    void mouseMove (const juce::MouseEvent& event) override
    {
        if (lfoIsChaos (processor.getLfoShape (index))) return;
        const int point = pointAt (event.position.x);
        if (point != hoveredPoint) { hoveredPoint = point; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hoveredPoint = -1; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto shape = processor.getLfoShape (index);
        if (lfoIsChaos (shape))
        {
            const int mode = juce::jlimit (0, 2, (int) value (processor, prefix + "Mode"));
            chaosTrace.paint (g, getLocalBounds(), accent, shape, mode, false); return;
        }
        const auto bounds = getLocalBounds().toFloat();
        const auto r = plotBounds();
        g.setColour (insetColour); g.fillRoundedRectangle (bounds, 8);
        g.setColour (borderColour); g.drawRoundedRectangle (bounds.reduced (0.5f), 8, 1);
        for (int i = 0; i <= gridX; ++i)
        {
            g.setColour (borderColour.withAlpha (snapX ? .8f : i % 4 == 0 ? .8f : .25f));
            g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * (float) i / (float) gridX), r.getY(), r.getBottom());
        }
        for (int i = 0; i <= gridY; ++i)
        {
            g.setColour (borderColour.withAlpha (i * 2 == gridY ? 1.0f : snapY ? .7f : .4f));
            g.drawHorizontalLine (juce::roundToInt (r.getY() + r.getHeight() * (float) i / (float) gridY), r.getX(), r.getRight());
        }
        if (editMode == 1 || selectionFirst != 0 || selectionLast != 32)
        {
            const float left = r.getX() + r.getWidth() * (float) selectionFirst / 32.0f;
            const float right = r.getX() + r.getWidth() * (float) selectionLast / 32.0f;
            g.setColour (accent.withAlpha (.1f)); g.fillRect (left, r.getY(), juce::jmax (2.0f, right - left), r.getHeight());
            g.setColour (accent.withAlpha (.7f)); g.drawVerticalLine (juce::roundToInt (left), r.getY(), r.getBottom());
            g.drawVerticalLine (juce::roundToInt (right), r.getY(), r.getBottom());
        }
        drawText (g, "+1", { 4, 8, 23, 15 }, 9);
        drawText (g, "0", { 7, getHeight() / 2 - 7, 17, 15 }, 9);
        drawText (g, "-1", { 6, getHeight() - 26, 20, 15 }, 9);
        const bool random = processor.getLfoShape (index) == 4, savedRandom = random && toolView;
        const auto data = readCurve();
        const auto sample = [&] (float phase)
        {
            if (! savedRandom) return lfoSample (processor, index, phase);
            const float point = juce::jlimit (0.0f, 32.0f, phase * 32.0f);
            const auto left = (size_t) std::floor (point), right = juce::jmin ((size_t) 32, left + 1);
            return data.interpolation == 1 ? data.points[left] : data.points[left] + (data.points[right] - data.points[left]) * (point - (float) left);
        };
        juce::Path path;
        for (int i = 0; i <= 384; ++i)
        {
            const float phase = (float) i / 384;
            const auto point = graphPoint (phase, sample (phase));
            if (i == 0) path.startNewSubPath (point); else path.lineTo (point);
        }
        g.setColour (accent.withAlpha (0.12f)); g.strokePath (path, juce::PathStrokeType (6));
        g.setColour (accent); g.strokePath (path, juce::PathStrokeType (2));
        if (! random || savedRandom)
            for (int i = 0; i < 33; ++i)
            {
                const auto point = graphPoint ((float) i / 32, sample ((float) i / 32));
                const float diameter = i == hoveredPoint ? 7.5f : 4.0f;
                g.setColour (i == hoveredPoint ? primaryText : accent);
                g.fillEllipse (juce::Rectangle<float> (diameter, diameter).withCentre (point));
            }
        const float phase = juce::jlimit (0.0f, 1.0f, processor.getLfoPhase (index));
        const float x = r.getX() + phase * r.getWidth();
        if (! random)
        {
            const auto start = (float) anyvoc::lfoReadPhase (0, value (processor, prefix + "Phase"), (int) value (processor, prefix + "Direction"));
            const float startX = r.getX() + start * r.getWidth();
            const float dash[] { 3, 3 };
            g.setColour (accent.withAlpha (.45f)); g.drawDashedLine ({ startX, r.getY(), startX, r.getBottom() }, dash, 2);
            drawText (g, "START", { juce::jlimit (31, getWidth() - 55, (int) startX - 20), getHeight() - 16, 43, 13 }, 8, accent, false, juce::Justification::centred);
        }
        if (! random)
        {
            g.setColour (primaryText.withAlpha (0.45f));
            g.drawLine (x, r.getY(), x, r.getBottom(), 1);
            juce::Path marker; marker.addTriangle (x - 4, r.getY() - 7, x + 4, r.getY() - 7, x, r.getY() - 1);
            g.fillPath (marker);
        }
        if (savedRandom) drawText (g, "SAVED CUSTOM CURVE / RANDOM OUTPUT IS NOT SHOWN", { 34, 1, 650, 15 }, 9, accent, true);
        else if (random) drawText (g, "RANDOM HAS NO FIXED CURVE  /  TOOLS EDIT THE SAVED CUSTOM CURVE", getLocalBounds().reduced (30, 16), 11, primaryText, true, juce::Justification::centred);
        if (hoveredPoint >= 0 && (! random || savedRandom))
            drawText (g, "POINT " + juce::String (hoveredPoint + 1) + "   " + formatted (sample ((float) hoveredPoint / 32), Unit::signedPercent),
                      { getWidth() - 180, 1, 162, 15 }, 9, primaryText, false, juce::Justification::centredRight);
    }
    std::function<void()> onEdited;

private:
    juce::Rectangle<float> plotBounds() const { return getLocalBounds().toFloat().withTrimmedLeft (31).withTrimmedRight (15).reduced (0, 18); }
    int rawPointAt (float x) const { return juce::jlimit (0, 32, juce::roundToInt ((x - plotBounds().getX()) / plotBounds().getWidth() * 32)); }
    int pointAt (float x) const
    {
        if (! snapX) return rawPointAt (x);
        const float phase = juce::jlimit (0.0f, 1.0f, (x - plotBounds().getX()) / plotBounds().getWidth());
        const int gridPoint = (int) std::floor (phase * (float) gridX + .5f);
        return juce::jlimit (0, 32, gridPoint * (32 / gridX));
    }
    float valueAt (float y) const
    {
        const float n = juce::jlimit (-1.0f, 1.0f, 1.0f - (y - plotBounds().getY()) / plotBounds().getHeight() * 2);
        return snapY ? anyvoc::LfoCurveDocument::snapValue (n, gridY) : n;
    }
    void updateViewProperties()
    {
        auto& props = getProperties();
        props.set ("curveSelectionFirst", selectionFirst); props.set ("curveSelectionLast", selectionLast);
        props.set ("curveGridX", gridX); props.set ("curveGridY", gridY);
        props.set ("curveSnapX", snapX); props.set ("curveSnapY", snapY); props.set ("curveEditMode", editMode);
    }
    juce::Point<float> graphPoint (float phase, float sample) const
    {
        const auto r = plotBounds();
        return { r.getX() + phase * r.getWidth(), r.getCentreY() - sample * r.getHeight() * 0.5f };
    }
    // The operation outlives the component if a host callback closes the editor.
    // Mark every gesture closed before notifying the host, then use only this
    // independent state to balance the remaining gestures and sound transaction.
    struct CurveEdit : LfoCurveSnapshot
    {
        std::weak_ptr<int> lifetime;
        std::array<bool, 46> gesturing{};
        std::function<bool()> isCurrent;
        bool active = true;

        CurveEdit (AnyvocAudioProcessor& p, const juce::String& prefixIn, std::weak_ptr<int> alive, std::function<bool()> current)
            : LfoCurveSnapshot (p, prefixIn), lifetime (std::move (alive)), isCurrent (std::move (current)) {}
        bool valid() const
        {
            return active && ! lifetime.expired() && (! isCurrent || isCurrent()) && LfoCurveSnapshot::valid();
        }
        bool write (int slot, float plain)
        {
            if (! valid()) { finish(); return false; }
            auto* parameter = parameters[(size_t) slot];
            const float target = parameter->convertTo0to1 (parameter->convertFrom0to1 (parameter->convertTo0to1 (plain)));
            if (target == expected[(size_t) slot]) return true;
            if (! gesturing[(size_t) slot])
            {
                gesturing[(size_t) slot] = true;
                parameter->beginChangeGesture();
                if (! valid()) { finish(); return false; }
            }
            expected[(size_t) slot] = target;
            processor.setUserParameter (parameter->paramID, plain);
            if (! valid()) { finish(); return false; }
            return true;
        }
        void finish()
        {
            if (! active) return;
            active = false;
            const auto pending = gesturing;
            gesturing.fill (false);
            for (size_t i = 0; i < parameters.size(); ++i) if (pending[i]) parameters[i]->endChangeGesture();
            processor.endUserEdit();
        }
    };
    std::shared_ptr<CurveEdit> beginStroke (const juce::String& label, std::function<bool()> isCurrent = {})
    {
        const juce::Component::SafePointer<LfoCurveEditor> safe (this);
        const LfoCurveSnapshot entry (processor, prefix);
        endStroke();
        if (safe == nullptr || ! canEdit() || ! entry.valid() || (isCurrent && ! isCurrent())) return {};
        auto edit = std::make_shared<CurveEdit> (processor, prefix, lifetime,
            [safe, current = std::move (isCurrent)] { return safe != nullptr && safe->canEdit() && (! current || current()); });
        stroke = edit;
        processor.beginUserEdit (label);
        if (! edit->valid()) { edit->finish(); return {}; }
        return edit;
    }
    AnyvocAudioProcessor& processor;
    int index, lastPoint = 0, hoveredPoint = -1;
    juce::String prefix;
    juce::Colour accent;
    float lastValue = 0;
    bool drawing = false;
    int selectionFirst = 0, selectionLast = 32, selectionAnchor = 0, editMode = 0, gridX = 32, gridY = 4;
    bool selecting = false, snapX = false, snapY = false, toolView = false;
    std::shared_ptr<int> lifetime = std::make_shared<int> (0);
    std::shared_ptr<CurveEdit> stroke;
    LfoChaosTrace chaosTrace;

};

class DefaultLfoCurveServices final : public LfoCurveServices,
                                      public std::enable_shared_from_this<DefaultLfoCurveServices>
{
public:
    juce::String readClipboard() override { return juce::SystemClipboard::getTextFromClipboard(); }
    void writeClipboard (const juce::String& text) override { juce::SystemClipboard::copyTextToClipboard (text); }
    void chooseCurveFile (bool save, std::function<void (juce::File)> completion) override
    {
        chooser = std::make_unique<juce::FileChooser> (save ? "Save LFO curve" : "Load LFO curve",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("My Curve.anycurve"), "*.anycurve");
        const int flags = save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting
                               : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        chooser->launchAsync (flags, [weak = weak_from_this(), completion = std::move (completion)] (const juce::FileChooser& selected)
        {
            if (auto keepAlive = weak.lock())
            {
                const auto file = selected.getResult();
                completion (file);
            }
        });
    }
private:
    std::unique_ptr<juce::FileChooser> chooser;
};

class LfoDrawingTools final : public juce::Component
{
public:
    LfoDrawingTools (AnyvocAudioProcessor& p, int indexIn, LfoCurveEditor& canvasIn,
                     juce::Colour colour, std::shared_ptr<LfoCurveServices> servicesIn)
        : processor (p), index (indexIn), prefix ("lfo" + juce::String (indexIn + 1)),
          canvas (canvasIn), accent (colour), services (std::move (servicesIn))
    {
        setComponentID (prefix + "ToolsPanel");
        const auto box = [this] (juce::ComboBox& control, const char* suffix, juce::Rectangle<int> bounds, const juce::String& tip)
        {
            addAndMakeVisible (control); control.setComponentID (prefix + suffix); control.setTitle (suffix);
            control.setBounds (bounds); control.setTooltip (tip);
        };
        box (editMode, "EditMode", { 0, 18, 104, 26 }, "Draw edits points. Select drags an inclusive range without changing sound.");
        editMode.addItem ("Draw", 1); editMode.addItem ("Select", 2); editMode.setSelectedId (1, juce::dontSendNotification);
        box (gridX, "GridX", { 124, 18, 66, 26 }, "Horizontal divisions across the whole cycle. Snap X rounds to the nearest grid point.");
        box (gridY, "GridY", { 292, 18, 66, 26 }, "Vertical divisions across -1 to +1. Exact half-grid positions round upwards.");
        for (int division : { 1, 2, 4, 8, 16, 32 })
        {
            gridX.addItem (juce::String (division), division); gridY.addItem (juce::String (division), division);
        }
        gridX.setSelectedId (32, juce::dontSendNotification); gridY.setSelectedId (4, juce::dontSendNotification);
        for (auto* control : { &editMode, &gridX, &gridY }) control->onChange = [this] { updateCanvasTools(); };
        const auto toggle = [this] (juce::ToggleButton& control, const char* suffix, int x, const char* title)
        {
            addAndMakeVisible (control); control.setComponentID (prefix + suffix); control.setButtonText (title);
            control.setBounds (x, 18, 78, 26); control.setTooltip ("Snapping affects new drawing only; existing points are unchanged.");
            control.onClick = [this] { updateCanvasTools(); };
        };
        toggle (snapX, "SnapX", 196, "SNAP X"); toggle (snapY, "SnapY", 364, "SNAP Y");
        const auto button = [this] (juce::TextButton& control, const char* suffix, const char* title,
                                    juce::Rectangle<int> bounds, const juce::String& tip)
        {
            addAndMakeVisible (control); control.setComponentID (prefix + suffix); control.setButtonText (title);
            control.setBounds (bounds); control.setTooltip (tip);
        };
        button (selectAll, "SelectAll", "SELECT ALL", { 464, 18, 106, 26 }, "Select all 33 points. This does not change sound or undo history.");
        button (flipX, "FlipX", "FLIP X", { 582, 18, 84, 26 }, "Reverse the selected points horizontally. Points outside the selection stay unchanged.");
        button (flipY, "FlipY", "FLIP Y", { 678, 18, 84, 26 }, "Invert selected values around zero. One undoable edit.");
        selectAll.onClick = [this] { if (canOperate()) { canvas.selectAll(); repaint(); } };
        flipX.onClick = [this] { flip (false); }; flipY.onClick = [this] { flip (true); };
        const auto slider = [this] (juce::Slider& control, const char* suffix, juce::Rectangle<int> bounds,
                                    double minimum, double maximum, double initial, const juce::String& tip)
        {
            addAndMakeVisible (control); control.setComponentID (prefix + suffix); control.setTitle (suffix);
            control.setSliderStyle (juce::Slider::LinearHorizontal); control.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 24);
            styleSliderText (control); control.setScrollWheelEnabled (false); control.setBounds (bounds);
            control.setRange (minimum, maximum, 0); control.setValue (initial, juce::dontSendNotification);
            control.setDoubleClickReturnValue (true, initial); control.setNumDecimalPlacesToDisplay (3); control.setTooltip (tip);
        };
        slider (offsetX, "OffsetX", { 0, 72, 172, 26 }, -32, 32, 0, "Move by point units: 1 point = 1/32 cycle. APPLY commits; values outside the fixed selection hold its endpoint.");
        slider (offsetY, "OffsetY", { 190, 72, 172, 26 }, -2, 2, 0, "Add this value after vertical scaling, clamped to -1..+1. APPLY commits.");
        slider (scaleX, "ScaleX", { 380, 72, 172, 26 }, .25, 4, 1, "Scale width around the selection midpoint. 1 is unchanged. APPLY commits.");
        slider (scaleY, "ScaleY", { 570, 72, 172, 26 }, 0, 4, 1, "Scale height around zero. 0 makes a flat selection. APPLY commits.");
        button (apply, "ApplyTransform", "APPLY TRANSFORM", { 784, 72, 232, 26 }, "Apply the four staged values to the fixed selection as one undo step. Identity leaves sound and history unchanged.");
        apply.onClick = [this]
        {
            if (! canOperate()) return;
            const auto before = canvas.readCurve(); anyvoc::LfoCurveData after;
            if (anyvoc::LfoCurveDocument::transform (before, canvas.firstSelected(), canvas.lastSelected(),
                    (float) offsetX.getValue(), (float) offsetY.getValue(), (float) scaleX.getValue(), (float) scaleY.getValue(), after))
                applyCurve (after, "Transform LFO curve");
        };
        box (pointIndex, "PointIndex", { 0, 122, 76, 26 }, "Point 1 is the start and point 33 is the end. Selecting a point does not change sound.");
        for (int i = 1; i <= 33; ++i) pointIndex.addItem (juce::String (i), i);
        pointIndex.setSelectedId (1, juce::dontSendNotification);
        slider (pointValue, "PointValue", { 90, 122, 152, 26 }, -1, 1, 0, "Precise point value from -1 to +1. SET commits only this point; grid snapping does not affect numeric entry.");
        pointIndex.onChange = [this] { if (canOperate()) refreshPoint (true); };
        button (setPoint, "SetPoint", "SET", { 252, 122, 60, 26 }, "Apply the staged value to this one point as one undo step.");
        setPoint.onClick = [this]
        {
            if (! canOperate()) return;
            auto data = canvas.readCurve(); data.points[(size_t) juce::jlimit (0, 32, pointIndex.getSelectedId() - 1)] = (float) pointValue.getValue();
            applyCurve (data, "Set LFO curve point");
        };
        button (copy, "CopyCurve", "COPY CURVE", { 336, 122, 132, 26 }, "Copy all 33 points and interpolation as reusable curve XML; selection and timing settings are excluded.");
        button (paste, "PasteCurve", "PASTE CURVE", { 480, 122, 132, 26 }, "Paste a complete Anyvoc curve, replacing all 33 points and interpolation and selecting Custom. Timing settings are kept.");
        button (load, "LoadCurve", "LOAD CURVE", { 624, 122, 132, 26 }, "Load a .anycurve asset (up to 16 KiB) and select Custom. It is not a sound preset; timing settings are kept.");
        button (save, "SaveCurve", "SAVE CURVE", { 768, 122, 132, 26 }, "Save all 33 points and interpolation as a .anycurve asset without changing sound or undo history.");
        copy.onClick = [this] { copyCurve(); }; paste.onClick = [this] { pasteCurve(); };
        load.onClick = [this] { chooseFile (false); }; save.onClick = [this] { chooseFile (true); };
        addAndMakeVisible (status); status.setComponentID (prefix + "ToolsStatus"); status.setBounds (0, 151, 1016, 16);
        status.setFont (font (9)); status.setColour (juce::Label::textColourId, secondaryText); status.setInterceptsMouseClicks (false, false);
        message ("Select a range, stage a transform, then APPLY. COPY / SAVE always use the whole curve.");
    }
    bool canOperate() const { return matrixHierarchyVisible (this) && isEnabled() && canvas.canEdit(); }
    void invalidatePending() { ++requestSerial; }
    void visibilityChanged() override { if (! matrixHierarchyVisible (this)) invalidatePending(); }
    void refresh()
    {
        setEnabled (! lfoIsChaos (processor.getLfoShape (index)));
        setAlpha (isEnabled() ? 1.0f : .35f);
        refreshPoint (false); repaint();
    }
    void paint (juce::Graphics& g) override
    {
        drawText (g, "EDIT MODE", { 0, 0, 104, 16 }, 9);
        drawText (g, "X GRID", { 124, 0, 66, 16 }, 9); drawText (g, "Y GRID", { 292, 0, 66, 16 }, 9);
        drawText (g, "SELECTED  " + juce::String (canvas.firstSelected() + 1) + " - " + juce::String (canvas.lastSelected() + 1),
                  { 784, 18, 232, 26 }, 10, accent, true, juce::Justification::centredRight);
        const juce::StringArray labels { "X OFFSET / POINTS", "Y OFFSET / VALUE", "WIDTH / MULTIPLIER", "HEIGHT / MULTIPLIER" };
        for (int i = 0; i < 4; ++i) drawText (g, labels[i], { i * 190, 54, 172, 16 }, 9);
        drawText (g, "POINT", { 0, 104, 76, 16 }, 9); drawText (g, "VALUE / -1 TO +1", { 90, 104, 222, 16 }, 9);
        drawText (g, processor.getLfoShape (index) == 4 ? "SAVED CUSTOM CURVE" : "FULL CURVE / POINTS + INTERPOLATION",
                  { 336, 104, 564, 16 }, 9, accent);
    }
private:
    void updateCanvasTools()
    {
        if (! canOperate()) return;
        const juce::Component::SafePointer<LfoDrawingTools> safe (this);
        canvas.setViewTools (editMode.getSelectedId() - 1, gridX.getSelectedId(), gridY.getSelectedId(), snapX.getToggleState(), snapY.getToggleState());
        if (safe != nullptr) repaint();
    }
    void refreshPoint (bool force)
    {
        const int point = juce::jlimit (0, 32, pointIndex.getSelectedId() - 1);
        const float n = canvas.readCurve().points[(size_t) point];
        if (force || lastPointIndex != point || n != lastPointValue)
        {
            pointValue.setValue (n, juce::dontSendNotification); lastPointIndex = point; lastPointValue = n;
        }
    }
    void message (const juce::String& text) { status.setText (text, juce::dontSendNotification); status.setTooltip (text); }
    std::function<bool()> currentGuard()
    {
        const juce::Component::SafePointer<LfoDrawingTools> safe (this); const auto serial = requestSerial;
        return [safe, serial] { return safe != nullptr && safe->requestSerial == serial && safe->canOperate(); };
    }
    void applyCurve (const anyvoc::LfoCurveData& data, const juce::String& label, bool replaceAsset = false)
    {
        if (! canOperate()) return;
        const juce::Component::SafePointer<LfoDrawingTools> safe (this);
        const bool unchanged = ! canvas.dataWouldChange (data, replaceAsset);
        const bool applied = canvas.applyData (data, label, replaceAsset, currentGuard());
        if (safe == nullptr) return;
        message (applied ? unchanged ? "Curve unchanged; no undo step added." : "Curve applied. UNDO restores the previous sound." : "Edit cancelled because the sound or view changed.");
        refreshPoint (true);
    }
    void flip (bool vertical)
    {
        if (! canOperate()) return;
        auto data = canvas.readCurve();
        const bool valid = vertical ? anyvoc::LfoCurveDocument::flipVertical (data, canvas.firstSelected(), canvas.lastSelected())
                                    : anyvoc::LfoCurveDocument::flipHorizontal (data, canvas.firstSelected(), canvas.lastSelected());
        if (valid) applyCurve (data, vertical ? "Flip LFO curve vertically" : "Flip LFO curve horizontally");
    }
    void copyCurve()
    {
        if (! canOperate()) return;
        const juce::Component::SafePointer<LfoDrawingTools> safe (this); auto service = services;
        const auto text = anyvoc::LfoCurveDocument::serialize (canvas.readCurve());
        if (text.isEmpty()) { message ("Unable to copy this curve."); return; }
        service->writeClipboard (text);
        if (safe != nullptr && canOperate()) message ("Copied all 33 points and interpolation. Paste into another LFO or instance.");
    }
    void pasteCurve()
    {
        if (! canOperate()) return;
        const juce::Component::SafePointer<LfoDrawingTools> safe (this); auto service = services;
        const LfoCurveSnapshot snapshot (processor, prefix); const auto current = currentGuard();
        const auto text = service->readClipboard();
        if (safe == nullptr || ! current() || ! snapshot.valid()) return;
        anyvoc::LfoCurveData data; const auto result = anyvoc::LfoCurveDocument::parse (text, data);
        if (result.failed()) { message ("Cannot paste curve: " + result.getErrorMessage()); return; }
        if (current() && snapshot.valid()) applyCurve (data, "Paste LFO curve", true);
    }
    void chooseFile (bool saving)
    {
        if (! canOperate()) return;
        invalidatePending(); const auto current = currentGuard();
        const juce::Component::SafePointer<LfoDrawingTools> safe (this); auto service = services;
        auto snapshot = std::make_shared<LfoCurveSnapshot> (processor, prefix); const auto data = canvas.readCurve();
        auto replied = std::make_shared<bool> (false);
        service->chooseCurveFile (saving, [safe, current, snapshot, data, saving, replied] (juce::File file)
        {
            if (*replied) return;
            *replied = true;
            if (safe == nullptr || ! current() || ! snapshot->valid()) return;
            if (file == juce::File()) { safe->message ("Curve file selection cancelled."); return; }
            if (saving)
            {
                if (file.getFileExtension().isEmpty()) file = file.withFileExtension ("anycurve");
                const auto result = anyvoc::LfoCurveDocument::saveFile (file, data);
                if (safe != nullptr) safe->message (result.wasOk() ? "Curve saved: " + file.getFileName() : "Cannot save curve: " + result.getErrorMessage());
                return;
            }
            anyvoc::LfoCurveData loaded; const auto result = anyvoc::LfoCurveDocument::loadFile (file, loaded);
            if (safe == nullptr || ! current() || ! snapshot->valid()) return;
            if (result.failed()) { safe->message ("Cannot load curve: " + result.getErrorMessage()); return; }
            safe->applyCurve (loaded, "Load LFO curve", true);
        });
    }
    AnyvocAudioProcessor& processor;
    int index;
    juce::String prefix;
    LfoCurveEditor& canvas;
    juce::Colour accent;
    std::shared_ptr<LfoCurveServices> services;
    juce::ComboBox editMode, gridX, gridY, pointIndex;
    juce::ToggleButton snapX, snapY;
    juce::Slider offsetX, offsetY, scaleX, scaleY, pointValue;
    juce::TextButton selectAll, flipX, flipY, apply, setPoint, copy, paste, load, save;
    juce::Label status;
    int lastPointIndex = -1;
    float lastPointValue = 0;
    std::uint64_t requestSerial = 0;
};

class LfoDetail final : public juce::Component
{
public:
    LfoDetail (AnyvocAudioProcessor& p, int indexIn, juce::Colour colour, std::shared_ptr<LfoCurveServices> services)
        : processor (p), index (indexIn), prefix ("lfo" + juce::String (indexIn + 1)), accent (colour),
          canvas (p, indexIn, colour), drawingTools (p, indexIn, canvas, colour, std::move (services)), rateMode (p, indexIn),
          phase (p, prefix + "Phase", "START PHASE", "Reading offset in a repeating LFO cycle. 360 degrees wraps to zero. Phase and its modulation have no effect on Random, Lorenz or Rossler.", Unit::degrees, colour),
          swing (p, "swingAmount", "GLOBAL SWING", "Shared timing feel for all tempo-synchronized LFOs with FOLLOW SWING enabled. 0% is straight 50:50; 100% is 75:25.", Unit::percent, colour),
          division (p, prefix + "Division", "BEAT DIVISION", lfoDivisions, "Rate relative to the current tempo. T is triplet; D is dotted. For chaos, the beat division controls evolution speed, not a repeating cycle.",
                    [&p, indexIn] { return p.getLfoRateMode (indexIn) == 1; }),
          mode (p, prefix + "Mode", "PLAY MODE", { "Free", "Trigger", "Envelope" }, "Free runs continuously. Trigger restarts from a note. Envelope plays once; chaos evolves for one nominal rate period, then holds its final state."),
          direction (p, prefix + "Direction", "DIRECTION", { "Forward", "Reverse", "Ping Pong" }, "Read the curve forward, backward, or out and back in one cycle. Random, Lorenz and Rossler have no repeating shape, so direction does not apply.",
                     [&p, indexIn] { return lfoHasPhase (p.getLfoShape (indexIn)); }),
          swingGrid (p, "swingDivision", "SWING GRID", { "1/8", "1/16" }, "The shared swing grid for all LFOs that follow swing."),
          interpolation (p, prefix + "Interpolation", "CUSTOM CURVE", { "Linear", "Step" }, "Linear joins drawn points with ramps. Step holds each point until the next one.",
                         [&p, indexIn] { return p.getLfoShape (indexIn) == 5; }),
          shape (p, indexIn)
    {
        setBounds (0, 0, designWidth, designHeight);
        setComponentID ("LfoDetail" + juce::String (index + 1));
        addAndMakeVisible (selector);
        for (int i = 0; i < 10; ++i) selector.addItem ("LFO " + juce::String (i + 1), i + 1);
        selector.setComponentID ("LFOLabSelector" + juce::String (index + 1)); selector.setTitle ("Choose an LFO to edit");
        selector.setBounds (344, 131, 150, 26); selector.setSelectedItemIndex (index, juce::dontSendNotification);
        selector.onChange = [this]
        {
            if (! matrixHierarchyVisible (this)) return;
            const int selected = selector.getSelectedItemIndex(); const auto select = onSelectLfo;
            if (selected != index && select) select (selected);
        };
        addAndMakeVisible (closeButton); closeButton.setButtonText ("DONE"); closeButton.setBounds (982, 131, 94, 28);
        closeButton.setComponentID ("LfoDetailClose" + juce::String (index + 1));
        closeButton.onClick = [this] { close(); };
        addAndMakeVisible (canvas); canvas.setBounds (52, 174, 1016, 176);
        canvas.onEdited = [this] { shape.refresh(); refresh(); };
        const juce::StringArray names { "SINE", "RAMP", "TRIANGLE", "PULSES", "CLEAR" };
        for (int i = 0; i < 5; ++i)
        {
            auto& button = curveButtons[(size_t) i]; addAndMakeVisible (button);
            button.setButtonText (names[i]); button.setBounds (52 + i * 90, 361, 80, 26);
            button.setComponentID (prefix + "CurvePreset" + juce::String (i));
            button.onClick = [this, i] { canvas.applyCurve (i); };
            button.setTooltip ("Replace the custom curve with " + names[i].toLowerCase() + ". You can draw over it immediately.");
        }
        addAndMakeVisible (toolsButton); toolsButton.setButtonText ("TOOLS");
        toolsButton.setComponentID (prefix + "Tools"); toolsButton.setBounds (962, 361, 106, 26);
        toolsButton.setTooltip ("Switch the lower panel between playback controls and curve tools. The graph and keyboard remain available.");
        toolsButton.onClick = [this]
        {
            if (! matrixHierarchyVisible (this) || ! finishDrawing() || ! matrixHierarchyVisible (this)) return;
            toolsOpen = ! toolsOpen; drawingTools.invalidatePending(); refresh();
        };
        addChildComponent (drawingTools); drawingTools.setBounds (52, 399, 1016, 167);
        addAndMakeVisible (rateMode); rateMode.setBounds (52, 399, 184, 47);
        const auto setupRate = [this] (SoundSlider& slider, const juce::String& suffix, int requiredMode)
        {
            addAndMakeVisible (slider); slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 76, 26);
            styleSliderText (slider); slider.setScrollWheelEnabled (false); slider.setBounds (260, 418, 184, 28);
            slider.setTitle (requiredMode == 2 ? "LFO audio rate" : "LFO free rate");
            slider.setTooltip (requiredMode == 2 ? "Independent speed from 20 to 1000. Periodic shapes use Hz; chaos uses this to scale evolution speed, not cycles. Saved separately from the slow rate."
                                               : "The original slow LFO rate. Periodic shapes use Hz; chaos uses evolution speed, not cycles. Tempo and Audio Rate keep this value for later.");
            auto attachment = std::make_unique<SoundSliderAttachment> (processor, prefix + suffix, slider);
            slider.textFromValueFunction = [owner = &processor, lfo = index] (double n)
            {
                return lfoIsChaos (owner->getLfoShape (lfo)) ? juce::String (n, n < 10.0 ? 2 : 1) : formatted (n, Unit::hertz);
            };
            slider.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue() * (text.containsIgnoreCase ("k") ? 1000 : 1); };
            slider.userEditAllowed = [owner = &processor, lfo = index, requiredMode] { return owner->getLfoRateMode (lfo) == requiredMode; };
            slider.updateText(); return attachment;
        };
        rateAttachment = setupRate (rate, "Rate", 0); audioRateAttachment = setupRate (audioRate, "AudioRateHz", 2);
        // Preserve the old Lab Sync component ID for host/UI integrations. The
        // visible rate-mode selector now presents all three mutually exclusive modes.
        addChildComponent (sync); sync.setButtonText ("SYNC");
        syncAttachment = std::make_unique<SoundButtonAttachment> (processor, prefix + "Sync", sync);
        addAndMakeVisible (division); division.setBounds (260, 399, 184, 47);
        addAndMakeVisible (mode); mode.setBounds (468, 399, 184, 47);
        addAndMakeVisible (direction); direction.setBounds (676, 399, 184, 47);
        addAndMakeVisible (shape); shape.setBounds (884, 399, 184, 47);
        addAndMakeVisible (phase); phase.setBounds (80, 458, 128, 84);
        phase.slider.userEditAllowed = [owner = &processor, lfo = index] { return lfoHasPhase (owner->getLfoShape (lfo)); };
        addAndMakeVisible (followSwing); followSwing.setButtonText ("FOLLOW SWING"); followSwing.setBounds (260, 482, 184, 27);
        followSwing.setTooltip ("Follow the shared swing amount and grid while RATE MODE is Tempo. The saved setting is kept in the other rate modes.");
        followSwingAttachment = std::make_unique<SoundButtonAttachment> (processor, prefix + "FollowSwing", followSwing,
                                                                       [owner = &processor, lfo = index] { return owner->getLfoRateMode (lfo) == 1; });
        addAndMakeVisible (swing); swing.setBounds (496, 458, 128, 84);
        addAndMakeVisible (swingGrid); swingGrid.setBounds (676, 467, 184, 47);
        addAndMakeVisible (interpolation); interpolation.setBounds (884, 467, 184, 47);
        refresh();
    }
    bool finishDrawing()
    {
        const juce::Component::SafePointer<LfoDetail> safe (this);
        canvas.endStroke();
        return safe != nullptr;
    }
    void close() { if (finishDrawing()) { drawingTools.invalidatePending(); setVisible (false); } }
    void visibilityChanged() override { if (! matrixHierarchyVisible (this)) drawingTools.invalidatePending(); }
    bool refresh()
    {
        const juce::Component::SafePointer<LfoDetail> safe (this);
        const int speedMode = processor.getLfoRateMode (index);
        rate.setVisible (! toolsOpen && speedMode == 0); if (safe == nullptr) return false;
        audioRate.setVisible (! toolsOpen && speedMode == 2); if (safe == nullptr) return false;
        division.setVisible (! toolsOpen && speedMode == 1); if (safe == nullptr) return false;
        const std::array<juce::Component*, 9> playbackControls { &rateMode, &mode, &direction, &shape, &phase, &followSwing, &swing, &swingGrid, &interpolation };
        for (auto* control : playbackControls) { control->setVisible (! toolsOpen); if (safe == nullptr) return false; }
        drawingTools.setVisible (toolsOpen); if (safe == nullptr) return false;
        canvas.setToolsVisible (toolsOpen);
        toolsButton.setButtonText (toolsOpen ? "CONTROLS" : "TOOLS");
        const int currentShape = processor.getLfoShape (index);
        const bool periodic = lfoHasPhase (currentShape), chaos = lfoIsChaos (currentShape);
        phase.setEnabled (periodic); if (safe == nullptr) return false;
        phase.setAlpha (periodic ? 1.0f : .32f);
        direction.setEnabled (periodic); if (safe == nullptr) return false;
        direction.setAlpha (periodic ? 1.0f : .32f);
        followSwing.setEnabled (speedMode == 1); if (safe == nullptr) return false;
        followSwing.setAlpha (speedMode == 1 ? 1.0f : .32f);
        const bool custom = processor.getLfoShape (index) == 5;
        interpolation.setEnabled (custom); if (safe == nullptr) return false;
        interpolation.setAlpha (custom ? 1.0f : .32f);
        canvas.setEnabled (! chaos); if (safe == nullptr) return false;
        if (chaos) { canvas.endStroke(); if (safe == nullptr) return false; }
        for (auto& button : curveButtons)
        {
            button.setEnabled (! chaos); if (safe == nullptr) return false;
            button.setAlpha (chaos ? .32f : 1.0f);
        }
        rate.updateText(); if (safe == nullptr) return false;
        audioRate.updateText(); if (safe == nullptr) return false;
        rateMode.refresh(); shape.refresh(); canvas.refresh(); drawingTools.refresh(); repaint();
        selector.setSelectedItemIndex (index, juce::dontSendNotification);
        return true;
    }
    Knob& phaseControl() { return phase; }
    bool hitTest (int x, int y) override { return juce::Rectangle<int> (24, 117, 1076, 461).contains (x, y); }
    void paint (juce::Graphics& g) override
    {
        drawCard (g, { 24, 117, 1076, 461 }, "LFO LAB", "", accent);
        drawText (g, (processor.isTempoFromHost() ? "HOST  " : "LOCAL  ") + juce::String (processor.getTempo(), 1) + " BPM",
                  { 788, 132, 174, 23 }, 10, accent, true, juce::Justification::centredRight);
        const auto currentShape = processor.getLfoShape (index);
        const bool chaos = lfoIsChaos (currentShape);
        drawText (g, chaos ? "EVOLVING CHAOS / REAL ENGINE TRAJECTORY / SPEED IS NOT A REPEAT FREQUENCY"
                          : "DRAW ACROSS THE GRAPH TO CREATE YOUR OWN MOVEMENT", { 52, 156, 1016, 15 }, 9, secondaryText, true);
        drawText (g, chaos ? "DRAWING OFF  /  SAVED CUSTOM POINTS ARE KEPT"
                          : "33 POINTS  /  DASHED: START  /  WHITE: LIVE READ", { 516, 361, 432, 26 }, 8.5f, secondaryText, false, juce::Justification::centredRight);
        if (toolsOpen) return;
        const int speedMode = processor.getLfoRateMode (index);
        if (speedMode != 1) drawText (g, chaos ? "EVOLUTION SPEED" : speedMode == 2 ? "AUDIO RATE / HZ" : "RATE / HZ", { 260, 399, 184, 16 }, 10.5f);
        const bool following = speedMode == 1 && value (processor, prefix + "FollowSwing") > .5f;
        drawText (g, following ? "SHARED TIMING ACTIVE" : speedMode == 1 ? "STRAIGHT TIMING" : "TEMPO MODE ONLY",
                  { 260, 516, 184, 15 }, 8, following ? accent : secondaryText);
        const float swingPercent = value (processor, "swingAmount") * 25.0f;
        drawText (g, juce::String (50.0f + swingPercent, 1) + " : " + juce::String (50.0f - swingPercent, 1),
                  { 676, 519, 184, 15 }, 9, accent, false, juce::Justification::centred);
        if (chaos)
            drawText (g, "PHASE / DIRECTION: N/A. Trigger resets chaos; Envelope evolves for one nominal period, then holds.",
                      { 52, 544, 1016, 22 }, 10, primaryText);
        else if (currentShape == 4)
            drawText (g, "PHASE / DIRECTION: N/A FOR RANDOM", { 52, 544, 1016, 22 }, 10, primaryText);
        else
        {
            const int playMode = juce::jlimit (0, 2, (int) value (processor, prefix + "Mode"));
            const juce::StringArray tips {
                "FREE runs continuously. Drag a source onto START PHASE for phase modulation; shift-drag knobs for fine adjustment.",
                "TRIGGER restarts on each note. START PHASE and DIRECTION choose where and how the curve is read.",
                "ENVELOPE plays one cycle per note and holds the final reading. PING PONG travels out and back in that cycle."
            };
            drawText (g, tips[playMode], { 52, 544, 1016, 22 }, 10.5f, primaryText);
        }
    }
    std::function<void (int)> onSelectLfo;
private:
    AnyvocAudioProcessor& processor;
    int index;
    juce::String prefix;
    juce::Colour accent;
    LfoCurveEditor canvas;
    LfoDrawingTools drawingTools;
    LfoRateModeChoice rateMode;
    Knob phase, swing;
    SoundSlider rate, audioRate;
    Choice division, mode, direction, swingGrid, interpolation;
    LfoShapeChoice shape;
    juce::TextButton closeButton, toolsButton;
    bool toolsOpen = false;
    juce::ComboBox selector;
    std::array<juce::TextButton, 5> curveButtons;
    juce::ToggleButton sync, followSwing;
    std::unique_ptr<SoundSliderAttachment> rateAttachment, audioRateAttachment;
    std::unique_ptr<SoundButtonAttachment> syncAttachment, followSwingAttachment;
};

class EffectDisplay final : public juce::Component
{
public:
    EffectDisplay (AnyvocAudioProcessor& p, int indexIn, juce::Colour colour)
        : processor (p), index (indexIn), accent (colour) {}
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (insetColour); g.fillRoundedRectangle (r, 7);
        auto plot = r.reduced (12);
        g.setColour (borderColour);
        g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());
        juce::Path path;
        if (index < 2)
        {
            for (int line = 0; line < (index == 0 ? 1 : 3); ++line)
            {
                path.clear();
                for (int i = 0; i <= 120; ++i)
                {
                    const auto t = (float) i / 120;
                    const float v = index == 0 ? std::tanh ((t * 2 - 1) * (1 + value (processor, "drive") * 8))
                                               : std::sin ((t * 2 + (float) line * 0.13f) * juce::MathConstants<float>::twoPi);
                    const auto x = plot.getX() + t * plot.getWidth(), y = plot.getCentreY() - v * plot.getHeight() * 0.42f;
                    if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
                }
                g.setColour (accent.withAlpha (1.0f - (float) line * 0.3f));
                g.strokePath (path, juce::PathStrokeType (1.6f));
            }
        }
        else
        {
            const int bars = index == 2 ? 9 : 34;
            for (int i = 0; i < bars; ++i)
            {
                const auto t = (float) i / (float) bars;
                const auto decay = index == 2 ? value (processor, "delayFeedback") : value (processor, "reverbSize");
                const auto amplitude = std::exp (-t * (5.5f - decay * 4)) * plot.getHeight() * 0.42f;
                const auto x = plot.getX() + 4 + t * plot.getWidth();
                g.setColour (accent.withAlpha (0.9f - t * 0.6f));
                g.drawLine (x, plot.getCentreY() - amplitude, x, plot.getCentreY() + amplitude, index == 2 ? 3.0f : 1.4f);
            }
        }
    }
private:
    AnyvocAudioProcessor& processor;
    int index;
    juce::Colour accent;
};

enum class HarmonicChange { failed, unchanged, changed };

HarmonicChange applyHarmonicsIfChanged (anyvoc::WavetableDocument& document, int frame,
                             const std::vector<anyvoc::WavetableDocument::HarmonicEdit>& edits)
{
    const auto* samples = document.frameData (frame);
    if (samples == nullptr) return HarmonicChange::failed;
    std::array<float, anyvoc::WavetableDocument::frameLength> before{};
    std::copy_n (samples, before.size(), before.begin());
    if (! document.setHarmonics (frame, edits)) return HarmonicChange::failed;
    // Successful silent/no-op commands must not republish a new audio asset.
    return std::equal (before.begin(), before.end(), document.frameData (frame)) ? HarmonicChange::unchanged : HarmonicChange::changed;
}

class WavetableCanvas final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int harmonicsPerPage = 64;
    static constexpr int pageCount = (anyvoc::WavetableDocument::maximumHarmonics + harmonicsPerPage - 1) / harmonicsPerPage;

    WavetableCanvas (anyvoc::WavetableDocument& model, juce::Colour colour)
        : document (model), accent (colour)
    {
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        setWantsKeyboardFocus (true);
        updateTooltip();
    }
    ~WavetableCanvas() override { cancelStroke(); }
    void setFrame (int index) { finishStroke(); frame = juce::jlimit (0, document.frameCount() - 1, index); refresh(); }
    void setView (int next) { finishStroke(); view = juce::jlimit (0, 2, next); hovered = -1; updateTooltip(); refresh(); }
    int getView() const { return view; }
    void setHarmonicPage (int next) { finishStroke(); page = juce::jlimit (0, pageCount - 1, next); hovered = -1; refresh(); }
    int getHarmonicPage() const { return page; }
    int firstHarmonic() const { return page * harmonicsPerPage + 1; }
    int lastHarmonic() const { return juce::jmin (anyvoc::WavetableDocument::maximumHarmonics, firstHarmonic() + harmonicsPerPage - 1); }
    juce::String pageRange() const { return juce::String (firstHarmonic()) + "-" + juce::String (lastHarmonic()); }
    void refresh() { spectrum = document.harmonicSpectrum (frame); repaint(); }

    void finishStroke()
    {
        if (! drawing) return;
        bool rejected = false;
        if (view != 0)
        {
            std::vector<anyvoc::WavetableDocument::HarmonicEdit> edits;
            edits.reserve (harmonicsPerPage);
            for (int i = 0; i < visibleHarmonics(); ++i)
                if (changedHarmonics[(size_t) i])
                {
                    const int harmonic = firstHarmonic() + i;
                    edits.push_back ({ harmonic, spectrum.amplitudes[(size_t) harmonic - 1], spectrum.phases[(size_t) harmonic - 1], view == 1, view == 2 });
                }
            // A whole stroke is one FFT round trip and one local/global edit.
            const auto result = applyHarmonicsIfChanged (document, frame, edits);
            changed = result == HarmonicChange::changed;
            rejected = result == HarmonicChange::failed;
        }
        document.endEdit(); drawing = false;
        const bool edited = changed; changed = false;
        refresh();
        if (edited && onEdited) onEdited();
        if (onStrokeEnd) onStrokeEnd();
        if (rejected && onRejected) onRejected();
    }

    void cancelStroke()
    {
        const bool wasDrawing = drawing;
        if (drawing) document.endEdit();
        drawing = false; changed = false;
        changedHarmonics.fill (false);
        if (wasDrawing && onStrokeEnd) onStrokeEnd();
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu()) return;
        finishStroke(); grabKeyboardFocus();
        if (onStrokeBegin) onStrokeBegin();
        document.beginEdit(); drawing = true; changed = false;
        changedHarmonics.fill (false);
        lastIndex = indexAt (event.position.x); lastValue = valueAt (event.position.y);
        if (view != 0) setSpectralValue (lastIndex, lastValue);
        else changed = document.draw (frame, lastIndex, lastValue, lastIndex, lastValue);
        hovered = lastIndex; repaint();
    }
    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! drawing) return;
        const int next = indexAt (event.position.x);
        const float nextValue = valueAt (event.position.y);
        if (view != 0)
        {
            const int low = juce::jmin (lastIndex, next), high = juce::jmax (lastIndex, next);
            for (int i = low; i <= high; ++i)
            {
                const float blend = next == lastIndex ? 1.0f : (float) (i - lastIndex) / (float) (next - lastIndex);
                setSpectralValue (i, lastValue + (nextValue - lastValue) * blend);
            }
        }
        else changed = document.draw (frame, lastIndex, lastValue, next, nextValue) || changed;
        lastIndex = next; lastValue = nextValue; hovered = next; repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { finishStroke(); }
    void focusLost (FocusChangeType) override { finishStroke(); }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey) { finishStroke(); return true; }
        return false;
    }
    void mouseMove (const juce::MouseEvent& event) override
    {
        const int point = indexAt (event.position.x);
        if (point != hovered) { hovered = point; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto r = plotBounds();
        g.setColour (insetColour); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8);
        g.setColour (borderColour); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8, 1);
        for (int i = 0; i <= 8; ++i)
        {
            g.setColour (borderColour.withAlpha (0.6f));
            g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * (float) i / 8), r.getY(), r.getBottom());
        }
        for (int i = 0; i <= 4; ++i)
        {
            g.setColour (borderColour.withAlpha (i == 2 ? 1.0f : 0.5f));
            g.drawHorizontalLine (juce::roundToInt (r.getY() + r.getHeight() * (float) i / 4), r.getX(), r.getRight());
        }
        const auto degree = juce::String::charToString (0x00b0);
        drawText (g, view == 0 ? "+1" : view == 1 ? "100%" : "+180" + degree, { 3, 11, view == 0 ? 25 : 40, 14 }, 8.5f, secondaryText, false, juce::Justification::centredRight);
        drawText (g, view == 0 ? "-1" : view == 1 ? "0%" : "-180" + degree, { 3, getHeight() - 33, view == 0 ? 25 : 40, 14 }, 8.5f, secondaryText, false, juce::Justification::centredRight);
        if (view != 0)
        {
            const int count = visibleHarmonics();
            const float width = r.getWidth() / (float) count;
            if (view == 2)
            {
                drawText (g, "0" + degree, { 3, juce::roundToInt (r.getCentreY()) - 7, 40, 14 }, 8.5f, secondaryText, false, juce::Justification::centredRight);
                g.setColour (accent.withAlpha (0.3f)); g.drawHorizontalLine (juce::roundToInt (r.getCentreY()), r.getX(), r.getRight());
            }
            for (int i = 0; i < count; ++i)
            {
                const auto index = (size_t) (firstHarmonic() + i - 1);
                const float amplitude = spectrum.amplitudes[index];
                const bool silent = amplitude <= 1.0e-6f;
                const float x = r.getX() + ((float) i + 0.5f) * width;
                if (view == 1)
                {
                    const float height = juce::jlimit (0.0f, 1.0f, amplitude) * r.getHeight();
                    g.setColour (i == hovered ? primaryText : silent ? secondaryText.withAlpha (0.24f) : accent.withAlpha (i % 2 == 0 ? 0.95f : 0.6f));
                    g.fillRoundedRectangle (x - width * 0.5f + 1, r.getBottom() - juce::jmax (2.0f, height), width - 2, juce::jmax (2.0f, height), 1);
                }
                else
                {
                    const float y = r.getCentreY() - spectrum.phases[index] * r.getHeight();
                    g.setColour (i == hovered ? primaryText.withAlpha (silent ? 0.5f : 1.0f) : silent ? secondaryText.withAlpha (0.2f) : accent.withAlpha (0.8f));
                    g.drawLine (x, r.getCentreY(), x, y, 1.5f);
                    g.fillEllipse (x - 2.1f, y - 2.1f, 4.2f, 4.2f);
                }
            }
            for (int local : { 0, 8, 16, 24, 32, 40, 48, 56, count - 1 })
                drawText (g, juce::String (firstHarmonic() + local), { juce::roundToInt (r.getX() + ((float) local + 0.5f) * width) - 15, getHeight() - 17, 30, 14 }, 8, secondaryText, false, juce::Justification::centred);
            drawText (g, "HARMONICS " + pageRange() + "  /  1023 MAX", { 48, 1, 285, 14 }, 8.5f, accent, true);
            if (hovered >= 0)
            {
                const auto index = (size_t) (firstHarmonic() + hovered - 1);
                const auto level = spectrum.amplitudes[index];
                const auto phase = juce::String (spectrum.phases[index] * 360.0f, 1) + degree;
                drawText (g, "H" + juce::String (index + 1) + "   " + juce::String (level * 100.0f, 2) + "%   " + phase + (level <= 1.0e-6f ? "  / SILENT" : ""),
                          { 342, 1, getWidth() - 360, 14 }, 9, primaryText, false, juce::Justification::centredRight);
            }
            else drawText (g, view == 1 ? "AMPLITUDE  /  HOVER FOR VALUES" : "PHASE  /  DIM POINTS ARE SILENT", { 342, 1, getWidth() - 360, 14 }, 8.5f, secondaryText, false, juce::Justification::centredRight);
        }
        else if (const auto* samples = document.frameData (frame))
        {
            juce::Path wave; float peak = 0;
            for (int i = 0; i < anyvoc::WavetableDocument::frameLength; ++i)
            {
                const auto sample = samples[i]; peak = juce::jmax (peak, std::abs (sample));
                const float x = r.getX() + (float) i / (anyvoc::WavetableDocument::frameLength - 1) * r.getWidth();
                const float y = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, sample) * r.getHeight() * 0.5f;
                if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
            }
            g.setColour (accent.withAlpha (0.12f)); g.strokePath (wave, juce::PathStrokeType (6));
            g.setColour (accent); g.strokePath (wave, juce::PathStrokeType (1.8f));
            if (peak > 1.001f) drawText (g, "PEAK > 1  /  TRY NORMALIZE", { getWidth() - 240, 1, 222, 14 }, 9, orange, true, juce::Justification::centredRight);
        }
    }
    std::function<void()> onEdited, onStrokeBegin, onStrokeEnd, onRejected;
private:
    int visibleHarmonics() const { return lastHarmonic() - firstHarmonic() + 1; }
    void setSpectralValue (int local, float n)
    {
        const auto index = (size_t) (firstHarmonic() + local - 1);
        if (view == 1) spectrum.amplitudes[index] = n;
        else spectrum.phases[index] = n;
        changedHarmonics[(size_t) local] = true;
    }
    void updateTooltip()
    {
        setTooltip (view == 0 ? "Draw one waveform cycle. Release the mouse to apply the stroke as one undoable sound edit."
                   : view == 1 ? "Paint harmonic strength from 0% to 100%. Each page shows up to 64 of the 1023 harmonics. Existing phases are preserved. One stroke applies all painted bars together."
                               : "Paint sine-relative phase from -180 to +180 degrees; both endpoints describe the same angle. Amplitudes are preserved. A silent harmonic stays silent: use HARMONICS to give it strength first.");
    }
    juce::Rectangle<float> plotBounds() const
    {
        return view == 0 ? getLocalBounds().toFloat().withTrimmedLeft (31).withTrimmedRight (14).reduced (0, 18)
                         : getLocalBounds().toFloat().withTrimmedLeft (48).withTrimmedRight (14).withTrimmedTop (22).withTrimmedBottom (20);
    }
    int indexAt (float x) const
    {
        const float position = juce::jlimit (0.0f, 1.0f, (x - plotBounds().getX()) / plotBounds().getWidth());
        return view != 0 ? juce::jmin (visibleHarmonics() - 1, (int) (position * (float) visibleHarmonics())) : juce::roundToInt (position * (anyvoc::WavetableDocument::frameLength - 1));
    }
    float valueAt (float y) const
    {
        const float proportion = juce::jlimit (0.0f, 1.0f, 1.0f - (y - plotBounds().getY()) / plotBounds().getHeight());
        return view == 0 ? proportion * 2.0f - 1.0f : view == 1 ? proportion : proportion - 0.5f;
    }
    anyvoc::WavetableDocument& document;
    juce::Colour accent;
    int frame = 0, lastIndex = 0, hovered = -1, view = 0, page = 0;
    float lastValue = 0;
    bool drawing = false, changed = false;
    anyvoc::WavetableDocument::HarmonicSpectrum spectrum;
    std::array<bool, harmonicsPerPage> changedHarmonics{};
};

class WavetableFrameStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    WavetableFrameStrip (anyvoc::WavetableDocument& model, juce::Colour colour) : document (model), accent (colour)
    {
        setTitle ("Wavetable frames");
        setTooltip ("Select a frame to edit and audition. Selecting also moves the oscillator WAVE position to this frame.");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void setSelected (int index) { selected = index; repaint(); }
    void mouseDown (const juce::MouseEvent& event) override
    {
        const int frame = juce::jlimit (0, document.frameCount() - 1, (int) (event.position.x / (float) getWidth() * (float) document.frameCount()));
        if (onSelect) onSelect (frame);
    }
    void paint (juce::Graphics& g) override
    {
        const int count = document.frameCount();
        const float width = (float) getWidth() / (float) count;
        for (int i = 0; i < count; ++i)
        {
            auto r = juce::Rectangle<float> ((float) i * width, 0, width, (float) getHeight()).reduced (1);
            g.setColour (i == selected ? accent.withAlpha (0.15f) : insetColour); g.fillRoundedRectangle (r, 4);
            g.setColour (i == selected ? accent : borderColour); g.drawRoundedRectangle (r.reduced (0.5f), 4, 1);
            if (width >= 38)
                if (const auto* samples = document.frameData (i))
                {
                    juce::Path line;
                    for (int p = 0; p < 32; ++p)
                    {
                        const float x = r.getX() + 4 + (float) p / 31 * (r.getWidth() - 8);
                        const float y = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, samples[p * 64]) * 7;
                        if (p == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
                    }
                    g.setColour (accent.withAlpha (i == selected ? 0.7f : 0.25f)); g.strokePath (line, juce::PathStrokeType (1));
                }
            drawText (g, juce::String (i + 1), r.toNearestInt(), 10, i == selected ? primaryText : secondaryText, true, juce::Justification::centred);
        }
    }
    std::function<void (int)> onSelect;
private:
    anyvoc::WavetableDocument& document;
    juce::Colour accent;
    int selected = 0;
};

class WavetableDetail final : public juce::Component
{
public:
    WavetableDetail (AnyvocAudioProcessor& p, int index, juce::Colour colour)
        : processor (p), oscillator (index), accent (colour), canvas (document, colour), frameStrip (document, colour)
    {
        setBounds (0, 0, designWidth, designHeight);
        canvas.setTitle ("Osc " + juce::String::charToString ((juce::juce_wchar) ('A' + index)) + " wavetable drawing canvas");
        canvas.setComponentID ("osc" + juce::String (index + 1) + "WavetableCanvas");
        addAndMakeVisible (canvas); canvas.setBounds (104, 207, 800, 188);
        canvas.onEdited = [this] { dirty = true; applyChanges(); refreshControls(); };
        canvas.onRejected = [this] { showStatus ("This harmonic edit exceeds the table's limits. The sound is unchanged; try Normalize first.", true); };
        canvas.onStrokeBegin = [this]
        {
            processor.beginUserEdit ((canvas.getView() == 0 ? "Draw wavetable " : canvas.getView() == 1 ? "Draw harmonic amplitudes " : "Draw harmonic phases ")
                                    + juce::String::charToString ((juce::juce_wchar) ('A' + oscillator)));
        };
        canvas.onStrokeEnd = [this] { processor.endUserEdit(); };
        addAndMakeVisible (frameStrip); frameStrip.setBounds (104, 404, 800, 28);
        frameStrip.onSelect = [this] (int frame) { selectFrame (frame); };
        setup (done, "DONE", { 936, 134, 80, 29 }, [this] { close(); }, "Close the editor; completed edits are already applied to your sound.");
        setup (waveButton, "WAVE", { 104, 171, 70, 25 }, [this] { setView (0); }, "Draw one complete waveform cycle.");
        setup (harmonicButton, "HARMONICS", { 182, 171, 106, 25 }, [this] { setView (1); }, "Paint harmonic amplitudes in pages of up to 64. All harmonics from 1 to 1023 are editable; phases are preserved.");
        setup (phaseButton, "PHASE", { 296, 171, 74, 25 }, [this] { setView (2); }, "Move the sine-relative phase of each existing harmonic from -180 to +180 degrees. Its amplitude is preserved; silent harmonics stay silent.");
        waveButton.setComponentID ("wavetableViewWave"); harmonicButton.setComponentID ("wavetableViewHarmonics"); phaseButton.setComponentID ("wavetableViewPhase");
        setup (apply, "APPLY", { 744, 171, 96, 25 }, [this] { canvas.finishStroke(); dirty = true; applyChanges(); }, "Commit the current table to the oscillator. Drawing also applies on mouse release.");
        setup (exportButton, "EXPORT WAV", { 856, 171, 160, 25 }, [this] { exportTable(); }, "Export every frame as a standard 2048-sample-per-frame WAV wavetable, preserving its edited waveform and phases.");
        addAndMakeVisible (tableName); tableName.setBounds (386, 171, 342, 25);
        tableName.setEditable (false, true, false);
        tableName.setColour (juce::Label::backgroundColourId, insetColour);
        tableName.setColour (juce::Label::textColourId, primaryText);
        tableName.setColour (juce::Label::outlineColourId, borderColour);
        tableName.setColour (juce::Label::textWhenEditingColourId, primaryText);
        tableName.setColour (juce::Label::backgroundWhenEditingColourId, insetColour);
        tableName.setTooltip ("Double-click to name your edited wavetable. The name is stored with your preset.");
        tableName.onTextChange = [this] { if (! loading) { ScopedSoundEdit edit (processor, "Rename wavetable"); dirty = true; applyChanges(); } };
        addAndMakeVisible (harmonicPage); harmonicPage.setBounds (146, 207, 142, 25);
        harmonicPage.setComponentID ("wavetableHarmonicPage"); harmonicPage.setTitle ("Harmonic page");
        harmonicPage.setTooltip ("Choose a range of harmonics to display. HARMONICS and PHASE share this page. Changing pages finishes any active stroke and does not edit the sound.");
        for (int page = 0; page < WavetableCanvas::pageCount; ++page)
        {
            const int first = page * WavetableCanvas::harmonicsPerPage + 1;
            harmonicPage.addItem (juce::String (first) + "-" + juce::String (juce::jmin (anyvoc::WavetableDocument::maximumHarmonics, first + WavetableCanvas::harmonicsPerPage - 1)), page + 1);
        }
        harmonicPage.setSelectedItemIndex (0, juce::dontSendNotification);
        harmonicPage.onChange = [this] { if (harmonicPage.getSelectedItemIndex() >= 0) setHarmonicPage (harmonicPage.getSelectedItemIndex()); };
        setup (previousHarmonics, "<", { 296, 207, 26, 25 }, [this] { setHarmonicPage (canvas.getHarmonicPage() - 1); }, "Show the previous page of harmonics.");
        setup (nextHarmonics, ">", { 330, 207, 26, 25 }, [this] { setHarmonicPage (canvas.getHarmonicPage() + 1); }, "Show the next page of harmonics, up to harmonic 1023.");
        previousHarmonics.setComponentID ("wavetableHarmonicPrevious"); nextHarmonics.setComponentID ("wavetableHarmonicNext");
        setup (phaseReset, "RESET PAGE PHASE", { 376, 207, 148, 25 }, [this] { changePagePhases (false); }, {});
        setup (phaseRandom, "RANDOM PAGE PHASE", { 536, 207, 156, 25 }, [this] { changePagePhases (true); }, {});
        phaseReset.setComponentID ("wavetablePhaseReset"); phaseRandom.setComponentID ("wavetablePhaseRandom");
        const juce::StringArray processNames { "NORMALIZE", "REMOVE DC", "INVERT", "SMOOTH" };
        const juce::StringArray processTips { "Scale this frame so its loudest peak reaches full level.", "Remove the average DC offset from this frame.", "Flip the waveform's polarity.", "Soften sharp edges with a periodic smoothing pass." };
        for (int i = 0; i < 4; ++i)
            setup (processButtons[(size_t) i], processNames[i], { 920, 207 + i * 40, 96, 30 },
                   [this, i] { canvas.finishStroke(); change (document.processFrame (selectedFrame, i), "Frame processed."); }, processTips[i]);
        setup (previous, "<", { 920, 405, 43, 26 }, [this] { selectFrame (selectedFrame - 1); }, "Previous frame");
        setup (next, ">", { 973, 405, 43, 26 }, [this] { selectFrame (selectedFrame + 1); }, "Next frame");
        const juce::StringArray generateNames { "SINE", "TRIANGLE", "SAW", "SQUARE", "NOISE", "CLEAR" };
        const std::array<int, 6> positions { 104, 174, 264, 334, 414, 484 };
        const std::array<int, 6> widths { 64, 84, 64, 74, 64, 64 };
        for (int i = 0; i < 6; ++i)
            setup (generateButtons[(size_t) i], generateNames[i], { positions[(size_t) i], 444, widths[(size_t) i], 26 },
                   [this, i] { canvas.finishStroke(); change (document.generate (selectedFrame, i), "Generated a new frame."); },
                   "Replace only the selected frame with " + generateNames[i].toLowerCase() + ". Undo is available.");
        setup (duplicate, "DUPLICATE", { 565, 444, 96, 26 }, [this]
        {
            canvas.finishStroke();
            if (document.duplicateFrame (selectedFrame)) { ++selectedFrame; change (true, "Duplicated the frame."); }
            else showStatus ("A table can contain up to 32 frames.", true);
        }, "Insert a copy immediately after this frame. Maximum: 32 frames.");
        setup (remove, "DELETE", { 669, 444, 75, 26 }, [this]
        {
            canvas.finishStroke();
            change (document.deleteFrame (selectedFrame), "Deleted the frame.", "Keep at least one frame in the table.");
        }, "Delete the selected frame. At least one frame must remain.");
        setup (undo, "TABLE UNDO", { 104, 487, 100, 26 }, [this] { canvas.finishStroke(); change (document.undo(), "Undid the last table edit."); }, "Step back within this table's local edit history (up to 20 edits). The header UNDO restores whole sound edits.");
        setup (redo, "TABLE REDO", { 212, 487, 100, 26 }, [this] { canvas.finishStroke(); change (document.redo(), "Restored the next table edit."); }, "Restore a locally undone table edit. This resulting sound change also enters the header history.");
        undo.setComponentID ("tableUndo"); redo.setComponentID ("tableRedo");
        setup (revert, "REVERT", { 320, 487, 76, 26 }, [this] { revertSession(); }, "Restore the table from when this editor was opened. This resets the local table history; the sound change remains globally undoable.");
        addAndMakeVisible (morphFrom); addAndMakeVisible (morphTo);
        morphFrom.setComponentID ("wavetableMorphFrom"); morphTo.setComponentID ("wavetableMorphTo");
        morphFrom.setBounds (445, 487, 50, 26); morphTo.setBounds (530, 487, 50, 26);
        morphFrom.setTooltip ("First endpoint of the morph range."); morphTo.setTooltip ("Last endpoint of the morph range.");
        addAndMakeVisible (morphMode); morphMode.setBounds (592, 487, 140, 26);
        morphMode.setComponentID ("wavetableMorphMode"); morphMode.setTitle ("Wavetable morph method");
        morphMode.addItemList ({ "Waveform", "Spectral" }, 1); morphMode.setSelectedId (1, juce::dontSendNotification);
        morphMode.setTooltip ("Waveform blends corresponding waveform samples. Spectral blends harmonic amplitudes and takes the shortest phase path between endpoint frames. Choosing a mode does not apply it; press MORPH.");
        morphMode.onChange = [this] { canvas.finishStroke(); repaint(); };
        setup (morph, "MORPH", { 744, 487, 80, 26 }, [this]
        {
            canvas.finishStroke();
            const int first = morphFrom.getSelectedItemIndex(), last = morphTo.getSelectedItemIndex();
            if (last - first < 2) { showStatus ("Morph needs an interior frame between its endpoints.", true); return; }
            juce::AudioBuffer<float> before; before.makeCopyOf (document.data());
            const bool success = document.morph (first, last, morphMode.getSelectedItemIndex());
            bool changed = false;
            if (success)
                for (int channel = 0; channel < before.getNumChannels(); ++channel)
                    changed = changed || ! std::equal (before.getReadPointer (channel), before.getReadPointer (channel) + before.getNumSamples(), document.data().getReadPointer (channel));
            if (changed) change (true, morphMode.getSelectedId() == 2 ? "Spectrally morphed the interior frames." : "Morphed the interior waveforms.");
            else showStatus (success ? "The interior frames already match this morph." : "These frames could not be morphed.", ! success);
        }, "Apply the selected Waveform or Spectral method to existing interior frames between FROM and TO. Both endpoints are preserved; duplicate frames first to create more steps.");
        morph.setComponentID ("wavetableMorph");
        setView (0);
    }

    ~WavetableDetail() override { canvas.finishStroke(); chooser.reset(); }

    bool open()
    {
        canvas.finishStroke();
        knownRevision = processor.getWavetableRevision (oscillator);
        original = processor.getEditableWavetable (oscillator);
        originalName = processor.getWavetableName (oscillator);
        if (! document.load (original)) { showStatus ("This wavetable could not be opened.", true); return false; }
        selectedFrame = juce::jlimit (0, document.frameCount() - 1,
                                    juce::roundToInt (value (processor, "osc" + juce::String (oscillator + 1) + "Position") * (float) (document.frameCount() - 1)));
        loading = true;
        tableName.setText (originalName.isEmpty() ? "User Drawn " + juce::String::charToString ((juce::juce_wchar) ('A' + oscillator)) : originalName,
                           juce::dontSendNotification);
        loading = false; dirty = false;
        refreshControls(); setVisible (true); toFront (false);
        showStatus ("Draw, release, and hear your change. Undo is always nearby.");
        return true;
    }

    bool close()
    {
        canvas.finishStroke();
        if (dirty && ! applyChanges()) return false;
        setVisible (false); return true;
    }
    void dismiss() { canvas.cancelStroke(); dirty = false; setVisible (false); }
    bool dismissIfAssetChanged()
    {
        if (! isVisible() || knownRevision == processor.getWavetableRevision (oscillator)) return false;
        dismiss(); return true;
    }

    bool hitTest (int x, int y) override { return juce::Rectangle<int> (24, 117, 1076, 461).contains (x, y); }
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! juce::Rectangle<int> (82, 117, 956, 461).contains (event.getPosition())) close();
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (background.withAlpha (0.86f)); g.fillRoundedRectangle (24, 117, 1076, 461, 12);
        drawCard (g, { 82, 117, 956, 461 }, "OSC " + juce::String::charToString ((juce::juce_wchar) ('A' + oscillator)) + " / WAVETABLE LAB", "", accent);
        drawText (g, "WAVE / AMPLITUDE / PHASE", { 602, 132, 318, 22 }, 10, accent, true, juce::Justification::centredRight);
        drawText (g, "FRAME " + juce::String (selectedFrame + 1) + " / " + juce::String (document.frameCount()), { 915, canvas.getView() == 0 ? 373 : 389, 106, canvas.getView() == 0 ? 22 : 14 }, 10, primaryText, true, juce::Justification::centred);
        if (canvas.getView() != 0)
        {
            drawText (g, "PAGE", { 104, 210, 36, 19 }, 9, secondaryText, true);
            drawText (g, canvas.getView() == 1 ? "AMPLITUDE / Drag bars to set their strength. PHASE moves each partial." : "0% AMPLITUDE STAYS SILENT",
                      canvas.getView() == 1 ? juce::Rectangle<int> (376, 210, 640, 19) : juce::Rectangle<int> (708, 210, 308, 19), 9.5f, secondaryText);
        }
        drawText (g, "FROM", { 408, 490, 34, 20 }, 9, secondaryText, true);
        drawText (g, "TO", { 505, 490, 23, 20 }, 9, secondaryText, true);
        drawText (g, morphMode.getSelectedId() == 2 ? "AMPLITUDE + PHASE" : "WAVEFORM CROSSFADE", { 840, 490, 176, 20 }, 8.5f, secondaryText, true, juce::Justification::centredRight);
        g.setColour (statusError ? orange : secondaryText); g.setFont (font (10.5f));
        g.drawFittedText (status, { 104, 521, 912, 18 }, juce::Justification::centredLeft, 1);
        drawText (g, canvas.getView() == 0 ? "Edits apply after each stroke. TABLE UNDO edits this table; header UNDO restores sound edits and closes a restored table."
                                         : "HARMONICS sets strength; PHASE changes how existing partials line up. A silent partial stays silent in PHASE.",
                  { 104, 543, 912, 22 }, 10.5f, primaryText);
    }
private:
    void setup (juce::TextButton& button, const juce::String& text, juce::Rectangle<int> bounds,
                std::function<void()> action, juce::String tip)
    {
        addAndMakeVisible (button); button.setButtonText (text); button.setBounds (bounds);
        button.onClick = std::move (action); button.setTooltip (tip); button.setWantsKeyboardFocus (false);
    }
    void setView (int view)
    {
        canvas.setView (view);
        waveButton.setToggleState (view == 0, juce::dontSendNotification);
        harmonicButton.setToggleState (view == 1, juce::dontSendNotification);
        phaseButton.setToggleState (view == 2, juce::dontSendNotification);
        canvas.setBounds (104, view == 0 ? 207 : 239, 800, view == 0 ? 188 : 156);
        for (int i = 0; i < 4; ++i) processButtons[(size_t) i].setBounds (920, (view == 0 ? 207 : 239) + i * 40, 96, 30);
        harmonicPage.setVisible (view != 0); previousHarmonics.setVisible (view != 0); nextHarmonics.setVisible (view != 0);
        phaseReset.setVisible (view == 2); phaseRandom.setVisible (view == 2);
        updateHarmonicNavigation(); repaint();
    }
    void setHarmonicPage (int page)
    {
        canvas.setHarmonicPage (page);
        harmonicPage.setSelectedItemIndex (canvas.getHarmonicPage(), juce::dontSendNotification);
        updateHarmonicNavigation(); repaint();
    }
    void updateHarmonicNavigation()
    {
        previousHarmonics.setEnabled (canvas.getHarmonicPage() > 0);
        nextHarmonics.setEnabled (canvas.getHarmonicPage() + 1 < WavetableCanvas::pageCount);
        phaseReset.setTooltip ("Set sine-relative phase to 0 degrees for non-silent harmonics " + canvas.pageRange() + " in the selected frame. Amplitudes and all other harmonics/frames are preserved. Silent harmonics stay silent.");
        phaseRandom.setTooltip ("Randomize phases for non-silent harmonics " + canvas.pageRange() + " in the selected frame. Amplitudes and all other harmonics/frames are preserved. This reshapes the waveform; silent harmonics stay silent.");
    }
    void changePagePhases (bool randomise)
    {
        canvas.finishStroke();
        if (canvas.getView() != 2) return;
        const auto spectrum = document.harmonicSpectrum (selectedFrame);
        std::vector<anyvoc::WavetableDocument::HarmonicEdit> edits;
        for (int harmonic = canvas.firstHarmonic(); harmonic <= canvas.lastHarmonic(); ++harmonic)
            if (spectrum.amplitudes[(size_t) harmonic - 1] > 1.0e-6f)
                edits.push_back ({ harmonic, 0.0f, randomise ? juce::Random::getSystemRandom().nextFloat() - 0.5f : 0.0f, false, true });
        if (edits.empty()) { showStatus ("This page has no non-silent harmonics. Add strength in HARMONICS before editing phase."); return; }
        const auto result = applyHarmonicsIfChanged (document, selectedFrame, edits);
        if (result == HarmonicChange::changed)
            change (true, (randomise ? "Randomized phases for harmonics " : "Reset phases for harmonics ") + canvas.pageRange() + ".");
        else if (result == HarmonicChange::failed)
            showStatus ("These phases exceed the table's limits. The sound is unchanged; try Normalize first.", true);
        else showStatus ("Page phases already match; the sound is unchanged.");
    }
    void selectFrame (int frame)
    {
        canvas.finishStroke(); selectedFrame = juce::jlimit (0, document.frameCount() - 1, frame);
        canvas.setFrame (selectedFrame); frameStrip.setSelected (selectedFrame);
        setValueWithGesture (processor, "osc" + juce::String (oscillator + 1) + "Position",
                             document.frameCount() <= 1 ? 0.0f : (float) selectedFrame / (float) (document.frameCount() - 1));
        previous.setEnabled (selectedFrame > 0); next.setEnabled (selectedFrame + 1 < document.frameCount());
        repaint();
    }
    void refreshControls()
    {
        selectedFrame = juce::jlimit (0, document.frameCount() - 1, selectedFrame);
        canvas.setFrame (selectedFrame); frameStrip.setSelected (selectedFrame);
        undo.setEnabled (document.canUndo()); redo.setEnabled (document.canRedo());
        duplicate.setEnabled (document.frameCount() < anyvoc::WavetableDocument::maximumFrames);
        remove.setEnabled (document.frameCount() > 1);
        previous.setEnabled (selectedFrame > 0); next.setEnabled (selectedFrame + 1 < document.frameCount());
        const int oldCount = morphFrom.getNumItems();
        if (oldCount != document.frameCount())
        {
            const int first = juce::jmax (0, morphFrom.getSelectedItemIndex());
            const int last = morphTo.getSelectedItemIndex();
            morphFrom.clear (juce::dontSendNotification); morphTo.clear (juce::dontSendNotification);
            for (int i = 1; i <= document.frameCount(); ++i) { morphFrom.addItem (juce::String (i), i); morphTo.addItem (juce::String (i), i); }
            morphFrom.setSelectedItemIndex (juce::jmin (first, document.frameCount() - 1), juce::dontSendNotification);
            morphTo.setSelectedItemIndex (last < 0 || last == oldCount - 1 ? document.frameCount() - 1 : juce::jmin (last, document.frameCount() - 1), juce::dontSendNotification);
        }
        morph.setEnabled (document.frameCount() >= 3);
        repaint();
    }
    bool applyChanges()
    {
        if (knownRevision != processor.getWavetableRevision (oscillator))
        {
            showStatus ("The oscillator's table has changed. Reopen EDIT to work on the new table.", true);
            return false;
        }
        const auto name = tableName.getText().trim().isEmpty() ? juce::String ("User Drawn") : tableName.getText().trim();
        if (! processor.applyEditedWavetable (oscillator, document.data(), name))
        {
            showStatus ("Could not apply this table. Your edits remain here; try Normalize or Undo.", true); return false;
        }
        knownRevision = processor.getWavetableRevision (oscillator);
        document.replaceCurrentData (processor.getEditableWavetable (oscillator));
        canvas.refresh(); frameStrip.repaint();
        dirty = false;
        showStatus ("Applied to oscillator " + juce::String::charToString ((juce::juce_wchar) ('A' + oscillator)) + ". Save your preset to keep it.");
        return true;
    }
    void change (bool changed, const juce::String& success, const juce::String& failure = "No change was made.")
    {
        if (! changed) { showStatus (failure, true); return; }
        ScopedSoundEdit edit (processor, "Wavetable: " + success.trimCharactersAtEnd ("."));
        dirty = true;
        refreshControls(); selectFrame (selectedFrame);
        if (applyChanges()) showStatus (success + " Applied to your sound.");
    }
    void revertSession()
    {
        canvas.finishStroke();
        ScopedSoundEdit edit (processor, "Revert wavetable session");
        if (! document.load (original)) { showStatus ("The opening table could not be restored.", true); return; }
        tableName.setText (originalName, juce::dontSendNotification);
        dirty = true; refreshControls();
        if (applyChanges()) showStatus ("Restored the opening table. Local undo history was reset.");
    }
    void exportTable()
    {
        canvas.finishStroke();
        if (dirty && ! applyChanges()) return;
        const auto file = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Anyvoc Wavetable.wav");
        chooser = std::make_unique<juce::FileChooser> ("Export WAV wavetable", file, "*.wav");
        auto safe = juce::Component::SafePointer<WavetableDetail> (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safe] (const juce::FileChooser& selected)
        {
            if (safe == nullptr || selected.getResult() == juce::File()) return;
            const auto destination = selected.getResult().withFileExtension ("wav");
            const bool success = safe->processor.exportWavetable (safe->oscillator, destination);
            safe->showStatus (success ? "Exported " + destination.getFileName() : "Could not export. Choose a writable folder.", ! success);
        });
    }
    void showStatus (juce::String message, bool error = false) { status = std::move (message); statusError = error; repaint(); }
    AnyvocAudioProcessor& processor;
    int oscillator, selectedFrame = 0;
    juce::Colour accent;
    anyvoc::WavetableDocument document;
    juce::AudioBuffer<float> original;
    juce::String originalName, status;
    uint64_t knownRevision = 0;
    bool dirty = false, loading = false, statusError = false;
    WavetableCanvas canvas;
    WavetableFrameStrip frameStrip;
    juce::TextButton done, waveButton, harmonicButton, phaseButton, apply, exportButton, previous, next, duplicate, remove, undo, redo, revert, morph;
    juce::TextButton previousHarmonics, nextHarmonics, phaseReset, phaseRandom;
    std::array<juce::TextButton, 4> processButtons;
    std::array<juce::TextButton, 6> generateButtons;
    juce::Label tableName;
    juce::ComboBox morphFrom, morphTo, morphMode, harmonicPage;
    std::unique_ptr<juce::FileChooser> chooser;
};

const juce::StringArray warpModeNames { "Off", "Bend +", "Bend -", "Pulse Width", "Sync", "Mirror", "Fold",
                                       "FM - Frequency", "PM - Phase", "AM - Amplitude", "RM - Ring" };
const juce::StringArray crossSourceNames { "OSC A", "OSC B", "OSC C", "SUB", "NOISE" };

class WarpModeChoice final : public juce::Component
{
public:
    WarpModeChoice (AnyvocAudioProcessor& p, int oscillatorIn, int slotIn)
        : processor (p), oscillator (oscillatorIn), slot (slotIn)
    {
        addAndMakeVisible (box); box.addItemList (warpModeNames, 1);
        box.setComponentID ("osc" + juce::String (oscillator + 1) + "Warp" + juce::String (slot + 1) + "Mode");
        box.setTitle ("Oscillator " + juce::String (oscillator + 1) + " Warp " + juce::String (slot + 1) + " mode");
        box.setTooltip ("Choose a classic waveform transformation or FM, PM, AM or RM from an audio source. Cross modes replace the classic mode in this slot; AMOUNT sets depth. Both slots are saved with the sound.");
        box.onChange = [this]
        {
            const int mode = box.getSelectedItemIndex();
            if (mode < 0) return;
            const juce::Component::SafePointer<WarpModeChoice> safe (this);
            processor.setWarpMode (oscillator, slot, mode);
            if (safe == nullptr) return;
            refresh();
            if (onSelection) onSelection();
        };
        refresh();
    }
    void refresh() { box.setSelectedItemIndex (processor.getWarpMode (oscillator, slot), juce::dontSendNotification); }
    void resized() override { box.setBounds (getLocalBounds()); }
    std::function<void()> onSelection;
private:
    AnyvocAudioProcessor& processor;
    int oscillator, slot;
    juce::ComboBox box;
};

class WarpDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    WarpDisplay (AnyvocAudioProcessor& p, int oscillatorIn, juce::Colour colour)
        : processor (p), oscillator (oscillatorIn), accent (colour)
    {
        setTitle ("Warped wavetable preview");
        setTooltip ("The coloured line shows the base wavetable after classic Warp stages. The faint line is the unwarped source. Live FM/PM/AM/RM, per-voice modulation, filters and effects are not shown. Play notes to hear cross modulation.");
    }
    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto plot = bounds.reduced (13, 18);
        g.setColour (insetColour); g.fillRoundedRectangle (bounds, 8);
        g.setColour (borderColour); g.drawRoundedRectangle (bounds.reduced (0.5f), 8, 1);
        for (int i = 1; i < 8; ++i)
        {
            g.setColour (borderColour.withAlpha (0.55f));
            g.drawVerticalLine (juce::roundToInt (plot.getX() + plot.getWidth() * (float) i / 8), plot.getY(), plot.getBottom());
        }
        for (int i = 0; i <= 4; ++i)
        {
            g.setColour (borderColour.withAlpha (i == 2 ? 1.0f : 0.45f));
            g.drawHorizontalLine (juce::roundToInt (plot.getY() + plot.getHeight() * (float) i / 4), plot.getX(), plot.getRight());
        }
        const float position = value (processor, "osc" + juce::String (oscillator + 1) + "Position");
        std::array<float, 513> raw{}, warped{};
        float scale = 1.0f;
        for (size_t i = 0; i < raw.size(); ++i)
        {
            const auto phase = (float) i / (float) (raw.size() - 1);
            raw[i] = processor.getWavetableSample (oscillator, phase, position);
            warped[i] = processor.getWarpedWavetableSample (oscillator, phase, position);
            scale = juce::jmax (scale, std::abs (raw[i]), std::abs (warped[i]));
        }
        juce::Path rawPath, warpedPath;
        for (size_t i = 0; i < raw.size(); ++i)
        {
            const float x = plot.getX() + (float) i / (float) (raw.size() - 1) * plot.getWidth();
            const float rawY = plot.getCentreY() - raw[i] / scale * plot.getHeight() * 0.46f;
            const float warpedY = plot.getCentreY() - warped[i] / scale * plot.getHeight() * 0.46f;
            if (i == 0) { rawPath.startNewSubPath (x, rawY); warpedPath.startNewSubPath (x, warpedY); }
            else { rawPath.lineTo (x, rawY); warpedPath.lineTo (x, warpedY); }
        }
        g.setColour (secondaryText.withAlpha (0.36f)); g.strokePath (rawPath, juce::PathStrokeType (1.2f));
        g.setColour (accent.withAlpha (0.09f)); g.strokePath (warpedPath, juce::PathStrokeType (7));
        g.setColour (accent); g.strokePath (warpedPath, juce::PathStrokeType (1.8f));
        drawText (g, "ONE CYCLE", { 14, 3, 95, 12 }, 8, secondaryText, true);
        const bool cross = processor.getWarpMode (oscillator, 0) >= 7 || processor.getWarpMode (oscillator, 1) >= 7;
        drawText (g, cross ? "LIVE CROSS MODULATION NOT SHOWN"
                          : scale > 1.01f ? "AUTO SCALE  " + juce::String (scale, 2) + "x" : "BASE WAVE",
                  { getWidth() - 300, 3, 286, 12 }, 8, cross ? accent : secondaryText, true, juce::Justification::centredRight);
    }
private:
    AnyvocAudioProcessor& processor;
    int oscillator;
    juce::Colour accent;
};

class WarpDetail final : public juce::Component
{
public:
    WarpDetail (AnyvocAudioProcessor& p, int oscillatorIn, juce::Colour colour,
                std::function<void (int)> openOscillator, std::function<void()> editRaw)
        : processor (p), oscillator (oscillatorIn), accent (colour),
          prefix ("osc" + juce::String (oscillator + 1)), display (p, oscillator, colour),
          position (p, prefix + "Position", "WAVE", "Choose the source wavetable frame before both warp stages.", Unit::percent, colour)
    {
        setBounds (0, 0, designWidth, designHeight);
        addAndMakeVisible (display); display.setBounds (104, 188, 800, 158);
        addAndMakeVisible (position); position.setBounds (920, 204, 96, 111);
        setup (done, "DONE", { 936, 132, 80, 29 }, [this] { setVisible (false); }, "Close Warp Lab. All changes are already applied to your sound.");
        setup (rawEdit, "RAW EDIT", { 808, 132, 112, 29 }, std::move (editRaw), "Open Wavetable Lab to draw the original waveform. Warp stays non-destructive and is applied after the table.");
        for (int i = 0; i < 3; ++i)
        {
            setup (oscillatorButtons[(size_t) i], "OSC " + juce::String::charToString ((juce::juce_wchar) ('A' + i)),
                   { 481 + i * 78, 132, 70, 29 }, [openOscillator, i] { openOscillator (i); },
                   "Open this oscillator's two wavetable warp stages. Available in Wavetable mode.");
            oscillatorButtons[(size_t) i].setToggleState (i == oscillator, juce::dontSendNotification);
        }
        for (int slot = 0; slot < 2; ++slot)
        {
            const int x = 104 + slot * 464;
            const auto slotPrefix = prefix + "Warp" + juce::String (slot + 1);
            modes[(size_t) slot] = std::make_unique<WarpModeChoice> (processor, oscillator, slot);
            addAndMakeVisible (*modes[(size_t) slot]); modes[(size_t) slot]->setBounds (x + 20, 405, 268, 27);
            modes[(size_t) slot]->onSelection = [this] { refresh(); };
            sources[(size_t) slot] = std::make_unique<Choice> (processor, slotPrefix + "CrossSource", "", crossSourceNames,
                "Audio source for FM/PM/AM/RM. Taken before source LEVEL, PAN and filters: LEVEL 0 still modulates. Uses the previous sample of this voice; choosing this same oscillator creates feedback. Sample and Granular sources work too.");
            addAndMakeVisible (*sources[(size_t) slot]); sources[(size_t) slot]->setBounds (x + 90, 440, 198, 26);
            sources[(size_t) slot]->box.setTitle ("Warp " + juce::String (slot + 1) + " audio source");
            amounts[(size_t) slot] = std::make_unique<Knob> (processor, slotPrefix + "Amount", "AMOUNT",
                "Strength of this warp stage. FM changes frequency; PM phase; AM loudness; RM multiplies the signal. Choose a source for cross modes, then drag an LFO or Macro chip here for movement. Off bypasses the stage.", Unit::percent, accent);
            addAndMakeVisible (*amounts[(size_t) slot]); amounts[(size_t) slot]->setBounds (x + 329, 390, 95, 109);
        }
        refresh();
    }
    Knob& amountControl (int slot) { return *amounts[(size_t) slot]; }
    Knob& positionControl() { return position; }
    void refresh()
    {
        for (int i = 0; i < 3; ++i)
            oscillatorButtons[(size_t) i].setEnabled (value (processor, "osc" + juce::String (i + 1) + "Mode") < 0.5f);
        for (int slot = 0; slot < 2; ++slot)
        {
            modes[(size_t) slot]->refresh();
            sources[(size_t) slot]->setEnabled (processor.getWarpMode (oscillator, slot) >= 7);
        }
        display.repaint(); repaint();
    }
    bool hitTest (int x, int y) override { return juce::Rectangle<int> (24, 117, 1076, 461).contains (x, y); }
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! juce::Rectangle<int> (82, 117, 956, 461).contains (event.getPosition())) setVisible (false);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (background.withAlpha (0.94f)); g.fillRoundedRectangle (24, 117, 1076, 461, 12);
        drawCard (g, { 82, 117, 956, 461 }, "OSC " + juce::String::charToString ((juce::juce_wchar) ('A' + oscillator)) + " / WARP LAB", "", accent);
        drawText (g, processor.getWavetableName (oscillator), { 104, 165, 500, 18 }, 11, primaryText);
        drawText (g, "WAVETABLE ONLY", { 799, 165, 217, 18 }, 9, accent, true, juce::Justification::centredRight);
        g.setColour (secondaryText.withAlpha (0.36f)); g.drawLine (105, 359, 125, 359, 1.2f);
        drawText (g, "RAW", { 132, 351, 60, 16 }, 9, secondaryText, true);
        g.setColour (accent); g.drawLine (197, 359, 217, 359, 1.8f);
        drawText (g, "CLASSIC WARP", { 224, 351, 105, 16 }, 9, accent, true);
        drawText (g, "Base preview; live cross modulation is heard when you play the keys below.",
                  { 340, 350, 676, 18 }, 10, secondaryText, false, juce::Justification::centredRight);
        const juce::StringArray descriptions {
            "This stage is bypassed. Choose a mode\nto reshape the wavetable.",
            "Bend the cycle's phase in one direction.\nRaise AMOUNT to stretch its shape.",
            "Bend the cycle's phase in the opposite\ndirection to Bend +.",
            "Shift the width of the waveform cycle.\nTry an LFO on AMOUNT for movement.",
            "Pack more cycles into one period.\nRaise AMOUNT for a sharper sync tone.",
            "Reflect portions of the waveform cycle.\nExplore new symmetry with AMOUNT.",
            "Fold waveform peaks back on themselves.\nRaise AMOUNT for richer harmonics.",
            "Source bends the playback frequency.\nTry OSC B at LEVEL 0 for metallic tones.",
            "Source moves the waveform's phase.\nTune the source for bells or bright bass.",
            "Source shapes the carrier's loudness.\nA silent source halves it at full depth.",
            "Multiply the carrier by the source.\nA silent source mutes it at full depth."
        };
        for (int slot = 0; slot < 2; ++slot)
        {
            const int x = 104 + slot * 464;
            const auto slotPrefix = prefix + "Warp" + juce::String (slot + 1);
            const int mode = processor.getWarpMode (oscillator, slot);
            g.setColour (insetColour); g.fillRoundedRectangle ((float) x, 377, 448, 151, 9);
            g.setColour (borderColour); g.drawRoundedRectangle ((float) x, 377, 448, 151, 9, 1);
            drawText (g, "WARP " + juce::String (slot + 1), { x + 20, 383, 160, 18 }, 11, accent, true);
            drawText (g, mode == 0 ? "BYPASSED" : "ACTIVE", { x + 194, 383, 94, 18 }, 8.5f,
                      mode == 0 ? secondaryText : accent, true, juce::Justification::centredRight);
            drawText (g, "SOURCE", { x + 20, 444, 68, 18 }, 9, mode >= 7 ? accent : secondaryText, true);
            g.setColour (secondaryText); g.setFont (font (11));
            g.drawFittedText (descriptions[mode], { x + 20, 477, 293, 40 }, juce::Justification::topLeft, 2);
            const int source = juce::jlimit (0, 4, (int) value (processor, slotPrefix + "CrossSource"));
            drawText (g, mode >= 7 && source == oscillator ? "SELF FEEDBACK" : "DROP LFO HERE",
                      { x + 320, 506, 116, 15 }, 8, mode >= 7 && source == oscillator ? orange : secondaryText, true,
                      juce::Justification::centred);
        }
        const auto stage = [this] (int slot)
        {
            const auto slotPrefix = prefix + "Warp" + juce::String (slot + 1);
            const int mode = processor.getWarpMode (oscillator, slot);
            const int source = juce::jlimit (0, 4, (int) value (processor, slotPrefix + "CrossSource"));
            const auto name = mode >= 7 ? warpModeNames[mode].upToFirstOccurrenceOf (" -", false, false) + " from " + crossSourceNames[source] : warpModeNames[mode];
            return name + (mode == 0 ? juce::String() : "  " + formatted (value (processor, slotPrefix + "Amount"), Unit::percent));
        };
        drawText (g, "RAW TABLE   >   1: " + stage (0) + "   >   2: " + stage (1) + "   >   FILTERS / FX",
                  { 104, 534, 912, 23 }, 10.5f, accent, true, juce::Justification::centred);
        drawText (g, "Cross sources work at LEVEL 0. Tune the source oscillator to change the tone; your original table stays editable.",
                  { 104, 558, 912, 14 }, 9.5f, secondaryText, false, juce::Justification::centred);
    }
private:
    void setup (juce::TextButton& button, const juce::String& text, juce::Rectangle<int> bounds,
                std::function<void()> action, juce::String tip)
    {
        addAndMakeVisible (button); button.setButtonText (text); button.setBounds (bounds);
        button.onClick = std::move (action); button.setTooltip (tip); button.setWantsKeyboardFocus (false);
    }
    AnyvocAudioProcessor& processor;
    int oscillator;
    juce::Colour accent;
    juce::String prefix;
    WarpDisplay display;
    Knob position;
    std::array<std::unique_ptr<WarpModeChoice>, 2> modes;
    std::array<std::unique_ptr<Choice>, 2> sources;
    std::array<std::unique_ptr<Knob>, 2> amounts;
    std::array<juce::TextButton, 3> oscillatorButtons;
    juce::TextButton done, rawEdit;
};

class OscillatorDetail final : public juce::Component
{
public:
    OscillatorDetail (AnyvocAudioProcessor& p, int index, juce::Colour colour,
                      std::function<void()> chooseFile, std::function<void (const juce::File&)> importFile)
        : processor (p), oscillator (index), accent (colour),
          prefix ("osc" + juce::String (index + 1)),
          modeChoice (p, prefix + "Mode", "", { "Wavetable", "Sample", "Granular" },
                      "Wavetable scans cycles. Sample plays an audio recording. Granular layers small pieces of that recording."),
          display (p, index, colour)
    {
        setBounds (0, 0, designWidth, designHeight);
        addAndMakeVisible (modeChoice); modeChoice.setBounds (184, 177, 170, 28);
        addAndMakeVisible (display); display.setBounds (184, 222, 748, 90);
        display.onImport = std::move (importFile);
        addAndMakeVisible (close); close.setButtonText ("DONE"); close.setBounds (854, 134, 80, 29);
        close.onClick = [this] { setVisible (false); };
        addAndMakeVisible (import); import.setButtonText ("IMPORT WAV"); import.setBounds (810, 177, 124, 28);
        import.onClick = std::move (chooseFile);
        import.setTooltip ("Choose a WAV. Sample and Granular accept audio recordings up to 20 seconds.");
        position = &addKnob ("Position", "POSITION", "Wavetable frame, sample start, or granular centre. The amber marker shows this position.",
                             Unit::percent, { 182, 341, 96, 112 });
        root = &addKnob ("Root", "ROOT NOTE", "The original pitch of your recording. Playing this MIDI note uses the sample's original pitch. Type a note name such as C4.",
                         Unit::note, { 293, 341, 96, 112 });
        addAndMakeVisible (loop); loop.setButtonText ("LOOP"); loop.setBounds (407, 358, 110, 28);
        loop.setTooltip ("Repeat sample playback while a note is held, or wrap grains at the boundaries of the recording.");
        addAndMakeVisible (reverse); reverse.setButtonText ("REVERSE"); reverse.setBounds (407, 400, 122, 28);
        reverse.setTooltip ("Read the imported audio backwards.");
        loopAttachment = std::make_unique<SoundButtonAttachment> (processor, prefix + "Loop", loop);
        reverseAttachment = std::make_unique<SoundButtonAttachment> (processor, prefix + "Reverse", reverse);
        grains[0] = &addKnob ("GrainSize", "GRAIN SIZE", "Length of each tiny audio grain. Short grains sound textured; longer grains reveal the original recording.",
                              Unit::seconds, { 545, 341, 118, 112 });
        grains[1] = &addKnob ("GrainDensity", "DENSITY", "How many grains begin per second. Higher density makes a smoother cloud of sound.",
                              Unit::density, { 675, 341, 118, 112 });
        grains[2] = &addKnob ("GrainSpray", "SPRAY", "Randomize grain positions around the centre to discover different parts of your recording.",
                              Unit::percent, { 805, 341, 118, 112 });
        refresh();
    }

    Knob& positionControl() { return *position; }

    void refresh()
    {
        const int mode = (int) value (processor, prefix + "Mode");
        position->setCaption (mode == 0 ? "WAVE" : mode == 1 ? "START" : "CENTRE");
        root->setEnabled (mode != 0); root->setAlpha (mode != 0 ? 1.0f : 0.3f);
        loop.setEnabled (mode != 0);
        reverse.setEnabled (mode != 0);
        loop.setAlpha (mode != 0 ? 1.0f : 0.3f);
        reverse.setAlpha (mode != 0 ? 1.0f : 0.3f);
        for (auto* grain : grains) { grain->setEnabled (mode == 2); grain->setAlpha (mode == 2 ? 1.0f : 0.3f); }
        if (mode != previousMode) { previousMode = mode; repaint(); }
        display.repaint();
        repaint (370, 178, 430, 28);
    }

    bool hitTest (int x, int y) override
    {
        // Keep the piano and performance macros usable while editing the source.
        return juce::Rectangle<int> (24, 117, 1076, 461).contains (x, y);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! juce::Rectangle<int> (154, 117, 812, 461).contains (event.getPosition())) setVisible (false);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (background.withAlpha (0.82f));
        g.fillRoundedRectangle (24, 117, 1076, 461, 12);
        drawCard (g, { 154, 117, 812, 461 }, "OSC " + juce::String::charToString ((juce::juce_wchar) ('A' + oscillator)) + " / SOURCE LAB", "", accent);
        const int mode = (int) value (processor, prefix + "Mode");
        const auto sourceName = mode == 0 ? processor.getWavetableName (oscillator) : processor.getSampleName (oscillator);
        drawText (g, sourceName, { 371, 179, 426, 24 }, 12, primaryText);
        g.setColour (borderColour); g.drawVerticalLine (532, 339, 453);
        drawText (g, "SOURCE & PLAYBACK", { 184, 318, 340, 17 }, 9, secondaryText, true);
        drawText (g, "GRANULAR TEXTURE", { 552, 318, 370, 17 }, 9,
                  mode == 2 ? accent : secondaryText.withAlpha (0.5f), true);
        const juce::StringArray headlines {
            "WAVETABLE   Scan a palette of repeating waveforms.",
            "SAMPLE   Play your recording across the keyboard.",
            "GRANULAR   Turn small pieces of audio into a moving texture."
        };
        const juce::StringArray descriptions {
            "Turn WAVE to move through the table. Drag LFO 1 onto WAVE for an evolving tone.\nTo use a vocal, field recording or drum hit, choose Sample or Granular above.",
            "Import a WAV, set its ROOT NOTE, then play. START is applied when a note begins.\nTry LOOP for a sustained sound, or REVERSE to hear the recording backwards.",
            "Import a WAV and move CENTRE to explore it. SIZE changes the grain length; DENSITY\nchanges the overlap. Add SPRAY for variation, then modulate CENTRE with an LFO."
        };
        drawText (g, headlines[juce::jlimit (0, 2, mode)], { 184, 479, 748, 21 }, 12, accent, true);
        g.setColour (secondaryText); g.setFont (font (12));
        g.drawFittedText (descriptions[juce::jlimit (0, 2, mode)], { 184, 509, 748, 40 }, juce::Justification::topLeft, 2);
        drawText (g, "Play the keyboard below while editing. Source imports accept WAV recordings up to 20 seconds.",
                  { 184, 554, 748, 15 }, 9);
    }

private:
    Knob& addKnob (const juce::String& suffix, juce::String name, juce::String tip, Unit unit, juce::Rectangle<int> bounds)
    {
        auto control = std::make_unique<Knob> (processor, prefix + suffix, std::move (name), std::move (tip), unit, accent);
        addAndMakeVisible (*control); control->setBounds (bounds);
        auto& result = *control; controls.push_back (std::move (control)); return result;
    }
    AnyvocAudioProcessor& processor;
    int oscillator, previousMode = -1;
    juce::Colour accent;
    juce::String prefix;
    Choice modeChoice;
    WaveDisplay display;
    juce::TextButton close, import;
    juce::ToggleButton loop, reverse;
    std::unique_ptr<SoundButtonAttachment> loopAttachment, reverseAttachment;
    std::vector<std::unique_ptr<Knob>> controls;
    Knob* position = nullptr;
    Knob* root = nullptr;
    std::array<Knob*, 3> grains{};
};

class PhaseDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    PhaseDisplay (AnyvocAudioProcessor& p, int oscillatorIn, juce::Colour colour)
        : processor (p), oscillator (oscillatorIn), accent (colour), prefix ("osc" + juce::String (oscillator + 1))
    {
        setTitle ("Oscillator starting phase and random range");
        setTooltip ("Settings diagram, not live audio. The line marks the next note's base starting phase. The shaded region extends forwards by RANDOM PHASE, wrapping around the cycle. RANDOM UNISON lets the other unison voices start anywhere in the cycle.");
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat(); const auto plot = r.reduced (10, 10);
        g.setColour (insetColour); g.fillRoundedRectangle (r, 6);
        g.setColour (borderColour); g.drawRoundedRectangle (r.reduced (0.5f), 6, 1);
        const int mode = (int) value (processor, prefix + "Mode");
        if (mode != 0)
        {
            drawText (g, mode == 1 ? "SAMPLE PLAYBACK" : "GRANULAR PLAYBACK", { 8, 10, getWidth() - 16, 18 }, 10, secondaryText, true, juce::Justification::centred);
            drawText (g, "Phase controls apply to Wavetable mode.", { 8, 31, getWidth() - 16, 18 }, 10, secondaryText, false, juce::Justification::centred);
            return;
        }
        const float phase = value (processor, prefix + "Phase");
        const float start = phase >= 1.0f ? 0.0f : phase;
        const float random = value (processor, prefix + "PhaseRandom");
        const auto shade = [&g, &plot] (float from, float to)
        {
            if (to > from) g.fillRect (plot.getX() + from * plot.getWidth(), plot.getY(), (to - from) * plot.getWidth(), plot.getHeight());
        };
        g.setColour (accent.withAlpha (0.13f));
        shade (start, juce::jmin (1.0f, start + random));
        if (start + random > 1.0f) shade (0.0f, start + random - 1.0f);
        g.setColour (borderColour); g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());
        const float position = value (processor, prefix + "Position");
        std::array<float, 193> samples{}; float peak = 1.0f;
        for (size_t i = 0; i < samples.size(); ++i)
        {
            samples[i] = processor.getWarpedWavetableSample (oscillator, (float) i / (float) (samples.size() - 1), position);
            peak = juce::jmax (peak, std::abs (samples[i]));
        }
        juce::Path wave;
        for (size_t i = 0; i < samples.size(); ++i)
        {
            const float x = plot.getX() + (float) i / (float) (samples.size() - 1) * plot.getWidth();
            const float y = plot.getCentreY() - samples[i] / peak * plot.getHeight() * 0.42f;
            if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
        }
        g.setColour (secondaryText.withAlpha (0.65f)); g.strokePath (wave, juce::PathStrokeType (1.1f));
        const float marker = plot.getX() + start * plot.getWidth();
        g.setColour (accent); g.drawVerticalLine (juce::roundToInt (marker), plot.getY(), plot.getBottom());
        juce::Path pointer; pointer.addTriangle (marker - 3.5f, plot.getY() - 5, marker + 3.5f, plot.getY() - 5, marker, plot.getY() + 1);
        g.fillPath (pointer);
        if (processor.getWarpMode (oscillator, 0) >= 7 || processor.getWarpMode (oscillator, 1) >= 7)
            drawText (g, "LIVE CROSS MOD NOT SHOWN", { 5, getHeight() - 13, getWidth() - 10, 11 }, 7,
                      secondaryText, true, juce::Justification::centredRight);
    }
private:
    AnyvocAudioProcessor& processor;
    int oscillator;
    juce::Colour accent;
    juce::String prefix;
};

class VoicingPanel final : public juce::Component
{
public:
    explicit VoicingPanel (AnyvocAudioProcessor& p)
        : processor (p), mode (p, "voiceMode", "VOICE MODE", { "Poly", "Mono", "Legato" },
                              "Poly plays chords. Mono plays the newest note and restarts its envelopes. Legato keeps envelopes and source playback running when notes overlap."),
          glide (p, "glideTime", "GLIDE", "Pitch-slide time in Mono and Legato. Zero changes pitch immediately. With ALWAYS off, only overlapping notes glide.", Unit::seconds, mint)
    {
        setBounds (0, 0, designWidth, designHeight); setComponentID ("voicingPanel"); setTitle ("Voicing and phase");
        addAndMakeVisible (mode); mode.setBounds (60, 190, 192, 49);
        mode.box.setComponentID ("voiceModeChoice"); mode.box.setTitle ("Voice mode");
        mode.box.onChange = [this] { refresh(); };
        addAndMakeVisible (glide); glide.setBounds (280, 186, 104, 101);
        glide.slider.setComponentID ("glideTime"); glide.slider.setTitle ("Glide time");
        addAndMakeVisible (always); always.setBounds (405, 200, 154, 25); always.setButtonText ("ALWAYS GLIDE");
        always.setComponentID ("glideAlways"); always.setTitle ("Always glide");
        always.setTooltip ("On: slide from the previous pitch even after releasing all keys. Off: slide only while notes overlap. Glide is available in Mono and Legato.");
        alwaysAttachment = std::make_unique<SoundButtonAttachment> (processor, "glideAlways", always);
        done.setButtonText ("DONE"); done.setComponentID ("voicingDone"); done.setBounds (990, 132, 92, 29);
        done.onClick = [this] { setVisible (false); }; done.setWantsKeyboardFocus (false);
        done.setTooltip ("Close Voicing. Your changes are already part of the current sound."); addAndMakeVisible (done);
        const std::array<juce::Colour, 3> colours { mint, lilac, blue };
        for (int i = 0; i < 3; ++i)
        {
            const int x = 42 + i * 352; const auto prefix = "osc" + juce::String (i + 1); const auto colour = colours[(size_t) i];
            displays[(size_t) i] = std::make_unique<PhaseDisplay> (processor, i, colour);
            addAndMakeVisible (*displays[(size_t) i]); displays[(size_t) i]->setBounds (x + 18, 354, 300, 61);
            phases[(size_t) i] = std::make_unique<Knob> (processor, prefix + "Phase", "START PHASE",
                "Starting position within the wavetable cycle, shown in degrees. 360 degrees wraps to zero. Applies to a new or retriggered note; it does not move a held note's running phase.", Unit::degrees, colour);
            addAndMakeVisible (*phases[(size_t) i]); phases[(size_t) i]->setBounds (x + 31, 421, 106, 85);
            phases[(size_t) i]->slider.setComponentID (prefix + "Phase"); phases[(size_t) i]->slider.setTitle ("Osc " + juce::String::charToString ((juce::juce_wchar) ('A' + i)) + " start phase in degrees");
            randoms[(size_t) i] = std::make_unique<Knob> (processor, prefix + "PhaseRandom", "RANDOM PHASE",
                "Random range added forwards from START PHASE on each new note. 0% repeats the same starting point; 100% allows the full cycle. Applies only when a wavetable note starts or retriggers.", Unit::percent, colour);
            addAndMakeVisible (*randoms[(size_t) i]); randoms[(size_t) i]->setBounds (x + 183, 421, 106, 85);
            randoms[(size_t) i]->slider.setComponentID (prefix + "PhaseRandom"); randoms[(size_t) i]->slider.setTitle ("Osc " + juce::String::charToString ((juce::juce_wchar) ('A' + i)) + " random phase range");
            auto& unison = randomUnison[(size_t) i]; addAndMakeVisible (unison);
            unison.setBounds (x + 18, 511, 300, 25); unison.setButtonText ("RANDOM UNISON"); unison.setComponentID (prefix + "RandomUnison");
            unison.setTooltip ("On: the main unison voice uses RANDOM PHASE, while the other voices start anywhere in the full cycle. Off: every voice uses the limited RANDOM PHASE range; at 0% they all start aligned. Wavetable only, on the next note.");
            randomUnisonAttachments[(size_t) i] = std::make_unique<SoundButtonAttachment> (processor, prefix + "RandomUnison", unison);
        }
        refresh();
    }
    void open() { refresh(); setVisible (true); toFront (false); }
    void refresh()
    {
        const bool mono = value (processor, "voiceMode") > 0.5f;
        glide.setEnabled (mono); glide.setAlpha (mono ? 1.0f : 0.4f);
        refreshToggle (always, mono, value (processor, "glideAlways") > 0.5f);
        for (int i = 0; i < 3; ++i)
        {
            const bool wavetable = value (processor, "osc" + juce::String (i + 1) + "Mode") < 0.5f;
            phases[(size_t) i]->setEnabled (wavetable); phases[(size_t) i]->setAlpha (wavetable ? 1.0f : 0.4f);
            randoms[(size_t) i]->setEnabled (wavetable); randoms[(size_t) i]->setAlpha (wavetable ? 1.0f : 0.4f);
            refreshToggle (randomUnison[(size_t) i], wavetable, value (processor, "osc" + juce::String (i + 1) + "RandomUnison") > 0.5f);
            displays[(size_t) i]->repaint();
        }
        repaint();
    }
    bool hitTest (int x, int y) override { return juce::Rectangle<int> (24, 117, 1076, 461).contains (x, y); }
    void paint (juce::Graphics& g) override
    {
        drawCard (g, { 24, 117, 1076, 461 }, "VOICING & PHASE", "", mint);
        const int voiceMode = juce::jlimit (0, 2, (int) value (processor, "voiceMode"));
        const juce::StringArray names { "POLY / CHORDS", "MONO / RETRIGGER", "LEGATO / CONNECTED" };
        drawText (g, names[voiceMode], { 695, 135, 278, 22 }, 11, mint, true, juce::Justification::centredRight);
        g.setColour (insetColour); g.fillRoundedRectangle (42, 176, 1040, 125, 9);
        g.setColour (borderColour); g.drawRoundedRectangle (42, 176, 1040, 125, 9, 1); g.drawVerticalLine (581, 191, 286);
        drawText (g, voiceMode == 0 ? "Play chords and overlapping notes." : "One note at a time; newest note wins.",
                  { 60, 250, 192, 35 }, 10, secondaryText);
        g.setColour (secondaryText); g.setFont (font (10.5f));
        g.drawFittedText (voiceMode == 0 ? "Glide is available\nin Mono and Legato."
                         : value (processor, "glideAlways") > 0.5f ? "Slide after a gap, as well\nas between overlapping notes."
                                                                  : "Slide only while notes overlap.\nAfter a gap, pitch starts directly.",
                          { 405, 235, 158, 50 }, juce::Justification::topLeft, 3);
        const juce::StringArray headlines { "PLAY MORE THAN ONE NOTE", "A FRESH ATTACK ON EACH NOTE", "LINK NOTES INTO ONE PHRASE" };
        const juce::StringArray descriptions {
            "Poly gives each note its own voice. Play chords or let notes ring together.\nGlide is disabled in this mode.",
            "Mono plays one note and restarts the envelopes and source on each key.\nHold a key, then play another to try the glide.",
            "Overlapping notes keep the envelopes and source playback running.\nRelease every key for a new attack; use glide to connect pitches."
        };
        drawText (g, headlines[voiceMode], { 602, 195, 461, 22 }, 12, primaryText, true);
        g.setColour (secondaryText); g.setFont (font (11));
        g.drawFittedText (descriptions[voiceMode], { 602, 227, 461, 50 }, juce::Justification::topLeft, 3);
        const std::array<juce::Colour, 3> colours { mint, lilac, blue };
        for (int i = 0; i < 3; ++i)
        {
            const int x = 42 + i * 352; const auto prefix = "osc" + juce::String (i + 1);
            const bool wavetable = value (processor, prefix + "Mode") < 0.5f;
            g.setColour (cardColour.brighter (0.025f)); g.fillRoundedRectangle ((float) x, 314, 336, 239, 9);
            g.setColour (borderColour); g.drawRoundedRectangle ((float) x, 314, 336, 239, 9, 1);
            drawText (g, "OSC " + juce::String::charToString ((juce::juce_wchar) ('A' + i)) + " / PHASE", { x + 18, 325, 190, 20 }, 12, colours[(size_t) i], true);
            drawText (g, wavetable ? "NEXT NOTE" : "WAVETABLE ONLY", { x + 204, 327, 114, 18 }, 8.5f, secondaryText, true, juce::Justification::centredRight);
            const bool unison = value (processor, prefix + "RandomUnison") > 0.5f;
            drawText (g, ! wavetable ? "Switch source mode to use these controls." : unison ? "Main voice: range above / others: full random" : "All voices: range above / 0% aligns them",
                      { x + 18, 536, 300, 15 }, 9, secondaryText);
        }
        drawText (g, "The pointer marks the next wavetable note's start; shading shows its random range. Legato overlap keeps the running phase. Settings preview only.",
                  { 42, 557, 1040, 17 }, 10, secondaryText, false, juce::Justification::centred);
    }
private:
    static void refreshToggle (juce::ToggleButton& button, bool enabled, bool parameterState)
    {
        // This bundled JUCE Button ignores setToggleState while disabled. A host
        // or preset update can therefore be missed by its attachment. Resync the
        // display before restoring availability, without writing back to APVTS.
        if (button.getToggleState() != parameterState)
        {
            button.setEnabled (true);
            button.setToggleState (parameterState, juce::dontSendNotification);
        }
        button.setEnabled (enabled); button.setAlpha (enabled ? 1.0f : 0.4f);
    }
    AnyvocAudioProcessor& processor;
    Choice mode;
    Knob glide;
    juce::ToggleButton always;
    juce::TextButton done;
    std::unique_ptr<SoundButtonAttachment> alwaysAttachment;
    std::array<std::unique_ptr<PhaseDisplay>, 3> displays;
    std::array<std::unique_ptr<Knob>, 3> phases, randoms;
    std::array<juce::ToggleButton, 3> randomUnison;
    std::array<std::unique_ptr<SoundButtonAttachment>, 3> randomUnisonAttachments;
};

class PresetBrowser final : public juce::Component, private juce::ListBoxModel,
                            private juce::KeyListener
{
public:
    PresetBrowser (AnyvocAudioProcessor& p, anyvoc::PresetLibrary& libraryIn,
                   std::function<bool (const anyvoc::PresetEntry&)> loadIn)
        : processor (p), library (libraryIn), loadEntry (std::move (loadIn))
    {
        setBounds (0, 0, designWidth, designHeight);
        setTitle ("Preset browser"); setComponentID ("presetBrowser");
        addAndMakeVisible (search); search.setBounds (42, 197, 290, 30);
        search.setComponentID ("presetSearch"); search.setTitle ("Preset search");
        search.setTooltip ("Search names, categories and sources. Selecting a result leaves your current sound playing until you load it.");
        search.setTextToShowWhenEmpty ("Search sounds...", secondaryText);
        search.setColour (juce::TextEditor::backgroundColourId, insetColour);
        search.setColour (juce::TextEditor::textColourId, primaryText);
        search.setColour (juce::TextEditor::outlineColourId, borderColour);
        search.setColour (juce::TextEditor::focusedOutlineColourId, mint.withAlpha (0.7f));
        search.setColour (juce::TextEditor::highlightColourId, mint.withAlpha (0.25f));
        search.setFont (font (12)); search.setIndents (10, 7);
        search.onTextChange = [this] { applyFilter(); };
        search.onReturnKey = [this] { loadSelected(); };
        search.onEscapeKey = [this] { setVisible (false); };
        search.addKeyListener (this);
        setupChoice (source, "presetSource", "Preset source", { 344, 197, 145, 30 }, { "All sources", "Factory", "User" });
        setupChoice (category, "presetCategory", "Preset category", { 501, 197, 183, 30 }, { "All categories" });
        setupChoice (sort, "presetSort", "Preset sort", { 874, 197, 208, 30 }, { "Name", "Category" });
        setup (favouritesOnly, "FAVOURITES ONLY", { 696, 197, 166, 30 }, [this] { applyFilter(); }, "Show only the sounds you have starred.");
        favouritesOnly.setComponentID ("presetFavouritesOnly"); favouritesOnly.setClickingTogglesState (true);
        setup (done, "DONE", { 990, 132, 92, 29 }, [this] { setVisible (false); }, "Close the browser. Your loaded sound stays ready to play.");
        setup (refreshButton, "REFRESH", { 886, 132, 92, 29 }, [this]
        {
            feedback.clear(); feedbackError = false; library.refresh(); refresh();
        }, "Scan your user preset folder again for new or changed .anyvoc files.");
        refreshButton.setComponentID ("presetRefresh");
        setup (openFolder, "OPEN FOLDER", { 756, 132, 118, 29 }, [this]
        {
            const auto directory = library.getDirectory();
            const auto result = directory.createDirectory();
            if (result.failed() || ! directory.startAsProcess())
            {
                feedback = "Could not open the preset folder: " + directory.getFullPathName(); feedbackError = true; repaint();
            }
        }, "Open your user preset folder. Put .anyvoc files here; use subfolders as categories, then REFRESH.");
        openFolder.setComponentID ("presetOpenFolder");
        addAndMakeVisible (results); results.setBounds (42, 261, 730, 250);
        results.setModel (this); results.setRowHeight (35); results.setMultipleSelectionEnabled (false);
        results.setOutlineThickness (1); results.setColour (juce::ListBox::backgroundColourId, insetColour);
        results.setColour (juce::ListBox::outlineColourId, borderColour);
        results.setComponentID ("presetResults"); results.setTitle ("Preset results");
        results.setWantsKeyboardFocus (true); results.addKeyListener (this);
        setup (favourite, "FAVOURITE", { 808, 419, 120, 29 }, [this]
        {
            if (const auto* entry = selectedEntry()) toggleFavourite (entry->id, ! entry->favourite);
        }, "Star the selected sound without loading it. Favourites are stored in your local library settings.");
        favourite.setComponentID ("presetFavourite");
        setup (loadButton, "LOAD SOUND", { 938, 419, 124, 29 }, [this] { loadSelected(); },
               "Load the selected sound, then audition it with the keyboard below. Double-click a result or press Return for the same action.");
        loadButton.setComponentID ("presetLoad");
        loadButton.setColour (juce::TextButton::buttonColourId, mint.withAlpha (0.16f));
        loadButton.setColour (juce::TextButton::textColourOffId, mint);
        setup (previous, "<  PREVIOUS", { 788, 530, 140, 29 }, [this] { selectRelative (-1); },
               "Select the previous result. Press Return or LOAD SOUND to hear it.");
        setup (next, "NEXT  >", { 940, 530, 142, 29 }, [this] { selectRelative (1); },
               "Select the next result. Press Return or LOAD SOUND to hear it.");
        previous.setComponentID ("presetPrevious"); next.setComponentID ("presetNext");
        refresh();
    }
    ~PresetBrowser() override
    {
        search.removeKeyListener (this); results.removeKeyListener (this); results.setModel (nullptr);
    }
    void open()
    {
        refresh();
        if (selectedId.isEmpty()) selectId (processor.getLoadedPresetId());
        setVisible (true); toFront (false); search.grabKeyboardFocus(); repaint();
    }
    bool loadAdjacent (int delta)
    {
        refresh();
        if (entries.empty()) return false;
        int current = -1;
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].id == processor.getLoadedPresetId()) { current = (int) i; break; }
        const int count = (int) entries.size();
        results.selectRow (current < 0 ? (delta > 0 ? 0 : count - 1) : (current + delta + count) % count);
        loadSelected(); return true;
    }
    void refresh()
    {
        const auto revision = library.getRevision();
        const auto loadedId = processor.getLoadedPresetId(), loadedName = processor.getLoadedPresetName();
        if (revision != knownRevision)
        {
            knownRevision = revision; rebuildCategories(); applyFilter (false);
        }
        if (knownLoadedId != loadedId || knownLoadedName != loadedName)
        {
            knownLoadedId = loadedId; knownLoadedName = loadedName; results.repaint(); repaint();
        }
        const auto status = library.getStatus(); const bool scanning = library.isScanning();
        if (status != scanStatus || scanning != wasScanning)
        {
            scanStatus = status; wasScanning = scanning; repaint();
        }
        refreshButton.setEnabled (! scanning);
        refreshButton.setButtonText (scanning ? "SCANNING" : "REFRESH");
    }
    bool hitTest (int x, int y) override { return juce::Rectangle<int> (24, 117, 1076, 461).contains (x, y); }
    void paint (juce::Graphics& g) override
    {
        drawCard (g, { 24, 117, 1076, 461 }, "PRESET LIBRARY", "", mint);
        drawText (g, "PLAYING: " + processor.getLoadedPresetName() + "   /   The keys below audition this loaded sound.",
                  { 42, 157, 1040, 19 }, 10.5f, secondaryText);
        drawText (g, "SEARCH", { 42, 179, 290, 16 }, 9, secondaryText, true);
        drawText (g, "SOURCE", { 344, 179, 145, 16 }, 9, secondaryText, true);
        drawText (g, "CATEGORY", { 501, 179, 183, 16 }, 9, secondaryText, true);
        drawText (g, "YOUR COLLECTION", { 696, 179, 166, 16 }, 9, secondaryText, true);
        drawText (g, "SORT BY", { 874, 179, 208, 16 }, 9, secondaryText, true);
        drawText (g, "NAME", { 88, 239, 340, 16 }, 9, secondaryText, true);
        drawText (g, "CATEGORY", { 432, 239, 175, 16 }, 9, secondaryText, true);
        drawText (g, "SOURCE", { 622, 239, 95, 16 }, 9, secondaryText, true);
        g.setColour (insetColour); g.fillRoundedRectangle (788, 238, 294, 273, 8);
        g.setColour (borderColour); g.drawRoundedRectangle (788, 238, 294, 273, 8, 1);
        if (const auto* entry = selectedEntry())
        {
            g.setColour (primaryText); g.setFont (font (17, true));
            g.drawFittedText (entry->name, { 808, 255, 254, 45 }, juce::Justification::topLeft, 2);
            drawText (g, entry->category + "   /   " + entry->source, { 808, 307, 254, 20 }, 11, mint);
            g.setColour (secondaryText); g.setFont (font (10.5f));
            g.drawFittedText (entry->factoryIndex >= 0 ? "A factory starting point. Load it and make it your own; SAVE keeps your changes as a user preset."
                                                      : entry->file.getRelativePathFrom (library.getDirectory()),
                              { 808, 339, 254, 42 }, juce::Justification::topLeft, 3);
            const bool playing = entry->id == processor.getLoadedPresetId();
            drawText (g, playing ? "CURRENTLY PLAYING" : "SELECTED / NOT LOADED", { 808, 390, 254, 18 }, 9.5f,
                      playing ? mint : orange, true);
            g.setColour (secondaryText); g.setFont (font (10.5f));
            g.drawFittedText (playing ? "The keyboard plays this sound.\nSelecting another row leaves it playing."
                                     : "Press Return or double-click the row,\nor choose LOAD SOUND to hear it.",
                              { 808, 463, 254, 34 }, juce::Justification::topLeft, 2);
        }
        else
        {
            drawText (g, "NO SOUND SELECTED", { 808, 258, 254, 24 }, 13, primaryText, true);
            g.setColour (secondaryText); g.setFont (font (11));
            g.drawFittedText ("Change your search or filters. Save your own .anyvoc sounds in the user folder to grow this collection.",
                              { 808, 302, 254, 80 }, juce::Justification::topLeft, 4);
        }
        const auto count = juce::String ((int) entries.size()) + (entries.size() == 1 ? " sound" : " sounds");
        const auto message = feedback.isNotEmpty() ? feedback : scanStatus.isNotEmpty() ? scanStatus
                                                               : "Select a sound; load it when you are ready to play.";
        drawText (g, count + (wasScanning ? "  /  Scanning user presets..." : "  /  Selection does not change the sound."),
                  { 42, 523, 730, 18 }, 10, mint);
        drawText (g, message, { 42, 547, 730, 18 }, 10, feedbackError ? orange : secondaryText);
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (! entries.empty()) return;
        drawText (g, wasScanning ? "SCANNING YOUR LIBRARY" : "NO MATCHING SOUNDS", { 80, 337, 654, 26 },
                  16, primaryText, true, juce::Justification::centred);
        drawText (g, "Try a different search, turn off FAVOURITES ONLY, or choose All sources.",
                  { 80, 373, 654, 24 }, 11, secondaryText, false, juce::Justification::centred);
    }
private:
    void setup (juce::TextButton& button, const juce::String& text, juce::Rectangle<int> bounds,
                std::function<void()> action, juce::String tooltip)
    {
        addAndMakeVisible (button); button.setButtonText (text); button.setBounds (bounds);
        button.onClick = std::move (action); button.setTooltip (tooltip); button.setWantsKeyboardFocus (false);
    }
    void setupChoice (juce::ComboBox& box, const juce::String& id, const juce::String& title,
                      juce::Rectangle<int> bounds, const juce::StringArray& items)
    {
        addAndMakeVisible (box); box.setBounds (bounds); box.addItemList (items, 1);
        box.setSelectedId (1, juce::dontSendNotification); box.setComponentID (id); box.setTitle (title);
        box.onChange = [this] { applyFilter(); };
    }
    void rebuildCategories()
    {
        const auto old = category.getSelectedId() > 1 ? category.getText() : juce::String();
        juce::StringArray categories;
        for (const auto& entry : library.getEntries()) if (entry.category.isNotEmpty()) categories.addIfNotAlreadyThere (entry.category, true);
        categories.sort (true); category.clear (juce::dontSendNotification); category.addItem ("All categories", 1);
        category.addItemList (categories, 2);
        const int index = categories.indexOf (old, true);
        category.setSelectedId (index < 0 ? 1 : index + 2, juce::dontSendNotification);
    }
    void applyFilter (bool clearFeedback = true)
    {
        if (clearFeedback) { feedback.clear(); feedbackError = false; }
        const auto preservedId = selectedId.isEmpty() ? processor.getLoadedPresetId() : selectedId;
        const auto chosenSource = source.getSelectedId() <= 1 ? juce::String() : source.getText();
        const auto chosenCategory = category.getSelectedId() <= 1 ? juce::String() : category.getText();
        entries = library.query (search.getText(), chosenCategory, chosenSource, favouritesOnly.getToggleState(),
                                 sort.getSelectedId() == 2 ? anyvoc::PresetSort::category : anyvoc::PresetSort::name);
        updatingSelection = true; results.updateContent();
        selectId (preservedId);
        updatingSelection = false; updateDetailControls(); repaint();
    }
    void selectId (const juce::String& id)
    {
        int index = -1;
        for (size_t i = 0; i < entries.size(); ++i) if (entries[i].id == id) { index = (int) i; break; }
        if (index < 0 && ! entries.empty()) index = 0;
        if (index < 0) { results.deselectAllRows(); selectedId.clear(); }
        else { results.selectRow (index); selectedId = entries[(size_t) index].id; }
        updateDetailControls();
    }
    const anyvoc::PresetEntry* selectedEntry() const
    {
        const int row = results.getSelectedRow();
        return juce::isPositiveAndBelow (row, (int) entries.size()) ? &entries[(size_t) row] : nullptr;
    }
    void updateDetailControls()
    {
        const auto* entry = selectedEntry(); loadButton.setEnabled (entry != nullptr); favourite.setEnabled (entry != nullptr);
        favourite.setButtonText (entry != nullptr && entry->favourite ? "FAVOURITED" : "FAVOURITE");
        favourite.setToggleState (entry != nullptr && entry->favourite, juce::dontSendNotification);
        previous.setEnabled (! entries.empty()); next.setEnabled (! entries.empty());
    }
    void selectRelative (int delta)
    {
        if (entries.empty()) return;
        const int count = (int) entries.size(), current = results.getSelectedRow();
        results.selectRow (current < 0 ? 0 : (current + delta + count) % count);
    }
    void loadSelected()
    {
        if (const auto* entry = selectedEntry())
        {
            const auto selected = *entry;
            const bool loaded = loadEntry (selected);
            feedback = loaded ? "Loaded " + selected.name + ". Play the keyboard below to audition it."
                              : "Could not load " + selected.name + ". The file may have moved or be invalid; your current sound is unchanged.";
            feedbackError = ! loaded; refresh(); results.repaint(); repaint();
        }
    }
    void toggleFavourite (const juce::String& id, bool desired)
    {
        const auto result = library.setFavourite (id, desired);
        feedback = result.wasOk() ? desired ? "Added to your favourites." : "Removed from your favourites."
                                 : "Could not save favourites: " + result.getErrorMessage();
        feedbackError = result.failed(); refresh(); repaint();
    }
    static void drawStar (juce::Graphics& g, juce::Point<float> centre, bool filled)
    {
        juce::Path star;
        for (int point = 0; point < 10; ++point)
        {
            const float angle = juce::MathConstants<float>::twoPi * (float) point / 10.0f - juce::MathConstants<float>::halfPi;
            const float radius = point % 2 == 0 ? 7.0f : 3.1f;
            const auto position = centre + juce::Point<float> (std::cos (angle), std::sin (angle)) * radius;
            if (point == 0) star.startNewSubPath (position); else star.lineTo (position);
        }
        star.closeSubPath(); g.setColour (filled ? orange : secondaryText.withAlpha (0.7f));
        if (filled) g.fillPath (star); else g.strokePath (star, juce::PathStrokeType (1.1f));
    }
    int getNumRows() override { return (int) entries.size(); }
    juce::String getNameForRow (int row) override
    {
        if (! juce::isPositiveAndBelow (row, (int) entries.size())) return {};
        const auto& entry = entries[(size_t) row];
        return entry.name + " / " + entry.category + " / " + entry.source + (entry.favourite ? " / Favourite" : "");
    }
    juce::String getTooltipForRow (int row) override
    {
        if (! juce::isPositiveAndBelow (row, (int) entries.size())) return {};
        return getNameForRow (row) + ". Click the star to favourite; select then press Return, or double-click the name, to load.";
    }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) entries.size())) return;
        const auto& entry = entries[(size_t) row]; const bool playing = entry.id == processor.getLoadedPresetId();
        g.fillAll (selected ? mint.withAlpha (0.11f) : row % 2 == 0 ? insetColour : cardColour.withAlpha (0.4f));
        if (selected) { g.setColour (mint.withAlpha (0.65f)); g.fillRect (0, 0, 2, height); }
        drawStar (g, { 19, (float) height * 0.5f }, entry.favourite);
        drawText (g, entry.name, { 46, 0, 328, height }, 12, selected ? primaryText : secondaryText, selected);
        drawText (g, entry.category, { 390, 0, 175, height }, 10.5f, secondaryText);
        drawText (g, entry.source, { 580, 0, 95, height }, 9.5f, entry.factoryIndex >= 0 ? lilac : blue);
        if (playing) { g.setColour (mint); g.fillEllipse ((float) width - 17, (float) height * 0.5f - 2.5f, 5, 5); }
        g.setColour (borderColour.withAlpha (0.35f)); g.drawHorizontalLine (height - 1, 10, (float) width - 10);
    }
    void selectedRowsChanged (int) override
    {
        if (const auto* entry = selectedEntry()) selectedId = entry->id; else selectedId.clear();
        if (! updatingSelection) { feedback.clear(); feedbackError = false; }
        updateDetailControls(); repaint();
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& event) override
    {
        if (event.position.x < 37 && juce::isPositiveAndBelow (row, (int) entries.size()))
        {
            const auto entry = entries[(size_t) row]; toggleFavourite (entry.id, ! entry.favourite);
        }
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent& event) override
    {
        if (event.position.x >= 37 && juce::isPositiveAndBelow (row, (int) entries.size()))
        {
            results.selectRow (row); loadSelected();
        }
    }
    void returnKeyPressed (int) override { loadSelected(); }
    bool keyPressed (const juce::KeyPress& key, juce::Component* origin) override
    {
        if (key == juce::KeyPress::escapeKey) { setVisible (false); return true; }
        if (origin == &search && (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey))
        {
            selectRelative (key == juce::KeyPress::downKey ? 1 : -1); return true;
        }
        return false;
    }
    AnyvocAudioProcessor& processor;
    anyvoc::PresetLibrary& library;
    std::function<bool (const anyvoc::PresetEntry&)> loadEntry;
    juce::TextEditor search;
    juce::ComboBox source, category, sort;
    juce::TextButton favouritesOnly, done, refreshButton, openFolder, favourite, loadButton, previous, next;
    juce::ListBox results;
    std::vector<anyvoc::PresetEntry> entries;
    juce::String selectedId, knownLoadedId, knownLoadedName, scanStatus, feedback;
    std::uint64_t knownRevision = std::numeric_limits<std::uint64_t>::max();
    bool wasScanning = false, feedbackError = false, updatingSelection = false;
};

class HelpOverlay final : public juce::Component
{
public:
    HelpOverlay()
    {
        close.setButtonText ("GOT IT");
        close.onClick = [this] { setVisible (false); };
        addAndMakeVisible (close);
        close.setBounds (830, 168, 90, 30);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.65f));
        g.setColour (cardColour); g.fillRoundedRectangle (180, 145, 760, 466, 18);
        g.setColour (borderColour); g.drawRoundedRectangle (180, 145, 760, 466, 18, 1);
        drawText (g, "MAKE YOUR FIRST SOUND", { 210, 171, 580, 26 }, 21, primaryText, true);
        drawText (g, "Start by listening. Open the full editors when you want to go further.", { 210, 206, 680, 22 }, 13);
        const juce::StringArray titles { "01   CHOOSE A SOUND & PLAY", "02   SHAPE WHAT YOU HEAR", "03   MAKE IT MOVE", "04   BUILD YOUR OWN INSTRUMENT" };
        const juce::StringArray descriptions {
            "On PLAY, choose a sound in BROWSE, then LOAD SOUND. Click the piano keys or send MIDI.\nYour keyboard, output volume and performance macros stay available in every editor.",
            "TONE changes Filter A. NOTE SHAPE sets how a note starts, holds and fades away.\nSPACE adds echoes and reverb. These are the same parameters as the full editors.",
            "Drag an LFO chip above onto CUTOFF. Drag the coloured outer ring to set its depth.\nMOD SOURCES offers more sources; MOD opens curves and the complete routing matrix.",
            "SYNTH chooses waveforms, samples and granular layers. FILTER shapes their spectrum; FX adds space.\nUse INIT for a clean starting point. SAVE keeps your sound; UNDO lets you explore freely."
        };
        for (int i = 0; i < 4; ++i)
        {
            const int y = 251 + i * 76;
            drawText (g, titles[i], { 210, y, 680, 20 }, 12, i % 2 == 0 ? mint : lilac, true);
            g.setColour (secondaryText); g.setFont (font (12));
            g.drawFittedText (descriptions[i], { 210, y + 23, 690, 40 }, juce::Justification::topLeft, 2);
        }
        drawText (g, "TIP   UNDO / REDO restore sound edits (Ctrl+Z / Ctrl+Y). Text fields keep their own undo.",
                  { 210, 569, 680, 22 }, 11, primaryText);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! juce::Rectangle<int> (180, 145, 760, 466).contains (e.getPosition())) setVisible (false);
    }
private:
    juce::TextButton close;
};
}

class AnyvocAudioProcessorEditor::Interface final : public juce::Component,
                                                  public juce::DragAndDropContainer,
                                                  private juce::Timer
{
public:
    explicit Interface (AnyvocAudioProcessor& processorIn, juce::File libraryDirectory, juce::File librarySettings,
                        std::shared_ptr<LfoCurveServices> services)
        : processor (processorIn),
          presetLibrary (processorIn.getPresetNames(), libraryDirectory == juce::File() ? anyvoc::PresetLibrary::defaultPresetDirectory() : libraryDirectory,
                         librarySettings == juce::File() ? anyvoc::PresetLibrary::defaultSettingsFile() : librarySettings),
          curveServices (services != nullptr ? std::move (services) : std::make_shared<DefaultLfoCurveServices>()),
          keyboard (processor.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard),
          tooltip (this, 650)
    {
        setLookAndFeel (&lookAndFeel);
        for (auto* page : { &synthPage, &modPage, &fxPage, &filterPage, &playPage })
        {
            addAndMakeVisible (page);
            page->setBounds (0, 0, designWidth, designHeight);
            page->setInterceptsMouseClicks (false, true);
        }
        configureHeader();
        buildSynth();
        buildModulation();
        buildEffects();
        buildFilters();
        buildPlay();
        buildFooter();
        buildSourceDetails();
        buildLfoDetails();
        envelopeLab = std::make_unique<EnvelopeLab> (processor);
        addChildComponent (*envelopeLab);
        sourcePalette = std::make_unique<ModulationSourcePalette>();
        sourcePalette->onSelect = [this] (int source) { selectModulationSource (source); };
        addChildComponent (*sourcePalette);
        voicingPanel = std::make_unique<VoicingPanel> (processor);
        addChildComponent (*voicingPanel);
        browser = std::make_unique<PresetBrowser> (processor, presetLibrary, [this] (const anyvoc::PresetEntry& entry) { return loadLibraryEntry (entry); });
        addChildComponent (*browser);
        refreshLfoCards();
        refreshEnvelopeControls();
        refreshRouteIndicators();
        previousProgram = processor.getCurrentProgram();
        refreshPresetIdentity();
        addChildComponent (help);
        help.setBounds (0, 0, designWidth, designHeight);
        showPage (4);
        setSize (designWidth, designHeight);
        startTimerHz (24);
    }

    ~Interface() override
    {
        stopTimer();
        chooser.reset();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (background);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c2634), 0, 0, background, 680, 260, false));
        g.fillRect (0, 0, designWidth, 110);
        drawText (g, "ANYVOC", { 24, 17, 134, 31 }, 27, primaryText, true);
        drawText (g, "HYBRID INSTRUMENT", { 27, 46, 132, 14 }, 9.5f, mint);
        g.setColour (borderColour.withAlpha (0.7f)); g.drawHorizontalLine (68, 24, 1096);
        if (currentPage == 0) paintSynth (g);
        else if (currentPage == 1) paintModulation (g);
        else if (currentPage == 2) paintEffects (g);
        else if (currentPage == 3) paintFilters (g);
        else paintPlay (g);
        paintFooter (g);
    }

    bool handleHistoryKey (const juce::KeyPress& key)
    {
        // Even an empty text undo stack belongs to that field. Never fall
        // through from a search/name/value edit into whole-sound history.
        for (auto* focus = juce::Component::getCurrentlyFocusedComponent(); focus != nullptr; focus = focus->getParentComponent())
            if (dynamic_cast<juce::TextEditor*> (focus) != nullptr) return false;
        const auto modifiers = key.getModifiers();
        if ((! modifiers.isCommandDown() && ! modifiers.isCtrlDown()) || modifiers.isAltDown()) return false;
        const int letter = key.getKeyCode();
        if (letter != 'z' && letter != 'Z' && letter != 'y' && letter != 'Y') return false;
        performHistory (letter == 'y' || letter == 'Y' || modifiers.isShiftDown());
        return true;
    }

private:
    void refreshHistoryControls()
    {
        const bool active = processor.isUserEditActive();
        const auto revision = processor.getHistoryRevision();
        if (revision == displayedHistoryRevision && active == historyWasActive) return;
        displayedHistoryRevision = revision; historyWasActive = active;
        soundUndo.setEnabled (! active && processor.canUndo());
        soundRedo.setEnabled (! active && processor.canRedo());
        soundUndo.setTooltip (active ? "Finish the current edit before undoing."
                             : processor.canUndo() ? "Undo " + processor.getUndoName() + ". Ctrl+Z (Cmd+Z on macOS)."
                                                   : "No sound edits to undo. Change a control, draw a curve, or load a sound to begin.");
        soundRedo.setTooltip (active ? "Finish the current edit before redoing."
                             : processor.canRedo() ? "Redo " + processor.getRedoName() + ". Ctrl+Y or Ctrl+Shift+Z."
                                                   : "No sound edits to redo. Making another sound edit clears redo.");
    }

    void performHistory (bool forward)
    {
        const juce::Component::SafePointer<Interface> safe (this);
        if (processor.isUserEditActive())
        {
            status = "Finish the current drag or stroke before using Undo or Redo.";
            refreshHistoryControls(); repaint(); return;
        }
        // Clicking a history button explicitly commits an open numeric/name
        // field first; keyboard shortcuts above leave native text undo alone.
        if (auto* focus = juce::Component::getCurrentlyFocusedComponent())
            if (dynamic_cast<juce::TextEditor*> (focus) != nullptr && isParentOf (focus))
                for (auto* parent = focus->getParentComponent(); parent != nullptr; parent = parent->getParentComponent())
                    if (auto* label = dynamic_cast<juce::Label*> (parent)) { label->hideEditor (false); break; }
        std::array<uint64_t, 3> revisions{};
        for (int i = 0; i < 3; ++i) revisions[(size_t) i] = processor.getWavetableRevision (i);
        const auto name = forward ? processor.getRedoName() : processor.getUndoName();
        const auto result = forward ? processor.redoUserEdit() : processor.undoUserEdit();
        bool closedTable = false;
        for (int i = 0; i < 3; ++i)
            if (processor.getWavetableRevision (i) != revisions[(size_t) i] && wavetableDetails[(size_t) i] != nullptr)
                closedTable = wavetableDetails[(size_t) i]->dismissIfAssetChanged() || closedTable;
        status = result.wasOk() ? (forward ? "Redid " : "Undid ") + name + "." : result.getErrorMessage();
        if (closedTable) status += " Table restored; reopen EDIT to continue drawing.";
        previousProgram = processor.getCurrentProgram();
        refreshPresetIdentity(); refreshHistoryControls(); refreshRouteIndicators();
        if (safe == nullptr || ! refreshLfoCards() || ! refreshEnvelopeControls()) return;
        if (envelopeLab != nullptr && envelopeLab->isVisible() && ! envelopeLab->refresh()) return;
        for (auto& detail : lfoDetails) if (detail->isVisible() && ! detail->refresh()) return;
        if (browser != nullptr) browser->refresh();
        if (voicingPanel != nullptr) voicingPanel->refresh();
        repaint();
    }

    Knob& knob (juce::Component& page, const juce::String& id, juce::String name,
                juce::String tip, Unit unit, juce::Rectangle<int> bounds, juce::Colour accent = mint)
    {
        auto control = std::make_unique<Knob> (processor, id, std::move (name), std::move (tip), unit, accent);
        page.addAndMakeVisible (*control);
        control->setBounds (bounds);
        auto& reference = *control;
        if (id == "cutoff") registerModulationTarget (reference, 1);
        if (id == "filter2Cutoff") registerModulationTarget (reference, 8);
        if (id == "resonance") registerModulationTarget (reference, 9);
        if (id == "filter2Resonance") registerModulationTarget (reference, 10);
        if (id == "filterBalance") registerModulationTarget (reference, 11);
        for (int i = 0; i < 3; ++i)
            if (id == "osc" + juce::String (i + 1) + "Position") registerModulationTarget (reference, i + 2);
        knobs.push_back (std::move (control));
        return reference;
    }

    Choice& choice (juce::Component& page, const juce::String& id, juce::String name,
                    const juce::StringArray& options, juce::String tip, juce::Rectangle<int> bounds, std::function<bool()> allowed = {})
    {
        auto control = std::make_unique<Choice> (processor, id, std::move (name), options, std::move (tip), std::move (allowed));
        page.addAndMakeVisible (*control);
        control->setBounds (bounds);
        auto& reference = *control;
        choices.push_back (std::move (control));
        return reference;
    }

    void button (juce::Component& parent, juce::TextButton& control, const juce::String& text,
                 juce::Rectangle<int> bounds, std::function<void()> action, juce::String tip = {})
    {
        parent.addAndMakeVisible (control); control.setButtonText (text); control.setBounds (bounds);
        control.onClick = std::move (action); control.setTooltip (tip); control.setWantsKeyboardFocus (false);
    }

    void configureHeader()
    {
        tempoDisplay = std::make_unique<TempoDisplay> (processor);
        addAndMakeVisible (*tempoDisplay); tempoDisplay->setBounds (172, 17, 110, 41);
        button (*this, presetName, processor.getLoadedPresetName(), { 330, 21, 186, 32 }, [this] { openPresetBrowser(); },
                "Currently loaded sound. Open the preset library to search, favourite and load factory or user sounds.");
        presetName.setComponentID ("loadedPresetName");
        button (*this, browseButton, "BROWSE", { 524, 21, 68, 32 }, [this] { openPresetBrowser(); },
                "Browse factory and user presets. Selecting a result does not load it until you choose LOAD SOUND.");
        browseButton.setComponentID ("presetBrowse");
        button (*this, previousPreset, "<", { 294, 21, 28, 32 }, [this] { stepPreset (-1); }, "Load the previous sound in the current browser results.");
        button (*this, nextPreset, ">", { 600, 21, 28, 32 }, [this] { stepPreset (1); }, "Load the next sound in the current browser results.");
        button (*this, save, "SAVE", { 644, 21, 60, 32 }, [this] { saveSound(); }, "Save this sound as an .anyvoc preset");
        button (*this, load, "LOAD", { 712, 21, 60, 32 }, [this] { loadSound(); }, "Load a saved .anyvoc preset");
        button (*this, init, "INIT", { 780, 21, 54, 32 }, [this]
        {
            dismissWavetableDetails(); processor.selectFactoryPreset (0); refreshPresetIdentity();
            previousProgram = processor.getCurrentProgram();
            status = "Initialized. Your next sound starts here.";
        }, "Reset all sound controls to the initial preset");
        button (*this, soundUndo, "UNDO", { 846, 21, 62, 32 }, [this] { performHistory (false); });
        button (*this, soundRedo, "REDO", { 916, 21, 62, 32 }, [this] { performHistory (true); });
        soundUndo.setComponentID ("soundUndo"); soundRedo.setComponentID ("soundRedo");
        soundUndo.setTitle ("Undo sound edit"); soundRedo.setTitle ("Redo sound edit");
        refreshHistoryControls();
        button (*this, helpButton, "?  GUIDE", { 990, 21, 106, 32 }, [this]
        {
            if (! dismissEnvelopeLab()) return;
            help.setVisible (true); help.toFront (false);
        }, "A quick guide to making your first sound");
        // Keep the editor indices stable; order navigation around the sound-design workflow.
        const juce::StringArray names { "SYNTH", "MOD", "FX", "FILTER", "PLAY" };
        const juce::StringArray ids { "pageSynth", "pageMod", "pageFx", "pageFilter", "pagePlay" };
        const juce::StringArray tips { "Choose and layer sound sources; edit waves, samples, granular texture and voicing.",
            "Make parameters move with envelopes and LFOs; inspect and edit every modulation route.",
            "Finish your sound with saturation, chorus, delay and reverb.",
            "Shape tone with two filters and choose how each source passes through them.",
            "Play and shape a sound with the essential controls. All full editors remain one click away." };
        constexpr std::array<int, 5> order { 4, 0, 3, 1, 2 };
        for (int position = 0; position < 5; ++position)
        {
            const int i = order[(size_t) position];
            button (*this, tabs[(size_t) i], names[i], { 24 + position * 80, 78, 72, 27 }, [this, i] { showPage (i); }, tips[i]);
            tabs[(size_t) i].setComponentID (ids[i]);
            tabs[(size_t) i].setRadioGroupId (100);
            tabs[(size_t) i].setClickingTogglesState (true);
        }
        button (*this, sourcePaletteButton, "MOD SOURCES", { 432, 78, 102, 27 }, [this] { openModulationSources(); },
                "Open all ten LFOs, four envelopes, eight macros and performance sources. Drag a source onto a knob to assign it.");
        sourcePaletteButton.setComponentID ("openModulationSources");
        const juce::StringArray sourceNames { "LFO 1", "LFO 2", "FILTER ENV", "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4" };
        const std::array<int, 7> sourceIds { 1, 2, 3, 6, 7, 8, 9 };
        for (int i = 0; i < 7; ++i)
        {
            auto chip = std::make_unique<ModulationChip> (sourceIds[(size_t) i], sourceNames[i], i % 2 == 0 ? mint : lilac);
            chip->onSelect = [this] (int source) { selectModulationSource (source); };
            addAndMakeVisible (*chip); chip->setBounds (542 + i * 80, 78, 74, 27);
            sourceChips.push_back (std::move (chip));
        }
    }

    void buildPlay()
    {
        playPage.setComponentID ("playPage");
        playPage.setTitle ("Play and shape your sound");
        button (playPage, playBrowse, "CHOOSE A SOUND", { 862, 135, 218, 36 }, [this] { openPresetBrowser(); },
                "Browse sounds by name or category, then choose LOAD SOUND. Your current sound stays until you load another.");
        playBrowse.setComponentID ("playBrowse");
        button (playPage, playFilters, "EDIT", { 288, 215, 64, 25 }, [this] { showPage (3); },
                "Open both filters and their routing. Filter A affects only the sources sent to it.");
        playFilters.setComponentID ("playFilters");
        playPage.addAndMakeVisible (playFilterEnabled);
        playFilterEnabled.setBounds (211, 215, 70, 25);
        playFilterEnabled.setComponentID ("playFilterEnabled");
        playFilterEnabled.setTitle ("Enable Filter A");
        playFilterEnabled.setTooltip ("Enable or bypass Filter A. This does not change source routing or Filter B.");
        playFilterAttachment = std::make_unique<SoundButtonAttachment> (processor, "filterEnabled", playFilterEnabled);
        playCutoff = &knob (playPage, "cutoff", "CUTOFF", "Filter A frequency. With a low-pass filter, turn left for a darker sound and right for a brighter sound.",
                            Unit::hertz, { 58, 272, 130, 137 });
        playCutoff->slider.setComponentID ("playCutoff");
        playCutoff->setComponentID ("playCutoffControl");
        playCutoff->ring.setComponentID ("playCutoffModulationRing");
        playResonance = &knob (playPage, "resonance", "RESONANCE", "Emphasize the frequencies around Filter A's cutoff. Try small amounts first.",
                               Unit::percent, { 212, 272, 130, 137 });
        playResonance->slider.setComponentID ("playResonance");
        playResonance->setComponentID ("playResonanceControl");
        playResonance->ring.setComponentID ("playResonanceModulationRing");
        for (auto* control : { playCutoff, playResonance })
        {
            const juce::Component::SafePointer<Knob> target (control);
            control->slider.userEditAllowed = [owner = &processor, target]
            { return target != nullptr && matrixHierarchyVisible (target.getComponent()) && value (*owner, "filterEnabled") > .5f; };
        }
        button (playPage, playEnvelopeEdit, "EDIT", { 652, 215, 64, 25 }, [this] { openEnvelopeLab (0); },
                "Open the full amplitude envelope, including curves and beat-synchronized stage lengths.");
        playEnvelopeEdit.setComponentID ("playEnvelopeEdit");
        playEnvelope = std::make_unique<EnvelopeDisplay> (processor, "amp", lilac);
        playPage.addAndMakeVisible (*playEnvelope);
        playEnvelope->setBounds (407, 266, 310, 52);
        playEnvelope->onOpen = [this] { openEnvelopeLab (0); };
        const auto firstEnvelopeKnob = knobs.size();
        envelopeKnobs (playPage, "amp", 398, 332, 80, 89, lilac);
        const juce::StringArray envelopeIds { "playAttack", "playDecay", "playSustain", "playRelease" };
        for (int i = 0; i < 4; ++i) knobs[firstEnvelopeKnob + (size_t) i]->slider.setComponentID (envelopeIds[i]);
        button (playPage, playEffects, "EDIT", { 1016, 215, 64, 25 }, [this] { showPage (2); },
                "Open all effects, including echo timing, feedback, reverb size, chorus and saturation.");
        playEffects.setComponentID ("playEffects");
        auto& echo = knob (playPage, "delayMix", "ECHO", "Delay mix: blend rhythmic repeats with your dry sound. EDIT opens delay time and feedback.",
                           Unit::percent, { 786, 272, 130, 137 }, blue);
        echo.slider.setComponentID ("playEcho");
        auto& space = knob (playPage, "reverbMix", "REVERB", "Reverb mix: blend a room or long tail with your dry sound. EDIT opens room size.",
                            Unit::percent, { 940, 272, 130, 137 }, blue);
        space.slider.setComponentID ("playReverb");

        const juce::StringArray labels { "SOURCES / SYNTH", "TONE / FILTER", "MOVEMENT / MOD", "FINISH / FX" };
        const juce::StringArray ids { "playOpenSynth", "playOpenFilter", "playOpenMod", "playOpenFx" };
        constexpr std::array<int, 4> destinations { 0, 3, 1, 2 };
        for (int i = 0; i < 4; ++i)
        {
            const int destination = destinations[(size_t) i];
            button (playPage, playEditors[(size_t) i], labels[i], { 42 + i * 264, 537, 248, 27 },
                    [this, destination] { showPage (destination); }, "Open the full editor. Switching pages keeps every sound setting.");
            playEditors[(size_t) i].setComponentID (ids[i]);
        }
        refreshPlay();
    }

    bool refreshPlay()
    {
        const juce::Component::SafePointer<Interface> safe (this);
        const bool enabled = value (processor, "filterEnabled") > .5f;
        playFilterEnabled.setButtonText (enabled ? "ON" : "OFF");
        for (auto* control : { playCutoff, playResonance })
        {
            // JUCE does not end a drag on mouse-up once its slider is disabled.
            // Finish the sound edit first if automation bypassed A mid-gesture.
            if (! enabled && control->slider.onEditCancelled)
            {
                const auto cancel = control->slider.onEditCancelled;
                cancel();
                if (safe == nullptr) return false;
            }
            control->setEnabled (enabled);
            if (safe == nullptr) return false;
            control->setAlpha (enabled ? 1.0f : 0.45f);
        }
        playEnvelope->repaint();
        repaint (24, 117, 1076, 461);
        return true;
    }

    void paintPlay (juce::Graphics& g)
    {
        drawCard (g, { 24, 117, 1076, 74 }, "", "", mint);
        drawText (g, processor.getLoadedPresetName(), { 48, 130, 792, 28 }, 23, primaryText, true);
        drawText (g, "Choose a sound. Play a note. Make it yours.", { 48, 160, 792, 18 }, 12);
        drawCard (g, { 24, 204, 348, 270 }, "TONE", "", mint);
        const bool enabled = value (processor, "filterEnabled") > .5f;
        const juce::StringArray types { "Low pass / brightness", "High pass / low end", "Band pass / focus" };
        drawText (g, enabled ? "Filter A: " + types[juce::jlimit (0, 2, (int) value (processor, "filterType"))]
                            : "Filter A bypassed / switch ON to shape tone", { 42, 247, 312, 18 }, 11);
        drawText (g, enabled ? "Drag an LFO chip onto CUTOFF to add movement."
                            : "Your filter settings are kept while bypassed.", { 42, 431, 312, 18 }, 10.5f, mint);
        drawText (g, "Only sources routed through Filter A are affected.", { 42, 450, 312, 15 }, 9.5f);
        drawCard (g, { 388, 204, 348, 270 }, "NOTE SHAPE", "Amplitude envelope / how a note unfolds", lilac);
        const bool synced = value (processor, "ampTempoSync") > .5f;
        drawText (g, synced ? "BPM timing is on. Use EDIT to change stage lengths."
                           : "ATTACK: fade in. RELEASE: fade out after a key.", { 406, 431, 312, 18 }, 10.5f, lilac);
        drawText (g, "SUSTAIN sets the level while you hold a note.", { 406, 450, 312, 15 }, 9.5f);
        drawCard (g, { 752, 204, 348, 270 }, "SPACE", "Delay and reverb / the final atmosphere", blue);
        drawText (g, "ECHO adds repeats. REVERB adds a room or tail.", { 770, 431, 312, 18 }, 10.5f, blue);
        drawText (g, "Start with a little; keep the original sound clear.", { 770, 450, 312, 15 }, 9.5f);
        drawCard (g, { 24, 486, 1076, 92 }, "GO DEEPER", "", blue);
        drawText (g, "One instrument. Open any editor to explore the same sound in more detail.", { 204, 500, 872, 20 }, 11);
    }

    void buildSynth()
    {
        const std::array<juce::Colour, 3> colours { mint, lilac, blue };
        for (int i = 0; i < 3; ++i)
        {
            const auto prefix = "osc" + juce::String (i + 1);
            const int x = 24 + i * 364;
            const auto colour = colours[(size_t) i];
            waves[(size_t) i] = std::make_unique<WaveDisplay> (processor, i, colour);
            synthPage.addAndMakeVisible (*waves[(size_t) i]);
            waves[(size_t) i]->setBounds (x + 16, 170, 316, 51);
            waves[(size_t) i]->onImport = [this, i] (const juce::File& file) { importWave (i, file); };
            choice (synthPage, prefix + "Mode", "", { "Wavetable", "Sample", "Granular" },
                    "Choose a synthesis mode. EDIT opens the sample and granular controls.", { x + 91, 128, 134, 25 });
            button (synthPage, importButtons[(size_t) i], "WAV", { x + 233, 128, 45, 25 },
                    [this, i] { chooseWave (i); }, "Import WAV into the selected source mode, or drop it onto the waveform.");
            button (synthPage, detailButtons[(size_t) i], "EDIT", { x + 286, 128, 46, 25 },
                    [this, i] { openSourceDetail (i); }, "Wavetable: draw waves and harmonics. Sample / Granular: edit the root note, loop, reverse and grain texture.");
            const juce::StringArray names { "WAVE", "LEVEL", "SEMITONE", "FINE" };
            const juce::StringArray ids { "Position", "Level", "Semi", "Fine" };
            const std::array<Unit, 4> units { Unit::percent, Unit::percent, Unit::semitones, Unit::cents };
            const juce::StringArray tips { "Choose the wavetable frame, sample start, or granular centre. Modulate this for evolving tones.",
                "Volume of this oscillator.", "Tune the oscillator in semitones. 12 semitones equals one octave.",
                "Fine pitch adjustment in cents. 100 cents equals one semitone." };
            for (int k = 0; k < 4; ++k)
            {
                auto& control = knob (synthPage, prefix + ids[k], names[k], tips[k], units[(size_t) k],
                                      { x + 11 + k * 82, 230, 80, 74 }, colour);
                if (k == 0)
                {
                    positionKnobs[(size_t) i] = &control;
                    const int mode = (int) value (processor, prefix + "Mode");
                    control.setCaption (mode == 0 ? "WAVE" : mode == 1 ? "START" : "CENTRE");
                }
            }
            knob (synthPage, prefix + "Unison", "UNISON", "Stack up to seven slightly different voices for a richer sound.",
                  Unit::integer, { x + 11, 307, 80, 73 }, colour);
            knob (synthPage, prefix + "Detune", "DETUNE", "Spread the pitches of stacked unison voices. Increase UNISON first.",
                  Unit::cents, { x + 93, 307, 80, 73 }, colour);
            knob (synthPage, prefix + "Pan", "PAN", "Place this oscillator between the left and right speakers.",
                  Unit::pan, { x + 175, 307, 80, 73 }, colour);
            button (synthPage, warpButtons[(size_t) i], "WARP", { x + 264, 331, 68, 29 },
                    [this, i] { openWarpDetail (i); },
                    "Open two non-destructive waveform transformations. Available in Wavetable mode; Sample and Granular playback bypass warp.");
            warpButtons[(size_t) i].setEnabled (value (processor, prefix + "Mode") < 0.5f);
        }
        choice (synthPage, "filterType", "", { "Low Pass", "High Pass", "Band Pass" },
                "Low Pass removes brightness; High Pass removes bass; Band Pass keeps a narrow band.", { 173, 405, 180, 25 });
        knob (synthPage, "cutoff", "CUTOFF", "Filter frequency. Lower a low-pass cutoff to make the tone darker.",
              Unit::hertz, { 43, 450, 98, 100 });
        knob (synthPage, "resonance", "RESONANCE", "Emphasize the frequencies around the filter cutoff.",
              Unit::percent, { 148, 450, 98, 100 });
        knob (synthPage, "filterEnv", "ENV AMOUNT", "Let the filter envelope open or close the filter on each note.",
              Unit::signedPercent, { 253, 450, 98, 100 });
        button (synthPage, openFiltersButton, "DUAL FILTERS", { 239, 552, 114, 20 }, [this] { showPage (3); },
                "Open both filters, response curves, serial / parallel processing and each source's filter routing.");
        ampEnvelope = std::make_unique<EnvelopeDisplay> (processor, "amp", lilac);
        synthPage.addAndMakeVisible (*ampEnvelope); ampEnvelope->setBounds (408, 437, 305, 36);
        ampEnvelope->onOpen = [this] { openEnvelopeLab (0); };
        button (synthPage, ampEnvelopeEdit, "EDIT", { 642, 405, 75, 25 }, [this] { openEnvelopeLab (0); },
                "Open the amp envelope's timing, curves, BPM synchronization and Legato retrigger controls.");
        ampEnvelopeEdit.setComponentID ("openAmpEnvelopeLab");
        envelopeKnobs (synthPage, "amp", 398, 478, 78, 80, lilac);
        knob (synthPage, "subLevel", "SUB", "Add a sine wave an octave below oscillator A for extra bass.",
              Unit::percent, { 765, 441, 74, 96 }, blue);
        knob (synthPage, "noiseLevel", "NOISE", "Blend in noise for breath, texture and percussion.",
              Unit::percent, { 838, 441, 74, 96 }, blue);
        button (synthPage, voicingButton, "VOICING", { 976, 405, 106, 25 }, [this] { openVoicing(); },
                "Choose chords, mono or legato playing; set glide and each wavetable oscillator's starting phase.");
        voicingButton.setComponentID ("openVoicing");
        synthPage.addAndMakeVisible (arp);
        arp.setButtonText ("ARPEGGIATOR"); arp.setBounds (924, 442, 153, 25);
        arp.setTooltip ("Turn held notes into a rhythmic pattern. The rate follows your DAW tempo.");
        arpAttachment = std::make_unique<SoundButtonAttachment> (processor, "arpEnabled", arp);
        choice (synthPage, "arpRate", "RATE", { "1/4", "1/8", "1/16", "1/32" },
                "Arpeggiator speed, synchronized to your DAW tempo.", { 931, 477, 140, 44 });
        for (int i = 0; i < 2; ++i)
        {
            auto slider = std::make_unique<SoundSlider>();
            slider->setSliderStyle (juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 39, 20);
            styleSliderText (*slider);
            slider->setBounds (967, 524 + i * 25, 111, 20);
            slider->setScrollWheelEnabled (false);
            slider->setColour (juce::Slider::trackColourId, blue);
            slider->setTooltip (i == 0 ? "Extend the arpeggio over one, two or three octaves."
                                      : "Length of each arpeggiator note as a fraction of its step.");
            slider->setTitle (i == 0 ? "Arpeggiator octaves" : "Arpeggiator gate");
            synthPage.addAndMakeVisible (*slider);
            auto attachment = std::make_unique<SoundSliderAttachment>
                                  (processor, i == 0 ? "arpOctaves" : "arpGate", *slider);
            slider->textFromValueFunction = [i] (double n) { return formatted (n, i == 0 ? Unit::integer : Unit::percent); };
            slider->valueFromTextFunction = [i] (const juce::String& text) { return text.getDoubleValue() / (i == 0 ? 1.0 : 100.0); };
            slider->updateText();
            matrixAmounts.push_back (std::move (slider));
            matrixAttachments.push_back (std::move (attachment));
        }
    }

    void envelopeKnobs (juce::Component& page, juce::String prefix, int x, int y, int width, int height, juce::Colour accent)
    {
        const juce::StringArray ids { "Attack", "Decay", "Sustain", "Release" };
        const juce::StringArray tips { "Time to rise from silence to full level after a note starts.",
            "Time to fall from the peak to the sustain level.", "Level held while the key remains pressed.",
            "Time to fade out after releasing a key." };
        for (int i = 0; i < 4; ++i)
        {
            auto& control = knob (page, prefix + ids[i], ids[i].toUpperCase(), tips[i], i == 2 ? Unit::percent : Unit::seconds,
                                  { x + i * width, y, width, height }, accent);
            if (i != 2) configureEnvelopeTimeKnob (control, prefix, i == 0 ? 1 : i == 1 ? 3 : 5);
        }
    }

    void configureEnvelopeTimeKnob (Knob& control, const juce::String& prefix, int stage)
    {
        control.slider.userEditAllowed = [owner = &processor, prefix] { return value (*owner, prefix + "TempoSync") < .5f; };
        control.slider.textFromValueFunction = [owner = &processor, prefix, stage] (double seconds)
        {
            if (value (*owner, prefix + "TempoSync") > .5f)
                return envelopeDivisions[juce::jlimit (0, 13, (int) value (*owner, prefix + envelopeStages[stage] + "Division"))];
            return formatted (seconds, Unit::seconds);
        };
        control.slider.setTooltip (control.slider.getTooltip() + " BPM mode shows its beat division here; open EDIT to adjust synchronized stages.");
        envelopeTimeKnobs.push_back ({ &control, prefix });
    }

    bool refreshEnvelopeControls()
    {
        const juce::Component::SafePointer<Interface> safe (this);
        for (auto& binding : envelopeTimeKnobs)
        {
            const bool synced = value (processor, binding.prefix + "TempoSync") > .5f;
            binding.control->setEnabled (! synced);
            if (safe == nullptr) return false;
            binding.control->setAlpha (synced ? .5f : 1.0f);
            binding.control->slider.updateText();
            if (safe == nullptr) return false;
        }
        return true;
    }

    void buildModulation()
    {
        for (int i = 0; i < 10; ++i)
        {
            const auto accent = i % 2 == 0 ? mint : lilac;
            lfos[(size_t) i] = std::make_unique<LfoDisplay> (processor, i + 1, accent);
            lfos[(size_t) i]->setComponentID ("lfo" + juce::String (i + 1) + "Display");
            modPage.addAndMakeVisible (*lfos[(size_t) i]);
            lfos[(size_t) i]->onOpen = [this, i] { openLfoDetail (i); };
            button (modPage, lfoEditButtons[(size_t) i], "EDIT LFO " + juce::String (i + 1), {},
                    [this, i] { openLfoDetail (i); }, "Draw a custom curve or view real chaos motion. Edit speed, tempo synchronization and trigger mode.");
            lfoEditButtons[(size_t) i].setComponentID ("lfo" + juce::String (i + 1) + "Edit");
            const auto prefix = "lfo" + juce::String (i + 1);
            lfoRateKnobs[(size_t) i] = &knob (modPage, prefix + "Rate", "RATE", "Periodic shapes use cycles per second. Lorenz and Rossler use this as evolution speed, not a repeating frequency.", Unit::hertz, {}, accent);
            lfoRateKnobs[(size_t) i]->slider.userEditAllowed = [owner = &processor, i] { return owner->getLfoRateMode (i) == 0; };
            lfoAudioRateKnobs[(size_t) i] = &knob (modPage, prefix + "AudioRateHz", "AUDIO HZ", "Independent 20-1000 modulation speed. Periodic shapes use Hz; chaos uses evolution speed. Open EDIT to change the rate mode.", Unit::hertz, {}, accent);
            lfoAudioRateKnobs[(size_t) i]->slider.userEditAllowed = [owner = &processor, i] { return owner->getLfoRateMode (i) == 2; };
            const auto speedText = [owner = &processor, i] (double n)
            {
                return lfoIsChaos (owner->getLfoShape (i)) ? juce::String (n, n < 10.0 ? 2 : 1) : formatted (n, Unit::hertz);
            };
            lfoRateKnobs[(size_t) i]->slider.textFromValueFunction = speedText;
            lfoAudioRateKnobs[(size_t) i]->slider.textFromValueFunction = speedText;
            lfoDivisionChoices[(size_t) i] = &choice (modPage, prefix + "Division", "RATE", lfoDivisions,
                                                     "LFO speed synchronized to the host or local tempo. Chaos evolves with the tempo but does not repeat. T is triplet; D is dotted.", {},
                                                     [owner = &processor, i] { return owner->getLfoRateMode (i) == 1; });
            modPage.addAndMakeVisible (lfoSyncButtons[(size_t) i]);
            lfoSyncButtons[(size_t) i].setButtonText ("SYNC");
            lfoSyncButtons[(size_t) i].setTooltip ("Synchronize to a beat division. Switching off resumes Free Hz or the remembered Audio Rate mode; EDIT offers all three modes.");
            lfoSyncAttachments[(size_t) i] = std::make_unique<SoundButtonAttachment> (processor, prefix + "Sync", lfoSyncButtons[(size_t) i]);
            lfoShapeChoices[(size_t) i] = std::make_unique<LfoShapeChoice> (processor, i);
            modPage.addAndMakeVisible (*lfoShapeChoices[(size_t) i]);
        }
        for (int slot = 0; slot < 2; ++slot)
        {
            auto& selector = lfoCardSelectors[(size_t) slot]; modPage.addAndMakeVisible (selector);
            for (int i = 0; i < 10; ++i) selector.addItem ("LFO " + juce::String (i + 1), i + 1);
            selector.setBounds (53 + slot * 364, 128, 106, 25);
            selector.setComponentID ("LfoCardSelect" + juce::String (slot + 1)); selector.setTitle ("Choose LFO for card " + juce::String (slot + 1));
            selector.setTooltip ("Choose any of the ten independent LFOs. Selecting the other visible LFO swaps the two cards.");
            selector.setSelectedItemIndex (slot, juce::dontSendNotification);
            selector.onChange = [this, slot]
            {
                const int selected = lfoCardSelectors[(size_t) slot].getSelectedItemIndex();
                if (selected < 0 || selected >= 10) return;
                const int other = 1 - slot;
                if (selectedLfos[(size_t) other] == selected) selectedLfos[(size_t) other] = selectedLfos[(size_t) slot];
                selectedLfos[(size_t) slot] = selected;
                if (! refreshLfoCards()) return;
                repaint();
            };
        }
        const juce::StringArray prefixes { "amp", "env", "env3", "env4" };
        for (int i = 0; i < 4; ++i)
        {
            auto& page = envelopePages[(size_t) i]; modPage.addAndMakeVisible (page); page.setBounds (752, 117, 348, 188); page.setInterceptsMouseClicks (false, true);
            modEnvelopes[(size_t) i] = std::make_unique<EnvelopeDisplay> (processor, prefixes[i], i == 0 ? lilac : blue);
            modEnvelopes[(size_t) i]->setComponentID (prefixes[i] + "EnvelopeMod");
            page.addAndMakeVisible (*modEnvelopes[(size_t) i]); modEnvelopes[(size_t) i]->setBounds (21, 43, 302, 52);
            modEnvelopes[(size_t) i]->onOpen = [this, i] { openEnvelopeLab (i); };
            const juce::StringArray names { "Attack", "Decay", "Sustain", "Release" };
            for (int k = 0; k < 4; ++k)
            {
                auto& control = knob (page, prefixes[i] + names[k], names[k].toUpperCase(), "Shape this envelope after each note; use it as a source in the modulation matrix.",
                                      k == 2 ? Unit::percent : Unit::seconds, { 13 + k * 78, 106, 78, 74 }, i == 0 ? lilac : blue);
                if (i == 0) control.slider.setComponentID (prefixes[i] + names[k] + "Mod");
                if (k != 2) configureEnvelopeTimeKnob (control, prefixes[i], k == 0 ? 1 : k == 1 ? 3 : 5);
            }
            page.setVisible (i == selectedEnvelope);
        }
        modPage.addAndMakeVisible (envelopeSelector);
        envelopeSelector.addItemList ({ "Filter Envelope", "Amp Envelope", "Envelope 3", "Envelope 4" }, 1);
        envelopeSelector.setComponentID ("EnvelopeSelect"); envelopeSelector.setTitle ("Choose an envelope to edit");
        envelopeSelector.setBounds (781, 128, 210, 25); envelopeSelector.setSelectedItemIndex (0, juce::dontSendNotification);
        button (modPage, modEnvelopeEdit, "EDIT", { 1007, 128, 75, 25 }, [this] { openEnvelopeLab (selectedEnvelope); },
                "Open the selected envelope's six stages, curves, BPM synchronization and Legato retrigger controls.");
        modEnvelopeEdit.setComponentID ("openModEnvelopeLab");
        envelopeSelector.onChange = [this]
        {
            const int selected = envelopeSelector.getSelectedItemIndex();
            if (selected < 0) return;
            selectedEnvelope = selected == 0 ? 1 : selected == 1 ? 0 : selected;
            const juce::Component::SafePointer<Interface> safe (this);
            for (int i = 0; i < 4; ++i)
            {
                envelopePages[(size_t) i].setVisible (i == selectedEnvelope);
                if (safe == nullptr) return;
            }
        };
        modulationMatrix = std::make_unique<ModulationMatrix> (processor);
        modulationMatrix->onStatus = [this] (const juce::String& message) { status = message; repaint(); };
        modPage.addAndMakeVisible (*modulationMatrix);
        modulationMatrix->setBounds (24, 117, 1076, 461);
        refreshLfoCards();
    }

    void buildFilters()
    {
        for (int i = 0; i < 2; ++i)
        {
            const int x = 24 + i * 544;
            const auto accent = i == 0 ? mint : lilac;
            const auto prefix = i == 0 ? juce::String ("filter") : juce::String ("filter2");
            auto& enabled = filterEnabledButtons[(size_t) i];
            filterPage.addAndMakeVisible (enabled); enabled.setButtonText ("ON"); enabled.setBounds (x + 124, 128, 76, 25);
            enabled.setTitle (i == 0 ? "Filter A enabled" : "Filter B enabled");
            enabled.setTooltip ("When switched off, this filter passes audio through unchanged. A disabled parallel branch is still audible as dry audio.");
            filterEnabledAttachments[(size_t) i] = std::make_unique<SoundButtonAttachment>
                (processor, prefix + "Enabled", enabled);
            choice (filterPage, prefix + "Type", "", { "Low Pass", "High Pass", "Band Pass" },
                    "Low Pass removes brightness; High Pass removes bass; Band Pass keeps frequencies around the cutoff.", { x + 212, 128, 170, 25 });
            choice (filterPage, prefix + "Slope", "", { "12 dB/oct", "24 dB/oct" },
                    "24 dB cascades two filter stages for a steeper response. Resonance affects both stages.", { x + 392, 128, 122, 25 });
            filterResponses[(size_t) i] = std::make_unique<FilterResponse> (processor, i, accent);
            filterPage.addAndMakeVisible (*filterResponses[(size_t) i]);
            filterResponses[(size_t) i]->setBounds (x + 18, 166, 496, 127);
            knob (filterPage, i == 0 ? "cutoff" : "filter2Cutoff", "CUTOFF", "Base filter frequency. You can also drag the response curve left and right.",
                  Unit::hertz, { x + 22, 303, 108, 108 }, accent);
            knob (filterPage, i == 0 ? "resonance" : "filter2Resonance", "RESONANCE", "Emphasize the cutoff frequency. Drag the response curve up and down to adjust this.",
                  Unit::percent, { x + 148, 303, 108, 108 }, accent);
            knob (filterPage, i == 0 ? "filterEnv" : "filter2Env", "ENV AMOUNT", "Move cutoff with the shared filter envelope on MOD. Negative amounts close the filter when the envelope rises.",
                  Unit::signedPercent, { x + 274, 303, 108, 108 }, accent);
            knob (filterPage, prefix + "KeyTrack", "KEY TRACK", "Make cutoff follow the played note. At 100%, one octave on the keyboard moves cutoff by one octave.",
                  Unit::percent, { x + 400, 303, 108, 108 }, accent);
        }
        auto& routing = choice (filterPage, "filterRouting", "", { "Serial: A > B", "Serial: B > A", "Parallel: A + B" },
                                "Both sends follow this path. A only / B only never enter the other filter. Parallel balance weights the two branches.", { 180, 447, 179, 25 });
        routing.box.setTitle ("Filter routing");
        filterRoutingDiagram = std::make_unique<FilterRoutingDiagram> (processor);
        filterPage.addAndMakeVisible (*filterRoutingDiagram); filterRoutingDiagram->setBounds (382, 441, 550, 54);
        const juce::StringArray sourceIds { "osc1FilterRoute", "osc2FilterRoute", "osc3FilterRoute", "subFilterRoute", "noiseFilterRoute" };
        const juce::StringArray names { "OSC A", "OSC B", "OSC C", "SUB", "NOISE" };
        for (int i = 0; i < 5; ++i)
            choice (filterPage, sourceIds[i], names[i], { "Both", "A only", "B only", "Bypass" },
                    "Both follows the selected serial / parallel path. A only and B only use just that filter. Bypass reaches the output without either filter.",
                    { 42 + i * 179, 502, 165, 47 });
        parallelBalance = &knob (filterPage, "filterBalance", "PARALLEL MIX", "Parallel mode only: 0% is the A branch, 100% is B. At 50%, the branches have equal weight. A disabled filter passes its branch through unchanged.",
                                 Unit::percent, { 974, 456, 108, 108 }, blue);
    }

    void refreshFilters()
    {
        for (int i = 0; i < 2; ++i)
        {
            const bool enabled = value (processor, i == 0 ? "filterEnabled" : "filter2Enabled") > 0.5f;
            filterEnabledButtons[(size_t) i].setButtonText (enabled ? "ON" : "OFF");
            filterResponses[(size_t) i]->repaint();
        }
        filterRoutingDiagram->repaint();
        parallelBalance->setAlpha (value (processor, "filterRouting") > 1.5f ? 1.0f : 0.48f);
    }

    void buildEffects()
    {
        const std::array<juce::Colour, 4> accents { orange, lilac, mint, blue };
        for (int i = 0; i < 4; ++i)
        {
            const int x = 24 + i * 272;
            effects[(size_t) i] = std::make_unique<EffectDisplay> (processor, i, accents[(size_t) i]);
            fxPage.addAndMakeVisible (*effects[(size_t) i]);
            effects[(size_t) i]->setBounds (x + 18, 186, 220, 96);
        }
        knob (fxPage, "drive", "DRIVE", "Add warm saturation and harmonics. Higher settings make the tone more aggressive.",
              Unit::percent, { 86, 307, 132, 146 }, orange);
        knob (fxPage, "chorusMix", "MIX", "Blend a gently detuned chorus into the dry sound for width and movement.",
              Unit::percent, { 358, 307, 132, 146 }, lilac);
        knob (fxPage, "delayTime", "TIME", "Time between delay repeats, in seconds.",
              Unit::seconds, { 591, 308, 100, 113 });
        knob (fxPage, "delayFeedback", "FEEDBACK", "Amount fed back into the delay. Higher settings produce more repeats.",
              Unit::percent, { 704, 308, 100, 113 });
        knob (fxPage, "delayMix", "MIX", "Balance the dry sound with the echo effect.",
              Unit::percent, { 650, 437, 98, 107 });
        knob (fxPage, "reverbSize", "SIZE", "Size and decay of the simulated acoustic space.",
              Unit::percent, { 862, 308, 100, 113 }, blue);
        knob (fxPage, "reverbMix", "MIX", "Blend the reverb with the dry sound. Start small for a natural result.",
              Unit::percent, { 976, 308, 100, 113 }, blue);
    }

    void buildFooter()
    {
        addAndMakeVisible (macroLiveContext);
        macroLiveContext.setComponentID ("MacroLiveContext");
        macroLiveContext.setBounds (42, 629, 196, 11);
        macroLiveContext.setFont (font (8.5f)); macroLiveContext.setBorderSize (juce::BorderSize<int> (0));
        macroLiveContext.setColour (juce::Label::textColourId, secondaryText);
        macroLiveContext.setInterceptsMouseClicks (false, false);
        macroLiveContext.setTooltip ("EV is the actual effective macro value read by the audio engine. It shows the newest active voice, including release; other notes may differ. With no active voice it shows the smoothed base. The editable knob always sets the saved base value.");
        for (int bank = 0; bank < 2; ++bank)
        {
            auto& page = macroPages[(size_t) bank]; addAndMakeVisible (page); page.setBounds (24, 640, 348, 99); page.setInterceptsMouseClicks (false, true);
            for (int i = 0; i < 4; ++i)
            {
                const int number = bank * 4 + i + 1;
                auto& control = knob (page, "macro" + juce::String (number), "MACRO " + juce::String (number),
                      "Set the saved base value. Drag a source here to modulate Macro " + juce::String (number)
                      + ", then use this macro as a source or auxiliary. Incoming modulation is separate for each voice and feeds the next audio sample; EV is the actual value, not the base knob.",
                      Unit::percent, { 9 + i * 80, 0, 78, 84 }, i % 2 == 0 ? mint : lilac);
                registerModulationTarget (control, anyvoc::macroDestinationId (number - 1));
                const juce::Component::SafePointer<Knob> target (&control);
                control.slider.userEditAllowed = [target]
                { return target != nullptr && matrixHierarchyVisible (target.getComponent()) && target->isEnabled(); };
                auto& live = macroLiveValues[(size_t) number - 1]; page.addAndMakeVisible (live);
                live.setComponentID ("macro" + juce::String (number) + "LiveValue");
                live.setTitle ("Macro " + juce::String (number) + " effective value");
                live.setBounds (9 + i * 80, 86, 78, 12);
                live.setFont (font (9.2f)); live.setJustificationType (juce::Justification::centred);
                live.setBorderSize (juce::BorderSize<int> (0));
                live.setColour (juce::Label::textColourId, i % 2 == 0 ? mint : lilac);
                live.setInterceptsMouseClicks (false, false);
                live.setTooltip (macroLiveContext.getTooltip());
            }
            page.setVisible (bank == 0);
            auto& select = macroBankButtons[(size_t) bank];
            button (*this, select, bank == 0 ? "1 - 4" : "5 - 8", { 250 + bank * 54, 606, 48, 24 }, [this, bank]
            {
                ++copyNavigationGeneration;
                const juce::Component::SafePointer<Interface> safe (this);
                for (int i = 0; i < 2; ++i)
                {
                    macroPages[(size_t) i].setVisible (i == bank);
                    if (safe == nullptr) return;
                    macroBankButtons[(size_t) i].setToggleState (i == bank, juce::dontSendNotification);
                }
            }, "Show this group of four macros. All eight values remain active when you change groups.");
            select.setComponentID ("MacroBank" + juce::String (bank + 1)); select.setToggleState (bank == 0, juce::dontSendNotification);
        }
        refreshMacroValues();
        addAndMakeVisible (keyboard);
        keyboard.setBounds (390, 641, 552, 85);
        keyboard.setAvailableRange (24, 96);
        keyboard.setLowestVisibleKey (48);
        keyboard.setKeyWidth (21);
        keyboard.setOctaveForMiddleC (4);
        keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffc8d0e1));
        keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff202536));
        keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, background);
        keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, mint.withAlpha (0.35f));
        keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, mint.withAlpha (0.7f));
        keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, juce::Colour (0xff465069));
        keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
        keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, cardColour);
        keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, secondaryText);
        button (*this, panicButton, "ALL NOTES OFF", { 816, 606, 126, 24 }, [this]
        {
            processor.panic(); status = "All notes released.";
        }, "Stop all playing notes and clear lingering voices");
        knob (*this, "master", "MASTER", "Final instrument volume. Keep the meter below its red peak region.",
              Unit::percent, { 977, 638, 78, 88 });
    }

    void buildSourceDetails()
    {
        const std::array<juce::Colour, 3> colours { mint, lilac, blue };
        for (int i = 0; i < 3; ++i)
        {
            details[(size_t) i] = std::make_unique<OscillatorDetail>
                (processor, i, colours[(size_t) i], [this, i] { chooseWave (i); },
                 [this, i] (const juce::File& file) { importWave (i, file); });
            addChildComponent (*details[(size_t) i]);
            registerModulationTarget (details[(size_t) i]->positionControl(), i + 2);
            wavetableDetails[(size_t) i] = std::make_unique<WavetableDetail> (processor, i, colours[(size_t) i]);
            addChildComponent (*wavetableDetails[(size_t) i]);
            warpDetails[(size_t) i] = std::make_unique<WarpDetail> (processor, i, colours[(size_t) i],
                [this] (int oscillator) { openWarpDetail (oscillator); }, [this, i] { openSourceDetail (i); });
            addChildComponent (*warpDetails[(size_t) i]);
            registerModulationTarget (warpDetails[(size_t) i]->positionControl(), i + 2);
            for (int slot = 0; slot < 2; ++slot)
                registerModulationTarget (warpDetails[(size_t) i]->amountControl (slot), 12 + i * 2 + slot);
        }
    }

    void dismissWavetableDetails()
    {
        for (auto& detail : wavetableDetails) if (detail != nullptr) detail->dismiss();
    }

    void openSourceDetail (int oscillator)
    {
        if (! dismissEnvelopeLab()) return;
        if (voicingPanel != nullptr) voicingPanel->setVisible (false);
        if (browser != nullptr) browser->setVisible (false);
        dismissWarpDetails();
        if (! dismissLfoDetails()) return;
        dismissWavetableDetails();
        for (auto& detail : details) detail->setVisible (false);
        const auto mode = (int) value (processor, "osc" + juce::String (oscillator + 1) + "Mode");
        if (mode == 0)
        {
            const bool opened = wavetableDetails[(size_t) oscillator]->open();
            status = opened ? "Wavetable Lab: draw a wave or its harmonics, then release to hear the change."
                            : "This table could not be opened for editing. Choose a WAV wavetable with up to 32 frames.";
        }
        else
        {
            details[(size_t) oscillator]->refresh();
            details[(size_t) oscillator]->setVisible (true);
            details[(size_t) oscillator]->toFront (false);
            status = "Source Lab: choose a mode, import a WAV, and play the keyboard below.";
        }
    }

    void dismissWarpDetails()
    {
        for (auto& detail : warpDetails) if (detail != nullptr) detail->setVisible (false);
    }

    bool warpDetailIsVisible() const
    {
        for (auto& detail : warpDetails) if (detail != nullptr && detail->isVisible()) return true;
        return false;
    }

    void openWarpDetail (int oscillator)
    {
        if (! dismissEnvelopeLab()) return;
        if (voicingPanel != nullptr) voicingPanel->setVisible (false);
        if (browser != nullptr) browser->setVisible (false);
        if (value (processor, "osc" + juce::String (oscillator + 1) + "Mode") > 0.5f)
        {
            status = "Warp is available in Wavetable mode. Sample and Granular playback bypass both warp stages.";
            return;
        }
        dismissWavetableDetails(); dismissWarpDetails();
        for (auto& detail : details) detail->setVisible (false);
        if (! dismissLfoDetails()) return;
        warpDetails[(size_t) oscillator]->refresh();
        warpDetails[(size_t) oscillator]->setVisible (true);
        warpDetails[(size_t) oscillator]->toFront (false);
        status = "Warp Lab: choose a mode, turn AMOUNT, then drag an LFO or Macro chip onto it for movement.";
    }

    bool dismissEnvelopeLab() { return envelopeLab == nullptr || envelopeLab->close(); }

    void openEnvelopeLab (int index)
    {
        const juce::Component::SafePointer<Interface> safe (this);
        if (sourcePalette != nullptr) sourcePalette->setVisible (false);
        if (voicingPanel != nullptr) voicingPanel->setVisible (false);
        if (browser != nullptr) browser->setVisible (false);
        help.setVisible (false);
        dismissWarpDetails(); if (safe == nullptr) return;
        dismissWavetableDetails(); if (safe == nullptr) return;
        for (auto& detail : details)
        {
            if (detail != nullptr) detail->setVisible (false);
            if (safe == nullptr) return;
        }
        if (! dismissLfoDetails() || safe == nullptr) return;
        if (modulationMatrix != nullptr) modulationMatrix->dismissDetail();
        if (safe == nullptr) return;
        if (! envelopeLab->open (index) || safe == nullptr) return;
        status = "Envelope Lab: drag stage circles or curve diamonds; BPM uses beat divisions. The keyboard remains available below.";
    }

    void buildLfoDetails()
    {
        for (int i = 0; i < 10; ++i)
        {
            lfoDetails[(size_t) i] = std::make_unique<LfoDetail> (processor, i, i % 2 == 0 ? mint : lilac, curveServices);
            addChildComponent (*lfoDetails[(size_t) i]);
            lfoDetails[(size_t) i]->onSelectLfo = [this] (int selected) { openLfoDetail (selected); };
            registerModulationTarget (lfoDetails[(size_t) i]->phaseControl(), anyvoc::lfoPhaseDestinationId (i));
        }
    }

    bool dismissLfoDetails()
    {
        const juce::Component::SafePointer<Interface> safe (this);
        for (int i = 0; i < 10; ++i)
        {
            if (lfoDetails[(size_t) i] != nullptr) lfoDetails[(size_t) i]->close();
            if (safe == nullptr) return false;
        }
        return true;
    }

    void openLfoDetail (int index)
    {
        const juce::Component::SafePointer<Interface> safe (this);
        if (! dismissEnvelopeLab()) return;
        if (index < 0 || index >= 10) return;
        if (sourcePalette != nullptr) sourcePalette->setVisible (false);
        if (voicingPanel != nullptr) voicingPanel->setVisible (false);
        if (browser != nullptr) browser->setVisible (false);
        dismissWarpDetails();
        if (safe == nullptr) return;
        dismissWavetableDetails();
        if (safe == nullptr) return;
        for (auto& detail : details) { detail->setVisible (false); if (safe == nullptr) return; }
        if (! dismissLfoDetails()) return;
        if (! lfoDetails[(size_t) index]->refresh() || safe == nullptr) return;
        lfoDetails[(size_t) index]->setVisible (true);
        if (safe == nullptr) return;
        lfoDetails[(size_t) index]->toFront (false);
        status = "LFO Lab: TOOLS opens selection, grid, transforms and reusable curve files. Lorenz / Rossler show actual chaos motion.";
    }

    bool refreshLfoCards()
    {
        const juce::Component::SafePointer<Interface> safe (this);
        for (int i = 0; i < 10; ++i)
        {
            const int slot = selectedLfos[0] == i ? 0 : selectedLfos[1] == i ? 1 : -1;
            const bool shown = slot >= 0;
            lfos[(size_t) i]->setVisible (shown);
            if (safe == nullptr) return false;
            lfoEditButtons[(size_t) i].setVisible (shown);
            if (safe == nullptr) return false;
            lfoSyncButtons[(size_t) i].setVisible (shown);
            if (safe == nullptr) return false;
            lfoShapeChoices[(size_t) i]->setVisible (shown);
            if (safe == nullptr) return false;
            const int speedMode = processor.getLfoRateMode (i);
            lfoRateKnobs[(size_t) i]->setVisible (shown && speedMode == 0);
            if (safe == nullptr) return false;
            lfoAudioRateKnobs[(size_t) i]->setVisible (shown && speedMode == 2);
            if (safe == nullptr) return false;
            lfoDivisionChoices[(size_t) i]->setVisible (shown && speedMode == 1);
            if (safe == nullptr) return false;
            if (! shown) continue;
            const int x = 24 + slot * 364;
            lfos[(size_t) i]->setBounds (x + 17, 158, 314, 58);
            lfoEditButtons[(size_t) i].setBounds (x + 224, 128, 108, 25);
            lfoRateKnobs[(size_t) i]->setBounds (x + 24, 225, 82, 71);
            lfoAudioRateKnobs[(size_t) i]->setBounds (x + 24, 225, 82, 71);
            lfoDivisionChoices[(size_t) i]->setBounds (x + 21, 233, 91, 47);
            lfoSyncButtons[(size_t) i].setBounds (x + 124, 217, 95, 24);
            lfoShapeChoices[(size_t) i]->setBounds (x + 124, 243, 199, 47);
            lfoShapeChoices[(size_t) i]->refresh();
            const bool chaos = lfoIsChaos (processor.getLfoShape (i));
            lfoRateKnobs[(size_t) i]->setCaption (chaos ? "SPEED" : "RATE");
            lfoAudioRateKnobs[(size_t) i]->setCaption (chaos ? "FAST SPEED" : "AUDIO HZ");
            lfoRateKnobs[(size_t) i]->slider.updateText(); if (safe == nullptr) return false;
            lfoAudioRateKnobs[(size_t) i]->slider.updateText(); if (safe == nullptr) return false;
            lfos[(size_t) i]->refresh();
        }
        for (int slot = 0; slot < 2; ++slot) lfoCardSelectors[(size_t) slot].setSelectedItemIndex (selectedLfos[(size_t) slot], juce::dontSendNotification);
        return true;
    }

    void registerModulationTarget (Knob& control, int destination)
    {
        control.setModulationTarget (destination);
        const juce::Component::SafePointer<Knob> target (&control);
        control.onModulationDrop = [this, target] (int source, int dest)
        {
            const juce::Component::SafePointer<Interface> safe (this);
            const bool assigned = processor.assignModulation (source, dest, 0.25f, [safe, target, dest]
            {
                if (safe == nullptr || target == nullptr) return false;
                const int lfo = anyvoc::lfoIndexForPhaseDestination (dest);
                if (lfo >= 0) return matrixHierarchyVisible (target.getComponent()) && target->isEnabled()
                                     && lfoHasPhase (safe->processor.getLfoShape (lfo));
                return anyvoc::macroIndexForDestination (dest) < 0
                    || (matrixHierarchyVisible (target.getComponent()) && target->isEnabled());
            });
            if (safe == nullptr) return;
            if (assigned)
            {
                selectModulationSource (source);
                if (safe == nullptr) return;
                status = sourceName (source) + " -> " + destinationName (dest) + " assigned. Drag the outer ring up/down to change depth; Shift for fine adjustment.";
            }
            else status = "Assignment stopped. Check the 32 routes on MOD, then drag the source again.";
            refreshRouteIndicators();
        };
        control.onModulationMenu = [this] (int dest, juce::Component* target) { showModulationMenu (dest, target); };
        control.ring.onMenu = [this, &control, destination] { showModulationMenu (destination, &control); };
        control.ring.onDepthChanged = [this, destination] (int route, float depth)
        {
            status = sourceName (selectedModulationSource) + " -> " + destinationName (destination)
                + " / Route " + juce::String (route + 1) + ": " + formatted (depth, Unit::signedPercent) + " (selected route only)";
            refreshRouteIndicators();
        };
        modulationTargets.push_back (&control);
    }

    juce::String sourceName (int source) const
    {
        return modulationSourceName (source);
    }

    void selectModulationSource (int source)
    {
        const juce::Component::SafePointer<Interface> safe (this);
        selectedModulationSource = source;
        preferredModulationRoutes.fill (-1);
        refreshRouteIndicators();
        if (safe == nullptr) return;
        status = sourceName (source) + " selected. Outer rings show one route per target; drag a ring up/down to set depth. Right-click to choose another route.";
    }

    juce::String destinationName (int destination) const
    {
        return modulationDestinationNames[juce::jlimit (0, modulationDestinationNames.size() - 1, destination)];
    }

    void setParameter (const juce::String& id, float newValue)
    {
        setValueWithGesture (processor, id, newValue);
    }

    void clearRoute (int route)
    {
        processor.resetModulationRoute (route);
    }

    void showModulationMenu (int destination, juce::Component* target)
    {
        const bool macroTarget = anyvoc::macroIndexForDestination (destination) >= 0;
        if (macroTarget && (target == nullptr || ! matrixHierarchyVisible (target))) return;
        juce::PopupMenu menu;
        menu.addSectionHeader (destinationName (destination) + " modulation");
        int count = 0;
        std::array<int, matrixRouteCount> menuSources{};
        std::array<anyvoc::ModRoute, matrixRouteCount> menuRoutes{};
        std::array<MatrixRouteSnapshot, matrixRouteCount> menuSnapshots{};
        for (int i = 0; i < matrixRouteCount; ++i)
        {
            menuSnapshots[(size_t) i] = matrixRouteSnapshot (processor, i);
            const auto prefix = "mod" + juce::String (i + 1);
            const auto source = processor.getModulationSource (i);
            if (source == 0 || processor.getModulationDestination (i) != destination) continue;
            menuSources[(size_t) i] = source;
            menuRoutes[(size_t) i] = processor.getModulationRoute (i);
            ++count;
            juce::PopupMenu route;
            const auto amount = value (processor, prefix + "Amount");
            const int base = 100 + i * 10;
            route.addItem (base + 6, "Edit this route on the knob");
            route.addItem (base + 8, "Edit curve and auxiliary source");
            route.addItem (base + 9, "Copy route and edit copy");
            route.addItem (base + 7, menuRoutes[(size_t) i].enabled ? "Bypass this route" : "Enable this route");
            route.addSeparator();
            route.addItem (base + 1, "Set amount to +10%");
            route.addItem (base + 2, "Set amount to +25%");
            route.addItem (base + 3, "Set amount to +50%");
            route.addItem (base + 4, "Reverse direction");
            route.addSeparator(); route.addItem (base + 5, "Remove this route");
            menu.addSubMenu ("Route " + juce::String (i + 1) + " / " + sourceName (source) + "   " + formatted (amount, Unit::signedPercent)
                             + (menuRoutes[(size_t) i].enabled ? "" : "  [bypassed]"), route);
        }
        if (count == 0) menu.addItem (99, "Drag a source chip here to add a route", false);
        menu.addSeparator(); menu.addItem (1, "Open full modulation matrix");
        if (count > 0) menu.addItem (2, "Remove all routes to this target");
        auto safe = juce::Component::SafePointer<Interface> (this);
        const juce::Component::SafePointer<juce::Component> safeTarget (target);
        const auto menuRevision = processor.getHistoryRevision();
        const auto menuNavigation = copyNavigationGeneration;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [safe, safeTarget, macroTarget, destination, menuSources, menuRoutes, menuSnapshots, menuRevision, menuNavigation] (int result)
        {
            if (safe == nullptr || result == 0) return;
            if (macroTarget && (safeTarget == nullptr || ! matrixHierarchyVisible (safeTarget.getComponent()))) return;
            if (safe->processor.getHistoryRevision() != menuRevision) return;
            if (result == 1) { safe->showPage (1); if (safe != nullptr) safe->modulationMatrix->showExpanded(); return; }
            for (int i = 0; i < matrixRouteCount; ++i)
                if (matrixRouteSnapshot (safe->processor, i) != menuSnapshots[(size_t) i]) return;
            if (result == 2)
            {
                const auto isCurrent = [safe, safeTarget, menuNavigation, menuRevision]
                {
                    return safe != nullptr && safeTarget != nullptr && safe->copyNavigationGeneration == menuNavigation
                        && safe->processor.getHistoryRevision() == menuRevision
                        && matrixHierarchyVisible (safeTarget.getComponent()) && safeTarget->isEnabled();
                };
                if (! isCurrent()) return;
                const bool removed = safe->processor.resetModulationRoutesToDestination (destination, isCurrent);
                if (safe == nullptr) return;
                safe->status = removed ? "Removed modulation from " + safe->destinationName (destination) + "."
                                       : "Removal interrupted; review current routes";
            }
            else if (result >= 100)
            {
                const int route = (result - 100) / 10, action = (result - 100) % 10;
                if (route < 0 || route >= matrixRouteCount) return;
                const auto prefix = "mod" + juce::String (route + 1);
                if (safe->processor.getModulationDestination (route) != destination
                    || matrixRouteSnapshot (safe->processor, route) != menuSnapshots[(size_t) route]) return;
                if (action == 9)
                {
                    const auto isCurrent = [safe, safeTarget, menuNavigation]
                    {
                        return safe != nullptr && safeTarget != nullptr && safe->copyNavigationGeneration == menuNavigation
                            && matrixHierarchyVisible (safeTarget.getComponent()) && safeTarget->isEnabled();
                    };
                    if (! isCurrent()) return;
                    auto copied = safe->processor.copyModulationRoute (route, isCurrent);
                    if (safe == nullptr) return;
                    if (copied.status == AnyvocAudioProcessor::ModulationCopyStatus::copied && ! isCurrent())
                        copied = { AnyvocAudioProcessor::ModulationCopyStatus::cancelled, -1 };
                    if (copied.status == AnyvocAudioProcessor::ModulationCopyStatus::copied)
                    {
                        const auto revision = safe->processor.getHistoryRevision();
                        const auto snapshot = matrixRouteSnapshot (safe->processor, copied.destination);
                        safe->showPage (1);
                        if (safe == nullptr || safe->processor.getHistoryRevision() != revision
                            || matrixRouteSnapshot (safe->processor, copied.destination) != snapshot) return;
                    }
                    safe->modulationMatrix->reportCopyResult (route, copied, true);
                    return;
                }
                if (action == 8)
                {
                    safe->showPage (1); if (safe != nullptr) safe->modulationMatrix->showDetail (route); return;
                }
                if (action == 6)
                {
                    safe->selectModulationSource (menuSources[(size_t) route]);
                    if (safe == nullptr) return;
                    safe->preferredModulationRoutes[(size_t) destination] = route;
                    safe->refreshRouteIndicators();
                    return;
                }
                if (action == 5) safe->clearRoute (route);
                else if (action == 7) safe->setParameter (prefix + "Enabled", menuRoutes[(size_t) route].enabled ? 0.0f : 1.0f);
                else
                {
                    const float amount = action == 1 ? 0.1f : action == 2 ? 0.25f : action == 3 ? 0.5f
                                         : -value (safe->processor, prefix + "Amount");
                    safe->setParameter (prefix + "Amount", amount);
                }
                if (safe == nullptr) return;
                safe->status = "Updated modulation for " + safe->destinationName (destination) + ".";
            }
            safe->refreshRouteIndicators();
        });
    }

    void refreshRouteIndicators()
    {
        const juce::Component::SafePointer<Interface> safe (this);
        for (auto& chip : sourceChips) chip->selectSource (selectedModulationSource);
        if (sourcePalette != nullptr) sourcePalette->select (selectedModulationSource);
        for (auto* target : modulationTargets)
        {
            int count = 0, selectedRoute = -1;
            const int destination = target->getModulationDestination();
            for (int i = 0; i < matrixRouteCount; ++i)
                if (processor.getModulationDestination (i) == destination
                    && processor.getModulationSource (i) > 0)
                {
                    ++count;
                    if (processor.getModulationSource (i) == selectedModulationSource
                        && (selectedRoute < 0 || i == preferredModulationRoutes[(size_t) destination])) selectedRoute = i;
                }
            target->setRouteCount (count);
            target->ring.select (selectedRoute, selectedModulationSource, destination);
            if (safe == nullptr) return;
        }
    }

    void refreshMacroValues()
    {
        // One coherent engine snapshot supplies every visible/hidden macro.
        // Never write telemetry into the base sliders or parameter state.
        const auto snapshot = processor.getMacroModulationSnapshot();
        macroLiveContext.setText (snapshot.hasActiveVoice ? "EV: latest voice" : "EV: idle base", juce::dontSendNotification);
        macroLiveContext.getProperties().set ("macroHasActiveVoice", snapshot.hasActiveVoice);
        macroLiveContext.getProperties().set ("macroVoiceId", (juce::int64) snapshot.voiceId);
        for (size_t i = 0; i < macroLiveValues.size(); ++i)
        {
            auto& display = macroLiveValues[i];
            const float actual = std::isfinite (snapshot.values[i]) ? juce::jlimit (0.0f, 1.0f, snapshot.values[i]) : 0.0f;
            display.setText ("EV " + juce::String (actual * 100.0f, 1) + "%", juce::dontSendNotification);
            display.getProperties().set ("macroEffectiveValue", (double) actual);
            display.getProperties().set ("macroHasActiveVoice", snapshot.hasActiveVoice);
            display.getProperties().set ("macroVoiceId", (juce::int64) snapshot.voiceId);
        }
    }

    void dragOperationStarted (const juce::DragAndDropTarget::SourceDetails&) override
    {
        const juce::Component::SafePointer<Interface> safe (this);
        // PLAY stays in place, but the envelope editor must not cover its drop targets.
        if (! dismissEnvelopeLab() || safe == nullptr) return;
        if (sourcePalette != nullptr) sourcePalette->setVisible (false);
        if (voicingPanel != nullptr) voicingPanel->setVisible (false);
        if (browser != nullptr) browser->setVisible (false);
        help.setVisible (false);
        dismissWavetableDetails();
        if (safe == nullptr) return;
        const bool warpOpen = warpDetailIsVisible();
        const bool lfoOpen = std::any_of (lfoDetails.begin(), lfoDetails.end(), [] (const auto& detail) { return detail->isVisible(); });
        if (! warpOpen && ! lfoOpen && currentPage != 3 && currentPage != 4) showPage (0);
        if (safe == nullptr) return;
        for (auto* target : modulationTargets) target->setTargetHighlighted (true);
        status = lfoOpen ? "Drop onto a MACRO, or START PHASE for a periodic LFO. Random and Chaos have no phase target."
                 : warpOpen ? "Drop onto a MACRO, WARP AMOUNT or WAVE. Choose an active warp mode; right-click to edit routes."
                 : currentPage == 3 ? "Drop onto a MACRO or highlighted filter knob. Right-click the target to edit routes."
                 : currentPage == 4 ? "Drop onto CUTOFF, RESONANCE or a MACRO. Drag the coloured outer ring to change the modulation depth."
                                  : "Drop onto a MACRO, WAVE / START / CENTRE or filter knob. A macro can be both a source and a target.";
    }

    void dragOperationEnded (const juce::DragAndDropTarget::SourceDetails&) override
    {
        for (auto* target : modulationTargets) target->setTargetHighlighted (false);
    }

    void paintSynth (juce::Graphics& g)
    {
        const std::array<juce::Colour, 3> accents { mint, lilac, blue };
        for (int i = 0; i < 3; ++i)
        {
            const int x = 24 + i * 364;
            drawCard (g, { x, 117, 348, 270 }, "OSC " + juce::String::charToString ((juce::juce_wchar) ('A' + i)), "", accents[(size_t) i]);
            const int mode = (int) value (processor, "osc" + juce::String (i + 1) + "Mode");
            drawText (g, mode == 0 ? processor.getWavetableName (i) : processor.getSampleName (i), { x + 18, 151, 310, 17 }, 10.5f);
            drawText (g, "DUAL WARP", { x + 258, 307, 80, 17 }, 10.5f, secondaryText, false, juce::Justification::centred);
            drawText (g, mode == 0 ? "1  >  2" : "WT ONLY", { x + 264, 363, 68, 14 }, 8.5f,
                      mode == 0 ? accents[(size_t) i] : secondaryText, false, juce::Justification::centred);
        }
        drawCard (g, { 24, 398, 348, 180 }, "FILTER A", "", mint);
        drawText (g, value (processor, "filterEnabled") > 0.5f ? "A ACTIVE" : "A BYPASSED", { 42, 553, 180, 18 }, 9,
                  value (processor, "filterEnabled") > 0.5f ? mint : secondaryText, true);
        drawCard (g, { 388, 398, 348, 180 }, "AMP ENVELOPE", "", lilac);
        drawCard (g, { 752, 398, 348, 180 }, "VOICE & TEXTURE", "", blue);
        const int voiceMode = juce::jlimit (0, 2, (int) value (processor, "voiceMode"));
        const juce::StringArray voiceNames { "POLY", "MONO", "LEGATO" };
        drawText (g, voiceNames[voiceMode], { 893, 405, 75, 14 }, 8.5f, blue, true, juce::Justification::centred);
        drawText (g, voiceMode == 0 ? "GLIDE OFF" : formatted (value (processor, "glideTime"), Unit::seconds),
                  { 893, 419, 75, 12 }, 8, secondaryText, false, juce::Justification::centred);
        g.setColour (borderColour); g.drawVerticalLine (917, 443, 559);
        drawText (g, "LOW END + TEXTURE", { 770, 546, 140, 15 }, 8.5f, secondaryText, false, juce::Justification::centred);
        drawText (g, "OCT", { 931, 525, 34, 18 }, 9);
        drawText (g, "GATE", { 931, 550, 34, 18 }, 9);
    }

    void paintModulation (juce::Graphics& g)
    {
        drawCard (g, { 24, 117, 348, 188 }, "", "", mint);
        drawCard (g, { 388, 117, 348, 188 }, "", "", lilac);
        const juce::StringArray modes { "FREE", "TRIGGER", "ENVELOPE" };
        for (int i = 0; i < 2; ++i)
            drawText (g, modes[juce::jlimit (0, 2, (int) value (processor, "lfo" + juce::String (selectedLfos[(size_t) i] + 1) + "Mode"))],
                      { 168 + i * 364, 131, 76, 20 }, 8.5f, secondaryText, true);
        drawCard (g, { 752, 117, 348, 188 }, "", "", blue);
    }

    void paintFilters (juce::Graphics& g)
    {
        drawCard (g, { 24, 117, 532, 306 }, "FILTER A", "", mint);
        drawCard (g, { 568, 117, 532, 306 }, "FILTER B", "", lilac);
        drawCard (g, { 24, 435, 1076, 143 }, "SIGNAL FLOW", "", blue);
        g.setColour (borderColour); g.drawVerticalLine (954, 452, 562);
        drawText (g, "A only / B only skip the other filter. Bypass skips both. Disabled filters pass audio unchanged.",
                  { 42, 556, 893, 17 }, 9.5f, secondaryText);
    }

    void paintEffects (juce::Graphics& g)
    {
        const juce::StringArray names { "SATURATION", "CHORUS", "DELAY", "REVERB" };
        const juce::StringArray subtitles { "Warmth, bite and harmonics", "Width and gentle movement", "Rhythmic echoes and repeats", "A little room. Or a new world." };
        const std::array<juce::Colour, 4> accents { orange, lilac, mint, blue };
        for (int i = 0; i < 4; ++i)
        {
            const int x = 24 + i * 272;
            drawCard (g, { x, 117, 256, 461 }, names[i], subtitles[i], accents[(size_t) i]);
            drawText (g, "0" + juce::String (i + 1), { x + 208, 130, 30, 20 }, 11, accents[(size_t) i], true, juce::Justification::centredRight);
        }
        const juce::StringArray notes { "Try a little drive to give soft sounds\nmore presence and character.",
            "A subtle mix makes a narrow sound\nfeel wider and more alive.", "", "Turn up SIZE for long, spacious tails.\nUse MIX to keep your sound clear." };
        for (int i : { 0, 1, 3 })
        {
            g.setColour (secondaryText); g.setFont (font (11));
            g.drawFittedText (notes[i], { 42 + i * 272, 478, 220, 48 }, juce::Justification::centred, 2);
        }
    }

    void paintFooter (juce::Graphics& g)
    {
        drawCard (g, { 24, 594, 348, 147 }, "PERFORMANCE MACROS", "", mint);
        drawCard (g, { 388, 594, 570, 147 }, "PLAY", "", lilac);
        drawText (g, "Click keys to audition", { 463, 609, 235, 20 }, 10.5f);
        drawCard (g, { 974, 594, 126, 147 }, "OUTPUT", "", mint);
        const float db = juce::Decibels::gainToDecibels (meter, -60.0f);
        const float amount = juce::jlimit (0.0f, 1.0f, (db + 60) / 60.0f);
        for (int i = 0; i < 18; ++i)
        {
            auto colour = i > 15 ? juce::Colour (0xffff857f) : i > 12 ? orange : mint;
            g.setColour (amount > (float) i / 18.0f ? colour : borderColour);
            g.fillRoundedRectangle (1067, (float) (718 - i * 4), 13, 2.5f, 0.7f);
        }
        drawText (g, "-60", { 1081, 710, 18, 12 }, 7);
        drawText (g, "0", { 1081, 647, 16, 12 }, 7);
        drawText (g, status, { 26, 744, 930, 13 }, 9, secondaryText);
        drawText (g, "ANYVOC  /  " JucePlugin_VersionString, { 955, 744, 144, 13 }, 8.5f, secondaryText, false, juce::Justification::centredRight);
    }

    void showPage (int index)
    {
        if (index < 0 || index >= (int) tabs.size()) return;
        ++copyNavigationGeneration;
        if (modulationMatrix != nullptr) modulationMatrix->cancelPendingNavigation();
        if (! dismissEnvelopeLab()) return;
        if (sourcePalette != nullptr) sourcePalette->setVisible (false);
        if (index != 1 && modulationMatrix != nullptr) modulationMatrix->dismissDetail();
        if (voicingPanel != nullptr) voicingPanel->setVisible (false);
        if (browser != nullptr) browser->setVisible (false);
        help.setVisible (false);
        currentPage = index;
        if (index != 0) dismissWarpDetails();
        if (index != 0) dismissWavetableDetails();
        if (index != 0) for (auto& detail : details) if (detail != nullptr) detail->setVisible (false);
        if (index != 1 && ! dismissLfoDetails()) return;
        synthPage.setVisible (index == 0); modPage.setVisible (index == 1); fxPage.setVisible (index == 2); filterPage.setVisible (index == 3);
        playPage.setVisible (index == 4);
        for (int i = 0; i < (int) tabs.size(); ++i) tabs[(size_t) i].setToggleState (i == index, juce::dontSendNotification);
        if (index == 3) refreshFilters();
        if (index == 4) { if (! refreshPlay() || ! refreshEnvelopeControls()) return; }
        repaint();
    }

    void stepPreset (int delta)
    {
        if (browser != nullptr && ! browser->loadAdjacent (delta))
            status = "No sounds match the current browser filters. Open BROWSE to change the search.";
    }

    void openPresetBrowser()
    {
        if (! dismissEnvelopeLab()) return;
        if (sourcePalette != nullptr) sourcePalette->setVisible (false);
        if (voicingPanel != nullptr) voicingPanel->setVisible (false);
        dismissWavetableDetails(); dismissWarpDetails(); help.setVisible (false);
        for (auto& detail : details) if (detail != nullptr) detail->setVisible (false);
        if (! dismissLfoDetails()) return;
        browser->open();
        status = "Browse and select freely. LOAD SOUND, double-click or Return loads your selection; the keyboard plays the loaded sound.";
    }

    void openVoicing()
    {
        if (! dismissEnvelopeLab()) return;
        if (sourcePalette != nullptr) sourcePalette->setVisible (false);
        dismissWavetableDetails(); dismissWarpDetails(); help.setVisible (false);
        if (browser != nullptr) browser->setVisible (false);
        for (auto& detail : details) if (detail != nullptr) detail->setVisible (false);
        if (! dismissLfoDetails()) return;
        voicingPanel->open();
        status = "Voicing: choose Poly for chords, or Mono / Legato for single-note lines. Phase changes apply to the next wavetable note.";
    }

    void openModulationSources()
    {
        const juce::Component::SafePointer<Interface> safe (this);
        if (! dismissEnvelopeLab()) return;
        if (voicingPanel != nullptr) voicingPanel->setVisible (false);
        if (browser != nullptr) browser->setVisible (false);
        dismissWavetableDetails(); if (safe == nullptr) return;
        dismissWarpDetails(); if (safe == nullptr) return;
        help.setVisible (false);
        for (auto& detail : details)
        { if (detail != nullptr) detail->setVisible (false); if (safe == nullptr) return; }
        for (auto& detail : lfoDetails)
            if (detail->isVisible() && (! detail->finishDrawing() || safe == nullptr)) return;
        if (modulationMatrix != nullptr) modulationMatrix->dismissDetail();
        if (safe == nullptr) return;
        sourcePalette->select (selectedModulationSource); sourcePalette->setVisible (true); sourcePalette->toFront (false);
        status = "Choose any modulation source here, then drag its chip onto a target. DONE closes the source list.";
    }

    bool loadLibraryEntry (const anyvoc::PresetEntry& entry)
    {
        dismissWavetableDetails();
        const bool loaded = entry.factoryIndex >= 0 ? (processor.selectFactoryPreset (entry.factoryIndex), true)
                                                   : processor.loadPreset (entry.file);
        status = loaded ? "Loaded " + processor.getLoadedPresetName()
                        : "Could not load " + entry.name + ". Your current sound is unchanged. Refresh the library if the file moved.";
        if (loaded) { previousProgram = processor.getCurrentProgram(); refreshPresetIdentity(); }
        return loaded;
    }

    void refreshPresetIdentity()
    {
        const auto name = processor.getLoadedPresetName(), id = processor.getLoadedPresetId();
        if (name != previousPresetName || id != previousPresetId)
        {
            previousPresetName = name; previousPresetId = id;
            presetName.setButtonText (name); presetName.setTitle ("Loaded preset: " + name);
            presetName.setTooltip ("Currently loaded: " + name + ". Open BROWSE to search and load other sounds.");
        }
    }

    juce::File presetDirectory() const
    {
        auto dir = presetLibrary.getDirectory();
        dir.createDirectory();
        return dir;
    }

    void saveSound()
    {
        chooser = std::make_unique<juce::FileChooser> ("Save your Anyvoc sound", presetDirectory().getChildFile ("My Sound.anyvoc"), "*.anyvoc");
        auto safe = juce::Component::SafePointer<Interface> (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe] (const juce::FileChooser& selected)
        {
            if (safe == nullptr) return;
            auto file = selected.getResult();
            if (file == juce::File()) return;
            file = file.withFileExtension ("anyvoc");
            if (safe->processor.savePreset (file))
            {
                safe->status = "Saved " + file.getFileName();
                safe->previousProgram = safe->processor.getCurrentProgram();
                safe->refreshPresetIdentity(); safe->presetLibrary.refresh();
            }
            else safe->status = "Could not save. Please choose another location.";
        });
    }

    void loadSound()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load an Anyvoc sound", presetDirectory(), "*.anyvoc");
        auto safe = juce::Component::SafePointer<Interface> (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe] (const juce::FileChooser& selected)
        {
            if (safe == nullptr) return;
            const auto file = selected.getResult();
            if (file == juce::File()) return;
            safe->dismissWavetableDetails();
            if (safe->processor.loadPreset (file))
            {
                safe->status = "Loaded " + file.getFileName();
                safe->previousProgram = safe->processor.getCurrentProgram();
                safe->refreshPresetIdentity(); safe->presetLibrary.refresh();
            }
            else safe->status = "This preset could not be loaded. Choose a valid .anyvoc file.";
        });
    }

    void chooseWave (int oscillator)
    {
        const int mode = (int) value (processor, "osc" + juce::String (oscillator + 1) + "Mode");
        chooser = std::make_unique<juce::FileChooser> (mode == 0 ? "Import a WAV wavetable" : "Import an audio sample (WAV, up to 20 seconds)",
                                                      juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.wav");
        auto safe = juce::Component::SafePointer<Interface> (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe, oscillator] (const juce::FileChooser& selected)
        {
            if (safe != nullptr && selected.getResult() != juce::File()) safe->importWave (oscillator, selected.getResult());
        });
    }

    void importWave (int oscillator, const juce::File& file)
    {
        if (wavetableDetails[(size_t) oscillator] != nullptr) wavetableDetails[(size_t) oscillator]->dismiss();
        const int mode = (int) value (processor, "osc" + juce::String (oscillator + 1) + "Mode");
        const bool imported = mode == 0 ? processor.importWavetable (oscillator, file) : processor.importSample (oscillator, file);
        status = imported ? "Imported " + file.getFileName() + " into oscillator " + juce::String::charToString ((juce::juce_wchar) ('A' + oscillator))
                          : mode == 0 ? "Could not import this WAV. Choose a valid WAV wavetable."
                                      : "Could not import this sample. Use a valid mono/stereo WAV no longer than 20 seconds.";
        if (details[(size_t) oscillator] != nullptr) details[(size_t) oscillator]->refresh();
        repaint();
    }

    void timerCallback() override
    {
        meter = juce::jmax (processor.getOutputLevel(), meter * 0.84f);
        refreshMacroValues();
        tempoDisplay->refresh();
        refreshPresetIdentity();
        refreshHistoryControls();
        if (browser != nullptr) browser->refresh();
        if (voicingPanel != nullptr && voicingPanel->isVisible()) voicingPanel->refresh();
        if (! refreshLfoCards() || ! refreshEnvelopeControls()) return;
        if (envelopeLab != nullptr && envelopeLab->isVisible() && ! envelopeLab->refresh()) return;
        if (modulationMatrix != nullptr) modulationMatrix->refresh();
        for (auto& detail : lfoDetails) if (detail->isVisible() && ! detail->refresh()) return;
        for (int i = 0; i < 3; ++i)
        {
            const auto mode = (int) value (processor, "osc" + juce::String (i + 1) + "Mode");
            positionKnobs[(size_t) i]->setCaption (mode == 0 ? "WAVE" : mode == 1 ? "START" : "CENTRE");
            warpButtons[(size_t) i].setEnabled (mode == 0);
            if (warpDetails[(size_t) i]->isVisible())
            {
                if (mode == 0) warpDetails[(size_t) i]->refresh();
                else
                {
                    warpDetails[(size_t) i]->setVisible (false);
                    status = "Warp is available in Wavetable mode. Sample and Granular playback bypass both warp stages.";
                }
            }
            if (details[(size_t) i]->isVisible()) details[(size_t) i]->refresh();
            if (wavetableDetails[(size_t) i]->dismissIfAssetChanged())
                status = "The oscillator's table changed. Reopen EDIT to work on the current wavetable.";
            if (mode != 0 && wavetableDetails[(size_t) i]->isVisible()) wavetableDetails[(size_t) i]->dismiss();
        }
        if (++routeRefreshTicks >= 6) { routeRefreshTicks = 0; refreshRouteIndicators(); }
        const int program = processor.getCurrentProgram();
        if (program != previousProgram)
        {
            dismissWavetableDetails();
            previousProgram = program;
        }
        if (currentPage == 0)
        {
            for (auto& wave : waves) wave->repaint();
            ampEnvelope->repaint();
        }
        else if (currentPage == 1)
        {
            for (auto& lfo : lfos) lfo->repaint();
            modEnvelopes[(size_t) selectedEnvelope]->repaint();
            repaint (24, 128, 712, 26);
        }
        else if (currentPage == 2) for (auto& effect : effects) effect->repaint();
        else if (currentPage == 3) refreshFilters();
        else if (! refreshPlay()) return;
        repaint (974, 640, 126, 90);
        repaint (24, 744, 1076, 15);
        if (currentPage == 0) { repaint (24, 150, 1076, 20); repaint (24, 363, 1076, 14); repaint (893, 405, 75, 26); repaint (42, 553, 180, 18); }
    }

    AnyvocAudioProcessor& processor;
    anyvoc::PresetLibrary presetLibrary;
    std::shared_ptr<LfoCurveServices> curveServices;
    InstrumentLookAndFeel lookAndFeel;
    juce::Component synthPage, modPage, fxPage, filterPage, playPage;
    std::array<juce::TextButton, 5> tabs;
    juce::TextButton playBrowse, playFilters, playEnvelopeEdit, playEffects;
    std::array<juce::TextButton, 4> playEditors;
    juce::ToggleButton playFilterEnabled;
    std::unique_ptr<SoundButtonAttachment> playFilterAttachment;
    std::unique_ptr<EnvelopeDisplay> playEnvelope;
    Knob* playCutoff = nullptr;
    Knob* playResonance = nullptr;
    std::array<juce::TextButton, 3> importButtons, detailButtons, warpButtons;
    juce::TextButton previousPreset, nextPreset, save, load, init, helpButton, panicButton, openFiltersButton, voicingButton;
    juce::TextButton presetName, browseButton, soundUndo, soundRedo;
    std::unique_ptr<PresetBrowser> browser;
    std::unique_ptr<VoicingPanel> voicingPanel;
    std::unique_ptr<ModulationSourcePalette> sourcePalette;
    juce::TextButton sourcePaletteButton;
    std::array<juce::Component, 2> macroPages;
    std::array<juce::TextButton, 2> macroBankButtons;
    std::array<juce::Label, anyvoc::macroCount> macroLiveValues;
    juce::Label macroLiveContext;
    std::unique_ptr<TempoDisplay> tempoDisplay;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<std::unique_ptr<ModulationChip>> sourceChips;
    std::vector<Knob*> modulationTargets;
    int selectedModulationSource = 1;
    std::array<int, anyvoc::modulationDestinationCount> preferredModulationRoutes{};
    std::unique_ptr<ModulationMatrix> modulationMatrix;
    std::array<std::unique_ptr<FilterResponse>, 2> filterResponses;
    std::unique_ptr<FilterRoutingDiagram> filterRoutingDiagram;
    std::array<juce::ToggleButton, 2> filterEnabledButtons;
    std::array<std::unique_ptr<SoundButtonAttachment>, 2> filterEnabledAttachments;
    Knob* parallelBalance = nullptr;
    std::array<Knob*, 3> positionKnobs{};
    std::array<std::unique_ptr<OscillatorDetail>, 3> details;
    std::array<std::unique_ptr<WavetableDetail>, 3> wavetableDetails;
    std::array<std::unique_ptr<WarpDetail>, 3> warpDetails;
    std::array<std::unique_ptr<WaveDisplay>, 3> waves;
    std::array<std::unique_ptr<LfoDisplay>, 10> lfos;
    std::array<juce::TextButton, 10> lfoEditButtons;
    std::array<juce::ToggleButton, 10> lfoSyncButtons;
    std::array<std::unique_ptr<SoundButtonAttachment>, 10> lfoSyncAttachments;
    std::array<Knob*, 10> lfoRateKnobs{};
    std::array<Knob*, 10> lfoAudioRateKnobs{};
    std::array<Choice*, 10> lfoDivisionChoices{};
    std::array<std::unique_ptr<LfoShapeChoice>, 10> lfoShapeChoices;
    std::array<std::unique_ptr<LfoDetail>, 10> lfoDetails;
    std::array<juce::ComboBox, 2> lfoCardSelectors;
    std::array<int, 2> selectedLfos { 0, 1 };
    std::array<juce::Component, 4> envelopePages;
    std::array<std::unique_ptr<EnvelopeDisplay>, 4> modEnvelopes;
    juce::ComboBox envelopeSelector;
    int selectedEnvelope = 1;
    std::array<std::unique_ptr<EffectDisplay>, 4> effects;
    std::unique_ptr<EnvelopeDisplay> ampEnvelope;
    std::unique_ptr<EnvelopeLab> envelopeLab;
    juce::TextButton ampEnvelopeEdit, modEnvelopeEdit;
    struct EnvelopeTimeKnob { Knob* control; juce::String prefix; };
    std::vector<EnvelopeTimeKnob> envelopeTimeKnobs;
    std::vector<std::unique_ptr<SoundSlider>> matrixAmounts;
    std::vector<std::unique_ptr<SoundSliderAttachment>> matrixAttachments;
    juce::ToggleButton arp;
    std::unique_ptr<SoundButtonAttachment> arpAttachment;
    juce::MidiKeyboardComponent keyboard;
    juce::TooltipWindow tooltip;
    HelpOverlay help;
    std::unique_ptr<juce::FileChooser> chooser;
    int currentPage = 4, previousProgram = -1, routeRefreshTicks = 0;
    juce::String previousPresetId, previousPresetName;
    uint64_t displayedHistoryRevision = std::numeric_limits<uint64_t>::max(), copyNavigationGeneration = 0;
    bool historyWasActive = false;
    float meter = 0;
    juce::String status = "Ready. Choose a preset, play a note, and make it yours.";
};

AnyvocAudioProcessorEditor::AnyvocAudioProcessorEditor (AnyvocAudioProcessor& processor, juce::File libraryDirectory, juce::File librarySettings,
                                                      std::shared_ptr<LfoCurveServices> curveServices)
    : AudioProcessorEditor (&processor), interface (std::make_unique<Interface> (processor, std::move (libraryDirectory), std::move (librarySettings), std::move (curveServices)))
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    addAndMakeVisible (*interface);
    setResizable (true, true);
    setResizeLimits (1008, 684, 1568, 1064);
    if (auto* limits = getConstrainer()) limits->setFixedAspectRatio ((double) designWidth / designHeight);
    setSize (designWidth, designHeight);
}

AnyvocAudioProcessorEditor::~AnyvocAudioProcessorEditor() = default;

void AnyvocAudioProcessorEditor::paint (juce::Graphics& g) { g.fillAll (background); }

bool AnyvocAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    return interface->handleHistoryKey (key);
}

void AnyvocAudioProcessorEditor::resized()
{
    const float scale = juce::jmin ((float) getWidth() / designWidth, (float) getHeight() / designHeight);
    interface->setTransform (juce::AffineTransform::scale (scale));
    interface->setTopLeftPosition (0, 0);
}
