export interface LoadRatios {
    top: number;
    bottom: number;
    left: number;
    right: number;
}

/** Percentages in the displayed mat orientation, before colour/height clipping. */
export function calculateLoadRatios(
    frame: Uint8Array,
    rows: number,
    columns: number,
    noiseThreshold: number,
): LoadRatios | null {
    if (frame.length !== rows * columns) {
        return null;
    }
    let a = 0;
    let b = 0;
    let c = 0;
    let d = 0;
    for (let row = 0; row < rows; row++) {
        for (let column = 0; column < columns; column++) {
            const weight = Math.max(0, frame[row * columns + column] - noiseThreshold);
            if (row < rows / 2) {
                if (column < columns / 2) a += weight;
                else b += weight;
            } else {
                if (column < columns / 2) c += weight;
                else d += weight;
            }
        }
    }
    const total = a + b + c + d;
    if (total === 0) {
        return null;
    }
    const top = ((a + b) / total) * 100;
    const left = ((a + c) / total) * 100;
    return { top, bottom: 100 - top, left, right: 100 - left };
}

/** One pair of bars, reused for both axes of both graphs. */
export class LoadRatioIndicator {
    private readonly output: HTMLOutputElement;
    private readonly firstValue: HTMLElement;
    private readonly secondValue: HTMLElement;
    private readonly firstFill: HTMLElement;
    private readonly secondFill: HTMLElement;

    public constructor(
        container: HTMLElement,
        caption: string,
        firstLabel: string,
        secondLabel: string,
    ) {
        // All labels are fixed application strings.
        container.innerHTML = `
            <span class="ratio-caption">${caption}</span>
            <output class="ratio-output" title="Percentages normalized to 100">— : —</output>
            <div class="ratio-bars" aria-hidden="true">
                <div class="ratio-bar-item">
                    <div class="ratio-track">
                        <div class="ratio-fill ratio-first-fill"></div>
                        <span class="ratio-value ratio-first-value">—</span>
                        <span class="ratio-cell-label">${firstLabel}</span>
                    </div>
                </div>
                <div class="ratio-bar-item">
                    <div class="ratio-track">
                        <div class="ratio-fill ratio-second-fill"></div>
                        <span class="ratio-value ratio-second-value">—</span>
                        <span class="ratio-cell-label">${secondLabel}</span>
                    </div>
                </div>
            </div>`;
        container.title = "Signal above the noise threshold. Top, bottom, left and right follow the 2D mat orientation, including when the 3D camera rotates.";
        this.output = container.querySelector<HTMLOutputElement>(".ratio-output")!;
        this.firstValue = container.querySelector<HTMLElement>(".ratio-first-value")!;
        this.secondValue = container.querySelector<HTMLElement>(".ratio-second-value")!;
        this.firstFill = container.querySelector<HTMLElement>(".ratio-first-fill")!;
        this.secondFill = container.querySelector<HTMLElement>(".ratio-second-fill")!;
        this.output.setAttribute("aria-label", `${caption}: no load`);
        // Avoid announcing rapidly changing sensor data on every frame.
        this.output.setAttribute("aria-live", "off");
    }

    public update(first: number | null, caption: string): void {
        if (first === null) {
            this.output.value = "— : —";
            this.output.setAttribute("aria-label", `${caption}: no load`);
            this.firstValue.textContent = "—";
            this.secondValue.textContent = "—";
            this.firstFill.style.setProperty("--load-percent", "0%");
            this.secondFill.style.setProperty("--load-percent", "0%");
            return;
        }
        // Round one side and complement it so the displayed pair totals 100.
        const firstPercent = Math.round(first * 10) / 10;
        const secondPercent = 100 - firstPercent;
        const firstText = firstPercent.toFixed(1);
        const secondText = secondPercent.toFixed(1);
        this.output.value = `${firstText} : ${secondText}`;
        this.output.setAttribute("aria-label", `${caption}: ${firstText}% to ${secondText}%`);
        this.firstValue.textContent = `${firstText}%`;
        this.secondValue.textContent = `${secondText}%`;
        this.firstFill.style.setProperty("--load-percent", `${firstPercent}%`);
        this.secondFill.style.setProperty("--load-percent", `${secondPercent}%`);
    }
}
