<template>
    <BasePage
        :title="$t('moduleoverview.Title')"
        :isLoading="dataLoading"
        :isWideScreen="true"
        :showWebSocket="true"
        :isWebsocketConnected="isWebsocketConnected"
        :showReload="true"
        @reload="reloadData"
    >
        <BootstrapAlert v-model="alert.show" dismissible :variant="alert.type">
            {{ alert.message }}
        </BootstrapAlert>

        <div class="d-flex flex-wrap gap-2 align-items-center justify-content-between mb-3">
            <div class="d-flex flex-wrap gap-2 align-items-center module-overview-status-row">
                <InverterTotalInfo v-if="liveData.total" :totalData="liveData.total" class="module-overview-row-totals" />
                <div class="module-overview-status-badges">
                    <div class="card module-overview-status-card">
                        <div class="card-header text-bg-secondary">{{ $t('moduleoverview.Modules') }}</div>
                        <div class="card-body card-text">
                            <div class="module-overview-status-values">
                                <div class="module-overview-status-value">
                                    <span>{{ $t('moduleoverview.Modules') }}</span>
                                    <strong>{{ visibleModules.length }}</strong>
                                </div>
                                <div class="module-overview-status-value">
                                    <span>{{ $t('moduleoverview.Producing') }}</span>
                                    <strong>{{ producingCount }}</strong>
                                </div>
                                <div class="module-overview-status-value">
                                    <span>{{ $t('moduleoverview.Offline') }}</span>
                                    <strong>{{ offlineCount }}</strong>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
            <div class="d-flex flex-wrap gap-2 align-items-center">
                <select v-model="heatmapMode" class="form-select form-select-sm module-overview-select">
                    <option value="none">{{ $t('moduleoverview.HeatmapNone') }}</option>
                    <option value="power">{{ $t('moduleoverview.HeatmapPower') }}</option>
                    <option value="powerMax">{{ $t('moduleoverview.HeatmapPowerMax') }}</option>
                    <option value="powerDiff">{{ $t('moduleoverview.HeatmapPowerDiff') }}</option>
                    <option value="yieldDay">{{ $t('moduleoverview.HeatmapYieldDay') }}</option>
                    <option value="yieldDayDiff">{{ $t('moduleoverview.HeatmapYieldDayDiff') }}</option>
                </select>
                <select v-model.number="zoomFactor" class="form-select form-select-sm module-overview-select" :title="$t('moduleoverview.Zoom')">
                    <option :value="0.5">50%</option>
                    <option :value="0.75">75%</option>
                    <option :value="1">100%</option>
                    <option :value="1.25">125%</option>
                    <option :value="1.5">150%</option>
                </select>
                <div class="form-check form-switch mb-0">
                    <input
                        id="showDisabledModules"
                        v-model="showDisabledModules"
                        class="form-check-input"
                        type="checkbox"
                    />
                    <label class="form-check-label" for="showDisabledModules">
                        {{ $t('moduleoverview.ShowDisabled') }}
                    </label>
                </div>
                <div class="btn-group" role="group">
                    <button
                        type="button"
                        class="btn btn-outline-primary"
                        :class="{ active: backgroundDrawMode }"
                        :disabled="!editMode"
                        @click="toggleBackgroundDrawMode"
                    >
                        <BIconBrush />&nbsp;{{ $t('moduleoverview.DrawBackground') }}
                    </button>
                    <button type="button" class="btn btn-outline-secondary" :disabled="!editMode || backgroundPaths.length === 0" @click="undoBackgroundPath">
                        <BIconArrowCounterclockwise />&nbsp;{{ $t('moduleoverview.Undo') }}
                    </button>
                    <button type="button" class="btn btn-outline-danger" :disabled="!editMode || backgroundPaths.length === 0" @click="clearBackground">
                        <BIconTrash />&nbsp;{{ $t('moduleoverview.ClearBackground') }}
                    </button>
                </div>
                <div class="d-flex align-items-center gap-2 module-overview-draw-tools" :class="{ invisible: !editMode }">
                    <input v-model="backgroundStrokeColor" class="form-control form-control-color" type="color" :title="$t('moduleoverview.DrawColor')" />
                    <input
                        v-model.number="backgroundStrokeWidth"
                        class="form-range module-overview-stroke-width"
                        type="range"
                        min="1"
                        max="20"
                        :title="$t('moduleoverview.DrawWidth')"
                    />
                </div>
                <div class="btn-group" role="group">
                    <button type="button" class="btn btn-outline-primary" :class="{ active: editMode }" :disabled="!isLogged" @click="toggleEditMode">
                        <BIconPencilSquare />&nbsp;{{ $t('moduleoverview.EditMode') }}
                    </button>
                    <button type="button" class="btn btn-outline-secondary" :disabled="!editMode" @click="arrangeModules">
                        <BIconGrid3x3Gap />&nbsp;{{ $t('moduleoverview.Arrange') }}
                    </button>
                    <button type="button" class="btn btn-primary" :disabled="!isLogged || !editMode || layoutSaving" @click="saveLayout">
                        <BIconSave />&nbsp;{{ $t('moduleoverview.SaveLayout') }}
                    </button>
                </div>
            </div>
        </div>

        <BootstrapAlert :show="visibleModules.length === 0" variant="info">
            {{ $t('moduleoverview.NoModules') }}
        </BootstrapAlert>

        <div
            v-if="visibleModules.length > 0"
            ref="canvas"
            class="module-overview-canvas"
            :class="{ 'module-overview-canvas-edit': editMode }"
            :style="canvasStyle"
        >
            <div class="module-overview-zoom-spacer" :style="{ width: `${scaledCanvasWidth}px`, height: `${scaledCanvasHeight}px` }">
                <div class="module-overview-workspace" :style="workspaceStyle">
                    <div
                        v-if="editMode"
                        class="module-overview-grid"
                        :style="{ width: `${canvasWidth}px`, height: `${canvasHeight}px` }"
                    ></div>
                    <svg
                        ref="backgroundSvg"
                        class="module-overview-background"
                        :class="{ 'module-overview-background-draw': editMode && backgroundDrawMode }"
                        :width="canvasWidth"
                        :height="canvasHeight"
                        :viewBox="`0 0 ${canvasWidth} ${canvasHeight}`"
                        @pointerdown="onBackgroundPointerDown"
                        @pointermove="onBackgroundPointerMove"
                        @pointerleave="onBackgroundPointerLeave"
                    >
                        <path
                            v-for="path in backgroundPaths"
                            :key="path.id"
                            class="module-overview-background-path"
                            :d="pathData(path)"
                            :stroke="path.color"
                            :stroke-width="path.width"
                            fill="none"
                            stroke-linecap="round"
                            stroke-linejoin="round"
                            @pointerdown.stop="onBackgroundPathPointerDown($event, path.id)"
                            @contextmenu.prevent="confirmDeleteBackgroundPath(path.id)"
                        />
                        <line
                            v-if="backgroundPreviewLine !== null"
                            class="module-overview-background-preview"
                            :x1="backgroundPreviewLine.x1"
                            :y1="backgroundPreviewLine.y1"
                            :x2="backgroundPreviewLine.x2"
                            :y2="backgroundPreviewLine.y2"
                            :stroke="backgroundPreviewLine.color"
                            :stroke-width="backgroundPreviewLine.width"
                            stroke-linecap="round"
                        />
                        <template v-if="editMode && backgroundDrawMode">
                            <circle
                                v-for="point in backgroundControlPoints"
                                :key="`${point.pathId}:${point.pointIndex}`"
                                class="module-overview-background-point"
                                :class="{ 'module-overview-background-point-active': point.pathId === activeBackgroundPathId }"
                                :cx="point.x"
                                :cy="point.y"
                                r="5"
                                @pointerdown.stop="onBackgroundPointPointerDown($event, point.pathId, point.pointIndex)"
                                @contextmenu.prevent="confirmDeleteBackgroundPath(point.pathId)"
                            />
                        </template>
                    </svg>
                    <div
                        v-for="module in visibleModules"
                        :key="module.key"
                        class="module-card"
                        :class="[statusClass(module), { 'module-card-edit': editMode, 'module-card-dragging': dragState?.key === module.key }]"
                        :style="[moduleStyle(module.key), heatmapStyle(module)]"
                        :title="moduleDebugTitle(module)"
                        @pointerdown="onPointerDown($event, module.key)"
                    >
                        <div class="module-card-header">
                            <span class="module-title">{{ module.inverterName }}</span>
                            <span class="badge rounded-pill" :class="statusBadgeClass(module)">
                                {{ $t('moduleoverview.Channel', { channel: module.channel + 1 }) }}
                            </span>
                        </div>
                        <div class="module-power">{{ formatValue(module.Power) }}</div>
                        <div class="module-values">
                            <span>{{ formatValue(module.Voltage) }}</span>
                            <span>{{ formatValue(module.Current) }}</span>
                            <span>{{ formatValue(module.YieldDay) }}</span>
                        </div>
                        <div class="module-key">{{ module.key }}</div>
                    </div>
                </div>
            </div>
        </div>
    </BasePage>
