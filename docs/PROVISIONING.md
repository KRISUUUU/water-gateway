# Provisioning

## Overview

On first boot (or after factory reset), the device enters **provisioning mode**
because no WiFi credentials are configured. In this mode, the device starts a
WiFi Access Point and serves a simplified configuration page.

## Provisioning Flow

1. Device boots, `ConfigStore` loads config
2. `AppCore` checks if WiFi SSID is empty → enters provisioning mode
3. WiFi AP starts with SSID `WMBus-GW-Setup` (open network)
4. HTTP server starts on `192.168.4.1:80`
5. User connects to AP, opens `http://192.168.4.1/`
6. Provisioning page displays form for:
   - WiFi SSID and password
   - MQTT broker host and port
   - Device hostname
   - Admin password (mandatory, sets initial hash)
7. User submits form → config is validated and saved to NVS
8. Device reboots into normal mode

## Provisioning Page

The provisioning page is a minimal HTML form (not the full web panel).
It is served directly by the HTTP server without requiring SPIFFS access,
embedded as a const string in the provisioning component.

## Security During Provisioning

- The AP is open (no WPA) for ease of initial setup
- The provisioning page is only served when the device is in provisioning mode
- Normal mode does not expose the provisioning endpoint
- The admin password is set during provisioning (not left as default)
- After provisioning completes, the AP is shut down

## Re-Provisioning

To re-enter provisioning mode:
1. Use the web panel: System → Factory Reset
2. Or: hold a reset button for 10 seconds (if hardware supports it — GPIO-based)
3. Or: erase NVS via serial console (`idf.py erase-flash`)

## Provisioning State Machine

```
IDLE → ACTIVE (AP started, serving form) → COMPLETED (config saved) → REBOOT
```

If provisioning fails (invalid input), the device remains in ACTIVE state
and redisplays the form with error messages.
