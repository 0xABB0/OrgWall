# Connectivity & networking — OS-surface atlas (finer grain)
> domains D41–D49. Consumed by 00-atlas.md. Capability ceiling, axis-neutral; platform APIs are hints.

### D41 · net-socket — transport sockets
def: the BSD/Winsock socket transport layer.
- **transport**: · TCP · UDP · raw (ICMP/IP) · QUIC-as-UDP (UDP-GSO/GRO segmentation offload) · SCTP? (wasm: no raw sockets — WebRTC data-channel / WebSocket only, hint via `?`)
- **address-family**: · IPv4 · IPv6 · dual-stack (v4-mapped) · Unix-domain (AF_UNIX, see D50)
- **socket-options**: · TCP_NODELAY · SO_RCVBUF/SO_SNDBUF · SO_KEEPALIVE & idle/interval/probe · SO_REUSEADDR/SO_REUSEPORT · IP_TOS/DSCP/IPV6_TCLASS · TTL/hop-limit
- **multicast**: · group join/leave (IP_ADD_MEMBERSHIP) · source-specific (SSM) · interface select · loopback & TTL scope · broadcast (SO_BROADCAST)
- **connection**: connect · listen · accept · half-close/shutdown
- **resolution**: getaddrinfo / getnameinfo · happy-eyeballs ordering
- **readiness**: non-blocking · integrate with D04 (epoll/kqueue/IOCP/GCD)
- **fd-passing**: SCM_RIGHTS / credential passing over UDS (boundary with D50)
- **hints**: congestion-control select (CC algo) · bandwidth/pacing hints · path-MTU query
↓under: raw / packet sockets (AF_PACKET / BPF / pcap) · eBPF socket filters / XDP
apps: servers · games (netcode) · P2P · VoIP · custom protocols
status: spawn (`net` domain; `apps/hello-net`)

### D42 · net-iface — interfaces, reachability & policy
def: knowing the network environment and its changes.
- **enumeration**: interface list · per-iface addresses (v4/v6/link-local) · MAC/hardware addr · MTU · flags (up/loopback/p2p)
- **reachability**: · link up/down events · route-to-host reachability (SCNetworkReachability / ConnectivityManager / NetworkInformation API) · default-route presence
- **connection-type**: · wifi · cellular · ethernet · loopback · other (NWPathMonitor / NetworkCapabilities transport)
- **cost-flags**: · metered/expensive · constrained (Low-Data Mode) · roaming · temporary/restricted
- **vpn**: VPN presence & tunnel-iface detection (boundary; tunnel provisioning out of scope?)
- **captive-portal**: detection & needs-sign-in state
- **proxy**: · system proxy config · PAC URL / auto-config · per-scheme proxy (read-only)
- **policy**: per-app network rules awareness · data-usage accounting (per-app/per-iface byte counters)
apps: sync clients · streaming (adaptive on metered) · offline-aware apps
status: none

### D43 · net-discovery — name resolution & service discovery
def: finding hosts and services by name on the Internet and the LAN.
- **dns**: · A/AAAA · SRV · TXT · PTR/reverse · CNAME · DoH/DoT-encrypted awareness (read system resolver policy) · custom resolver/nameserver select?
- **mdns**: · browse · resolve · `.local` link-local resolution (Bonjour / NSD / Avahi / dns-sd)
- **dns-sd**: · service register/advertise (publish) · TXT-record metadata · domain enumeration · unregister
- **ssdp-upnp**: · SSDP M-SEARCH discovery · UPnP device/service description (boundary; control out of scope) · WS-Discovery?
- **link-local**: link-local addr resolution · zero-conf rendezvous
- wasm: no raw mDNS/DNS — discovery limited to fetch/WebSocket to known hosts (hint `?`)
apps: Chromecast/AirPlay-style discovery · LAN multiplayer · IoT setup
status: none

### D44 · net-http — HTTP stack & web transport
def: the platform's batteries-included HTTP/TLS client (and server boundary).
- **versions**: · HTTP/1.1 · HTTP/2 (multiplexing) · HTTP/3 (QUIC) · ALPN negotiation
- **tls**: · version/cipher config · certificate pinning · client certs (mTLS) · custom trust/CA · OCSP/revocation awareness · SNI
- **semantics**: · cookies & cookie store · redirect following & policy · auth (Basic/Digest/Bearer/NTLM/Negotiate) · conditional/cache headers
- **body**: · streaming upload · streaming download · range/partial (resume) · multipart · form-urlencoded · chunked transfer
- **websocket**: · client connect · ping/pong · frame send/recv · close (WHATWG `WebSocket` on wasm) · per-message-deflate
- **integration**: · system proxy · system HTTP cache · system cookie jar
- **background-transfer**: · discretionary/background download/upload (NSURLSession background · WorkManager · DownloadManager) · resume on relaunch
- wasm: WHATWG `fetch` + streams; no raw socket control, no pinning, CORS-gated (hint `?`)
↑beyond: HTTP/3 0-RTT · ECH (encrypted client hello) · connection coalescing · server push · early hints (103)
apps: every networked app · downloaders · API clients · browsers
status: spawn (`http`/`server` domains; `design/http2-client.md`)