</template>

<script lang="ts">
import BasePage from '@/components/BasePage.vue';
import BootstrapAlert from '@/components/BootstrapAlert.vue';
import InverterTotalInfo from '@/components/InverterTotalInfo.vue';
import type { AlertResponse } from '@/types/AlertResponse';
import type { Inverter, InverterStatistics, LiveData, ValueObject } from '@/types/LiveDataStatus';
import { authHeader, authUrl, handleResponse, isLoggedIn } from '@/utils/authentication';
import { waitRestart } from '@/utils/waitRestart';
import WebSocketService from '@/utils/websocketService';
import { BIconArrowCounterclockwise, BIconBrush, BIconGrid3x3Gap, BIconPencilSquare, BIconSave, BIconTrash } from 'bootstrap-icons-vue';
import type { CSSProperties } from 'vue';
import { defineComponent } from 'vue';

interface ModulePosition {
    x: number;
    y: number;
}

interface DrawingPoint {
    x: number;
    y: number;
}

interface BackgroundPath {
    id: number;
    color: string;
    width: number;
    points: DrawingPoint[];
    closed: boolean;
}

interface BackgroundControlPoint extends DrawingPoint {
    pathId: number;
    pointIndex: number;
}

interface BackgroundPreviewLine {
    x1: number;
    y1: number;
    x2: number;
    y2: number;
    color: string;
    width: number;
}

interface ModuleOverviewLayout {
    version: number;
    zoomFactor?: number;
    heatmapMode?: HeatmapMode;
    showDisabledModules?: boolean;
    modules?: Array<{
        key: string;
        x: number;
        y: number;
    }>;
    backgroundPaths?: BackgroundPath[];
}

const MODULE_WIDTH = 150;
const MODULE_HEIGHT = 250;
const MODULE_GAP = 18;
const PLACEMENT_GRID_SIZE = 16;
const CANVAS_MIN_WIDTH = 960;
const CANVAS_MIN_HEIGHT = 320;
const CANVAS_MIN_VIEWPORT_HEIGHT = 320;
const CANVAS_BOTTOM_GAP = 16;
const CANVAS_VERTICAL_OVERFLOW_TOLERANCE = 24;
const MODULE_OVERVIEW_LAYOUT_FILE = 'module_overview.json';

interface ModuleItem {
    key: string;
    inverterName: string;
    serial: string;
    channel: number;
    pollEnabled: boolean;
    reachable: boolean;
    producing: boolean;
    powerMaximum: number;
    Power?: ValueObject;
    Voltage?: ValueObject;
    Current?: ValueObject;
    YieldDay?: ValueObject;
}

type HeatmapMode = 'none' | 'power' | 'powerMax' | 'powerDiff' | 'yieldDay' | 'yieldDayDiff';

