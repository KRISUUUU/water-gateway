# Security

## Threat Model

### Asset Inventory

| Asset | Sensitivity | Storage |
|-------|------------|---------|
| WiFi credentials | High | NVS (encrypted if enabled) |
| MQTT credentials | High | NVS |
| Admin password hash | High | NVS |
| Session tokens | Medium | RAM only |
| Device config (non-secret) | Low | NVS |
| Watchlist aliases/notes | Low-Medium | SPIFFS (`/storage/watchlist.db`) |
| Raw telegrams | Low | Transient (RAM queue) |
| Diagnostic counters | Low | RAM |
| Log buffer | Low-Medium | RAM (may contain operational details) |
| Firmware image | Medium | Flash OTA partitions |

### Threat Actors

1. **Local network attacker** — Has network access to the device (same LAN segment).
   Most relevant threat for a local IoT device.
2. **Physical attacker** — Has physical access to the ESP32 board.
   Can read flash, connect UART, etc.
3. **Compromised MQTT broker** — Malicious messages from broker.
4. **Malicious OTA source** — Serves tampered firmware images.

### Attack Surface

| Surface | Entry Point | Threats | Mitigations |
|---------|------------|---------|-------------|
| Web panel (HTTP) | Port 80, all endpoints | Unauthorized access, CSRF, XSS, brute force | Auth required, session tokens, input validation, no inline JS eval |
| MQTT client | Outbound connection | Credential theft, message injection | Credentials in NVS (not logged), optional TLS, validate broker cert |
| OTA upload | `/api/ota/upload` | Malicious firmware, denial of service | Auth required, binary content-type checks, image-size checks, OTA validation + rollback |
| OTA URL | `/api/ota/url` | MITM, malicious firmware | HTTPS scheme required, auth required, image validation (certificate trust posture depends on build/runtime TLS config) |
| Config import | `/api/config` POST | Malformed config, resource exhaustion | Validation before persistence, size limits |
| Config export | `/api/config` GET | Credential leakage | Secret fields redacted in response |
| Support bundle | `/api/support-bundle` | Credential leakage | Secrets redacted, auth required |
| Serial/UART | Physical console | Full device access | Out of scope for firmware mitigation; document physical security |
| NVS storage | Flash read (physical) | Credential extraction | Optional NVS encryption (documented, not default) |
| RF interface | 868 MHz radio | Spoofed/replayed frames | Not mitigated at gateway level (gateway is a receiver; external decoders handle validation) |

### Threat Analysis

#### T1: Unauthorized Web Panel Access
- **Risk:** High (local attacker can reconfigure device, trigger OTA, read diagnostics)
- **Mitigation:**
  - Management `/api/*` endpoints require valid session token
  - Only startup/auth bootstrap endpoints are unauthenticated: `/api/bootstrap`, `/api/auth/login`
  - Session token is 32 random bytes (hex-encoded), not guessable
  - Session expires after configurable timeout (default 1 hour)
  - Failed login attempts are rate-limited (max 5 per minute)
  - If no admin password hash exists yet, passwordless bootstrap login is allowed only while provisioning mode is active
  - Operator should set `auth.admin_password` immediately and reboot
  - Password change endpoint requires current password when a password is already set
  - API responses set defensive headers (`Cache-Control: no-store`, `Pragma: no-cache`, `X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`, `Referrer-Policy: no-referrer`, restrictive API CSP)

#### T2: Credential Leakage via Logs/Export/UI
- **Risk:** High (leaked WiFi/MQTT credentials compromise network)
- **Mitigation:**
  - Secret fields are never included in `ESP_LOG*` output
  - Config export replaces secrets with `"***"`
  - Support bundle redacts all secret fields and exports only meter/watchlist counts
  - Web UI API never returns plaintext passwords
  - Error messages do not include credential values

#### T3: Malicious OTA Firmware
- **Risk:** Critical (compromised firmware = full device control)
- **Mitigation:**
  - OTA endpoints require authentication
  - Image header validated (ESP-IDF app descriptor check)
  - Image size validated against partition size
  - Rollback on boot failure while app is still pending verification
  - Factory partition as ultimate fallback
  - URL OTA enforces HTTPS scheme at API layer; certificate trust details are deployment/build dependent
  - Future: firmware signing with secure boot v2

#### T4: MQTT Credential Theft
- **Risk:** Medium (attacker could subscribe to all topics if broker access gained)
- **Mitigation:**
  - MQTT credentials stored in NVS, never logged
  - Optional TLS for MQTT connection
  - Credentials redacted in all export paths
  - Broker-side ACLs recommended in operational docs

#### T5: Config Import Injection
- **Risk:** Medium (malformed config could crash device or change credentials)
- **Mitigation:**
  - All imported config is validated before persistence
  - Field length limits enforced
  - Invalid config rejected with specific error messages
  - Config version compatibility checked before import

#### T6: Session Fixation / Hijacking
- **Risk:** Medium (local network attacker could steal session token)
- **Mitigation:**
  - Session token generated from cryptographic random source (`esp_fill_random`)
  - Token transmitted via HTTP header (not URL parameter)
  - Single active session (new login invalidates previous)
  - Session expiry enforced server-side
  - Note: Without HTTPS, token can be sniffed on LAN. HTTPS for web panel
    is a documented future enhancement.

