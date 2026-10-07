#include "PluginEditor.h"

using namespace ds;
namespace T = ds::theme;

namespace
{
    juce::StringArray badgesFor (const Snapshot& s, const juce::String& u)
    {
        juce::StringArray b;
        if (s.isSuper (u))    b.add ("ADMIN");
        else if (s.isMod (u)) b.add ("MOD");
        if (s.hasKyoto (u))   b.add ("KYOTO");
        const auto custom = s.customRoles.find (u.toLowerCase());
        if (custom != s.customRoles.end()) b.addIfNotAlreadyThere (custom->second.label);
        return b;
    }

    juce::Colour badgeColour (juce::String value, juce::Colour fallback)
    {
        value = value.trim().replace ("#", "");
        if (value.length() == 3)
        {
            juce::String expanded;
            for (int i = 0; i < value.length(); ++i)
                expanded += juce::String::charToString (value[i]) + juce::String::charToString (value[i]);
            value = expanded;
        }
        if (value.length() != 6) return fallback;
        return juce::Colour::fromString ("ff" + value);
    }

    std::map<juce::String, T::Badge> badgeStylesFor (const Snapshot& s, const juce::String& u)
    {
        std::map<juce::String, T::Badge> styles;
        const auto custom = s.customRoles.find (u.toLowerCase());
        if (custom != s.customRoles.end())
        {
            const auto ink = badgeColour (custom->second.color, juce::Colour (0xffd5e0ff));
            const auto fill = badgeColour (custom->second.bg, juce::Colour (0xff1a2438));
            styles[custom->second.label] = { fill, ink, ink.withAlpha (0.65f) };
        }
        return styles;
    }

    std::vector<ThemeChoice> defaultThemes()
    {
        return { { "amber", "Amber" }, { "ash", "Ash" }, { "bloodmoon", "Bloodmoon" },
                 { "bone", "Bone" }, { "default", "Default" }, { "goonr", "GOONR" },
                 { "ice", "Ice" }, { "light", "Light" }, { "moss", "Moss" },
                 { "neon", "Neon" }, { "rust", "Rust" }, { "sulfur", "Sulfur" },
                 { "trippah", "TRIPPAH" }, { "violet", "Violet" }, { "void", "Void" },
                 { "wine", "Wine" } };
    }

    juce::String when (juce::int64 ms) { return juce::Time (ms).formatted ("%d %b %H:%M"); }

    void show (std::initializer_list<juce::Component*> l, bool v) { for (auto* c : l) c->setVisible (v); }

    void styleEditor (juce::TextEditor& e, const juce::String& hint, int maxLen, bool multi)
    {
        e.setMultiLine (multi, multi);
        e.setReturnKeyStartsNewLine (multi);
        e.setScrollbarsShown (multi);
        e.setFont (T::font (13.0f));
        e.setTextToShowWhenEmpty (hint, T::dim);
        e.setInputRestrictions (maxLen);
    }
}

