// Canonical DreamShare Worker entry point. Deploy this file as worker.js.
import { handleKyotoModule } from "./module-rules.js";

/**
 * DREAMSHARE Wifi Bridge — Cloudflare Worker (durable threads)
 *
 * CLIENTS (same API, same accounts, same feed)
 * --------------------------------------------
 * 1. DreamShare web (GOODNIGHT homepage iframe) — posts with session token
 *    from the homepage login; WAVs via ?op=audio_part + R2/KV vault.
 * 2. DreamShare Lite VST3 — signs in with user + password against this
 *    worker (action: "login"); creates accounts on first use; posts
 *    threads/comments/chat/reacts with the returned token. Audio is
 *    optional (hasAudio flag; play links may point at dreamdaw.com).
 *
 * Both clients share:
 *   - feed bin  abffdbc  (threads + chat + mods)
 *   - users bin ffedede  (accounts + sessions)
 *   - optional DREAMSHARE_R2 / DREAMSHARE_KV for large WAVs + presence
 *
 * WHY THREADS WERE VANISHING
 * --------------------------
 * Older builds stored the lounge ONLY in the Cache API. Cloudflare cache
 * is per data-center and is evicted (often overnight). This build writes
 * the feed to the JSON bin (and KV if bound). Cache is never source of truth.
 *
 * SETUP
 * 1. dash.cloudflare.com → Workers & Pages → your DreamShare worker
 * 2. Edit code → DELETE all → paste THIS entire file → Deploy
 * 3. To keep WAVs (up to 75 MB) bind ONE of these. R2 is the one to use.
 *      R2:     Workers → R2 → Create bucket "dreamshare-tapes"
 *              Settings → Bindings → R2 bucket → Variable name: DREAMSHARE_R2
 *      or KV:  Workers → KV → Create namespace "DREAMSHARE"
 *              Settings → Bindings → KV namespace → Variable name: DREAMSHARE_KV
 *      Without a binding, only small tapes (about 6 MB) can be stored,
 *      and they go to the public JSON bin in pieces.
 * 4. Open the worker URL. JSON must say "storage": "durable-v4"
 *    and "audioMaxBytes": 78643200.
 *    "audioVault": true means the 75 MB tape vault is bound.
 * 5. VST Lite hardcodes: https://dreamshare-api.keganacummings.workers.dev/
 *    Web HTML WORKER_URL must match that same origin.
 *
 * Super-admins (hardcoded): Trippah, Goonr
 * Mods live in the durable feed (not the cache).
 *
 * API surface used by both clients
 * --------------------------------
 * GET  /                 → feed (threads, chat, mods, supers, online, roles, vst)
 * GET  /?download=vst    → DreamShare Windows VST3 zip (from R2 releases/)
 * GET  /?wav=<id>[&part=N] → stream tape (chunks or R2/KV parts)
 * POST /?op=audio_part   → binary WAV part (headers X-SW-Token, X-SW-Upload,
 *                          X-SW-Index, X-SW-Parts) — web only for large tapes
 * POST /  body.action:
 *   login | register     → { user, pass|password } → { token, role, user }
 *   create_thread|post|thread
 *   comment|reply
 *   delete_thread | delete_comment
 *   chat_send|chat | chat_list|chat_get | chat_delete | chat_clear
 *   react                → { kind: thread|comment|chat, id, emoji, threadId? }
 *   promote_mod|mod | demote_mod|unmod
 *   set_role | clear_role
 *   set_custom_role | clear_custom_role   (Trippah/Goonr only — collective badge)
 *   presence|heartbeat
 *   social_list|friends_list | friend_request | friend_accept|friend_decline | friend_remove
 *   dm_list | dm_send|dm
 *   wav_request | wav_request_approve|wav_request_decline | wav_request_fulfill | wav_request_status
 *   audio_part_b64
 *   discord_status | discord_channels | discord_messages | discord_send | discord_react
 *     (requires worker secrets DISCORD_BOT_TOKEN; optional DISCORD_GUILD_ID)
 *   GET /?dmwav=<messageId>&token=<session>[&part=N] → private DM WAV stream
 *
 * Theme patch compatibility:
 *   set_theme/theme, session/whoami/me, login themePack/themes
 *   customRoles via GET/feed + set_custom_role/clear_custom_role
 *
 * Discord lite (0.4.1):
 *   VST Discord tab uses bot API only — never embeds discord.com or holds a user token.
 *   Posts/reactions are sent as the bot. Message content needs Message Content Intent.
 */

const BIN = 'https://extendsclass.com/api/json-storage/bin/abffdbc';
const BIN_API = 'https://extendsclass.com/api/json-storage/bin';
const FEED_BIN_ID = 'abffdbc';
const USERS_BIN = 'https://extendsclass.com/api/json-storage/bin/ffedede';
const STORAGE = 'durable-v4';

// DreamShare VST theme compatibility patch.
// The compiled VST can authenticate against these APIs; the web UI can also
// consume the same payload. The binary in the supplied ZIP does not contain
// the source needed to add a native theme dropdown/effects renderer.
const THEME_API_VERSION = 'v1';
const VST_PATCH_VERSION = '0.5.0';
const KYOTO_MODULE_FORMAT = 'kyoteppah-module-1';
const KYOTO_API_VERSION = '0.3.0';
const VST_RELEASE = {
  version: '0.2.3',
  name: 'DreamShare-Windows-VST3.zip',
  label: 'DreamShare VST3 (Windows x64)',
  r2Key: 'releases/DreamShare-Windows-VST3.zip',
  // Optional public mirror if R2 is not bound:
  fallbackUrl: ''
};
const SUPER_ADMINS = ['Trippah', 'Goonr'];
const META_ID = '_dreamshare_meta';
const MAX_BIN = 88000;
const AUDIO_KEEP_MS = 100 * 24 * 60 * 60 * 1000;
const CHUNK_B64 = 68000;
const MAX_AUDIO_BYTES = 75 * 1024 * 1024;
const MAX_PART_BYTES = 4 * 1024 * 1024;
const SESSION_MS = 30 * 24 * 60 * 60 * 1000;

// Instrument preset storage — account backed so presets survive FL/VST restarts.
const PRESET_MAX_PER_USER = 60;
const PRESET_MAX_STATE = 24000;
const PRESET_NAME_MAX = 48;
const PRESET_MACHINE_MAX = 64;
const COMMUNITY_MAX = 500;
const COMMUNITY_STATE_MAX = 24000;
const COMMUNITY_NAME_MAX = 64;
const COMMUNITY_DESC_MAX = 280;


