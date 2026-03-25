# PROJECT_RULES.md

## 1. Current Project Phase
The repository is currently beyond bootstrap and early stabilization.
The current phase is:
- technical product completion
- service/web usability completion
- honest pre-hardware productization

Before adding premium features, prioritize:
- provisioning end-to-end
- normal-mode end-to-end
- usable web panel
- clean diagnostics
- truthful OTA/config/auth behavior
- build/test/docs consistency

## 2. Scope Discipline
Do not start feature expansion while core product flows are incomplete.
Specifically, do NOT prioritize yet:
- detected meters UX
- watchlist UX
- advanced Home Assistant polish
- extra protocol/product features
- decorative frontend work

## 3. Architecture Rules
- Keep strict separation between RF, MQTT, HTTP/UI, auth, OTA, config, diagnostics, and storage.
- Do not create god-modules.
- `app_core` must remain an orchestrator.
- Runtime logic may be split into focused files, but responsibilities must stay clear.
- Board-specific wiring must remain outside orchestration logic.

## 4. Provisioning/Product Rules
Provisioning must be genuinely usable:
- AP starts
- web UI is reachable
- config submission works
- config is validated
- config is persisted
- device behavior after save is explicit

No fake provisioning UX is allowed.

## 5. Web/UI Rules
- Static assets must actually be packaged and served correctly.
- `/` must resolve correctly.
- Missing assets must log clearly.
- UI should be simple, robust, and service-oriented.
- No overdesigned frontend.
- No fake buttons for unavailable features.

## 6. Normal Mode Rules
Normal mode must be coherent:
- Wi-Fi config present -> normal mode path
- HTTP/API remains reachable
- auth remains coherent
- MQTT lifecycle is visible
- failures degrade clearly and safely

## 7. OTA Rules
- Do not pretend OTA upload works if it does not.
- URL OTA must be honest and clearly documented.
- OTA state must be consistent in code, API, and docs.

## 8. Security Rules
- Never log secrets.
- Redact secrets in logs, UI, config export, and support bundle.
- Protect admin endpoints.
- Validate all external input.
- Keep config redaction and secret overwrite behavior safe.

## 9. Testing and Docs
- Keep host tests passing.
- Keep ESP-IDF build passing.
- Docs must match code reality.
- If hardware validation is missing, say so clearly.

## 10. Change Discipline
- Prefer small safe patches over sweeping rewrites.
- Do not silently change architecture or contracts.
- Refactors must improve clarity, not just move code around.