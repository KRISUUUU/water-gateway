import { $, clearChildren, text } from "./ui_common.js";
import { clearMsg, errorMessage, setEmptyState, setMsg } from "./page_messages.js";

function createCell(row, value, className) {
    const td = document.createElement("td");
    if (className) {
        td.className = className;
    }
    td.textContent = text(value, "");
    row.appendChild(td);
    return td;
}

export function createWatchlistController({
    state,
    apiClient,
    updateWatchlist,
    getCurrentPage,
    showPage,
}) {
    let loadDetectedMeters = async () => {};

    function setDetectedMetersLoader(loader) {
        loadDetectedMeters = loader;
    }

    function openWatchlistEntry(key, suggestedAlias, suggestedNote) {
        $("#wl-key").value = key || "";
        $("#wl-alias").value = suggestedAlias || "";
        $("#wl-note").value = suggestedNote || "";
        $("#wl-enabled").checked = true;
        showPage("watchlist");
    }

    function watchAliasMap() {
        const map = {};
        state.cacheWatchlist.forEach((entry) => {
            map[entry.key] = entry.alias || "";
        });
        return map;
    }

    async function loadWatchlist() {
        const body = $("#watchlist-body");
        try {
            const data = await apiClient.api("GET", "/api/watchlist");
            updateWatchlist(data.watchlist || []);
            clearChildren(body);
            clearMsg("#wl-msg");
            setEmptyState("#watchlist-empty", "Watchlist is empty.", state.cacheWatchlist.length > 0);
            state.cacheWatchlist.forEach((entry) => {
                const tr = document.createElement("tr");
                createCell(tr, entry.enabled ? "YES" : "NO");
                createCell(tr, entry.alias || "");
                createCell(tr, entry.key || "", "hex");
                createCell(tr, entry.note || "");
                tr.addEventListener("click", () => {
                    $("#wl-key").value = entry.key || "";
                    $("#wl-alias").value = entry.alias || "";
                    $("#wl-note").value = entry.note || "";
                    $("#wl-enabled").checked = !!entry.enabled;
                });
                body.appendChild(tr);
            });
        } catch (err) {
            clearChildren(body);
            setEmptyState("#watchlist-empty", errorMessage(err, "Unable to load watchlist."), false);
            setMsg("#wl-msg", "error", errorMessage(err, "Unable to load watchlist."));
        }
    }

    async function submitWatchlistEntry() {
        clearMsg("#wl-msg");
        const key = $("#wl-key").value.trim();
        if (!key) {
            setMsg("#wl-msg", "error", "Meter key is required.");
            return;
        }
        try {
            await apiClient.api("POST", "/api/watchlist", {
                key: key,
                alias: $("#wl-alias").value || "",
                note: $("#wl-note").value || "",
                enabled: $("#wl-enabled").checked,
            });
            setMsg("#wl-msg", "success", "Watchlist updated.");
            await loadWatchlist();
            if (getCurrentPage() === "meters") {
                await loadDetectedMeters();
            }
        } catch (err) {
            setMsg("#wl-msg", "error", errorMessage(err, "Watchlist update failed."));
        }
    }

    async function deleteWatchlistEntry() {
        clearMsg("#wl-msg");
        const key = $("#wl-key").value.trim();
        if (!key) {
            setMsg("#wl-msg", "error", "Meter key is required.");
            return;
        }
        try {
            await apiClient.api("POST", "/api/watchlist/delete", { key: key });
            setMsg("#wl-msg", "success", "Watchlist entry removed.");
            $("#wl-key").value = "";
            $("#wl-alias").value = "";
            $("#wl-note").value = "";
            $("#wl-enabled").checked = true;
            await loadWatchlist();
            await loadDetectedMeters();
        } catch (err) {
            setMsg("#wl-msg", "error", errorMessage(err, "Watchlist delete failed."));
        }
    }

    function bindEvents() {
        $("#wl-save").addEventListener("click", submitWatchlistEntry);
        $("#wl-delete").addEventListener("click", deleteWatchlistEntry);
    }

    return {
        bindEvents,
        loadWatchlist,
        openWatchlistEntry,
        setDetectedMetersLoader,
        watchAliasMap,
    };
}