DreamShareEditor::DreamShareEditor (DreamShareProcessor& p)
    : AudioProcessorEditor (p), proc (p), client (p.getClient())
{
    T::applyPalette (client.getSession().theme.isNotEmpty() ? client.getSession().theme : "trippah");
    setLookAndFeel (&laf);
    setResizable (true, true);
    setResizeLimits (380, 360, 1400, 1000);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &whoLabel, &statusLabel, &onlineBtn, &logoutBtn, &chatTab, &threadsTab, &dmsTab, &discordTab, &themeBox,
             &loginInfo, &loginMsg, &userEd, &passEd, &loginBtn,
             &chatFeed, &chatInput, &chatEmoji, &chatSend,
             &dmFeed, &dmPeerBox, &dmInput, &dmSend, &dmRequests,
             &discordFeed, &discordChannels, &discordNote, &discordInput, &discordRefresh, &discordSend,
             &threadFeed, &detailFeed, &newThreadBtn, &backBtn,
             &composeTitle, &composeBody, &commentInput, &composeCount, &composeAudioBtn, &composeAudioLabel,
             &composeEmoji, &composePost, &composeCancel, &commentEmoji, &commentSend })
        addChildComponent (c);
    statusLabel.setVisible (true);

    // header
    whoLabel.setFont (T::font (11.0f));      whoLabel.setColour (juce::Label::textColourId, T::ok);
    whoLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setFont (T::font (11.0f));   statusLabel.setColour (juce::Label::textColourId, T::dim);
    statusLabel.setText ("DREAMSHARE LITE", juce::dontSendNotification);
    onlineBtn.onClick = [this] { showOnline(); };
    logoutBtn.onClick = [this] { client.logout(); };
    themeBox.setTextWhenNothingSelected ("Theme");
    themeBox.onChange = [this] { selectTheme(); };
    themeChoices = defaultThemes();
    for (int i = 0; i < (int) themeChoices.size(); ++i)
        themeBox.addItem (themeChoices[(size_t) i].name, i + 1);

    for (auto* b : { &chatTab, &threadsTab, &dmsTab, &discordTab })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (7);
    }
    chatTab.setToggleState (true, juce::dontSendNotification);
    chatTab.onClick = [this]
    {
        tab = Tab::chat;
        client.setActiveChannel (ds::ActiveChannel::feed);
        updateVisibility();
        rebuildChat();
    };
    threadsTab.onClick = [this]
    {
        tab = Tab::threads;
        client.setActiveChannel (ds::ActiveChannel::feed);
        updateVisibility();
        rebuildThreadList();
        if (tmode == TMode::detail) rebuildDetail();
    };
    dmsTab.onClick = [this]
    {
        tab = Tab::dms;
        client.setActiveChannel (ds::ActiveChannel::dms);
        updateVisibility();
        rebuildDms();
    };
    discordTab.onClick = [this]
    {
        tab = Tab::discord;
        client.setActiveChannel (ds::ActiveChannel::discord);
        updateVisibility();
        loadDiscordChannels();
    };

    // login
    loginInfo.setFont (T::font (12.0f));
    loginInfo.setColour (juce::Label::textColourId, T::dim);
    loginInfo.setText ("Sign in with your DreamShare name. A name nobody has used yet creates a new account.",
                       juce::dontSendNotification);
    loginMsg.setFont (T::font (11.5f));
    loginMsg.setColour (juce::Label::textColourId, T::pink);
    styleEditor (userEd, "username", 20, false);
    userEd.setInputRestrictions (20, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_");
    styleEditor (passEd, "password", 200, false);
    passEd.setPasswordCharacter ((juce::juce_wchar) '*');
    userEd.onReturnKey = [this] { passEd.grabKeyboardFocus(); };
    passEd.onReturnKey = [this] { doLogin(); };
    loginBtn.onClick   = [this] { doLogin(); };
    loginBtn.setColour (juce::TextButton::buttonColourId, T::accent);
    loginBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);

    // emoji buttons
    for (auto* b : { &chatEmoji, &composeEmoji, &commentEmoji })
        b->setGlyph (T::cp ({ 0x1F600 }));
    chatEmoji.onClick    = [this] { pickEmoji (chatInput, chatEmoji); };
    composeEmoji.onClick = [this] { pickEmoji (composeBody, composeEmoji); };
    commentEmoji.onClick = [this] { pickEmoji (commentInput, commentEmoji); };

    // chat
    styleEditor (chatInput, "Say something...", 400, false);
    chatInput.onReturnKey = [this] { sendChat(); };
    chatSend.onClick      = [this] { sendChat(); };
    chatFeed.setEmptyText ("No messages yet. Say hello.");

    // DMs
    dmPeerBox.setTextWhenNothingSelected ("Choose a user");
    dmPeerBox.onChange = [this] { selectDmPeer(); };
    dmFeed.setEmptyText ("Choose a user to open a private conversation.");
    styleEditor (dmInput, "Private message...", 1000, false);
    dmInput.onReturnKey = [this] { sendDm(); };
    dmSend.onClick = [this] { sendDm(); };
    dmRequests.onClick = [this] { showSocialRequests(); };

    discordNote.setFont (T::font (11.0f));
    discordNote.setColour (juce::Label::textColourId, T::dim);
    discordNote.setText ("Discord lite. Posts as the DreamShare bot, not your account.", juce::dontSendNotification);
    discordChannels.setTextWhenNothingSelected ("Channel");
    discordChannels.onChange = [this] { loadDiscordMessages(); };
    discordFeed.setEmptyText ("Pick a channel. Message text needs the Message Content Intent.");
    discordFeed.onReact = [this] (const ds::Item& it, const juce::String& key) { reactDiscord (it, key); };
    styleEditor (discordInput, "Message as the bot...", 2000, false);
    discordInput.onReturnKey = [this] { sendDiscord(); };
    discordSend.onClick = [this] { sendDiscord(); };
    discordRefresh.onClick = [this] { loadDiscordChannels(); };

    // threads
    threadFeed.setEmptyText ("No threads yet. Start one with + New thread.");
    newThreadBtn.onClick = [this] { tmode = TMode::compose; updateVisibility(); composeTitle.grabKeyboardFocus(); };
    backBtn.onClick      = [this] { tmode = TMode::list; openId = {}; updateVisibility(); rebuildThreadList(); };

    styleEditor (composeTitle, "Thread title", 120, false);
    styleEditor (composeBody,  "What do you want to share?", 1000, true);
    styleEditor (commentInput, "Write a reply...", 500, false);
    composeCount.setFont (T::font (11.0f));
    composeCount.setColour (juce::Label::textColourId, T::dim);
    composeCount.setText ("0/120 " + T::cp ({ 0xB7 }) + " 0/1000", juce::dontSendNotification);
    auto upd = [this]
    {
        composeCount.setText (juce::String (composeTitle.getText().length()) + "/120 " + T::cp ({ 0xB7 }) + " "
                              + juce::String (composeBody.getText().length()) + "/1000", juce::dontSendNotification);
    };
    composeTitle.onTextChange = upd;
    composeBody.onTextChange  = upd;
    composeAudioLabel.setFont(T::font(10.5f));
    composeAudioLabel.setColour(juce::Label::textColourId,T::dim);
    composeAudioLabel.setText("No WAV attached",juce::dontSendNotification);
    composeAudioBtn.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Choose WAV to share", juce::File(), "*.wav");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
                                  {
                                      composeAudioFile = chooser.getResult();
                                      if (composeAudioFile.existsAsFile())
                                          composeAudioLabel.setText (composeAudioFile.getFileName(), juce::dontSendNotification);
                                      fileChooser.reset();
                                  });
    };
    composeTitle.onReturnKey  = [this] { composeBody.grabKeyboardFocus(); };
    composePost.onClick       = [this] { postThread(); };
    composeCancel.onClick     = [this] { tmode = TMode::list; updateVisibility(); };
    composePost.setColour (juce::TextButton::buttonColourId, T::accent);
    composePost.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    commentInput.onReturnKey  = [this] { postComment(); };
    commentSend.onClick       = [this] { postComment(); };

    // shared feed callbacks
    for (auto* f : { &chatFeed, &threadFeed, &detailFeed })
    {
        f->onUser     = [this] (const juce::String& u, juce::Point<int> pos) { showUserMenu (u, pos); };
        f->onDelete   = [this] (const Item& it) { act (it, true); };
        f->onReact    = [this] (const Item& it, const juce::String& key) { toggleReaction (it, key); };
        f->onAddReact = [this] (const Item& it, juce::Rectangle<int> r) { pickReaction (it, r); };
    }
    threadFeed.onOpen = [this] (const Item& it) { openThread (it.id); };
    dmFeed.onOpen = [this] (const Item& it)
    {
        if (it.audioParts <= 0 || it.id.isEmpty()) return;
        fileChooser = std::make_unique<juce::FileChooser> ("Save private WAV", juce::File(), "*.wav");
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this, it] (const juce::FileChooser& chooser)
                                  {
                                      const auto destination = chooser.getResult();
                                      fileChooser.reset();
                                      if (! destination.getFullPathName().isEmpty())
                                          client.downloadDmWav (it.id, it.audioParts, destination,
                                                                [this] (bool ok, const juce::String& err)
                                                                { setStatus (ok ? "Private WAV saved" : err); });
                                  });
    };

    setSize (juce::jlimit(420,1400,proc.editorW), juce::jlimit(380,1000,proc.editorH));
    client.setActiveChannel (ds::ActiveChannel::feed);
    refreshAll();
    client.addListener (this);     // polling starts now
}

