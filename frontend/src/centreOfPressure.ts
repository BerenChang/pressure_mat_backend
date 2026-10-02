export interface CentreOfPressure {
    /** Zero-based column and row, measured at sensor centres. */
    column: number;
    row: number;
}

/** Use signal above the noise floor, before display scaling or clipping.
 * This estimates CoP from sensor values; calibrated force values are needed
 * for a physical centre of pressure when sensor response is nonlinear.
 */
export function calculateCentreOfPressure(
    frame: Uint8Array,
    rows: number,
    columns: number,
    noiseThreshold: number,
): CentreOfPressure | null {
    if (frame.length !== rows * columns) {
        return null;
    }

    let total = 0;
    let weightedColumn = 0;
    let weightedRow = 0;

    for (let row = 0; row < rows; row++) {
        for (let column = 0; column < columns; column++) {
            const weight = Math.max(
                0,
                frame[row * columns + column] - noiseThreshold,
            );
            total += weight;
            weightedColumn += column * weight;
            weightedRow += row * weight;
        }
    }

    return total > 0
        ? { column: weightedColumn / total, row: weightedRow / total }
        : null;
}
