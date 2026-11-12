import QtQuick

// Rolling RX/TX line graph for one interface. Both series are byte-per-second
// values, newest on the right; the window is fixed at `capacity` samples so the
// trace scrolls left as samples arrive. Drawn with Canvas, no chart module.
Item {
    id: graph

    property var rxSeries: []
    property var txSeries: []
    property int capacity: 120
    property color rxColor: "#3fbf6a"
    property color txColor: "#4b9fe8"
    property color gridColor: "#2c2c2c"
    property color axisColor: "#8c8c8c"
    property color backgroundColor: "#151515"

    implicitHeight: 200
    implicitWidth: 480

    onRxSeriesChanged: canvas.requestPaint()
    onTxSeriesChanged: canvas.requestPaint()
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
        for (let i = 0; i < rxSeries.length; ++i)
            peak = Math.max(peak, rxSeries[i]);
        for (let i = 0; i < txSeries.length; ++i)
            peak = Math.max(peak, txSeries[i]);
        // A floor keeps an idle graph readable instead of dividing by zero.
        return Math.max(peak, 1024);
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: 1

        onPaint: {
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

            function drawSeries(series, color, fill) {
                if (!series || series.length === 0)
                    return;
                const step = plotWidth / (graph.capacity - 1);
                const offset = graph.capacity - series.length;
                ctx.beginPath();
                for (let i = 0; i < series.length; ++i) {
                    const x = (offset + i) * step;
                    const y = top + plotHeight * (1 - Math.min(1, series[i] / peak));
                    if (i === 0)
                        ctx.moveTo(x, y);
                    else
                        ctx.lineTo(x, y);
                }
                ctx.strokeStyle = color;
                ctx.lineWidth = 1.4;
                ctx.stroke();

                if (fill) {
                    ctx.lineTo((offset + series.length - 1) * step, top + plotHeight);
                    ctx.lineTo(offset * step, top + plotHeight);
                    ctx.closePath();
                    ctx.fillStyle = Qt.rgba(color.r, color.g, color.b, 0.14);
                    ctx.fill();
                }
            }

            drawSeries(graph.txSeries, graph.txColor, false);
            drawSeries(graph.rxSeries, graph.rxColor, true);
        }
    }
}
