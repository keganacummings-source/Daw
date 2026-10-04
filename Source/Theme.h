#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Colours and fonts taken from DREAMSHARELITE.html so the plugin matches the web viewer.
namespace ds::theme
{
    inline juce::Colour bg      { 0xff0e0608 };
    inline juce::Colour header  { 0xff1a0a10 };
    inline juce::Colour panel   { 0xff1a0c12 };
    inline juce::Colour accent  { 0xffc04068 };
    inline juce::Colour text    { 0xfff4e4ea };
    inline juce::Colour dim     { 0xff9a7884 };
    inline juce::Colour border  { 0xff341820 };
    inline juce::Colour ok      { 0xff7cff6b };
    inline juce::Colour pink    { 0xffe07090 };
    inline juce::Colour chipBg  { 0xff12080c };
    inline juce::Colour chipBd  { 0xff341820 };
    inline juce::Colour chipOn  { 0xff7a2844 };

    struct Badge { juce::Colour fill, ink, edge; };
    inline Badge badge (const juce::String& label)
    {
        if (label == "ADMIN") return { juce::Colour (0xff3a2010), juce::Colour (0xffffd0a0), juce::Colour (0xff805030) };
        if (label == "MOD")   return { juce::Colour (0xff3a1820), juce::Colour (0xffffb0b0), juce::Colour (0xff802030) };
        if (label == "KYOTO") return { juce::Colour (0xff1a2438), juce::Colour (0xffc9d7ff), juce::Colour (0xff3a4e80) };
        return                       { juce::Colour (0xff1a2438), juce::Colour (0xffc9d7ff), juce::Colour (0xff3a4e80) };
    }

