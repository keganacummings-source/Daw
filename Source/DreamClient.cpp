#include "DreamClient.h"

namespace ds
{
static const char* kWorker = "https://dreamshare-api.keganacummings.workers.dev/";
static const int   kPullMs = 8000;    // same cadence as DREAMSHARELITE.html
static const int   kBeatMs = 20000;

juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> props)
{
    auto* o = new juce::DynamicObject();
    for (auto& p : props) o->setProperty (p.first, p.second);
    return juce::var (o);
}

bool Snapshot::hasKyoto (const juce::String& u) const
{
    auto it = roles.find (u.toLowerCase());
    return it != roles.end() && it->second.contains ("kyoto");
}

bool Snapshot::inChat (const juce::String& u) const
{
    for (auto& m : chat) if (m.user.equalsIgnoreCase (u)) return true;
    return false;
}

// ---------------------------------------------------------------- parsing
static Reactions parseReactions (const juce::var& v)
{
    Reactions r;
    if (auto* o = v.getDynamicObject())
        for (auto& nv : o->getProperties())
        {
            juce::StringArray users;
            if (auto* a = nv.value.getArray())
                for (auto& u : *a) users.add (u.toString());
            if (users.size() > 0) r[nv.name.toString()] = users;
        }
    return r;
}

static std::vector<ChatMsg> parseChat (const juce::var& v)
{
    std::vector<ChatMsg> out;
    if (auto* a = v.getArray())
        for (auto& m : *a)
        {
            ChatMsg c;
            c.id   = m["id"].toString();
            c.user = m["user"].toString();
            c.text = m["text"].toString();
            c.at   = (juce::int64) m["at"];
            c.reactions = parseReactions (m["reactions"]);
            if (c.id.isNotEmpty() && c.user.isNotEmpty()) out.push_back (std::move (c));
        }
    return out;
}



static std::vector<SocialRequest> parseRequests (const juce::var& v)
{
    std::vector<SocialRequest> out;
    if (auto* a=v.getArray()) for(auto& r:*a)
    {
        SocialRequest x;
        x.id=r["id"].toString(); x.from=r["from"].toString(); x.to=r["to"].toString();
        x.status=r["status"].toString(); x.source=r["source"].toString(); x.note=r["note"].toString();
        x.at=(juce::int64)r["at"];
        if(x.id.isNotEmpty()) out.push_back(std::move(x));
    }
    return out;
}

static std::vector<DirectMsg> parseDms (const juce::var& v)
{
    std::vector<DirectMsg> out;
    if (auto* a = v.getArray())
        for (auto& m : *a)
        {
            DirectMsg d;
            d.id=m["id"].toString(); d.from=m["from"].toString(); d.to=m["to"].toString();
            d.text=m["text"].toString(); d.requestId=m["requestId"].toString();
            d.audioStore=m["audioStore"].toString(); d.audioUpload=m["audioUpload"].toString();
            d.audioMime=m["audioMime"].toString(); d.at=(juce::int64)m["at"];
            d.audioParts=(int)m["audioParts"]; d.audioBytes=(juce::int64)m["audioBytes"];
            if (d.id.isNotEmpty()) out.push_back(std::move(d));
        }
    return out;
}

static juce::StringArray parseStrings (const juce::var& v)
{
    juce::StringArray s;
    if (auto* a = v.getArray()) for (auto& x : *a) s.add (x.toString());
    return s;
}

// ---------------------------------------------------------------- lifecycle
Client::Client() : juce::Thread ("DreamShare network")
{
    loadSession();
    startThread (juce::Thread::Priority::low);
}

Client::~Client()
{
    signalThreadShouldExit();
    notify();
    stopThread (15000);
}

void Client::addListener (Listener* l)    { listeners.add (l);    ++listenerCount; forcePull = true; notify(); }
void Client::removeListener (Listener* l) { listeners.remove (l); --listenerCount; }

Session Client::getSession() const                      { const juce::ScopedLock sl (lock); return session; }
std::shared_ptr<const Snapshot> Client::getSnapshot() const { const juce::ScopedLock sl (lock); return snapshot; }

