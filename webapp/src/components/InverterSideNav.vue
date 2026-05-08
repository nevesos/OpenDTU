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