DreamShareEditor::~DreamShareEditor()
{
    client.removeListener (this);  // polling stops when the last editor closes
    setLookAndFeel (nullptr);
}

// ------------------------------------------------------------------ painting / layout
void DreamShareEditor::paint (juce::Graphics& g)
{
    g.fillAll (T::bg);
    // Static background only: avoid per-frame animation while the DAW is resizing or scrolling.
    g.setColour (T::bg.withAlpha (0.98f));
    g.fillRect (0, 34, getWidth(), juce::jmax (0, getHeight() - 55));
    g.setColour (T::header);  g.fillRect (0, 0, getWidth(), 40);
    g.setColour (T::border);  g.drawHorizontalLine (39, 0.0f, (float) getWidth());
    g.setColour (T::border);  g.drawHorizontalLine (getHeight() - 21, 0.0f, (float) getWidth());
    g.setColour (T::pink);
    g.setFont (T::font (12.5f, true));
    g.drawText ("DREAMSHARE", 12, 0, 92, 40, juce::Justification::centredLeft, false);
    g.setColour (T::dim);
    g.setFont (T::font (10.0f, true));
    g.drawText ("LITE", 104, 2, 40, 40, juce::Justification::centredLeft, false);
}

void DreamShareEditor::resized()
{
    proc.editorW = getWidth();
    proc.editorH = getHeight();

    auto r = getLocalBounds();
    auto top = r.removeFromTop (40).reduced (8, 6);
    logoutBtn.setBounds (top.removeFromRight (58));  top.removeFromRight (6);
    onlineBtn.setBounds (top.removeFromRight (52));  top.removeFromRight (6);
    whoLabel.setBounds  (top.removeFromRight (90));
    statusLabel.setBounds (r.removeFromBottom (21).reduced (8, 0));
    auto tabs = r.removeFromTop (34).reduced (8, 3);
    auto themeArea = tabs.removeFromRight (juce::jmin (120, juce::jmax (100, tabs.getWidth() - 150)));
    themeBox.setBounds (themeArea);
    tabs.removeFromRight (6);
    chatTab.setBounds (tabs.removeFromLeft (58));  tabs.removeFromLeft (4);
    threadsTab.setBounds (tabs.removeFromLeft (72)); tabs.removeFromLeft (4);
    dmsTab.setBounds (tabs.removeFromLeft (58)); tabs.removeFromLeft (4);
    discordTab.setBounds (tabs.removeFromLeft (72));
    const auto body = r;

    {   // login
        auto box = body.withSizeKeepingCentre (juce::jmin (300, body.getWidth() - 24), 220);
        loginInfo.setBounds (box.removeFromTop (52));  box.removeFromTop (6);
        userEd.setBounds (box.removeFromTop (30));     box.removeFromTop (6);
        passEd.setBounds (box.removeFromTop (30));     box.removeFromTop (8);
        loginBtn.setBounds (box.removeFromTop (32));   box.removeFromTop (6);
        loginMsg.setBounds (box);
    }
    {   // chat
        auto b = body;
        auto in = b.removeFromBottom (40).reduced (6, 5);
        chatSend.setBounds (in.removeFromRight (56));  in.removeFromRight (4);
        chatEmoji.setBounds (in.removeFromLeft (34));  in.removeFromLeft (4);
        chatInput.setBounds (in);
        chatFeed.setBounds (b);
    }
    {   // private DMs
        auto b = body;
        auto bar = b.removeFromTop (34).reduced (6, 4);
        dmPeerBox.setBounds (bar.removeFromLeft (juce::jmax (120, bar.getWidth() - 190)));
        bar.removeFromLeft (6);
        dmRequests.setBounds (bar.removeFromRight (108));
        auto in = b.removeFromBottom (40).reduced (6, 5);
        dmSend.setBounds (in.removeFromRight (56)); in.removeFromRight (4);
        dmInput.setBounds (in);
        dmFeed.setBounds (b);
    }
    {   // Discord lite
        auto b = body;
        auto bar = b.removeFromTop (34).reduced (6, 4);
        discordRefresh.setBounds (bar.removeFromRight (72));
        bar.removeFromRight (6);
        discordChannels.setBounds (bar);
        discordNote.setBounds (b.removeFromTop (18).reduced (8, 0));
        auto in = b.removeFromBottom (40).reduced (6, 5);
        discordSend.setBounds (in.removeFromRight (56)); in.removeFromRight (4);
        discordInput.setBounds (in);
        discordFeed.setBounds (b);
    }
    {   // thread list
        auto b = body;
        auto bar = b.removeFromTop (34).reduced (6, 4);
        newThreadBtn.setBounds (bar.removeFromLeft (120));
        threadFeed.setBounds (b);
    }
    {   // compose
        auto b = body.reduced (8, 6);
        composeTitle.setBounds (b.removeFromTop (30));  b.removeFromTop (6);
        auto btns = b.removeFromBottom (32);
        composePost.setBounds (btns.removeFromRight (110));  btns.removeFromRight (6);
        composeCancel.setBounds (btns.removeFromRight (70)); btns.removeFromRight(6);
        composeAudioBtn.setBounds(btns.removeFromRight(88)); btns.removeFromRight(6);
        composeEmoji.setBounds (btns.removeFromLeft (34));
        composeCount.setBounds (btns.withTrimmedLeft (6));
        composeAudioLabel.setBounds(btns.removeFromRight(150));
        b.removeFromBottom (6);
        composeBody.setBounds (b);
    }
    {   // thread detail
        auto b = body;
        auto bar = b.removeFromTop (34).reduced (6, 4);
        backBtn.setBounds (bar.removeFromLeft (70));
        auto in = b.removeFromBottom (40).reduced (6, 5);
        commentSend.setBounds (in.removeFromRight (56));  in.removeFromRight (4);
        commentEmoji.setBounds (in.removeFromLeft (34));  in.removeFromLeft (4);
        commentInput.setBounds (in);
        detailFeed.setBounds (b);
    }
}

