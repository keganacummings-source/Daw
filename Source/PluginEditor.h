#pragma once
#include "PluginProcessor.h"
#include "FeedView.h"
#include "EmojiGrid.h"
#include "Theme.h"

class DreamShareEditor : public juce::AudioProcessorEditor,
                         private ds::Client::Listener
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

    void setStatus (const juce::String&);
    void refreshAll();
    void rebuildChat();
    void rebuildThreadList();
    void rebuildDetail();
    void updateHeader();
    void updateVisibility();

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

    // header
    juce::Label whoLabel, statusLabel;
    juce::TextButton onlineBtn { "o 0" }, logoutBtn { "Logout" }, chatTab { "Chat" }, threadsTab { "Threads" };

    // login
    juce::Label loginInfo, loginMsg;
    juce::TextEditor userEd, passEd;
    juce::TextButton loginBtn { "Sign in" };

    // chat
    ds::FeedView chatFeed;
    juce::TextEditor chatInput;
    juce::TextButton chatEmoji { "" }, chatSend { "Send" };

    // threads: list / compose / detail
    ds::FeedView threadFeed, detailFeed;
    juce::TextButton newThreadBtn { "+ New thread" }, backBtn { "< Back" };
    juce::TextEditor composeTitle, composeBody, commentInput;
    juce::Label composeCount;
    juce::TextButton composeEmoji { "" }, composePost { "Post thread" }, composeCancel { "Cancel" };
    juce::TextButton commentEmoji { "" }, commentSend { "Reply" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DreamShareEditor)
};
