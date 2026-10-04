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
    setResizeLimits (300, 340, 1000, 1200);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &whoLabel, &statusLabel, &onlineBtn, &logoutBtn, &chatTab, &threadsTab, &themeBox,
             &loginInfo, &loginMsg, &userEd, &passEd, &loginBtn,
             &chatFeed, &chatInput, &chatEmoji, &chatSend,
             &threadFeed, &detailFeed, &newThreadBtn, &backBtn,
             &composeTitle, &composeBody, &commentInput, &composeCount,
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

    for (auto* b : { &chatTab, &threadsTab })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (7);
    }
    chatTab.setToggleState (true, juce::dontSendNotification);
    chatTab.onClick    = [this] { tab = Tab::chat;    updateVisibility(); };
    threadsTab.onClick = [this] { tab = Tab::threads; updateVisibility(); };

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

    setSize (proc.editorW, proc.editorH);
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
    if (appliedTheme == "goonr")
    {
        g.setFont (T::font (11.0f, true));
        for (int x = 14; x < getWidth(); x += 31)
            for (int y = -20; y < getHeight(); y += 54)
            {
                const int offset = (int) ((animationTick * 3 + (juce::uint32) (x * 7)) % 54);
                g.setColour (T::accent.withAlpha (((x + y + (int) animationTick) % 4 == 0) ? 0.32f : 0.12f));
                g.drawText (((x / 31 + y / 54 + (int) animationTick / 3) % 2) ? "1" : "0",
                            x, y + offset, 12, 15, juce::Justification::centred, false);
            }
    }
    else if (appliedTheme == "trippah")
    {
        for (int i = 0; i < 14; ++i)
        {
            const int x = (i * 97 + (int) (animationTick * (i % 3 + 1) * 2)) % juce::jmax (1, getWidth());
            const int y = (i * 71 + (int) (animationTick * (i % 2 + 1))) % juce::jmax (1, getHeight());
            const float scale = 0.65f + (float) (i % 4) * 0.12f;
            g.setColour (T::pink.withAlpha (0.12f));
            g.drawLine ((float) x, (float) y, (float) x, (float) y + 10.0f * scale, 2.0f * scale);
            g.setColour (T::accent.withAlpha (0.14f));
            g.fillEllipse ((float) x - 7.0f * scale, (float) y - 3.0f * scale,
                           14.0f * scale, 8.0f * scale);
            g.setColour (T::pink.withAlpha (0.18f));
            g.fillRoundedRectangle ((float) ((x + 43) % juce::jmax (1, getWidth())),
                                    (float) ((y + 29) % juce::jmax (1, getHeight())),
                                    15.0f * scale, 6.0f * scale, 3.0f * scale);
        }
    }
    g.setColour (T::header);  g.fillRect (0, 0, getWidth(), 34);
    g.setColour (T::border);  g.drawHorizontalLine (33, 0.0f, (float) getWidth());
    g.setColour (T::border);  g.drawHorizontalLine (getHeight() - 21, 0.0f, (float) getWidth());
    g.setColour (T::pink);
    g.setFont (T::font (12.5f, true));
    g.drawText ("DREAMSHARE", 10, 0, 82, 34, juce::Justification::centredLeft, false);
    g.setColour (T::dim);
    g.setFont (T::font (10.0f, true));
    g.drawText ("LITE", 90, 2, 40, 34, juce::Justification::centredLeft, false);
}