// ------------------------------------------------------------------ state -> UI
void DreamShareEditor::setStatus (const juce::String& s) { statusLabel.setText (s, juce::dontSendNotification); }

void DreamShareEditor::clientChanged()                       { refreshAll(); }
void DreamShareEditor::clientStatus (const juce::String& s)  { setStatus (s); }
void DreamShareEditor::timerCallback()                       { /* visual effects intentionally disabled for DAW performance */ }

bool DreamShareEditor::canModerate (const Snapshot& s, const Session& me) const
{
    return me.valid() && (me.role == "super" || s.isMod (me.user));
}

void DreamShareEditor::refreshAll()
{
    updateHeader();
    updateVisibility();

    // Only rebuild the feed for the tab the user is actually looking at.
    // This cuts UI work and avoids fighting scroll while other channels update.
    if (! client.getSession().valid())
        return;

    switch (tab)
    {
        case Tab::chat:
            rebuildChat();
            break;
        case Tab::threads:
            rebuildThreadList();
            if (tmode == TMode::detail)
                rebuildDetail();
            break;
        case Tab::dms:
            rebuildDms();
            break;
        case Tab::discord:
            // Discord is on-demand (loadDiscordChannels / loadDiscordMessages).
            break;
    }
}

void DreamShareEditor::updateHeader()
{
    const auto me = client.getSession();
    const auto snap = client.getSnapshot();
    whoLabel.setText (me.user, juce::dontSendNotification);
    onlineBtn.setButtonText (T::cp ({ 0x25CF }) + " " + juce::String (snap ? snap->online.size() : 0));
    updateTheme();

    auto choices = snap && ! snap->themes.empty() ? snap->themes : defaultThemes();
    bool changed = choices.size() != themeChoices.size();
    if (! changed)
        for (size_t i = 0; i < choices.size(); ++i)
            if (choices[i].id != themeChoices[i].id || choices[i].name != themeChoices[i].name)
            {
                changed = true;
                break;
            }
    if (changed)
    {
        themeChoices = std::move (choices);
        syncingThemeBox = true;
        themeBox.clear (juce::dontSendNotification);
        for (int i = 0; i < (int) themeChoices.size(); ++i)
            themeBox.addItem (themeChoices[(size_t) i].name, i + 1);
        syncingThemeBox = false;
    }

    const auto activeTheme = me.theme.isNotEmpty() ? me.theme : "trippah";
    int selectedId = 0;
    for (int i = 0; i < (int) themeChoices.size(); ++i)
        if (themeChoices[(size_t) i].id == activeTheme) { selectedId = i + 1; break; }
    syncingThemeBox = true;
    themeBox.setSelectedId (selectedId, juce::dontSendNotification);
    themeBox.setEnabled (me.valid());
    syncingThemeBox = false;
}

void DreamShareEditor::updateTheme()
{
    const auto me = client.getSession();
    const auto themeId = me.theme.isNotEmpty() ? me.theme : "trippah";
    if (themeId == appliedTheme) return;
    appliedTheme = themeId;
    T::applyPalette (themeId);
    laf.updatePalette();
    whoLabel.setColour (juce::Label::textColourId, T::ok);
    statusLabel.setColour (juce::Label::textColourId, T::dim);
    loginInfo.setColour (juce::Label::textColourId, T::dim);
    loginMsg.setColour (juce::Label::textColourId, T::pink);
    composeCount.setColour (juce::Label::textColourId, T::dim);
    loginBtn.setColour (juce::TextButton::buttonColourId, T::accent);
    composePost.setColour (juce::TextButton::buttonColourId, T::accent);
    repaint();
    chatFeed.repaint();
    threadFeed.repaint();
    detailFeed.repaint();
    stopTimer();
}

void DreamShareEditor::selectTheme()
{
    if (syncingThemeBox || ! client.getSession().valid()) return;
    const int selected = themeBox.getSelectedId();
    if (selected <= 0 || selected > (int) themeChoices.size()) return;
    const auto id = themeChoices[(size_t) selected - 1].id;
    if (id == client.getSession().theme) return;
    themeBox.setEnabled (false);
    juce::Component::SafePointer<DreamShareEditor> safe (this);
    client.send (obj ({ { "action", "set_theme" }, { "theme", id } }),
                 [safe] (bool ok, const juce::String& error)
                 {
                     if (safe == nullptr) return;
                     safe->themeBox.setEnabled (safe->client.getSession().valid());
                     safe->setStatus (ok ? "Theme saved" : error);
                     if (! ok) safe->updateHeader();
                 });
}

void DreamShareEditor::updateVisibility()
{
    const bool in = client.getSession().valid();
    const bool c = in && tab == Tab::chat, t = in && tab == Tab::threads;

    show ({ &loginInfo, &loginMsg, &userEd, &passEd, &loginBtn }, ! in);
    show ({ &whoLabel, &onlineBtn, &logoutBtn, &chatTab, &threadsTab, &dmsTab, &discordTab, &themeBox }, in);
    show ({ &chatFeed, &chatInput, &chatEmoji, &chatSend }, c);
    const bool d = in && tab == Tab::dms;
    show ({ &dmFeed, &dmPeerBox, &dmInput, &dmSend, &dmRequests }, d);
    const bool disc = in && tab == Tab::discord;
    show ({ &discordFeed, &discordChannels, &discordNote, &discordInput, &discordRefresh, &discordSend }, disc);
    show ({ &newThreadBtn, &threadFeed }, t && tmode == TMode::list);
    show ({ &composeTitle, &composeBody, &composeCount, &composeAudioBtn, &composeAudioLabel, &composeEmoji, &composePost, &composeCancel },
          t && tmode == TMode::compose);
    show ({ &backBtn, &detailFeed, &commentInput, &commentEmoji, &commentSend }, t && tmode == TMode::detail);
}