const THEME_IDS = ["abyss","amber","ash","aurora","bloodmoon","bone","cherry","cinder","cobalt","copper","dawn","default","dusk","ember","fog","goonr","graphite","honey","ice","ink","lagoon","light","lilac","marrow","mint","moss","neon","olive","paper","pine","plum","rust","sand","static","steel","sulfur","trippah","velvet","violet","void","vsttrippah","wine"];
const THEMES = [{"id":"abyss","name":"ABYSS","tag":"deep sea","vars":{"--bg":"#04080e","--panel":"#0a1420","--accent":"#3ec8d8","--accent-dim":"#147888","--accent-bright":"#d4fbff","--accent2":"#7ee0ea","--text":"#e4f8fa","--text-dim":"#6e949c","--border":"#143038","--glow":"rgba(62,200,216,0.22)","--panel-2":"#04060c","--radius":"4px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(62,200,216,0.22), transparent 62%), linear-gradient(180deg, #0a1420, #04080e)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"ABYSS.html"},{"id":"amber","name":"Amber","tag":"sodium lamp — honey light from the floor","vars":{"--bg":"#140e06","--panel":"#24180a","--accent":"#f5b042","--accent-dim":"#b45309","--accent-bright":"#fde68a","--accent2":"#ea580c","--text":"#fff7e8","--text-dim":"#c4a574","--border":"#4a3414","--glow":"rgba(245,176,66,0.28)","--panel-2":"#1a1208","--radius":"2px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 50% 28% at 50% 110%, rgba(240,160,32,0.28), transparent 62%), radial-gradient(circle at 80% 12%, rgba(255,196,90,0.14), transparent 40%), repeating-linear-gradient(0deg, rgba(80,40,0,0.08) 0 1px, transparent 1px 4px), linear-gradient(180deg, #1c1206, #120c04)","veil":"radial-gradient(ellipse at center, transparent 56%, rgba(40,20,0,0.4) 100%)","file":"AMBER.html"},{"id":"ash","name":"Ash","tag":"cold smoke — steel grey, drifting ash","vars":{"--bg":"#12151c","--panel":"#1b212b","--accent":"#9bb4c8","--accent-dim":"#5d7386","--accent-bright":"#d5e6f2","--accent2":"#7f96aa","--text":"#e7eef4","--text-dim":"#8b97a6","--border":"#2c3542","--glow":"rgba(155,180,200,0.2)","--panel-2":"#10141b","--radius":"0px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 26% at 18% 0%, rgba(180,190,200,0.16), transparent 60%), radial-gradient(ellipse 30% 40% at 90% 100%, rgba(40,48,58,0.55), transparent 60%), repeating-linear-gradient(115deg, transparent 0 12px, rgba(255,255,255,0.02) 12px 13px), linear-gradient(180deg, #1a1e26, #0c0e14)","veil":"radial-gradient(ellipse at center, transparent 55%, rgba(8,10,14,0.45) 100%)","file":"ASH.html"},{"id":"aurora","name":"AURORA","tag":"green violet night","vars":{"--bg":"#070812","--panel":"#10141e","--accent":"#5dffa8","--accent-dim":"#2a8a68","--accent-bright":"#e8fff4","--accent2":"#b06bff","--text":"#eefaf4","--text-dim":"#8aa498","--border":"#243038","--glow":"rgba(93,255,168,0.2)","--panel-2":"#060810","--radius":"8px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(93,255,168,0.2), transparent 62%), linear-gradient(180deg, #10141e, #070812)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"AURORA.html"},{"id":"bloodmoon","name":"Bloodmoon","tag":"eclipse — one red moon, black sky","vars":{"--bg":"#070203","--panel":"#16080c","--accent":"#e23a4a","--accent-dim":"#8a1424","--accent-bright":"#ff8a90","--accent2":"#ffd0c8","--text":"#f8e8e6","--text-dim":"#a07878","--border":"#3a1820","--glow":"rgba(226,58,74,0.28)","--panel-2":"#100408","--radius":"0px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(circle at 78% 16%, rgba(255,214,206,0.95) 0 14px, rgba(196,36,48,0.55) 16px 42px, rgba(60,6,12,0.28) 44px 86px, transparent 110px), radial-gradient(ellipse 70% 36% at 50% 110%, rgba(90,0,16,0.45), transparent 62%), linear-gradient(185deg, #050103 0%, #140206 48%, #070203 100%)","veil":"radial-gradient(ellipse at center, transparent 52%, rgba(40,0,8,0.5) 100%)","file":"BLOODMOON.html"},{"id":"bone","name":"Bone","tag":"ivory studio — dry parchment on charcoal","vars":{"--bg":"#161310","--panel":"#24201b","--accent":"#f0e2c8","--accent-dim":"#a89878","--accent-bright":"#fff8ea","--accent2":"#d4b483","--text":"#f7f1e6","--text-dim":"#a39888","--border":"#3a332c","--glow":"rgba(240,226,200,0.16)","--panel-2":"#1c1814","--radius":"2px","--font":"Georgia, 'Iowan Old Style', serif"},"scene":"radial-gradient(ellipse 55% 24% at 50% 0%, rgba(255,244,220,0.08), transparent 60%), repeating-linear-gradient(90deg, transparent 0 18px, rgba(232,216,184,0.04) 18px 19px), linear-gradient(180deg, #221e1a, #141210)","veil":"radial-gradient(ellipse at center, transparent 62%, rgba(20,16,12,0.4) 100%)","file":"BONE.html"},{"id":"cherry","name":"CHERRY","tag":"pink night","vars":{"--bg":"#140810","--panel":"#241018","--accent":"#ff6a9a","--accent-dim":"#c03068","--accent-bright":"#ffd0e0","--accent2":"#ff8ab0","--text":"#fff0f4","--text-dim":"#c498a8","--border":"#4a2434","--glow":"rgba(255,106,154,0.22)","--panel-2":"#10060c","--radius":"10px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(255,106,154,0.22), transparent 62%), linear-gradient(180deg, #241018, #140810)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"CHERRY.html"},{"id":"cinder","name":"CINDER","tag":"ash rose","vars":{"--bg":"#120c0c","--panel":"#201414","--accent":"#e08a8a","--accent-dim":"#a05050","--accent-bright":"#ffd8d8","--accent2":"#f0b0b0","--text":"#f8eeee","--text-dim":"#b89898","--border":"#402828","--glow":"rgba(224,138,138,0.2)","--panel-2":"#0e0808","--radius":"6px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(224,138,138,0.2), transparent 62%), linear-gradient(180deg, #201414, #120c0c)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"CINDER.html"},{"id":"cobalt","name":"COBALT","tag":"studio blue","vars":{"--bg":"#070c18","--panel":"#101828","--accent":"#4d8dff","--accent-dim":"#2456b0","--accent-bright":"#cfe0ff","--accent2":"#7eb0ff","--text":"#e8f0ff","--text-dim":"#8496b4","--border":"#24344e","--glow":"rgba(77,141,255,0.24)","--panel-2":"#060a14","--radius":"0px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(77,141,255,0.24), transparent 62%), linear-gradient(180deg, #101828, #070c18)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"COBALT.html"},{"id":"copper","name":"COPPER","tag":"workshop metal","vars":{"--bg":"#140e0c","--panel":"#241814","--accent":"#e07a45","--accent-dim":"#a84828","--accent-bright":"#ffc2a4","--accent2":"#ffb088","--text":"#f8ece6","--text-dim":"#b5988c","--border":"#4a3028","--glow":"rgba(224,122,69,0.24)","--panel-2":"#100c0a","--radius":"0px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(224,122,69,0.24), transparent 62%), linear-gradient(180deg, #241814, #140e0c)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"COPPER.html"},{"id":"dawn","name":"DAWN","tag":"first light — warm paper, copper line","vars":{"--bg":"#1a120c","--panel":"#2a1c14","--accent":"#e8a05a","--accent-dim":"#a86830","--accent-bright":"#ffd2a0","--accent2":"#c46a3a","--text":"#f6efe6","--text-dim":"#b9a48f","--border":"#4a3424","--glow":"rgba(232,160,90,0.24)","--panel-2":"#140e0a","--radius":"10px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(232,160,90,0.24), transparent 62%), linear-gradient(180deg, #2a1c14, #1a120c)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"DAWN.html"},{"id":"default","name":"Default","tag":"night lamp — a red glow, no splatter","vars":{"--bg":"#0a0a0a","--panel":"#141414","--accent":"#e62020","--accent-dim":"#a01818","--accent-bright":"#ff5555","--accent2":"#ff3344","--text":"#f4f4f4","--text-dim":"#9a9a9a","--border":"#2c2c2c","--glow":"rgba(230,32,32,0.22)","--panel-2":"#0e0e0e","--radius":"0px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 42% 22% at 50% -8%, rgba(230,32,32,0.18), transparent 64%), repeating-linear-gradient(90deg, rgba(255,255,255,0.015) 0 1px, transparent 1px 3px), linear-gradient(180deg, #141414, #070707)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.4) 100%)","file":"DEFAULT.html"},{"id":"dusk","name":"DUSK","tag":"blue hour — indigo room","vars":{"--bg":"#0c1020","--panel":"#14182c","--accent":"#7aa2ff","--accent-dim":"#3d5cb0","--accent-bright":"#c5d6ff","--accent2":"#9a7adf","--text":"#e8eeff","--text-dim":"#8b97b8","--border":"#2a3350","--glow":"rgba(122,162,255,0.24)","--panel-2":"#0a0e1a","--radius":"0px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(122,162,255,0.24), transparent 62%), linear-gradient(180deg, #14182c, #0c1020)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"DUSK.html"},{"id":"ember","name":"EMBER","tag":"coal and orange","vars":{"--bg":"#100806","--panel":"#1c100c","--accent":"#ff6a2a","--accent-dim":"#b03810","--accent-bright":"#ffc8a8","--accent2":"#ff8a4a","--text":"#ffe8dc","--text-dim":"#c49888","--border":"#4a2818","--glow":"rgba(255,106,42,0.24)","--panel-2":"#0c0604","--radius":"2px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(255,106,42,0.24), transparent 62%), linear-gradient(180deg, #1c100c, #100806)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"EMBER.html"},{"id":"fog","name":"FOG","tag":"gray mist, soft edges","vars":{"--bg":"#121416","--panel":"#1c2024","--accent":"#c5d0d6","--accent-dim":"#7d8c96","--accent-bright":"#ffffff","--accent2":"#9eb0ba","--text":"#eef2f4","--text-dim":"#9aa6ae","--border":"#343c42","--glow":"rgba(197,208,214,0.18)","--panel-2":"#0e1012","--radius":"12px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(197,208,214,0.18), transparent 62%), linear-gradient(180deg, #1c2024, #121416)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"FOG.html"},{"id":"goonr","name":"GOONR","tag":"matrix rain — green scan, RGB edge","vars":{"--bg":"#020804","--panel":"#04140c","--accent":"#39ff14","--accent-dim":"#0d6b12","--accent-bright":"#b6ff9a","--accent2":"#00e5ff","--text":"#d8ffd0","--text-dim":"#5d8a62","--border":"#145c22","--glow":"rgba(57,255,20,0.35)","--panel-2":"#010a04","--radius":"0px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"linear-gradient(180deg, rgba(57,255,20,0.07), transparent 16%), repeating-linear-gradient(0deg, rgba(57,255,20,0.05) 0 1px, transparent 1px 8px), repeating-linear-gradient(90deg, rgba(0,229,255,0.035) 0 1px, transparent 1px 32px), radial-gradient(ellipse at 50% 120%, rgba(0,48,12,0.65), transparent 52%), #020804","veil":"radial-gradient(ellipse at center, transparent 62%, rgba(0,12,4,0.55) 100%)","file":"GOONR.html"},{"id":"graphite","name":"GRAPHITE","tag":"neutral dark","vars":{"--bg":"#101010","--panel":"#1a1a1a","--accent":"#e8e8e8","--accent-dim":"#8a8a8a","--accent-bright":"#ffffff","--accent2":"#bdbdbd","--text":"#f2f2f2","--text-dim":"#9a9a9a","--border":"#333333","--glow":"rgba(255,255,255,0.12)","--panel-2":"#0c0c0c","--radius":"0px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(255,255,255,0.12), transparent 62%), linear-gradient(180deg, #1a1a1a, #101010)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"GRAPHITE.html"},{"id":"honey","name":"HONEY","tag":"amber lamp","vars":{"--bg":"#161006","--panel":"#261c0c","--accent":"#f0c14a","--accent-dim":"#b08420","--accent-bright":"#fff3c4","--accent2":"#ffd56a","--text":"#fff8e6","--text-dim":"#c4b088","--border":"#4a3c18","--glow":"rgba(240,193,74,0.24)","--panel-2":"#120e06","--radius":"6px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(240,193,74,0.24), transparent 62%), linear-gradient(180deg, #261c0c, #161006)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"HONEY.html"},{"id":"ice","name":"Ice","tag":"frost glass — pale cyan, cold corners","vars":{"--bg":"#07141a","--panel":"#10242c","--accent":"#9aebf5","--accent-dim":"#3d8b9c","--accent-bright":"#e0fbff","--accent2":"#67c6de","--text":"#e7f7fb","--text-dim":"#7f9aa4","--border":"#1e3a44","--glow":"rgba(154,235,245,0.22)","--panel-2":"#0c1c22","--radius":"2px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"linear-gradient(180deg, rgba(200,244,255,0.08), transparent 22%), radial-gradient(ellipse 36% 20% at 10% 0%, rgba(142,224,240,0.18), transparent 62%), radial-gradient(ellipse 40% 28% at 100% 100%, rgba(80,140,180,0.16), transparent 60%), repeating-linear-gradient(0deg, rgba(200,240,255,0.035) 0 1px, transparent 1px 9px), linear-gradient(180deg, #102028, #081014)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(4,16,24,0.42) 100%)","file":"ICE.html"},{"id":"ink","name":"INK","tag":"black paper, blue ink","vars":{"--bg":"#07080c","--panel":"#101218","--accent":"#6ec1ff","--accent-dim":"#2a6ea8","--accent-bright":"#d7f1ff","--accent2":"#8fd0ff","--text":"#e7f3ff","--text-dim":"#8aa0b4","--border":"#243040","--glow":"rgba(110,193,255,0.22)","--panel-2":"#090b10","--radius":"2px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(110,193,255,0.22), transparent 62%), linear-gradient(180deg, #101218, #07080c)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"INK.html"},{"id":"lagoon","name":"LAGOON","tag":"teal water","vars":{"--bg":"#061416","--panel":"#0e2428","--accent":"#4ad0c8","--accent-dim":"#1a8880","--accent-bright":"#d8fffc","--accent2":"#7ae0d8","--text":"#e8fafa","--text-dim":"#7aa8a4","--border":"#1c4044","--glow":"rgba(74,208,200,0.22)","--panel-2":"#061012","--radius":"8px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(74,208,200,0.22), transparent 62%), linear-gradient(180deg, #0e2428, #061416)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"LAGOON.html"},{"id":"light","name":"Light","tag":"daylight paper — ruled desk, warm wash","vars":{"--bg":"#efe6d6","--panel":"#f7f1e6","--accent":"#9a3412","--accent-dim":"#7c2d12","--accent-bright":"#c2410c","--accent2":"#b45309","--text":"#1c1410","--text-dim":"#6b5344","--border":"#ddcbb6","--glow":"rgba(154,52,18,0.16)","--panel-2":"#f3eadc","--radius":"2px","--font":"Georgia, 'Iowan Old Style', serif"},"scene":"radial-gradient(ellipse 50% 30% at 50% -10%, rgba(255,248,236,0.9), transparent 60%), repeating-linear-gradient(0deg, rgba(90,60,40,0.05) 0 1px, transparent 1px 28px), linear-gradient(180deg, #f7f1e6, #e7dccb)","veil":"radial-gradient(ellipse at center, transparent 64%, rgba(120,90,60,0.12) 100%)","file":"LIGHT.html"},{"id":"lilac","name":"LILAC","tag":"soft purple day","vars":{"--bg":"#14101c","--panel":"#221c2e","--accent":"#c9b6ff","--accent-dim":"#7a68c0","--accent-bright":"#f4eeff","--accent2":"#e0d4ff","--text":"#f6f2ff","--text-dim":"#a89cc0","--border":"#3a324c","--glow":"rgba(201,182,255,0.22)","--panel-2":"#100e16","--radius":"12px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(201,182,255,0.22), transparent 62%), linear-gradient(180deg, #221c2e, #14101c)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"LILAC.html"},{"id":"marrow","name":"MARROW","tag":"warm bone","vars":{"--bg":"#16130e","--panel":"#262218","--accent":"#e6d2b0","--accent-dim":"#a89470","--accent-bright":"#fff6e6","--accent2":"#f0e0c4","--text":"#f8f4ec","--text-dim":"#b4a890","--border":"#463c30","--glow":"rgba(230,210,176,0.2)","--panel-2":"#12100c","--radius":"4px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(230,210,176,0.2), transparent 62%), linear-gradient(180deg, #262218, #16130e)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"MARROW.html"},{"id":"mint","name":"MINT","tag":"cool clinic","vars":{"--bg":"#071412","--panel":"#102220","--accent":"#7dffe0","--accent-dim":"#2a9a84","--accent-bright":"#e8fff8","--accent2":"#9affea","--text":"#e8faf6","--text-dim":"#7eaea4","--border":"#1e3c38","--glow":"rgba(125,255,224,0.2)","--panel-2":"#061210","--radius":"10px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(125,255,224,0.2), transparent 62%), linear-gradient(180deg, #102220, #071412)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"MINT.html"},{"id":"moss","name":"Moss","tag":"wet forest — damp green and a little gold","vars":{"--bg":"#08110c","--panel":"#122016","--accent":"#6ecf6a","--accent-dim":"#3f7d3c","--accent-bright":"#bbf7d0","--accent2":"#d6b25e","--text":"#e7f6e4","--text-dim":"#7d9478","--border":"#24382a","--glow":"rgba(110,207,106,0.22)","--panel-2":"#0c1610","--radius":"2px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 46% 30% at 0% 20%, rgba(110,207,106,0.18), transparent 60%), radial-gradient(ellipse 36% 28% at 100% 90%, rgba(200,160,64,0.12), transparent 58%), repeating-linear-gradient(28deg, transparent 0 10px, rgba(80,120,60,0.05) 10px 11px), linear-gradient(180deg, #102016, #08100a)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(4,16,8,0.42) 100%)","file":"MOSS.html"},{"id":"neon","name":"Neon","tag":"club grid — cyan streets, magenta sky","vars":{"--bg":"#070812","--panel":"#101426","--accent":"#22f0e0","--accent-dim":"#0e8f86","--accent-bright":"#99fff6","--accent2":"#ff3dbe","--text":"#e8fbff","--text-dim":"#7d90a8","--border":"#243044","--glow":"rgba(34,240,224,0.28)","--panel-2":"#0a0e1c","--radius":"0px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"linear-gradient(180deg, rgba(255,64,200,0.08), transparent 24%), repeating-linear-gradient(90deg, rgba(32,240,224,0.05) 0 1px, transparent 1px 48px), repeating-linear-gradient(0deg, rgba(255,64,200,0.04) 0 1px, transparent 1px 48px), radial-gradient(ellipse at 50% 120%, rgba(32,240,224,0.12), transparent 50%), #06080e","veil":"radial-gradient(ellipse at center, transparent 58%, rgba(4,0,16,0.55) 100%)","file":"NEON.html"},{"id":"olive","name":"OLIVE","tag":"army green","vars":{"--bg":"#101208","--panel":"#1c2010","--accent":"#c6d26a","--accent-dim":"#7a8430","--accent-bright":"#f0f6c0","--accent2":"#d8e48a","--text":"#f4f6e4","--text-dim":"#a4aa84","--border":"#3a4024","--glow":"rgba(198,210,106,0.2)","--panel-2":"#0c0e06","--radius":"2px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(198,210,106,0.2), transparent 62%), linear-gradient(180deg, #1c2010, #101208)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"OLIVE.html"},{"id":"paper","name":"PAPER","tag":"off-white desk","vars":{"--bg":"#efe8dc","--panel":"#f7f2ea","--accent":"#b4482c","--accent-dim":"#8a3420","--accent-bright":"#7a2414","--accent2":"#c46a48","--text":"#2a221c","--text-dim":"#7a6e62","--border":"#d8d0c4","--glow":"rgba(180,72,44,0.16)","--panel-2":"#e7e0d4","--radius":"8px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(180,72,44,0.16), transparent 62%), linear-gradient(180deg, #f7f2ea, #efe8dc)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"PAPER.html"},{"id":"pine","name":"PINE","tag":"deep forest","vars":{"--bg":"#07120c","--panel":"#102018","--accent":"#7dcc78","--accent-dim":"#3d7a40","--accent-bright":"#d4ffd0","--accent2":"#9ad68a","--text":"#e7f6e4","--text-dim":"#7f9c84","--border":"#24382c","--glow":"rgba(125,204,120,0.2)","--panel-2":"#08140e","--radius":"6px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(125,204,120,0.2), transparent 62%), linear-gradient(180deg, #102018, #07120c)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"PINE.html"},{"id":"plum","name":"PLUM","tag":"dark fruit","vars":{"--bg":"#140814","--panel":"#241028","--accent":"#d06adf","--accent-dim":"#8a3c96","--accent-bright":"#f6c8ff","--accent2":"#e090ea","--text":"#f8e8fa","--text-dim":"#b48cb8","--border":"#402044","--glow":"rgba(208,106,223,0.22)","--panel-2":"#100610","--radius":"8px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(208,106,223,0.22), transparent 62%), linear-gradient(180deg, #241028, #140814)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"PLUM.html"},{"id":"rust","name":"Rust","tag":"oxidized iron — orange streaks, dark metal","vars":{"--bg":"#140a07","--panel":"#24140e","--accent":"#e07a3d","--accent-dim":"#9a4e22","--accent-bright":"#fdba74","--accent2":"#c2410c","--text":"#f6e7dc","--text-dim":"#b08974","--border":"#4a2c1e","--glow":"rgba(224,122,61,0.26)","--panel-2":"#1a0e0a","--radius":"0px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"repeating-linear-gradient(118deg, rgba(208,96,40,0.05) 0 2px, transparent 2px 7px), radial-gradient(ellipse 40% 30% at 8% 90%, rgba(160,64,24,0.28), transparent 60%), radial-gradient(ellipse 30% 24% at 100% 10%, rgba(90,40,20,0.35), transparent 55%), linear-gradient(180deg, #1c0e0a, #120806)","veil":"radial-gradient(ellipse at center, transparent 58%, rgba(24,8,4,0.48) 100%)","file":"RUST.html"},{"id":"sand","name":"SAND","tag":"desert noon — dry gold","vars":{"--bg":"#1c160f","--panel":"#2a2218","--accent":"#e2c07a","--accent-dim":"#a48442","--accent-bright":"#fff0c8","--accent2":"#d8a85a","--text":"#f8f1e2","--text-dim":"#b5a48a","--border":"#4a3c2a","--glow":"rgba(226,192,122,0.22)","--panel-2":"#14110c","--radius":"4px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(226,192,122,0.22), transparent 62%), linear-gradient(180deg, #2a2218, #1c160f)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"SAND.html"},{"id":"static","name":"STATIC","tag":"crt green","vars":{"--bg":"#050806","--panel":"#0c120e","--accent":"#8dff6a","--accent-dim":"#3a8a28","--accent-bright":"#e4ffd8","--accent2":"#b6ff9a","--text":"#e8ffe4","--text-dim":"#7a9a70","--border":"#1c3020","--glow":"rgba(141,255,106,0.2)","--panel-2":"#040604","--radius":"0px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(141,255,106,0.2), transparent 62%), linear-gradient(180deg, #0c120e, #050806)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"STATIC.html"},{"id":"steel","name":"STEEL","tag":"industrial gray","vars":{"--bg":"#101214","--panel":"#1a1e22","--accent":"#d0d6dc","--accent-dim":"#808890","--accent-bright":"#ffffff","--accent2":"#aeb6bc","--text":"#f2f4f6","--text-dim":"#8e969c","--border":"#343a40","--glow":"rgba(208,214,220,0.16)","--panel-2":"#0c0e10","--radius":"0px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(208,214,220,0.16), transparent 62%), linear-gradient(180deg, #1a1e22, #101214)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"STEEL.html"},{"id":"sulfur","name":"Sulfur","tag":"chemical haze — sick lime over black","vars":{"--bg":"#070804","--panel":"#12180c","--accent":"#c6f531","--accent-dim":"#6d8f14","--accent-bright":"#eaff9a","--accent2":"#d24a22","--text":"#f3f8d8","--text-dim":"#8d9a68","--border":"#2c3814","--glow":"rgba(198,245,49,0.22)","--panel-2":"#0c1008","--radius":"0px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 36% 22% at 12% 8%, rgba(200,255,51,0.16), transparent 62%), radial-gradient(ellipse 40% 30% at 100% 100%, rgba(230,32,32,0.12), transparent 55%), repeating-linear-gradient(0deg, rgba(200,255,51,0.04) 0 1px, transparent 1px 5px), linear-gradient(180deg, #12160a, #070804)","veil":"radial-gradient(ellipse at center, transparent 58%, rgba(20,28,4,0.45) 100%)","file":"SULFUR.html"},{"id":"trippah","name":"TRIPPAH","tag":"wine cellar — soft falling spores, no letter rewrite","vars":{"--bg":"#0e0608","--panel":"#1a0c12","--accent":"#c04068","--accent-dim":"#7a2844","--accent-bright":"#e07090","--accent2":"#a02848","--text":"#f4e4ea","--text-dim":"#9a7884","--border":"#341820","--glow":"rgba(192,64,104,0.26)","--panel-2":"#12080c","--radius":"0px","--font":"'Fredoka', 'Share Tech Mono', ui-sans-serif, sans-serif"},"scene":"radial-gradient(ellipse 48% 28% at 6% -6%, rgba(192,64,104,0.34), transparent 62%), radial-gradient(ellipse 36% 24% at 100% 0%, rgba(90,20,48,0.45), transparent 58%), radial-gradient(circle at 22% 92%, rgba(224,112,144,0.1), transparent 36%), repeating-linear-gradient(0deg, rgba(0,0,0,0.14) 0 1px, transparent 1px 6px), linear-gradient(180deg, #1a0a10, #0e0608 42%, #070305)","veil":"radial-gradient(ellipse at center, transparent 58%, rgba(40,8,16,0.45) 100%)","file":"TRIPPAH.html"},{"id":"velvet","name":"VELVET","tag":"deep burgundy","vars":{"--bg":"#12060a","--panel":"#220c12","--accent":"#c43a58","--accent-dim":"#801828","--accent-bright":"#ffc0cc","--accent2":"#e06078","--text":"#f8e8ec","--text-dim":"#b48894","--border":"#401820","--glow":"rgba(196,58,88,0.24)","--panel-2":"#0e0408","--radius":"8px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 22% at 50% -6%, rgba(196,58,88,0.24), transparent 62%), linear-gradient(180deg, #220c12, #12060a)","veil":"radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.42) 100%)","file":"VELVET.html"},{"id":"violet","name":"Violet","tag":"ultraviolet room — bruise purple nebula","vars":{"--bg":"#0c0814","--panel":"#181028","--accent":"#c084fc","--accent-dim":"#7e22ce","--accent-bright":"#e9d5ff","--accent2":"#a855f7","--text":"#f3e8ff","--text-dim":"#a78bb8","--border":"#342450","--glow":"rgba(192,132,252,0.28)","--panel-2":"#120c1c","--radius":"2px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(ellipse 40% 32% at 15% 10%, rgba(180,108,255,0.22), transparent 60%), radial-gradient(ellipse 32% 28% at 90% 80%, rgba(80,40,180,0.28), transparent 62%), repeating-linear-gradient(0deg, rgba(180,108,255,0.035) 0 1px, transparent 1px 7px), linear-gradient(180deg, #140c22, #0c0814)","veil":"radial-gradient(ellipse at center, transparent 58%, rgba(16,6,32,0.5) 100%)","file":"VIOLET.html"},{"id":"void","name":"Void","tag":"deep space — almost black, two distant stars","vars":{"--bg":"#03050c","--panel":"#0a1020","--accent":"#7aa2e3","--accent-dim":"#345084","--accent-bright":"#dbe7ff","--accent2":"#4c6cb5","--text":"#e6eefc","--text-dim":"#7d8eae","--border":"#1a2740","--glow":"rgba(122,162,227,0.22)","--panel-2":"#060a14","--radius":"0px","--font":"'Share Tech Mono', ui-monospace, monospace"},"scene":"radial-gradient(circle at 72% 22%, rgba(220,230,255,0.85) 0 1px, rgba(120,160,220,0.25) 2px 3px, transparent 8px), radial-gradient(circle at 30% 70%, rgba(180,200,255,0.5) 0 1px, transparent 4px), radial-gradient(ellipse 60% 40% at 50% 100%, rgba(20,40,80,0.45), transparent 60%), linear-gradient(180deg, #02040a, #04060c 50%, #010208)","veil":"radial-gradient(ellipse at center, transparent 46%, rgba(0,0,8,0.72) 100%)","file":"VOID.html"},{"id":"vsttrippah","name":"VSTTRIPPAH","tag":"the wine-cellar colors every VST editor must use","vars":{"--bg":"#0e0608","--panel":"#1a0c12","--accent":"#c04068","--accent-dim":"#7a2844","--accent-bright":"#e07090","--accent2":"#a02848","--text":"#f4e4ea","--text-dim":"#9a7884","--border":"#341820","--glow":"rgba(192,64,104,0.26)","--panel-2":"#12080c","--radius":"0px","--font":"'Fredoka', 'Share Tech Mono', ui-sans-serif, sans-serif"},"scene":"radial-gradient(ellipse 48% 28% at 6% -6%, rgba(192,64,104,0.34), transparent 62%), linear-gradient(180deg, #1a0a10, #0e0608 42%, #070305)","veil":"radial-gradient(ellipse at center, transparent 58%, rgba(40,8,16,0.45) 100%)","file":"VSTTRIPPAH.html"},{"id":"wine","name":"Wine","tag":"cellar — bottles in the dark, one lamp","vars":{"--bg":"#12060c","--panel":"#241018","--accent":"#a33b5c","--accent-dim":"#6e243c","--accent-bright":"#e7b0c0","--accent2":"#7a2038","--text":"#f6e6ea","--text-dim":"#b08a96","--border":"#3d2030","--glow":"rgba(163,59,92,0.24)","--panel-2":"#180810","--radius":"2px","--font":"Georgia, 'Iowan Old Style', serif"},"scene":"radial-gradient(ellipse 28% 40% at 14% 78%, rgba(90,20,36,0.55), transparent 70%), radial-gradient(ellipse 18% 22% at 84% 30%, rgba(192,64,104,0.2), transparent 70%), radial-gradient(ellipse 50% 18% at 50% 0%, rgba(160,60,80,0.18), transparent 70%), linear-gradient(180deg, #16080e, #0c0608)","veil":"radial-gradient(ellipse at center, transparent 58%, rgba(24,6,10,0.48) 100%)","file":"WINE.html"}];

function cleanCommunityText(x, max) { return String(x || '').replace(/[<>]/g, '').replace(/\s+/g, ' ').trim().slice(0, max); }
function communityId(name, user) { return 'ci_' + String(user || 'user').toLowerCase().replace(/[^a-z0-9]/g,'').slice(0,16) + '_' + Date.now().toString(36) + '_' + randHex(3); }
async function communityIndex(env) {
  if (!env || !env.DREAMSHARE_KV) return [];
  const a = await env.DREAMSHARE_KV.get('community-instruments-v1', 'json');
  return Array.isArray(a) ? a : [];
}
async function communitySaveIndex(env, list) {
  if (!env || !env.DREAMSHARE_KV) throw new Error('DREAMSHARE_KV binding required');
  list = Array.isArray(list) ? list.slice(0, COMMUNITY_MAX) : [];
  await env.DREAMSHARE_KV.put('community-instruments-v1', JSON.stringify(list));
}
async function communityList(env) {
  const list = await communityIndex(env);
  return list.sort((a,b) => (b.updated || b.created || 0) - (a.updated || a.created || 0)).map(p => ({ id:p.id, name:p.name, author:p.author, description:p.description, created:p.created, updated:p.updated, downloads:p.downloads || 0 }));
}
async function communityGet(env, id) {
  if (!env || !env.DREAMSHARE_KV) throw new Error('DREAMSHARE_KV binding required');
  const clean = String(id || '').replace(/[^A-Za-z0-9_\-]/g,'').slice(0,80);
  if (!clean) return null;
  const p = await env.DREAMSHARE_KV.get('community-instrument:' + clean, 'json');
  if (!p) return null;
  const list = await communityIndex(env); const hit=list.find(x=>x.id===clean); if(hit){ hit.downloads=(hit.downloads||0)+1; hit.updated=hit.updated||hit.created||Date.now(); await communitySaveIndex(env,list); }
  return p;
}
async function communityPublish(env, user, body) {
  if (!env || !env.DREAMSHARE_KV) return { ok:false, error:'Community Instruments require a DREAMSHARE_KV binding.', code:'no-kv' };
  const name=cleanCommunityText(body.name,COMMUNITY_NAME_MAX); const description=cleanCommunityText(body.description,COMMUNITY_DESC_MAX);
  if(!name) return {ok:false,error:'Instrument name required'};
  let state=body.state; if(typeof state==='string'){try{state=JSON.parse(state)}catch(_){return {ok:false,error:'Invalid instrument state'}}}
  if(!state || typeof state!=='object') return {ok:false,error:'Instrument state required'};
  const encoded=JSON.stringify(state); if(encoded.length>COMMUNITY_STATE_MAX) return {ok:false,error:'Instrument is too large',code:'too-large'};
  const id=communityId(name,user), now=Date.now(); const entry={id,name,author:cleanName(user),description,created:now,updated:now,downloads:0,state};
  await env.DREAMSHARE_KV.put('community-instrument:'+id,JSON.stringify(entry));
  const list=await communityIndex(env); list.unshift({id,name,author:entry.author,description,created:now,updated:now,downloads:0}); await communitySaveIndex(env,list);
  return {ok:true,instrument:{id,name,author:entry.author,description,created:now,updated:now,downloads:0}};
}

function resolveTheme(id) {
  const key = String(id || 'trippah').toLowerCase().replace(/[^a-z0-9]/g, '');
  for (let i = 0; i < THEMES.length; i++) if (THEMES[i].id === key) return THEMES[i];
  return THEMES.find(function (t) { return t.id === 'trippah'; }) || THEMES[0];
}


const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET, POST, PUT, OPTIONS, HEAD',
  'Access-Control-Allow-Headers': 'Content-Type, Accept, Authorization, X-SW-Token, X-SW-Upload, X-SW-Index, X-SW-Parts, X-Requested-With',
  'Access-Control-Max-Age': '86400',
  'Cache-Control': 'no-store'
};

function json(data, status) {
  return new Response(JSON.stringify(data), {
    status: status || 200,
    headers: Object.assign({ 'Content-Type': 'application/json; charset=utf-8' }, CORS)
  });
}

function normUser(u) {
  return String(u || '').replace(/[^A-Za-z0-9_]/g, '').slice(0, 20);
}

function isSuper(user) {
  const u = String(user || '');
  return SUPER_ADMINS.some(function (a) { return a.toLowerCase() === u.toLowerCase(); });
}

function isMod(feed, user) {
  if (isSuper(user)) return true;
  const u = String(user || '').toLowerCase();
  return (feed.mods || []).some(function (m) { return String(m).toLowerCase() === u; });
}

// Custom collective badges (Trippah / Goonr only can assign)
// Shape: feed.customRoles[username] = { label, color, gradient }
const KYOTO_DEFAULT = {
  color: '#d5e0ff',
  gradient: 'linear-gradient(90deg, #7aa2e3 0%, #d5e0ff 45%, #c4b5fd 100%)',
  bg: '#1a2438'
};

function normalizeCustomRole(raw) {
  if (!raw || typeof raw !== 'object') return null;
  let label = String(raw.label || raw.name || raw.role || '').trim().slice(0, 24);
  if (!label) return null;
  // visible display name — allow letters, numbers, spaces, limited punctuation
  label = label.replace(/[^\w\s\-_.&+]/g, '').replace(/\s+/g, ' ').trim().slice(0, 24);
  if (!label || label.length < 2) return null;
  let color = String(raw.color || KYOTO_DEFAULT.color).trim();
  if (!/^#[0-9A-Fa-f]{3,8}$/.test(color)) color = KYOTO_DEFAULT.color;
  let gradient = String(raw.gradient || '').trim().slice(0, 200);
  if (!gradient || gradient.toLowerCase().indexOf('gradient') < 0) {
    gradient = KYOTO_DEFAULT.gradient;
  }
  let bg = String(raw.bg || raw.background || KYOTO_DEFAULT.bg).trim();
  if (!/^#[0-9A-Fa-f]{3,8}$/.test(bg)) bg = KYOTO_DEFAULT.bg;
  return { label: label, color: color, gradient: gradient, bg: bg };
}

function cleanCustomRoles(map) {
  const out = {};
  if (!map || typeof map !== 'object') return out;
  Object.keys(map).forEach(function (k) {
    const user = normUser(k);
    if (!user) return;
    const role = normalizeCustomRole(map[k]);
    if (role) out[user] = role;
  });
  return out;
}

function mergeCustomRoles(a, b) {
  return Object.assign({}, cleanCustomRoles(a), cleanCustomRoles(b));
}


function randHex(n) {
  const a = new Uint8Array(n);
  crypto.getRandomValues(a);
  return Array.from(a).map(function (b) { return ('0' + b.toString(16)).slice(-2); }).join('');
}

async function pbkdf2(password, saltHex) {
  const salt = new Uint8Array((String(saltHex || '').match(/.{1,2}/g) || []).map(function (b) { return parseInt(b, 16); }));
  const key = await crypto.subtle.importKey('raw', new TextEncoder().encode(String(password || '')), 'PBKDF2', false, ['deriveBits']);
  const bits = await crypto.subtle.deriveBits({ name: 'PBKDF2', salt: salt, iterations: 100000, hash: 'SHA-256' }, key, 256);
  return Array.from(new Uint8Array(bits)).map(function (b) { return ('0' + b.toString(16)).slice(-2); }).join('');
}

async function readAccounts(env) {
  try {
    if (env && env.DREAMSHARE_KV) {
      const j = await env.DREAMSHARE_KV.get('accounts-v1', 'json');
      if (j && j.users) return j;
    }
  } catch (_) {}
  const r = await fetch(USERS_BIN + '?t=' + Date.now(), {
    headers: { Accept: 'application/json', 'Cache-Control': 'no-cache' }
  });
  if (!r.ok) throw new Error('accounts ' + r.status);
  let j = await r.json();
  if (typeof j === 'string') j = JSON.parse(j);
  if (!j || typeof j !== 'object') j = {};
  if (!j.users || typeof j.users !== 'object') j.users = {};
  if (!j.sessions || typeof j.sessions !== 'object') j.sessions = {};
  ensureSocial(j);
  return j;
}

async function writeAccounts(env, data) {
  const payload = JSON.stringify({
    users: data.users || {},
    sessions: data.sessions || {},
    social: data.social || {},
    storage: 'accounts-v1',
    updated: Date.now()
  });
  let kvOk = false;
  try {
    if (env && env.DREAMSHARE_KV) {
      await env.DREAMSHARE_KV.put('accounts-v1', payload);
      kvOk = true;
    }
  } catch (_) {}
  const r = await fetch(USERS_BIN, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json', Accept: 'application/json' },
    body: payload
  });
  if (!r.ok && !kvOk) throw new Error('accounts save ' + r.status);
}


function ensureSocial(db) {
  if (!db.social || typeof db.social !== 'object') db.social = {};
  if (!db.social.users || typeof db.social.users !== 'object') db.social.users = {};
  return db.social;
}
function socialUser(db, user) {
  const s = ensureSocial(db);
  const key = String(user || '').toLowerCase();
  if (!s.users[key] || typeof s.users[key] !== 'object') {
    s.users[key] = { friends: [], incoming: [], outgoing: [], dms: [], wavRequests: [] };
  }
  const u = s.users[key];
  if (!Array.isArray(u.friends)) u.friends = [];
  if (!Array.isArray(u.incoming)) u.incoming = [];
  if (!Array.isArray(u.outgoing)) u.outgoing = [];
  if (!Array.isArray(u.dms)) u.dms = [];
  if (!Array.isArray(u.wavRequests)) u.wavRequests = [];
  return u;
}
function cleanName(x) { return normUser(x).slice(0, 20); }
function socialMessageId() { return 'dm' + Date.now() + Math.floor(Math.random() * 999); }
function socialRequestId(prefix) { return String(prefix || 'rq') + Date.now() + Math.floor(Math.random() * 999); }
function accountExists(db, name) {
  const n = cleanName(name);
  return !!(n && db.users && db.users[n.toLowerCase()]);
}
function addUniqueName(arr, name) {
  const n = cleanName(name);
  if (!n) return;
  if (!arr.some(x => String(x).toLowerCase() === n.toLowerCase())) arr.push(n);
}
function removeName(arr, name) {
  return (arr || []).filter(x => String(x).toLowerCase() !== String(name || '').toLowerCase());
}
function addDm(db, from, to, message) {
  const a = socialUser(db, from), b = socialUser(db, to);
  const fromRec = db.users && db.users[cleanName(from).toLowerCase()];
  const m = Object.assign({ id: socialMessageId(), from: cleanName(from), to: cleanName(to), at: Date.now(), theme: (fromRec && fromRec.theme) || 'trippah' }, message || {});
  a.dms.push(m); b.dms.push(m);
  a.dms = a.dms.slice(-200); b.dms = b.dms.slice(-200);
  return m;
}
function privateDmList(db, a, b) {
  const u = socialUser(db, a);
  return u.dms.filter(m =>
    (String(m.from).toLowerCase() === String(a).toLowerCase() && String(m.to).toLowerCase() === String(b).toLowerCase()) ||
    (String(m.from).toLowerCase() === String(b).toLowerCase() && String(m.to).toLowerCase() === String(a).toLowerCase())
  ).slice(-100);
}
function socialDirectory(db) {
  return Object.keys(db.users || {}).map(k => db.users[k] && db.users[k].name).filter(Boolean).sort((a,b)=>String(a).localeCompare(String(b))).slice(0, 500);
}

async function loginAccount(env, body) {
  const user = normUser(body.user);
  const pass = String(body.pass || body.password || '');
  if (!user || user.length < 3) return { ok: false, error: 'Username needs 3+ letters or numbers.', code: 'bad-user' };
  if (!pass) return { ok: false, error: 'Password required.', code: 'bad-pass' };
  const db = await readAccounts(env);
  const key = user.toLowerCase();
  const now = Date.now();
  Object.keys(db.sessions).forEach(function (k) {
    if (!db.sessions[k] || (db.sessions[k].exp || 0) < now) delete db.sessions[k];
  });
  let rec = db.users[key];
  let created = false;
  if (!rec) {
    const salt = randHex(16);
    rec = {
      name: user,
      salt: salt,
      hash: await pbkdf2(pass, salt),
      role: isSuper(user) ? 'super' : 'user',
      theme: 'trippah',
      at: now
    };
    db.users[key] = rec;
    created = true;
  } else {
    const hash = await pbkdf2(pass, rec.salt);
    if (hash !== rec.hash) return { ok: false, error: 'That username is taken. Wrong password.', code: 'taken' };
    if (isSuper(rec.name)) rec.role = 'super';
    if (!rec.theme) rec.theme = 'trippah';
  }
  // Refresh super flag if the hardcoded list changed
  if (isSuper(rec.name)) rec.role = 'super';
  else if (rec.role === 'super' && !isSuper(rec.name)) rec.role = 'user';
  const token = randHex(24);
  const themeId = String(rec.theme || 'trippah').toLowerCase();
  db.sessions[token] = { user: rec.name, role: rec.role || 'user', theme: themeId, exp: now + SESSION_MS };
  await writeAccounts(env, db);
  // Login is also a presence heartbeat so the API's live-user count is immediately accurate.
  const online = await touchOnline(env, rec.name, themeId);
  const themePack = resolveTheme(themeId);
  // Shape matches DreamShare (token + role + user + theme) and the web UI
  return {
    ok: true,
    user: rec.name,
    role: rec.role || 'user',
    token: token,
    theme: themePack.id,
    themePack: themePack,
    themes: THEMES.map(function (t) { return { id: t.id, name: t.name, tag: t.tag, effects: t.effects || '' }; }),
    created: created,
    accounts: 'unique-v1',
    clients: ['web', 'vst'],
    storage: STORAGE,
    themeApi: THEME_API_VERSION,
    vstPatch: VST_PATCH_VERSION,
    online: online,
            onlineUsers: await readPresence(env),
    onlineCount: online.length
  };
}

async function sessionFromBody(env, body, request) {
  let token = String((body && body.token) || '');
  // VST / web audio uploads also send X-SW-Token; accept it for all actions
  if ((!token || !/^[a-f0-9]{32,80}$/.test(token)) && request) {
    try {
      const hdr = request.headers.get('x-sw-token') || request.headers.get('authorization') || '';
      const m = String(hdr).match(/(?:Bearer\s+)?([a-f0-9]{32,80})/i);
      if (m) token = m[1];
    } catch (_) {}
  }
  if (!/^[a-f0-9]{32,80}$/.test(token)) return null;
  const db = await readAccounts(env);
  const s = db.sessions && db.sessions[token];
  if (!s || (s.exp || 0) < Date.now()) return null;
  const user = normUser(s.user);
  if (!user) return null;
  let theme = String(s.theme || '').toLowerCase();
  try {
    const rec = db.users && db.users[user.toLowerCase()];
    if (rec && rec.theme) theme = String(rec.theme).toLowerCase();
  } catch (_) {}
  if (!theme) theme = 'trippah';
  return {
    user: user,
    role: (s.role === 'super' || isSuper(user)) ? 'super' : 'user',
    token: token,
    theme: theme
  };
}


function cleanPresetMachine(x) {
  return String(x || '').replace(/[^A-Za-z0-9_-]/g, '').slice(0, PRESET_MACHINE_MAX);
}
function cleanPresetName(x) {
  return String(x || '').replace(/[\u0000-\u001f]/g, '').trim().slice(0, PRESET_NAME_MAX);
}
function ensurePresets(rec) {
  if (!rec || typeof rec !== 'object') return [];
  if (!Array.isArray(rec.presets)) rec.presets = [];
  return rec.presets;
}
function publicPresets(rec, machine) {
  const m = cleanPresetMachine(machine);
  return ensurePresets(rec)
    .filter(function (p) { return p && p.machine === m && p.name; })
    .sort(function (a, b) { return (b.updated || b.created || 0) - (a.updated || a.created || 0); })
    .map(function (p) {
      return { name: p.name, machine: p.machine, state: p.state, created: p.created || 0, updated: p.updated || 0 };
    });
}

function cleanChunkIds(list) {
  if (!Array.isArray(list)) return [];
  return list.filter(function (id) {
    return /^[A-Za-z0-9]+$/.test(String(id || '')) && String(id) !== FEED_BIN_ID;
  }).slice(0, 200);
}

function postToThread(p) {
  if (!p || p.id === META_ID) return null;
  const text = String(p.text || '');
  const title = String(p.title || text.split('\n')[0] || '').slice(0, 120) || (p.hasAudio ? 'audio' : 'untitled');
  const comments = Array.isArray(p.comments) ? p.comments : [];
  const chunks = cleanChunkIds(p.audioChunks);
  return {
    id: String(p.id || ('t' + (p.at || Date.now()))),
    user: String(p.user || 'anon').slice(0, 24),
    title: title,
    text: text.slice(0, 1000),
    at: p.at || 0,
    hasAudio: !!(p.hasAudio || p.audioData || p.audioUrl || chunks.length || vaultOf(p).audioParts),
    audioId: p.audioId || null,
    audioUrl: (typeof p.audioUrl === 'string' && p.audioUrl.indexOf('http') === 0) ? p.audioUrl : null,
    audioData: (typeof p.audioData === 'string' && p.audioData.indexOf('data:audio') === 0) ? p.audioData : null,
    audioChunks: chunks,
    audioExpires: p.audioExpires || 0,
    audioMime: p.audioMime || 'audio/wav',
    audioStore: vaultOf(p).audioStore,
    audioUpload: vaultOf(p).audioUpload,
    audioParts: vaultOf(p).audioParts,
    audioBytes: vaultOf(p).audioBytes,
    comments: comments.filter(function (c) { return c && c.id && c.id !== META_ID; }).map(function (c) {
      return { id: c.id, user: String(c.user || '').slice(0, 24), text: String(c.text || '').slice(0, 500), at: c.at || 0, reactions: c.reactions || {} };
    }),
    reactions: (p.reactions && typeof p.reactions === 'object') ? p.reactions : {}
  };
}

function threadToPost(t) {
  const chunks = cleanChunkIds(t.audioChunks);
  return {
    id: t.id,
    user: t.user,
    title: t.title || '',
    text: t.text || '',
    at: t.at || 0,
    hasAudio: !!(t.hasAudio || t.audioUrl || t.audioData || chunks.length),
    audioId: t.audioId || null,
    audioUrl: t.audioUrl || null,
    audioChunks: chunks,
    audioExpires: t.audioExpires || 0,
    audioMime: t.audioMime || 'audio/wav',
    audioStore: vaultOf(t).audioStore,
    audioUpload: vaultOf(t).audioUpload,
    audioParts: vaultOf(t).audioParts,
    audioBytes: vaultOf(t).audioBytes,
    comments: (t.comments || []).map(function (c) { return Object.assign({}, c, { theme: c && c.theme ? c.theme : (t.theme || 'trippah') }); }),
    reactions: (t.reactions && typeof t.reactions === 'object') ? t.reactions : {},
    theme: t.theme || 'trippah'
  };
}


function mergeReactions(a, b) {
  const out = {};
  function add(src) {
    if (!src || typeof src !== 'object') return;
    Object.keys(src).forEach(function (k) {
      const key = String(k || '').replace(/[^a-z0-9]/g, '').slice(0, 12);
      if (!key) return;
      const list = Array.isArray(src[k]) ? src[k] : [];
      if (!out[key]) out[key] = [];
      list.forEach(function (u) {
        const name = String(u || '').slice(0, 24);
        if (!name) return;
        if (!out[key].some(function (x) { return String(x).toLowerCase() === name.toLowerCase(); })) out[key].push(name);
      });
      if (!out[key].length) delete out[key];
    });
  }
  add(a); add(b);
  return out;
}
function mergeComment(older, newer) {
  const a = older || {};
  const b = newer || {};
  const pick = (b.at || 0) >= (a.at || 0) ? b : a;
  const other = pick === b ? a : b;
  return {
    id: pick.id || other.id,
    user: pick.user || other.user || '',
    text: (pick.text != null && pick.text !== '') ? pick.text : (other.text || ''),
    at: Math.max(pick.at || 0, other.at || 0),
    reactions: mergeReactions(a.reactions, b.reactions)
  };
}

function mergeThreads(a, b) {
  const map = new Map();
  function take(t) {
    if (!t || !t.id || t.id === META_ID) return;
    const prev = map.get(t.id);
    if (!prev) {
      map.set(t.id, {
        id: t.id,
        user: t.user || 'anon',
        title: t.title || '',
        text: t.text || '',
        at: t.at || 0,
        hasAudio: !!t.hasAudio,
        audioId: t.audioId || null,
        audioUrl: t.audioUrl || null,
        audioData: t.audioData || null,
        audioChunks: cleanChunkIds(t.audioChunks),
        audioExpires: t.audioExpires || 0,
        audioMime: t.audioMime || 'audio/wav',
        audioStore: vaultOf(t).audioStore,
        audioUpload: vaultOf(t).audioUpload,
        audioParts: vaultOf(t).audioParts,
        audioBytes: vaultOf(t).audioBytes,
        comments: Array.isArray(t.comments) ? t.comments.slice() : [],
        reactions: (t.reactions && typeof t.reactions === 'object') ? t.reactions : {}
      });
      return;
    }
    const newer = (t.at || 0) >= (prev.at || 0) ? t : prev;
    const older = newer === t ? prev : t;
    const seen = {};
    const comments = [];
    (older.comments || []).concat(newer.comments || []).forEach(function (c) {
      if (!c || !c.id) return;
      if (seen[c.id]) {
        const i = comments.findIndex(function (x) { return x.id === c.id; });
        if (i >= 0) comments[i] = mergeComment(comments[i], c);
        return;
      }
      seen[c.id] = 1;
      comments.push(c);
    });
    const chunks = cleanChunkIds(newer.audioChunks).length ? cleanChunkIds(newer.audioChunks) : cleanChunkIds(older.audioChunks);
    const vault = pickVault(newer, older);
    map.set(t.id, {
      id: t.id,
      user: newer.user || older.user,
      title: newer.title || older.title || '',
      text: (newer.text != null && newer.text !== '') ? newer.text : (older.text || ''),
      at: Math.max(newer.at || 0, older.at || 0),
      hasAudio: !!(newer.hasAudio || older.hasAudio || newer.audioData || older.audioData || newer.audioUrl || older.audioUrl || chunks.length || vault.audioParts),
      audioId: newer.audioId || older.audioId || null,
      audioUrl: newer.audioUrl || older.audioUrl || null,
      audioData: newer.audioData || older.audioData || null,
      audioChunks: chunks,
      audioExpires: Math.max(newer.audioExpires || 0, older.audioExpires || 0),
      audioMime: newer.audioMime || older.audioMime || 'audio/wav',
      audioStore: vault.audioStore,
      audioUpload: vault.audioUpload,
      audioParts: vault.audioParts,
      audioBytes: vault.audioBytes,
      comments: comments,
      reactions: mergeReactions(newer.reactions, older.reactions)
    });
  }
  (a || []).forEach(take);
  (b || []).forEach(take);
  return Array.from(map.values()).sort(function (x, y) { return (y.at || 0) - (x.at || 0); });
}

function feedFromRaw(raw) {
  if (!raw || typeof raw !== 'object') return { threads: [], mods: [], chat: [], updated: 0 };
  if (typeof raw === 'string') {
    try { raw = JSON.parse(raw); } catch (_) { return { threads: [], mods: [], chat: [], updated: 0 }; }
  }
  const fromThreads = (raw.threads || []).map(postToThread).filter(Boolean);
  const fromPosts = (raw.posts || []).map(postToThread).filter(Boolean);
  let mods = Array.isArray(raw.mods) ? raw.mods.slice() : [];
  (raw.posts || []).forEach(function (p) {
    if (p && p.id === META_ID && Array.isArray(p.mods)) mods = mods.concat(p.mods);
  });
  const seen = {};
  mods = mods.filter(function (m) {
    const k = String(m || '').toLowerCase();
    if (!k || seen[k]) return false;
    seen[k] = 1;
    return true;
  }).slice(0, 80);
  const chat = Array.isArray(raw.chat) ? raw.chat.filter(function (m) {
    return m && m.id && m.user && m.text;
  }).slice(-100) : [];
  const roles = (raw.roles && typeof raw.roles === 'object') ? raw.roles : {};
  const customRoles = cleanCustomRoles(raw.customRoles);
  return { threads: mergeThreads(fromPosts, fromThreads), mods: mods, chat: chat, roles: roles, customRoles: customRoles, updated: raw.updated || 0 };
}

function mergeFeeds(a, b) {
  const A = feedFromRaw(a);
  const B = feedFromRaw(b);
  const seen = {};
  const mods = (A.mods || []).concat(B.mods || []).filter(function (m) {
    const k = String(m || '').toLowerCase();
    if (!k || seen[k]) return false;
    seen[k] = 1;
    return true;
  });
  // Prefer the longer / newer chat ring
  let chat = (A.chat && A.chat.length >= (B.chat || []).length) ? A.chat : (B.chat || A.chat || []);
  if ((B.updated || 0) > (A.updated || 0) && (B.chat || []).length) chat = B.chat;
  chat = (chat || []).slice(-100);
  const roles = Object.assign({}, A.roles || {}, B.roles || {});
  const customRoles = mergeCustomRoles(A.customRoles, B.customRoles);
  return {
    threads: mergeThreads(A.threads, B.threads),
    mods: mods,
    chat: chat,
    roles: roles,
    customRoles: customRoles,
    updated: Math.max(A.updated || 0, B.updated || 0)
  };
}

async function readBin() {
  const r = await fetch(BIN + '?t=' + Date.now(), {
    headers: { Accept: 'application/json', 'Cache-Control': 'no-cache' }
  });
  if (!r.ok) throw new Error('bin read ' + r.status);
  const text = await r.text();
  let j = JSON.parse(text);
  if (typeof j === 'string') j = JSON.parse(j);
  if (!j || typeof j !== 'object') throw new Error('bin empty');
  return j;
}

async function readKv(env) {
  try {
    if (env && env.DREAMSHARE_KV) return await env.DREAMSHARE_KV.get('feed-v4', 'json');
  } catch (_) {}
  return null;
}

async function readFeed(env) {
  const bin = await readBin();
  const kv = await readKv(env);
  return mergeFeeds(bin, kv);
}

function binPayload(feed) {
  const threads = (feed.threads || []).filter(function (t) { return t && t.id && t.id !== META_ID; });
  const posts = threads.map(threadToPost);
  posts.push({
    id: META_ID,
    user: 'system',
    title: '',
    text: '',
    at: 0,
    hasAudio: false,
    audioId: null,
    mods: feed.mods || [],
    hidden: true
  });
  const slim = {
    posts: posts,
    threads: threads.map(function (t) {
      return {
        id: t.id, user: t.user, title: t.title || '', text: t.text || '', at: t.at || 0,
        hasAudio: !!(t.hasAudio || t.audioUrl || (t.audioChunks && t.audioChunks.length) || vaultOf(t).audioParts),
        audioId: t.audioId || null,
        audioUrl: t.audioUrl || null,
        audioChunks: cleanChunkIds(t.audioChunks),
        audioExpires: t.audioExpires || 0,
        audioMime: t.audioMime || 'audio/wav',
        audioStore: vaultOf(t).audioStore,
        audioUpload: vaultOf(t).audioUpload,
        audioParts: vaultOf(t).audioParts,
        audioBytes: vaultOf(t).audioBytes,
        comments: (t.comments || []).slice(-40),
        reactions: (t.reactions && typeof t.reactions === 'object') ? t.reactions : {}
      };
    }),
    mods: feed.mods || [],
    roles: feed.roles || {},
    customRoles: cleanCustomRoles(feed.customRoles),
    chat: Array.isArray(feed.chat) ? feed.chat.slice(-100) : [],
    updated: Date.now(),
    storage: STORAGE
  };
  let body = JSON.stringify(slim);
  while (body.length > MAX_BIN && slim.threads.length > 1) {
    const drop = slim.threads[slim.threads.length - 1];
    slim.threads.pop();
    slim.posts = slim.posts.filter(function (p) { return p.id !== drop.id; });
    body = JSON.stringify(slim);
  }
  return body;
}

async function writeBin(feed) {
  const before = await readBin();
  const beforeCount = ((before && before.posts) || []).filter(function (p) { return p && p.id !== META_ID; }).length;
  const nextCount = (feed.threads || []).filter(function (t) { return t && t.id !== META_ID; }).length;
  if (beforeCount > 0 && nextCount === 0) throw new Error('refusing to wipe threads');
  const body = binPayload(feed);
  const r = await fetch(BIN, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json', Accept: 'application/json' },
    body: body
  });
  if (!r.ok) throw new Error('bin write ' + r.status);
  return true;
}

async function writeKv(env, feed) {
  if (!env || !env.DREAMSHARE_KV) return false;
  const copy = {
    threads: (feed.threads || []).map(function (t) {
      const audio = (typeof t.audioData === 'string' && t.audioData.indexOf('data:audio') === 0)
        ? t.audioData.slice(0, 1500000) : null;
      return {
        id: t.id, user: t.user, title: t.title || '', text: t.text || '', at: t.at || 0,
        hasAudio: !!(t.hasAudio || audio || t.audioUrl || vaultOf(t).audioParts), audioId: t.audioId || null,
        audioUrl: t.audioUrl || null, audioData: audio,
        audioStore: vaultOf(t).audioStore,
        audioUpload: vaultOf(t).audioUpload,
        audioParts: vaultOf(t).audioParts,
        audioBytes: vaultOf(t).audioBytes,
        comments: t.comments || []
      };
    }),
    mods: feed.mods || [],
    roles: feed.roles || {},
    customRoles: cleanCustomRoles(feed.customRoles),
    chat: Array.isArray(feed.chat) ? feed.chat.slice(-100) : [],
    updated: Date.now(),
    storage: STORAGE
  };
  let body = JSON.stringify(copy);
  while (body.length > 20000000 && copy.threads.length) {
    for (let i = copy.threads.length - 1; i >= 0; i--) {
      if (copy.threads[i].audioData) { copy.threads[i].audioData = null; break; }
    }
    body = JSON.stringify(copy);
    if (!copy.threads.some(function (t) { return t.audioData; })) break;
  }
  await env.DREAMSHARE_KV.put('feed-v4', body);
  return true;
}


const ONLINE_TTL = 55000;
function presenceAt(v) { return (v && typeof v === 'object') ? Number(v.at || 0) : Number(v || 0); }
function presenceTheme(v) { return (v && typeof v === 'object' && v.theme) ? String(v.theme) : 'trippah'; }
async function readPresence(env) {
  const now = Date.now();
  let map = {};
  try {
    if (env && env.DREAMSHARE_KV) {
      const raw = await env.DREAMSHARE_KV.get('presence-v1');
      if (raw) map = JSON.parse(raw);
    }
  } catch (_) {}
  return Object.keys(map || {}).filter(function (k) { return now - presenceAt(map[k]) < ONLINE_TTL; }).map(function (k) {
    return { name: k, theme: presenceTheme(map[k]), online: true };
  });
}
async function readOnline(env) {
  const people = await readPresence(env);
  return people.map(function (p) { return p.name; });
}
async function touchOnline(env, user, theme) {
  const now = Date.now();
  let map = {};
  try {
    if (env && env.DREAMSHARE_KV) {
      const raw = await env.DREAMSHARE_KV.get('presence-v1');
      if (raw) map = JSON.parse(raw);
    }
  } catch (_) { map = {}; }
  map[user] = { at: now, theme: String(theme || 'trippah').toLowerCase() };
  Object.keys(map).forEach(function (k) { if (now - presenceAt(map[k]) > ONLINE_TTL) delete map[k]; });
  try {
    if (env && env.DREAMSHARE_KV) await env.DREAMSHARE_KV.put('presence-v1', JSON.stringify(map));
  } catch (_) {}
  return Object.keys(map);
}

async function writeFeed(env, feed) {
  await writeBin(feed);
  try { await writeKv(env, feed); } catch (_) {}
  return true;
}

async function deleteChunk(id) {
  if (!id || !/^[A-Za-z0-9]+$/.test(String(id)) || String(id) === FEED_BIN_ID) return;
  try { await fetch(BIN_API + '/' + id, { method: 'DELETE' }); } catch (_) {}
}

async function storeAudioChunks(dataUrl) {
  if (typeof dataUrl !== 'string' || dataUrl.indexOf('data:audio') !== 0) return null;
  const comma = dataUrl.indexOf(',');
  if (comma < 0) return null;
  const meta = dataUrl.slice(0, comma);
  const b64 = dataUrl.slice(comma + 1).replace(/\s/g, '');
  if (!b64 || b64.length > 12000000) return null;
  const mime = (meta.match(/^data:([^;]+)/) || [])[1] || 'audio/wav';
  const n = Math.ceil(b64.length / CHUNK_B64);
  if (n > 200) return null;
  const ids = [];
  try {
    for (let i = 0; i < n; i++) {
      const r = await fetch(BIN_API, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', Accept: 'application/json' },
        body: JSON.stringify({ i: i, n: n, b: b64.slice(i * CHUNK_B64, (i + 1) * CHUNK_B64) })
      });
      if (!r.ok) throw new Error('audio store ' + r.status);
      const j = await r.json();
      if (!j || !j.id) throw new Error('audio store missing id');
      ids.push(j.id);
    }
  } catch (err) {
    for (let i = 0; i < ids.length; i++) await deleteChunk(ids[i]);
    throw err;
  }
  return { chunks: ids, mime: mime, expires: Date.now() + AUDIO_KEEP_MS };
}

async function readChunkB64(ids) {
  let b64 = '';
  for (let i = 0; i < ids.length; i++) {
    const r = await fetch(BIN_API + '/' + ids[i] + '?t=' + Date.now(), {
      headers: { Accept: 'application/json', 'Cache-Control': 'no-cache' }
    });
    if (!r.ok) throw new Error('audio read ' + r.status);
    let j = await r.json();
    if (typeof j === 'string') j = JSON.parse(j);
    b64 += (j && j.b) || '';
  }
  return b64;
}

function b64ToBytes(b64) {
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return bytes;
}

async function purgeExpired(env, feed) {
  const now = Date.now();
  let changed = false;
  const doomed = [];
  (feed.threads || []).forEach(function (t) {
    if (!t || !t.audioExpires || now <= t.audioExpires) return;
    const has = (t.audioChunks && t.audioChunks.length) || t.audioData || t.audioUrl || t.hasAudio;
    if (!has) return;
    if (t.audioChunks) doomed.push.apply(doomed, t.audioChunks);
    const vaultGone = vaultOf(t);
    t.audioChunks = [];
    t.audioData = null;
    t.audioUrl = null;
    t.audioStore = '';
    t.audioUpload = '';
    t.audioParts = 0;
    t.audioBytes = 0;
    t.hasAudio = false;
    changed = true;
    t._vaultGone = vaultGone;
  });
  for (let i = 0; i < doomed.length && i < 30; i++) await deleteChunk(doomed[i]);
  for (let i = 0; i < (feed.threads || []).length; i++) {
    const t = feed.threads[i];
    if (t && t._vaultGone) {
      await deleteVault(env, t._vaultGone);
      delete t._vaultGone;
    }
  }
  return changed;
}

async function loadFeed(env) {
  const feed = await readFeed(env);
  if (await purgeExpired(env, feed)) {
    try { await writeBin(feed); } catch (_) {}
  }
  return feed;
}

function publicThread(t, origin) {
  const chunks = cleanChunkIds(t.audioChunks);
  const vault = vaultOf(t);
  const alive = !t.audioExpires || Date.now() <= t.audioExpires;
  const playUrl = (alive && chunks.length && origin) ? (origin + '/?wav=' + encodeURIComponent(t.id)) : (t.audioUrl || null);
  const has = !!(alive && (t.hasAudio || chunks.length || playUrl || vault.audioParts));
  return {
    id: t.id,
    user: t.user,
    title: t.title || '',
    text: t.text || '',
    at: t.at || 0,
    hasAudio: has,
    audioId: t.audioId || null,
    audioUrl: alive ? (playUrl || (vault.audioParts ? (origin + '/?wav=' + encodeURIComponent(t.id)) : null)) : null,
    audioChunks: alive ? chunks : [],
    audioExpires: t.audioExpires || 0,
    audioMime: t.audioMime || 'audio/wav',
    audioStore: alive ? vault.audioStore : '',
    audioParts: alive ? vault.audioParts : 0,
    audioBytes: alive ? vault.audioBytes : 0,
    audioKeepDays: 100,
    comments: (t.comments || []).map(function (c) {
      return { id: c.id, user: c.user, text: c.text, at: c.at || 0, reactions: c.reactions || {} };
    }),
    reactions: t.reactions || {}
  };
}

function vaultOf(t) {
  t = t || {};
  const store = (t.audioStore === 'r2' || t.audioStore === 'kv') ? t.audioStore : '';
  const upload = String(t.audioUpload || '').replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 48);
  const parts = Math.max(0, Math.min(20, parseInt(t.audioParts, 10) || 0));
  const bytes = Math.max(0, Math.min(MAX_AUDIO_BYTES, parseInt(t.audioBytes, 10) || 0));
  if (!store || !upload || !parts) return { audioStore: '', audioUpload: '', audioParts: 0, audioBytes: 0 };
  return { audioStore: store, audioUpload: upload, audioParts: parts, audioBytes: bytes };
}

