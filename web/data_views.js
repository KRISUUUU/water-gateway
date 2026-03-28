import {
    $,
    clearChildren,
    formatTimeMs,
    text,
} from "./ui_common.js";
import { errorMessage, setEmptyState } from "./page_messages.js";

function createCell(row, value, className) {
    const td = document.createElement("td");
    if (className) {
        td.className = className;
    }
    td.textContent = text(value, "");
    row.appendChild(td);
    return td;
}

function matchesMeterSearch(meter, search) {
    if (!search) {
        return true;
    }
    const value = search.toLowerCase();
    return (
        String(meter.key || "").toLowerCase().includes(value) ||
        String(meter.alias || "").toLowerCase().includes(value)
    );
}

export function createDataViewsController({
    apiClient,
    updateWatchlist,
    watchlistController,
}) {
    async function loadTelegrams() {
        const filter = $("#tg-filter").value || "all";
        const apiFilter = filter === "problematic" ? "crc_fail" : filter;
        const suffix = apiFilter === "all" ? "" : ("?filter=" + encodeURIComponent(apiFilter));
        const body = $("#tg-body");
        try {
            const [data, watchlist] = await Promise.all([
                apiClient.api("GET", "/api/telegrams" + suffix),
                apiClient.api("GET", "/api/watchlist"),
            ]);
            updateWatchlist(watchlist.watchlist || []);
            const aliases = watchlistController.watchAliasMap();
            const telegrams = data.telegrams || [];
            telegrams.sort((a, b) => Number(b.timestamp_ms || 0) - Number(a.timestamp_ms || 0));
            clearChildren(body);
            setEmptyState("#tg-empty", "No telegrams available yet.", telegrams.length > 0);

            telegrams.forEach((frame) => {
                const tr = document.createElement("tr");
                createCell(tr, formatTimeMs(frame.timestamp_ms));
                createCell(tr, frame.meter_key || "");
                createCell(tr, aliases[frame.meter_key] || "");
                const raw = createCell(tr, frame.raw_hex || "", "hex");
                raw.title = frame.raw_hex || "";
                createCell(tr, frame.frame_length);
                createCell(tr, frame.rssi_dbm);
                createCell(tr, frame.lqi);
                createCell(tr, frame.crc_ok ? "OK" : "FAIL");
                createCell(tr, frame.duplicate ? "YES" : "NO");
                createCell(tr, frame.watched ? "YES" : "NO");

                const actionsCell = document.createElement("td");
                const actions = document.createElement("div");
                actions.className = "btn-row";

                const copyBtn = document.createElement("button");
                copyBtn.className = "btn btn-secondary";
                copyBtn.textContent = "Copy";
                copyBtn.addEventListener("click", () => {
                    navigator.clipboard.writeText(frame.raw_hex || "").catch(() => {});
                    const previous = copyBtn.textContent;
                    copyBtn.textContent = "Copied!";
                    copyBtn.disabled = true;
                    window.setTimeout(() => {
                        copyBtn.textContent = previous;
                        copyBtn.disabled = false;
                    }, 2000);
                });
                actions.appendChild(copyBtn);

                const addBtn = document.createElement("button");
                addBtn.className = "btn btn-primary";
                addBtn.textContent = frame.watched ? "Edit Watch" : "Add Watch";
                addBtn.addEventListener("click", () => {
                    watchlistController.openWatchlistEntry(
                        frame.meter_key || "",
                        aliases[frame.meter_key] || "",
                        ""
                    );
                });
                actions.appendChild(addBtn);
                actionsCell.appendChild(actions);
                tr.appendChild(actionsCell);
                body.appendChild(tr);
            });
        } catch (err) {
            clearChildren(body);
            setEmptyState("#tg-empty", errorMessage(err, "Unable to load telegrams."), false);
        }
    }

    async function loadDetectedMeters() {
        const filter = $("#meters-filter").value || "all";
        const suffix = filter === "all" ? "" : ("?filter=" + encodeURIComponent(filter));
        const search = ($("#meters-search").value || "").trim();
        const body = $("#meters-body");
        try {
            const data = await apiClient.api("GET", "/api/meters/detected" + suffix);
            const meters = (data.meters || []).filter((meter) => matchesMeterSearch(meter, search));
            clearChildren(body);
            setEmptyState("#meters-empty", "No meters detected yet.", meters.length > 0);
            meters.forEach((meter) => {
                const tr = document.createElement("tr");
                createCell(tr, meter.alias || "");
                createCell(tr, meter.key || "", "hex");
                createCell(tr, meter.manufacturer_id || "--");
                createCell(tr, meter.device_id || "--");
                createCell(tr, formatTimeMs(meter.first_seen_ms));
                createCell(tr, formatTimeMs(meter.last_seen_ms));
                createCell(tr, meter.seen_count || 0);
                createCell(tr, text(meter.last_rssi_dbm, "--") + " / " + text(meter.last_lqi, "--"));
                createCell(
                    tr,
                    meter.watch_enabled ? "ENABLED" : meter.watched ? "DISABLED" : "NO"
                );
                createCell(tr, meter.note || "");

                const actionsCell = document.createElement("td");
                const row = document.createElement("div");
                row.className = "btn-row";
                const watchBtn = document.createElement("button");
                watchBtn.className = "btn btn-primary";
                watchBtn.textContent = meter.watched ? "Edit Watch" : "Add";
                watchBtn.addEventListener("click", () => {
                    watchlistController.openWatchlistEntry(
                        meter.key,
                        meter.alias || "",
                        meter.note || ""
                    );
                });
                row.appendChild(watchBtn);
                actionsCell.appendChild(row);
                tr.appendChild(actionsCell);
                body.appendChild(tr);
            });
        } catch (err) {
            clearChildren(body);
            setEmptyState("#meters-empty", errorMessage(err, "Unable to load detected meters."), false);
        }
    }

    function bindEvents() {
        $("#tg-filter").addEventListener("change", loadTelegrams);
        $("#tg-refresh").addEventListener("click", loadTelegrams);
        $("#meters-filter").addEventListener("change", loadDetectedMeters);
        $("#meters-search").addEventListener("input", loadDetectedMeters);
        $("#meters-refresh").addEventListener("click", loadDetectedMeters);
    }

    return {
        bindEvents,
        loadTelegrams,
        loadDetectedMeters,
    };
}
