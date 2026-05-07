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
        <div class="d-flex flex-wrap gap-2 align-items-center justify-content-between mb-3">
            <div>
                <span class="badge text-bg-secondary me-2">{{ $t('moduleoverview.Modules') }}: {{ visibleModules.length }}</span>
                <span class="badge text-bg-success me-2">{{ $t('moduleoverview.Producing') }}: {{ producingCount }}</span>
                <span class="badge text-bg-danger">{{ $t('moduleoverview.Offline') }}: {{ offlineCount }}</span>
            </div>
            <div class="d-flex flex-wrap gap-2 align-items-center">
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
                <select v-model="heatmapMode" class="form-select form-select-sm module-overview-select">
                    <option value="none">{{ $t('moduleoverview.HeatmapNone') }}</option>
                    <option value="power">{{ $t('moduleoverview.HeatmapPower') }}</option>
                    <option value="yieldDay">{{ $t('moduleoverview.HeatmapYieldDay') }}</option>
                </select>
                <div class="btn-group" role="group">
                    <button type="button" class="btn btn-outline-primary" :class="{ active: editMode }" @click="toggleEditMode">
                        <BIconPencilSquare />&nbsp;{{ $t('moduleoverview.EditMode') }}
                    </button>
                    <button type="button" class="btn btn-outline-secondary" :disabled="!editMode" @click="arrangeModules">
                        <BIconGrid3x3Gap />&nbsp;{{ $t('moduleoverview.Arrange') }}
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
        >
            <div class="module-overview-grid"></div>
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
    </BasePage>
</template>

<script lang="ts">
import BasePage from '@/components/BasePage.vue';
import BootstrapAlert from '@/components/BootstrapAlert.vue';
import type { Inverter, InverterStatistics, LiveData, ValueObject } from '@/types/LiveDataStatus';
import { authHeader, authUrl, handleResponse } from '@/utils/authentication';
import WebSocketService from '@/utils/websocketService';
import { BIconGrid3x3Gap, BIconPencilSquare } from 'bootstrap-icons-vue';
import { defineComponent } from 'vue';

interface ModulePosition {
    x: number;
    y: number;
}

const MODULE_WIDTH = 150;
const MODULE_HEIGHT = 250;
const MODULE_GAP = 18;
const PLACEMENT_GRID_SIZE = 16;

interface ModuleItem {
    key: string;
    inverterName: string;
    serial: string;
    channel: number;
    pollEnabled: boolean;
    reachable: boolean;
    producing: boolean;
    Power?: ValueObject;
    Voltage?: ValueObject;
    Current?: ValueObject;
    YieldDay?: ValueObject;
}

export default defineComponent({
    components: {
        BasePage,
        BootstrapAlert,
        BIconGrid3x3Gap,
        BIconPencilSquare,
    },
    data() {
        return {
            socket: {} as WebSocketService,
            dataLoading: true,
            liveData: { inverters: [] } as unknown as LiveData,
            isWebsocketConnected: false,
            editMode: false,
            showDisabledModules: false,
            heatmapMode: 'none' as 'none' | 'power' | 'yieldDay',
            positions: {} as Record<string, ModulePosition>,
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
        this.getInitialData();
        this.initSocket();
    },
    unmounted() {
        this.clearDragListeners();
        this.socket?.close();
    },
    computed: {
        modules(): ModuleItem[] {
            const modulesByKey = new Map<string, ModuleItem>();

            (this.liveData.inverters || [])
                .slice()
                .sort((a: Inverter, b: Inverter) => a.order - b.order)
                .forEach((inverter: Inverter) => {
                    const dcChannels = Object.entries(inverter.DC || {}) as [string, InverterStatistics][];

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
            if (this.heatmapMode === 'none') {
                return 0;
            }

            return Math.max(...this.visibleModules.map((module) => this.heatmapValue(module)), 0);
        },
    },
    methods: {
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
                    }
                });
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
        },
        ensureModulePositions() {
            const nextPositions = {} as Record<string, ModulePosition>;
            const occupiedPositions = new Set<string>();

            this.visibleModules.forEach((module, index) => {
                const existingPosition = this.positions[module.key];
                const existingPositionKey = existingPosition !== undefined ? this.positionKey(existingPosition) : '';

                if (existingPosition !== undefined && !occupiedPositions.has(existingPositionKey)) {
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
            const columns = Math.max(1, Math.floor(((this.$refs.canvas as HTMLElement | undefined)?.clientWidth || 960) / stepX));

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
        },
        onPointerDown(event: PointerEvent, key: string) {
            if (!this.editMode) {
                return;
            }

            event.preventDefault();
            this.clearDragListeners();

            const canvas = this.$refs.canvas as HTMLElement;
            const position = this.positions[key] || { x: 0, y: 0 };
            const rect = canvas.getBoundingClientRect();

            this.dragState = {
                key,
                pointerId: event.pointerId,
                offsetX: event.clientX - rect.left - position.x,
                offsetY: event.clientY - rect.top - position.y,
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

            const canvas = this.$refs.canvas as HTMLElement;
            const rect = canvas.getBoundingClientRect();
            const x = event.clientX - rect.left - this.dragState.offsetX;
            const y = event.clientY - rect.top - this.dragState.offsetY;
            const snappedX = this.snapToGrid(x);
            const snappedY = this.snapToGrid(y);

            this.positions = {
                ...this.positions,
                [this.dragState.key]: {
                    x: Math.max(0, Math.min(snappedX, canvas.clientWidth - MODULE_WIDTH)),
                    y: Math.max(0, Math.min(snappedY, canvas.clientHeight - MODULE_HEIGHT)),
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
        moduleStyle(key: string) {
            const position = this.positions[key] || { x: 0, y: 0 };
            return {
                left: `${position.x}px`,
                top: `${position.y}px`,
            };
        },
        heatmapValue(module: ModuleItem): number {
            if (this.heatmapMode === 'power') {
                return module.Power?.v ?? 0;
            }

            if (this.heatmapMode === 'yieldDay') {
                return module.YieldDay?.v ?? 0;
            }

            return 0;
        },
        heatmapStyle(module: ModuleItem) {
            if (this.heatmapMode === 'none' || this.heatmapMaximum <= 0 || !module.pollEnabled) {
                return {};
            }

            const value = Math.max(0, this.heatmapValue(module));
            const ratio = Math.min(1, value / this.heatmapMaximum);
            const hue = 210 - ratio * 150;
            const backgroundLightness = 96 - ratio * 24;
            const borderLightness = 58 - ratio * 18;

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
.module-overview-canvas {
    position: relative;
    min-height: 640px;
    overflow: auto;
    border: 1px solid var(--bs-border-color);
    border-radius: var(--bs-border-radius);
    background-color: var(--bs-body-bg);
}

.module-overview-canvas-edit {
    cursor: crosshair;
}

.module-overview-select {
    width: auto;
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

.module-card {
    position: absolute;
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
</style>
