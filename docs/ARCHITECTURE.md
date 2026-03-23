# Architecture

## Overview

This project implements an ESP32 + CC1101 Wireless M-Bus 868 MHz gateway.

Architectural direction:
- ESP32 + CC1101 acts primarily as an RF receiver and gateway
- heavy meter-specific decoding remains external by default
- firmware focuses on RF capture, transport, diagnostics, configuration, OTA, and serviceability

## Key Principles

- strict separation of concerns
- no heavy ISR logic
- event-driven boundaries where useful
- explicit contracts between modules
- versioned configuration
- diagnostics-first design
- secure handling of secrets
- host-testable non-hardware logic where practical

## Major Layers

- boot / app orchestration
- core/common types and error handling
- configuration
- connectivity services
- radio + frame pipeline
- MQTT transport
- HTTP/UI/auth
- OTA
- diagnostics / health / support bundle

## Current State

Initial scaffold. Detailed architecture will be expanded during implementation stages.