function pickVault(newer, older) {
  const a = vaultOf(newer);
  if (a.audioStore) return a;
  return vaultOf(older);
}

function audioSink(env) {
  if (env && env.DREAMSHARE_R2) return 'r2';
  if (env && env.DREAMSHARE_KV) return 'kv';
  return 'bin';
}

async function deleteVault(env, t) {
  const v = vaultOf(t);
  if (!v.audioStore) return;
  for (let i = 0; i < v.audioParts; i++) {
    const key = v.audioStore === 'r2' ? ('wav/' + v.audioUpload + '/' + i) : ('wav:' + v.audioUpload + ':' + i);
    try {
      if (v.audioStore === 'r2' && env && env.DREAMSHARE_R2) await env.DREAMSHARE_R2.delete(key);
      if (v.audioStore === 'kv' && env && env.DREAMSHARE_KV) await env.DREAMSHARE_KV.delete(key);
    } catch (_) {}
  }
}

async function handleAudioPart(request, env) {
  const token = String(request.headers.get('x-sw-token') || '');
  const upload = String(request.headers.get('x-sw-upload') || '').replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 48);
  const index = parseInt(request.headers.get('x-sw-index') || '', 10);
  const parts = parseInt(request.headers.get('x-sw-parts') || '', 10);
  const len = parseInt(request.headers.get('content-length') || '0', 10);
  if (!/^[A-Za-z0-9][A-Za-z0-9_\-]{3,47}$/.test(upload)) return json({ ok: false, error: 'bad upload' }, 400);
  if (!Number.isFinite(index) || index < 0 || index > 19) return json({ ok: false, error: 'bad part' }, 400);
  if (!Number.isFinite(parts) || parts < 1 || parts > 20 || index >= parts) return json({ ok: false, error: 'bad part count' }, 400);
  if (!len || len > MAX_PART_BYTES) return json({ ok: false, error: 'Each piece must be 4 MB or smaller' }, 413);
  let sess = null;
  try { sess = await sessionFromBody(env, { token: token }, request); }
  catch (err) { return json({ ok: false, error: 'account check failed' }, 502); }
  if (!sess) return json({ ok: false, error: 'Login required', code: 'auth' }, 401);
  const sink = audioSink(env);
  if (sink === 'bin') {
    return json({
      ok: false,
      error: 'Bind DREAMSHARE_R2 (recommended) or DREAMSHARE_KV on this worker. The public JSON bin cannot hold a 75 MB tape.',
      code: 'no-sink'
    }, 400);
  }
  const key = sink === 'r2' ? ('wav/' + upload + '/' + index) : ('wav:' + upload + ':' + index);
  try {
    if (sink === 'r2') {
      await env.DREAMSHARE_R2.put(key, request.body, { httpMetadata: { contentType: 'audio/wav' } });
    } else {
      await env.DREAMSHARE_KV.put(key, request.body);
    }
  } catch (err) {
    return json({ ok: false, error: 'tape store failed: ' + String(err && err.message || err) }, 502);
  }
  return json({ ok: true, sink: sink, upload: upload, index: index, parts: parts, audioMaxBytes: MAX_AUDIO_BYTES });
}
async function handleAudioPartB64(env, body, user) {
  const upload = String(body.upload || '').replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 48);
  const index = parseInt(body.index, 10), parts = parseInt(body.parts, 10);
  const b64 = String(body.b64 || '').replace(/\s/g, '');
  if (!/^[A-Za-z0-9][A-Za-z0-9_\-]{3,47}$/.test(upload)) return { ok:false, error:'bad upload' };
  if (!Number.isFinite(index) || index < 0 || index > 49 || !Number.isFinite(parts) || parts < 1 || parts > 50 || index >= parts)
    return { ok:false, error:'bad part' };
  if (!b64 || b64.length > 3000000) return { ok:false, error:'audio piece too large' };
  const sink = audioSink(env);
  if (sink === 'bin') return { ok:false, error:'Bind DREAMSHARE_R2 or DREAMSHARE_KV for WAV sharing', code:'no-sink' };
  let bytes;
  try { bytes = b64ToBytes(b64); } catch (_) { return { ok:false, error:'bad audio encoding' }; }
  if (bytes.byteLength > 2200000) return { ok:false, error:'audio piece too large' };
  const key = sink === 'r2' ? ('wav/' + upload + '/' + index) : ('wav:' + upload + ':' + index);
  try {
    if (sink === 'r2') await env.DREAMSHARE_R2.put(key, bytes, { httpMetadata:{ contentType:'audio/wav' } });
    else await env.DREAMSHARE_KV.put(key, bytes);
  } catch (err) {
    return { ok:false, error:'tape store failed: ' + String(err && err.message || err) };
  }
  return { ok:true, sink:sink, upload:upload, index:index, parts:parts, audioMaxBytes:MAX_AUDIO_BYTES };
}

