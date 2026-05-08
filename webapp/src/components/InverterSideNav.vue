<template>
    <div class="nav nav-pills row-cols-sm-1 gap-3" role="tablist" aria-orientation="vertical">
        <button
            v-for="inverter in inverters"
            :key="inverter.serial"
            class="nav-link border border-primary text-break"
            :class="{ active: inverter.serial === modelValue }"
            type="button"
            role="tab"
            :aria-selected="inverter.serial === modelValue"
            @click="selectInverter(inverter.serial)"
        >
            <span
                :key="`${inverter.serial}:${updateIndicators[inverter.serial]}`"
                class="inverter-update-marker"
                :class="{ 'inverter-update-marker-ping': updateIndicators[inverter.serial] !== undefined }"
            ></span>
            <div class="d-flex align-items-center">
                <div class="me-2">
                    <span
                        v-if="inverter.AC"
                        class="badge"
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
                <div class="ms-auto me-auto">
                    {{ inverter.name }}
                </div>
            </div>
        </button>
    </div>
</template>

<script lang="ts">
import type { Inverter } from '@/types/LiveDataStatus';
import type { PropType } from 'vue';
import { defineComponent } from 'vue';

export default defineComponent({
    props: {
        inverters: {
            type: Array as PropType<Inverter[]>,
            required: true,
        },
        modelValue: {
            type: String as PropType<string | null>,
            default: null,
        },
        updateIndicators: {
            type: Object as PropType<Record<string, number>>,
            default: () => ({}),
        },
    },
    emits: ['update:modelValue', 'select'],
    methods: {
        selectInverter(serial: string) {
            const nextSerial = this.modelValue === serial ? null : serial;
            this.$emit('update:modelValue', nextSerial);
            this.$emit('select', nextSerial);
        },
    },
});
</script>

<style scoped>
.nav-link {
    position: relative;
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
