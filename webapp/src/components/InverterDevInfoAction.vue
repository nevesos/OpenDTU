<template>
    <button
        :disabled="disabled"
        type="button"
        :class="buttonClass"
        @click="showDevInfo"
        v-tooltip
        :title="$t('home.ShowInverterInfo')"
    >
        <BIconCpu :style="{ fontSize: iconSize }" />
    </button>

    <ModalDialog :modalId="modalId" :title="$t('home.InverterInfo')" :loading="devInfoLoading">
        <DevInfo :devInfoList="devInfoList" />
    </ModalDialog>
</template>

<script lang="ts">
import DevInfo from '@/components/DevInfo.vue';
import ModalDialog from '@/components/ModalDialog.vue';
import type { DevInfoStatus } from '@/types/DevInfoStatus';
import { authHeader, handleResponse } from '@/utils/authentication';
import * as bootstrap from 'bootstrap';
import { BIconCpu } from 'bootstrap-icons-vue';
import { defineComponent } from 'vue';

let devInfoModalSequence = 0;

export default defineComponent({
    components: {
        BIconCpu,
        DevInfo,
        ModalDialog,
    },
    props: {
        serial: { type: String, required: true },
        disabled: { type: Boolean, default: false },
        buttonClass: { type: String, default: 'btn btn-sm btn-info' },
        iconSize: { type: String, default: '24px' },
    },
    data() {
        devInfoModalSequence += 1;

        return {
            modalId: `inverterDevInfoAction${devInfoModalSequence}`,
            devInfoView: null as bootstrap.Modal | null,
            devInfoList: {} as DevInfoStatus,
            devInfoLoading: false,
        };
    },
    mounted() {
        this.devInfoView = new bootstrap.Modal(`#${this.modalId}`);
    },
    unmounted() {
        this.devInfoView?.hide();
        this.devInfoView?.dispose();
    },
    methods: {
        showDevInfo() {
            this.devInfoLoading = true;
            this.devInfoView?.show();

            fetch(`/api/devinfo/status?inv=${this.serial}`, { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data: DevInfoStatus) => {
                    this.devInfoList = {
                        ...data,
                        serial: this.serial,
                    };
                })
                .finally(() => {
                    this.devInfoLoading = false;
                });
        },
    },
});
</script>
