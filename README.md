# DreamShare VST3 + Worker — GitHub Build

This repository is the GitHub-ready packaging/update for DreamShare VST3 and its Cloudflare Worker compatibility patch.

## What the build does

`build.ps1`:

1. Validates that Node.js is available.
2. Syntax-checks `src/worker.js` with `node --check`.
3. Reads the VST `moduleinfo.json` and verifies the product is `DreamShare`.
4. Stages the VST3 package and Worker into `dist/`.
5. Creates `DreamShare-Windows-VST3-0.2.1.zip`.
6. Writes a SHA-256 checksum beside the ZIP.
7. Optionally deploys the Worker with Wrangler when `-DeployWorker` is supplied.

## Build on Windows

Open PowerShell in the repository root and run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build.ps1
```

The release ZIP will be in `dist\`.

### Build and deploy the Worker

Install/authenticate Wrangler first if it is not already available:

```powershell
npm install -g wrangler
wrangler login
```

Then:

```powershell
.\build.ps1 -DeployWorker
```

The script deploys `src/worker.js` using the Wrangler configuration in `wrangler.toml`.

## Important VST limitation

The supplied VST3 is a compiled Windows x64 binary. The original JUCE/C++ source was not included, so this repository cannot truthfully rebuild native VST controls/effects that are absent from that binary. The build script therefore packages the supplied VST3 unchanged and applies the Worker-side compatibility patch.

If you later add the original JUCE/C++ project, the build script can be extended to compile the VST instead of packaging the supplied binary.

## Cloudflare configuration

The Worker source already contains the DreamShare theme/session/custom-role API compatibility implemented by patch 0.2.1. `wrangler.toml` is intentionally minimal; keep your existing Cloudflare bindings/secrets in your deployment configuration rather than committing credentials.