void DreamShareEditor::rebuildChat()
{
    const auto snap = client.getSnapshot();
    const auto me = client.getSession();
    std::vector<Item> v;
    if (snap)
    {
        const bool mod = canModerate (*snap, me);
        for (auto& m : snap->chat)
        {
            Item it;
            it.kind = "chat";  it.id = m.id;  it.user = m.user;  it.body = m.text;
            it.reactions = m.reactions;  it.me = me.user;
            it.badges = badgesFor (*snap, m.user);
            it.badgeStyles = badgeStylesFor (*snap, m.user);
            it.canDelete = me.valid() && (mod || m.user.equalsIgnoreCase (me.user));
            v.push_back (std::move (it));
        }
    }
    chatFeed.setItems (std::move (v), true);
}

void DreamShareEditor::rebuildThreadList()
{
    const auto snap = client.getSnapshot();
    const auto me = client.getSession();
    std::vector<Item> v;
    if (snap)
        for (auto& t : snap->threads)
        {
            Item it;
            it.kind = "thread";  it.id = t.id;  it.threadId = t.id;  it.user = t.user;
            it.title = t.title.isNotEmpty() ? t.title : "untitled";
            it.meta = juce::String ((int) t.comments.size()) + " comments " + T::cp ({ 0xB7 }) + " " + when (t.at)
                      + (t.hasAudio ? " " + T::cp ({ 0xB7 }) + " has audio (play on dreamdaw.com)" : juce::String());
            it.body = t.text.length() > 140 ? t.text.substring (0, 140) + "..." : t.text;
            it.reactions = t.reactions;  it.me = me.user;
            if (snap->inChat (t.user)) it.badges = badgesFor (*snap, t.user);
            it.badgeStyles = badgeStylesFor (*snap, t.user);
            it.clickable = true;
            v.push_back (std::move (it));
        }
    threadFeed.setItems (std::move (v), false);
}

void DreamShareEditor::rebuildDetail()
{
    if (tmode != TMode::detail) return;
    const auto snap = client.getSnapshot();
    const auto me = client.getSession();
    const Topic* topic = nullptr;
    if (snap)
        for (auto& t : snap->threads) if (t.id == openId) { topic = &t; break; }

    if (topic == nullptr)
    {
        tmode = TMode::list;  openId = {};
        updateVisibility();
        setStatus ("That thread is no longer available.");
        return;
    }

    const bool mod = canModerate (*snap, me);
    std::vector<Item> v;

    Item head;
    head.kind = "thread";  head.id = topic->id;  head.threadId = topic->id;  head.user = topic->user;
    head.title = topic->title.isNotEmpty() ? topic->title : "untitled";
    head.meta = when (topic->at) + (topic->hasAudio ? " " + T::cp ({ 0xB7 }) + " has audio (play on dreamdaw.com)" : juce::String());
    head.body = topic->text;  head.reactions = topic->reactions;  head.me = me.user;
    if (snap->inChat (topic->user)) head.badges = badgesFor (*snap, topic->user);
    head.badgeStyles = badgeStylesFor (*snap, topic->user);
    head.canDelete = me.valid() && (mod || topic->user.equalsIgnoreCase (me.user));
    v.push_back (std::move (head));

    for (auto& c : topic->comments)
    {
        Item it;
        it.kind = "comment";  it.id = c.id;  it.threadId = topic->id;  it.user = c.user;
        it.meta = when (c.at);  it.body = c.text;  it.reactions = c.reactions;  it.me = me.user;
        if (snap->inChat (c.user)) it.badges = badgesFor (*snap, c.user);
        it.badgeStyles = badgeStylesFor (*snap, c.user);
        it.canDelete = me.valid() && (mod || c.user.equalsIgnoreCase (me.user));
        v.push_back (std::move (it));
    }
    detailFeed.setItems (std::move (v), false);
}


void DreamShareEditor::rebuildDms()
{
    if (tab != Tab::dms) return;
    const auto snap = client.getSnapshot();
    const auto me = client.getSession();
    if (!snap || !me.valid()) return;

    syncingThemeBox = true;
    const auto wanted = dmPeerBox.getText();
    dmPeerBox.clear(juce::dontSendNotification);
    int selected = 0, n = 0;
    for (const auto& u : snap->directory)
    {
        if (u.equalsIgnoreCase(me.user)) continue;
        dmPeerBox.addItem(u, ++n);
        if (u.equalsIgnoreCase(wanted)) selected = n;
    }
    if (selected > 0) dmPeerBox.setSelectedId(selected, juce::dontSendNotification);
    syncingThemeBox = false;

    std::vector<Item> v;
    for (const auto& m : snap->dms)
    {
        Item it;
        it.kind = "dm"; it.id = m.id; it.user = m.from; it.body = m.text;
        it.meta = when(m.at) + (m.audioParts > 0 ? "  •  WAV attachment • click to open" : "");
        it.audioUpload=m.audioUpload; it.audioStore=m.audioStore; it.audioParts=m.audioParts;
        it.me = me.user; it.clickable = (m.audioParts > 0);
        if (m.audioParts > 0 && it.body.isEmpty()) it.body = "WAV attachment";
        v.push_back(std::move(it));
    }
    dmFeed.setItems(std::move(v), true);
}

void DreamShareEditor::selectDmPeer()
{
    if (syncingThemeBox || !client.getSession().valid()) return;
    const auto peer = dmPeerBox.getText().trim();
    if (peer.isEmpty()) return;
    dmInput.clear();
    client.send(obj({{"action","dm_list"},{"peer",peer}}), [this](bool ok, const juce::String& err)
    {
        if (!ok) setStatus(err);
    });
}

void DreamShareEditor::sendDm()
{
    const auto peer = dmPeerBox.getText().trim();
    const auto text = dmInput.getText().trim();
    if (peer.isEmpty()) { setStatus("Choose a user first"); return; }
    if (text.isEmpty()) return;
    dmInput.clear();
    client.send(obj({{"action","dm_send"},{"to",peer},{"text",text}}),
                [this,text](bool ok,const juce::String& err)
                { if(!ok){ dmInput.setText(text,false); setStatus(err); } });
}