### D45 · bluetooth — Bluetooth Classic & BLE
def: short-range wireless device communication.
- **adapter**: · state (on/off/unavailable) · power toggle (where permitted) · address/name query · authorization/permission
- **scan**: · discover devices · filter by service UUID · scan modes (low-latency/balanced/low-power) · advertise (peripheral) · background scanning & state restoration
- **pairing**: · bond/unbond · security levels (LE Secure Connections/legacy) · passkey/numeric-comparison · just-works · pairing UI prompts
- **gatt-client**: · service discovery · characteristic read/write (with/without response) · notify/indicate subscribe · descriptor (CCCD) · MTU negotiate
- **gatt-server**: · publish services/characteristics (peripheral role) · respond to read/write · send notifications
- **l2cap**: connection-oriented channels (CoC) for streaming
- **classic-profiles**: · A2DP · HFP/HSP · HID · SPP · MAP · PBAP? (CoreBluetooth = BLE only; Classic via external-accessory / Android BluetoothProfile)
- **connection**: · RSSI · connection params (interval/latency) · PHY (1M/2M/coded) · roles (central/peripheral/beacon)
- wasm: Web Bluetooth — GATT client only, user-gesture & origin gated, no advertise/scan-passive (hint `?`)
↑beyond: HCI raw access · BLE long-range / 2M PHY · channel sounding (distance) · LE Audio / Auracast?
apps: wearables · IoT · audio devices · fitness sensors · beacons
status: none

### D46 · radio-wifi — Wi-Fi & local radios
def: managing and ranging over Wi-Fi (and adjacent radios).
- **scan**: · scan results (SSID/BSSID/RSSI/channel/band) · scan trigger · capabilities (security/PHY)
- **connection-info**: · current SSID/BSSID · link speed/RSSI · frequency/band · IP config
- **join**: · programmatic join with consent (NEHotspotConfiguration · WifiNetworkSpecifier/Suggestion) · known-network management · forget
- **direct-aware**: · Wi-Fi Direct (P2P) · Wi-Fi Aware / NAN (neighbor discovery & data path)
- **soft-ap**: hotspot / tethering enable & config (gated)
- **ranging**: · 802.11mc RTT / FTM round-trip ranging · UWB ranging (NearbyInteraction / Android UWB, where present)
- **qos**: WMM / QoS / traffic-category hints
- wasm: no Wi-Fi surface at all (hint `?` / deny)
↑beyond: monitor mode · raw 802.11 frame capture/inject · vendor ranging extensions
apps: setup flows · proximity · indoor positioning · file transfer
status: none

### D47 · telephony — cellular, SMS & calls
def: the mobile-network and telephony surface.
- **carrier-info**: · carrier name/MCC/MNC · network operator · roaming state (CoreTelephony / TelephonyManager)
- **signal**: · signal strength/level · radio tech (GSM/LTE/5G-NR/NSA-SA) · cell info (where permitted)
- **sms-mms**: · send SMS · receive SMS (consent/default-app gated) · MMS · delivery reports · multipart concat (Android SmsManager; iOS compose-UI only, hint `?`)
- **calls**: · call state (idle/ringing/offhook) · CallKit / ConnectionService VoIP integration (incoming/outgoing call UI) · audio-session handoff (boundary D25) · call directory/blocking extension
- **sim-esim**: · SIM info/state · multi-SIM/subscription enumeration · eSIM provisioning (carrier-entitlement gated)
- **data-permission**: cellular-data usage permission/policy
- **emergency**: emergency/SOS call constraints & detection (read-only)
- **identity**: phone-number / IMEI / SIM-serial (heavily gated; mostly denied on modern OS, hint `?`)
- wasm: no telephony surface (deny)
apps: messaging · dialer replacements · VoIP (CallKit) · 2FA SMS readers
status: none

### D48 · usb — USB & peripheral buses
def: enumerating and talking to bus-attached peripherals from userspace.
- **enumeration**: · device list · hotplug attach/detach events · VID/PID/class match · serial/manufacturer strings
- **descriptors**: · device descriptor · configuration · interface(s) & alt-settings · endpoint descriptors · string descriptors
- **transfers**: · control · bulk · interrupt · isochronous (with packet framing/timing)
- **claiming**: · open device · set configuration · claim/release interface · select alt-setting · clear-halt/reset
- **roles**: · host mode · device/gadget mode (peripheral) · OTG role-swap
- **access**: · permission/consent prompt · exclusive vs shared claim · detach kernel driver (libusb)
- **web**: WebUSB — origin & user-gesture gated, blocklisted classes denied (hint `?`)
- **fabric**: Thunderbolt / PCIe device awareness (enumeration only, boundary)
↓under: libusb / WinUSB / IOUSBHost raw paths · USB-gadget configfs (device-mode composition)
apps: instrument control · firmware flashers · capture dongles · printers
status: none

### D49 · serial-bus — serial, GPIO & embedded buses
def: low-level wire protocols, mostly for embedded/industrial hosts.
- **serial-uart**: · open port (CDC-ACM / FTDI / `/dev/ttyS*`,`/dev/ttyUSB*`) · baud · parity/data-bits/stop-bits · flow control (RTS/CTS/XON-XOFF) · break/line-state (DTR/DSR/RI/CD) · RS-232/RS-485 mode
- **gpio**: · line read/write · direction/pull config · edge-interrupt events · bias/drive (`/dev/gpiochip`, libgpiod)
- **i2c**: master read/write · address · clock-rate · repeated-start (`/dev/i2c-*`)
- **spi**: master transfer · mode/CPOL-CPHA · bits-per-word · CS/speed (`/dev/spidev*`)
- **can**: · CAN / CAN-FD frames · filters/masks · bitrate (SocketCAN, where present)
- **pwm**: channel duty/period/enable
- **other**: 1-Wire · pin-mux awareness
- web: WebSerial — port pick (user-gesture/origin gated), no GPIO/I²C/SPI/CAN (hint `?` / deny)
↓under: `/dev/gpiochip` · `/dev/i2c` · `/dev/spidev` · `/dev/ttyS*` · FTDI direct — the under-OS embedded surface (the camera baresensor precedent)
apps: robotics · makers · lab automation · industrial control
status: none
