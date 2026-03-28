import { $, text } from "./ui_common.js";
import { clearMsg, setMsg } from "./page_messages.js";

export function mqttQueueSummary(mqtt) {
    const depth = Number(mqtt && mqtt.outbox_depth ? mqtt.outbox_depth : 0);
    const capacity = Number(mqtt && mqtt.outbox_capacity ? mqtt.outbox_capacity : 0);
    const held = !!(mqtt && mqtt.held_item);
    const retryFailures = Number(mqtt && mqtt.retry_failure_count ? mqtt.retry_failure_count : 0);
    const retries = Number(mqtt && mqtt.retry_count ? mqtt.retry_count : 0);
    const details = [];
    if (capacity > 0) {
        details.push(depth + "/" + capacity + " queued");
    } else if (depth > 0) {
        details.push(depth + " queued");
    } else {
        details.push("idle");
    }
    if (held) {
        details.push("held item");
    }
    if (retryFailures > 0) {
        details.push(retryFailures + " retry failures");
    } else if (retries > 0) {
        details.push(retries + " retries");
    }
    if (capacity > 0 && depth >= capacity) {
        return "High pressure: " + details.join(", ");
    }
    if (held || depth > 0) {
        return "Active queue: " + details.join(", ");
    }
    return "Clear: " + details.join(", ");
}

export function radioSummary(radioData) {
    const rsm = radioData && radioData.rsm ? radioData.rsm : {};
    const diag = radioData && radioData.diagnostics ? radioData.diagnostics : {};
    const counters = diag.radio_counters || {};
    const issues = [];
    if (Number(rsm.consecutive_errors || 0) > 0) {
        issues.push(rsm.consecutive_errors + " consecutive errors");
    }
    if (Number(counters.spi_errors || 0) > 0) {
        issues.push(counters.spi_errors + " SPI errors");
    }
    if (Number(counters.fifo_overflows || 0) > 0) {
        issues.push(counters.fifo_overflows + " FIFO overflows");
    }
    if (issues.length > 0) {
        return "Needs attention: " + issues.join(", ");
    }
    return "Stable: " + text(diag.radio_state || rsm.state, "unknown");
}

export function otaSummary(ota) {
    const state = String(ota && ota.state ? ota.state : "").toLowerCase();
    const progress = text(ota && ota.progress_pct, "0") + "%";
    if (!state || state === "idle") {
        return "Idle";
    }
    if (state.includes("error") || state.includes("fail")) {
        return "Problem: " + text(ota && ota.message, state);
    }
    if (state.includes("success") || state.includes("complete") || state === "uploaded") {
        return "Ready: " + text(ota && ota.message, "reboot may be required");
    }
    return "In progress: " + progress + " (" + text(ota && ota.message, state) + ")";
}

export function healthSummary(health) {
    if (!health) {
        return "--";
    }
    const errors = Number(health.error_count || 0);
    const warnings = Number(health.warning_count || 0);
    if (errors > 0) {
        return errors + " error(s): " + text(health.last_error_msg, "check diagnostics");
    }
    if (warnings > 0) {
        return warnings + " warning(s): " + text(health.last_warning_msg, "check diagnostics");
    }
    return "No active health alerts";
}

export function buildOperatorNotice(status, ota, stickyBanner) {
    const safeStatus = status || {};
    const safeOta = ota || {};
    const health = safeStatus.health || {};
    const mqtt = safeStatus.mqtt || {};

    if (String(safeStatus.mode || "").toLowerCase() === "provisioning") {
        return {
            kind: "warning",
            message:
                "Device is in provisioning mode. Finish required settings and reboot to enter normal operation.",
        };
    }

    const otaState = String(safeOta.state || "").toLowerCase();
    if (otaState && otaState !== "idle") {
        if (otaState.includes("error") || otaState.includes("fail")) {
            return {
                kind: "error",
                message: "OTA reports a problem: " + text(safeOta.message, safeOta.state),
            };
        }
        if (otaState.includes("success") || otaState.includes("complete") || otaState === "uploaded") {
            return {
                kind: "success",
                message: "OTA completed. Reboot the device if activation is still pending.",
            };
        }
        return {
            kind: "warning",
            message: "OTA is in progress: " + text(safeOta.progress_pct, "0") + "% complete.",
        };
    }

    if (Number(health.error_count || 0) > 0) {
        return {
            kind: "error",
            message: "Health monitor reports an error: " + text(health.last_error_msg, "check diagnostics"),
        };
    }
    if (Number(health.warning_count || 0) > 0) {
        return {
            kind: "warning",
            message:
                "Health monitor has warnings: " + text(health.last_warning_msg, "check diagnostics"),
        };
    }
    if (stickyBanner) {
        return stickyBanner;
    }
    if ((mqtt.held_item || Number(mqtt.outbox_depth || 0) > 0) &&
        String(mqtt.state || "").toLowerCase() !== "connected") {
        return {
            kind: "warning",
            message: "MQTT has queued data waiting for connectivity. Check broker reachability and queue pressure.",
        };
    }
    return null;
}

export function createBannerController({ state }) {
    function clear() {
        const banner = $("#app-banner");
        if (!banner) {
            return;
        }
        banner.className = "msg app-banner";
        clearMsg(banner);
    }

    function show(notice) {
        const banner = $("#app-banner");
        if (!banner || !notice) {
            return;
        }
        setMsg(banner, notice.kind, notice.message);
        banner.classList.add("app-banner");
    }

    function sync() {
        const notice = buildOperatorNotice(
            state.cacheStatus,
            state.cacheOtaStatus,
            state.stickyBanner
        );
        if (!notice) {
            clear();
            return;
        }
        show(notice);
    }

    function setStickyNotice(kind, message) {
        state.stickyBanner = message ? { kind: kind || "warning", message: message } : null;
        sync();
    }

    function clearStickyNotice() {
        state.stickyBanner = null;
        sync();
    }

    return {
        clear,
        sync,
        setStickyNotice,
        clearStickyNotice,
    };
}