void DreamShareEditor::showSocialRequests()
{
    const auto snap=client.getSnapshot();
    if(!snap) return;
    juce::PopupMenu m;
    struct Entry { int id; bool wav; bool accept; ds::SocialRequest req; };
    std::vector<Entry> entries;
    int next=10;
    m.addSectionHeader("WAV requests");
    for(const auto& r:snap->wavRequests)
        if(r.status=="pending" && r.to.equalsIgnoreCase(client.getSession().user))
        {
            m.addItem(next,"Approve • "+r.from); entries.push_back({next,true,true,r}); ++next;
            m.addItem(next,"Decline • "+r.from); entries.push_back({next,true,false,r}); ++next;
        }
    if(entries.empty()) m.addItem(1,"No pending WAV requests",false);
    m.addSeparator(); m.addSectionHeader("Friend requests");
    for(const auto& r:snap->friendIncoming)
        if(r.status=="pending")
        {
            m.addItem(next,"Accept friend • "+r.from); entries.push_back({next,false,true,r}); ++next;
            m.addItem(next,"Decline friend • "+r.from); entries.push_back({next,false,false,r}); ++next;
        }
    if(next==10) m.addItem(2,"No pending friend requests",false);

    juce::Component::SafePointer<DreamShareEditor> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&dmRequests),
      [safe,entries](int result)
      {
        if(safe==nullptr) return;
        for(const auto& e:entries) if(e.id==result)
        {
            if(e.wav)
            {
                if(e.accept) safe->client.send(obj({{"action","wav_request_approve"},{"requestId",e.req.id}}),
                    [safe,id=e.req.id](bool ok,const juce::String& err)
                    {
                        if(safe==nullptr)return;
                        if(!ok){safe->setStatus(err);return;}
                        safe->chooseWavForRequest(id);
                    });
                else safe->client.send(obj({{"action","wav_request_decline"},{"requestId",e.req.id}}));
            }
            else safe->client.send(obj({{"action",e.accept?"friend_accept":"friend_decline"},{"requestId",e.req.id}}));
            return;
        }
      });
}

void DreamShareEditor::chooseWavForRequest (const juce::String& requestId)
{
    fileChooser = std::make_unique<juce::FileChooser> ("Choose the approved WAV export", juce::File(), "*.wav");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, requestId] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();
                                  fileChooser.reset();
                                  if (! file.existsAsFile()) return;
                                  setStatus ("Uploading WAV export...");
                                  juce::Component::SafePointer<DreamShareEditor> safe (this);
                                  client.uploadWav (file, [safe, requestId] (bool ok, const juce::String& err,
                                                                           const juce::String& store,
                                                                           const juce::String& upload, int parts,
                                                                           juce::int64 bytes)
                                  {
                                      if (safe == nullptr) return;
                                      if (! ok) { safe->setStatus (err); return; }
                                      safe->setStatus ("Sending WAV to requester...");
                                      safe->client.send (obj ({{"action","wav_request_fulfill"},{"requestId",requestId},
                                                               {"audioStore",store},{"audioUpload",upload},{"audioParts",parts},
                                                               {"audioBytes",(double)bytes},{"audioMime","audio/wav"}}),
                                                         [safe] (bool ok2, const juce::String& err2)
                                                         { if (safe) safe->setStatus (ok2 ? "WAV delivered in DM" : err2); });
                                  });
                              });
}

void DreamShareEditor::openThread (const juce::String& id)
{
    openId = id;
    tmode = TMode::detail;
    updateVisibility();
    rebuildDetail();
    detailFeed.setViewPosition (0, 0);
}

// ------------------------------------------------------------------ actions
void DreamShareEditor::doLogin()
{
    const auto u = userEd.getText().trim();
    const auto p = passEd.getText();
    if (u.length() < 3) { loginMsg.setText ("Username needs 3+ letters or numbers.", juce::dontSendNotification); return; }
    if (p.isEmpty())    { loginMsg.setText ("Password required.", juce::dontSendNotification); return; }

    loginBtn.setEnabled (false);
    loginMsg.setText ("Signing in...", juce::dontSendNotification);
    juce::Component::SafePointer<DreamShareEditor> safe (this);
    client.login (u, p, [safe] (bool ok, const juce::String& err)
    {
        if (safe == nullptr) return;
        safe->loginBtn.setEnabled (true);
        safe->loginMsg.setText (ok ? juce::String() : err, juce::dontSendNotification);
        if (ok) safe->passEd.clear();
        safe->refreshAll();
    });
}

void DreamShareEditor::sendChat()
{
    const auto t = chatInput.getText().trim();
    if (t.isEmpty()) return;
    chatInput.clear();
    juce::Component::SafePointer<DreamShareEditor> safe (this);
    client.send (obj ({ { "action", "chat_send" }, { "text", t } }),
                 [safe, t] (bool ok, const juce::String& err)
                 {
                     if (safe == nullptr) return;
                     if (! ok) { safe->chatInput.setText (t, false); safe->setStatus (err); }
                 });
}

void DreamShareEditor::postThread()
{
    const auto title = composeTitle.getText().trim();
    const auto text  = composeBody.getText().trim();
    if (title.isEmpty() && text.isEmpty() && !composeAudioFile.existsAsFile()) { setStatus ("Add a title, message or WAV"); return; }

    composePost.setEnabled(false);
    juce::Component::SafePointer<DreamShareEditor> safe(this);

    auto finishPost = [safe,title,text](const juce::String& store,const juce::String& upload,int parts,juce::int64 bytes)
    {
        if(safe==nullptr) return;
        auto body=obj({{"action","create_thread"},{"title",title},{"text",text}});
        if(parts>0)
        {
            if(auto* o=body.getDynamicObject())
            {
                o->setProperty("hasAudio",true); o->setProperty("audioStore",store); o->setProperty("audioUpload",upload);
                o->setProperty("audioParts",parts); o->setProperty("audioBytes",(double)bytes); o->setProperty("audioMime","audio/wav");
            }
        }
        safe->client.send(body,[safe](bool ok,const juce::String& err)
        {
            if(safe==nullptr) return;
            safe->composePost.setEnabled(true);
            if(!ok){safe->setStatus(err);return;}
            safe->composeTitle.clear(); safe->composeBody.clear(); safe->composeAudioFile = juce::File();
            safe->composeAudioLabel.setText("No WAV attached",juce::dontSendNotification);
            safe->tmode=TMode::list; safe->updateVisibility(); safe->setStatus("Thread posted");
        });
    };

    if(composeAudioFile.existsAsFile())
    {
        setStatus("Uploading WAV...");
        client.uploadWav(composeAudioFile,[safe,finishPost](bool ok,const juce::String& err,const juce::String& store,
                                                           const juce::String& upload,int parts,juce::int64 bytes)
        {
            if(safe==nullptr) return;
            if(!ok){safe->composePost.setEnabled(true);safe->setStatus(err);return;}
            finishPost(store,upload,parts,bytes);
        });
    }
    else finishPost({}, {}, 0, 0);
}

