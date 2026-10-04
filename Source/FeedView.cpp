#include "FeedView.h"
#include "EmojiGrid.h"
#include <cmath>

namespace ds
{
using namespace theme;

namespace
{
    void makeLayout (juce::TextLayout& tl, const juce::String& s, const juce::Font& f, juce::Colour c, int width)
    {
        juce::AttributedString a;
        a.setWordWrap (juce::AttributedString::byWord);
        a.append (s, f, c);
        tl = juce::TextLayout();
        tl.createLayout (a, (float) width);
    }
    int heightOf (const juce::TextLayout& tl) { return (int) std::ceil (tl.getHeight()); }
}

struct FeedContent : public juce::Component
{
    explicit FeedContent (FeedView& o) : owner (o) {}
    void paint (juce::Graphics& g) override { owner.paintContent (g); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasDraggedSinceMouseDown()) owner.clicked (e.getPosition(), e);
    }
    FeedView& owner;
};

FeedView::FeedView()
{
    content = new FeedContent (*this);
    setViewedComponent (content, true);
    setScrollBarsShown (true, false);
    setScrollBarThickness (8);
}

void FeedView::resized()
{
    juce::Viewport::resized();
    layoutItems();
}

void FeedView::scrollToBottom()
{
    setViewPosition (0, juce::jmax (0, content->getHeight() - getMaximumVisibleHeight()));
}

void FeedView::setItems (std::vector<Item> newItems, bool stickToBottom)
{
    const int oldY = getViewPositionY();
    const bool wasAtBottom = firstFill || oldY + getMaximumVisibleHeight() >= content->getHeight() - 48;
    firstFill = false;

    items = std::move (newItems);
    layoutItems();

    if (stickToBottom && wasAtBottom) scrollToBottom();
    else                              setViewPosition (0, oldY);
}

void FeedView::layoutItems()
{
    const int w = juce::jmax (140, getWidth() - getScrollBarThickness());   // always reserve the scrollbar lane
    const int x0 = 14, cw = w - 28;
    const auto nameFont = font (12.5f, true), badgeFont = font (9.0f, true), metaFont = font (11.0f);
    const auto chipFont = font (12.0f);
    int y = 6;

    for (auto& it : items)
    {
        it.y = y;
        int cy = y + 8;

        // header: name, role badges, delete
        const int nw = (int) std::ceil (textWidth (it.user, nameFont)) + 2;
        it.nameR = { x0, cy, juce::jmin (nw, cw), 16 };
        it.badgeRs.clear();
        int bx = it.nameR.getRight() + 6;
        for (auto& b : it.badges)
        {
            const int bw = (int) std::ceil (textWidth (b, badgeFont)) + 10;
            it.badgeRs.push_back ({ b, { bx, cy + 2, bw, 13 } });
            bx += bw + 4;
        }
        it.delR = it.canDelete ? juce::Rectangle<int> (w - 6 - 8 - 34, cy, 34, 16) : juce::Rectangle<int>();
        cy += 20;

        if (it.title.isNotEmpty())
        {
            makeLayout (it.titleL, it.title, font (14.0f, true), text, cw);
            it.titleR = { x0, cy, cw, heightOf (it.titleL) };
            cy += it.titleR.getHeight() + 3;
        }
        else it.titleR = {};

        if (it.meta.isNotEmpty())
        {
            it.metaR = { x0, cy, cw, 14 };
            cy += 17;
        }
        else it.metaR = {};

        if (it.body.isNotEmpty())
        {
            makeLayout (it.bodyL, it.body, font (13.0f), text, cw);
            it.bodyR = { x0, cy, cw, heightOf (it.bodyL) };
            cy += it.bodyR.getHeight() + 6;
        }
        else it.bodyR = {};

        // reaction chips (only reactions that exist) + the "add" chip
        it.chips.clear();
        int rx = x0;
        const int chipH = 22;
        for (auto& pr : reactionSet())
        {
            auto f = it.reactions.find (pr.first);
            if (f == it.reactions.end() || f->second.isEmpty()) continue;
            Item::Chip c;
            c.key = pr.first;
            c.label = pr.second + " " + juce::String (f->second.size());
            c.on = f->second.contains (it.me, true);
            const int cwid = (int) std::ceil (textWidth (c.label, chipFont)) + 16;
            if (rx + cwid > x0 + cw && rx > x0) { rx = x0; cy += chipH + 4; }
            c.r = { rx, cy, cwid, chipH };
            rx += cwid + 4;
            it.chips.push_back (std::move (c));
        }
        if (rx + 30 > x0 + cw && rx > x0) { rx = x0; cy += chipH + 4; }
        it.addR = { rx, cy, 30, chipH };
        cy += chipH;

        it.h = (cy + 8) - it.y + 6;   // 6 = gap between cards
        y += it.h;
    }

    const int total = y + 4;
    content->setSize (w, juce::jmax (total, getMaximumVisibleHeight()));
}

