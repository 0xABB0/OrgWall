# Security, identity, power & events — OS-surface atlas (finer grain)
> domains D62–D69 + D80 (content-protection). Consumed by 00-atlas.md. Capability ceiling, axis-neutral; platform APIs are hints.

### D62 · permission — runtime consent & authorization
def: the cross-cutting consent gate in front of privacy-sensitive resources.
- **gated resources**: camera · microphone · location (precise/coarse) · contacts · calendar · photos/media · notifications · motion/activity · BLE/Bluetooth · nearby-devices/wifi-scan · tracking/ad-id (ATT) · files/folders · health · speech/recognition · screen-recording · accessibility · clipboard-read
- **status query**: undetermined · granted · denied · restricted (parental/MDM) · limited/partial (selected-photos) · blocked-cannot-prompt
- **scope & lifetime**: when-in-use vs always · one-time / "allow once" · session-scoped · precise vs approximate toggle · foreground-only grant · re-prompt eligibility window
- **request flow**: pre-prompt rationale UI · system-prompt trigger · purpose strings / declared rationale (Info.plist usage strings / manifest) · incremental / grouped request · denial → settings deep-link · revocation & re-request
- **privacy indicators**: in-use indicator (mic/camera dot/LED) · recent-access log · indicator-active query · per-resource active-use signal
- **runtime gating**: per-permission group · permission auto-reset on disuse · rationale-should-show signal
↑beyond: MDM/managed forced grants · enterprise consent bypass · privacy-manifest required-reason APIs
apps: every app touching a gated resource (cross-cuts D24/D37/D38/D45…).
status: none.

### D63 · credstore — keychain, secrets & secure hardware
def: storing secrets and using hardware-backed keys.
- **secret store**: generic / password items · internet / server-credential items · token & cookie storage · query / update / delete · item attributes & search (Keychain / Credential Manager / Secret Service / Keystore)
- **access control**: per-item ACL · biometric-gated unlock · device-passcode-gated · access-when-unlocked / after-first-unlock / this-device-only classes · app-presence requirement · invalidate-on-biometry-change
- **certificate & identity**: certificate store · identity (cert+key) items · client-cert selection · trust evaluation / chain validation · cert pinning store
- **hardware-backed keys**: key generation in secure hardware (Secure Enclave / TPM / StrongBox) · sign / decrypt / key-agreement without key extraction · key-protection level query (TEE vs StrongBox vs software) · biometric-bound key use
- **attestation**: key attestation cert chain · hardware-key provenance · attestation challenge / nonce
- **sharing & sync**: access-group / app-group shared items · keychain-sync / iCloud-keychain awareness · enterprise / managed credential provisioning · credential-provider extension surface
↑beyond: PIV / smartcard (CTK / WebAuthn-CTAP) · FIDO2 device keys · remote attestation services
apps: password managers, banking, enterprise, anything with tokens.
status: none.

### D64 · crypto — cryptographic primitives & random
def: the OS-provided cryptography and entropy.
- **CSPRNG**: secure random bytes (getrandom / SecRandomCopyBytes / RtlGenRandom-BCryptGenRandom / `crypto.getRandomValues`) · entropy-source readiness / blocking · uuid/guid generation
- **hashing**: SHA-2 · SHA-3 · BLAKE? · MD5/SHA-1 (legacy) · HMAC · incremental / streaming digest
- **symmetric & AEAD**: AES-GCM · AES-CBC/CTR · ChaCha20-Poly1305 · key-wrap · nonce/IV handling
- **asymmetric**: RSA (sign/encrypt/OAEP/PSS) · ECDSA / ECDH (P-256/384/521) · Ed25519 / X25519 · curve & key-size query
- **KDF & passwords**: HKDF · PBKDF2 · Argon2? / scrypt? · bcrypt? · salt handling
- **sign / verify**: signature generation & verification · detached vs attached · cert-chain-bound signing
- **acceleration & assurance**: hardware-accelerated primitives (AES-NI / ARMv8-crypto) · hardware-backed key binding (→D63) · constant-time guarantee surface · FIPS-mode / approved-algorithm awareness
↓under: raw cipher cores / CPU crypto extensions (→D07)
apps: secure messaging, crypto wallets, DRM, auth.
status: spawn (`digest`/`hash`/`rng`/`guid` partial domains).

### D65 · authn — sign-in, biometrics & passkeys
def: authenticating the user via OS mechanisms.
- **biometric / device auth**: face · fingerprint · iris? · enrollment & availability query · lockout / fallback state · biometry-type query (Face ID / Touch ID / Windows Hello / BiometricPrompt / LocalAuthentication)
- **fallback auth**: device passcode / PIN / pattern · password fallback · policy "biometric-or-device-credential"
- **passkeys / WebAuthn**: platform passkey create / assert · FIDO2 / CTAP2 · security-key (cross-device / hybrid) · attestation conveyance · resident / discoverable credentials · autofill / conditional-UI passkey (Credential Manager / AuthenticationServices / WebAuthn)
- **federated / platform sign-in**: Sign in with Apple · Google / Smart Lock · ASWebAuthenticationSession / Custom Tabs OAuth · saved password autofill · associated-domain credential sharing
- **auth context**: LAContext reuse / freshness window · presence / liveness signal · evaluated-policy query · re-authentication interval
↑beyond: enterprise SSO / Kerberos · attestation-conveyance to RP · cross-device hybrid transport
apps: banking, enterprise, any login, password managers.
status: none.

