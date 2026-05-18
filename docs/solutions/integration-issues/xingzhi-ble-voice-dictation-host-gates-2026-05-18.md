---
title: Xingzhi BLE Voice Dictation Host Gates
date: 2026-05-18
category: integration-issues
module: tools/windows-xingzhi-voice-dictation
problem_type: integration_issue
component: tooling
symptoms:
  - "BLE voice upload could be marked done before ASR and paste completed"
  - "Windows BLE scanning was unreliable after the Xingzhi device was already paired"
  - "ASR provider changes left stale environment variable, CLI, test, and documentation references"
root_cause: missing_workflow_step
resolution_type: code_fix
severity: medium
related_components:
  - "firmware/xingzhi-ble"
  - "firmware/xingzhi-voice-transport"
  - "tools/windows"
  - "siliconflow-asr"
tags:
  - "xingzhi"
  - "ble"
  - "voice-dictation"
  - "siliconflow"
  - "windows-paste"
  - "host-ack"
---

# Xingzhi BLE Voice Dictation Host Gates

## Problem

Xingzhi GPIO40 voice dictation crosses four failure-prone boundaries: device recording, BLE WAV transfer, cloud ASR, and Windows focus-window paste. Treating BLE receive as the success point makes the device show `done` even when transcription or paste fails.

## Symptoms

- The U4 receive-only path could save a WAV and write `done`, but that only proved BLE transfer, not usable dictation.
- When ASR was added, the host had to avoid leaking API keys and had to report failures back through the voice control characteristic.
- Windows scanning sometimes misses a paired/connected BLE device; direct `--address` was needed as a reliable operator path (session history).
- After switching from OpenAI to SiliconFlow, stale names like `OPENAI_API_KEY` and `--openai-model` remained in docs/tests until the provider change was made consistently.

## What Didn't Work

- Writing `done` immediately after `Saved WAV` was too early. It hid downstream ASR and paste failures from the device.
- Relying on one live mode was hard to debug. WAV receive, ASR-only, and paste need separate gates so the failed layer is obvious.
- Depending only on BLE scanning was brittle on Windows. The helper already supported direct address fallback, and the bat entry needed to preserve arbitrary argument pass-through instead of baking in one machine's address.
- Provider migration by doc edits alone was not enough. Tests, CLI help, environment variable parsing, and multipart request assertions all had to move together.

## Solution

Gate the host acknowledgement on the whole downstream workflow:

```python
status_message = post_receive(audio, output) if post_receive else "done"
await write_voice_control(client, status_message or "done", response=config.write_response)
```

The live `post_receive` path now runs SiliconFlow ASR and paste before returning `done`; exceptions are sanitized and sent back as `err:`:

```python
try:
    transcript = transcribe_audio(audio, output_path.name)
    paste_adapter.paste_text(transcript)
except Exception as exc:
    raise RuntimeError(sanitize_error(f"paste_failed:{exc}", [api_key])) from None
```

The helper uses `SILICONFLOW_API_KEY` from local `.env`, fixed model `TeleAI/TeleSpeechASR`, and SiliconFlow's multipart transcription endpoint. Live mode pastes recognized text into the current Windows focus window; `--dump-only` saves WAV only, and `--asr-only` / `--no-paste` transcribes without paste.

`tools/watch_xingzhi_voice_dictation.bat` wraps the common live helper command without storing secrets or addresses:

```bat
py -3 -u tools\windows_xingzhi_voice_dictation.py ^
  --output "%VOICE_OUTPUT%" ^
  %*
```

## Why This Works

The device only knows host success through the BLE voice control characteristic, so `done` must mean "the user-visible action succeeded", not just "bytes arrived". Delaying `done` until after ASR and paste keeps `voice=done` aligned with the text actually appearing in the target window.

Separate modes keep the hardware bring-up sequence debuggable: first prove BLE WAV transfer, then prove ASR from a saved WAV, then prove live paste. This follows the same staged hardware pattern used elsewhere in the Xingzhi port and reduces cross-layer ambiguity.

Direct address support is an operator convenience, not a requirement. It bypasses Windows BLE scan flakiness when a paired device is not discoverable, while the bat keeps `%*` pass-through so different machines can use scan, `--pair`, `--address`, `--dump-only`, or `--asr-only` as needed.

## Prevention

- For host-mediated hardware actions, define `done` as the end-user-visible outcome, not the first successful transport boundary.
- Keep each physical integration layer independently testable: BLE dump, ASR-only, paste-only/fake paste, then live end-to-end.
- Provider changes must update environment variable names, CLI flags, docs, and request tests in the same change.
- Never log API keys; include tests that force an error containing the secret and assert it is redacted.
- Keep Windows helper bat files as thin wrappers with argument pass-through and no machine-specific BLE address or secrets.

## Related Issues

- `docs/solutions/ui-bugs/xingzhi-st7789-display-inversion-white-background-2026-05-14.md` documents the same staged Xingzhi bring-up style for display debugging, but it is a different hardware boundary.
- `docs/plans/2026-05-14-002-feat-xingzhi-ble-voice-dictation-plan.md` captures the staged U4/U5/U6 gates that this implementation followed.
- SiliconFlow transcription reference: <https://docs.siliconflow.cn/cn/api-reference/audio/create-audio-transcriptions>
