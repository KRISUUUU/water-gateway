$ErrorActionPreference = "Stop"

function Write-FileUtf8 {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Content
    )

    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }

    Set-Content -Path $Path -Value $Content -Encoding utf8
}

Write-FileUtf8 "README.md" @'
# Water Gateway

Production-minded ESP-IDF firmware for ESP32 + CC1101 acting as a Wireless M-Bus 868 MHz RF gateway for water meter style telegrams.

## Project Goal

This project implements a robust ESP32 + CC1101 firmware focused on:

- receiving Wireless M-Bus telegrams on 868 MHz
- collecting raw frames and metadata
- publishing raw telegrams and gateway telemetry outward
- providing a built-in web panel for service and diagnostics
- supporting OTA updates with rollback thinking
- supporting first-boot provisioning
- integrating with MQTT / Home Assistant / external decoders such as wmbusmeters

## Architectural Direction

This project follows **Variant B**:

- ESP32 + CC1101 is primarily a robust RF receiver and gateway
- heavy meter-specific decoding remains external by default
- the firmware focuses on RF, transport, diagnostics, configuration, OTA, and operational visibility

## Current Status

Repository scaffold / architecture phase.

See:
- `PROJECT_RULES.md`
- `PROJECT_BRIEF.md`
- `docs/ARCHITECTURE.md`
- `docs/REPO_LAYOUT.md`

## Repository Structure

- `main/` - application entrypoint
- `components/` - firmware components
- `docs/` - architecture, operational, and implementation documentation
- `tests/` - host-side tests and fixtures
- `web/` - static assets for the embedded web UI

## Build Notes

This project is intended to use ESP-IDF.

Build and setup instructions will be expanded as implementation progresses.

## Quality Goals

- modular architecture
- explicit interfaces
- resilience and recoverability
- stable MQTT contracts
- serviceable web UI
- OTA + rollback
- clear docs
- host-testable logic where practical
'@

Write-FileUtf8 "PROJECT_RULES.md" @'
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
'@

Write-FileUtf8 "PROJECT_BRIEF.md" @'
# PROJECT_BRIEF.md

## Goal
Build a production-minded ESP-IDF firmware for ESP32 + CC1101 as a Wireless M-Bus 868 MHz receiver/gateway for water meter style telegrams.

## Architecture
Variant B:
- ESP32 + CC1101 receives radio frames and publishes raw telegrams + metadata + telemetry.
- Heavy meter-specific decoding remains external by default.
- Firmware focuses on RF reception, transport, diagnostics, OTA, configuration, and serviceability.

## Core Features
- Wireless M-Bus raw frame reception
- RF metadata collection
- MQTT publishing
- built-in web panel
- OTA upload + HTTPS OTA + rollback
- first-boot provisioning
- auth-protected service UI
- diagnostics and metrics
- support bundle export
- clean integration with HA / MQTT / external decoders

## Non-goals
- giant vendor-specific full decoder
- throwaway prototype
- ESPHome-based implementation
- monolithic architecture

## Quality Expectations
- modular design
- clear contracts
- strong reliability thinking
- secure defaults
- testable non-hardware logic
- good operational documentation
- maintainable over years
'@

Write-FileUtf8 "CMakeLists.txt" @'
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(water_gateway)
'@

Write-FileUtf8 "sdkconfig.defaults" @'
# Project defaults for ESP-IDF
# These values are intentionally conservative starter defaults.
# They may be refined later as architecture and feature set solidify.

CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"

# Logging
CONFIG_LOG_DEFAULT_LEVEL_INFO=y

# OTA support expectations
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y

# HTTP server/Web UI expectations
CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024
CONFIG_HTTPD_MAX_URI_LEN=512

# MQTT and networking baseline
CONFIG_LWIP_DNS_SUPPORT_MDNS_QUERIES=y

# Enable mDNS component compatibility where applicable
CONFIG_MDNS_MAX_INTERFACES=3
'@

Write-FileUtf8 "partitions.csv" @'
# Name,     Type, SubType, Offset,   Size,     Flags
nvs,        data, nvs,     0x9000,   0x6000,
otadata,    data, ota,     0xf000,   0x2000,
phy_init,   data, phy,     0x11000,  0x1000,
factory,    app,  factory, 0x20000,  0x180000,
ota_0,      app,  ota_0,   0x1A0000, 0x180000,
ota_1,      app,  ota_1,   0x320000, 0x180000,
storage,    data, spiffs,  0x4A0000, 0x160000,
'@