### D66 · integrity — attestation & anti-tamper
def: proving the app/device is genuine and untampered.
- **app attestation**: hardware-key app attestation (App Attest) · device legitimacy token (DeviceCheck) · Play Integrity verdict · SafetyNet (legacy) · nonce / challenge flow · server-verification boundary
- **device-integrity signals**: root / jailbreak indicators (as reported) · bootloader-unlock state · emulator / virtual-device detection · developer-mode / debug flag · hooking / instrumentation presence (Frida-class, best-effort)
- **code & process integrity**: code-signature / library validation (hardened runtime) · checksum / binary-tamper detection · debugger-attached detection · anti-debug / ptrace-deny
- **boot & platform state**: secure-boot / verified-boot state where exposed · measured-boot / TPM PCR query? · system-integrity-protection state
- **licensing**: store receipt / purchase validation · subscription / entitlement check · license-validation hooks
↑beyond: remote attestation services · TEE-backed device identity · hardware-attested boot measurements
apps: banking, anti-cheat, DRM, enterprise compliance.
status: none.

### D80 · content-protection — DRM & secure media path
def: gating playback of protected content and enforcing output protection.
- **DRM systems**: Widevine (L1/L2/L3) · FairPlay Streaming · PlayReady · ClearKey · license acquisition / server boundary · key-rotation · offline-license persistence
- **secure media path**: protected decode (secure decoder) · protected GPU surface · trusted display path
- **output protection**: HDCP enforcement & version · screen-capture / screenshot blocking (FLAG_SECURE / capture-exclusion) · external-display gating
- **hardware root**: TEE / secure-enclave-backed keys · key attestation · hardware-bound device identity
- **EME (web)**: Encrypted Media Extensions · CDM (Widevine/PlayReady/FairPlay) · robustness levels
↑beyond: TEE OSes (Trusty / OP-TEE) · L1 vs L3 robustness tiers · SVP secure-video-path
apps: streaming (Netflix / Disney+ / Spotify) · premium video / audio · enterprise DRM
status: none.

### D67 · power — battery, charging & energy policy
def: the device's energy state and the app's duty to respect it (MEL-ENGINE-VI).
- **battery state**: charge level (%) · charging / discharging / full / unknown · battery present / removable · battery health & capacity? · temperature where exposed · time-to-empty / time-to-full estimate?
- **power source**: AC / battery / UPS · charger type (USB / AC / wireless)? · charging-rate / fast-charge awareness · plugged-in change events
- **energy policy modes**: low-power / battery-saver mode & change events · app-standby bucket / energy-impact tier · background-restricted state · data-saver coupling
- **idle / sleep prevention**: display-on assertion · system-sleep prevention · idle-timer disable (IOPMAssertion / wake-lock / SetThreadExecutionState / Screen Wake Lock) · assertion lifetime & release · assertion enumeration
- **screen state**: screen-on request · keep-awake while-visible · brightness-coupling awareness
↑beyond: per-process energy accounting · charging-rate control? · power-profile (performance/balanced) hints
apps: media playback, games, long-running tasks, fitness.
status: spawn (`power` domain).

### D68 · thermal — thermal state & throttling
def: heat pressure and graceful degradation signals.
- **thermal state**: pressure levels (nominal → fair → serious → critical) · state-change notifications (thermalState / PowerManager thermal-status / `navigator`? n/a) · sustained-performance hint
- **per-component temperature**: CPU / GPU / battery / skin temperature where exposed · headroom query? · thermal-zone enumeration (sysfs)
- **throttling signals**: frequency-cap / clock-throttle notification · throttling-status query · recommended work-reduction signal · sustained-performance mode request
- **mitigation callbacks**: app-directed degradation request · thermal-mitigation event · cooldown / recovery notification
↓under: sysfs thermal zones / hwmon · raw temperature sensors (→D36)
apps: games, cameras, ML, sustained-load apps.
status: spawn (`thermal`/`temperature` domains).

### D69 · sysevents — session, sleep/wake & lifecycle
def: machine-level events the app reacts to.
- **sleep / wake**: system suspend / resume · display-sleep / display-wake · will-sleep veto / delay window · dark-wake / maintenance-wake awareness · clock-jump-after-wake
- **lock / screensaver**: screen lock / unlock · screensaver start / stop · secure-desktop transition? · session-lock event
- **session lifecycle**: login / logout · user fast-switch (attach / detach / activate) · remote-session (RDP / SSH) connect / disconnect · console-vs-remote query
- **shutdown / restart**: shutdown / restart / logout notification · veto / delay window (query-end-session) · power-off vs reboot distinction · save-state request before quit
- **system broadcasts**: time-change · timezone-change · locale / config-change (→D60) · DST transition · network-config-change (→D42)
- **autostart**: launch-on-login / boot · login-item / startup registration & query · "will terminate" / app-quit warning
↓under: D-Bus login1 / systemd-logind signals · session-change WTS notifications
apps: media (pause on sleep), security (lock on idle), agents, sync.
status: spawn (`event`/`signal` partial domains).
