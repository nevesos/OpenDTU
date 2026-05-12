<template>
    <div class="nav nav-pills row-cols-sm-1 gap-3 w-100 inverter-side-nav" role="listbox" aria-multiselectable="true" aria-orientation="vertical">
        <div v-for="inverter in inverters" :key="inverter.serial" class="d-flex align-items-stretch gap-1 w-100">
            <button
                class="nav-link border border-primary flex-grow-1 inverter-side-nav-select"
                :class="{ active: isSelected(inverter.serial) }"
                type="button"
                role="option"
                :aria-selected="isSelected(inverter.serial)"
                @click="selectInverter(inverter.serial)"
            >
                <span
                    :key="`${inverter.serial}:${updateIndicators[inverter.serial]}`"
                    class="inverter-update-marker"
                    :class="{ 'inverter-update-marker-ping': updateIndicators[inverter.serial] !== undefined }"
                ></span>
                <div class="d-flex align-items-center inverter-side-nav-content">
                    <div class="me-2 flex-shrink-0">
                        <span
                            v-if="inverter.AC"
                            class="badge inverter-side-nav-power"
                            :class="{
                                'text-bg-secondary': !inverter.poll_enabled,
                                'text-bg-danger': inverter.poll_enabled && !inverter.reachable,
                                'text-bg-warning': inverter.poll_enabled && inverter.reachable && !inverter.producing,
                                'text-bg-success': inverter.poll_enabled && inverter.reachable && inverter.producing,
                            }"
                        >
                            {{ $n(inverter.AC[0]?.Power?.v || 0, 'decimalNoDigits') }}
                            {{ inverter.AC[0]?.Power?.u }}
                        </span>
                        <span v-else class="badge text-bg-light">-</span>
                    </div>
                    <div class="inverter-side-nav-text">
                        <div class="inverter-side-nav-name">{{ inverter.name }}</div>
                        <div v-if="inverterInfoLine(inverter) !== ''" class="inverter-side-nav-values">
                            {{ inverterInfoLine(inverter) }}
                        </div>
                    </div>
                </div>
            </button>
            <InverterDevInfoAction
                :serial="inverter.serial"
                buttonClass="btn btn-sm btn-info inverter-side-nav-dev-info"
                iconSize="20px"
            />
        </div>
    </div>
</template>

<script lang="ts">
import InverterDevInfoAction from '@/components/InverterDevInfoAction.vue';
import type { Inverter, InverterStatistics, ValueObject } from '@/types/LiveDataStatus';
import type { PropType } from 'vue';
import { defineComponent } from 'vue';

export default defineComponent({
    components: {
        InverterDevInfoAction,
    },
    props: {
        inverters: {
            type: Array as PropType<Inverter[]>,
            required: true,
        },
        modelValue: {
            type: Array as PropType<string[]>,
            default: () => [],
        },
        updateIndicators: {
            type: Object as PropType<Record<string, number>>,
            default: () => ({}),
        },
    },
    emits: ['update:modelValue', 'select'],
    methods: {
        isSelected(serial: string): boolean {
            return this.modelValue.includes(serial);
        },
        selectInverter(serial: string) {
            const selected = this.isSelected(serial);
            const nextSerials = selected ? this.modelValue.filter((selectedSerial) => selectedSerial !== serial) : [...this.modelValue, serial];
            this.$emit('update:modelValue', nextSerials);
            this.$emit('select', selected ? null : serial);
        },
        inverterInfoLine(inverter: Inverter): string {
            const inverterChannelData = Object.values(inverter.INV || {})[0] as InverterStatistics | undefined;
            return [this.formatValue(inverterChannelData?.YieldDay), this.formatValue(inverterChannelData?.Temperature)]
                .filter((value) => value !== '-')
                .join(' / ');
        },
        formatValue(value?: ValueObject): string {
            if (value === undefined) {
                return '-';
            }

            return `${this.$n(value.v, value.d === 0 ? 'decimalNoDigits' : 'decimal')} ${value.u}`;
        },
    },
});
</script>

<style scoped>
.nav-link {
    position: relative;
}

.inverter-side-nav {
    display: flex;
    flex-direction: column;
}

.inverter-side-nav-select {
    flex: 1 1 0;
    min-width: 0;
    text-align: left;
}

.inverter-side-nav-content {
    min-width: 0;
    width: 100%;
}

.inverter-side-nav-text {
    min-width: 0;
    flex: 1 1 auto;
    overflow: hidden;
}

.inverter-side-nav-name,
.inverter-side-nav-values {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.inverter-side-nav-values {
    color: var(--bs-secondary-color);
    font-size: 0.8rem;
    line-height: 1.2;
}

.inverter-side-nav-power {
    font-size: 0.95rem;
    line-height: 1.2;
}

:deep(.inverter-side-nav-dev-info) {
    display: flex;
    flex: 0 0 auto;
    align-items: center;
    justify-content: center;
    width: 1.55rem;
    padding-right: 0.15rem;
    padding-left: 0.15rem;
    border-color: transparent;
    background-color: transparent;
    color: var(--bs-secondary-color);
}

:deep(.inverter-side-nav-dev-info:hover),
:deep(.inverter-side-nav-dev-info:focus) {
    border-color: var(--bs-border-color);
    background-color: transparent;
    color: var(--bs-body-color);
}

.nav-link.active .inverter-side-nav-values {
    color: rgb(var(--bs-light-rgb), 0.85);
}

@media (min-width: 768px) and (max-width: 1199.98px) {
    .inverter-side-nav-values {
        font-size: 0.72rem;
    }

    .inverter-side-nav-power {
        font-size: 0.85rem;
    }
}

@media (min-width: 768px) and (max-width: 899.98px) {
    .inverter-side-nav-values {
        display: none;
    }
}

.inverter-update-marker {
    position: absolute;
    top: 50%;
    right: 0.3rem;
    display: block;
    width: 8px;
    height: 8px;
    transform: translateY(-50%);
}

.inverter-update-marker:before {
    content: '';
    position: absolute;
    width: 8px;
    height: 8px;
    background: #00bb00;
    border-color: #00bb00;
    border-radius: 50%;
}

.inverter-update-marker-ping:after {
    content: '';
    position: absolute;
    width: 32px;
    height: 32px;
    margin: -12px 0 0 -12px;
    border: 1px solid #00bb00;
    border-radius: 50%;
    box-shadow:
        0 0 4px #00bb00,
        inset 0 0 4px rgb(56, 111, 169);
    transform: scale(0);
    animation: online 2.5s ease-in-out;
}

@keyframes online {
    0% {
        transform: scale(0.1);
        opacity: 1;
    }

    70% {
        transform: scale(2.5);
        opacity: 0;
    }

    100% {
        opacity: 0;
    }
}
</style>
