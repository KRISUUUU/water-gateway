# Security

This document will describe:
- auth/session model
- secret handling
- input validation expectations
- OTA security considerations
- configuration import/export safety
- support bundle redaction rules
- threat model

## Initial Direction

Security goals:
- no secret leakage in logs/UI/export
- protected administrative actions
- safe defaults
- explicit treatment of attack surfaces