Write-FileUtf8 ".clang-format" @'
BasedOnStyle: LLVM
IndentWidth: 4
TabWidth: 4
UseTab: Never
ColumnLimit: 100
BreakBeforeBraces: Attach
AllowShortFunctionsOnASingleLine: Empty
AllowShortIfStatementsOnASingleLine: false
AllowShortLoopsOnASingleLine: false
PointerAlignment: Left
SortIncludes: true
SpaceBeforeParens: ControlStatements
Cpp11BracedListStyle: true
'@

Write-FileUtf8 ".editorconfig" @'
root = true

[*]
charset = utf-8
end_of_line = lf
insert_final_newline = true
indent_style = space
indent_size = 4
trim_trailing_whitespace = true

[*.md]
trim_trailing_whitespace = false

[*.yml]
indent_size = 2

[*.yaml]
indent_size = 2

[*.json]
indent_size = 2
'@

Write-FileUtf8 ".gitignore" @'
# ESP-IDF
build/
sdkconfig
sdkconfig.old
dependencies.lock
managed_components/

# CMake / IDE
CMakeFiles/
CMakeCache.txt
compile_commands.json

# Python / virtual env
.venv/
venv/
__pycache__/
*.pyc

# Test / coverage artifacts
coverage/
*.gcda
*.gcno
*.profraw
*.profdata

# OS / editor
.DS_Store
.vscode/
.idea/

# Logs / temp
*.log
tmp/
'@

Write-FileUtf8 "main/CMakeLists.txt" @'
idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES app_core
)
'@

Write-FileUtf8 "main/app_main.cpp" @'
#include "app_core/app_core.hpp"

extern "C" void app_main(void) {
    app_core::AppCore app;
    app.start();
}
'@

Write-FileUtf8 "docs/ARCHITECTURE.md" @'
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
'@

Write-FileUtf8 "docs/REPO_LAYOUT.md" @'
# Repository Layout

## Top Level

- `main/` - application entrypoint
- `components/` - modular firmware components
- `docs/` - architecture, operations, testing, and design documentation
- `tests/` - host-side tests and fixtures
- `web/` - static files for embedded web UI

## Components

Each component should:
- own one focused responsibility
- expose a clear public API
- keep implementation details private
- avoid hidden coupling

## Docs

Documentation is expected to evolve with implementation and remain aligned with the code.
'@

Write-FileUtf8 "docs/CONFIGURATION.md" @'
# Configuration

This document will describe:
- configuration model
- config versioning
- validation rules
- migration strategy
- import/export behavior
- secret handling rules

## Initial Direction

Configuration will be:
- persisted in NVS
- versioned
- validated before commit
- designed for migration across firmware versions
'@

Write-FileUtf8 "docs/MQTT_TOPICS.md" @'
# MQTT Topics

This document will define the stable MQTT contract for the gateway.

## Initial Direction

Planned topic families:
- gateway status
- gateway telemetry
- gateway events
- raw telegram stream
- service/debug topics if needed

Detailed topic and payload definitions will be added during MQTT implementation.
'@

Write-FileUtf8 "docs/WEB_UI.md" @'
# Web UI

This document will describe:
- page structure
- API contract
- authentication model
- operational workflows
- diagnostics workflows

## Planned Pages

- dashboard
- live telegrams
- RF diagnostics
- MQTT diagnostics
- configuration
- OTA
- system/service
- logs
'@

Write-FileUtf8 "docs/OTA.md" @'
# OTA

This document will describe:
- partition strategy
- local upload OTA
- HTTPS URL OTA
- rollback behavior
- post-update health checks
- operational recovery

## Initial Direction

OTA support is a first-class feature of this project and is expected to include rollback-aware design.
'@

Write-FileUtf8 "docs/SECURITY.md" @'
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
'@

Write-FileUtf8 "docs/PROVISIONING.md" @'
# Provisioning

This document will describe:
- first boot behavior
- setup mode entry
- Wi-Fi and MQTT initial setup
- transition to normal operating mode

## Initial Direction

If configuration is missing, the device should enter setup/provisioning mode and allow basic configuration before normal operation.
'@

Write-FileUtf8 "docs/TESTING.md" @'
# Testing

This document will describe:
- host-testable modules
- unit test scope
- replay tests
- integration-like tests
- manual/HIL testing areas
- CI workflow expectations

## Initial Direction

Non-hardware logic should be host-testable where practical.
Hardware-specific behavior should be clearly isolated and documented.
'@

