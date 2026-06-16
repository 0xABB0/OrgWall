# Accessibility, speech, ML, XR & user-data services — OS-surface atlas (finer grain)
> domains D70–D79 (D76–D79 promoted to full domains). Consumed by 00-atlas.md. Capability ceiling, axis-neutral; platform APIs are hints.

---

## Band XV — Accessibility, speech & assistive

### D70 · a11y — accessibility tree & assistive control
def: expose UI semantics to assistive tech, and react to it.
- **tree**: element hierarchy · roles · states (busy/expanded/selected/disabled) · values · labels & hints (UIAccessibility / AccessibilityNodeInfo / NSAccessibility / UIAutomation / AT-SPI)
- **actions**: standard (activate/increment/scroll) · custom actions · escape/magic-tap (UIAccessibilityCustomAction)
- **focus**: focus order · navigation grouping · focus set/track · container traversal
- **announce**: live-region · layout-changed · value/screen-changed notifications · priority/politeness
- **input-routes**: switch control · voice control · full-keyboard access · gesture-to-action hooks
- **query-state**: screen-reader running · AT-present · element-focused query
- **display-assist**: magnification/zoom region · caption presence · differentiate-without-color awareness
↑beyond: serving as an AT *client* (driving other apps' trees) · UIAutomation provider+pattern model
↓under: raw AT-SPI/D-Bus bus traffic
apps: every app (compliance) · screen readers · switch/voice control utilities · UI-test automation
status: none

### D71 · speech-tts — text-to-speech synthesis
def: turn text into spoken audio via OS voices.
- **voices**: enumerate by language/locale · quality tier (compact/enhanced/premium) · gender · novelty · personal-voice (AVSpeechSynthesisVoice / SpeechSynthesizer / TextToSpeech)
- **prosody**: rate · pitch · volume · pre/post-utterance delay
- **markup**: SSML · phoneme/IPA input · pronunciation overrides
- **callbacks**: word/range boundary · will-speak/did-finish · pause/resume/cancel events
- **delivery**: speak-to-output vs synthesize-to-buffer/file · audio-route & ducking interplay (D25)
- **source**: on-device vs network voices · downloadable voice assets
apps: readers · navigation · accessibility · audiobooks · language learning
status: spawn (`tts` domain; `apps/hello-speech`)

### D72 · speech-stt — speech recognition & voice
def: turn speech into text and react to voice triggers.
- **engine**: on-device vs server recognition · supported-locale enumeration · model download (SFSpeechRecognizer / SpeechRecognizer / Web Speech)
- **results**: streaming partial vs final · N-best alternatives · per-word confidence · word timing/segments
- **mode**: free dictation vs command/contextual-strings biasing · custom vocabulary/hints
- **endpointing**: voice-activity detection · auto vs manual stop · silence timeout
- **trigger**: wake-word/"hey" hooks · always-listening session
- **consent**: mic authorization · on-device-only privacy mode · usage limits/throttling
apps: voice assistants · dictation · accessibility · transcription · voice-command UIs
status: spawn (`stt` domain)

---

## Band XVI — On-device intelligence

### D73 · ml-infer — neural inference & accelerators
def: run ML models on CPU / GPU / NPU through OS runtimes.
- **runtime**: backend selection (Core ML / NNAPI / DirectML / WebNN / TFLite-delegate) · model format load/import
- **accelerator**: enumerate & select (NPU/ANE · GPU · CPU) · capability & supported-op query · op-fallback policy
- **model-prep**: compile/optimize · quantize · cache compiled artifact
- **tensor-io**: feed/fetch tensors · zero-copy from camera/GPU surfaces (D21/D38) · layout/dtype conversion
- **execution**: sync vs async/batched dispatch · multi-input/output · partial-graph run
- **power**: perf vs low-power mode · thermal-aware throttling hint (D68)
↑beyond: vendor NPU SDKs (ANE-direct · Qualcomm/MediaTek/Hexagon) · op fallback orchestration
apps: photo apps · AR · transcription · on-device LLMs · creative tools
status: none

### D74 · ml-vision — OS vision / NL detectors
def: the platform's built-in perception (the metadata camera defers downstream).
- **face-body**: face detect/landmarks · body/skeleton pose · hand pose · gaze/attention (Vision / MLKit)
- **scene**: object/scene classification · saliency/attention regions · animal/subject detection
- **text**: OCR/text recognition · document structure/layout analysis · handwriting
- **segmentation**: foreground matte · person/hair/sky segmentation · subject lift/cutout
- **embeddings**: image feature print · text embedding · similarity/feature distance
- **codes**: barcode/QR detection (shared with D40) · rectangle/document detection
- **nl**: tokenization · language identification · named-entity · sentiment · lemmatization (Natural Language / MLKit)
- **audio**: sound classification/event detection (SoundAnalysis)
apps: photo organizers · scanners · accessibility · AR · content moderation
status: none

---

## Band XVII — Immersive / XR / spatial

### D75 · xr — head/hand tracking & spatial scene
def: virtual / augmented / mixed-reality surfaces.
- **session**: create/configure · reference spaces (local/stage/unbounded) · world-tracking state & relocalization (OpenXR / ARKit / ARCore / WebXR / RealityKit)
- **tracking**: head pose · eye/gaze tracking · hand joints/skeleton · body/full-skeleton tracking
- **controllers**: pose · buttons/axes · controller haptics · interaction profiles
- **world**: plane detection · scene mesh/reconstruction · anchors & persistence · raycast/hit-test · image/object/face tracking
- **passthrough**: passthrough blend mode · environment occlusion · lighting estimation
- **anchors-share**: spatial-anchor persistence · cloud/shared anchors · co-location
- **render**: per-eye views & projection · foveated rendering · compositor layers (quad/cylinder/equirect) · reprojection/timewarp
- **audio**: spatial-audio integration (D23)
↑beyond: OpenXR vendor extensions · raw eye-tracking streams · depth-sensor/LiDAR access
apps: AR/VR apps · spatial productivity · immersive games · visionOS apps
status: spawn (`design/xr.md`)

---

## Band XVIII — User-data services

### D76 · pim — contacts, calendar & reminders
def: read and write the OS personal-information stores under consent.
- **contacts**: read/write records · fields (name/phone/email/postal/photo) · groups · vCard import/export (Contacts / ContactsContract)
- **calendar**: calendars enumeration · events read/write · recurrence/exceptions · attendees & invites/RSVP · alarms/reminders-on-event (EventKit / CalendarContract)
- **reminders-tasks**: task lists · reminders/todos with due/priority · completion state (EventKit reminders)
- **accounts**: account-scoped containers · source/calendar provenance · default-store selection
- **consent**: per-store authorization (full vs limited/write-only) · contact-picker without grant
- **observe**: change notifications · external-modification refresh
↑beyond: CardDAV/CalDAV sync boundary · contact-picker UI without full grant
apps: calendars · CRM/contacts apps · schedulers · task managers · email clients
status: none

### D77 · media-library — photos, music & media assets
def: read and write the OS photo, music, and media-asset libraries under consent.
- **photo-assets**: enumerate assets · fetch image/video data · live-photos · burst/RAW · depth/portrait · screenshots/screen-recordings (PhotoKit / MediaStore)
- **photo-write**: add/save asset · create/edit albums · favorites · delete (with prompt)
- **photo-edit**: non-destructive edit/adjustment data · revert · content-editing extensions
- **access-tiers**: full vs limited/selected-photos · add-only · per-selection picker without library grant
- **music-media**: music/media-item library query · playlists · artwork · play-count/rating metadata (MPMediaLibrary / MediaStore)
- **metadata**: EXIF · GPS/location · capture date · orientation · creation/modification times
- **observe**: library change observation · incremental change details (insert/delete/update)
↑beyond: iCloud/cloud-asset download & progress · original-vs-rendered version access
apps: photo organizers · editors · social/sharing apps · music players · backup tools
status: none

### D78 · health-wallet — health, fitness & wallet
def: read and write health/fitness data and manage wallet passes under per-type consent.
- **metrics**: read/write quantity/category samples (steps · heart rate · HRV · blood-oxygen · weight · glucose) (HealthKit / Health Connect)
- **workouts**: workout sessions & routes · activity/energy/distance · live workout (watch) · segments
- **sleep-mind**: sleep stages · mindfulness/state-of-mind · respiratory
- **consent**: per-data-type read/write authorization · authorization-status opacity (read-grant not revealable) · background delivery
- **wallet-passes**: add/update passes · pass enumeration · boarding/event/loyalty/coupon (PassKit / Google Wallet)
- **wallet-payment**: payment-card provisioning boundary · in-app payment-sheet handoff (gated)
- **observe**: sample/anchored-query updates · pass-change notifications
↑beyond: clinical-records/FHIR import · ECG/raw-waveform access · in-app provisioning of secure-element cards
apps: fitness/activity trackers · health dashboards · medical apps · transit/loyalty/ticketing apps
status: none

### D79 · print — printing & page output
def: discover printers, configure jobs, and emit page output.
- **discover**: printer enumeration & default · capabilities (page sizes · duplex · color · resolution · trays · staple/finishing) · status/availability (NSPrintOperation / UIPrintInteractionController / AirPrint / IPP)
- **page-setup**: paper size & orientation · margins/imageable area · scaling/fit · range & copies/collation · N-up
- **job**: submit · monitor state (queued/printing/done/error) · cancel · multi-page rendering callback · per-page content
- **preview-ui**: system print panel/preview · printer-picker · interactive page setup
- **to-file**: print-to-PDF · export-to-image/page output · PDF page generation
↑beyond: raw IPP/spooler control · driverless-print attributes · direct CUPS/spooler queue access
apps: document editors · viewers · photo printing · point-of-sale/receipts · report generators
status: none
