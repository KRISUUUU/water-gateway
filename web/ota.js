import { $, appendKvRows, clearChildren, text } from "./ui_common.js";
import { clearMsg, errorMessage, renderKvLoadError, setMsg } from "./page_messages.js";
import { otaSummary } from "./ui_banner.js";

export function createOtaController({
    apiClient,
    updateOtaStatus,
    bannerController,
    refreshStatusOnly,
}) {
    async function loadOtaStatus() {
        try {
            const status = await apiClient.api("GET", "/api/ota/status");
            updateOtaStatus(status);
            clearMsg("#ota-msg");
            const otaGrid = $("#ota-state-grid");
            clearChildren(otaGrid);
            appendKvRows(otaGrid, [
                ["State", status.state],
                ["Summary", otaSummary(status)],
                ["Progress", text(status.progress_pct, "0") + "%"],
                ["Current Version", status.current_version],
                ["Message", status.message],
            ]);
            $("#ota-status").textContent = "Status: " + text(status.state);
            bannerController.sync();
        } catch (err) {
            const message = errorMessage(err, "Unable to load OTA status.");
            renderKvLoadError("#ota-state-grid", message);
            setMsg("#ota-msg", "error", message);
            $("#ota-status").textContent = "Status: unavailable";
        }
    }

    async function uploadFirmware() {
        const file = $("#ota-file").files[0];
        if (!file) {
            setMsg("#ota-msg", "warning", "Select a firmware file first.");
            return;
        }
        setMsg("#ota-msg", "warning", "Uploading firmware...");
        try {
            const response = await apiClient.requestJson("/api/ota/upload", {
                method: "POST",
                headers: { "Content-Type": "application/octet-stream" },
                body: file,
            });
            const detail = response.detail || "Upload complete. Reboot required.";
            updateOtaStatus({
                state: "uploaded",
                progress_pct: 100,
                message: detail,
            });
            setMsg("#ota-msg", "success", detail);
            bannerController.setStickyNotice(
                "warning",
                "Firmware upload finished. Reboot the device when you are ready to activate it."
            );
            await loadOtaStatus();
        } catch (err) {
            setMsg("#ota-msg", "error", "Upload failed: " + errorMessage(err, "request failed"));
        }
    }

    async function startOtaFromUrl() {
        const url = $("#ota-url").value.trim();
        if (!url) {
            setMsg("#ota-msg", "warning", "Enter a firmware URL first.");
            return;
        }
        try {
            await apiClient.api("POST", "/api/ota/url", { url: url });
            updateOtaStatus({
                state: "starting",
                progress_pct: 0,
                message: "Waiting for OTA worker to report progress.",
            });
            setMsg("#ota-msg", "success", "OTA URL update started.");
            bannerController.sync();
            await loadOtaStatus();
        } catch (err) {
            setMsg("#ota-msg", "error", "OTA URL failed: " + errorMessage(err, "request failed"));
        }
    }

    async function sendRebootCommand(messageSelector, options) {
        clearMsg(messageSelector);
        try {
            await apiClient.api("POST", "/api/system/reboot");
            if (messageSelector) {
                setMsg(messageSelector, "warning", "Reboot command sent. Connection may drop shortly.");
            }
            bannerController.setStickyNotice(
                "warning",
                "Reboot command sent. Wait for the device to come back online."
            );
        } catch (err) {
            if (messageSelector) {
                setMsg(messageSelector, "error", errorMessage(err, "Reboot failed."));
            }
        }
        if (options && options.refreshStatus) {
            await refreshStatusOnly();
        }
    }

    async function sendFactoryResetCommand() {
        clearMsg("#factory-msg");
        try {
            await apiClient.api("POST", "/api/system/factory-reset");
            setMsg("#factory-msg", "warning", "Factory reset command sent. Device will erase settings and reboot.");
            bannerController.setStickyNotice(
                "warning",
                "Factory reset started. Stored settings will be cleared during reboot."
            );
        } catch (err) {
            setMsg("#factory-msg", "error", errorMessage(err, "Factory reset failed."));
        }
    }

    function bindEvents() {
        $("#ota-upload-btn").addEventListener("click", uploadFirmware);
        $("#ota-url-btn").addEventListener("click", startOtaFromUrl);
        $("#sys-reboot").addEventListener("click", async () => {
            if (!confirm("Reboot device now?")) {
                return;
            }
            await sendRebootCommand("#factory-msg");
        });
        $("#dash-reboot").addEventListener("click", async () => {
            if (confirm("Reboot device now?")) {
                await sendRebootCommand(null, { refreshStatus: true });
            }
        });
        $("#sys-reset").addEventListener("click", async () => {
            if (!confirm("Factory reset will erase configuration and reboot. Continue?")) {
                return;
            }
            await sendFactoryResetCommand();
        });
    }

    return {
        bindEvents,
        loadOtaStatus,
    };
}
