# PROJECT_RULES.md

## 1. Scope Discipline
This repository is currently in a stabilization and pre-hardware-validation phase.
Before adding significant new features, prioritize:
- buildability
- API consistency
- architecture consistency
- safety/sanity
- documentation honesty
- test/CI realism

Do not start feature expansion while the repository is still internally inconsistent.

## 2. Architecture Rules
- Keep strict separation between RF, MQTT, HTTP/UI, auth, OTA, config, diagnostics, and storage.
- Do not create god-modules.
- `app_core` must remain an orchestrator, not a business-logic dump.
- Do not mix heavy logic into ISR.
- Keep public interfaces small and explicit.
- Prefer clean boundaries over convenience.
- Avoid hidden coupling.

## 3. Responsibility Boundaries
- RF and CC1101 behavior belongs in radio/pipeline layers.
- MQTT topic and payload logic belongs in MQTT-related modules.
- HTTP request handling belongs in API/HTTP modules.
- Auth/session logic belongs in auth modules.
- Config ownership belongs in config modules.
- OTA lifecycle belongs in OTA modules.
- Board-specific pin mapping must not live in orchestration logic.

## 4. Code Quality
- Clear naming only.
- No magic numbers without justification.
- Comments should explain intent, constraints, or non-obvious behavior.
- No vague TODOs.
- If a placeholder is necessary, explicitly label it as placeholder and describe what remains.

## 5. Reliability
- Handle failures explicitly.
- Design for Wi-Fi reconnects, MQTT reconnects, radio recovery, OTA recovery, and partial failures.
- Track counters for failures and recoveries.
- Prefer bounded buffers and controlled queues.
- No silent failure paths.

## 6. Security
- Never log secrets.
- Redact secrets in logs, UI, config export, and support bundle.
- Protect admin endpoints.
- Validate all external input.
- Treat OTA and config import/update as sensitive operations.
- Prefer safe defaults.
- Do not let redacted `"***"` values overwrite real secrets.

## 7. Configuration
- All config must be versioned.
- Config version is owned by the config system, not external clients.
- All config changes must be validated.
- Support migrations between versions.
- Separate user config from runtime state where practical.

## 8. Testing
- Non-hardware logic should be host-testable when practical.
- New logic should include tests or explicit test notes.
- If something is not testable, explain why and how to test it manually or with HIL.
- CI must reflect actual reality, not aspirational status.

## 9. Documentation
- Keep docs aligned with implementation.
- Update docs when contracts or schemas change.
- Document limitations honestly.
- Do not overclaim readiness.
- If a feature is partial or stubbed, docs must say so clearly.

## 10. Change Discipline
- Do not silently change architecture.
- Do not break contracts without explicit explanation.
- Refactors must explain benefits and affected dependencies.
- Small safe patches are preferred over giant rewrites.