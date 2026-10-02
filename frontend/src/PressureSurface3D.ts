import * as THREE from "three";
import type { CentreOfPressure } from "./centreOfPressure";
import { OrbitControls } from
    "three/examples/jsm/controls/OrbitControls.js";

const ROWS = 28;
const COLUMNS = 56;
const CELL_COUNT = ROWS * COLUMNS;
const MAX_HEIGHT = 18;

export class PressureSurface3D {
    private readonly container: HTMLElement;
    private readonly scene: THREE.Scene;
    private readonly camera: THREE.PerspectiveCamera;
    private readonly renderer: THREE.WebGLRenderer;
    private readonly controls: OrbitControls;
    private readonly geometry: THREE.PlaneGeometry;
    private readonly positionAttribute: THREE.BufferAttribute;
    private readonly colorAttribute: THREE.BufferAttribute;
    private readonly resizeObserver: ResizeObserver;
    private readonly centreMarker: THREE.Sprite;

    public constructor(container: HTMLElement) {
        this.container = container;
        this.scene = new THREE.Scene();
        this.scene.background = new THREE.Color(0x03060b);

        this.camera = new THREE.PerspectiveCamera(
            45,
            2,
            0.1,
            500,
        );

        this.camera.position.set(40, 42, 48);
        this.camera.lookAt(0, 0, 0);

        this.renderer = new THREE.WebGLRenderer({
            antialias: true,
        });

        this.renderer.setPixelRatio(
            Math.min(window.devicePixelRatio, 2),
        );

        this.renderer.outputColorSpace =
            THREE.SRGBColorSpace;

        this.container.replaceChildren(
            this.renderer.domElement,
        );

        this.geometry = new THREE.PlaneGeometry(
            56,
            28,
            COLUMNS - 1,
            ROWS - 1,
        );

        // Change the plane from XY orientation to XZ orientation.
        this.geometry.rotateX(-Math.PI / 2);

        this.positionAttribute =
            this.geometry.getAttribute(
                "position",
            ) as THREE.BufferAttribute;

        const colors =
            new Float32Array(CELL_COUNT * 3);

        this.colorAttribute =
            new THREE.BufferAttribute(colors, 3);

        this.geometry.setAttribute(
            "color",
            this.colorAttribute,
        );

        const material =
            new THREE.MeshStandardMaterial({
                vertexColors: true,
                roughness: 0.72,
                metalness: 0.05,
                side: THREE.DoubleSide,
            });

        const surface =
            new THREE.Mesh(this.geometry, material);

        this.scene.add(surface);

        // A billboard keeps the marker legible when the camera rotates.
        const markerCanvas = document.createElement("canvas");
        markerCanvas.width = 64;
        markerCanvas.height = 64;
        const markerContext = markerCanvas.getContext("2d");
        if (markerContext === null) {
            throw new Error("2D Canvas is not supported");
        }
        markerContext.beginPath();
        markerContext.arc(32, 32, 25, 0, Math.PI * 2);
        markerContext.fillStyle = "#ff0000";
        markerContext.fill();
        markerContext.strokeStyle = "#ffffff";
        markerContext.lineWidth = 6;
        markerContext.stroke();

        const markerTexture = new THREE.CanvasTexture(markerCanvas);
        markerTexture.colorSpace = THREE.SRGBColorSpace;
        this.centreMarker = new THREE.Sprite(new THREE.SpriteMaterial({
            map: markerTexture,
            sizeAttenuation: false,
            depthTest: false,
            depthWrite: false,
        }));
        this.centreMarker.scale.set(0.085, 0.085, 1);
        this.centreMarker.renderOrder = 1;
        this.centreMarker.visible = false;
        this.scene.add(this.centreMarker);

        const grid = new THREE.GridHelper(
            70,
            28,
            0x334155,
            0x172033,
        );

        grid.position.y = -0.15;
        this.scene.add(grid);

        const ambientLight =
            new THREE.AmbientLight(0xffffff, 1.3);

        this.scene.add(ambientLight);

        const mainLight =
            new THREE.DirectionalLight(0xffffff, 2.6);

        mainLight.position.set(25, 45, 30);
        this.scene.add(mainLight);

        const fillLight =
            new THREE.DirectionalLight(0x5b8cff, 1.2);

        fillLight.position.set(-30, 20, -20);
        this.scene.add(fillLight);

        this.controls = new OrbitControls(
            this.camera,
            this.renderer.domElement,
        );

        this.controls.enableDamping = true;
        this.controls.dampingFactor = 0.08;
        this.controls.target.set(0, 4, 0);
        this.controls.minDistance = 30;
        this.controls.maxDistance = 130;
        this.controls.maxPolarAngle =
            Math.PI * 0.49;

        this.resizeObserver = new ResizeObserver(
            () => this.resize(),
        );

        this.resizeObserver.observe(this.container);

        this.resize();
        this.update(new Uint8Array(CELL_COUNT));
        this.animate();
    }

