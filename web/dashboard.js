import {
    badgeClassByState,
    clearChildren,
    formatUptime,
    statCard,
    text,
} from "./ui_common.js";
import { clearMsg, errorMessage, setMsg } from "./page_messages.js";

function renderDashboard(status, counts) {
    const health = status.health || {};
    const metrics = status.metrics || {};
    const wifi = status.wifi || {};
    const mqtt = status.mqtt || {};
    const radio = status.radio || {};

    const statusGrid = document.querySelector("#dashboard-status-grid");
    clearChildren(statusGrid);
    statusGrid.appendChild(statCard("Health", text(health.state), badgeClassByState(health.state)));
    statusGrid.appendChild(statCard("Wi-Fi", text(wifi.state), badgeClassByState(wifi.state)));
    statusGrid.appendChild(statCard("MQTT", text(mqtt.state), badgeClassByState(mqtt.state)));
    statusGrid.appendChild(statCard("Radio", text(radio.state), badgeClassByState(radio.state)));
    statusGrid.appendChild(statCard("Mode", status.mode || "--"));
    statusGrid.appendChild(statCard("Firmware", status.firmware_version || "--"));

    const metricGrid = document.querySelector("#dashboard-metrics-grid");
    clearChildren(metricGrid);
    metricGrid.appendChild(statCard("Uptime", formatUptime(metrics.uptime_s)));
    metricGrid.appendChild(statCard("Frames Received", radio.frames_received || 0));
    metricGrid.appendChild(statCard("CRC Fail", radio.frames_crc_fail || 0));
    metricGrid.appendChild(statCard("Duplicates", counts.duplicateCount));
    metricGrid.appendChild(statCard("Incomplete Frames", radio.frames_incomplete || 0));
    metricGrid.appendChild(statCard("Dropped Too Long", radio.frames_dropped_too_long || 0));
    metricGrid.appendChild(statCard("MQTT Publish Failures", mqtt.publish_failures || 0));
    metricGrid.appendChild(statCard("Detected Meters", counts.detected));
    metricGrid.appendChild(statCard("Watchlist", counts.watchCount));
    metricGrid.appendChild(statCard("Wi-Fi RSSI", text(wifi.rssi_dbm, "--") + " dBm"));
    metricGrid.appendChild(
        statCard("Free Heap", Math.round((metrics.free_heap_bytes || 0) / 1024) + " KB")
    );
    metricGrid.appendChild(statCard("IP Address", wifi.ip_address || "--"));
}

export function createDashboardController({
    state,
    apiClient,
    updateStatus,
    updateWatchlist,
    bannerController,
}) {
    async function loadDashboard() {
        try {
            const [status, metersData, watchlistData] = await Promise.all([
                apiClient.api("GET", "/api/status"),
                apiClient.api("GET", "/api/meters/detected"),
                apiClient.api("GET", "/api/watchlist"),
            ]);
            const meters = metersData.meters || [];
            const counts = {
                detected: meters.length,
                watchCount: (watchlistData.watchlist || []).length,
                duplicateCount: meters.reduce(
                    (sum, meter) => sum + Number(meter.duplicate_count || 0),
                    0
                ),
            };

            updateStatus(status);
            updateWatchlist(watchlistData.watchlist || []);
            clearMsg("#dashboard-msg");
            state.dashboardCache.duplicateCount = counts.duplicateCount;
            state.dashboardCache.detected = counts.detected;
            state.dashboardCache.watchlistCount = counts.watchCount;
            renderDashboard(status, counts);
            bannerController.sync();
        } catch (err) {
            setMsg("#dashboard-msg", "error", errorMessage(err, "Unable to load dashboard."));
        }
    }

    async function loadDashboardLight() {
        try {
            const status = await apiClient.api("GET", "/api/status");
            updateStatus(status);
            clearMsg("#dashboard-msg");
            renderDashboard(status, {
                duplicateCount: state.dashboardCache.duplicateCount,
                detected: state.dashboardCache.detected,
                watchCount: state.dashboardCache.watchlistCount,
            });
            bannerController.sync();
        } catch (err) {
            setMsg(
                "#dashboard-msg",
                "warning",
                errorMessage(err, "Live refresh failed. Showing last known dashboard values.")
            );
        }
    }

    async function refreshStatusOnly() {
        if (state.currentPage === "dashboard") {
            await loadDashboardLight();
            return;
        }
        try {
            const status = await apiClient.api("GET", "/api/status");
            updateStatus(status);
            bannerController.sync();
        } catch (_) {}
    }

    return {
        loadDashboard,
        loadDashboardLight,
        refreshStatusOnly,
    };
}