Write-FileUtf8 "docs/OPERATIONS.md" @'
# Operations

This document will describe:
- normal operational behavior
- diagnostics workflow
- log usage
- OTA workflow
- recommended recovery flows
- expected service actions

## Initial Direction

Operational clarity is a project goal, not an afterthought.
'@

Write-FileUtf8 "docs/TROUBLESHOOTING.md" @'
# Troubleshooting

This document will describe common problem categories such as:
- Wi-Fi issues
- MQTT issues
- radio issues
- OTA failures
- web UI access issues
- provisioning issues

Detailed troubleshooting guidance will be added as implementation progresses.
'@

Write-FileUtf8 "docs/LIMITATIONS.md" @'
# Limitations

This document tracks:
- known protocol limitations
- hardware-related constraints
- CC1101-specific compromises
- implementation tradeoffs
- deferred features

## Initial Direction

The ESP32 + CC1101 gateway is intended to be robust and serviceable, but some protocol and hardware limitations may require careful practical compromises.
'@

Write-FileUtf8 "web/index.html" @'
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Water Gateway</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="styles.css">
</head>
<body>
  <main>
    <h1>Water Gateway</h1>
    <p>Web UI scaffold.</p>
    <div id="app"></div>
  </main>
  <script src="app.js"></script>
</body>
</html>
'@

Write-FileUtf8 "web/app.js" @'
document.addEventListener("DOMContentLoaded", () => {
  const app = document.getElementById("app");
  if (app) {
    app.textContent = "UI scaffold initialized.";
  }
});
'@

Write-FileUtf8 "web/styles.css" @'
body {
  font-family: sans-serif;
  margin: 0;
  padding: 2rem;
  background: #f6f7f9;
  color: #222;
}

main {
  max-width: 960px;
  margin: 0 auto;
}
'@

Write-FileUtf8 "components/web_ui/README.md" @'
# Web UI Component

This directory documents the embedded web UI asset strategy.

Current static assets live in `/web`.

As the project evolves, this area should describe:
- asset build strategy if introduced
- route/page structure
- API/UI integration expectations
- constraints for keeping the UI maintainable
'@

Write-FileUtf8 "tests/README.md" @'
# Tests

This directory contains host-side tests and fixtures.

## Goals

- validate host-testable logic
- keep contracts stable
- support replay-oriented testing for frame pipeline logic
- catch regressions early

## Initial Scope

Planned tests include:
- config validation
- config migration
- dedup logic
- MQTT payload generation
- health logic
- minimal frame pipeline logic
'@

Write-FileUtf8 "tests/fixtures/README.md" @'
# Test Fixtures

Fixture files in this directory are used for replay-style tests.

## Planned Usage

- raw frame replay
- metadata extraction tests
- dedup tests
- routing behavior tests
'@

Write-FileUtf8 "tests/fixtures/sample_frames.json" @'
[
  {
    "name": "placeholder_frame_1",
    "raw_hex": "AABBCCDDEEFF",
    "note": "Placeholder fixture to be replaced with realistic replay data later."
  }
]
'@

Write-FileUtf8 "tests/host/CMakeLists.txt" @'
cmake_minimum_required(VERSION 3.16)
project(water_gateway_host_tests LANGUAGES CXX)

enable_testing()
'@

Write-FileUtf8 ".github/workflows/ci.yml" @'
name: CI

on:
  push:
  pull_request:

jobs:
  basic-checks:
    runs-on: ubuntu-latest
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: List repository
        run: find . -maxdepth 3 | sort

      - name: Placeholder
        run: echo "CI scaffold created. Build/test steps to be added as implementation progresses."
'@

Write-FileUtf8 "components/common/CMakeLists.txt" @'
idf_component_register(
    INCLUDE_DIRS "include"
)
'@

Write-FileUtf8 "components/common/include/common/error.hpp" @'
#pragma once

#include <cstdint>

namespace common {

enum class ErrorCode : std::uint16_t {
    Ok = 0,

    InvalidArgument,
    InvalidState,
    NotInitialized,
    AlreadyInitialized,
    NotFound,
    NotSupported,
    Timeout,
    Busy,
    NoMemory,
    StorageFailure,
    ValidationFailed,
    SerializationFailed,
    DeserializationFailed,
    AuthFailed,
    AccessDenied,
    NetworkFailure,
    MqttFailure,
    HttpFailure,
    OtaFailure,
    RadioFailure,
    RecoveryTriggered,
    InternalError
};

}  // namespace common
'@