void DreamShareEditor::postComment()
{
    const auto t = commentInput.getText().trim();
    if (t.isEmpty() || openId.isEmpty()) return;
    commentInput.clear();
    juce::Component::SafePointer<DreamShareEditor> safe (this);
    client.send (obj ({ { "action", "comment" }, { "threadId", openId }, { "text", t } }),
                 [safe, t] (bool ok, const juce::String& err)
                 {
                     if (safe == nullptr) return;
                     if (! ok) { safe->commentInput.setText (t, false); safe->setStatus (err); }
                 });
}

void DreamShareEditor::act (const Item& it, bool)
{
    juce::Component::SafePointer<DreamShareEditor> safe (this);
    juce::var body;
    if (it.kind == "chat")         body = obj ({ { "action", "chat_delete" }, { "messageId", it.id } });
    else if (it.kind == "comment") body = obj ({ { "action", "delete_comment" }, { "threadId", it.threadId }, { "commentId", it.id } });
    else                           body = obj ({ { "action", "delete_thread" }, { "threadId", it.id } });

    const bool wasThread = it.kind == "thread";
    client.send (body, [safe, wasThread] (bool ok, const juce::String& err)
    {
        if (safe == nullptr) return;
        if (! ok) { safe->setStatus (err); return; }
        if (wasThread) { safe->tmode = TMode::list; safe->openId = {}; safe->updateVisibility(); }
        safe->setStatus ("Deleted");
    });
}

void DreamShareEditor::toggleReaction (const Item& it, const juce::String& key)
{
    if (! client.getSession().valid()) { setStatus ("Log in to react"); return; }
    bool had = false;
    auto f = it.reactions.find (key);
    if (f != it.reactions.end()) had = f->second.contains (it.me, true);

    juce::Component::SafePointer<DreamShareEditor> safe (this);
    client.send (obj ({ { "action", "react" }, { "kind", it.kind }, { "id", it.id },
                        { "emoji", key }, { "threadId", it.threadId }, { "active", ! had } }),
                 [safe] (bool ok, const juce::String& err)
                 {
                     if (safe != nullptr && ! ok) safe->setStatus (err);
                 });
}

void DreamShareEditor::pickReaction (const Item& it, juce::Rectangle<int> screenRect)
{
    juce::Component::SafePointer<DreamShareEditor> safe (this);
    auto grid = std::make_unique<EmojiGrid> (reactionSet(), 8,
        [safe, it] (const juce::String& key, const juce::String&)
        {
            if (safe != nullptr) safe->toggleReaction (it, key);
        });
    juce::CallOutBox::launchAsynchronously (std::move (grid), getLocalArea (nullptr, screenRect), this);
}

void DreamShareEditor::pickEmoji (juce::TextEditor& target, juce::Component& anchor)
{
    juce::Component::SafePointer<juce::TextEditor> safe (&target);
    auto grid = std::make_unique<EmojiGrid> (emojiPalette(), 8,
        [safe] (const juce::String&, const juce::String& glyph)
        {
            if (safe != nullptr) { safe->insertTextAtCaret (glyph); safe->grabKeyboardFocus(); }
        });
    juce::CallOutBox::launchAsynchronously (std::move (grid), getLocalArea (&anchor, anchor.getLocalBounds()), this);
}

void DreamShareEditor::showOnline()
{
    const auto snap = client.getSnapshot();
    juce::PopupMenu m;
    m.addSectionHeader ("Online");
    if (! snap || snap->online.isEmpty()) m.addItem (1, "Nobody connected", false);
    else for (int i = 0; i < snap->online.size(); ++i) m.addItem (100 + i, snap->online[i]);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (onlineBtn));
}

void DreamShareEditor::showUserMenu (const juce::String& name, juce::Point<int> pos)
{
    const auto snap = client.getSnapshot();
    const auto me = client.getSession();
    if (!snap || !me.valid() || name.equalsIgnoreCase(me.user)) return;

    const bool canMod = canModerate(*snap, me);
    const bool imSuper = me.role == "super" && snap->isSuper(me.user);
    const bool targetIsMod = snap->isMod(name), targetKyoto = snap->hasKyoto(name);
    const auto custom = snap->customRoles.find(name.toLowerCase());
    const bool hasCustomTag = custom != snap->customRoles.end();
    const bool isFriend = snap->friends.contains(name, true);

    juce::PopupMenu m;
    m.addSectionHeader(name);
    m.addItem(1, "Open private DM");
    m.addItem(2, isFriend ? "Remove friend" : "Add friend");
    m.addItem(3, "Request WAV export");
    m.addSeparator();

    if (canMod && !snap->isSuper(name))
    {
        if (imSuper) m.addItem(10, targetIsMod ? "Remove mod" : "Promote to mod");
        m.addItem(11, targetKyoto ? "Remove Kyoto role" : "Add Kyoto role");
        if (imSuper)
        {
            m.addItem(12, hasCustomTag ? "Change custom tag..." : "Set custom tag...");
            if (hasCustomTag) m.addItem(13, "Remove custom tag");
        }
    }

    juce::Component::SafePointer<DreamShareEditor> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({pos.x,pos.y,1,1}),
      [safe,name,isFriend,imSuper,targetIsMod,targetKyoto,hasCustomTag](int r)
      {
        if(safe==nullptr || r==0) return;
        auto& c=safe->client;
        if(r==1)
        {
            safe->tab=Tab::dms; safe->updateVisibility();
            safe->dmPeerBox.setText(name,juce::dontSendNotification);
            safe->selectDmPeer();
            return;
        }
        if(r==2) { c.send(obj({{"action",isFriend?"friend_remove":"friend_request"},{"target",name}})); return; }
        if(r==3)
        {
            c.send(obj({{"action","wav_request"},{"target",name},{"source",safe->tab==Tab::dms?"dm":"main"}}),
                    [safe](bool ok,const juce::String& e){ if(safe!=nullptr) safe->setStatus(ok?"WAV request sent":e); });
            return;
        }
        auto done=[safe](bool ok,const juce::String& e){ if(safe!=nullptr) safe->setStatus(ok?"Updated":e); };
        if(r==10 && imSuper) c.send(obj({{"action",targetIsMod?"demote_mod":"promote_mod"},{"target",name}}),done);
        if(r==11) c.send(obj({{"action",targetKyoto?"clear_role":"set_role"},{"target",name},{"role","kyoto"}}),done);
        if(r==12 && imSuper)
        {
            auto* alert=new juce::AlertWindow("Custom tag","Enter a badge label (2-24 characters).",juce::AlertWindow::NoIcon);
            alert->addTextEditor("tag",hasCustomTag?safe->client.getSnapshot()->customRoles.at(name.toLowerCase()).label:juce::String(),"Tag label");
            alert->addButton("Save",1,juce::KeyPress(juce::KeyPress::returnKey));
            alert->addButton("Cancel",0,juce::KeyPress(juce::KeyPress::escapeKey));
            juce::Component::SafePointer<juce::AlertWindow> a(alert);
            alert->enterModalState(true,juce::ModalCallbackFunction::create([safe,a,name](int result){
                if(safe==nullptr || a==nullptr || result!=1) return;
                const auto label=a->getTextEditorContents("tag").trim();
                if(label.length()<2 || label.length()>24){safe->setStatus("Custom tag must be 2-24 characters");return;}
                safe->client.send(obj({{"action","set_custom_role"},{"target",name},{"label",label}}));
            }),true);
        }
        if(r==13 && imSuper && hasCustomTag) c.send(obj({{"action","clear_custom_role"},{"target",name}}),done);
      });
}


