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

const canvas = requireElement<HTMLCanvasElement>("heatmap");
const context = canvas.getContext("2d");

if (context === null) {
    throw new Error("2D Canvas is not supported");
}

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

const surfaceContainer =
    requireElement<HTMLDivElement>("surface-3d");

const pressureSurface =
    new PressureSurface3D(surfaceContainer);

const sourceCanvas = document.createElement("canvas");
sourceCanvas.width = COLUMNS;
sourceCanvas.height = ROWS;

const sourceContext = sourceCanvas.getContext("2d");

if (sourceContext === null) {
    throw new Error("Could not create heatmap buffer");
}

const imageData = sourceContext.createImageData(COLUMNS, ROWS);
const colorTable = createColorTable();

let socket: WebSocket | null = null;
let latestFrame = new Uint8Array(CELL_COUNT);
let renderPending = false;
let totalFrames = 0;
let framesSinceReport = 0;
let readingActive = false;

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
        const value = latestFrame[index];
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

    pressureSurface.update(latestFrame);
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

function handleConfiguration(message: string): void {
    try {
        const configuration = JSON.parse(message);

        console.log("Backend configuration:", configuration);

        if (
            configuration.rows !== ROWS ||
            configuration.columns !== COLUMNS
        ) {
            console.warn(
                "Unexpected matrix dimensions:",
                configuration,
            );
        }
    } catch {
        console.warn("Unknown backend message:", message);
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
    const connected =
        socket?.readyState === WebSocket.OPEN;

    startButton.disabled =
        !connected || readingActive;

    stopButton.disabled =
        !connected || !readingActive;
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
        readingActive = true;
        updateControlButtons();
    
        console.log("Connected to pressure-mat backend");
    };

    socket.onmessage = (event: MessageEvent) => {
        if (typeof event.data === "string") {
            handleConfiguration(event.data);
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
        socket = null;
        updateControlButtons();
    
        window.setTimeout(connectWebSocket, 1000);
    };
}

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