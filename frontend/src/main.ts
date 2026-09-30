import "./style.css";
import { PressureSurface3D } from "./PressureSurface3D";

const ROWS = 28;
const COLUMNS = 56;
const CELL_COUNT = ROWS * COLUMNS;
const WEBSOCKET_URL = "ws://127.0.0.1:9002";

function requireElement<T extends HTMLElement>(id: string): T {
    const element = document.getElementById(id);

    if (element === null) {
        throw new Error(`Missing HTML element: ${id}`);
    }

    return element as T;
}

function require2DContext(
    canvasElement: HTMLCanvasElement,
): CanvasRenderingContext2D {
    const canvasContext =
        canvasElement.getContext("2d");

    if (canvasContext === null) {
        throw new Error(
            "2D Canvas is not supported",
        );
    }

    return canvasContext;
}

const canvas = requireElement<HTMLCanvasElement>("heatmap");
const context = require2DContext(canvas);

const statusDot = requireElement<HTMLSpanElement>("status-dot");
const connectionStatus =
    requireElement<HTMLSpanElement>("connection-status");
const frameRateElement =
    requireElement<HTMLElement>("frame-rate");
const frameCountElement =
    requireElement<HTMLElement>("frame-count");

const startButton =
    requireElement<HTMLButtonElement>("start-reading");

const stopButton =
    requireElement<HTMLButtonElement>("stop-reading");

const refreshPortsButton =
    requireElement<HTMLButtonElement>("refresh-ports");

const serialPortSelect =
    requireElement<HTMLSelectElement>("serial-port");

const baudRateSelect =
    requireElement<HTMLSelectElement>("baud-rate");

const connectSerialButton =
    requireElement<HTMLButtonElement>("connect-serial");

const disconnectSerialButton =
    requireElement<HTMLButtonElement>("disconnect-serial");

const surfaceContainer =
    requireElement<HTMLDivElement>("surface-3d");

const pressureSurface =
    new PressureSurface3D(surfaceContainer);

const noiseThresholdSlider =
    requireElement<HTMLInputElement>("noise-threshold");

const noiseThresholdValue =
    requireElement<HTMLOutputElement>(
        "noise-threshold-value",
    );

const displayMaximumSlider =
    requireElement<HTMLInputElement>("display-maximum");

const displayMaximumValue =
    requireElement<HTMLOutputElement>(
        "display-maximum-value",
    );

const sourceCanvas = document.createElement("canvas");
sourceCanvas.width = COLUMNS;
sourceCanvas.height = ROWS;

const sourceContext =
    require2DContext(sourceCanvas);

const imageData = sourceContext.createImageData(COLUMNS, ROWS);
const colorTable = createColorTable();

let socket: WebSocket | null = null;
let latestFrame = new Uint8Array(CELL_COUNT);
const visualizationFrame =
    new Uint8Array(CELL_COUNT);
let noiseThreshold = 0;
let displayMaximum = 255;
let renderPending = false;
let totalFrames = 0;
let framesSinceReport = 0;
let readingActive = false;
let serialConnected = false;

function createColorTable(): Uint8Array {
    const table = new Uint8Array(256 * 3);

    const anchors: Array<[number, number, number]> = [
        [3, 6, 15],
        [0, 60, 180],
        [0, 210, 220],
        [245, 225, 35],
        [255, 45, 20],
    ];

    for (let value = 0; value < 256; value++) {
        const position =
            (value / 255) * (anchors.length - 1);

        const lowerIndex = Math.floor(position);
        const upperIndex = Math.min(
            lowerIndex + 1,
            anchors.length - 1,
        );

        const fraction = position - lowerIndex;
        const lower = anchors[lowerIndex];
        const upper = anchors[upperIndex];

        for (let channel = 0; channel < 3; channel++) {
            table[value * 3 + channel] = Math.round(
                lower[channel] +
                (upper[channel] - lower[channel]) * fraction,
            );
        }
    }

    return table;
}

function renderHeatmap(): void {
    renderPending = false;

    for (let index = 0; index < CELL_COUNT; index++) {
        const rawValue = latestFrame[index];

        let value = 0;

        if (rawValue > noiseThreshold) {
            const displayRange = Math.max(
                1,
                displayMaximum - noiseThreshold,
            );

            value = Math.min(
                255,
                Math.round(
                    ((rawValue - noiseThreshold) * 255) /
                    displayRange,
                ),
            );
        }

        visualizationFrame[index] = value;
        const pixelOffset = index * 4;
        const colorOffset = value * 3;

        imageData.data[pixelOffset] =
            colorTable[colorOffset];

        imageData.data[pixelOffset + 1] =
            colorTable[colorOffset + 1];

        imageData.data[pixelOffset + 2] =
            colorTable[colorOffset + 2];

        imageData.data[pixelOffset + 3] = 255;
    }

    sourceContext.putImageData(imageData, 0, 0);

    context.imageSmoothingEnabled = true;
    context.imageSmoothingQuality = "high";

    context.clearRect(0, 0, canvas.width, canvas.height);
    context.drawImage(
        sourceCanvas,
        0,
        0,
        canvas.width,
        canvas.height,
    );

    pressureSurface.update(visualizationFrame);
}

