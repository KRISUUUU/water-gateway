# PROJECT_RULES.md

## 1. Current Project Phase
The repository is now in a combined Phase D + E:
- user/product layer completion
- hardening
- near-premium readiness
- still honest about missing hardware validation

Do not fall back into random feature sprawl.
Work toward coherent product value.

## 2. Scope Discipline
Priority is now:
- detected meters
- watchlist
- aliases and filtering
- live telegram UX
- better MQTT/HA flow
- OTA/auth/security hardening
- long-run stability
- warning cleanup
- docs/release polish

Still do NOT drift into:
- giant vendor-specific protocol decoding
- excessive frontend complexity
- fake “verified RF” claims
- random product features not aligned with the gateway mission

## 3. Architecture Rules
- Keep strict separation between RF, MQTT, HTTP/UI, auth, OTA, config, diagnostics, and storage.
- `app_core` remains an orchestrator.
- User/product features must sit on top of existing clean module boundaries.
- Do not create god-modules.
- Do not mix board wiring into unrelated layers.
- Do not put logic into the wrong layer just for convenience.

## 4. Product Rules
The UI and API must feel like a real gateway product:
- useful
- clear
- honest
- serviceable

No fake buttons, fake metrics, or fake states.

If something is not fully implemented or hardware-validated:
- say so in the UI/API/docs where appropriate

## 5. Detected Meters / Watchlist Rules
- Build detected meters on honest observable data from the gateway.
- Do not fake perfect meter decoding if not actually supported.
- Watchlist entries may include alias, note/tag, enabled state.
- Filtering and recent/live telegram UX should integrate with detected meters/watchlist cleanly.

## 6. OTA / Security Rules
- Do not claim OTA upload works unless it truly works.
- OTA URL/upload/API/UI/docs must stay aligned.
- Protect admin actions.
- Never log secrets.
- Redact secrets in logs, UI, export, and support bundles.
- Keep auth/session behavior explicit and safe.

## 7. Reliability Rules
- Improve health, logs, counters, watchdog, and recovery visibility.
- Avoid silent runtime degradation.
- Prefer explicit warnings and operational clarity.

## 8. Testing / Build Rules
- Keep host tests passing.
- Keep ESP-IDF build passing.
- Keep web asset packaging/build coherent.
- Clean up warnings where practical, especially ones that risk future breakage.
- Keep OTA partition margin in mind.

## 9. Documentation Rules
- Docs must match code reality.
- If hardware is required to validate something, say so clearly.
- Keep operations/troubleshooting practical and useful.
- Polish docs toward a serious release-quality project.

## 10. Change Discipline
- Small safe patches are preferred.
- Do not silently break working provisioning/build/static web flow.
- Do not overclaim readiness.
- Refactors must improve clarity and maintainability.