Write-FileUtf8 "components/common/include/common/result.hpp" @'
#pragma once

#include "common/error.hpp"

namespace common {

template <typename T>
class Result {
public:
    Result(const T& value)
        : ok_(true), value_(value), error_(ErrorCode::Ok) {
    }

    Result(T&& value)
        : ok_(true), value_(static_cast<T&&>(value)), error_(ErrorCode::Ok) {
    }

    Result(ErrorCode error)
        : ok_(false), value_(), error_(error) {
    }

    [[nodiscard]] bool ok() const {
        return ok_;
    }

    [[nodiscard]] const T& value() const {
        return value_;
    }

    [[nodiscard]] T& value() {
        return value_;
    }

    [[nodiscard]] ErrorCode error() const {
        return error_;
    }

private:
    bool ok_;
    T value_;
    ErrorCode error_;
};

template <>
class Result<void> {
public:
    Result()
        : ok_(true), error_(ErrorCode::Ok) {
    }

    Result(ErrorCode error)
        : ok_(false), error_(error) {
    }

    [[nodiscard]] bool ok() const {
        return ok_;
    }

    [[nodiscard]] ErrorCode error() const {
        return error_;
    }

private:
    bool ok_;
    ErrorCode error_;
};

}  // namespace common
'@

Write-FileUtf8 "components/common/include/common/types.hpp" @'
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace common {

using TimestampMs = std::uint64_t;
using Milliseconds = std::uint32_t;
using ByteBuffer = std::vector<std::uint8_t>;

enum class LogSeverity : std::uint8_t {
    Debug = 0,
    Info,
    Warn,
    Error
};

enum class SystemMode : std::uint8_t {
    Unconfigured = 0,
    Provisioning,
    Normal,
    Maintenance
};

struct BuildInfo {
    std::string version;
    std::string git_revision;
    std::string build_time_utc;
};

struct DeviceIdentity {
    std::string device_name;
    std::string hostname;
    std::string hardware_model;
    std::string firmware_name;
};

}  // namespace common
'@

Write-FileUtf8 "components/app_core/CMakeLists.txt" @'
idf_component_register(
    SRCS "src/app_core.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common event_bus config_store
)
'@

Write-FileUtf8 "components/app_core/include/app_core/app_core.hpp" @'
#pragma once

namespace app_core {

class AppCore {
public:
    AppCore() = default;
    ~AppCore() = default;

    void start();

private:
    void initialize_core_services();
    void determine_start_mode();
    void start_runtime();
};

}  // namespace app_core
'@

Write-FileUtf8 "components/app_core/src/app_core.cpp" @'
#include "app_core/app_core.hpp"

#include "config_store/config_store.hpp"
#include "event_bus/event_bus.hpp"

namespace app_core {

void AppCore::start() {
    initialize_core_services();
    determine_start_mode();
    start_runtime();
}

void AppCore::initialize_core_services() {
    event_bus::EventBus::instance().initialize();
    config_store::ConfigStore::instance().initialize();
}

void AppCore::determine_start_mode() {
    // Placeholder:
    // In later steps this should decide between provisioning mode,
    // normal mode, and possibly maintenance/recovery mode based on
    // config state, health info, and OTA boot status.
}

void AppCore::start_runtime() {
    // Placeholder:
    // In later steps this should start the orchestrated runtime flow
    // for services, tasks, radio pipeline, connectivity, and UI.
}

}  // namespace app_core
'@

Write-FileUtf8 "components/event_bus/CMakeLists.txt" @'
idf_component_register(
    SRCS "src/event_bus.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common
)
'@

Write-FileUtf8 "components/event_bus/include/event_bus/event_bus.hpp" @'
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "common/error.hpp"
#include "common/result.hpp"

namespace event_bus {

enum class EventType : std::uint16_t {
    None = 0,
    SystemStartup,
    ConfigLoaded,
    ConfigInvalid,
    ProvisioningRequested,
    WifiStateChanged,
    MqttStateChanged,
    RadioStateChanged,
    TelegramReceived,
    OtaStateChanged,
    WarningRaised,
    ErrorRaised
};

struct Event {
    EventType type{EventType::None};
    std::string topic;
    std::string payload;
};

using EventHandler = std::function<void(const Event&)>;

class EventBus {
public:
    static EventBus& instance();