#### T7: Physical Flash Extraction
- **Risk:** Low-Medium (physical access required)
- **Mitigation:**
  - NVS encryption available in ESP-IDF (documented as optional hardening)
  - Secure boot v2 available (documented as optional hardening)
  - These are not enabled by default due to provisioning complexity

## Security Design Rules

1. **Auth required for management API.** All management endpoints require a bearer token. Only `/api/bootstrap` and `/api/auth/login` are unauthenticated startup/auth endpoints. Passwordless bootstrap login is limited to provisioning mode (`wifi` not configured and no admin hash); normal-mode passwordless login is rejected.
2. **Secrets never logged.** Any `ESP_LOG*` call involving config data must use redacted versions.
3. **Secrets never exported.** Config export, support bundle, and API responses replace secret fields with `"***"`.
4. **Validate before persist.** No config change is saved without passing validation.
5. **Validate before boot.** OTA images are validated before marking as bootable.
6. **Fail closed.** If session validation fails, deny access. If config validation fails, keep old config.
7. **Bounded inputs.** All HTTP request bodies, config fields, and MQTT payloads have size limits.
8. **Minimal attack surface.** No telnet, no FTP, no debug endpoints in production. Only HTTP:80 and MQTT outbound.

## Password Storage

- Admin password stored as SHA-256 hash with 16-byte random salt
- Hash format: `salt_hex:hash_hex` (32 + 1 + 64 = 97 chars)
- On login, compute `SHA-256(salt + password)` and compare with stored hash
- ESP-IDF provides `mbedtls_sha256` for hashing and `esp_fill_random` for salt generation

## Session Management

- Session token: 32 random bytes, hex-encoded (64 characters)
- Generated on successful login via `esp_fill_random`
- Stored in RAM only (lost on reboot, which is acceptable)
- Validated on every authenticated request by comparing with stored active token
- Expired tokens are rejected; client must re-login
- Only one active session at a time
- Token comparison uses constant-time check to reduce timing side-channel risk
- Auth state is mutex-protected in firmware to avoid race conditions under concurrent HTTP requests

## HTTP Response Hardening

Repository-confirmed behavior:

- API JSON responses (`/api/*`) include:
  - `Cache-Control: no-store, no-cache, must-revalidate, max-age=0`
  - `Pragma: no-cache`
  - `Expires: 0`
  - `X-Content-Type-Options: nosniff`
  - `X-Frame-Options: DENY`
  - `Referrer-Policy: no-referrer`
  - `Content-Security-Policy: default-src 'none'; frame-ancestors 'none'`
- Static web responses include:
  - `Cache-Control: no-store, max-age=0`
  - `Pragma: no-cache`
  - `X-Content-Type-Options: nosniff`
  - `X-Frame-Options: DENY`
  - `Referrer-Policy: no-referrer`

This hardening improves caching and content-type safety, but does not replace transport security. The web/admin plane is still HTTP (no TLS) by default.

## Optional Hardening (Documented, Not Default)

These features are available in ESP-IDF but not enabled by default because they
add provisioning complexity:

1. **NVS Encryption** — Protects credentials at rest against flash extraction.
   Requires encryption key management during manufacturing.
2. **Secure Boot v2** — Ensures only signed firmware can run.
   Requires key management and signing infrastructure.
3. **Flash Encryption** — Encrypts entire flash contents.
   Requires careful key provisioning; one-time fuse burn on production devices.
4. **HTTPS for Web Panel** — Protects session tokens and API traffic.
   Requires certificate provisioning; self-signed certs cause browser warnings.

## Operational Security Guidance

1. Place the device on an isolated IoT VLAN when possible.
2. Use MQTT broker authentication and ACLs.
3. Set `auth.admin_password` immediately after provisioning and reboot.
4. If the device is accessible from untrusted networks, consider enabling NVS encryption.
5. Regularly export config backups (with secrets redacted) for documentation.
6. Monitor the device's MQTT status topic for unexpected offline events.
7. Keep firmware updated via OTA when security patches are released.

## Build-Time Security Posture Visibility

The firmware reports compile-time hardening posture in diagnostics:

- startup logs (`app_core`)
- `GET /api/status` under `security.build`
- support bundle under `security_posture`

Reported fields:

- `secure_boot_enabled`
- `flash_encryption_enabled`
- `nvs_encryption_enabled`
- `anti_rollback_enabled`
- `ota_rollback_enabled`
- `production_hardening_ready` (all of the above enabled)

This is a build-time signal only. It does not prove efuse state on a deployed
device and must not be treated as manufacturing attestation.

## Repository-Confirmed Security Posture

From repository defaults (`sdkconfig.defaults`) only:

- OTA rollback: enabled (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`)
- NVS encryption: disabled by default (`CONFIG_NVS_ENCRYPTION=n`)
- Secure Boot / Flash Encryption / anti-rollback: not explicitly enabled in repository defaults

Production enablement of these features depends on release configuration and
device provisioning decisions outside repository-only analysis.
