#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "DreamClient.h"
#include "Theme.h"

namespace ds
{
    /** One card in a feed: a chat line, a thread summary, a thread header or a comment. */
    struct Item
    {
        juce::String kind;       // "chat" | "thread" | "comment"
        juce::String id, threadId, user, title, body, meta, me;
        juce::StringArray badges;
        Reactions reactions;
        bool canDelete = false, clickable = false;

        // --- filled in by FeedView::layoutItems() ---
        struct Chip  { juce::String key, label; juce::Rectangle<int> r; bool on = false; };
        struct BadgeR { juce::String label; juce::Rectangle<int> r; };
        int y = 0, h = 0;
        juce::TextLayout titleL, bodyL;
        juce::Rectangle<int> nameR, delR, titleR, metaR, bodyR, addR;
        std::vector<BadgeR> badgeRs;
        std::vector<Chip> chips;
    };

    /** Scrolling list that paints its cards directly (no child component per message). */
    class FeedView : public juce::Viewport
    {
    public:
        FeedView();

        void setItems (std::vector<Item> newItems, bool stickToBottom);
        void setEmptyText (const juce::String& t) { emptyText = t; }
        void scrollToBottom();

        std::function<void (const Item&)> onOpen, onDelete;
        std::function<void (const juce::String& user, juce::Point<int> screenPos)> onUser;
        std::function<void (const Item&, const juce::String& key)> onReact;
        std::function<void (const Item&, juce::Rectangle<int> screenRect)> onAddReact;

        void resized() override;

        // used by the inner content component
        void paintContent (juce::Graphics&);
        void clicked (juce::Point<int>, const juce::MouseEvent&);

    private:
        void layoutItems();
        juce::Component* content = nullptr;
        std::vector<Item> items;
        juce::String emptyText { "Nothing here yet." };
        bool firstFill = true;
    };
}