function requestHeatmapRender(): void {
    if (renderPending) {
        return;
    }

    renderPending = true;
    requestAnimationFrame(renderHeatmap);
}

function setConnectionStatus(connected: boolean): void {
    statusDot.classList.toggle("connected", connected);
    connectionStatus.textContent =
        connected ? "Connected" : "Disconnected";
}

function handleBackendMessage(text: string): void {
    try {
        const message = JSON.parse(text) as {
            type?: string;
            rows?: number;
            columns?: number;
            ports?: unknown;
            baudRates?: unknown;
            connected?: boolean;
            reading?: boolean;
            port?: string;
        };

        if (message.type === "configuration") {
            console.log(
                "Backend configuration:",
                message,
            );

            if (
                message.rows !== ROWS ||
                message.columns !== COLUMNS
            ) {
                console.warn(
                    "Unexpected matrix dimensions:",
                    message,
                );
            }

            return;
        }

        if (message.type === "serial_ports") {
            const ports = Array.isArray(message.ports)
                ? message.ports.filter(
                    (value): value is string =>
                        typeof value === "string",
                )
                : [];

            const previousPort =
                serialPortSelect.value;

            serialPortSelect.replaceChildren();

            if (ports.length === 0) {
                serialPortSelect.add(
                    new Option(
                        "No ports detected",
                        "",
                    ),
                );
            } else {
                for (const port of ports) {
                    serialPortSelect.add(
                        new Option(port, port),
                    );
                }

                if (ports.includes(previousPort)) {
                    serialPortSelect.value =
                        previousPort;
                }
            }

            const baudRates =
                Array.isArray(message.baudRates)
                    ? message.baudRates.filter(
                        (value): value is number =>
                            typeof value === "number",
                    )
                    : [];

            if (baudRates.length > 0) {
                const previousBaud =
                    baudRateSelect.value;

                baudRateSelect.replaceChildren();

                for (const baudRate of baudRates) {
                    const value =
                        baudRate.toString();

                    baudRateSelect.add(
                        new Option(value, value),
                    );
                }

                if (
                    baudRates.includes(
                        Number(previousBaud),
                    )
                ) {
                    baudRateSelect.value =
                        previousBaud;
                }
            }

            updateControlButtons();
            return;
        }

        if (message.type === "serial_status") {
            serialConnected =
                message.connected === true;

            readingActive =
                serialConnected &&
                message.reading === true;

            if (
                message.port &&
                Array.from(
                    serialPortSelect.options,
                ).some(
                    (option) =>
                        option.value === message.port,
                )
            ) {
                serialPortSelect.value =
                    message.port;
            }

            statusDot.classList.add("connected");

            connectionStatus.textContent =
                serialConnected
                    ? `Mat connected (${message.port})`
                    : "Backend connected";

            updateControlButtons();
            return;
        }

        if (message.type === "serial_error") {
            const errorMessage =
                typeof (
                    message as { message?: unknown }
                ).message === "string"
                    ? (
                        message as { message: string }
                    ).message
                    : "Unknown serial error";
        
            serialConnected = false;
            readingActive = false;
        
            statusDot.classList.remove("connected");
            connectionStatus.textContent =
                `Serial error: ${errorMessage}`;
        
            updateControlButtons();
            return;
        }

        console.log("Backend message:", message);
    } catch {
        console.warn(
            "Unknown backend message:",
            text,
        );
    }
}

function handleFrame(buffer: ArrayBuffer): void {
    if (buffer.byteLength !== CELL_COUNT) {
        console.warn(
            `Ignored frame with ${buffer.byteLength} bytes`,
        );
        return;
    }

    const rawFrame = new Uint8Array(buffer);

    for (let x = 0; x < COLUMNS; x++) {
        for (let y = 0; y < ROWS; y++) {
            const rawIndex = x * ROWS + y;
            const displayIndex = y * COLUMNS + x;

            latestFrame[displayIndex] = rawFrame[rawIndex];
        }
    }

    totalFrames++;
    framesSinceReport++;

    requestHeatmapRender();
}