void DreamShareEditor::loadDiscordChannels()
{
    if (tab != Tab::discord || ! client.getSession().valid()) return;
    setStatus ("Loading Discord channels...");
    juce::Component::SafePointer<DreamShareEditor> safe (this);
    client.sendJson (obj ({ { "action", "discord_channels" } }),
                     [safe] (bool ok, const juce::var& payload, const juce::String& error)
                     {
                         if (safe == nullptr) return;
                         safe->discordChannels.clear (juce::dontSendNotification);
                         if (! ok)
                         {
                             safe->setStatus (error.isEmpty() ? "Discord channels failed" : error);
                             return;
                         }
                         auto* arr = payload["channels"].getArray();
                         int id = 1;
                         if (arr != nullptr)
                             for (const auto& ch : *arr)
                             {
                                 const auto name = ch["name"].toString();
                                 safe->discordChannels.addItem (name.isEmpty() ? ch["id"].toString() : ("#" + name), id);
                                 safe->discordChannels.getProperties().set ("id" + juce::String (id), ch["id"].toString());
                                 ++id;
                             }
                         safe->setStatus (juce::String (id - 1) + " Discord channels");
                         if (safe->discordChannels.getNumItems() > 0)
                         {
                             safe->discordChannels.setSelectedId (1, juce::dontSendNotification);
                             safe->loadDiscordMessages();
                         }
                     });
}

void DreamShareEditor::loadDiscordMessages()
{
    if (tab != Tab::discord) return;
    const int selected = discordChannels.getSelectedId();
    discordChannelId = discordChannels.getProperties()["id" + juce::String (selected)].toString();
    if (discordChannelId.isEmpty()) return;
    juce::Component::SafePointer<DreamShareEditor> safe (this);
    client.sendJson (obj ({ { "action", "discord_messages" }, { "channel", discordChannelId } }),
                     [safe] (bool ok, const juce::var& payload, const juce::String& error)
                     {
                         if (safe == nullptr) return;
                         std::vector<ds::Item> v;
                         if (! ok)
                         {
                             safe->setStatus (error.isEmpty() ? "Could not read channel" : error);
                             safe->discordFeed.setItems ({}, true);
                             return;
                         }
                         auto* arr = payload["messages"].getArray();
                         if (arr != nullptr)
                             for (const auto& m : *arr)
                             {
                                 ds::Item it;
                                 it.kind = "chat";
                                 it.id = m["id"].toString();
                                 it.user = m["user"].toString();
                                 it.body = m["text"].toString().isEmpty() ? "(no text — Message Content Intent still off, or embed-only)" : m["text"].toString();
                                 it.me = safe->client.getSession().user;
                                 it.reactions["eyes"] = {};
                                 v.push_back (std::move (it));
                             }
                         safe->discordFeed.setItems (std::move (v), true);
                         safe->setStatus ("Discord channel loaded");
                     });
}

void DreamShareEditor::sendDiscord()
{
    const auto text = discordInput.getText().trim();
    if (text.isEmpty() || discordChannelId.isEmpty()) return;
    discordSend.setEnabled (false);
    juce::Component::SafePointer<DreamShareEditor> safe (this);
    client.sendJson (obj ({ { "action", "discord_send" }, { "channel", discordChannelId }, { "text", text } }),
                     [safe] (bool ok, const juce::var&, const juce::String& error)
                     {
                         if (safe == nullptr) return;
                         safe->discordSend.setEnabled (true);
                         if (! ok) { safe->setStatus (error.isEmpty() ? "Discord send failed" : error); return; }
                         safe->discordInput.clear();
                         safe->setStatus ("Posted as the bot");
                         safe->loadDiscordMessages();
                     });
}

void DreamShareEditor::reactDiscord (const ds::Item& it, const juce::String&)
{
    if (discordChannelId.isEmpty() || it.id.isEmpty()) return;
    juce::Component::SafePointer<DreamShareEditor> safe (this);
    client.sendJson (obj ({ { "action", "discord_react" }, { "channel", discordChannelId }, { "message", it.id }, { "emoji", "👀" } }),
                     [safe] (bool ok, const juce::var&, const juce::String& error)
                     {
                         if (safe == nullptr) return;
                         safe->setStatus (ok ? "Reaction added as the bot" : (error.isEmpty() ? "Reaction failed" : error));
                     });
}
