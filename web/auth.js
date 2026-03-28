import {
    $,
    badgeClassByState,
    setHiddenIfPresent,
    text,
    toBool,
} from "./ui_common.js";
import { clearMsg, setMsg } from "./page_messages.js";

export function createAuthController({
    state,
    apiClient,
    bannerController,
    stopRefreshTimer,
    showApp,
    updateStatus,
}) {
    function isFirstBootProvisioning() {
        return !!(state.bootstrapInfo && state.bootstrapInfo.provisioning && !state.bootstrapInfo.password_set);
    }

    function showSetupScreen() {
        setHiddenIfPresent("#auth-startup-msg", true);
        setHiddenIfPresent("#login-form", true);
        setHiddenIfPresent("#setup-form", false);
        $("#login-subtitle").textContent =
            "Initial setup required. Configure Wi-Fi and admin password.";
    }

    function showSessionExpiredSignIn(message, kind) {
        stopRefreshTimer();
        state.token = "";
        sessionStorage.removeItem("wg_token");
        bannerController.clearStickyNotice();
        bannerController.clear();
        $("#app-shell").hidden = true;
        $("#login-page").hidden = false;
        setHiddenIfPresent("#auth-startup-msg", true);
        setHiddenIfPresent("#login-form", false);
        setHiddenIfPresent("#setup-form", true);
        $("#login-subtitle").textContent = "Sign in to manage your device.";
        if (message) {
            setMsg("#login-error", kind || "warning", message);
        } else {
            clearMsg("#login-error");
        }
    }

    function showStartupUnauthenticated(boot) {
        stopRefreshTimer();
        state.token = "";
        sessionStorage.removeItem("wg_token");
        bannerController.clearStickyNotice();
        bannerController.clear();
        $("#app-shell").hidden = true;
        $("#login-page").hidden = false;
        setHiddenIfPresent("#login-form", false);
        setHiddenIfPresent("#setup-form", true);
        if (boot && boot.bootstrap_failed) {
            $("#auth-startup-msg").hidden = false;
            $("#auth-startup-msg").textContent =
                "Unable to read bootstrap state. Showing sign in fallback.";
        } else {
            setHiddenIfPresent("#auth-startup-msg", true);
        }
        $("#login-subtitle").textContent = "Sign in to manage your device.";
    }

    function forceFirstBootSetup() {
        state.token = "";
        sessionStorage.removeItem("wg_token");
        bannerController.clearStickyNotice();
        bannerController.clear();
        $("#app-shell").hidden = true;
        $("#login-page").hidden = false;
        showSetupScreen();
    }

    function applyModeUi(mode) {
        const badge = $("#topbar-mode");
        badge.textContent = "mode: " + text(mode, "--");
        badge.className = badgeClassByState(mode);
        const subtitle = $("#login-subtitle");
        if (String(mode).toLowerCase() === "provisioning") {
            subtitle.textContent =
                "Provisioning mode detected. Use Initial Setup on first boot, or sign in if already configured.";
        } else {
            subtitle.textContent = "Sign in to manage your device.";
        }
    }

    function applySetupMqttEnabled(enabled) {
        ["#setup-mqtt-host", "#setup-mqtt-port", "#setup-mqtt-user", "#setup-mqtt-password"].forEach(
            (selector) => {
                const element = $(selector);
                element.disabled = !enabled;
            }
        );
    }

    async function bootstrap() {
        const timeoutMs = 5000;
        const controller = typeof AbortController !== "undefined" ? new AbortController() : null;
        let timeoutId = null;
        if (controller) {
            timeoutId = setTimeout(() => controller.abort(), timeoutMs);
        }
        const requestOptions = {
            cache: "no-store",
        };
        if (controller) {
            requestOptions.signal = controller.signal;
        }

        try {
            const data = await apiClient.requestJson("/api/bootstrap", requestOptions, {
                authorize: false,
                handleUnauthorized: false,
            });
            state.bootstrapInfo = {
                mode: data.mode || "unknown",
                provisioning: toBool(data.provisioning),
                password_set: toBool(data.password_set),
                bootstrap_failed: false,
            };
            return state.bootstrapInfo;
        } catch (_) {
            state.bootstrapInfo = {
                mode: "unknown",
                provisioning: false,
                password_set: true,
                bootstrap_failed: true,
            };
            return state.bootstrapInfo;
        } finally {
            if (timeoutId) {
                clearTimeout(timeoutId);
            }
        }
    }

    async function runInitialSetup() {
        clearMsg("#setup-msg");

        const ssid = $("#setup-ssid").value.trim();
        const wifiPassword = $("#setup-wifi-password").value;
        const adminPassword = $("#setup-admin-password").value;
        const mqttEnabled = $("#setup-mqtt-enabled").checked;
        const mqttHost = $("#setup-mqtt-host").value.trim();

        if (!ssid) {
            setMsg("#setup-msg", "error", "Wi-Fi SSID is required.");
            return;
        }
        if (!adminPassword) {
            setMsg("#setup-msg", "error", "Admin password is required.");
            return;
        }
        if (mqttEnabled && !mqttHost) {
            setMsg("#setup-msg", "error", "MQTT host is required when MQTT is enabled.");
            return;
        }

        setMsg("#setup-msg", "warning", "Saving initial setup...");

        const payload = {
            device: {
                name: $("#setup-device-name").value.trim(),
                hostname: $("#setup-hostname").value.trim(),
            },
            wifi: {
                ssid: ssid,
                password: wifiPassword,
            },
            auth: {
                admin_password: adminPassword,
            },
            mqtt: {
                enabled: mqttEnabled,
                host: mqttHost,
                username: $("#setup-mqtt-user").value.trim(),
                password: $("#setup-mqtt-password").value,
            },
        };
        const portValue = Number($("#setup-mqtt-port").value);
        if (!Number.isNaN(portValue) && portValue > 0) {
            payload.mqtt.port = portValue;
        }

        try {
            await apiClient.api("POST", "/api/bootstrap/setup", payload, {
                authorize: false,
                handleUnauthorized: false,
            });
            state.token = "";
            sessionStorage.removeItem("wg_token");
            setMsg(
                "#setup-msg",
                "success",
                "Initial setup saved. Reboot is required. After reboot, use normal admin login."
            );
        } catch (err) {
            const issues = err && err.data && Array.isArray(err.data.issues)
                ? err.data.issues.map((issue) => issue.field + ": " + issue.message).join("; ")
                : "";
            const suffix = issues ? " (" + issues + ")" : "";
            setMsg(
                "#setup-msg",
                "error",
                "Initial setup failed: " + ((err && err.message) || "unknown_error") + suffix
            );
        }
    }

    async function initializeApp() {
        const boot = await bootstrap();
        const firstBoot = !!(boot.provisioning && !boot.password_set);
        const stored = (sessionStorage.getItem("wg_token") || "").trim();

        if (firstBoot) {
            forceFirstBootSetup();
            return;
        }

        if (!stored) {
            showStartupUnauthenticated(boot);
            return;
        }

        state.token = stored;
        try {
            const status = await apiClient.api("GET", "/api/status");
            updateStatus(status);
            showApp();
        } catch (err) {
            if (err && err.message === "unauthorized") {
                return;
            }
            showSessionExpiredSignIn(
                "Stored session could not be restored. Sign in again to continue.",
                "warning"
            );
        }
    }

    function bindEvents() {
        $("#btn-logout").addEventListener("click", async () => {
            try {
                await apiClient.api("POST", "/api/auth/logout");
            } catch (_) {}
            showSessionExpiredSignIn("Signed out.", "success");
        });

        $("#login-form").addEventListener("submit", async (event) => {
            event.preventDefault();
            if (isFirstBootProvisioning()) {
                setMsg(
                    "#setup-msg",
                    "warning",
                    "First boot requires Initial Setup. Configure Wi-Fi and admin password below."
                );
                showSetupScreen();
                return;
            }
            const password = $("#login-pwd").value;
            try {
                const data = await apiClient.api("POST", "/api/auth/login", { password: password }, {
                    authorize: false,
                    handleUnauthorized: false,
                });
                if (!data.token) {
                    throw new Error("No auth token");
                }
                state.token = data.token;
                sessionStorage.setItem("wg_token", state.token);
                clearMsg("#login-error");
                showApp();
            } catch (err) {
                const retry = err && err.data && err.data.retry_after_s
                    ? " Try again in " + err.data.retry_after_s + "s."
                    : "";
                setMsg("#login-error", "error", (err.message || "Login failed") + retry);
            }
        });

        $("#setup-mqtt-enabled").addEventListener("change", (event) => {
            applySetupMqttEnabled(event.target.checked);
        });

        $("#setup-form").addEventListener("submit", async (event) => {
            event.preventDefault();
            await runInitialSetup();
        });
    }

    return {
        applyModeUi,
        applySetupMqttEnabled,
        bindEvents,
        initializeApp,
        showSessionExpiredSignIn,
    };
}
