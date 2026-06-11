# speech — todo

- Per-utterance abort is global-purge on win32 (SAPI purge), web
  (`speechSynthesis.cancel`), android (`tts.stop`) and linux (`CANCEL self`):
  aborting one utterance resolves every queued utterance of this app as ABORTED.
  Pick per-message paths where the OS ever grows them.
- Win32 STT: graceful stop completes immediately without draining a pending final
  hypothesis; language is the OS recognizer default only.
- Apple: report `on_device` honestly per locale (needs an SFSpeechRecognizer
  instance per locale at enumerate; currently conservatively false) and surface
  `requiresOnDeviceRecognition` as a listen option.
- Apple: distinguish task-error domains (network vs audio route) instead of
  blanket `RESULT_NETWORK` from the recognition handler.
- Android: voices appear only after the engine's async init; consider a hotplug
  event so apps don't need a manual refresh. TTS pause is honest-absent
  (`can_pause = false`) — Android offers none.
- Web: recognizers list is the navigator language only (the API has no enumerable
  locale list); STT exists in chromium/webkit only.
- Linux: per-utterance pause maps to client-wide PAUSE; index-mark ranges need
  SSML mode.
- Cloud/local providers (OpenAI, ElevenLabs, whisper.cpp, piper) as out-of-module
  providers once `http` lands — the provider vtable already suffices.
- TTS synthesis-to-buffer (offline render to PCM for the audio mixer) once the
  mixer lands — same lowering, new provider entry.
- Voice selection helpers (best voice for a BCP-47 tag) as pure functions over
  descriptors.