// ---- Kyoto Discord chat (DreamShare Chat tab) ----
// Single fixed channel only — Kyoto server #general.
// https://discord.com/channels/1518252339864014929/1518252340707197000
// Secrets stay on the worker; the VST never holds DISCORD_BOT_TOKEN.
const KYOTO_DISCORD_GUILD = '1518252339864014929';
const KYOTO_DISCORD_CHANNEL = '1518252340707197000';

function formatDreamShareDiscordContent(username, text) {
  const user = String(username || 'DreamShare').replace(/[\r\n]/g, ' ').trim().slice(0, 32) || 'DreamShare';
  const body = String(text || '').trim().slice(0, 1800);
  // Discord content limit 2000; leave room for attribution.
  return user + ': ' + body + '\nSent from DreamShare';
}

function parseDreamShareDiscordMessage(m) {
  const raw = String((m && m.content) || '');
  const fromDs = /Sent from DreamShare\s*$/i.test(raw);
  let user = (m.author && (m.author.global_name || m.author.username)) || 'discord';
  let text = raw;
  if (fromDs) {
    const withoutFooter = raw.replace(/\n?Sent from DreamShare\s*$/i, '');
    const colon = withoutFooter.indexOf(':');
    if (colon > 0 && colon < 40) {
      user = withoutFooter.slice(0, colon).trim() || user;
      text = withoutFooter.slice(colon + 1).trim();
    } else {
      text = withoutFooter.trim();
    }
  }
  return {
    id: m.id,
    user: user,
    text: text,
    at: m.timestamp || '',
    fromDreamShare: fromDs
  };
}