function updateControlButtons(): void {
    const backendConnected =
        socket?.readyState === WebSocket.OPEN;

    serialPortSelect.disabled =
        !backendConnected || serialConnected;

    baudRateSelect.disabled =
        !backendConnected || serialConnected;

    connectSerialButton.disabled =
        !backendConnected ||
        serialConnected ||
        serialPortSelect.value === "";

    disconnectSerialButton.disabled =
        !backendConnected || !serialConnected;

    startButton.disabled =
        !backendConnected ||
        !serialConnected ||
        readingActive;

    stopButton.disabled =
        !backendConnected ||
        !serialConnected ||
        !readingActive;
    
    refreshPortsButton.disabled =
        !backendConnected || serialConnected;
}

function connectWebSocket(): void {
    if (
        socket?.readyState === WebSocket.OPEN ||
        socket?.readyState === WebSocket.CONNECTING
    ) {
        return;
    }

    socket = new WebSocket(WEBSOCKET_URL);
    socket.binaryType = "arraybuffer";

    socket.onopen = () => {
        setConnectionStatus(true);
        readingActive = false;
        serialConnected = false;
        updateControlButtons();
    
        console.log("Connected to pressure-mat backend");
    };

    socket.onmessage = (event: MessageEvent) => {
        if (typeof event.data === "string") {
            handleBackendMessage(event.data);
            return;
        }

        if (event.data instanceof ArrayBuffer) {
            handleFrame(event.data);
        }
    };

    socket.onerror = () => {
        console.warn("Pressure-mat WebSocket unavailable");
    };

    socket.onclose = () => {
        setConnectionStatus(false);
        readingActive = false;
        serialConnected = false;
        socket = null;
        updateControlButtons();
    
        window.setTimeout(connectWebSocket, 1000);
    };
}

refreshPortsButton.addEventListener(
    "click",
    () => {
        if (
            socket?.readyState !== WebSocket.OPEN ||
            serialConnected
        ) {
            return;
        }

        socket.send(JSON.stringify({
            command: "refresh_ports",
        }));

        // Re-enabled when the refreshed list arrives.
        refreshPortsButton.disabled = true;
    },
);

serialPortSelect.addEventListener(
    "change",
    updateControlButtons,
);

connectSerialButton.addEventListener(
    "click",
    () => {
        if (
            socket?.readyState !== WebSocket.OPEN ||
            serialPortSelect.value === ""
        ) {
            return;
        }

        const command = {
            command: "connect",
            port: serialPortSelect.value,
            baudRate: Number(baudRateSelect.value),
        };

        socket.send(JSON.stringify(command));

        // Prevent repeated clicks while awaiting backend status.
        connectSerialButton.disabled = true;
    },
);

disconnectSerialButton.addEventListener(
    "click",
    () => {
        if (socket?.readyState !== WebSocket.OPEN) {
            return;
        }

        socket.send(JSON.stringify({
            command: "disconnect",
        }));

        disconnectSerialButton.disabled = true;
    },
);

noiseThresholdSlider.addEventListener(
    "input",
    () => {
        noiseThreshold = Math.min(
            Number(noiseThresholdSlider.value),
            254,
        );

        noiseThresholdSlider.value =
            noiseThreshold.toString();

        noiseThresholdValue.value =
            noiseThreshold.toString();

        if (displayMaximum <= noiseThreshold) {
            displayMaximum = noiseThreshold + 1;

            displayMaximumSlider.value =
                displayMaximum.toString();

            displayMaximumValue.value =
                displayMaximum.toString();
        }

        requestHeatmapRender();
    },
);

displayMaximumSlider.addEventListener(
    "input",
    () => {
        displayMaximum = Math.max(
            noiseThreshold + 1,
            Number(displayMaximumSlider.value),
        );

        displayMaximumSlider.value =
            displayMaximum.toString();

        displayMaximumValue.value =
            displayMaximum.toString();

        requestHeatmapRender();
    },
);

startButton.addEventListener("click", () => {
    if (socket?.readyState !== WebSocket.OPEN) {
        return;
    }

    socket.send("start_reading");
    readingActive = true;
    updateControlButtons();
});

stopButton.addEventListener("click", () => {
    if (socket?.readyState !== WebSocket.OPEN) {
        return;
    }

    socket.send("stop_reading");
    readingActive = false;
    updateControlButtons();
});

window.setInterval(() => {
    frameRateElement.textContent =
        `${framesSinceReport} FPS`;

    frameCountElement.textContent =
        totalFrames.toLocaleString();

    framesSinceReport = 0;
}, 1000);

renderHeatmap();
connectWebSocket();