export default defineComponent({
    components: {
        BasePage,
        BIconArrowCounterclockwise,
        BIconBrush,
        BootstrapAlert,
        BIconGrid3x3Gap,
        InverterTotalInfo,
        BIconPencilSquare,
        BIconSave,
        BIconTrash,
    },
    data() {
        return {
            socket: {} as WebSocketService,
            dataLoading: true,
            layoutSaving: false,
            isLogged: isLoggedIn(),
            alert: {} as AlertResponse,
            liveData: { inverters: [] } as unknown as LiveData,
            isWebsocketConnected: false,
            editMode: false,
            showDisabledModules: false,
            heatmapMode: 'none' as HeatmapMode,
            zoomFactor: 1,
            canvasAvailableHeight: CANVAS_MIN_VIEWPORT_HEIGHT,
            positions: {} as Record<string, ModulePosition>,
            backgroundDrawMode: false,
            backgroundStrokeColor: '#5b8def',
            backgroundStrokeWidth: 4,
            backgroundPaths: [] as BackgroundPath[],
            backgroundPathSequence: 0,
            activeBackgroundPathId: null as number | null,
            backgroundHoverPoint: null as DrawingPoint | null,
            backgroundPointDragState: null as
                | {
                      pointerId: number;
                      pathId: number;
                      pointIndex: number;
                      startX: number;
                      startY: number;
                      moved: boolean;
                  }
                | null,
            dragState: null as
                | {
                      key: string;
                      pointerId: number;
                      offsetX: number;
                      offsetY: number;
                  }
                | null,
        };
    },
    created() {
        this.loadLayout();
        this.getInitialData();
        this.initSocket();
        this.$emitter.on('logged-in', () => {
            this.isLogged = isLoggedIn();
        });
        this.$emitter.on('logged-out', () => {
            this.isLogged = isLoggedIn();
        });
    },
    mounted() {
        window.addEventListener('keydown', this.onBackgroundKeyDown);
        window.addEventListener('resize', this.updateCanvasAvailableHeight);
        this.updateCanvasAvailableHeightAfterRender();
    },
    unmounted() {
        this.clearDragListeners();
        this.clearBackgroundPointDragListeners();
        window.removeEventListener('resize', this.updateCanvasAvailableHeight);
        window.removeEventListener('keydown', this.onBackgroundKeyDown);
        this.socket?.close();
    },
    watch: {
        editMode() {
            this.updateCanvasAvailableHeightAfterRender();
        },
        visibleModules() {
            this.updateCanvasAvailableHeightAfterRender();
        },
        showDisabledModules() {
            this.updateCanvasAvailableHeightAfterRender();
        },
    },
    computed: {
        modules(): ModuleItem[] {
            const modulesByKey = new Map<string, ModuleItem>();

            (this.liveData.inverters || [])
                .slice()
                .sort((a: Inverter, b: Inverter) => a.order - b.order)
                .forEach((inverter: Inverter) => {
                    const dcChannels = Object.entries(inverter.DC || {}) as [string, InverterStatistics][];
                    const fallbackPowerMaximum =
                        inverter.limit_absolute > 0 && dcChannels.length > 0 ? inverter.limit_absolute / dcChannels.length : 0;

                    dcChannels
                        .sort(([a], [b]) => Number(a) - Number(b))
                        .forEach(([channelKey, channelData]) => {
                            const channel = Number(channelKey);
                            if (!Number.isFinite(channel)) {
                                return;
                            }

                            const key = `${inverter.serial}:DC:${channel}`;
                            modulesByKey.set(key, {
                                key,
                                inverterName: inverter.name,
                                serial: inverter.serial,
                                channel,
                                pollEnabled: inverter.poll_enabled,
                                reachable: inverter.reachable,
                                producing: inverter.producing,
                                powerMaximum: channelData.Power?.max ?? fallbackPowerMaximum,
                                Power: channelData.Power,
                                Voltage: channelData.Voltage,
                                Current: channelData.Current,
                                YieldDay: channelData.YieldDay,
                            });
                        });
                });

            return Array.from(modulesByKey.values());
        },
        visibleModules(): ModuleItem[] {
            if (this.showDisabledModules) {
                return this.modules;
            }

            return this.modules.filter((module) => module.pollEnabled);
        },
        producingCount(): number {
            return this.visibleModules.filter((module) => module.pollEnabled && module.reachable && module.producing).length;
        },
        offlineCount(): number {
            return this.visibleModules.filter((module) => module.pollEnabled && !module.reachable).length;
        },
        heatmapMaximum(): number {
            if (this.heatmapMode === 'none' || this.heatmapMode === 'powerMax' || this.isHeatmapDifferenceMode()) {
                return 0;
            }

            return Math.max(...this.heatmapModules.map((module) => this.heatmapValue(module)), 0);
        },
        heatmapDifferenceRange(): { minimum: number; maximum: number } | null {
            if (!this.isHeatmapDifferenceMode()) {
                return null;
            }

            const values = this.heatmapModules.map((module) => this.heatmapValue(module));
            if (values.length === 0) {
                return null;
            }

            return {
                minimum: Math.min(...values),
                maximum: Math.max(...values),
            };
        },
        heatmapModules(): ModuleItem[] {
            return this.visibleModules.filter((module) => module.pollEnabled);
        },
        backgroundControlPoints(): BackgroundControlPoint[] {
            return this.backgroundPaths.flatMap((path) =>
                path.points.map((point, pointIndex) => ({
                    ...point,
                    pathId: path.id,
                    pointIndex,
                }))
            );
        },
        backgroundPreviewLine(): BackgroundPreviewLine | null {
            if (!this.editMode || !this.backgroundDrawMode || this.backgroundHoverPoint === null || this.backgroundPointDragState !== null) {
                return null;
            }

            const activePath = this.backgroundPaths.find((path) => path.id === this.activeBackgroundPathId);
            const lastPoint = activePath?.points[activePath.points.length - 1];
            if (activePath === undefined || activePath.closed || lastPoint === undefined) {
                return null;
            }

            return {
                x1: lastPoint.x,
                y1: lastPoint.y,
                x2: this.backgroundHoverPoint.x,
                y2: this.backgroundHoverPoint.y,
                color: activePath.color,
                width: activePath.width,
            };
        },
        canvasWidth(): number {
            const maxModuleX = Math.max(...Object.values(this.positions).map((position) => position.x + MODULE_WIDTH + MODULE_GAP), 0);
            const maxPathX = Math.max(...this.backgroundPaths.flatMap((path) => path.points.map((point) => point.x + path.width + MODULE_GAP)), 0);

            return Math.max(CANVAS_MIN_WIDTH, Math.ceil(maxModuleX), Math.ceil(maxPathX));
        },
        canvasHeight(): number {
            const maxModuleY = Math.max(...Object.values(this.positions).map((position) => position.y + MODULE_HEIGHT + MODULE_GAP), 0);
            const maxPathY = Math.max(...this.backgroundPaths.flatMap((path) => path.points.map((point) => point.y + path.width + MODULE_GAP)), 0);
            const visibleCanvasHeight = Math.ceil(this.canvasAvailableHeight / this.zoomFactor);

            return Math.max(CANVAS_MIN_HEIGHT, visibleCanvasHeight, Math.ceil(maxModuleY), Math.ceil(maxPathY));
        },
        scaledCanvasWidth(): number {
            return Math.ceil(this.canvasWidth * this.zoomFactor);
        },
        scaledCanvasHeight(): number {
            return Math.ceil(this.canvasHeight * this.zoomFactor);
        },
        workspaceStyle() {
            return {
                width: `${this.canvasWidth}px`,
                height: `${this.canvasHeight}px`,
                transform: `scale(${this.zoomFactor})`,
            };
        },
        canvasStyle(): CSSProperties {
            return {
                height: `${this.canvasAvailableHeight}px`,
                overflowY:
                    this.scaledCanvasHeight > this.canvasAvailableHeight + CANVAS_VERTICAL_OVERFLOW_TOLERANCE ? 'auto' : 'hidden',
            };
        },
    },
    methods: {
        updateCanvasAvailableHeightAfterRender() {
            this.$nextTick(() => {
                window.setTimeout(() => {
                    this.updateCanvasAvailableHeight();
                }, 0);
            });
        },
        updateCanvasAvailableHeight() {
            const canvas = this.$refs.canvas as HTMLElement | undefined;
            if (canvas === undefined) {
                return;
            }

            const rect = canvas.getBoundingClientRect();
            const availableHeight = window.innerHeight - rect.top - CANVAS_BOTTOM_GAP;
            const nextCanvasAvailableHeight = Math.max(CANVAS_MIN_VIEWPORT_HEIGHT, Math.floor(availableHeight));
            if (this.canvasAvailableHeight !== nextCanvasAvailableHeight) {
                this.canvasAvailableHeight = nextCanvasAvailableHeight;
            }
        },
        getInitialData(triggerLoading: boolean = true) {
            if (triggerLoading) {
                this.dataLoading = true;
            }
            fetch('/api/livedata/status', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data) => {
                    this.liveData = data;
                    this.ensureModulePositions();
                    if (triggerLoading) {
                        this.dataLoading = false;
                        this.updateCanvasAvailableHeightAfterRender();
                    }
                });
        },
        loadLayout() {
            fetch(`/api/file/get?file=${MODULE_OVERVIEW_LAYOUT_FILE}`, { headers: authHeader() })
                .then((response) => {
                    if (response.status === 404) {
                        return null;
                    }
                    if (!response.ok) {
                        return handleResponse(response, this.$emitter, this.$router, true);
                    }
                    return response.json();
                })
                .then((layout: ModuleOverviewLayout | null) => {
                    if (layout === null) {
                        return;
                    }
                    this.applyLayout(layout);
                })
                .catch(() => {
                    this.alert.message = this.$t('moduleoverview.LoadLayoutFailed');
                    this.alert.type = 'warning';
                    this.alert.show = true;
                });
        },
        applyLayout(layout: ModuleOverviewLayout) {
            if (layout.version !== 1) {
                return;
            }

            const positions = {} as Record<string, ModulePosition>;
            (layout.modules || []).forEach((module) => {
                if (typeof module.key !== 'string' || !Number.isFinite(module.x) || !Number.isFinite(module.y)) {
                    return;
                }

                positions[module.key] = {
                    x: Math.max(0, this.snapToGrid(module.x)),
                    y: Math.max(0, this.snapToGrid(module.y)),
                };
            });

            this.positions = {
                ...this.positions,
                ...positions,
            };
            if (this.isValidZoomFactor(layout.zoomFactor)) {
                this.zoomFactor = layout.zoomFactor;
            }
            if (this.isValidHeatmapMode(layout.heatmapMode)) {
                this.heatmapMode = layout.heatmapMode;
            }
            if (typeof layout.showDisabledModules === 'boolean') {
                this.showDisabledModules = layout.showDisabledModules;
            }
            this.backgroundPaths = this.normalizeBackgroundPaths(layout.backgroundPaths || []);
            this.backgroundPathSequence = Math.max(...this.backgroundPaths.map((path) => path.id), 0);
            this.activeBackgroundPathId = null;
            this.backgroundHoverPoint = null;
            if (this.modules.length > 0) {
                this.ensureModulePositions();
            }
            this.updateCanvasAvailableHeightAfterRender();
        },
        normalizeBackgroundPaths(paths: BackgroundPath[]): BackgroundPath[] {
            return paths
                .filter((path) => Number.isFinite(path.id) && Array.isArray(path.points) && path.points.length > 0)
                .map((path) => ({
                    id: Math.trunc(path.id),
                    color: typeof path.color === 'string' ? path.color : '#5b8def',
                    width: Number.isFinite(path.width) ? Math.max(1, Math.trunc(path.width)) : 4,
                    closed: path.closed === true,
                    points: path.points
                        .filter((point) => Number.isFinite(point.x) && Number.isFinite(point.y))
                        .map((point) => ({
                            x: Math.max(0, Math.round(point.x)),
                            y: Math.max(0, Math.round(point.y)),
                        })),
                }))
                .filter((path) => path.points.length > 0);
        },
        isValidZoomFactor(zoomFactor?: number): zoomFactor is number {
            return zoomFactor === 0.5 || zoomFactor === 0.75 || zoomFactor === 1 || zoomFactor === 1.25 || zoomFactor === 1.5;
        },
        isValidHeatmapMode(heatmapMode?: HeatmapMode): heatmapMode is HeatmapMode {
            return (
                heatmapMode === 'none' ||
                heatmapMode === 'power' ||
                heatmapMode === 'powerMax' ||
                heatmapMode === 'powerDiff' ||
                heatmapMode === 'yieldDay' ||
                heatmapMode === 'yieldDayDiff'
            );
        },
        saveLayout() {
            this.layoutSaving = true;

            const formData = new FormData();
            const layoutBlob = new Blob([JSON.stringify(this.buildLayout())], { type: 'application/json' });
            formData.append('module_overview', layoutBlob, MODULE_OVERVIEW_LAYOUT_FILE);

            fetch(`/api/file/upload?file=${MODULE_OVERVIEW_LAYOUT_FILE}`, {
                method: 'POST',
                headers: authHeader(),
                body: formData,
            })
                .then((response) => {
                    if (!response.ok) {
                        return handleResponse(response, this.$emitter, this.$router);
                    }

                    this.alert.message = this.$t('apiresponse.1001');
                    this.alert.type = 'success';
                    this.alert.show = true;
                    waitRestart(this.$router);
                })
                .finally(() => {
                    this.layoutSaving = false;
                });
        },
        buildLayout(): ModuleOverviewLayout {
            return {
                version: 1,
                zoomFactor: this.zoomFactor,
                heatmapMode: this.heatmapMode,
                showDisabledModules: this.showDisabledModules,
                modules: Object.entries(this.positions)
                    .filter(([key]) => this.modules.some((module) => module.key === key))
                    .map(([key, position]) => ({
                        key,
                        x: Math.round(position.x),
                        y: Math.round(position.y),
                    })),
                backgroundPaths: this.backgroundPaths.map((path) => ({
                    id: path.id,
                    color: path.color,
                    width: path.width,
                    closed: path.closed,
                    points: path.points.map((point) => ({
                        x: Math.round(point.x),
                        y: Math.round(point.y),
                    })),
                })),
            };
        },
        reloadData() {
            this.socket?.close();
            this.getInitialData(false);
            this.initSocket();
        },
        initSocket() {
            const { protocol, host } = location;
            const authString = authUrl();
            const webSocketUrl = `${protocol === 'https:' ? 'wss' : 'ws'}://${authString}${host}/livedata`;

            this.socket = new WebSocketService(webSocketUrl, {
                onMessage: this.handleMessage,
                onOpen: () => {
                    this.isWebsocketConnected = true;
                },
                onClose: () => {
                    this.isWebsocketConnected = false;
                },
            });

            window.onbeforeunload = () => {
                this.socket?.close();
            };

            this.socket?.connect();
        },
        handleMessage(event: MessageEvent) {
            if (!event.data || event.data === '{}') {
                this.socket?.close();
                this.initSocket();
                return;
            }

            const newData = JSON.parse(event.data);
            if (!this.liveData.inverters) {
                this.liveData.inverters = [];
            }

            Object.assign(this.liveData.total || {}, newData.total);
            Object.assign(this.liveData.hints || {}, newData.hints);

            const idx = this.liveData.inverters.findIndex((i) => i.serial === newData.inverters[0].serial);
            if (idx === -1) {
                this.liveData.inverters.push(newData.inverters[0]);
            } else if (this.liveData.inverters[idx] !== undefined) {
                Object.assign(this.liveData.inverters[idx], newData.inverters[0]);
            }

            this.ensureModulePositions();
            this.updateCanvasAvailableHeightAfterRender();
        },
        ensureModulePositions() {
            const nextPositions = { ...this.positions } as Record<string, ModulePosition>;
            const occupiedPositions = new Set(Object.values(nextPositions).map((position) => this.positionKey(position)));

            this.visibleModules.forEach((module, index) => {
                const existingPosition = this.positions[module.key];
                const existingPositionKey = existingPosition !== undefined ? this.positionKey(existingPosition) : '';

                if (existingPosition !== undefined) {
                    nextPositions[module.key] = existingPosition;
                    occupiedPositions.add(existingPositionKey);
                } else {
                    const nextPosition = this.defaultPosition(index, occupiedPositions);
                    nextPositions[module.key] = nextPosition;
                    occupiedPositions.add(this.positionKey(nextPosition));
                }
            });

            this.positions = nextPositions;
        },
        defaultPosition(index: number, occupiedPositions: Set<string> = new Set()): ModulePosition {
            const stepX = this.snapToGrid(MODULE_WIDTH + MODULE_GAP);
            const stepY = this.snapToGrid(MODULE_HEIGHT + MODULE_GAP);
            const canvas = this.$refs.canvas as HTMLElement | undefined;
            const visibleCanvasWidth = canvas !== undefined ? canvas.clientWidth / this.zoomFactor : CANVAS_MIN_WIDTH;
            const columns = Math.max(1, Math.floor(visibleCanvasWidth / stepX));

            let nextIndex = index;
            let position = {
                x: PLACEMENT_GRID_SIZE + (nextIndex % columns) * stepX,
                y: PLACEMENT_GRID_SIZE + Math.floor(nextIndex / columns) * stepY,
            };

            while (occupiedPositions.has(this.positionKey(position))) {
                nextIndex += 1;
                position = {
                    x: PLACEMENT_GRID_SIZE + (nextIndex % columns) * stepX,
                    y: PLACEMENT_GRID_SIZE + Math.floor(nextIndex / columns) * stepY,
                };
            }

            return position;
        },
        positionKey(position: ModulePosition): string {
            return `${Math.round(position.x)}:${Math.round(position.y)}`;
        },
        snapToGrid(value: number): number {
            return Math.round(value / PLACEMENT_GRID_SIZE) * PLACEMENT_GRID_SIZE;
        },
        arrangeModules() {
            const nextPositions = {} as Record<string, ModulePosition>;
            this.visibleModules.forEach((module, index) => {
                nextPositions[module.key] = this.defaultPosition(index);
            });
            this.positions = nextPositions;
        },
        toggleEditMode() {
            this.editMode = !this.editMode;
            if (!this.editMode) {
                this.backgroundDrawMode = false;
                this.backgroundHoverPoint = null;
                this.clearBackgroundPointDragListeners();
            }
        },
        toggleBackgroundDrawMode() {
            if (!this.editMode) {
                return;
            }

            this.backgroundDrawMode = !this.backgroundDrawMode;
            this.backgroundHoverPoint = null;
            if (!this.backgroundDrawMode) {
                this.clearBackgroundPointDragListeners();
            }
        },
        onPointerDown(event: PointerEvent, key: string) {
            if (!this.editMode || this.backgroundDrawMode) {
                return;
            }

            event.preventDefault();
            this.clearDragListeners();

            const position = this.positions[key] || { x: 0, y: 0 };
            const pointerPosition = this.canvasPointerPosition(event);

            this.dragState = {
                key,
                pointerId: event.pointerId,
                offsetX: pointerPosition.x - position.x,
                offsetY: pointerPosition.y - position.y,
            };

            (event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);
            window.addEventListener('pointermove', this.onPointerMove);
            window.addEventListener('pointerup', this.onPointerUp);
            window.addEventListener('pointercancel', this.onPointerUp);
        },
        onPointerMove(event: PointerEvent) {
            if (!this.dragState || event.pointerId !== this.dragState.pointerId) {
                return;
            }

            const pointerPosition = this.canvasPointerPosition(event);
            const x = pointerPosition.x - this.dragState.offsetX;
            const y = pointerPosition.y - this.dragState.offsetY;
            const snappedX = this.snapToGrid(x);
            const snappedY = this.snapToGrid(y);

            this.positions = {
                ...this.positions,
                [this.dragState.key]: {
                    x: Math.max(0, Math.min(snappedX, this.canvasWidth - MODULE_WIDTH)),
                    y: Math.max(0, Math.min(snappedY, this.canvasHeight - MODULE_HEIGHT)),
                },
            };
        },
        onPointerUp(event: PointerEvent) {
            if (!this.dragState || event.pointerId !== this.dragState.pointerId) {
                return;
            }

            this.dragState = null;
            this.clearDragListeners();
        },
        clearDragListeners() {
            window.removeEventListener('pointermove', this.onPointerMove);
            window.removeEventListener('pointerup', this.onPointerUp);
            window.removeEventListener('pointercancel', this.onPointerUp);
        },
        onBackgroundPointerDown(event: PointerEvent) {
            if (!this.editMode || !this.backgroundDrawMode) {
                return;
            }

            if (event.button !== 0) {
                return;
            }

            event.preventDefault();
            const point = this.backgroundPointerPosition(event);
            this.backgroundHoverPoint = point;

            const activePath = this.backgroundPaths.find((path) => path.id === this.activeBackgroundPathId);
            if (
                activePath === undefined ||
                activePath.closed ||
                activePath.color !== this.backgroundStrokeColor ||
                activePath.width !== this.backgroundStrokeWidth
            ) {
                const pathId = this.backgroundPathSequence + 1;
                this.backgroundPathSequence = pathId;
                this.activeBackgroundPathId = pathId;
                this.backgroundPaths = [
                    ...this.backgroundPaths,
                    {
                        id: pathId,
                        color: this.backgroundStrokeColor,
                        width: this.backgroundStrokeWidth,
                        points: [point],
                        closed: false,
                    },
                ];
                return;
            }

            this.backgroundPaths = this.backgroundPaths.map((path) => {
                if (path.id !== activePath.id) {
                    return path;
                }

                return {
                    ...path,
                    points: [...path.points, point],
                };
            });
        },
        onBackgroundPointerMove(event: PointerEvent) {
            if (!this.editMode || !this.backgroundDrawMode) {
                return;
            }

            this.backgroundHoverPoint = this.backgroundPointerPosition(event);
        },
        onBackgroundPointerLeave() {
            if (this.backgroundPointDragState === null) {
                this.backgroundHoverPoint = null;
            }
        },
        onBackgroundPathPointerDown(event: PointerEvent, pathId: number) {
            if (!this.editMode || !this.backgroundDrawMode || event.button !== 0) {
                return;
            }

            event.preventDefault();
            const point = this.backgroundPointerPosition(event);
            this.insertBackgroundPointOnPath(pathId, point);
            this.backgroundHoverPoint = point;
        },
        onBackgroundPointPointerDown(event: PointerEvent, pathId: number, pointIndex: number) {
            if (!this.editMode || !this.backgroundDrawMode) {
                return;
            }

            if (event.button !== 0) {
                return;
            }

            event.preventDefault();
            this.clearBackgroundPointDragListeners();
            this.activeBackgroundPathId = pathId;
            const point = this.backgroundPointerPosition(event);
            this.backgroundPointDragState = {
                pointerId: event.pointerId,
                pathId,
                pointIndex,
                startX: point.x,
                startY: point.y,
                moved: false,
            };
            this.backgroundHoverPoint = point;

            (event.currentTarget as SVGCircleElement).setPointerCapture(event.pointerId);
            window.addEventListener('pointermove', this.onBackgroundPointPointerMove);
            window.addEventListener('pointerup', this.onBackgroundPointPointerUp);
            window.addEventListener('pointercancel', this.onBackgroundPointPointerUp);
        },
        onBackgroundPointPointerMove(event: PointerEvent) {
            if (!this.backgroundPointDragState || event.pointerId !== this.backgroundPointDragState.pointerId) {
                return;
            }

            const point = this.backgroundPointerPosition(event);
            this.backgroundHoverPoint = point;
            const moved =
                this.backgroundPointDragState.moved ||
                Math.hypot(point.x - this.backgroundPointDragState.startX, point.y - this.backgroundPointDragState.startY) > 3;
            this.backgroundPointDragState = {
                ...this.backgroundPointDragState,
                moved,
            };
            this.backgroundPaths = this.backgroundPaths.map((path) => {
                if (path.id !== this.backgroundPointDragState?.pathId) {
                    return path;
                }

                return {
                    ...path,
                    points: path.points.map((existingPoint, pointIndex) =>
                        pointIndex === this.backgroundPointDragState?.pointIndex ? point : existingPoint
                    ),
                };
            });
        },
        onBackgroundPointPointerUp(event: PointerEvent) {
            if (!this.backgroundPointDragState || event.pointerId !== this.backgroundPointDragState.pointerId) {
                return;
            }

            const dragState = this.backgroundPointDragState;
            const point = this.backgroundPointerPosition(event);
            if (!dragState.moved) {
                this.closeBackgroundPathAtPoint(dragState.pathId, dragState.pointIndex);
            } else {
                const closingPointIndex = this.findBackgroundClosingPointIndex(dragState.pathId, dragState.pointIndex, point);
                if (closingPointIndex !== null) {
                    this.closeBackgroundPathAtPoint(dragState.pathId, closingPointIndex, true);
                }
            }

            this.backgroundPointDragState = null;
            this.clearBackgroundPointDragListeners();
        },
        clearBackgroundPointDragListeners() {
            window.removeEventListener('pointermove', this.onBackgroundPointPointerMove);
            window.removeEventListener('pointerup', this.onBackgroundPointPointerUp);
            window.removeEventListener('pointercancel', this.onBackgroundPointPointerUp);
        },
        onBackgroundKeyDown(event: KeyboardEvent) {
            if (event.key !== 'Escape' || !this.editMode || !this.backgroundDrawMode) {
                return;
            }

            event.preventDefault();
            this.activeBackgroundPathId = null;
            this.backgroundHoverPoint = null;
            this.backgroundPointDragState = null;
            this.clearBackgroundPointDragListeners();
        },
        closeBackgroundPathAtPoint(pathId: number, pointIndex: number, replaceLastPoint: boolean = false) {
            const path = this.backgroundPaths.find((backgroundPath) => backgroundPath.id === pathId);
            if (
                path === undefined ||
                path.closed ||
                path.id !== this.activeBackgroundPathId ||
                path.points.length < 2 ||
                pointIndex === path.points.length - 1
            ) {
                return;
            }

            const closingPoint = path.points[pointIndex];
            if (closingPoint === undefined) {
                return;
            }

            this.backgroundPaths = this.backgroundPaths.map((backgroundPath) => {
                if (backgroundPath.id !== path.id) {
                    return backgroundPath;
                }

                return {
                    ...backgroundPath,
                    points: replaceLastPoint
                        ? [...backgroundPath.points.slice(0, -1), { ...closingPoint }]
                        : [...backgroundPath.points, { ...closingPoint }],
                    closed: true,
                };
            });
            this.activeBackgroundPathId = null;
            this.backgroundHoverPoint = null;
        },
        findBackgroundClosingPointIndex(pathId: number, movedPointIndex: number, point: DrawingPoint): number | null {
            const path = this.backgroundPaths.find((backgroundPath) => backgroundPath.id === pathId);
            if (
                path === undefined ||
                path.closed ||
                path.id !== this.activeBackgroundPathId ||
                movedPointIndex !== path.points.length - 1 ||
                path.points.length < 3
            ) {
                return null;
            }

            const closingPointIndex = path.points.findIndex((existingPoint, pointIndex) => {
                if (pointIndex === movedPointIndex) {
                    return false;
                }

                return Math.hypot(point.x - existingPoint.x, point.y - existingPoint.y) <= 8;
            });

            return closingPointIndex === -1 ? null : closingPointIndex;
        },
        insertBackgroundPointOnPath(pathId: number, point: DrawingPoint) {
            const path = this.backgroundPaths.find((backgroundPath) => backgroundPath.id === pathId);
            if (path === undefined || path.points.length < 2) {
                return;
            }

            const insertAfterPointIndex = this.findNearestBackgroundSegmentStartIndex(path, point);
            if (insertAfterPointIndex === null) {
                return;
            }

            this.backgroundPaths = this.backgroundPaths.map((backgroundPath) => {
                if (backgroundPath.id !== path.id) {
                    return backgroundPath;
                }

                return {
                    ...backgroundPath,
                    points: [
                        ...backgroundPath.points.slice(0, insertAfterPointIndex + 1),
                        point,
                        ...backgroundPath.points.slice(insertAfterPointIndex + 1),
                    ],
                };
            });
            this.activeBackgroundPathId = path.closed ? null : path.id;
        },
        findNearestBackgroundSegmentStartIndex(path: BackgroundPath, point: DrawingPoint): number | null {
            let nearestSegmentStartIndex: number | null = null;
            let nearestDistance = Number.POSITIVE_INFINITY;

            for (let pointIndex = 0; pointIndex < path.points.length - 1; pointIndex += 1) {
                const startPoint = path.points[pointIndex];
                const endPoint = path.points[pointIndex + 1];
                if (startPoint === undefined || endPoint === undefined) {
                    continue;
                }

                const distance = this.distanceToSegment(point, startPoint, endPoint);
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    nearestSegmentStartIndex = pointIndex;
                }
            }

            const hitTolerance = Math.max(8, path.width + 4);
            return nearestDistance <= hitTolerance ? nearestSegmentStartIndex : null;
        },
        distanceToSegment(point: DrawingPoint, startPoint: DrawingPoint, endPoint: DrawingPoint): number {
            const deltaX = endPoint.x - startPoint.x;
            const deltaY = endPoint.y - startPoint.y;
            const lengthSquared = deltaX * deltaX + deltaY * deltaY;
            if (lengthSquared === 0) {
                return Math.hypot(point.x - startPoint.x, point.y - startPoint.y);
            }

            const projection = Math.max(0, Math.min(1, ((point.x - startPoint.x) * deltaX + (point.y - startPoint.y) * deltaY) / lengthSquared));
            const projectedX = startPoint.x + projection * deltaX;
            const projectedY = startPoint.y + projection * deltaY;

            return Math.hypot(point.x - projectedX, point.y - projectedY);
        },
        deleteBackgroundPath(pathId: number) {
            this.backgroundPaths = this.backgroundPaths.filter((path) => path.id !== pathId);
            if (this.activeBackgroundPathId === pathId) {
                this.activeBackgroundPathId = null;
                this.backgroundHoverPoint = null;
            }
        },
        confirmDeleteBackgroundPath(pathId: number) {
            if (window.confirm(this.$t('moduleoverview.ConfirmDeleteShape'))) {
                this.deleteBackgroundPath(pathId);
            }
        },
        backgroundPointerPosition(event: PointerEvent): DrawingPoint {
            const position = this.canvasPointerPosition(event);

            return {
                x: Math.max(0, Math.round(position.x)),
                y: Math.max(0, Math.round(position.y)),
            };
        },
        canvasPointerPosition(event: PointerEvent): DrawingPoint {
            const canvas = this.$refs.canvas as HTMLElement;
            const rect = canvas.getBoundingClientRect();

            return {
                x: (event.clientX - rect.left + canvas.scrollLeft) / this.zoomFactor,
                y: (event.clientY - rect.top + canvas.scrollTop) / this.zoomFactor,
            };
        },
        pathData(path: BackgroundPath): string {
            const { points } = path;
            if (points.length === 0) {
                return '';
            }

            const firstPoint = points[0];
            if (firstPoint === undefined) {
                return '';
            }

            const commands = [`M ${firstPoint.x} ${firstPoint.y}`, ...points.slice(1).map((point) => `L ${point.x} ${point.y}`)];
            return path.closed && this.isSamePoint(firstPoint, points[points.length - 1]) ? `${commands.join(' ')} Z` : commands.join(' ');
        },
        isSamePoint(firstPoint: DrawingPoint, secondPoint?: DrawingPoint): boolean {
            return secondPoint !== undefined && firstPoint.x === secondPoint.x && firstPoint.y === secondPoint.y;
        },
        undoBackgroundPath() {
            const activePath = this.backgroundPaths.find((path) => path.id === this.activeBackgroundPathId);
            if (activePath === undefined) {
                this.backgroundPaths = this.backgroundPaths.slice(0, -1);
                this.activeBackgroundPathId = this.backgroundPaths[this.backgroundPaths.length - 1]?.id ?? null;
                return;
            }

            if (activePath.closed) {
                this.backgroundPaths = this.backgroundPaths.map((path) => {
                    if (path.id !== activePath.id) {
                        return path;
                    }

                    return {
                        ...path,
                        closed: false,
                    };
                });
                return;
            }

            if (activePath.points.length <= 1) {
                this.backgroundPaths = this.backgroundPaths.filter((path) => path.id !== activePath.id);
                this.activeBackgroundPathId = this.backgroundPaths[this.backgroundPaths.length - 1]?.id ?? null;
                return;
            }

            this.backgroundPaths = this.backgroundPaths.map((path) => {
                if (path.id !== activePath.id) {
                    return path;
                }

                return {
                    ...path,
                    points: path.points.slice(0, -1),
                };
            });
        },
        clearBackground() {
            this.backgroundPaths = [];
            this.backgroundPathSequence = 0;
            this.activeBackgroundPathId = null;
            this.backgroundHoverPoint = null;
            this.clearBackgroundPointDragListeners();
        },
        backgroundSvgMarkup(): string {
            const paths = this.backgroundPaths
                .map(
                    (path) =>
                        `<path d="${this.pathData(path)}" stroke="${path.color}" stroke-width="${path.width}" fill="none" stroke-linecap="round" stroke-linejoin="round"/>`
                )
                .join('');

            return `<svg xmlns="http://www.w3.org/2000/svg" width="${this.canvasWidth}" height="${this.canvasHeight}" viewBox="0 0 ${this.canvasWidth} ${this.canvasHeight}">${paths}</svg>`;
        },
        moduleStyle(key: string) {
            const position = this.positions[key] || { x: 0, y: 0 };
            return {
                left: `${position.x}px`,
                top: `${position.y}px`,
            };
        },
        heatmapValue(module: ModuleItem): number {
            if (this.heatmapMode === 'power' || this.heatmapMode === 'powerMax' || this.heatmapMode === 'powerDiff') {
                return module.Power?.v ?? 0;
            }

            if (this.heatmapMode === 'yieldDay' || this.heatmapMode === 'yieldDayDiff') {
                return module.YieldDay?.v ?? 0;
            }

            return 0;
        },
        isHeatmapDifferenceMode(): boolean {
            return this.heatmapMode === 'powerDiff' || this.heatmapMode === 'yieldDayDiff';
        },
        heatmapStyle(module: ModuleItem) {
            if (this.heatmapMode === 'none' || !module.pollEnabled) {
                return {};
            }

            const value = Math.max(0, this.heatmapValue(module));
            if (this.isHeatmapDifferenceMode()) {
                const range = this.heatmapDifferenceRange;
                if (range === null || range.maximum <= range.minimum) {
                    return {};
                }

                return this.heatmapColorStyle((value - range.minimum) / (range.maximum - range.minimum));
            }

            const maximum = this.heatmapMode === 'powerMax' ? module.powerMaximum : this.heatmapMaximum;
            if (maximum <= 0) {
                return {};
            }

            return this.heatmapColorStyle(value / maximum);
        },
        heatmapColorStyle(ratio: number) {
            const normalizedRatio = Math.min(1, Math.max(0, ratio));
            const hue = 210 - normalizedRatio * 150;
            const backgroundLightness = 96 - normalizedRatio * 24;
            const borderLightness = 58 - normalizedRatio * 18;

            return {
                backgroundColor: `hsl(${hue}, 85%, ${backgroundLightness}%)`,
                borderColor: `hsl(${hue}, 80%, ${borderLightness}%)`,
            };
        },
        statusClass(module: ModuleItem) {
            return {
                'module-card-disabled': !module.pollEnabled,
                'module-card-offline': module.pollEnabled && !module.reachable,
                'module-card-idle': module.pollEnabled && module.reachable && !module.producing,
                'module-card-producing': module.pollEnabled && module.reachable && module.producing,
            };
        },
        statusBadgeClass(module: ModuleItem) {
            return {
                'text-bg-secondary': !module.pollEnabled,
                'text-bg-danger': module.pollEnabled && !module.reachable,
                'text-bg-warning': module.pollEnabled && module.reachable && !module.producing,
                'text-bg-success': module.pollEnabled && module.reachable && module.producing,
            };
        },
        formatValue(value?: ValueObject): string {
            if (value === undefined) {
                return '-';
            }

            return `${this.$n(value.v, value.d === 0 ? 'decimalNoDigits' : 'decimal')} ${value.u}`;
        },
        moduleDebugTitle(module: ModuleItem): string {
            const position = this.positions[module.key] || { x: 0, y: 0 };

            return [
                `key: ${module.key}`,
                `serial: ${module.serial}`,
                `inverter: ${module.inverterName}`,
                `channel: ${module.channel}`,
                `x: ${Math.round(position.x)}`,
                `y: ${Math.round(position.y)}`,
                `poll_enabled: ${module.pollEnabled}`,
                `reachable: ${module.reachable}`,
                `producing: ${module.producing}`,
                `powerMaximum: ${module.powerMaximum}`,
                `Power: ${this.debugValue(module.Power)}`,
                `Voltage: ${this.debugValue(module.Voltage)}`,
                `Current: ${this.debugValue(module.Current)}`,
                `YieldDay: ${this.debugValue(module.YieldDay)}`,
            ].join('\n');
        },
        debugValue(value?: ValueObject): string {
            if (value === undefined) {
                return 'undefined';
            }

            return `${value.v} ${value.u} (d=${value.d}, max=${value.max ?? 'n/a'})`;
        },
    },
});
</script>