    public update(
        frame: Uint8Array,
        centre: CentreOfPressure | null = null,
    ): void {
        if (frame.length !== CELL_COUNT) {
            return;
        }

        for (let index = 0; index < CELL_COUNT; index++) {
            const normalized = frame[index] / 255;
            const height =
                Math.pow(normalized, 1.15) * MAX_HEIGHT;

            this.positionAttribute.setY(index, height);

            const [red, green, blue] =
                this.pressureColor(normalized);

            this.colorAttribute.setXYZ(
                index,
                red,
                green,
                blue,
            );
        }

        this.positionAttribute.needsUpdate = true;
        this.colorAttribute.needsUpdate = true;

        this.geometry.computeVertexNormals();
        this.updateCentreMarker(centre);
    }

    private updateCentreMarker(centre: CentreOfPressure | null): void {
        this.centreMarker.visible = centre !== null;
        if (centre === null) {
            return;
        }

        const column = Math.min(COLUMNS - 1, Math.max(0, centre.column));
        const row = Math.min(ROWS - 1, Math.max(0, centre.row));
        const left = Math.min(COLUMNS - 2, Math.floor(column));
        const top = Math.min(ROWS - 2, Math.floor(row));
        const fx = column - left;
        const fy = row - top;
        const a = top * COLUMNS + left;
        const b = a + COLUMNS;
        const c = b + 1;
        const d = a + 1;
        const positions = this.positionAttribute;

        // Interpolate the actual PlaneGeometry triangle beneath the marker.
        const height = fx + fy <= 1
            ? positions.getY(a)
                + fx * (positions.getY(d) - positions.getY(a))
                + fy * (positions.getY(b) - positions.getY(a))
            : positions.getY(c)
                + (1 - fx) * (positions.getY(b) - positions.getY(c))
                + (1 - fy) * (positions.getY(d) - positions.getY(c));

        this.centreMarker.position.set(
            positions.getX(a) + fx * (positions.getX(d) - positions.getX(a)),
            height + 0.35,
            positions.getZ(a) + fy * (positions.getZ(b) - positions.getZ(a)),
        );
    }

    private pressureColor(
        value: number,
    ): [number, number, number] {
        const anchors: Array<
            [number, number, number]
        > = [
            [3 / 255, 6 / 255, 15 / 255],
            [0, 60 / 255, 180 / 255],
            [0, 210 / 255, 220 / 255],
            [245 / 255, 225 / 255, 35 / 255],
            [1, 45 / 255, 20 / 255],
        ];

        const position =
            value * (anchors.length - 1);

        const lowerIndex = Math.floor(position);
        const upperIndex = Math.min(
            lowerIndex + 1,
            anchors.length - 1,
        );

        const fraction = position - lowerIndex;
        const lower = anchors[lowerIndex];
        const upper = anchors[upperIndex];

        return [
            lower[0] +
                (upper[0] - lower[0]) * fraction,
            lower[1] +
                (upper[1] - lower[1]) * fraction,
            lower[2] +
                (upper[2] - lower[2]) * fraction,
        ];
    }

    private resize(): void {
        const width = this.container.clientWidth;
        const height = this.container.clientHeight;

        if (width === 0 || height === 0) {
            return;
        }

        this.camera.aspect = width / height;
        this.camera.updateProjectionMatrix();

        this.renderer.setSize(width, height, false);
    }

    private readonly animate = (): void => {
        this.controls.update();
        this.renderer.render(
            this.scene,
            this.camera,
        );

        requestAnimationFrame(this.animate);
    };
}
