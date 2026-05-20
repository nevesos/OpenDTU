<template>
    <CardElement :text="$t('energyhistory.DataManagement')" textVariant="text-bg-primary" add-space table>
        <div class="px-3 pt-3">
            <BootstrapAlert
                v-model="alert.show"
                dismissible
                :variant="alert.type"
                :auto-dismiss="alert.type === 'success' ? 5000 : 0"
                :jump-to-top="false"
            >
                {{ alert.message }}
            </BootstrapAlert>
        </div>

        <div class="row g-3 px-3 pb-3 align-items-end">
            <div class="col-12 col-lg-5">
                <label class="form-label" for="energy-history-import-file">{{ $t('energyhistory.ImportFile') }}</label>
                <input
                    id="energy-history-import-file"
                    ref="uploadInput"
                    class="form-control"
                    type="file"
                    @change="onUploadFileSelected"
                />
            </div>
            <div class="col-12 col-lg-5">
                <label class="form-label" for="energy-history-import-path">{{ $t('energyhistory.ImportTargetPath') }}</label>
                <input
                    id="energy-history-import-path"
                    v-model="uploadPath"
                    class="form-control"
                    type="text"
                    placeholder="/energy/5m/total_2026_05.eh5"
                />
            </div>
            <div class="col-12 col-lg-2 d-grid">
                <button type="button" class="btn btn-primary" :disabled="uploading || !uploadFile || !uploadPath" @click="uploadSelectedFile">
                    <BIconUpload class="me-1" />
                    {{ uploading ? $t('energyhistory.Uploading') : $t('energyhistory.UploadOverwrite') }}
                </button>
            </div>
        </div>

        <div class="table-responsive">
            <table class="table table-hover table-condensed align-middle mb-0">
                <thead>
                    <tr>
                        <th>{{ $t('energyhistory.FilePath') }}</th>
                        <th class="text-end">{{ $t('energyhistory.FileSize') }}</th>
                        <th class="text-end">
                            <button type="button" class="btn btn-outline-secondary btn-sm" :disabled="loading" @click="loadFiles">
                                <BIconArrowClockwise />
                            </button>
                        </th>
                    </tr>
                </thead>
                <tbody>
                    <tr
                        v-for="file in files"
                        :key="file.path"
                        class="energy-history-file-row"
                        :class="{ 'table-active': selectedFilePath === file.path }"
                        @click="selectFile(file)"
                    >
                        <td class="text-break">{{ file.path }}</td>
                        <td class="text-end text-nowrap">{{ formatBytes(file.size) }}</td>
                        <td class="text-end">
                            <button
                                type="button"
                                class="btn btn-outline-primary btn-sm"
                                :title="$t('energyhistory.Download')"
                                @click.stop="downloadFile(file)"
                            >
                                <BIconDownload />
                            </button>
                        </td>
                    </tr>
                    <tr v-if="!loading && files.length === 0">
                        <td colspan="3" class="text-center text-muted">{{ $t('energyhistory.NoFiles') }}</td>
                    </tr>
                    <tr v-if="loading">
                        <td colspan="3" class="text-center text-muted">{{ $t('base.Loading') }}</td>
                    </tr>
                </tbody>
            </table>
        </div>
    </CardElement>
</template>

<script lang="ts">
import BootstrapAlert from '@/components/BootstrapAlert.vue';
import CardElement from '@/components/CardElement.vue';
import { authHeader, handleResponse } from '@/utils/authentication';
import { BIconArrowClockwise, BIconDownload, BIconUpload } from 'bootstrap-icons-vue';
import { defineComponent } from 'vue';

interface EnergyHistoryFile {
    path: string;
    size: number;
}

interface EnergyHistoryFileListResponse {
    files?: EnergyHistoryFile[];
}

interface AlertState {
    show: boolean;
    type: string;
    message: string;
}

