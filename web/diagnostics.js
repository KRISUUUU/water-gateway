import {
    $,
    appendKvRows,
    clearChildren,
    formatUptime,
    text,
} from "./ui_common.js";
import {
    clearMsg,
    errorMessage,
    renderKvLoadError,
    setEmptyState,
    setMsg,
} from "./page_messages.js";
import {
    healthSummary,
    mqttQueueSummary,
    otaSummary,
    radioSummary,
} from "./ui_banner.js";

export function createDiagnosticsController({
    apiClient,
    updateStatus,
    updateOtaStatus,
    updateWatchlist,
    bannerController,
}) {
    async function loadDiagnostics() {
        try {
            const [radioData, mqttData, statusData, otaData] = await Promise.all([
                apiClient.api("GET", "/api/diagnostics/radio"),
                apiClient.api("GET", "/api/diagnostics/mqtt"),
                apiClient.api("GET", "/api/status"),
                apiClient.api("GET", "/api/ota/status"),
            ]);
            const radioEl = $("#rf-stats");
            const mqttEl = $("#mqtt-stats");
            const sysEl = $("#sys-stats");
            const otaEl = $("#diag-ota-stats");
            const summaryEl = $("#diag-summary");
            clearChildren(radioEl);
            clearChildren(mqttEl);
            clearChildren(sysEl);
            clearChildren(otaEl);
            clearChildren(summaryEl);
            clearMsg("#diag-msg");

            updateStatus(statusData);
            updateOtaStatus(otaData);
            bannerController.sync();

            const rsm = radioData.rsm || {};
            const diag = radioData.diagnostics || {};
            const counters = diag.radio_counters || {};
            appendKvRows(summaryEl, [
                ["Health", healthSummary(statusData.health || {})],
                ["MQTT Queue", mqttQueueSummary(mqttData)],
                ["Radio", radioSummary(radioData)],
                ["OTA", otaSummary(otaData)],
            ]);

            appendKvRows(radioEl, [
                ["RSM State", rsm.state],
                ["Consecutive Errors", rsm.consecutive_errors],
                ["Radio State", diag.radio_state],
                ["Frames Received", counters.frames_received],
                ["CRC OK", counters.frames_crc_ok],
                ["CRC Fail", counters.frames_crc_fail],
                ["Incomplete", counters.frames_incomplete],
                ["Dropped Too Long", counters.frames_dropped_too_long],
                ["FIFO Overflows", counters.fifo_overflows],
                ["SPI Errors", counters.spi_errors],
            ]);

            appendKvRows(mqttEl, [
                ["State", mqttData.state],
                ["Broker", mqttData.broker_uri],
                ["Queue State", mqttQueueSummary(mqttData)],
                ["Publish Count", mqttData.publish_count],
                ["Publish Failures", mqttData.publish_failures],
                ["Reconnects", mqttData.reconnect_count],
                ["Held Item", mqttData.held_item ? "yes" : "no"],
                ["Hold Count", mqttData.hold_count],
                ["Retry Attempts", mqttData.retry_count],
                ["Retry Failures", mqttData.retry_failure_count],
                ["Outbox", text(mqttData.outbox_depth, 0) + " / " + text(mqttData.outbox_capacity, 0)],
                ["Last Publish (ms)", mqttData.last_publish_epoch_ms],
            ]);

            const health = statusData.health || {};
            const metrics = statusData.metrics || {};
            const wifi = statusData.wifi || {};
            appendKvRows(sysEl, [
                ["Health", health.state],
                ["Health Detail", healthSummary(health)],
                ["Uptime", formatUptime(metrics.uptime_s)],
                ["Heap Free", metrics.free_heap_bytes],
                ["Heap Min", metrics.min_free_heap_bytes],
                ["Largest Block", metrics.largest_free_block],
                ["Wi-Fi State", wifi.state],
                ["Wi-Fi IP", wifi.ip_address],
                ["Wi-Fi RSSI", wifi.rssi_dbm],
            ]);

            appendKvRows(otaEl, [
                ["State", otaData.state],
                ["Summary", otaSummary(otaData)],
                ["Progress", text(otaData.progress_pct, "0") + "%"],
                ["Message", otaData.message],
                ["Version", otaData.current_version],
            ]);
        } catch (err) {
            const message = errorMessage(err, "Unable to load diagnostics.");
            setMsg("#diag-msg", "error", message);
            renderKvLoadError("#diag-summary", message);
            renderKvLoadError("#rf-stats", message);
            renderKvLoadError("#mqtt-stats", message);
            renderKvLoadError("#sys-stats", message);
            renderKvLoadError("#diag-ota-stats", message);
        }
    }

    async function loadSupport() {
        const summary = $("#support-summary");
        clearChildren(summary);
        clearMsg("#support-msg");
        try {
            const [status, ota, watch] = await Promise.all([
                apiClient.api("GET", "/api/status"),
                apiClient.api("GET", "/api/ota/status"),
                apiClient.api("GET", "/api/watchlist"),
            ]);
            updateStatus(status);
            updateOtaStatus(ota);
            updateWatchlist(watch.watchlist || []);
            bannerController.sync();
            appendKvRows(summary, [
                ["Firmware", status.firmware_version],
                ["Mode", status.mode],
                ["Health", healthSummary(status.health || {})],
                ["MQTT", status.mqtt ? status.mqtt.state : "--"],
                ["MQTT Queue", mqttQueueSummary(status.mqtt || {})],
                ["Watchlist Entries", (watch.watchlist || []).length],
                ["OTA", otaSummary(ota)],
            ]);
        } catch (err) {
            const message = errorMessage(err, "Unable to load support summary.");
            setMsg("#support-msg", "error", message);
            renderKvLoadError("#support-summary", message);
        }
    }

    async function loadLogs() {
        try {
            const data = await apiClient.api("GET", "/api/logs");
            const lines = data.entries || [];
            const filter = $("#log-filter").value;
            const filtered = filter ? lines.filter((line) => line.severity === filter) : lines;
            const output = filtered
                .map((line) => {
                    const timestamp = line.timestamp_us
                        ? new Date(Math.floor(line.timestamp_us / 1000)).toISOString()
                        : "";
                    return (
                        "[" + text(line.severity, "?").toUpperCase().padEnd(7) + "] " +
                        timestamp + " " + text(line.message, "")
                    );
                })
                .join("\n");
            $("#log-output").textContent = output || "No logs.";
            setEmptyState("#logs-empty", "No logs available.", filtered.length > 0);
        } catch (err) {
            $("#log-output").textContent = "";
            setEmptyState("#logs-empty", errorMessage(err, "Unable to load logs."), false);
        }
    }

    async function downloadSupportBundle() {
        clearMsg("#support-msg");
        try {
            const blob = await apiClient.downloadBlob("/api/support-bundle");
            const link = document.createElement("a");
            link.href = URL.createObjectURL(blob);
            link.download = "support-bundle.json";
            link.click();
            setMsg("#support-msg", "success", "Support bundle downloaded.");
        } catch (err) {
            setMsg("#support-msg", "error", errorMessage(err, "Support bundle download failed."));
        }
    }

    function bindEvents() {
        $("#diag-refresh").addEventListener("click", loadDiagnostics);
        $("#log-filter").addEventListener("change", loadLogs);
        $("#log-refresh").addEventListener("click", loadLogs);
        $("#sys-bundle").addEventListener("click", downloadSupportBundle);
    }

    return {
        bindEvents,
        loadDiagnostics,
        loadLogs,
        loadSupport,
    };
}