    common::Result<void> initialize();
    common::Result<std::uint32_t> subscribe(EventType type, EventHandler handler);
    common::Result<void> unsubscribe(EventType type, std::uint32_t subscription_id);
    common::Result<void> publish(const Event& event);

private:
    EventBus() = default;

    bool initialized_{false};
    std::uint32_t next_subscription_id_{1};
    std::map<EventType, std::map<std::uint32_t, EventHandler>> handlers_;
};

}  // namespace event_bus
'@

Write-FileUtf8 "components/event_bus/src/event_bus.cpp" @'
#include "event_bus/event_bus.hpp"

namespace event_bus {

EventBus& EventBus::instance() {
    static EventBus bus;
    return bus;
}

common::Result<void> EventBus::initialize() {
    if (initialized_) {
        return common::Result<void>(common::ErrorCode::AlreadyInitialized);
    }

    initialized_ = true;
    return common::Result<void>();
}

common::Result<std::uint32_t> EventBus::subscribe(EventType type, EventHandler handler) {
    if (!initialized_) {
        return common::Result<std::uint32_t>(common::ErrorCode::NotInitialized);
    }

    if (!handler) {
        return common::Result<std::uint32_t>(common::ErrorCode::InvalidArgument);
    }

    const std::uint32_t id = next_subscription_id_++;
    handlers_[type][id] = handler;
    return common::Result<std::uint32_t>(id);
}

common::Result<void> EventBus::unsubscribe(EventType type, std::uint32_t subscription_id) {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    auto type_it = handlers_.find(type);
    if (type_it == handlers_.end()) {
        return common::Result<void>(common::ErrorCode::NotFound);
    }

    auto& subscriptions = type_it->second;
    auto sub_it = subscriptions.find(subscription_id);
    if (sub_it == subscriptions.end()) {
        return common::Result<void>(common::ErrorCode::NotFound);
    }

    subscriptions.erase(sub_it);
    return common::Result<void>();
}

common::Result<void> EventBus::publish(const Event& event) {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    auto type_it = handlers_.find(event.type);
    if (type_it == handlers_.end()) {
        return common::Result<void>();
    }

    for (const auto& [_, handler] : type_it->second) {
        if (handler) {
            handler(event);
        }
    }

    return common::Result<void>();
}

}  // namespace event_bus
'@

Write-FileUtf8 "components/config_store/CMakeLists.txt" @'
idf_component_register(
    SRCS
        "src/config_store.cpp"
        "src/config_validation.cpp"
        "src/config_migration.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common
)
'@

Write-FileUtf8 "components/config_store/include/config_store/config_models.hpp" @'
#pragma once

#include <cstdint>
#include <string>

namespace config_store {

inline constexpr std::uint32_t kCurrentConfigVersion = 1;

struct DeviceConfig {
    std::string device_name{"water-gateway"};
    std::string hostname{"water-gateway"};
    std::string admin_username{"admin"};
    std::string admin_password_hash{};
};

struct WifiConfig {
    bool enabled{true};
    std::string ssid{};
    std::string password{};
};

struct MqttConfig {
    bool enabled{true};
    std::string broker_host{};
    std::uint16_t broker_port{1883};
    std::string username{};
    std::string password{};
    std::string client_id{"water-gateway"};
    std::string topic_prefix{"watergw"};
};

struct RadioConfig {
    double frequency_mhz{868.95};
    std::int32_t frequency_offset_hz{0};
    bool publish_raw_frames{true};
    bool publish_diagnostics{true};
};

struct LoggingConfig {
    std::uint8_t level{1};
};

struct AppConfig {
    std::uint32_t version{kCurrentConfigVersion};
    DeviceConfig device{};
    WifiConfig wifi{};
    MqttConfig mqtt{};
    RadioConfig radio{};
    LoggingConfig logging{};
};

}  // namespace config_store
'@

Write-FileUtf8 "components/config_store/include/config_store/config_validation.hpp" @'
#pragma once

#include <string>
#include <vector>

#include "config_store/config_models.hpp"

namespace config_store {

struct ValidationIssue {
    std::string field;
    std::string message;
};

struct ValidationResult {
    bool valid{true};
    std::vector<ValidationIssue> issues{};
};

ValidationResult validate_config(const AppConfig& config);

}  // namespace config_store
'@

Write-FileUtf8 "components/config_store/include/config_store/config_migration.hpp" @'
#pragma once

#include "common/result.hpp"
#include "config_store/config_models.hpp"

namespace config_store {

common::Result<AppConfig> migrate_to_current(const AppConfig& input);

}  // namespace config_store
'@

