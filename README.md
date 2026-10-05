# DreamShare Lite 0.4.0

A native JUCE VST3 chat/social client for DreamShare.

## UI / performance overhaul
- Cleaner DREAMDAW-style header and compact tab navigation.
- Responsive sizing with a larger usable resize range.
- Removed animated background rendering from the continuous repaint path.
- Feed layout is cached between unchanged resize dimensions to reduce DAW-window stutter.
- Direct-painted cards remain lightweight instead of creating a child component per message.
- Scrolling keeps a thin scrollbar and drag scrolling enabled.

## Social
- Main Chat, Threads and private DMs.
- Click any username to open:
  - private DM
  - Add/Remove Friend
  - Request WAV export
  - existing moderator/admin actions
- Friend requests can be accepted/declined from WAV Requests / social controls.
- Private messages are stored outside the public feed API and require an authenticated session.

## WAV sharing
- Thread composer has an Attach WAV control.
- WAVs up to 75 MB are uploaded in background chunks and stored in the configured R2/KV audio vault.
- Approved WAV requests can be fulfilled by selecting the rendered WAV export.
- WAV attachments received in DMs can be saved locally from the DM view.

### Important DAW limitation
A generic VST3 cannot command every host DAW to perform a full-project bounce/export. The request workflow therefore stops at approval and then lets the exporting user choose the WAV produced by their DAW. This avoids pretending the plugin can access a host's full project mix when the host does not expose that API.

## API
The bundled `src/worker.js` adds:
- `social_list`
- `friend_request`, `friend_accept`, `friend_decline`, `friend_remove`
- `dm_list`, `dm_send`
- `wav_request`, `wav_request_approve`, `wav_request_decline`, `wav_request_fulfill`
- `audio_part_b64`
- authenticated private `?dmwav=<message>&token=<session>&part=<n>` retrieval

The existing public thread/chat/reaction/theme API remains compatible.

## Build
The GitHub workflow still performs the worker syntax check and builds Windows/macOS VST3 packages with JUCE 8.0.6.