<style scoped>
.module-overview-status-row {
    flex: 1 1 48rem;
}

.module-overview-status-badges {
    display: flex;
    flex: 0 0 auto;
    align-items: stretch;
}

.module-overview-status-card {
    width: 20rem;
}

.module-overview-status-card .card-body {
    padding: 0.45rem 0.75rem;
}

.module-overview-status-values {
    display: grid;
    grid-template-columns: repeat(3, minmax(0, 1fr));
    gap: 0.5rem;
}

.module-overview-status-value span,
.module-overview-status-value strong {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.module-overview-status-value {
    display: flex;
    gap: 0.35rem;
    align-items: baseline;
    justify-content: center;
}

.module-overview-status-value span {
    color: var(--bs-secondary-color);
    font-size: 0.75rem;
}

.module-overview-status-value strong {
    font-size: 1.25rem;
    line-height: 1.2;
}

.module-overview-row-totals {
    flex: 1 1 42rem;
    max-width: 52rem;
    min-width: 32rem;
}

.module-overview-row-totals :deep(.row) {
    --bs-gutter-x: 0.5rem;
    --bs-gutter-y: 0.5rem;
}

.module-overview-row-totals :deep(.card-body) {
    padding: 0.45rem 0.75rem;
}

.module-overview-row-totals :deep(h2) {
    margin-bottom: 0;
    font-size: 1.25rem;
}

.module-overview-canvas {
    position: relative;
    overflow-x: auto;
    overflow-y: auto;
    border: 1px solid var(--bs-border-color);
    border-radius: var(--bs-border-radius);
    background-color: var(--bs-body-bg);
}

.module-overview-canvas-edit {
    cursor: crosshair;
}

.module-overview-zoom-spacer {
    position: relative;
}

.module-overview-workspace {
    position: absolute;
    top: 0;
    left: 0;
    transform-origin: top left;
}

.module-overview-select {
    width: auto;
}

.module-overview-draw-tools {
    min-width: 150px;
}

.module-overview-stroke-width {
    width: 90px;
}

.module-overview-grid {
    position: absolute;
    inset: 0;
    min-width: 100%;
    min-height: 100%;
    background-image:
        linear-gradient(var(--bs-border-color-translucent) 1px, transparent 1px),
        linear-gradient(90deg, var(--bs-border-color-translucent) 1px, transparent 1px);
    background-size: 32px 32px;
    opacity: 0.55;
}

.module-overview-background {
    position: absolute;
    top: 0;
    left: 0;
    pointer-events: none;
}

.module-overview-background-draw {
    pointer-events: auto;
    cursor: crosshair;
}

.module-overview-background-preview {
    opacity: 0.55;
    pointer-events: none;
}

.module-overview-background-point {
    fill: var(--bs-body-bg);
    stroke: var(--bs-primary);
    stroke-width: 2;
    cursor: move;
}

.module-overview-background-point-active {
    fill: var(--bs-primary);
    stroke: var(--bs-body-bg);
}

.module-card {
    position: absolute;
    z-index: 1;
    box-sizing: border-box;
    width: 150px;
    height: 250px;
    padding: 0.65rem;
    border: 2px solid var(--bs-border-color);
    border-radius: var(--bs-border-radius);
    background-color: var(--bs-body-bg);
    box-shadow: var(--bs-box-shadow-sm);
    touch-action: none;
    user-select: none;
}

.module-card-edit {
    cursor: grab;
}

.module-card-edit:active {
    cursor: grabbing;
}

.module-card-dragging {
    z-index: 2;
}

.module-card-producing {
    border-color: var(--bs-success);
}

.module-card-idle {
    border-color: var(--bs-warning);
}

.module-card-offline {
    border-color: var(--bs-danger);
}

.module-card-disabled {
    border-color: var(--bs-secondary);
    opacity: 0.75;
}

.module-card-header {
    display: grid;
    grid-template-columns: 1fr;
    gap: 0.5rem;
}

.module-title {
    min-width: 0;
    overflow: hidden;
    font-weight: 600;
    line-height: 1.1;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.module-power {
    margin-top: 0.75rem;
    padding: 0.2rem 0.35rem;
    border-radius: var(--bs-border-radius-sm);
    background-color: rgb(var(--bs-body-bg-rgb), 0.82);
    font-size: 1.35rem;
    font-weight: 700;
    line-height: 1.1;
}

.module-values {
    display: grid;
    grid-template-columns: 1fr;
    gap: 0.25rem;
    margin-top: 0.7rem;
    padding: 0.35rem;
    border-radius: var(--bs-border-radius-sm);
    background-color: rgb(var(--bs-body-bg-rgb), 0.82);
    color: var(--bs-body-color);
    font-size: 0.78rem;
}

.module-values span {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.module-key {
    position: absolute;
    right: 0.6rem;
    bottom: 0.55rem;
    left: 0.6rem;
    overflow: hidden;
    color: var(--bs-secondary-color);
    font-family: var(--bs-font-monospace);
    font-size: 0.68rem;
    text-overflow: ellipsis;
    white-space: nowrap;
}

@media (max-width: 767.98px) {
    .module-overview-status-row,
    .module-overview-row-totals {
        flex-basis: 100%;
        min-width: 100%;
        max-width: none;
    }

    .module-overview-status-badges {
        flex: 1 1 100%;
    }

    .module-overview-status-card {
        width: 100%;
    }
}
</style>
