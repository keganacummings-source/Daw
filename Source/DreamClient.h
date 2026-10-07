#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace ds
{
    // emoji key ("up","heart","fire","laugh","skull","moon","eyes","100") -> users who reacted
    using Reactions = std::map<juce::String, juce::StringArray>;

    struct Comment  { juce::String id, user, text; juce::int64 at = 0; Reactions reactions; };
    struct Topic    { juce::String id, user, title, text; juce::int64 at = 0; bool hasAudio = false;
                      std::vector<Comment> comments; Reactions reactions; };
    struct ChatMsg  { juce::String id, user, text; juce::int64 at = 0; Reactions reactions; };
    struct DirectMsg { juce::String id, from, to, text, requestId, audioStore, audioUpload, audioMime; juce::int64 at = 0; int audioParts = 0; juce::int64 audioBytes = 0; };
    struct SocialRequest { juce::String id, from, to, status, source, note; juce::int64 at = 0; };

    struct Session
    {
        juce::String user, role, token, theme;
        bool valid() const { return user.isNotEmpty() && token.isNotEmpty(); }
    };

    struct ThemeChoice { juce::String id, name; };
    struct CustomRole { juce::String label, color, bg; };

    // Immutable copy of the last feed pull. Shared with the UI via shared_ptr.
    struct Snapshot
    {
        std::vector<Topic>   threads;
        std::vector<ChatMsg> chat;
        std::vector<ThemeChoice> themes;
        juce::StringArray    mods, supers, online, directory, friends;
        std::vector<DirectMsg> dms;
        std::vector<SocialRequest> friendIncoming, wavRequests;
        std::map<juce::String, juce::StringArray> roles;   // key = lower-case username
        std::map<juce::String, CustomRole> customRoles;    // key = lower-case username

        bool isSuper  (const juce::String& u) const { return supers.contains (u, true); }
        bool isMod    (const juce::String& u) const { return isSuper (u) || mods.contains (u, true); }
        bool hasKyoto (const juce::String& u) const;
        bool inChat   (const juce::String& u) const;
    };

    // Build a JSON object var:  obj({{"action","chat_send"},{"text","hi"}})
    juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> props);

    /** Which UI channel is currently visible. Polling only refreshes this channel. */
    enum class ActiveChannel { feed, dms, discord };

    /** One shared client per process (held via SharedResourcePointer).
        Owns one background thread; it only polls while at least one editor is open. */
    class Client : private juce::Thread
    {
    public:
        Client();
        ~Client() override;

        struct Listener
        {
            virtual ~Listener() = default;
            virtual void clientChanged() {}
            virtual void clientStatus (const juce::String&) {}
        };
        void addListener (Listener*);       // message thread only
        void removeListener (Listener*);    // message thread only

        Session getSession() const;
        std::shared_ptr<const Snapshot> getSnapshot() const;

        /** Tell the network thread which tab is open so it only polls that channel. */
        void setActiveChannel (ActiveChannel channel);
        ActiveChannel getActiveChannel() const { return activeChannel.load(); }

        using Done = std::function<void (bool ok, const juce::String& error)>;
        using UploadDone = std::function<void (bool ok, const juce::String& error, const juce::String& store,
                                               const juce::String& upload, int parts, juce::int64 bytes)>;

        void login (const juce::String& user, const juce::String& pass, Done done);
        void logout();
        void refreshNow();
        void uploadWav (const juce::File&, UploadDone);
        void downloadDmWav (const juce::String& messageId, int parts, const juce::File& destination, Done);
        /** Sends an authenticated action (user + token are added automatically). */
        void send (juce::var body, Done done = {});
        using JsonDone = std::function<void (bool ok, const juce::var& payload, const juce::String& error)>;
        void sendJson (juce::var body, JsonDone done);

    private:
        void run() override;
        void runTasks();
        void enqueue (std::function<void()> fn);
        bool http (bool post, const juce::String& postBody, juce::var& out, juce::String& err);
        void pull();
        void beat();
        void applyChat (const juce::var& chatArray);
        void loadSession();
        void saveSession (const Session&) const;
        void setSession (const Session&);
        void notifyChanged();
        void notifyStatus (const juce::String&);
        juce::File sessionFile() const;
        int pullIntervalMs() const;

        mutable juce::CriticalSection lock;
        Session session;
        std::shared_ptr<const Snapshot> snapshot;

        juce::CriticalSection taskLock;
        std::deque<std::function<void()>> tasks;

        juce::ListenerList<Listener> listeners;   // message thread only
        std::atomic<int> listenerCount { 0 };
        std::atomic<bool> forcePull { false };
        std::atomic<ActiveChannel> activeChannel { ActiveChannel::feed };
        juce::uint32 lastPull = 0, lastBeat = 0, lastSocial = 0;

        JUCE_DECLARE_WEAK_REFERENCEABLE (Client)
    };
}