async function discordLite(env, action, body) {
  const token = env && env.DISCORD_BOT_TOKEN;
  if (!token) return { ok: false, status: 503, error: 'Set worker secret DISCORD_BOT_TOKEN. The VST never holds the bot token.' };
  const call = async (path, method, payload) => {
    const res = await fetch('https://discord.com/api/v10' + path, {
      method: method || 'GET',
      headers: {
        Authorization: 'Bot ' + token,
        'Content-Type': 'application/json',
        'User-Agent': 'DreamShareLite (https://dreamdaw.com, 0.4.1)'
      },
      body: payload ? JSON.stringify(payload) : undefined
    });
    const text = await res.text();
    let data = null;
    try { data = text ? JSON.parse(text) : null; } catch (e) { data = { raw: text }; }
    if (!res.ok) {
      const msg = (data && (data.message || data.error)) || ('Discord ' + res.status);
      return { ok: false, status: res.status, error: msg };
    }
    return { ok: true, data: data };
  };

  // Always Kyoto #general — no multi-channel browsing.
  const guild = (env && env.DISCORD_GUILD_ID) || KYOTO_DISCORD_GUILD;
  const channel = KYOTO_DISCORD_CHANNEL;

  if (action === 'discord_status') {
    const me = await call('/users/@me', 'GET');
    if (!me.ok) return me;
    return {
      ok: true,
      bot: { id: me.data.id, name: me.data.username },
      postsAs: 'bot',
      guild: guild,
      channel: channel,
      channelName: 'general'
    };
  }

  if (action === 'discord_channels') {
    // Single fixed channel — Chat tab does not list the whole server.
    return {
      ok: true,
      guild: guild,
      channels: [{ id: channel, name: 'general', parent: '' }]
    };
  }

  if (action === 'discord_messages') {
    const msgs = await call('/channels/' + channel + '/messages?limit=50', 'GET');
    if (!msgs.ok) return msgs;
    const messages = (msgs.data || []).slice().reverse().map(parseDreamShareDiscordMessage);
    return { ok: true, channel: channel, channelName: 'general', guild: guild, messages: messages };
  }

  if (action === 'discord_send') {
    const dreamUser = String(body.user || body.from || body.username || '').trim();
    const text = String(body.text || body.content || '').trim();
    if (!text) return { ok: false, status: 400, error: 'empty message' };
    const content = formatDreamShareDiscordContent(dreamUser, text);
    const sent = await call('/channels/' + channel + '/messages', 'POST', { content: content });
    if (!sent.ok) return sent;
    return {
      ok: true,
      id: sent.data && sent.data.id,
      postsAs: 'bot',
      channel: channel,
      attributedTo: dreamUser || 'DreamShare'
    };
  }

  if (action === 'discord_react') {
    const message = String(body.message || body.id || '');
    const emoji = encodeURIComponent(String(body.emoji || body.key || '👀'));
    if (!message) return { ok: false, status: 400, error: 'message required' };
    const reacted = await call('/channels/' + channel + '/messages/' + message + '/reactions/' + emoji + '/@me', 'PUT');
    if (!reacted.ok) return reacted;
    return { ok: true };
  }

  return { ok: false, status: 400, error: 'unknown discord action' };
}