void FeedView::paintContent (juce::Graphics& g)
{
    const int w = content->getWidth();

    if (items.empty())
    {
        g.setColour (dim);
        g.setFont (font (12.5f));
        g.drawFittedText (emptyText, 16, 0, w - 32, juce::jmin (content->getHeight(), 120),
                          juce::Justification::centred, 3);
        return;
    }

    const auto clip = g.getClipBounds();
    for (auto& it : items)
    {
        if (it.y + it.h < clip.getY() || it.y > clip.getBottom()) continue;

        const juce::Rectangle<float> card (6.0f, (float) it.y, (float) (w - 12), (float) (it.h - 6));
        g.setColour (panel);
        g.fillRoundedRectangle (card, 7.0f);
        g.setColour (border);
        g.drawRoundedRectangle (card.reduced (0.5f), 7.0f, 1.0f);

        if (it.user.isNotEmpty())
        {
            g.setColour (pink);
            g.setFont (font (12.5f, true));
            g.drawText (it.user, it.nameR, juce::Justification::centredLeft, true);
        }
        for (auto& b : it.badgeRs)
        {
            const auto bc = badge (b.label);
            g.setColour (bc.fill);  g.fillRoundedRectangle (b.r.toFloat(), 3.0f);
            g.setColour (bc.edge);  g.drawRoundedRectangle (b.r.toFloat().reduced (0.5f), 3.0f, 1.0f);
            g.setColour (bc.ink);   g.setFont (font (9.0f, true));
            g.drawText (b.label, b.r, juce::Justification::centred, false);
        }
        if (it.canDelete)
        {
            g.setColour (juce::Colour (0xff1a1216)); g.fillRoundedRectangle (it.delR.toFloat(), 5.0f);
            g.setColour (border);                    g.drawRoundedRectangle (it.delR.toFloat().reduced (0.5f), 5.0f, 1.0f);
            g.setColour (dim);  g.setFont (font (10.5f));
            g.drawText ("del", it.delR, juce::Justification::centred, false);
        }

        if (! it.titleR.isEmpty()) it.titleL.draw (g, it.titleR.toFloat());
        if (! it.metaR.isEmpty())
        {
            g.setColour (dim); g.setFont (font (11.0f));
            g.drawText (it.meta, it.metaR, juce::Justification::centredLeft, true);
        }
        if (! it.bodyR.isEmpty()) it.bodyL.draw (g, it.bodyR.toFloat());

        g.setFont (font (12.0f));
        for (auto& c : it.chips)
        {
            g.setColour (c.on ? chipOn : chipBg);  g.fillRoundedRectangle (c.r.toFloat(), 11.0f);
            g.setColour (c.on ? accent : chipBd);  g.drawRoundedRectangle (c.r.toFloat().reduced (0.5f), 11.0f, 1.0f);
            g.setColour (text);
            g.drawText (c.label, c.r, juce::Justification::centred, false);
        }
        g.setColour (chipBg);  g.fillRoundedRectangle (it.addR.toFloat(), 11.0f);
        g.setColour (chipBd);  g.drawRoundedRectangle (it.addR.toFloat().reduced (0.5f), 11.0f, 1.0f);
        g.setColour (dim);     g.setFont (font (14.0f));
        g.drawText ("+", it.addR, juce::Justification::centred, false);
    }
}

void FeedView::clicked (juce::Point<int> p, const juce::MouseEvent& e)
{
    for (auto& it : items)
    {
        if (p.y < it.y || p.y >= it.y + it.h) continue;

        const Item copy = it;   // callbacks may rebuild the list
        if (copy.canDelete && copy.delR.contains (p))      { if (onDelete) onDelete (copy); return; }
        for (auto& c : copy.chips)
            if (c.r.contains (p))                          { if (onReact) onReact (copy, c.key); return; }
        if (copy.addR.contains (p))
        {
            if (onAddReact) onAddReact (copy, content->localAreaToGlobal (copy.addR));
            return;
        }
        if (copy.user.isNotEmpty() && copy.nameR.contains (p))
        {
            if (onUser) onUser (copy.user, e.getScreenPosition());
            return;
        }
        if (copy.clickable && onOpen) onOpen (copy);
        return;
    }
}
}
