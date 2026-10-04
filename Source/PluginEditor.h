#pragma once
#include "PluginProcessor.h"
#include "FeedView.h"
#include "EmojiGrid.h"
#include "Theme.h"

class DreamShareEditor : public juce::AudioProcessorEditor,
                         private ds::Client::Listener,
                         private juce::Timer
{
public:
    explicit DreamShareEditor (DreamShareProcessor&);
    ~DreamShareEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Tab   { chat, threads };
    enum class TMode { list, compose, detail };

    // ds::Client::Listener
    void clientChanged() override;
    void clientStatus (const juce::String&) override;
    void timerCallback() override;

    void setStatus (const juce::String&);
    void refreshAll();
    void rebuildChat();
    void rebuildThreadList();
    void rebuildDetail();
    void updateHeader();
    void updateVisibility();
    void updateTheme();
    void selectTheme();

    void doLogin();
    void sendChat();
    void postThread();
    void postComment();
    void openThread (const juce::String& id);
    void act (const ds::Item&, bool isDelete);
    void toggleReaction (const ds::Item&, const juce::String& key);
    void pickReaction (const ds::Item&, juce::Rectangle<int> screenRect);
    void pickEmoji (juce::TextEditor& target, juce::Component& anchor);
    void showUserMenu (const juce::String& name, juce::Point<int> screenPos);
    void showOnline();
    bool canModerate (const ds::Snapshot&, const ds::Session&) const;

    DreamShareProcessor& proc;
    ds::Client& client;
    ds::LookAndFeel laf;

    Tab tab = Tab::chat;
    TMode tmode = TMode::list;
    juce::String openId;
    juce::uint32 animationTick = 0;

    // header
    juce::Label whoLabel, statusLabel;
    juce::TextButton onlineBtn { "o 0" }, logoutBtn { "Logout" }, chatTab { "Chat" }, threadsTab { "Threads" };
    juce::ComboBox themeBox;
    std::vector<ds::ThemeChoice> themeChoices;
    juce::String appliedTheme;
    bool syncingThemeBox = false;

    // login
    juce::Label loginInfo, loginMsg;
    juce::TextEditor userEd, passEd;
    juce::TextButton loginBtn { "Sign in" };

    // chat
    ds::FeedView chatFeed;
    juce::TextEditor chatInput;
    ds::GlyphButton chatEmoji;
    juce::TextButton chatSend { "Send" };

    // threads: list / compose / detail
    ds::FeedView threadFeed, detailFeed;
    juce::TextButton newThreadBtn { "+ New thread" }, backBtn { "< Back" };
    juce::TextEditor composeTitle, composeBody, commentInput;
    juce::Label composeCount;
    ds::GlyphButton composeEmoji;
    juce::TextButton composePost { "Post thread" }, composeCancel { "Cancel" };
    ds::GlyphButton commentEmoji;
    juce::TextButton commentSend { "Reply" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DreamShareEditor)
};
