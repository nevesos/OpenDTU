<template>
    <div
        class="module-card"
        :class="[statusClass, { 'module-card-edit': editMode, 'module-card-dragging': isDragging }]"
        :style="[moduleStyle, heatmapStyle]"
        :title="debugTitle"
        @pointerdown="$emit('pointerdown', $event)"
    >
        <div class="module-card-header">
            <span class="module-title">{{ module.inverterName }}</span>
            <span class="badge rounded-pill" :class="statusBadgeClass">
                {{ $t('moduleoverview.Channel', { channel: module.channel + 1 }) }}
            </span>
        </div>
        <div v-if="!module.hasLiveData" class="module-card-loading">
            <div class="spinner-border m-1" role="status">
                <span class="visually-hidden">{{ $t('home.LoadingInverter') }}</span>
            </div>
            <span>{{ $t('home.LoadingInverter') }}</span>
        </div>
        <template v-else>
            <div class="module-power">{{ formatValue(module.Power) }}</div>
            <div class="module-values">
                <span>{{ formatValue(module.Voltage) }}</span>
                <span>{{ formatValue(module.Current) }}</span>
                <span>{{ formatValue(module.YieldDay) }}</span>
            </div>
        </template>
        <div class="module-key">{{ module.key }}</div>
    </div>
</template>

<script lang="ts">
import type { ModuleItem, ModulePosition } from '@/types/ModuleOverview';
import type { ValueObject } from '@/types/LiveDataStatus';
import type { CSSProperties, PropType } from 'vue';
import { defineComponent } from 'vue';

export default defineComponent({
    props: {
        module: {
            type: Object as PropType<ModuleItem>,
            required: true,
        },
        position: {
            type: Object as PropType<ModulePosition>,
            default: () => ({ x: 0, y: 0 }),
        },
        editMode: {
            type: Boolean,
            required: true,
        },
        isDragging: {
            type: Boolean,
            required: true,
        },
        heatmapStyle: {
            type: Object as PropType<CSSProperties>,
            default: () => ({}),
        },
    },
    emits: ['pointerdown'],
    computed: {
        moduleStyle(): CSSProperties {
            return {
                left: `${this.position.x}px`,
                top: `${this.position.y}px`,
            };
        },
        statusClass() {
            return {
                'module-card-disabled': !this.module.pollEnabled,
                'module-card-offline': this.module.pollEnabled && !this.module.reachable,
                'module-card-idle': this.module.pollEnabled && this.module.reachable && !this.module.producing,
                'module-card-producing': this.module.pollEnabled && this.module.reachable && this.module.producing,
            };
        },
        statusBadgeClass() {
            return {
                'text-bg-secondary': !this.module.pollEnabled,
                'text-bg-danger': this.module.pollEnabled && !this.module.reachable,
                'text-bg-warning': this.module.pollEnabled && this.module.reachable && !this.module.producing,
                'text-bg-success': this.module.pollEnabled && this.module.reachable && this.module.producing,
            };
        },
        debugTitle(): string {
            return [
                `key: ${this.module.key}`,
                `serial: ${this.module.serial}`,
                `inverter: ${this.module.inverterName}`,
                `channel: ${this.module.channel}`,
                `x: ${Math.round(this.position.x)}`,
                `y: ${Math.round(this.position.y)}`,
                `poll_enabled: ${this.module.pollEnabled}`,
                `reachable: ${this.module.reachable}`,
                `producing: ${this.module.producing}`,
                `hasLiveData: ${this.module.hasLiveData}`,
                `powerMaximum: ${this.module.powerMaximum}`,
                `Power: ${this.debugValue(this.module.Power)}`,
                `Voltage: ${this.debugValue(this.module.Voltage)}`,
                `Current: ${this.debugValue(this.module.Current)}`,
                `YieldDay: ${this.debugValue(this.module.YieldDay)}`,
            ].join('\n');
        },
    },
    methods: {
        formatValue(value?: ValueObject): string {
            if (value === undefined) {
                return '-';
            }

            return `${this.$n(value.v, value.d === 0 ? 'decimalNoDigits' : 'decimal')} ${value.u}`;
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

.module-card-loading {
    display: flex;
    min-height: 120px;
    align-items: center;
    justify-content: center;
    flex-direction: column;
    gap: 0.35rem;
    padding: 0.75rem 0;
    color: var(--bs-secondary-color);
    font-size: 0.8rem;
    line-height: 1.2;
    text-align: center;
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
