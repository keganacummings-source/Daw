# DreamShare Lite (VST3)

A small chat client for Dreamdaw's DreamShare, as a plugin. It has **no audio and no MIDI buses**;
it only talks to the same Cloudflare Worker API as `DREAMSHARELITE.html`
(`https://dreamshare-api.keganacummings.workers.dev/`, set in `Source/DreamClient.cpp`).

## What it does
- **Chat**: live room (last 100 messages), send, delete your own lines (mods can delete any).
- **Threads**: browse, post a thread (120-char title, 1000-char body), open a thread, reply (500 chars), delete.
- **Reactions**: the same 8 the server allows (thumbs up, heart, fire, laugh, skull, moon, eyes, 100) on chat lines, threads and comments. Click a chip to toggle, `+` to pick.
- **Emoji posting**: emoji button next to every text box inserts emoji into your message.
- **Roles**: ADMIN / MOD / KYOTO badges next to names. Mods/admins click a name for: promote/remove mod (admins only), add/remove Kyoto, delete their chat lines.
- **Online list**: click the `● N` button.
- **Login**: same accounts as the website. A new name creates an account (that is how the worker behaves).

## Lightweight by design
- No web view. Native JUCE drawing, one shared network thread for all plugin instances.
- Polls every 8 s (presence every 20 s) **only while a plugin window is open**. Closed window = zero traffic.
- The session token is saved to `DreamShareLite/session.json` in your user app-data folder, not in DAW projects.

## Build
Needs CMake 3.22+ and a C++17 compiler. JUCE 8.0.6 is downloaded automatically.

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

Output: `build/DreamShareLite_artefacts/Release/VST3/DreamShare Lite.vst3`

Or push this folder to GitHub: the included workflow builds Windows, macOS (universal) and Linux
and attaches the `.vst3` files as downloadable artifacts.

Install: Windows `C:\Program Files\Common Files\VST3` · macOS `~/Library/Audio/Plug-Ins/VST3` · Linux `~/.vst3`

## If your DAW won't load it
Some hosts (Ableton Live especially) only list plugins that have audio I/O. Configure with
`-DDS_AUDIO_PASSTHROUGH=ON` to add a stereo pass-through that leaves audio untouched.

## Notes
- JUCE is AGPLv3 / commercial. Distributing this plugin means publishing its source (or holding a JUCE licence).
- macOS: unsigned builds may need `xattr -dr com.apple.quarantine "DreamShare Lite.vst3"`.
- Emoji are drawn by the OS font: colour on Windows/macOS, depends on installed fonts on Linux.
- Some DAWs grab keyboard input before plugin text boxes get it. This affects every plugin with a text field, not just this one.
- Audio tapes attached to threads are shown as a "has audio" tag; playing/uploading WAVs is not part of Lite.