Write-FileUtf8 "components/config_store/include/config_store/config_store.hpp" @'
#pragma once

#include "common/result.hpp"
#include "config_store/config_models.hpp"
#include "config_store/config_validation.hpp"

namespace config_store {

class ConfigStore {
public:
    static ConfigStore& instance();

    common::Result<void> initialize();
    common::Result<AppConfig> load() const;
    common::Result<void> save(const AppConfig& config);
    common::Result<void> reset_to_defaults();

    [[nodiscard]] bool is_initialized() const;
    [[nodiscard]] bool has_valid_config() const;

private:
    ConfigStore() = default;

    bool initialized_{false};
    AppConfig current_config_{};
    bool has_valid_config_{false};
};

}  // namespace config_store
'@

Write-FileUtf8 "components/config_store/src/config_validation.cpp" @'
#include "config_store/config_validation.hpp"

namespace config_store {

ValidationResult validate_config(const AppConfig& config) {
    ValidationResult result{};

    if (config.device.device_name.empty()) {
        result.valid = false;
        result.issues.push_back({"device.device_name", "Device name must not be empty"});
    }

    if (config.device.hostname.empty()) {
        result.valid = false;
        result.issues.push_back({"device.hostname", "Hostname must not be empty"});
    }

    if (config.mqtt.enabled) {
        if (config.mqtt.broker_host.empty()) {
            result.valid = false;
            result.issues.push_back({"mqtt.broker_host", "MQTT broker host must not be empty"});
        }

        if (config.mqtt.broker_port == 0) {
            result.valid = false;
            result.issues.push_back({"mqtt.broker_port", "MQTT broker port must be non-zero"});
        }

        if (config.mqtt.topic_prefix.empty()) {
            result.valid = false;
            result.issues.push_back({"mqtt.topic_prefix", "MQTT topic prefix must not be empty"});
        }
    }

    if (config.radio.frequency_mhz < 300.0 || config.radio.frequency_mhz > 1000.0) {
        result.valid = false;
        result.issues.push_back({"radio.frequency_mhz", "Radio frequency is outside supported range"});
    }

    return result;
}

}  // namespace config_store
'@

Write-FileUtf8 "components/config_store/src/config_migration.cpp" @'
#include "config_store/config_migration.hpp"

namespace config_store {

common::Result<AppConfig> migrate_to_current(const AppConfig& input) {
    AppConfig migrated = input;

    if (migrated.version == 0) {
        migrated.version = kCurrentConfigVersion;
    }

    if (migrated.version != kCurrentConfigVersion) {
        return common::Result<AppConfig>(common::ErrorCode::NotSupported);
    }

    return common::Result<AppConfig>(migrated);
}

}  // namespace config_store
'@

Write-FileUtf8 "components/config_store/src/config_store.cpp" @'
#include "config_store/config_store.hpp"

#include "config_store/config_migration.hpp"

namespace config_store {

ConfigStore& ConfigStore::instance() {
    static ConfigStore store;
    return store;
}

common::Result<void> ConfigStore::initialize() {
    if (initialized_) {
        return common::Result<void>(common::ErrorCode::AlreadyInitialized);
    }

    current_config_ = AppConfig{};
    const auto validation = validate_config(current_config_);
    has_valid_config_ = validation.valid;
    initialized_ = true;

    return common::Result<void>();
}

common::Result<AppConfig> ConfigStore::load() const {
    if (!initialized_) {
        return common::Result<AppConfig>(common::ErrorCode::NotInitialized);
    }

    return common::Result<AppConfig>(current_config_);
}

common::Result<void> ConfigStore::save(const AppConfig& config) {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    const auto migrated = migrate_to_current(config);
    if (!migrated.ok()) {
        return common::Result<void>(migrated.error());
    }

    const auto validation = validate_config(migrated.value());
    if (!validation.valid) {
        return common::Result<void>(common::ErrorCode::ValidationFailed);
    }

    current_config_ = migrated.value();
    has_valid_config_ = true;

    return common::Result<void>();
}

common::Result<void> ConfigStore::reset_to_defaults() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    current_config_ = AppConfig{};
    has_valid_config_ = validate_config(current_config_).valid;

    return common::Result<void>();
}

bool ConfigStore::is_initialized() const {
    return initialized_;
}

bool ConfigStore::has_valid_config() const {
    return has_valid_config_;
}

}  // namespace config_store
'@

Write-Host "Starter files written successfully."