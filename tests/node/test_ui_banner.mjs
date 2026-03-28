import assert from "node:assert/strict";

import {
    buildOperatorNotice,
    healthSummary,
    mqttQueueSummary,
    otaSummary,
} from "../../web/ui_banner.js";

assert.equal(
    mqttQueueSummary({
        outbox_depth: 0,
        outbox_capacity: 32,
        held_item: false,
        retry_count: 0,
        retry_failure_count: 0,
    }),
    "Clear: 0/32 queued"
);

const queuePressure = mqttQueueSummary({
    outbox_depth: 8,
    outbox_capacity: 8,
    held_item: true,
    retry_count: 2,
    retry_failure_count: 1,
});
assert.match(queuePressure, /High pressure/);
assert.match(queuePressure, /held item/);
assert.match(queuePressure, /retry failures/);

assert.equal(
    healthSummary({
        warning_count: 1,
        error_count: 0,
        last_warning_msg: "Low heap margin",
    }),
    "1 warning(s): Low heap margin"
);

assert.equal(
    otaSummary({
        state: "uploaded",
        progress_pct: 100,
        message: "Upload complete. Reboot required.",
    }),
    "Ready: Upload complete. Reboot required."
);

const notice = buildOperatorNotice(
    {
        mode: "normal",
        health: {
            error_count: 1,
            warning_count: 0,
            last_error_msg: "MQTT worker stalled",
        },
        mqtt: {
            state: "Disconnected",
            outbox_depth: 4,
            held_item: true,
        },
    },
    { state: "idle" },
    { kind: "warning", message: "Reboot required." }
);
assert.deepEqual(notice, {
    kind: "error",
    message: "Health monitor reports an error: MQTT worker stalled",
});

console.log("UI banner helper tests passed.");
