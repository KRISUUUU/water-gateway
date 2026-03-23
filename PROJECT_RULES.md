# PROJECT_RULES.md

## Architecture
- Keep strict separation between RF, MQTT, HTTP/UI, auth, OTA, config, diagnostics, and storage.
- Do not create god-modules.
- Do not mix heavy logic into ISR.
- Keep public interfaces small and explicit.
- Prefer clean boundaries over convenience.
- Avoid hidden coupling.

## Code Quality
- Clear naming only.
- No magic numbers without justification.
- Comments should explain intent, constraints, or non-obvious behavior.
- No vague TODOs.
- If a placeholder is needed, explain exactly what is missing and why.

## Reliability
- Handle failures explicitly.
- Design for Wi-Fi reconnects, MQTT reconnects, radio recovery, OTA recovery, and partial failures.
- Track counters for failures and recoveries.
- Prefer bounded buffers and controlled queues.
- No silent failure paths.

## Security
- Never log secrets.
- Redact secrets in logs, UI, config export, and support bundle.
- Protect admin endpoints.
- Validate all external input.
- Treat OTA and config import as sensitive operations.
- Prefer safe defaults.

## Configuration
- All config must be versioned.
- All config changes must be validated.
- Support migrations between versions.
- Separate user config from runtime state where practical.

## Testing
- Non-hardware logic should be host-testable when practical.
- New logic should include tests or explicit test notes.
- If something is not testable, explain why and how to test it manually or with HIL.

## Documentation
- Keep docs aligned with implementation.
- Update docs when contracts or schemas change.
- Document limitations honestly.

## Change Discipline
- Do not silently change architecture.
- Do not break contracts without explicit explanation.
- Refactors must explain benefits and affected dependencies.
