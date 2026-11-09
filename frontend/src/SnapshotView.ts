import { calculateCentreOfPressure } from "./centreOfPressure";

type Snapshot = {
    type: "snapshot";
    id: string;
    rows: number;
    columns: number;
    layout: string;
    timing: string;
    triggerNs: string;
    beforeNs: string;
    afterNs: string;
    alpha: number;
    gapMs: number;
    values: number[];
};

export class SnapshotView {
    private canvas = document.querySelector<HTMLCanvasElement>("#snapshot-heatmap")!;
    private status = document.querySelector<HTMLElement>("#snapshot-status")!;
    private download = document.querySelector<HTMLButtonElement>("#download-snapshot")!;
    private snapshot: Snapshot | null = null;

    private colors: Uint8Array;

    constructor(colors: Uint8Array) {
        this.colors = colors;
        if (new URLSearchParams(location.search).get("snapshot") === "1") {
            document.body.classList.add("snapshot-only");
            document.title = "Frozen Pressure Snapshot";
        }
        this.download.addEventListener("click", () => {
            if (!this.snapshot) return;
            const url = URL.createObjectURL(new Blob(
                [JSON.stringify(this.snapshot, null, 2)], { type: "application/json" },
            ));
            const link = document.createElement("a");
            link.href = url;
            link.download = `pressure-snapshot-${this.snapshot.id}.json`;
            link.click();
            setTimeout(() => URL.revokeObjectURL(url), 1000);
        });
    }

    disconnected(): void {
        this.status.textContent = "Backend disconnected — showing the last captured snapshot";
    }

    handle(text: string): boolean {
        let message: Record<string, unknown>;
        try { message = JSON.parse(text); } catch { return false; }
        if (!message || typeof message !== "object") return false;
        if (message.type === "snapshot_pending") {
            this.status.textContent = `Capture ${message.id}: waiting for the next frame…`;
            return true;
        }
        if (message.type === "snapshot_error") {
            this.status.textContent = `Capture ${message.id} failed: ${message.message}. Previous snapshot retained.`;
            return true;
        }
        if (message.type !== "snapshot") return false;
        if (message.rows !== 28 || message.columns !== 56 ||
            message.layout !== "column-major" || !Array.isArray(message.values) ||
            message.values.length !== 1568 ||
            !message.values.every(v => typeof v === "number" && Number.isFinite(v) && v >= 0 && v <= 255) ||
            typeof message.gapMs !== "number" || !Number.isFinite(message.gapMs)) return true;
        this.snapshot = message as unknown as Snapshot;
        this.draw(this.snapshot.values);
        this.download.disabled = false;
        this.status.textContent = `Capture ${this.snapshot.id} frozen · frame interval ${this.snapshot.gapMs.toFixed(1)} ms · estimated at trigger receipt`;
        return true;
    }

    private draw(values: number[]): void {
        const source = document.createElement("canvas");
        source.width = 56;
        source.height = 28;
        const sourceContext = source.getContext("2d")!;
        const pixels = sourceContext.createImageData(56, 28);
        const displayValues = new Float64Array(1568);
        // Match main.ts: firmware wire order is x * ROWS + y.
        for (let y = 0; y < 28; y++) {
            for (let x = 0; x < 56; x++) {
                displayValues[y * 56 + x] = values[x * 28 + y];
                const color = Math.round(values[x * 28 + y]) * 3;
                const index = (y * 56 + x) * 4;
                pixels.data[index] = this.colors[color];
                pixels.data[index + 1] = this.colors[color + 1];
                pixels.data[index + 2] = this.colors[color + 2];
                pixels.data[index + 3] = 255;
            }
        }
        sourceContext.putImageData(pixels, 0, 0);
        const context = this.canvas.getContext("2d")!;
        context.imageSmoothingEnabled = true;
        context.imageSmoothingQuality = "high";
        context.clearRect(0, 0, this.canvas.width, this.canvas.height);
        context.drawImage(source, 0, 0, this.canvas.width, this.canvas.height);

        // Match the frozen heatmap's raw scale; preserve interpolated fractions.
        const centre = calculateCentreOfPressure(displayValues, 28, 56, 0);
        if (centre === null) return;
        const x = ((centre.column + 0.5) / 56) * this.canvas.width;
        const y = ((centre.row + 0.5) / 28) * this.canvas.height;
        const radius = 6 * this.canvas.width / (this.canvas.clientWidth || this.canvas.width);
        context.save();
        context.beginPath();
        context.arc(x, y, radius, 0, Math.PI * 2);
        context.fillStyle = "#ff0000";
        context.fill();
        context.strokeStyle = "#ffffff";
        context.lineWidth = radius * 0.25;
        context.stroke();
        context.restore();
    }
}
