#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <utility>
#include <vector>
#include "Theme.h"

namespace ds
{
    using EmojiList = std::vector<std::pair<juce::String, juce::String>>;   // key, glyph

    // The 8 server-side reaction keys. The worker rejects anything else.
    inline const EmojiList& reactionSet()
    {
        static const EmojiList r {
            { "up",    theme::cp ({ 0x1F44D }) },
            { "heart", theme::cp ({ 0x2764, 0xFE0F }) },
            { "fire",  theme::cp ({ 0x1F525 }) },
            { "laugh", theme::cp ({ 0x1F602 }) },
            { "skull", theme::cp ({ 0x1F480 }) },
            { "moon",  theme::cp ({ 0x1F319 }) },
            { "eyes",  theme::cp ({ 0x1F440 }) },
            { "100",   theme::cp ({ 0x1F4AF }) } };
        return r;
    }

    // Emoji for the chat / thread / comment boxes (plain text, so any of them can be posted).
    inline const EmojiList& emojiPalette()
    {
        static const EmojiList p = []
        {
            const juce::juce_wchar codes[] = {
                0x1F600, 0x1F601, 0x1F602, 0x1F923, 0x1F60A, 0x1F60D, 0x1F60E, 0x1F972,
                0x1F62D, 0x1F634, 0x1F914, 0x1F605, 0x1F642, 0x1F62E, 0x1F624, 0x1F973,
                0x1F44D, 0x1F44E, 0x1F44F, 0x1F64C, 0x1F64F, 0x1F4AA, 0x1F91D, 0x1F918,
                0x2764,  0x1F494, 0x1F525, 0x2728,  0x1F389, 0x1F4AF, 0x1F480, 0x1F440,
                0x1F3B5, 0x1F3B6, 0x1F3B9, 0x1F3B8, 0x1F941, 0x1F3A7, 0x1F3A4, 0x1F50A,
                0x1F319, 0x2B50,  0x1F308, 0x1F4A4 };
            EmojiList l;
            for (auto c : codes)
            {
                auto g = theme::cp ({ c });
                if (c == 0x2764) g += theme::cp ({ 0xFE0F });
                l.emplace_back (g, g);
            }
            return l;
        }();
        return p;
    }

    /** Button that paints its emoji itself. (JUCE's TextButton swaps wide emoji for "..." .) */
    class GlyphButton : public juce::Button
    {
    public:
        explicit GlyphButton (const juce::String& g = {}) : juce::Button ("emoji"), glyph (g) {}
        void setGlyph (const juce::String& g) { glyph = g; repaint(); }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (down ? juce::Colour (0xff3a1418) : over ? juce::Colour (0xff2a1820) : juce::Colour (0xff1a1216));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (theme::border);
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
            g.setColour (theme::text);
            g.setFont (theme::font (18.0f));
            g.drawText (glyph, getLocalBounds(), juce::Justification::centred, false);
        }

    private:
        juce::String glyph;
    };

    /** Small grid of emoji buttons for use inside a CallOutBox. */
    class EmojiGrid : public juce::Component
    {
    public:
        EmojiGrid (const EmojiList& items, int cols, std::function<void (const juce::String& key, const juce::String& glyph)> pick)
        {
            const int cell = 36;
            cols = juce::jmax (1, juce::jmin (cols, (int) items.size()));
            const int rows = ((int) items.size() + cols - 1) / cols;
            for (size_t i = 0; i < items.size(); ++i)
            {
                auto* b = buttons.add (new GlyphButton (items[i].second));
                b->setBounds (6 + ((int) i % cols) * cell, 6 + ((int) i / cols) * cell, cell - 2, cell - 2);
                const auto key = items[i].first, glyph = items[i].second;
                b->onClick = [this, pick, key, glyph]
                {
                    pick (key, glyph);
                    if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
                };
                addAndMakeVisible (b);
            }
            setSize (12 + cols * cell, 12 + rows * cell);
        }

    private:
        juce::OwnedArray<GlyphButton> buttons;
    };
}