// ---------------------------------------------------------------- session file (shared by all instances)
juce::File Client::sessionFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("DreamShareLite").getChildFile ("session.json");
}

void Client::loadSession()
{
    auto v = juce::JSON::parse (sessionFile());
    Session s { v["user"].toString(), v["role"].toString(), v["token"].toString(), v["theme"].toString() };
    if (s.valid()) session = s;
}

void Client::saveSession (const Session& s) const
{
    auto f = sessionFile();
    if (! s.valid()) { f.deleteFile(); return; }
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::JSON::toString (obj ({ { "user", s.user }, { "role", s.role },
                                                     { "token", s.token }, { "theme", s.theme } })));
}

void Client::setSession (const Session& s)
{
    { const juce::ScopedLock sl (lock); session = s; }
    saveSession (s);
}

// ---------------------------------------------------------------- notifications (always on message thread)
void Client::notifyChanged()
{
    juce::MessageManager::callAsync ([w = juce::WeakReference<Client> (this)]
    {
        if (w != nullptr) w->listeners.call ([] (Listener& l) { l.clientChanged(); });
    });
}

void Client::notifyStatus (const juce::String& s)
{
    juce::MessageManager::callAsync ([w = juce::WeakReference<Client> (this), s]
    {
        if (w != nullptr) w->listeners.call ([&s] (Listener& l) { l.clientStatus (s); });
    });
}

// ---------------------------------------------------------------- tasks
void Client::enqueue (std::function<void()> fn)
{
    { const juce::ScopedLock sl (taskLock); tasks.push_back (std::move (fn)); }
    notify();
}

void Client::runTasks()
{
    for (;;)
    {
        std::function<void()> fn;
        {
            const juce::ScopedLock sl (taskLock);
            if (tasks.empty()) return;
            fn = std::move (tasks.front());
            tasks.pop_front();
        }
        fn();
        if (threadShouldExit()) return;
    }
}

void Client::run()
{
    while (! threadShouldExit())
    {
        runTasks();

        if (listenerCount.load() > 0 && getSession().valid())
        {
            const auto now = juce::Time::getMillisecondCounter();
            if (forcePull.exchange (false) || now - lastPull >= (juce::uint32) kPullMs)
            {
                pull();
                lastPull = juce::Time::getMillisecondCounter();
            }
            if (now - lastBeat >= (juce::uint32) kBeatMs)
            {
                beat();
                lastBeat = juce::Time::getMillisecondCounter();
            }
        }
        wait (400);
    }
}

