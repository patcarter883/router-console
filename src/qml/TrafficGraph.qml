// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick

// RX/TX line graph over a stored history window. The series are one value per
// grid slot (`bucketMs` wide, slot i centred at gridStartMs + i*bucketMs), a
// negative value marks a slot with no measurement and breaks the line. Drawing
// is linear in the slot count, which the range selector caps at 600.
Item {
    id: graph

    property var rxSeries: []
    property var txSeries: []
    property real gridStartMs: 0
    property real fromMs: 0
    property real toMs: 0
    property int bucketMs: 1000
    property color rxColor: "#3fbf6a"
    property color txColor: "#4b9fe8"
    property color gridColor: "#2c2c2c"
    property color axisColor: "#8c8c8c"
    property color backgroundColor: "#151515"

    // Paint cost, accumulated in onPaint and reported from the GUI thread (the
    // paint callback runs on the render thread, where calling into the model is
    // not allowed).
    property real paintMs: 0
    property int paintCount: 0
    property int reported: 0

    implicitHeight: 200
    implicitWidth: 480

    onRxSeriesChanged: canvas.requestPaint()
    onTxSeriesChanged: canvas.requestPaint()
    onGridStartMsChanged: canvas.requestPaint()
    onFromMsChanged: canvas.requestPaint()
    onToMsChanged: canvas.requestPaint()
    onBucketMsChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()

    function formatBits(bytesPerSecond) {
        const bits = Math.max(0, bytesPerSecond) * 8;
        if (bits >= 1e9) return (bits / 1e9).toFixed(1) + " Gbps";
        if (bits >= 1e6) return (bits / 1e6).toFixed(1) + " Mbps";
        if (bits >= 1e3) return (bits / 1e3).toFixed(1) + " kbps";
        return bits.toFixed(0) + " bps";
    }

    function peak() {
        let peak = 0;
        for (let i = 0; i < rxSeries.length; ++i) {
            if (rxSeries[i] > peak)
                peak = rxSeries[i];
        }
        for (let i = 0; i < txSeries.length; ++i) {
            if (txSeries[i] > peak)
                peak = txSeries[i];
        }
        // A floor keeps an idle graph readable instead of dividing by zero.
        return Math.max(peak, 1024);
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: 1

        onPaint: {
            const startedAt = Date.now();
            const ctx = getContext("2d");
            ctx.reset();

            const w = width;
            const h = height;
            const labelWidth = 62;
            const plotWidth = Math.max(1, w - labelWidth);
            const plotHeight = Math.max(1, h - 6);
            const top = 3;

            ctx.fillStyle = graph.backgroundColor;
            ctx.fillRect(0, 0, w, h);

            const peak = graph.peak();
            const rows = 4;
            ctx.strokeStyle = graph.gridColor;
            ctx.lineWidth = 1;
            ctx.fillStyle = graph.axisColor;
            ctx.font = "10px monospace";
            ctx.textAlign = "right";

            for (let i = 0; i <= rows; ++i) {
                const y = top + (plotHeight * i) / rows;
                ctx.beginPath();
                ctx.moveTo(0, y + 0.5);
                ctx.lineTo(plotWidth, y + 0.5);
                ctx.stroke();
                const value = peak * (1 - i / rows);
                ctx.fillText(graph.formatBits(value), w - 3, y + 3.5);
            }

            const columns = 6;
            for (let i = 0; i <= columns; ++i) {
                const x = (plotWidth * i) / columns;
                ctx.beginPath();
                ctx.moveTo(x + 0.5, top);
                ctx.lineTo(x + 0.5, top + plotHeight);
                ctx.stroke();
            }

            const span = Math.max(1, graph.toMs - graph.fromMs);
            const step = (graph.bucketMs / span) * plotWidth;

            function drawSeries(series, color, fill) {
                if (!series || series.length === 0)
                    return;
                // One path per contiguous run: a gap in the history means the
                // client was not sampling, not that traffic was zero.
                let runStart = -1;
                const runs = [];
                for (let i = 0; i < series.length; ++i) {
                    const valid = series[i] >= 0;
                    if (valid && runStart < 0) {
                        runStart = i;
                    } else if (!valid && runStart >= 0) {
                        runs.push([runStart, i - 1]);
                        runStart = -1;
                    }
                }
                if (runStart >= 0)
                    runs.push([runStart, series.length - 1]);

                ctx.strokeStyle = color;
                ctx.lineWidth = 1.4;
                for (let r = 0; r < runs.length; ++r) {
                    const first = runs[r][0];
                    const last = runs[r][1];
                    const xOf = (i) => (graph.gridStartMs + (i + 0.5) * graph.bucketMs
                                        - graph.fromMs) / span * plotWidth;
                    ctx.beginPath();
                    for (let i = first; i <= last; ++i) {
                        const x = xOf(i);
                        const y = top + plotHeight * (1 - Math.min(1, series[i] / peak));
                        if (i === first)
                            ctx.moveTo(x, y);
                        else
                            ctx.lineTo(x, y);
                    }
                    ctx.stroke();

                    if (fill && last > first) {
                        ctx.lineTo(xOf(last), top + plotHeight);
                        ctx.lineTo(xOf(first), top + plotHeight);
                        ctx.closePath();
                        ctx.fillStyle = Qt.rgba(color.r, color.g, color.b, 0.14);
                        ctx.fill();
                    }
                }
            }

            drawSeries(graph.txSeries, graph.txColor, false);
            drawSeries(graph.rxSeries, graph.rxColor, true);

            const elapsed = Date.now() - startedAt;
            graph.paintMs += elapsed;
            graph.paintCount += 1;;
        }
    }
}