    inline void applyPalette (const juce::String& requested)
    {
        const auto id = requested.toLowerCase();
        const auto set = [] (unsigned int b, unsigned int h, unsigned int p, unsigned int a,
                             unsigned int t, unsigned int d, unsigned int e, unsigned int pk,
                             unsigned int cb, unsigned int ce, unsigned int co)
        {
            bg = juce::Colour (b); header = juce::Colour (h); panel = juce::Colour (p);
            accent = juce::Colour (a); text = juce::Colour (t); dim = juce::Colour (d);
            border = juce::Colour (e); pink = juce::Colour (pk);
            chipBg = juce::Colour (cb); chipBd = juce::Colour (ce); chipOn = juce::Colour (co);
        };
        if (id == "amber") set (0xff140e06, 0xff1c1206, 0xff24180a, 0xfff5b042, 0xfffff7e8, 0xffc4a574, 0xff4a3414, 0xfffde68a, 0xff1a1208, 0xff4a3414, 0xffb45309);
        else if (id == "ash") set (0xff12151c, 0xff1a1e26, 0xff1b212b, 0xff9bb4c8, 0xffe7eef4, 0xff8b97a6, 0xff2c3542, 0xffd5e6f2, 0xff10141b, 0xff2c3542, 0xff5d7386);
        else if (id == "bloodmoon") set (0xff070203, 0xff140206, 0xff16080c, 0xffe23a4a, 0xfff8e8e6, 0xffa07878, 0xff3a1820, 0xffff8a90, 0xff100408, 0xff3a1820, 0xff8a1424);
        else if (id == "bone") set (0xff161310, 0xff221e1a, 0xff24201b, 0xffd4b483, 0xfff7f1e6, 0xffa39888, 0xff3a332c, 0xfffff8ea, 0xff1c1814, 0xff3a332c, 0xffa89878);
        else if (id == "default") set (0xff0a0a0a, 0xff141414, 0xff141414, 0xffe62020, 0xfff4f4f4, 0xff9a9a9a, 0xff2c2c2c, 0xffff5555, 0xff0e0e0e, 0xff2c2c2c, 0xffa01818);
        else if (id == "goonr") set (0xff020804, 0xff04140c, 0xff04140c, 0xff39ff14, 0xffd8ffd0, 0xff5d8a62, 0xff145c22, 0xffb6ff9a, 0xff010a04, 0xff145c22, 0xff0d6b12);
        else if (id == "ice") set (0xff07141a, 0xff102028, 0xff10242c, 0xff9aebf5, 0xffe7f7fb, 0xff7f9aa4, 0xff1e3a44, 0xffe0fbff, 0xff0c1c22, 0xff1e3a44, 0xff3d8b9c);
        else if (id == "light") set (0xffefe6d6, 0xfff7f1e6, 0xfff7f1e6, 0xff9a3412, 0xff1c1410, 0xff6b5344, 0xffddcbb6, 0xffc2410c, 0xfff3eadc, 0xffddcbb6, 0xff7c2d12);
        else if (id == "moss") set (0xff08110c, 0xff102016, 0xff122016, 0xff6ecf6a, 0xffe7f6e4, 0xff7d9478, 0xff24382a, 0xffbbf7d0, 0xff0c1610, 0xff24382a, 0xff3f7d3c);
        else if (id == "neon") set (0xff070812, 0xff101426, 0xff101426, 0xff22f0e0, 0xffe8fbff, 0xff7d90a8, 0xff243044, 0xffff3dbe, 0xff0a0e1c, 0xff243044, 0xff0e8f86);
        else if (id == "rust") set (0xff140a07, 0xff1c0e0a, 0xff24140e, 0xffe07a3d, 0xfff6e7dc, 0xffb08974, 0xff4a2c1e, 0xfffdba74, 0xff1a0e0a, 0xff4a2c1e, 0xff9a4e22);
        else if (id == "sulfur") set (0xff070804, 0xff12160a, 0xff12180c, 0xffc6f531, 0xfff3f8d8, 0xff8d9a68, 0xff2c3814, 0xffeaff9a, 0xff0c1008, 0xff2c3814, 0xff6d8f14);
        else if (id == "trippah") set (0xff0e0608, 0xff1a0a10, 0xff1a0c12, 0xffc04068, 0xfff4e4ea, 0xff9a7884, 0xff341820, 0xffe07090, 0xff12080c, 0xff341820, 0xff7a2844);
        else if (id == "violet") set (0xff0c0814, 0xff140c22, 0xff181028, 0xffc084fc, 0xfff3e8ff, 0xffa78bb8, 0xff342450, 0xffe9d5ff, 0xff120c1c, 0xff342450, 0xff7e22ce);
        else if (id == "void") set (0xff03050c, 0xff02040a, 0xff0a1020, 0xff7aa2e3, 0xffe6eefc, 0xff7d8eae, 0xff1a2740, 0xffdbe7ff, 0xff060a14, 0xff1a2740, 0xff345084);
        else if (id == "wine") set (0xff12060c, 0xff16080e, 0xff241018, 0xffa33b5c, 0xfff6e6ea, 0xffb08a96, 0xff3d2030, 0xffe7b0c0, 0xff180810, 0xff3d2030, 0xff6e243c);
        else applyPalette ("trippah");
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
            updatePalette();
        }

        void updatePalette()
        {
            using namespace theme;
            setColour (juce::ResizableWindow::backgroundColourId, bg);
            setColour (juce::TextButton::buttonColourId, panel);
            setColour (juce::TextButton::buttonOnColourId, chipOn);
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextButton::textColourOnId, pink);
            setColour (juce::TextEditor::backgroundColourId, bg);
            setColour (juce::TextEditor::textColourId, text);
            setColour (juce::TextEditor::outlineColourId, border);
            setColour (juce::TextEditor::focusedOutlineColourId, accent);
            setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.45f));
            setColour (juce::TextEditor::highlightedTextColourId, text);
            setColour (juce::CaretComponent::caretColourId, text);
            setColour (juce::Label::textColourId, text);
            setColour (juce::ScrollBar::thumbColourId, accent.withAlpha (0.5f));
            setColour (juce::PopupMenu::backgroundColourId, panel);
            setColour (juce::PopupMenu::textColourId, text);
            setColour (juce::PopupMenu::headerTextColourId, dim);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, chipOn);
            setColour (juce::PopupMenu::highlightedTextColourId, text);
        }
    };
}
