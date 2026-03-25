# Provisioning

## Overview

On first boot (or after factory reset), the device enters **provisioning mode**
because no WiFi credentials are configured. In this mode, the device starts a
WiFi Access Point and starts HTTP on `192.168.4.1:80` so initial configuration
can be completed from the web UI/API over AP.

## Provisioning Flow

1. Device boots, `ConfigStore` loads config
2. `AppCore` checks if WiFi SSID is empty → enters provisioning mode
3. WiFi AP starts with SSID `WMBus-GW-Setup` (open network)
4. Auth service initializes (required because config endpoints are protected)
5. HTTP server starts on `192.168.4.1:80`
6. API handlers + static web handler are registered
7. User connects to AP, opens `http://192.168.4.1/`
8. User authenticates (`POST /api/auth/login`; first boot accepts any non-empty password)
9. User saves config (`POST /api/config`), including `auth.admin_password` when setting first admin password
10. API response indicates `reboot_required`
11. User reboots (`POST /api/system/reboot`)
12. Device boots into normal mode (with WiFi STA)

## Provisioning Interface

Provisioning currently uses the same HTTP/API/static handler stack as normal mode,
but over AP (`WMBus-GW-Setup`) instead of STA.

## Security During Provisioning

- The AP is open (no WPA) for ease of initial setup
- Provisioning is exposed only when WiFi is not configured
- Normal mode still exposes management APIs behind auth
- Admin password should be set during first provisioning save (`auth.admin_password`)
- If auth fields are changed later (`admin_password`, session timeout), API may return `relogin_required`
- After provisioning completes, the AP is shut down

## Re-Provisioning

To re-enter provisioning mode:

1. Use the web panel: System → Factory Reset
2. Or: hold a reset button for 10 seconds (if hardware supports it — GPIO-based)
3. Or: erase NVS via serial console (`idf.py erase-flash`)

## Provisioning State

```text
IDLE → ACTIVE (AP + HTTP ready) → COMPLETED (optional marker) → REBOOT
```

If config validation fails, API returns validation issues and the device remains
in ACTIVE mode.
