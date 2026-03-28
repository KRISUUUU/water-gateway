import { $, clearChildren } from "./ui_common.js";
import { clearMsg, errorMessage, setMsg } from "./page_messages.js";

function renderConfigSection(container, sectionName, sectionObject) {
    const card = document.createElement("div");
    card.className = "card";
    const title = document.createElement("h3");
    title.textContent = sectionName;
    card.appendChild(title);
    Object.keys(sectionObject).forEach((key) => {
        const value = sectionObject[key];
        const id = "cfg-" + sectionName.toLowerCase() + "-" + key;
        if (key === "password_set") {
            const row = document.createElement("div");
            row.className = "kv-item";
            const left = document.createElement("span");
            left.textContent = key;
            const right = document.createElement("span");
            right.textContent = value ? "yes" : "no";
            row.appendChild(left);
            row.appendChild(right);
            card.appendChild(row);
            return;
        }
        const label = document.createElement("label");
        label.setAttribute("for", id);
        label.textContent = key;
        const input = document.createElement("input");
        input.id = id;
        if (typeof value === "boolean") {
            input.type = "checkbox";
            input.checked = value;
        } else if (typeof value === "number") {
            input.type = "number";
            input.value = String(value);
        } else {
            input.type = "text";
            input.value = value === undefined || value === null ? "" : String(value);
        }
        label.appendChild(input);
        card.appendChild(label);
    });
    container.appendChild(card);
}

function collectConfigValues(cfg) {
    ["device", "wifi", "mqtt", "radio", "auth", "logging"].forEach((section) => {
        if (!cfg[section]) {
            return;
        }
        Object.keys(cfg[section]).forEach((key) => {
            if (key === "password_set") {
                return;
            }
            const el = $("#cfg-" + section + "-" + key);
            if (!el) {
                return;
            }
            if (el.type === "checkbox") {
                cfg[section][key] = el.checked;
            } else if (el.type === "number") {
                cfg[section][key] = Number(el.value);
            } else if (el.value !== "***") {
                cfg[section][key] = el.value;
            }
        });
    });
}

export function createSettingsController({
    state,
    apiClient,
    bannerController,
    showSessionExpiredSignIn,
}) {
    async function loadConfig() {
        try {
            clearMsg("#cfg-msg");
            const cfg = await apiClient.api("GET", "/api/config");
            const container = $("#config-form-container");
            clearChildren(container);
            if (state.cacheStatus && String(state.cacheStatus.mode || "").toLowerCase() === "provisioning") {
                const onboarding = document.createElement("div");
                onboarding.className = "card";
                const heading = document.createElement("h3");
                heading.textContent = "Provisioning Checklist";
                const hint = document.createElement("p");
                hint.className = "hint";
                hint.textContent =
                    "Complete Wi-Fi, admin password, and MQTT settings. Save settings and reboot to enter normal mode.";
                onboarding.appendChild(heading);
                onboarding.appendChild(hint);
                container.appendChild(onboarding);
            }
            renderConfigSection(container, "Device", cfg.device || {});
            renderConfigSection(container, "WiFi", cfg.wifi || {});
            renderConfigSection(container, "MQTT", cfg.mqtt || {});
            renderConfigSection(container, "Radio", cfg.radio || {});
            renderConfigSection(container, "Auth", cfg.auth || {});
            renderConfigSection(container, "Logging", cfg.logging || {});
        } catch (err) {
            clearChildren($("#config-form-container"));
            setMsg("#cfg-msg", "error", errorMessage(err, "Unable to load settings."));
        }
    }

    async function saveConfig() {
        clearMsg("#cfg-msg");
        try {
            const cfg = await apiClient.api("GET", "/api/config");
            collectConfigValues(cfg);
            const response = await apiClient.api("POST", "/api/config", cfg);
            if (response.relogin_required) {
                bannerController.clearStickyNotice();
                showSessionExpiredSignIn(
                    "Settings saved. Sign in again because authentication settings changed.",
                    "warning"
                );
                return;
            }
            if (response.reboot_required) {
                setMsg("#cfg-msg", "warning", "Settings saved. Reboot the device to apply runtime changes.");
                bannerController.setStickyNotice(
                    "warning",
                    "Runtime settings changed. Reboot the device to apply the new configuration."
                );
                return;
            }
            setMsg("#cfg-msg", "success", "Settings saved.");
            bannerController.sync();
        } catch (err) {
            const issues = err && err.data && err.data.issues ? err.data.issues : [];
            if (issues.length > 0) {
                const detail = issues.map((issue) => issue.field + ": " + issue.message).join("; ");
                setMsg("#cfg-msg", "error", "Validation failed: " + detail);
                return;
            }
            setMsg("#cfg-msg", "error", errorMessage(err, "Save failed."));
        }
    }

    async function exportConfigFile() {
        try {
            const cfg = await apiClient.api("GET", "/api/config");
            const blob = new Blob([JSON.stringify(cfg, null, 2)], {
                type: "application/json",
            });
            const link = document.createElement("a");
            link.href = URL.createObjectURL(blob);
            link.download = "wmbus-gw-config.json";
            link.click();
        } catch (err) {
            setMsg("#cfg-msg", "error", errorMessage(err, "Config export failed."));
        }
    }

    function bindEvents() {
        $("#cfg-save").addEventListener("click", saveConfig);
        $("#cfg-export").addEventListener("click", exportConfigFile);
    }

    return {
        bindEvents,
        loadConfig,
    };
}