/** Best-effort mirror of a DreamShare chat line into Kyoto #general. */
async function mirrorChatToKyotoDiscord(env, username, text) {
  try {
    if (!env || !env.DISCORD_BOT_TOKEN) return;
    const content = formatDreamShareDiscordContent(username, text);
    await fetch('https://discord.com/api/v10/channels/' + KYOTO_DISCORD_CHANNEL + '/messages', {
      method: 'POST',
      headers: {
        Authorization: 'Bot ' + env.DISCORD_BOT_TOKEN,
        'Content-Type': 'application/json',
        'User-Agent': 'DreamShareLite (https://dreamdaw.com, 0.4.1)'
      },
      body: JSON.stringify({ content: content })
    });
  } catch (e) { /* non-fatal */ }
}


function runKyotoModule(action, body, sess, env) {
  return handleKyotoModule(action, body, sess, env, {
    STORAGE,
    KYOTO_API_VERSION,
    KYOTO_MODULE_FORMAT,
    isSuper,
    communityIndex
  });
}

export default {
  async fetch(request, env) {
    const method = (request.method || 'GET').toUpperCase();
    const url = new URL(request.url);

    if (method === 'OPTIONS') {
      return new Response(null, { status: 204, headers: CORS });
    }

    if ((method === 'POST' || method === 'PUT') && url.searchParams.get('op') === 'audio_part') {
      return handleAudioPart(request, env);
    }

    // ---- VST release download (web UI "Download VST" button) ----
    if ((method === 'GET' || method === 'HEAD') && (url.searchParams.get('download') === 'vst' || url.pathname.endsWith('/vst') || url.searchParams.get('op') === 'vst_download')) {
      const meta = {
        ok: true,
        storage: STORAGE,
        vst: {
          version: VST_RELEASE.version,
          name: VST_RELEASE.name,
          label: VST_RELEASE.label,
          r2: !!(env && env.DREAMSHARE_R2),
          url: null
        }
      };
      // Prefer R2 object if bound
      if (env && env.DREAMSHARE_R2) {
        try {
          const obj = await env.DREAMSHARE_R2.get(VST_RELEASE.r2Key);
          if (obj) {
            if (method === 'HEAD') {
              return new Response(null, {
                status: 200,
                headers: Object.assign({}, CORS, {
                  'Content-Type': 'application/zip',
                  'Content-Disposition': 'attachment; filename="' + VST_RELEASE.name + '"',
                  'X-VST-Version': VST_RELEASE.version
                })
              });
            }
            return new Response(obj.body, {
              status: 200,
              headers: Object.assign({}, CORS, {
                'Content-Type': 'application/zip',
                'Content-Disposition': 'attachment; filename="' + VST_RELEASE.name + '"',
                'Cache-Control': 'public, max-age=300',
                'X-VST-Version': VST_RELEASE.version
              })
            });
          }
        } catch (err) {
          return json({ ok: false, error: 'vst store read failed', detail: String(err && err.message || err) }, 502);
        }
      }
      if (VST_RELEASE.fallbackUrl) {
        return Response.redirect(VST_RELEASE.fallbackUrl, 302);
      }
      // Metadata only — client can show "not uploaded yet"
      if (url.searchParams.get('meta') === '1' || method === 'HEAD') {
        return json(meta, 200);
      }
      return json({
        ok: false,
        error: 'VST package not uploaded yet. Bind DREAMSHARE_R2 and put the zip at ' + VST_RELEASE.r2Key,
        code: 'vst-missing',
        vst: meta.vst
      }, 404);
    }

    if (method === 'GET' || method === 'HEAD') {
      const community = String(url.searchParams.get('community') || '');
      if (community === 'instruments') {
        try { return json({ ok:true, community:'instruments', instruments:await communityList(env), storage: env && env.DREAMSHARE_KV ? 'kv' : 'unavailable' }); }
        catch (err) { return json({ok:false,error:String(err && err.message || err)},502); }
      }
      if (community) {
        try { const item=await communityGet(env,community); return item ? json(Object.assign({ok:true},item)) : json({ok:false,error:'community instrument not found'},404); }
        catch (err) { return json({ok:false,error:String(err && err.message || err)},502); }
      }

      const dmWavId = String(url.searchParams.get('dmwav') || '').replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 64);
      if (dmWavId) {
        try {
          const token = String(url.searchParams.get('token') || '');
          const sess = await sessionFromBody(env, { token: token }, request);
          if (!sess) return json({ ok:false, error:'Login required', code:'auth' }, 401);
          const db = await readAccounts(env);
          const me = socialUser(db, sess.user);
          const msg = me.dms.find(m => m && m.id === dmWavId && m.audioUpload);
          if (!msg) return json({ ok:false, error:'private wav missing' }, 404);
          const other = String(msg.from).toLowerCase() === sess.user.toLowerCase() ? msg.to : msg.from;
          if (String(msg.from).toLowerCase() !== sess.user.toLowerCase() && String(msg.to).toLowerCase() !== sess.user.toLowerCase())
            return json({ ok:false, error:'not allowed' }, 403);
          const sink = msg.audioStore || audioSink(env);
          const part = parseInt(url.searchParams.get('part') || '0', 10);
          if (!Number.isFinite(part) || part < 0 || part >= (parseInt(msg.audioParts,10)||0))
            return json({ ok:false, error:'wav piece missing' }, 404);
          const key = sink === 'r2' ? ('wav/' + msg.audioUpload + '/' + part) : ('wav:' + msg.audioUpload + ':' + part);
          if (sink === 'r2') {
            if (!env || !env.DREAMSHARE_R2) return json({ok:false,error:'R2 not bound'},502);
            const obj = await env.DREAMSHARE_R2.get(key);
            if (!obj) return json({ok:false,error:'wav piece missing'},404);
            return new Response(obj.body,{status:200,headers:Object.assign({},CORS,{'Content-Type':msg.audioMime||'audio/wav','Cache-Control':'private, max-age=3600'})});
          }
          if (!env || !env.DREAMSHARE_KV) return json({ok:false,error:'KV not bound'},502);
          const bytes = await env.DREAMSHARE_KV.get(key,'arrayBuffer');
          if (!bytes) return json({ok:false,error:'wav piece missing'},404);
          return new Response(bytes,{status:200,headers:Object.assign({},CORS,{'Content-Type':msg.audioMime||'audio/wav','Cache-Control':'private, max-age=3600'})});
        } catch (err) { return json({ok:false,error:'private wav read failed'},502); }
      }

      const wavId = String(url.searchParams.get('wav') || '').replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 48);
      if (wavId) {
        try {
          const feed = await loadFeed(env);
          const thread = (feed.threads || []).find(function (t) { return t && t.id === wavId; });
          if (!thread || (thread.audioExpires && Date.now() > thread.audioExpires)) {
            return json({ ok: false, error: 'wav expired or missing' }, 404);
          }
          const vault = vaultOf(thread);
          if (vault.audioStore) {
            const part = parseInt(url.searchParams.get('part') || '0', 10);
            if (!Number.isFinite(part) || part < 0 || part >= vault.audioParts) {
              return json({ ok: false, error: 'wav piece missing' }, 404);
            }
            const key = vault.audioStore === 'r2'
              ? ('wav/' + vault.audioUpload + '/' + part)
              : ('wav:' + vault.audioUpload + ':' + part);
            const mime = thread.audioMime || 'audio/wav';
            const headers = {
              'Content-Type': mime,
              'Cache-Control': 'public, max-age=86400',
              'Access-Control-Allow-Origin': '*'
            };
            if (vault.audioStore === 'r2') {
              if (!env || !env.DREAMSHARE_R2) return json({ ok: false, error: 'R2 not bound' }, 502);
              const obj = await env.DREAMSHARE_R2.get(key);
              if (!obj) return json({ ok: false, error: 'wav piece missing' }, 404);
              return new Response(obj.body, { status: 200, headers: headers });
            }
            if (!env || !env.DREAMSHARE_KV) return json({ ok: false, error: 'KV not bound' }, 502);
            const stream = await env.DREAMSHARE_KV.get(key, 'stream');
            if (!stream) return json({ ok: false, error: 'wav piece missing' }, 404);
            return new Response(stream, { status: 200, headers: headers });
          }
          const chunks = cleanChunkIds(thread.audioChunks);
          if (!chunks.length) return json({ ok: false, error: 'wav expired or missing' }, 404);
          const b64 = await readChunkB64(chunks);
          const bytes = b64ToBytes(b64);
          return new Response(bytes, {
            status: 200,
            headers: {
              'Content-Type': thread.audioMime || 'audio/wav',
              'Cache-Control': 'public, max-age=86400',
              'Access-Control-Allow-Origin': '*'
            }
          });
        } catch (err) {
          return json({ ok: false, error: 'wav read failed' }, 502);
        }
      }
      try {
        const feed = await loadFeed(env);
        const origin = url.origin;
        const threads = (feed.threads || []).map(function (t) { return publicThread(t, origin); });
        if (method === 'HEAD') {
          return new Response(null, { status: 200, headers: CORS });
        }
        const chat = Array.isArray(feed.chat) ? feed.chat.slice(-100) : [];
        const online = await readOnline(env);
        // VST simple feed (?feed=1) — plain chat log for Kyoto / DreamShare Lite logBox
        if (url.searchParams.get('feed') === '1' || url.searchParams.get('format') === 'vst') {
          let log = '';
          for (let i = 0; i < chat.length; i++) {
            const m = chat[i];
            if (!m) continue;
            log += String(m.user || '?') + ': ' + String(m.text || '') + '\n';
          }
          return json({
            ok: true,
            storage: STORAGE,
            body: log,
            messages: chat,
            chat: chat,
            threads: threads.slice(0, 40),
            online: online,
            onlineUsers: await readPresence(env),
            onlineCount: online.length,
            themeApi: THEME_API_VERSION,
            vstPatch: VST_PATCH_VERSION,
            kyotoApi: KYOTO_API_VERSION
          });
        }

        return json({
          ok: true,
          storage: STORAGE,
          clients: ['web', 'vst', 'kyoto'],
          themes: THEMES.map(function (t) { return { id: t.id, name: t.name, tag: t.tag, effects: t.effects || '' }; }),
          themeDefault: 'trippah',
          vst: {
            version: VST_RELEASE.version,
            name: VST_RELEASE.name,
            label: VST_RELEASE.label,
            download: true,
            path: '/?download=vst'
          },
          audioKeepDays: 100,
          audioMaxBytes: MAX_AUDIO_BYTES,
          reactions: 'server-v1',
          audioVault: audioSink(env) !== 'bin',
          audioSink: audioSink(env),
          threads: threads,
          // alias for older / alternate clients that still look at posts[]
          posts: threads,
          chat: chat,
          chatMax: 100,
          mods: feed.mods || [],
          roles: feed.roles || {},
          customRoles: cleanCustomRoles(feed.customRoles),
          online: online,
            onlineUsers: await readPresence(env),
          onlineCount: online.length,
          activeUsers: online.length,
          supers: SUPER_ADMINS,
          updated: feed.updated || Date.now(),
          kv: !!(env && env.DREAMSHARE_KV),
          themeApi: THEME_API_VERSION,
          vstPatch: VST_PATCH_VERSION,
          kyotoApi: KYOTO_API_VERSION,
          customRolesApi: 'v1'
        });
      } catch (err) {
        return json({ ok: false, error: 'store read failed', detail: String(err && err.message || err) }, 502);
      }
    }

    if (method === 'POST' || method === 'PUT') {
      let body = {};
      try { body = await request.json(); }
      catch (_) {
        try { const t = await request.text(); body = t ? JSON.parse(t) : {}; }
        catch (_) { body = {}; }
      }

      const action = String(body.action || body.type || 'create_thread').toLowerCase();
      if (action === 'login' || action === 'register') {
        try { return json(await loginAccount(env, body)); }
        catch (err) { return json({ ok: false, error: String(err && err.message || err) }, 502); }
      }
      let sess = null;
      try { sess = await sessionFromBody(env, body, request); }
      catch (err) { return json({ ok: false, error: 'account check failed' }, 502); }
      // Shared auth for web iframe + DreamShare Lite VST
      if (!sess) return json({ ok: false, error: 'Login required', code: 'auth' }, 401);
      const user = sess.user;

      // ---- Kyoto / Community module router ----
      // Keep every Kyoto action in this single authenticated block, before the
      // normal feed/action switch. This prevents duplicate handlers and makes
      // module_get/upload failures deterministic.
      const kyotoActions = [
        'module_list', 'module_get', 'module_publish', 'module_delete',
        'module_remove', 'catalog', 'catalog_delete', 'community',
        'community_get', 'community_publish'
      ];
      if (kyotoActions.indexOf(action) >= 0) {
        try {
          if (action === 'community_publish') {
            const looksKyoto = !!(body.module || body.format === 'kyoteppah-module-1' ||
              body.format === 'kyoteppah-effect-1' ||
              (body.state && (body.state.widgets || body.state.slots ||
               body.state.steps || body.state.format === 'kyoteppah-module-1' ||
               body.state.format === 'kyoteppah-effect-1')));
            if (!looksKyoto) return json(await communityPublish(env, user, body), 200);
          }
          const r = await runKyotoModule(action, body, sess, env);
          if (r != null) return json(r);
          return json({ ok: false, error: 'unsupported Kyoto action', action: action }, 400);
        } catch (err) {
          return json({ ok: false, error: String(err && err.message || err) }, 502);
        }
      }

      if (action === 'list_threads' || action === 'threads') {
        let threadFeed;
        try { threadFeed = await loadFeed(env); }
        catch (err) { return json({ ok:false, error:'store read failed' }, 502); }
        const threads = Array.isArray(threadFeed.threads) ? threadFeed.threads.slice(-80).reverse() : [];
        return json({ ok:true, storage:STORAGE, threads:threads, count:threads.length });
      }

      // ---- Instrument presets (account-backed; safe to use from web instruments or VST WebView) ----
      if (action === 'preset_list' || action === 'preset_get') {
        const machine = cleanPresetMachine(body.machine || body.instrument || body.plugin);
        if (!machine) return json({ ok: false, error: 'missing instrument' }, 400);
        const db = await readAccounts(env);
        const rec = db.users && db.users[user.toLowerCase()];
        if (!rec) return json({ ok: false, error: 'account missing' }, 404);
        const presets = publicPresets(rec, machine);
        return json({
          ok: true,
          storage: STORAGE,
          user: user,
          machine: machine,
          presets: presets,
          count: presets.length,
          max: PRESET_MAX_PER_USER
        });
      }

      if (action === 'preset_save') {
        const machine = cleanPresetMachine(body.machine || body.instrument || body.plugin);
        const name = cleanPresetName(body.name || body.presetName);
        if (!machine) return json({ ok: false, error: 'missing instrument' }, 400);
        if (!name) return json({ ok: false, error: 'preset name required' }, 400);

        let state = body.state;
        if (typeof state === 'string') {
          try { state = JSON.parse(state); } catch (_) { return json({ ok: false, error: 'preset state must be valid JSON' }, 400); }
        }
        if (!state || typeof state !== 'object' || Array.isArray(state)) {
          return json({ ok: false, error: 'preset state must be an object' }, 400);
        }
        let stateJson = '';
        try { stateJson = JSON.stringify(state); } catch (_) { return json({ ok: false, error: 'preset state could not be saved' }, 400); }
        if (stateJson.length > PRESET_MAX_STATE) return json({ ok: false, error: 'preset state is too large' }, 413);

        const db = await readAccounts(env);
        const rec = db.users && db.users[user.toLowerCase()];
        if (!rec) return json({ ok: false, error: 'account missing' }, 404);
        const presets = ensurePresets(rec);
        const now = Date.now();
        const existing = presets.find(function (p) {
          return p && p.machine === machine && String(p.name).toLowerCase() === name.toLowerCase();
        });
        const entry = {
          name: existing ? existing.name : name,
          machine: machine,
          state: state,
          created: existing && existing.created ? existing.created : now,
          updated: now
        };
        if (existing) {
          const ix = presets.indexOf(existing);
          presets[ix] = entry;
        } else {
          presets.push(entry);
        }

        // Enforce the account-wide cap, preserving the newest presets.
        presets.sort(function (a, b) { return (b.updated || b.created || 0) - (a.updated || a.created || 0); });
        if (presets.length > PRESET_MAX_PER_USER) presets.splice(PRESET_MAX_PER_USER);

        db.users[user.toLowerCase()] = rec;
        await writeAccounts(env, db);
        return json({
          ok: true,
          storage: STORAGE,
          user: user,
          machine: machine,
          preset: entry,
          presets: publicPresets(rec, machine),
          count: publicPresets(rec, machine).length,
          max: PRESET_MAX_PER_USER
        });
      }

      if (action === 'preset_load') {
        const machine = cleanPresetMachine(body.machine || body.instrument || body.plugin);
        const name = cleanPresetName(body.name || body.presetName);
        if (!machine || !name) return json({ ok: false, error: 'instrument and preset name required' }, 400);
        const db = await readAccounts(env);
        const rec = db.users && db.users[user.toLowerCase()];
        if (!rec) return json({ ok: false, error: 'account missing' }, 404);
        const preset = ensurePresets(rec).find(function (p) {
          return p && p.machine === machine && String(p.name).toLowerCase() === name.toLowerCase();
        });
        if (!preset) return json({ ok: false, error: 'preset not found' }, 404);
        return json({ ok: true, storage: STORAGE, user: user, machine: machine, preset: preset });
      }

      if (action === 'preset_delete') {
        const machine = cleanPresetMachine(body.machine || body.instrument || body.plugin);
        const name = cleanPresetName(body.name || body.presetName);
        if (!machine || !name) return json({ ok: false, error: 'instrument and preset name required' }, 400);
        const db = await readAccounts(env);
        const rec = db.users && db.users[user.toLowerCase()];
        if (!rec) return json({ ok: false, error: 'account missing' }, 404);
        const before = ensurePresets(rec).length;
        rec.presets = ensurePresets(rec).filter(function (p) {
          return !(p && p.machine === machine && String(p.name).toLowerCase() === name.toLowerCase());
        });
        if (rec.presets.length === before) return json({ ok: false, error: 'preset not found' }, 404);
        db.users[user.toLowerCase()] = rec;
        await writeAccounts(env, db);
        return json({
          ok: true,
          storage: STORAGE,
          user: user,
          machine: machine,
          deleted: name,
          presets: publicPresets(rec, machine)
        });
      }

      // ---- Private social layer: friends, DMs and approval-based WAV requests ----
      if (action === 'social_list' || action === 'friends_list') {
        const db = await readAccounts(env), me = socialUser(db, user);
        function themed(names) {
          return (names || []).map(function (n) {
            const rec = db.users && db.users[String(n || '').toLowerCase()];
            return { name: n, theme: (rec && rec.theme) || 'trippah', online: false };
          });
        }
        return json({ ok:true, storage:STORAGE, user:user, friends:me.friends, friendsDetailed:themed(me.friends), incoming:me.incoming.slice(-50),
          outgoing:me.outgoing.slice(-50), wavRequests:me.wavRequests.slice(-50), directory:socialDirectory(db) });
      }
      if (action === 'friend_request') {
        const target = cleanName(body.target || body.to);
        if (!accountExists(await readAccounts(env), target)) return json({ok:false,error:'User not found'},404);
        if (target.toLowerCase() === user.toLowerCase()) return json({ok:false,error:'You cannot friend yourself'},400);
        const db = await readAccounts(env), me = socialUser(db,user), them = socialUser(db,target);
        if (me.friends.some(x=>x.toLowerCase()===target.toLowerCase())) return json({ok:true,status:'friends'});
        if (!them.incoming.some(x=>x.from.toLowerCase()===user.toLowerCase() && x.status==='pending')) {
          them.incoming.push({id:socialRequestId('fr'),from:user,to:target,at:Date.now(),status:'pending'});
          me.outgoing.push({id:them.incoming[them.incoming.length-1].id,from:user,to:target,at:Date.now(),status:'pending'});
          them.incoming=them.incoming.slice(-100); me.outgoing=me.outgoing.slice(-100);
        }
        await writeAccounts(env,db); return json({ok:true,status:'pending',target:target});
      }
      if (action === 'friend_accept' || action === 'friend_decline') {
        const id=String(body.requestId||body.id||'').replace(/[^A-Za-z0-9_\-]/g,'').slice(0,64);
        const db=await readAccounts(env), me=socialUser(db,user);
        const req=me.incoming.find(x=>x.id===id);
        if(!req) return json({ok:false,error:'Friend request not found'},404);
        const sender=socialUser(db,req.from);
        me.incoming=me.incoming.filter(x=>x.id!==id);
        sender.outgoing=sender.outgoing.filter(x=>x.id!==id);
        if(action==='friend_accept'){ addUniqueName(me.friends,req.from); addUniqueName(sender.friends,user); }
        await writeAccounts(env,db); return json({ok:true,status:action==='friend_accept'?'friends':'declined'});
      }
      if (action === 'friend_remove') {
        const target=cleanName(body.target||body.to), db=await readAccounts(env), me=socialUser(db,user), them=socialUser(db,target);
        me.friends=removeName(me.friends,target); them.friends=removeName(them.friends,user);
        await writeAccounts(env,db); return json({ok:true,status:'removed'});
      }
      if (action === 'dm_list') {
        const peer=cleanName(body.peer||body.to);
        const db=await readAccounts(env);
        if(!accountExists(db,peer)) return json({ok:false,error:'User not found'},404);
        return json({ok:true,peer:peer,messages:privateDmList(db,user,peer),friends:socialUser(db,user).friends});
      }
      if (action === 'dm_send' || action === 'dm') {
        const peer=cleanName(body.to||body.peer||body.target), text=String(body.text||body.message||'').slice(0,1000).trim();
        const db=await readAccounts(env);
        if(!accountExists(db,peer)) return json({ok:false,error:'User not found'},404);
        if(!text && !body.audioUpload) return json({ok:false,error:'empty message'},400);
        const msg=addDm(db,user,peer,{text:text,audioId:body.audioId||null,audioUrl:null,audioStore:body.audioStore||'',audioUpload:String(body.audioUpload||'').slice(0,48),audioParts:Math.max(0,parseInt(body.audioParts,10)||0),audioBytes:Math.max(0,parseInt(body.audioBytes,10)||0),audioMime:String(body.audioMime||'audio/wav').slice(0,40)});
        await writeAccounts(env,db); return json({ok:true,message:msg,messages:privateDmList(db,user,peer)});
      }
      if (action === 'wav_request') {
        const target=cleanName(body.target||body.to||body.user), db=await readAccounts(env);
        if(!accountExists(db,target)) return json({ok:false,error:'User not found'},404);
        if(target.toLowerCase()===user.toLowerCase()) return json({ok:false,error:'You cannot request your own WAV'},400);
        const recipient=socialUser(db,target), me=socialUser(db,user);
        const rq={id:socialRequestId('wav'),from:user,to:target,at:Date.now(),status:'pending',source:String(body.source||'dm').slice(0,8),note:String(body.note||'').slice(0,300)};
        recipient.wavRequests.push(rq); me.wavRequests.push(rq); recipient.wavRequests=recipient.wavRequests.slice(-100); me.wavRequests=me.wavRequests.slice(-100);
        await writeAccounts(env,db); return json({ok:true,request:rq});
      }
      if (action === 'wav_request_approve' || action === 'wav_request_decline') {
        const id=String(body.requestId||body.id||'').replace(/[^A-Za-z0-9_\-]/g,'').slice(0,64), db=await readAccounts(env), me=socialUser(db,user);
        const rq=me.wavRequests.find(x=>x.id===id && String(x.to).toLowerCase()===user.toLowerCase());
        if(!rq) return json({ok:false,error:'WAV request not found'},404);
        rq.status=action==='wav_request_approve'?'approved':'declined'; rq.decidedAt=Date.now();
        const sender=socialUser(db,rq.from), mirror=sender.wavRequests.find(x=>x.id===id); if(mirror){mirror.status=rq.status;mirror.decidedAt=rq.decidedAt;}
        await writeAccounts(env,db); return json({ok:true,status:rq.status,request:rq});
      }
      if (action === 'wav_request_fulfill') {
        const id=String(body.requestId||body.id||'').replace(/[^A-Za-z0-9_\-]/g,'').slice(0,64), db=await readAccounts(env), me=socialUser(db,user);
        const rq=me.wavRequests.find(x=>x.id===id && String(x.to).toLowerCase()===user.toLowerCase() && x.status==='approved');
        if(!rq) return json({ok:false,error:'Approved WAV request not found'},404);
        if(!body.audioUpload || !parseInt(body.audioParts,10)) return json({ok:false,error:'Attach the approved WAV export first'},400);
        const msg=addDm(db,user,rq.from,{text:'WAV export for your approved request',requestId:id,audioId:'a'+id,audioStore:body.audioStore,audioUpload:String(body.audioUpload).slice(0,48),audioParts:parseInt(body.audioParts,10),audioBytes:parseInt(body.audioBytes,10)||0,audioMime:String(body.audioMime||'audio/wav').slice(0,40)});
        rq.status='fulfilled'; rq.fulfilledAt=Date.now();
        const mirror=socialUser(db,rq.from).wavRequests.find(x=>x.id===id); if(mirror){mirror.status='fulfilled';mirror.fulfilledAt=rq.fulfilledAt;}
        await writeAccounts(env,db); return json({ok:true,message:msg,request:rq});
      }
      if (action === 'wav_request_status') {
        const db=await readAccounts(env), me=socialUser(db,user);
        return json({ok:true,requests:me.wavRequests.slice(-100)});
      }
      if (action === 'audio_part_b64') {
        const result=await handleAudioPartB64(env,body,user);
        return json(result,result.ok?200:(result.code==='no-sink'?400:413));
      }

      // Lightweight session check (VST / homepage can refresh role + user)
      if (action === 'session' || action === 'whoami' || action === 'me') {
        const themePack = resolveTheme(sess.theme || 'trippah');
        return json({
          ok: true,
          user: user,
          role: sess.role,
          token: sess.token || body.token || '',
          theme: themePack.id,
          themePack: themePack,
          themes: THEMES.map(function (t) { return { id: t.id, name: t.name, tag: t.tag, effects: t.effects || '' }; }),
          storage: STORAGE,
          clients: ['web', 'vst'],
          supers: SUPER_ADMINS,
          themeApi: THEME_API_VERSION,
          vstPatch: VST_PATCH_VERSION,
          customRolesApi: 'v1'
        });
      }

      // Persist UI theme on the account (DreamShare web + VST dropdown)
      if (action === 'set_theme' || action === 'theme') {
        const wanted = String(body.theme || body.id || body.name || '').toLowerCase().replace(/[^a-z0-9]/g, '');
        if (!wanted || THEME_IDS.indexOf(wanted) < 0) {
          return json({ ok: false, error: 'unknown theme', themes: THEME_IDS }, 400);
        }
        const db = await readAccounts(env);
        const key = user.toLowerCase();
        const rec = db.users[key];
        if (!rec) return json({ ok: false, error: 'account missing' }, 404);
        rec.theme = wanted;
        db.users[key] = rec;
        // keep session theme in sync when token is present
        if (sess.token && db.sessions && db.sessions[sess.token]) {
          db.sessions[sess.token].theme = wanted;
        }
        await writeAccounts(env, db);
        const themePack = resolveTheme(wanted);
        return json({
          ok: true,
          storage: STORAGE,
          theme: themePack.id,
          themePack: themePack,
          themes: THEMES.map(function (t) { return { id: t.id, name: t.name, tag: t.tag, effects: t.effects || '' }; }),
          user: user,
          themeApi: THEME_API_VERSION,
          vstPatch: VST_PATCH_VERSION
        });
      }

      let feed;
      try { feed = await loadFeed(env); }
      catch (err) { return json({ ok: false, error: 'store read failed' }, 502); }
      if (!Array.isArray(feed.threads)) feed.threads = [];
      if (!Array.isArray(feed.mods)) feed.mods = [];
      if (!feed.customRoles || typeof feed.customRoles !== 'object') feed.customRoles = {};

      try {
        if (action === 'create_thread' || action === 'post' || action === 'thread') {
          const title = String(body.title || body.text || '').slice(0, 120).trim();
          const text = String(body.text || '').slice(0, 1000).trim();
          const vault = vaultOf({
            audioStore: body.audioStore,
            audioUpload: body.audioUpload,
            audioParts: body.audioParts,
            audioBytes: body.audioBytes
          });
          if (vault.audioBytes > MAX_AUDIO_BYTES) return json({ ok: false, error: 'WAV over 75 MB' }, 413);
          if ((body.audioStore === 'r2' || body.audioStore === 'kv') && !vault.audioStore) {
            return json({ ok: false, error: 'tape incomplete' }, 400);
          }
          if (vault.audioStore === 'r2' && audioSink(env) !== 'r2') return json({ ok: false, error: 'R2 not bound' }, 400);
          if (vault.audioStore === 'kv' && audioSink(env) === 'bin') return json({ ok: false, error: 'KV not bound' }, 400);
          if (!title && !text && !body.audioData && !body.hasAudio && !vault.audioParts) return json({ ok: false, error: 'empty' }, 400);
          const id = String(body.id || ('t' + Date.now() + Math.floor(Math.random() * 999))).replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 48);
          let audioData = null;
          if (typeof body.audioData === 'string' && body.audioData.indexOf('data:audio') === 0) {
            audioData = body.audioData.slice(0, 2500000);
          }
          let audioChunks = cleanChunkIds(body.audioChunks);
          let audioExpires = Number(body.audioExpires) || 0;
          let audioMime = String(body.audioMime || 'audio/wav').slice(0, 40);
          if (audioData && !audioChunks.length) {
            const stored = await storeAudioChunks(audioData);
            if (!stored) return json({ ok: false, error: 'wav too big' }, 400);
            audioChunks = stored.chunks;
            audioExpires = stored.expires;
            audioMime = stored.mime;
          }
          if (audioChunks.length && !audioExpires) audioExpires = Date.now() + AUDIO_KEEP_MS;
          let audioUrl = (typeof body.audioUrl === 'string' && body.audioUrl.indexOf('http') === 0) ? body.audioUrl : null;
          const thread = {
            id: id,
            user: user,
            title: title || text.slice(0, 80) || 'audio',
            text: text || title,
            at: body.at || Date.now(),
            comments: [],
            theme: String((sess && sess.theme) || 'trippah').toLowerCase(),
            hasAudio: !!(body.hasAudio || audioChunks.length || audioUrl || vault.audioParts),
            audioId: body.audioId || (audioChunks.length || vault.audioParts ? ('a' + id) : null),
            audioUrl: audioUrl,
            audioChunks: audioChunks,
            audioExpires: audioExpires || (vault.audioParts ? (Date.now() + AUDIO_KEEP_MS) : 0),
            audioMime: audioMime,
            audioStore: vault.audioStore,
            audioUpload: vault.audioUpload,
            audioParts: vault.audioParts,
            audioBytes: vault.audioBytes
          };
          feed.threads = mergeThreads(feed.threads.filter(function (t) { return t && t.id !== id; }), [thread]);
          feed.updated = Date.now();
          await writeFeed(env, feed);
          const origin = new URL(request.url).origin;
          return json({ ok: true, storage: STORAGE, id: id, audioKeepDays: 100, thread: publicThread(thread, origin) });
        }

        if (action === 'comment' || action === 'reply') {
          const threadId = String(body.threadId || body.id || '').replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 48);
          const text = String(body.text || '').slice(0, 500).trim();
          if (!threadId || !text) return json({ ok: false, error: 'empty' }, 400);
          const thread = feed.threads.find(function (t) { return t && t.id === threadId; });
          if (!thread) return json({ ok: false, error: 'thread not found' }, 404);
          if (!Array.isArray(thread.comments)) thread.comments = [];
          const cid = String(body.commentId || ('c' + Date.now() + Math.floor(Math.random() * 999))).replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 48);
          const comment = { id: cid, user: user, text: text, at: body.at || Date.now(), theme: String((sess && sess.theme) || 'trippah').toLowerCase() };
          thread.comments.push(comment);
          if (thread.comments.length > 80) thread.comments = thread.comments.slice(-80);
          feed.updated = Date.now();
          await writeFeed(env, feed);
          return json({ ok: true, storage: STORAGE, id: cid, comment: comment, threadId: threadId });
        }

        if (action === 'delete_thread') {
          const threadId = String(body.threadId || body.id || '').replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 48);
          const thread = feed.threads.find(function (t) { return t && t.id === threadId; });
          if (!thread) return json({ ok: false, error: 'thread not found' }, 404);
          const owner = String(thread.user || '').toLowerCase() === user.toLowerCase();
          if (!owner && !isMod(feed, user)) return json({ ok: false, error: 'not allowed' }, 403);
          const gone = cleanChunkIds(thread.audioChunks);
          const vaultGone = vaultOf(thread);
          feed.threads = feed.threads.filter(function (t) { return t && t.id !== threadId; });
          feed.updated = Date.now();
          await writeFeed(env, feed);
          for (let i = 0; i < gone.length; i++) await deleteChunk(gone[i]);
          await deleteVault(env, vaultGone);
          return json({ ok: true, storage: STORAGE, deleted: threadId });
        }

        if (action === 'delete_comment') {
          const threadId = String(body.threadId || '').replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 48);
          const commentId = String(body.commentId || body.id || '').replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 48);
          const thread = feed.threads.find(function (t) { return t && t.id === threadId; });
          if (!thread) return json({ ok: false, error: 'thread not found' }, 404);
          const comment = (thread.comments || []).find(function (c) { return c && c.id === commentId; });
          if (!comment) return json({ ok: false, error: 'comment not found' }, 404);
          const owner = String(comment.user || '').toLowerCase() === user.toLowerCase();
          if (!owner && !isMod(feed, user)) return json({ ok: false, error: 'not allowed' }, 403);
          thread.comments = (thread.comments || []).filter(function (c) { return c && c.id !== commentId; });
          feed.updated = Date.now();
          await writeFeed(env, feed);
          return json({ ok: true, storage: STORAGE, deleted: commentId });
        }

        if (action === 'promote_mod' || action === 'mod') {
          if (!isSuper(user) || sess.role !== 'super') return json({ ok: false, error: 'only the real Trippah / Goonr login can promote mods' }, 403);
          const target = normUser(body.target || body.who);
          if (!target || target.length < 3) return json({ ok: false, error: 'bad target' }, 400);
          if (isSuper(target)) return json({ ok: false, error: 'supers are already above mods' }, 400);
          if (!feed.mods.some(function (m) { return String(m).toLowerCase() === target.toLowerCase(); })) feed.mods.push(target);
          feed.updated = Date.now();
          await writeFeed(env, feed);
          return json({ ok: true, storage: STORAGE, mods: feed.mods, promoted: target });
        }

        if (action === 'demote_mod' || action === 'unmod') {
          if (!isSuper(user) || sess.role !== 'super') return json({ ok: false, error: 'only the real Trippah / Goonr login can demote mods' }, 403);
          const target = normUser(body.target || body.who);
          if (!target) return json({ ok: false, error: 'bad target' }, 400);
          feed.mods = feed.mods.filter(function (m) { return String(m).toLowerCase() !== target.toLowerCase(); });
          feed.updated = Date.now();
          await writeFeed(env, feed);
          return json({ ok: true, storage: STORAGE, mods: feed.mods, demoted: target });
        }

        // ---- Live chat (last 100 messages, ring buffer) ----
        if (action === 'chat_list' || action === 'chat_get' || action === 'feed') {
          const chat = Array.isArray(feed.chat) ? feed.chat.slice(-100) : [];
          let log = '';
          for (let i = 0; i < chat.length; i++) {
            const m = chat[i];
            if (!m) continue;
            log += String(m.user || '?') + ': ' + String(m.text || '') + '\n';
          }
          return json({
            ok: true,
            storage: STORAGE,
            chat: chat,
            body: log,
            messages: chat,
            chatMax: 100,
            mods: feed.mods || [],
            supers: SUPER_ADMINS,
            roles: feed.roles || {},
            customRoles: feed.customRoles || {},
            threads: (feed.threads || []).slice(0, 40),
            kyotoApi: KYOTO_API_VERSION
          });
        }

        if (action === 'chat_send' || action === 'chat') {
          const text = String(body.text || body.message || '').slice(0, 400).trim();
          if (!text) return json({ ok: false, error: 'empty message' }, 400);
          if (!Array.isArray(feed.chat)) feed.chat = [];
          const msg = {
            id: String(body.id || ('m' + Date.now() + Math.floor(Math.random() * 999))).replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 48),
            user: user,
            text: text,
            at: body.at || Date.now(),
            theme: String((sess && sess.theme) || body.theme || 'trippah').toLowerCase(),
            reactions: {}
          };
          feed.chat.push(msg);
          // Keep only the last 100 messages — older ones are dropped
          if (feed.chat.length > 100) feed.chat = feed.chat.slice(-100);
          feed.updated = Date.now();
          await writeFeed(env, feed);
          // Mirror into Kyoto Discord #general with username + "Sent from DreamShare"
          await mirrorChatToKyotoDiscord(env, user, text);
          return json({ ok: true, storage: STORAGE, message: msg, chat: feed.chat.slice(-100), chatMax: 100 });
        }


        if (action === 'react') {
          const kind = String(body.kind || 'comment');
          const emoji = String(body.emoji || '').replace(/[^a-z0-9]/g, '').slice(0, 12);
          const allowed = { up:1, heart:1, fire:1, laugh:1, skull:1, moon:1, eyes:1, '100':1 };
          if (!allowed[emoji]) return json({ ok: false, error: 'bad emoji' }, 400);
          const id = String(body.id || '').replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 48);
          if (!id) return json({ ok: false, error: 'missing id' }, 400);
          function toggle(bag) {
            bag.reactions = bag.reactions || {};
            const list = Array.isArray(bag.reactions[emoji]) ? bag.reactions[emoji].slice() : [];
            const me = user.toLowerCase();
            const idx = list.findIndex(function (u) { return String(u).toLowerCase() === me; });
            if (idx >= 0) list.splice(idx, 1);
            else list.push(user);
            bag.reactions[emoji] = list.slice(0, 80);
            return bag.reactions;
          }
          if (kind === 'chat') {
            if (!Array.isArray(feed.chat)) feed.chat = [];
            const msg = feed.chat.find(function (m) { return m && m.id === id; });
            if (!msg) return json({ ok: false, error: 'message not found' }, 404);
            toggle(msg);
            feed.updated = Date.now();
            await writeFeed(env, feed);
            return json({ ok: true, storage: STORAGE, reactions: msg.reactions, chat: feed.chat.slice(-100) });
          }
          const threadId = String(body.threadId || (kind === 'thread' ? id : '')).replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 48);
          const thread = feed.threads.find(function (t) { return t && t.id === threadId; });
          if (!thread) return json({ ok: false, error: 'thread not found' }, 404);
          if (kind === 'thread') {
            toggle(thread);
          } else {
            const comment = (thread.comments || []).find(function (c) { return c && c.id === id; });
            if (!comment) return json({ ok: false, error: 'comment not found' }, 404);
            toggle(comment);
          }
          feed.updated = Date.now();
          await writeFeed(env, feed);
          return json({ ok: true, storage: STORAGE, threadId: thread.id });
        }

        if (action === 'chat_delete' || action === 'chat_mod_delete') {
          const msgId = String(body.messageId || body.id || '').replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 48);
          if (!msgId) return json({ ok: false, error: 'missing id' }, 400);
          if (!Array.isArray(feed.chat)) feed.chat = [];
          const msg = feed.chat.find(function (m) { return m && m.id === msgId; });
          if (!msg) return json({ ok: false, error: 'message not found' }, 404);
          const owner = String(msg.user || '').toLowerCase() === user.toLowerCase();
          const canMod = isSuper(user) || isMod(feed, user) || sess.role === 'super';
          if (!owner && !canMod) return json({ ok: false, error: 'not allowed' }, 403);
          feed.chat = feed.chat.filter(function (m) { return m && m.id !== msgId; });
          feed.updated = Date.now();
          await writeFeed(env, feed);
          return json({ ok: true, storage: STORAGE, deleted: msgId, chat: feed.chat.slice(-100) });
        }

        if (action === 'chat_clear') {
          const canMod = isSuper(user) || isMod(feed, user) || sess.role === 'super';
          if (!canMod) return json({ ok: false, error: 'mods only' }, 403);
          feed.chat = [];
          feed.updated = Date.now();
          await writeFeed(env, feed);
          return json({ ok: true, storage: STORAGE, chat: [], cleared: true });
        }

        if (action === 'presence' || action === 'heartbeat') {
          const online = await touchOnline(env, user, sess && sess.theme);
          return json({
            ok: true,
            storage: STORAGE,
            online: online,
            onlineUsers: await readPresence(env),
            onlineCount: online.length,
            activeUsers: online.length,
            user: user,
            presenceTtlMs: ONLINE_TTL
          });
        }

        if (action === 'set_role' || action === 'clear_role') {
          const canMod = isSuper(user) || isMod(feed, user) || sess.role === 'super';
          if (!canMod) return json({ ok: false, error: 'mods only' }, 403);
          const target = String(body.target || body.name || '').trim();
          const roleName = String(body.role || 'kyoto').toLowerCase();
          if (!target) return json({ ok: false, error: 'missing target' }, 400);
          if (roleName !== 'kyoto') return json({ ok: false, error: 'unknown role' }, 400);
          if (!feed.roles || typeof feed.roles !== 'object') feed.roles = {};
          const key = Object.keys(feed.roles).find(function (k) { return k.toLowerCase() === target.toLowerCase(); }) || target;
          let list = Array.isArray(feed.roles[key]) ? feed.roles[key].slice() : [];
          if (action === 'set_role') {
            if (list.indexOf('kyoto') < 0) list.push('kyoto');
            feed.roles[target] = list;
            if (key !== target) delete feed.roles[key];
          } else {
            list = list.filter(function (r) { return r !== 'kyoto'; });
            if (list.length) feed.roles[key] = list;
            else delete feed.roles[key];
          }
          feed.updated = Date.now();
          await writeFeed(env, feed);
          return json({ ok: true, storage: STORAGE, roles: feed.roles, customRoles: cleanCustomRoles(feed.customRoles), target: target, role: roleName });
        }

        // ---- Custom collective roles (Trippah / Goonr only) ----
        // Assign a Kyoto-style badge with custom label + color/gradient.
        // Used when an admin clicks a name in chat/feed.
        if (action === 'set_custom_role' || action === 'custom_role' || action === 'set_collective') {
          if (!isSuper(user) || sess.role !== 'super') {
            return json({ ok: false, error: 'only Trippah / Goonr can assign custom roles' }, 403);
          }
          const target = normUser(body.target || body.name || body.who);
          if (!target || target.length < 3) return json({ ok: false, error: 'bad target' }, 400);
          if (isSuper(target)) return json({ ok: false, error: 'supers already stand out' }, 400);
          const role = normalizeCustomRole({
            label: body.label || body.roleName || body.role || body.collective || body.text,
            color: body.color || body.hex,
            gradient: body.gradient || body.grad,
            bg: body.bg || body.background
          });
          if (!role) return json({ ok: false, error: 'label needs 2–24 characters' }, 400);
          if (!feed.customRoles || typeof feed.customRoles !== 'object') feed.customRoles = {};
          // normalize key to the live username casing if known
          const existingKey = Object.keys(feed.customRoles).find(function (k) {
            return String(k).toLowerCase() === target.toLowerCase();
          });
          if (existingKey && existingKey !== target) delete feed.customRoles[existingKey];
          feed.customRoles[target] = role;
          feed.customRoles = cleanCustomRoles(feed.customRoles);
          feed.updated = Date.now();
          await writeFeed(env, feed);
          return json({
            ok: true,
            storage: STORAGE,
            target: target,
            customRole: role,
            customRoles: feed.customRoles,
            roles: feed.roles || {},
            style: 'kyoto'
          });
        }

        if (action === 'clear_custom_role' || action === 'remove_custom_role' || action === 'clear_collective') {
          if (!isSuper(user) || sess.role !== 'super') {
            return json({ ok: false, error: 'only Trippah / Goonr can clear custom roles' }, 403);
          }
          const target = normUser(body.target || body.name || body.who);
          if (!target) return json({ ok: false, error: 'bad target' }, 400);
          if (!feed.customRoles || typeof feed.customRoles !== 'object') feed.customRoles = {};
          Object.keys(feed.customRoles).forEach(function (k) {
            if (String(k).toLowerCase() === target.toLowerCase()) delete feed.customRoles[k];
          });
          feed.updated = Date.now();
          await writeFeed(env, feed);
          return json({
            ok: true,
            storage: STORAGE,
            target: target,
            cleared: true,
            customRoles: cleanCustomRoles(feed.customRoles),
            roles: feed.roles || {}
          });
        }

        // ---- Discord lite (0.4.1) — authenticated session required ----
        if (action === 'discord_status' || action === 'discord_channels' || action === 'discord_messages' || action === 'discord_send' || action === 'discord_react') {
          const out = await discordLite(env, action, body);
          return json(out, out.ok ? 200 : (out.status || 502));
        }

        return json({ ok: false, error: 'unknown action', action: action }, 400);
      } catch (err) {
        return json({ ok: false, error: String(err && err.message || err) }, 502);
      }
    }

    return json({ ok: false, error: 'unsupported method', got: method }, 405);
  }
};