export default defineComponent({
    name: 'EnergyHistoryImportExport',
    components: {
        BootstrapAlert,
        CardElement,
        BIconArrowClockwise,
        BIconDownload,
        BIconUpload,
    },
    emits: ['changed', 'selected'],
    data() {
        return {
            loading: false,
            uploading: false,
            files: [] as EnergyHistoryFile[],
            selectedFilePath: '',
            uploadFile: null as File | null,
            uploadPath: '',
            alert: {
                show: false,
                type: 'info',
                message: '',
            } as AlertState,
        };
    },
    created() {
        this.loadFiles();
    },
    methods: {
        loadFiles(): Promise<void> {
            this.loading = true;
            return fetch('/api/energy/history/file/list', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data: EnergyHistoryFileListResponse) => {
                    this.files = (data.files || []).sort((a, b) => a.path.localeCompare(b.path));
                    if (this.selectedFilePath && !this.files.some((file) => file.path === this.selectedFilePath)) {
                        this.selectedFilePath = '';
                        this.$emit('selected', null);
                    }
                })
                .catch(() => {
                    this.files = [];
                })
                .finally(() => {
                    this.loading = false;
                });
        },
        selectFile(file: EnergyHistoryFile) {
            this.selectedFilePath = file.path;
            this.$emit('selected', file);
        },
        downloadFile(file: EnergyHistoryFile) {
            const params = new URLSearchParams();
            params.set('file', file.path);

            fetch('/api/energy/history/file/download?' + params.toString(), { headers: authHeader() })
                .then((response) => {
                    if (!response.ok) {
                        throw new Error(response.statusText);
                    }
                    return response.blob();
                })
                .then((blob: Blob) => {
                    const url = URL.createObjectURL(blob);
                    const link = document.createElement('a');
                    link.href = url;
                    link.download = file.path.substring(file.path.lastIndexOf('/') + 1);
                    link.click();
                    URL.revokeObjectURL(url);
                })
                .catch(() => {
                    this.alert = {
                        show: true,
                        type: 'danger',
                        message: String(this.$t('energyhistory.DownloadFailed')),
                    };
                });
        },
        onUploadFileSelected(event: Event) {
            const input = event.target as HTMLInputElement;
            this.uploadFile = input.files?.[0] || null;
            if (this.uploadFile && !this.uploadPath) {
                this.uploadPath = `/energy/5m/${this.uploadFile.name}`;
            }
        },
        uploadSelectedFile() {
            if (!this.uploadFile || !this.uploadPath) {
                return;
            }

            this.uploading = true;
            const params = new URLSearchParams();
            params.set('file', this.uploadPath);

            const formData = new FormData();
            formData.append('file', this.uploadFile, this.uploadFile.name);

            fetch('/api/energy/history/file/upload?' + params.toString(), {
                method: 'POST',
                headers: authHeader(),
                body: formData,
            })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data) => {
                    this.alert = {
                        show: true,
                        type: data.type || 'success',
                        message: data.message || String(this.$t('energyhistory.UploadSuccess')),
                    };
                    this.uploadFile = null;
                    const input = this.$refs.uploadInput as HTMLInputElement | undefined;
                    if (input) {
                        input.value = '';
                    }
                    this.$emit('changed');
                    return this.loadFiles();
                })
                .catch(() => {
                    this.alert = {
                        show: true,
                        type: 'danger',
                        message: String(this.$t('energyhistory.UploadFailed')),
                    };
                })
                .finally(() => {
                    this.uploading = false;
                });
        },
        formatBytes(value: number): string {
            if (value < 1024) {
                return `${value} B`;
            }
            if (value < 1024 * 1024) {
                return `${(value / 1024).toFixed(1)} KiB`;
            }
            return `${(value / 1024 / 1024).toFixed(1)} MiB`;
        },
    },
});
</script>

<style scoped>
.energy-history-file-row {
    cursor: pointer;
}
</style>