// ---------------------------------------------------------------- HTTP
bool Client::http (bool post, const juce::String& postBody, juce::var& out, juce::String& err)
{
    int status = 0;
    std::unique_ptr<juce::InputStream> in;

    if (post)
    {
        in = juce::URL (kWorker).withPOSTData (postBody).createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                .withConnectionTimeoutMs (12000)
                .withStatusCode (&status)
                .withHttpRequestCmd ("POST")
                .withExtraHeaders ("Content-Type: application/json\r\nAccept: application/json\r\n"));
    }
    else
    {
        in = juce::URL (kWorker).withParameter ("t", juce::String (juce::Time::currentTimeMillis()))
                 .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                         .withConnectionTimeoutMs (12000)
                                         .withStatusCode (&status)
                                         .withExtraHeaders ("Accept: application/json\r\n"));
    }

    if (in == nullptr) { err = "Cannot reach DreamShare. Check your connection."; return false; }

    out = juce::JSON::parse (in->readEntireStreamAsString());
    if (! out.isObject()) { err = "Unreadable server response (HTTP " + juce::String (status) + ")"; return false; }

    const bool okFlag = ! out.hasProperty ("ok") || (bool) out["ok"];
    if (! okFlag || status >= 400)
    {
        err = out["error"].toString();
        if (err.isEmpty()) err = "HTTP " + juce::String (status);
        if (out["code"].toString() == "auth")
        {
            setSession ({});               // token expired -> back to login screen
            notifyChanged();
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- feed
void Client::pull()
{
    juce::var j; juce::String err;
    if (! http (false, {}, j, err)) { notifyStatus (err); return; }

    auto me = getSession();
    if (me.valid() && me.theme.isEmpty())
    {
        juce::var sessionInfo;
        const auto request = obj ({ { "action", "session" }, { "user", me.user }, { "token", me.token } });
        if (http (true, juce::JSON::toString (request, true), sessionInfo, err))
        {
            const auto themeId = sessionInfo["theme"].toString().toLowerCase();
            if (themeId.isNotEmpty())
            {
                me.theme = themeId;
                setSession (me);
            }
        }
    }

    auto s = std::make_shared<Snapshot>();

    if (auto* arr = j["threads"].getArray())
        for (auto& tv : *arr)
        {
            Topic t;
            t.id = tv["id"].toString();  t.user = tv["user"].toString();
            t.title = tv["title"].toString();  t.text = tv["text"].toString();
            t.at = (juce::int64) tv["at"];  t.hasAudio = (bool) tv["hasAudio"];
            t.reactions = parseReactions (tv["reactions"]);
            if (auto* ca = tv["comments"].getArray())
                for (auto& cv : *ca)
                {
                    Comment c;
                    c.id = cv["id"].toString();  c.user = cv["user"].toString();
                    c.text = cv["text"].toString();  c.at = (juce::int64) cv["at"];
                    c.reactions = parseReactions (cv["reactions"]);
                    t.comments.push_back (std::move (c));
                }
            if (t.id.isNotEmpty()) s->threads.push_back (std::move (t));
        }

    s->chat    = parseChat (j["chat"]);
    s->mods    = parseStrings (j["mods"]);
    s->supers  = parseStrings (j["supers"]);
    s->directory = parseStrings (j["directory"]);
    if (s->supers.isEmpty()) { s->supers.add ("Trippah"); s->supers.add ("Goonr"); }

    if (auto* themes = j["themes"].getArray())
        for (auto& value : *themes)
        {
            ThemeChoice choice { value["id"].toString().toLowerCase(), value["name"].toString() };
            if (choice.id.isNotEmpty() && choice.name.isNotEmpty()) s->themes.push_back (std::move (choice));
        }

    if (auto* ro = j["roles"].getDynamicObject())
        for (auto& nv : ro->getProperties())
            s->roles[nv.name.toString().toLowerCase()] = parseStrings (nv.value);

    if (auto* custom = j["customRoles"].getDynamicObject())
        for (auto& nv : custom->getProperties())
        {
            CustomRole role { nv.value["label"].toString(), nv.value["color"].toString(),
                              nv.value["bg"].toString() };
            if (role.label.isNotEmpty()) s->customRoles[nv.name.toString().toLowerCase()] = std::move (role);
        }

    // Online list: server presence + me + anyone who chatted in the last 3 minutes (same as the web viewer)
    juce::StringArray online = parseStrings (j["online"]);
    const auto currentSession = getSession();
    if (currentSession.valid()) online.addIfNotAlreadyThere (currentSession.user, true);
    const auto now = juce::Time::currentTimeMillis();
    for (auto& m : s->chat)
        if (now - m.at < 180000) online.addIfNotAlreadyThere (m.user, true);
    s->online = online;

    // Private social metadata is deliberately fetched through an authenticated POST;
    // it is never exposed by the public feed GET.
    if (currentSession.valid())
    {
        juce::var social; juce::String socialErr;
        auto req = obj({{"action","social_list"}, {"user", currentSession.user}, {"token", currentSession.token}});
        if (http(true,juce::JSON::toString(req,true),social,socialErr))
        {
            s->friends = parseStrings(social["friends"]);
            s->directory = parseStrings(social["directory"]);
            s->friendIncoming = parseRequests(social["incoming"]);
            s->wavRequests = parseRequests(social["wavRequests"]);
        }
    }

    { const juce::ScopedLock sl (lock); snapshot = s; }
    notifyChanged();
    notifyStatus ("Live " + juce::String::charToString (0xB7) + " online " + juce::String (online.size()));
}

void Client::beat()
{
    juce::var j; juce::String err;
    const auto me = getSession();
    if (! me.valid()) return;
    auto body = obj ({ { "action", "presence" }, { "user", me.user }, { "token", me.token } });
    http (true, juce::JSON::toString (body, true), j, err);   // best effort
}

void Client::applyChat (const juce::var& chatArray)
{
    const juce::ScopedLock sl (lock);
    auto s = snapshot ? std::make_shared<Snapshot> (*snapshot) : std::make_shared<Snapshot>();
    s->chat = parseChat (chatArray);
    snapshot = s;
}

void Client::refreshNow() { forcePull = true; notify(); }

// ---------------------------------------------------------------- public actions
void Client::login (const juce::String& user, const juce::String& pass, Done done)
{
    enqueue ([this, user, pass, done]
    {
        juce::var j; juce::String err;
        auto body = obj ({ { "action", "login" }, { "user", user }, { "pass", pass } });
        const bool ok = http (true, juce::JSON::toString (body, true), j, err) && j["token"].toString().isNotEmpty();
        if (ok)
        {
            Session next { j["user"].toString().isNotEmpty() ? j["user"].toString() : user,
                           j["role"].toString(), j["token"].toString(), j["theme"].toString().toLowerCase() };
            setSession (next);
            forcePull = true;
            lastBeat = 0;
        }
        else if (err.isEmpty()) err = "Login did not return a session token.";

        juce::MessageManager::callAsync ([done, ok, err] { if (done) done (ok, err); });
        notifyChanged();
    });
}

void Client::logout()
{
    setSession ({});
    notifyChanged();
}


void Client::uploadWav (const juce::File& file, UploadDone done)
{
    enqueue ([this, file, done]
    {
        const auto me = getSession();
        if (!me.valid()) { juce::MessageManager::callAsync ([done] { if(done) done(false,"Login required",{}, {},0,0); }); return; }
        if (!file.existsAsFile()) { juce::MessageManager::callAsync ([done] { if(done) done(false,"WAV file not found",{}, {},0,0); }); return; }
        const auto bytesTotal = file.getSize();
        constexpr juce::int64 maxBytes = 75LL * 1024LL * 1024LL;
        if (bytesTotal <= 0 || bytesTotal > maxBytes) {
            juce::MessageManager::callAsync ([done,bytesTotal,maxBytes] { if(done) done(false, bytesTotal>maxBytes?"WAV is over 75 MB":"Empty WAV",{}, {},0,bytesTotal); });
            return;
        }
        constexpr int chunkBytes = 2000000;
        const int parts = (int)((bytesTotal + chunkBytes - 1) / chunkBytes);
        if (parts > 50) { juce::MessageManager::callAsync ([done] { if(done) done(false,"WAV requires too many upload parts",{}, {},0,0); }); return; }
        const auto upload = "u" + juce::String(juce::Time::currentTimeMillis()) + juce::String(juce::Random::getSystemRandom().nextInt(999));
        juce::FileInputStream in(file);
        if (!in.openedOk()) { juce::MessageManager::callAsync ([done] { if(done) done(false,"Could not open WAV",{}, {},0,0); }); return; }

        juce::MemoryBlock block;
        juce::String sink;
        for (int i=0; i<parts; ++i)
        {
            const int want=(int)juce::jmin<juce::int64>(chunkBytes, bytesTotal-(juce::int64)i*chunkBytes);
            block.setSize((size_t)want,false);
            if (in.read(block.getData(),want)!=want) {
                juce::MessageManager::callAsync ([done] { if(done) done(false,"Could not read WAV",{}, {},0,0); }); return;
            }
            const auto b64=juce::Base64::toBase64(block.getData(),block.getSize());
            auto body=obj({{"action","audio_part_b64"},{"user",me.user},{"token",me.token},{"upload",upload},{"index",i},{"parts",parts},{"b64",b64}});
            juce::var j; juce::String err;
            if (!http(true,juce::JSON::toString(body,true),j,err) || !(bool)j["ok"]) {
                if(err.isEmpty()) err=j["error"].toString();
                juce::MessageManager::callAsync ([done,err] { if(done) done(false,err.isEmpty()?"WAV upload failed":err,{}, {},0,0); });
                return;
            }
            if (sink.isEmpty()) sink = j["sink"].toString();
        }
        // The server returns the same storage sink for every part; R2 is preferred.
        if (sink.isEmpty()) sink = "r2";
        juce::MessageManager::callAsync ([done,sink,upload,parts,bytesTotal] {
            if(done) done(true,{},sink,upload,parts,bytesTotal);
        });
    });
}


void Client::downloadDmWav (const juce::String& messageId, int parts, const juce::File& destination, Done done)
{
    enqueue ([this,messageId,parts,destination,done]
    {
        const auto me=getSession();
        if(!me.valid()){juce::MessageManager::callAsync([done]{if(done)done(false,"Login required");});return;}
        if(parts<1 || parts>50){juce::MessageManager::callAsync([done]{if(done)done(false,"Invalid WAV parts");});return;}
        auto out=destination.createOutputStream();
        if(!out){juce::MessageManager::callAsync([done]{if(done)done(false,"Could not create output file");});return;}
        bool ok=true; juce::String err;
        for(int i=0;i<parts && ok;++i)
        {
            int status=0;
            auto url=juce::URL(kWorker).withParameter("dmwav",messageId)
                     .withParameter("token",me.token).withParameter("part",juce::String(i));
            auto in=url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs(15000).withStatusCode(&status)
                       .withExtraHeaders("Accept: audio/wav\r\n"));
            if(!in || status>=400){ok=false;err="Could not download WAV part "+juce::String(i+1);break;}
            juce::MemoryBlock mb;
            if(!in->readIntoMemoryBlock(mb) || mb.getSize()==0){ok=false;err="Empty WAV part";break;}
            out->write(mb.getData(),mb.getSize());
        }
        out->flush();
        out.reset();
        if(!ok) destination.deleteFile();
        juce::MessageManager::callAsync([done,ok,err]{if(done)done(ok,err);});
    });
}

void Client::send (juce::var body, Done done)
{
    enqueue ([this, body, done]() mutable
    {
        const auto me = getSession();
        if (! me.valid())
        {
            juce::MessageManager::callAsync ([done] { if (done) done (false, "Login required"); });
            return;
        }
        if (auto* o = body.getDynamicObject())
        {
            o->setProperty ("user", me.user);
            o->setProperty ("token", me.token);
        }
        juce::var j; juce::String err;
        const bool ok = http (true, juce::JSON::toString (body, true), j, err);
        if (ok)
        {
            if (body["action"].toString() == "set_theme" || body["action"].toString() == "theme")
            {
                auto updated = getSession();
                updated.theme = j["theme"].toString().toLowerCase();
                if (updated.theme.isNotEmpty())
                {
                    setSession (updated);
                    notifyChanged();
                }
            }
            if (j["chat"].isArray()) { applyChat (j["chat"]); notifyChanged(); }   // instant chat update
            if (body["action"].toString() == "dm_list" && j["messages"].isArray())
            {
                const juce::ScopedLock sl(lock);
                auto updated = snapshot ? std::make_shared<Snapshot>(*snapshot) : std::make_shared<Snapshot>();
                updated->dms = parseDms(j["messages"]);
                snapshot = updated;
                notifyChanged();
            }
            if (body["action"].toString() == "social_list")
            {
                const juce::ScopedLock sl(lock);
                auto updated = snapshot ? std::make_shared<Snapshot>(*snapshot) : std::make_shared<Snapshot>();
                updated->friends = parseStrings(j["friends"]);
                updated->directory = parseStrings(j["directory"]);
                updated->friendIncoming = parseRequests(j["incoming"]);
                updated->wavRequests = parseRequests(j["wavRequests"]);
                snapshot = updated;
                notifyChanged();
            }
            forcePull = true;
        }
        juce::MessageManager::callAsync ([done, ok, err] { if (done) done (ok, err); });
    });
}
}
