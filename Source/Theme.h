#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Colours and fonts taken from DREAMSHARELITE.html so the plugin matches the web viewer.
namespace ds::theme
{
    inline const juce::Colour bg      { 0xff0a080a };
    inline const juce::Colour header  { 0xff100c10 };
    inline const juce::Colour panel   { 0xff141014 };
    inline const juce::Colour accent  { 0xffe62020 };
    inline const juce::Colour text    { 0xfff2ece8 };
    inline const juce::Colour dim     { 0xff9a8e8e };
    inline const juce::Colour border  { 0xff2e2024 };
    inline const juce::Colour ok      { 0xff7cff6b };
    inline const juce::Colour pink    { 0xffffb0b0 };
    inline const juce::Colour chipBg  { 0xff140c10 };
    inline const juce::Colour chipBd  { 0xff3a2428 };
    inline const juce::Colour chipOn  { 0xff3a1418 };

    struct Badge { juce::Colour fill, ink, edge; };
    inline Badge badge (const juce::String& label)
    {
        if (label == "ADMIN") return { juce::Colour (0xff3a2010), juce::Colour (0xffffd0a0), juce::Colour (0xff805030) };
        if (label == "MOD")   return { juce::Colour (0xff3a1820), juce::Colour (0xffffb0b0), juce::Colour (0xff802030) };
        return                       { juce::Colour (0xff1a2438), juce::Colour (0xffc9d7ff), juce::Colour (0xff3a4e80) }; // KYOTO
    }

    inline juce::Font font (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Width of a single line of text (works on every JUCE 7/8 build).
    inline float textWidth (const juce::String& s, const juce::Font& f)
    {
        juce::AttributedString a;
        a.setWordWrap (juce::AttributedString::none);
        a.append (s, f, juce::Colours::white);
        juce::TextLayout tl;
        tl.createLayout (a, 4000.0f);
        return tl.getWidth();
    }

    // Build a string from Unicode code points (avoids source-file encoding problems).
    inline juce::String cp (std::initializer_list<juce::juce_wchar> cps)
    {
        juce::String s;
        for (auto c : cps) s += juce::String::charToString (c);
        return s;
    }
}

namespace ds
{
    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            using namespace theme;
            setColour (juce::ResizableWindow::backgroundColourId, bg);
            setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a1216));
            setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a1418));
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextButton::textColourOnId, pink);
            setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0a0608));
            setColour (juce::TextEditor::textColourId, text);
            setColour (juce::TextEditor::outlineColourId, border);
            setColour (juce::TextEditor::focusedOutlineColourId, accent);
            setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.45f));
            setColour (juce::TextEditor::highlightedTextColourId, text);
            setColour (juce::CaretComponent::caretColourId, text);
            setColour (juce::Label::textColourId, text);
            setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff4a2a30));
            setColour (juce::PopupMenu::backgroundColourId, panel);
            setColour (juce::PopupMenu::textColourId, text);
            setColour (juce::PopupMenu::headerTextColourId, dim);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2a1820));
            setColour (juce::PopupMenu::highlightedTextColourId, text);
        }
    };
}