void DreamShareEditor::resized()
{
    proc.editorW = getWidth();
    proc.editorH = getHeight();

    auto r = getLocalBounds();
    auto top = r.removeFromTop (34).reduced (8, 5);
    logoutBtn.setBounds (top.removeFromRight (58));  top.removeFromRight (6);
    onlineBtn.setBounds (top.removeFromRight (52));  top.removeFromRight (6);
    whoLabel.setBounds  (top.removeFromRight (90));
    statusLabel.setBounds (r.removeFromBottom (21).reduced (8, 0));
    auto tabs = r.removeFromTop (30).reduced (8, 2);
    auto themeArea = tabs.removeFromRight (juce::jmin (120, juce::jmax (100, tabs.getWidth() - 150)));
    themeBox.setBounds (themeArea);
    tabs.removeFromRight (6);
    chatTab.setBounds (tabs.removeFromLeft (66));  tabs.removeFromLeft (4);
    threadsTab.setBounds (tabs.removeFromLeft (74));
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
        composeCancel.setBounds (btns.removeFromRight (70));
        composeEmoji.setBounds (btns.removeFromLeft (34));
        composeCount.setBounds (btns.withTrimmedLeft (6));
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
void DreamShareEditor::timerCallback()                       { ++animationTick; repaint(); }

bool DreamShareEditor::canModerate (const Snapshot& s, const Session& me) const
{
    return me.valid() && (me.role == "super" || s.isMod (me.user));
}

void DreamShareEditor::refreshAll()
{
    updateHeader();
    updateVisibility();
    rebuildChat();
    rebuildThreadList();
    rebuildDetail();
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
    if (themeId == "goonr" || themeId == "trippah") startTimerHz (8);
    else stopTimer();
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
    show ({ &whoLabel, &onlineBtn, &logoutBtn, &chatTab, &threadsTab, &themeBox }, in);
    show ({ &chatFeed, &chatInput, &chatEmoji, &chatSend }, c);
    show ({ &newThreadBtn, &threadFeed }, t && tmode == TMode::list);
    show ({ &composeTitle, &composeBody, &composeCount, &composeEmoji, &composePost, &composeCancel },
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
    if (title.isEmpty()) { setStatus ("Set a thread title"); return; }
    if (text.isEmpty())  { setStatus ("Write the thread text"); return; }

    composePost.setEnabled (false);
    juce::Component::SafePointer<DreamShareEditor> safe (this);
    client.send (obj ({ { "action", "create_thread" }, { "title", title }, { "text", text } }),
                 [safe] (bool ok, const juce::String& err)
                 {
                     if (safe == nullptr) return;
                     safe->composePost.setEnabled (true);
                     if (! ok) { safe->setStatus (err); return; }
                     safe->composeTitle.clear();  safe->composeBody.clear();
                     safe->tmode = TMode::list;
                     safe->updateVisibility();
                     safe->setStatus ("Thread posted");
                 });
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
    if (! snap || ! canModerate (*snap, me) || snap->isSuper (name)) return;

    const bool imSuper = me.role == "super" && snap->isSuper (me.user);
    const bool targetIsMod = snap->isMod (name), targetKyoto = snap->hasKyoto (name);
    const auto custom = snap->customRoles.find (name.toLowerCase());
    const bool hasCustomTag = custom != snap->customRoles.end();
    const auto customTag = hasCustomTag ? custom->second.label : juce::String();

    juce::PopupMenu m;
    m.addSectionHeader (name);
    if (imSuper) m.addItem (1, targetIsMod ? "Remove mod" : "Promote to mod");
    m.addItem (2, targetKyoto ? "Remove Kyoto role" : "Add Kyoto role");
    m.addItem (3, "Delete their chat lines");
    if (imSuper)
    {
        m.addSeparator();
        m.addItem (4, hasCustomTag ? "Change custom tag..." : "Set custom tag...");
        if (hasCustomTag) m.addItem (5, "Remove custom tag");
    }

    juce::StringArray chatIds;
    for (auto& c : snap->chat) if (c.user.equalsIgnoreCase (name)) chatIds.add (c.id);

    juce::Component::SafePointer<DreamShareEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (pos.x, pos.y, 1, 1)),
        [safe, name, targetIsMod, targetKyoto, chatIds, imSuper, hasCustomTag, customTag] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto done = [safe] (bool ok, const juce::String& err)
            {
                if (safe != nullptr) safe->setStatus (ok ? juce::String ("Updated") : err);
            };
            auto& c = safe->client;
            if (r == 1) c.send (obj ({ { "action", targetIsMod ? "demote_mod" : "promote_mod" }, { "target", name } }), done);
            if (r == 2) c.send (obj ({ { "action", targetKyoto ? "clear_role" : "set_role" }, { "target", name }, { "role", "kyoto" } }), done);
            if (r == 3) for (auto& id : chatIds) c.send (obj ({ { "action", "chat_delete" }, { "messageId", id } }), done);
            if (r == 4 && imSuper)
            {
                auto* alert = new juce::AlertWindow ("Custom tag", "Enter a badge label (2-24 characters).",
                                                      juce::AlertWindow::NoIcon);
                alert->addTextEditor ("tag", customTag, "Tag label");
                alert->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
                alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                juce::Component::SafePointer<juce::AlertWindow> alertSafe (alert);
                alert->enterModalState (true, juce::ModalCallbackFunction::create (
                    [safe, alertSafe, name] (int result)
                    {
                        if (safe == nullptr || alertSafe == nullptr || result != 1) return;
                        const auto label = alertSafe->getTextEditorContents ("tag").trim();
                        if (label.length() < 2 || label.length() > 24)
                        {
                            safe->setStatus ("Custom tag must be 2-24 characters");
                            return;
                        }
                        safe->client.send (obj ({ { "action", "set_custom_role" },
                                                  { "target", name }, { "label", label } }),
                                           [safe] (bool ok, const juce::String& err)
                                           {
                                               if (safe != nullptr) safe->setStatus (ok ? "Custom tag saved" : err);
                                           });
                    }), true);
            }
            if (r == 5 && imSuper && hasCustomTag)
                c.send (obj ({ { "action", "clear_custom_role" }, { "target", name } }),
                        [safe] (bool ok, const juce::String& err)
                        {
                            if (safe != nullptr) safe->setStatus (ok ? "Custom tag removed" : err);
                        });
        });
}
