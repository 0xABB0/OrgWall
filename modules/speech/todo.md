# speech — todo

- Android backend: TextToSpeech + SpeechRecognizer via JNI (`platform` bridge),
  `RECORD_AUDIO` manifest fragment, java helper like camera's.
- Win32 backend: WinRT `Windows.Media.SpeechSynthesis` / `SpeechRecognition`.
- Linux backend: speech-dispatcher for TTS; evaluate vosk/whisper.cpp as an STT
  provider rather than a host backend.
- Web backend: SpeechSynthesis / webkitSpeechRecognition.
- Apple: report `on_device` honestly per locale (needs an SFSpeechRecognizer
  instance per locale at enumerate; currently conservatively false) and surface
  `requiresOnDeviceRecognition` as a listen option.
- Apple: distinguish task-error domains (network vs audio route) instead of
  blanket `RESULT_NETWORK` from the recognition handler.
- TTS synthesis-to-buffer (offline render to PCM for the audio mixer) once the
  mixer lands — same lowering, new provider entry.
- Voice selection helpers (best voice for a BCP-47 tag) as pure functions over
  descriptors.
