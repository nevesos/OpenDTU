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
                            <button
                                type="button"
                                class="btn btn-outline-secondary btn-sm ms-1"
                                :disabled="deepScanLoadingPath === file.path"
                                :title="$t('energyhistory.DeepScan')"
                                @click.stop="deepScanFile(file)"
                            >
                                <BIconSearch />
                            </button>
                            <button
                                type="button"
                                class="btn btn-outline-danger btn-sm ms-1"
                                :title="$t('energyhistory.Delete')"
                                @click.stop="deleteFile(file)"
                            >
                                <BIconTrash />
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

        <div class="px-3 py-3" v-if="selectedFile">
            <div class="energy-history-file-details">
                <div class="small text-muted">{{ $t('energyhistory.SelectedFile') }}</div>
                <div class="fw-semibold text-break">{{ selectedFile.path }}</div>
                <div class="row g-2 small mt-2">
                    <div class="col-auto">
                        <strong>{{ $t('energyhistory.FileType') }}:</strong>
                        <span class="ms-2">{{ selectedFileDetails.typeLabel }}</span>
                    </div>
                    <div class="col-auto">
                        <strong>{{ $t('energyhistory.Target') }}:</strong>
                        <span class="ms-2">{{ selectedFileDetails.target }}</span>
                    </div>
                    <div class="col-auto">
                        <strong>{{ $t('energyhistory.Year') }}:</strong>
                        <span class="ms-2">{{ selectedFileDetails.year }}</span>
                    </div>
                    <div class="col-auto" v-if="selectedFileDetails.month">
                        <strong>{{ $t('energyhistory.Month') }}:</strong>
                        <span class="ms-2">{{ selectedFileDetails.month }}</span>
                    </div>
                    <div class="col-auto">
                        <strong>{{ $t('energyhistory.FileSize') }}:</strong>
                        <span class="ms-2">{{ formatBytes(selectedFile.size) }}</span>
                    </div>
                </div>

                <div class="energy-history-file-scan mt-3" v-if="selectedFileDeepScan && selectedFileDeepScanPath === selectedFile.path">
                    <div class="d-flex flex-wrap align-items-center gap-2 mb-2">
                        <div class="fw-semibold">{{ $t('energyhistory.DeepScanResult') }}</div>
                        <button
                            v-if="selectedFileDeepScan.scan.can_truncate_final_block"
                            type="button"
                            class="btn btn-warning btn-sm"
                            :disabled="recoveringFilePath === selectedFile.path"
                            @click="recoverFile(selectedFile)"
                        >
                            {{ recoveringFilePath === selectedFile.path ? $t('energyhistory.Recovering') : $t('energyhistory.RecoverFile') }}
                        </button>
                    </div>
                    <div class="row g-2 small">
                        <div class="col-auto">
                            <strong>{{ $t('energyhistory.ValidBlocks') }}:</strong>
                            <span class="ms-2">{{ $n(selectedFileDeepScan.scan.valid_blocks || 0) }}</span>
                        </div>
                        <div class="col-auto">
                            <strong>{{ $t('energyhistory.SkippedBlocks') }}:</strong>
                            <span class="ms-2">{{ $n(selectedFileDeepScan.scan.skipped_blocks || 0) }}</span>
                        </div>
                        <div class="col-auto">
                            <strong>{{ $t('energyhistory.ValidRecords') }}:</strong>
                            <span class="ms-2">{{ $n(selectedFileDeepScan.scan.valid_records || 0) }}</span>
                        </div>
                        <div class="col-auto">
                            <strong>{{ $t('energyhistory.FileSize') }}:</strong>
                            <span class="ms-2">{{ formatBytes(selectedFileDeepScan.scan.file_size || 0) }}</span>
                        </div>
                        <div class="col-auto">
                            <strong>{{ $t('energyhistory.InvalidFinalBlock') }}:</strong>
                            <span class="ms-2">{{ selectedFileDeepScan.scan.invalid_final_block ? $t('base.Yes') : $t('base.No') }}</span>
                        </div>
                        <div class="col-auto">
                            <strong>{{ $t('energyhistory.CanTruncateFinalBlock') }}:</strong>
                            <span class="ms-2">{{ selectedFileDeepScan.scan.can_truncate_final_block ? $t('base.Yes') : $t('base.No') }}</span>
                        </div>
                    </div>
                    <details class="mt-2">
                        <summary>{{ $t('energyhistory.RawData') }}</summary>
                        <pre class="energy-history-raw-data mt-2 mb-0">{{ formatDeepScan(selectedFileDeepScan) }}</pre>
                    </details>
                </div>
                <div class="row g-2 mt-3" v-if="selectedFilePreviewResolution === '5m'">
                    <div class="col-12 col-sm-4 col-lg-3">
                        <label class="form-label" for="energy-history-file-preview-date">{{ $t('energyhistory.Date') }}</label>
                        <input
                            id="energy-history-file-preview-date"
                            v-model="selectedFilePreviewDate"
                            type="date"
                            class="form-control"
                            :min="selectedFilePreviewMinDate"
                            :max="selectedFilePreviewMaxDate"
                            @change="loadSelectedFilePreview"
                        />
                    </div>
                </div>

                <div class="table-responsive mt-3">
                    <div class="fw-semibold mb-2">{{ $t('energyhistory.Results') }}</div>
                    <table class="table table-hover table-condensed align-middle mb-0">
                        <thead>
                            <tr>
                                <th>{{ selectedFileFirstColumnLabel }}</th>
                                <th>{{ $t('energyhistory.YieldWh') }}</th>
                                <th v-if="selectedFilePreviewResolution === '5m'">{{ $t('energyhistory.AvgPowerW') }}</th>
                                <th v-if="selectedFilePreviewResolution !== '5m'">{{ $t('energyhistory.MaxPowerW') }}</th>
                                <th v-if="selectedFilePreviewResolution !== '5m'">{{ $t('energyhistory.AvgPowerW') }}</th>
                                <th v-if="selectedFilePreviewResolution !== '5m'">{{ $t('energyhistory.RuntimeMin') }}</th>
                                <th v-if="selectedFilePreviewResolution === 'day'">{{ $t('energyhistory.SampleCount') }}</th>
                                <th v-if="selectedFilePreviewResolution === 'month'">{{ $t('energyhistory.DayCount') }}</th>
                                <th v-if="selectedFilePreviewResolution !== '5m'">{{ $t('energyhistory.Flags') }}</th>
                            </tr>
                        </thead>
                        <tbody>
                            <tr v-for="(row, index) in selectedFileRows" :key="index" class="energy-history-preview-row">
                                <td>
                                    {{ selectedFileFirstColumnValue(row) }}
                                    <pre class="energy-history-row-raw">{{ formatRawRow(row) }}</pre>
                                </td>
                                <td>{{ formatKwh(row.yield_wh || 0) }}</td>
                                <td v-if="selectedFilePreviewResolution === '5m'">{{ $n(calculateAveragePowerW(row, selectedFileRows, index)) }}</td>
                                <td v-if="selectedFilePreviewResolution !== '5m'">{{ $n(row.max_power_w || 0) }}</td>
                                <td v-if="selectedFilePreviewResolution !== '5m'">{{ $n(calculateAveragePowerW(row, selectedFileRows, index)) }}</td>
                                <td v-if="selectedFilePreviewResolution !== '5m'">{{ $n(row.runtime_min || 0) }}</td>
                                <td v-if="selectedFilePreviewResolution === 'day'">{{ $n(row.sample_count || 0) }}</td>
                                <td v-if="selectedFilePreviewResolution === 'month'">{{ $n(row.day_count || 0) }}</td>
                                <td v-if="selectedFilePreviewResolution !== '5m'">{{ row.flags }}</td>
                            </tr>
                            <tr v-if="!selectedFilePreviewLoading && selectedFileRows.length === 0">
                                <td colspan="8" class="text-center text-muted">{{ $t('energyhistory.NoData') }}</td>
                            </tr>
                            <tr v-if="selectedFilePreviewLoading">
                                <td colspan="8" class="text-center text-muted">{{ $t('base.Loading') }}</td>
                            </tr>
                        </tbody>
                    </table>
                </div>

                <details class="mt-3" v-if="selectedFileRawJson">
                    <summary class="fw-semibold">{{ $t('energyhistory.RawData') }}</summary>
                    <pre class="energy-history-raw-data mt-2 mb-0">{{ selectedFileRawJson }}</pre>
                </details>
            </div>
        </div>
    </CardElement>
</template>

<script lang="ts">
import BootstrapAlert from '@/components/BootstrapAlert.vue';
import CardElement from '@/components/CardElement.vue';
import { authHeader, handleResponse } from '@/utils/authentication';
import { BIconArrowClockwise, BIconDownload, BIconSearch, BIconTrash, BIconUpload } from 'bootstrap-icons-vue';
import { defineComponent } from 'vue';

interface EnergyHistoryFile {
    path: string;
    size: number;
}

interface EnergyHistoryFileDetails {
    typeLabel: string;
    target: string;
    year: string;
    month?: string;
}

type EnergyHistoryResolution = '5m' | 'day' | 'month';

interface EnergyHistoryFileQuery {
    target: string;
    resolution: EnergyHistoryResolution;
    year: number;
    month?: number;
    date?: string;
    from?: number;
    to?: number;
}

interface EnergyHistoryRow {
    slot?: number;
    day_of_year?: number;
    month?: number;
    yield_wh?: number;
    max_power_w?: number;
    avg_power_w?: number;
    runtime_min?: number;
    sample_count?: number;
    day_count?: number;
    flags?: number;
}

interface EnergyHistoryResponse {
    target?: string;
    resolution?: string;
    date?: string;
    year?: number;
    from?: number;
    to?: number;
    interval_sec?: number;
    count?: number;
    data?: EnergyHistoryRow[];
    scan?: Record<string, unknown>;
    type?: string;
    message?: string;
    code?: number;
}

interface EnergyHistoryFileScan {
    files_scanned?: number;
    valid_blocks?: number;
    skipped_blocks?: number;
    valid_records?: number;
    skipped_records?: number;
    file_size?: number;
    last_valid_offset?: number;
    invalid_final_block?: boolean;
    can_truncate_final_block?: boolean;
}

interface EnergyHistoryFileScanResponse {
    file?: string;
    scan: EnergyHistoryFileScan;
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
        BIconSearch,
        BIconTrash,
        BIconUpload,
    },
    emits: ['changed'],
    data() {
        return {
            loading: false,
            uploading: false,
            files: [] as EnergyHistoryFile[],
            selectedFilePath: '',
            selectedFile: null as EnergyHistoryFile | null,
            selectedFileRows: [] as EnergyHistoryRow[],
            selectedFileRawData: null as EnergyHistoryResponse | null,
            selectedFileDeepScan: null as EnergyHistoryFileScanResponse | null,
            selectedFileDeepScanPath: '',
            deepScanLoadingPath: '',
            recoveringFilePath: '',
            selectedFileQuery: null as EnergyHistoryFileQuery | null,
            selectedFilePreviewDate: '',
            selectedFilePreviewLoading: false,
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
    computed: {
        selectedFileDetails(): EnergyHistoryFileDetails {
            if (!this.selectedFile) {
                return {
                    typeLabel: String(this.$t('energyhistory.FileTypeUnknown')),
                    target: '',
                    year: '',
                };
            }

            let match = this.selectedFile.path.match(/\/energy\/5m\/(total|inv_\d+)_(\d{4})_(\d{2})\.eh5$/);
            if (match) {
                return {
                    typeLabel: String(this.$t('energyhistory.FileTypeFiveMinute')),
                    target: match[1] || '',
                    year: match[2] || '',
                    month: match[3] || '',
                };
            }

            match = this.selectedFile.path.match(/\/energy\/day\/(total|inv_\d+)_(\d{4})\.ehd$/);
            if (match) {
                return {
                    typeLabel: String(this.$t('energyhistory.FileTypeDay')),
                    target: match[1] || '',
                    year: match[2] || '',
                };
            }

            match = this.selectedFile.path.match(/\/energy\/month\/(total|inv_\d+)_(\d{4})\.ehm$/);
            if (match) {
                return {
                    typeLabel: String(this.$t('energyhistory.FileTypeMonth')),
                    target: match[1] || '',
                    year: match[2] || '',
                };
            }

            return {
                typeLabel: String(this.$t('energyhistory.FileTypeUnknown')),
                target: '-',
                year: '-',
            };
        },
        selectedFilePreviewResolution(): EnergyHistoryResolution {
            return this.selectedFileQuery?.resolution || '5m';
        },
        selectedFileFirstColumnLabel(): string {
            if (this.selectedFilePreviewResolution === '5m') {
                return this.$t('energyhistory.Time');
            }
            if (this.selectedFilePreviewResolution === 'day') {
                return this.$t('energyhistory.Date');
            }
            return this.$t('energyhistory.Period');
        },
        selectedFilePreviewMinDate(): string {
            if (!this.selectedFileQuery || this.selectedFilePreviewResolution !== '5m') {
                return '';
            }
            return `${String(this.selectedFileQuery.year).padStart(4, '0')}-${String(this.selectedFileQuery.month || 1).padStart(2, '0')}-01`;
        },
        selectedFilePreviewMaxDate(): string {
            if (!this.selectedFileQuery || this.selectedFilePreviewResolution !== '5m') {
                return '';
            }
            const year = this.selectedFileQuery.year;
            const month = this.selectedFileQuery.month || 1;
            const lastDay = new Date(year, month, 0).getDate();
            return `${String(year).padStart(4, '0')}-${String(month).padStart(2, '0')}-${String(lastDay).padStart(2, '0')}`;
        },
        selectedFileRawJson(): string {
            return this.selectedFileRawData ? JSON.stringify(this.selectedFileRawData, null, 2) : '';
        },
    },
    methods: {
        loadFiles(): Promise<void> {
            this.loading = true;
            return fetch('/api/energy/history/file/list', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data: EnergyHistoryFileListResponse) => {
                    this.files = (data.files || []).sort((a, b) => a.path.localeCompare(b.path));
                    const selectedFile = this.files.find((file) => file.path === this.selectedFilePath);
                    if (selectedFile) {
                        this.selectedFile = selectedFile;
                    } else if (this.selectedFilePath) {
                        this.selectedFilePath = '';
                        this.selectedFile = null;
                        this.selectedFileRows = [];
                        this.selectedFileRawData = null;
                        this.selectedFileDeepScan = null;
                        this.selectedFileDeepScanPath = '';
                        this.selectedFileQuery = null;
                        this.selectedFilePreviewDate = '';
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
            this.selectedFile = file;
            if (this.selectedFileDeepScanPath !== file.path) {
                this.selectedFileDeepScan = null;
                this.selectedFileDeepScanPath = '';
            }
            this.selectedFileQuery = this.parseFileQuery(file.path);
            this.selectedFilePreviewDate = this.selectedFileQuery?.date || '';
            this.loadSelectedFilePreview();
        },
        parseFileQuery(path: string): EnergyHistoryFileQuery | null {
            let match = path.match(/\/energy\/5m\/(total|inv_\d+)_(\d{4})_(\d{2})\.eh5$/);
            if (match) {
                const year = Number(match[2] || 0);
                const month = Number(match[3] || 0);
                return {
                    target: match[1] || '',
                    resolution: '5m',
                    year,
                    month,
                    date: `${String(year).padStart(4, '0')}-${String(month).padStart(2, '0')}-01`,
                };
            }

            match = path.match(/\/energy\/day\/(total|inv_\d+)_(\d{4})\.ehd$/);
            if (match) {
                const year = Number(match[2] || 0);
                return {
                    target: match[1] || '',
                    resolution: 'day',
                    year,
                    from: 1,
                    to: this.isLeapYear(year) ? 366 : 365,
                };
            }

            match = path.match(/\/energy\/month\/(total|inv_\d+)_(\d{4})\.ehm$/);
            if (match) {
                const year = Number(match[2] || 0);
                return {
                    target: match[1] || '',
                    resolution: 'month',
                    year,
                    from: 1,
                    to: 12,
                };
            }

            return null;
        },
        loadSelectedFilePreview(): Promise<void> {
            if (!this.selectedFileQuery) {
                this.selectedFileRows = [];
                this.selectedFileRawData = null;
                this.selectedFilePreviewDate = '';
                return Promise.resolve();
            }

            const query = this.selectedFileQuery;
            if (query.resolution === '5m') {
                return this.loadSelectedFiveMinutePreview(query);
            }

            const params = new URLSearchParams();
            params.set('target', query.target);
            params.set('resolution', query.resolution);
            params.set('year', String(query.year));
            params.set('from', String(query.from || 1));
            params.set('to', String(query.to || 12));

            this.selectedFilePreviewLoading = true;
            return fetch('/api/energy/history?' + params.toString(), { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router, true))
                .then((data: EnergyHistoryResponse) => {
                    this.selectedFileRows = data.data || [];
                    this.selectedFileRawData = data;
                })
                .catch(() => {
                    this.selectedFileRows = [];
                    this.selectedFileRawData = null;
                })
                .finally(() => {
                    this.selectedFilePreviewLoading = false;
                });
        },
        loadSelectedFiveMinutePreview(query: EnergyHistoryFileQuery): Promise<void> {
            const date = this.selectedFilePreviewDate || query.date || this.selectedFilePreviewMinDate;
            this.selectedFilePreviewDate = date;
            this.selectedFilePreviewLoading = true;

            return this.fetchFiveMinuteData(query, date)
                .then((data) => {
                    const rows = data.data || [];
                    if (rows.length > 0 || date !== this.selectedFilePreviewMinDate) {
                        this.selectedFileRows = rows;
                        this.selectedFileRawData = data;
                        return;
                    }
                    return this.findFirstFiveMinuteData(query)
                        .then((result) => {
                            this.selectedFilePreviewDate = result.date;
                            this.selectedFileRows = result.data.data || [];
                            this.selectedFileRawData = result.data;
                        });
                })
                .catch(() => {
                    this.selectedFileRows = [];
                    this.selectedFileRawData = null;
                })
                .finally(() => {
                    this.selectedFilePreviewLoading = false;
                });
        },
        fetchFiveMinuteData(query: EnergyHistoryFileQuery, date: string): Promise<EnergyHistoryResponse> {
            const params = new URLSearchParams();
            params.set('target', query.target);
            params.set('resolution', '5m');
            params.set('date', date);

            return fetch('/api/energy/history?' + params.toString(), { headers: authHeader() })
                .then((response) => response.json())
                .then((data: EnergyHistoryResponse) => data)
                .catch(() => ({ data: [] }));
        },
        findFirstFiveMinuteData(query: EnergyHistoryFileQuery): Promise<{ date: string; data: EnergyHistoryResponse }> {
            const year = query.year;
            const month = query.month || 1;
            const lastDay = new Date(year, month, 0).getDate();
            const dates = Array.from({ length: lastDay }, (_, index) => {
                const day = index + 1;
                return `${String(year).padStart(4, '0')}-${String(month).padStart(2, '0')}-${String(day).padStart(2, '0')}`;
            });

            return dates.reduce(
                (previous, date) => previous.then((result) => {
                    if ((result.data.data || []).length > 0) {
                        return result;
                    }
                    return this.fetchFiveMinuteData(query, date)
                        .then((data) => ({ date, data }));
                }),
                Promise.resolve({ date: this.selectedFilePreviewMinDate, data: { data: [] } as EnergyHistoryResponse }),
            );
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
        deleteFile(file: EnergyHistoryFile) {
            if (!window.confirm(String(this.$t('energyhistory.DeleteFileConfirm', { file: file.path })))) {
                return;
            }

            const params = new URLSearchParams();
            params.set('file', file.path);

            fetch('/api/energy/history/file/delete?' + params.toString(), {
                method: 'POST',
                headers: authHeader(),
            })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data) => {
                    this.alert = {
                        show: true,
                        type: data.type || 'success',
                        message: data.message || String(this.$t('energyhistory.DeleteSuccess')),
                    };
                    if (this.selectedFilePath === file.path) {
                        this.selectedFilePath = '';
                        this.selectedFile = null;
                        this.selectedFileRows = [];
                        this.selectedFileRawData = null;
                        this.selectedFileDeepScan = null;
                        this.selectedFileDeepScanPath = '';
                        this.selectedFileQuery = null;
                        this.selectedFilePreviewDate = '';
                    }
                    this.$emit('changed');
                    return this.loadFiles();
                })
                .catch(() => {
                    this.alert = {
                        show: true,
                        type: 'danger',
                        message: String(this.$t('energyhistory.DeleteFailed')),
                    };
                });
        },
        deepScanFile(file: EnergyHistoryFile) {
            this.selectedFilePath = file.path;
            this.selectedFile = file;
            this.selectedFileQuery = this.parseFileQuery(file.path);
            this.selectedFilePreviewDate = this.selectedFileQuery?.date || '';
            this.loadSelectedFilePreview();
            this.deepScanLoadingPath = file.path;

            const params = new URLSearchParams();
            params.set('file', file.path);

            fetch('/api/energy/history/file/scan?' + params.toString(), { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data: EnergyHistoryFileScanResponse) => {
                    this.selectedFileDeepScan = data;
                    this.selectedFileDeepScanPath = file.path;
                })
                .catch(() => {
                    this.alert = {
                        show: true,
                        type: 'danger',
                        message: String(this.$t('energyhistory.DeepScanFailed')),
                    };
                    this.selectedFileDeepScan = null;
                    this.selectedFileDeepScanPath = '';
                })
                .finally(() => {
                    this.deepScanLoadingPath = '';
                });
        },
        recoverFile(file: EnergyHistoryFile) {
            if (!window.confirm(String(this.$t('energyhistory.RecoverFileConfirm', { file: file.path })))) {
                return;
            }

            this.recoveringFilePath = file.path;
            const params = new URLSearchParams();
            params.set('file', file.path);

            fetch('/api/energy/history/file/recover?' + params.toString(), {
                method: 'POST',
                headers: authHeader(),
            })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data: EnergyHistoryFileScanResponse & { type?: string; message?: string }) => {
                    this.alert = {
                        show: true,
                        type: data.type || 'success',
                        message: data.message || String(this.$t('energyhistory.RecoverSuccess')),
                    };
                    this.selectedFileDeepScan = data;
                    this.selectedFileDeepScanPath = file.path;
                    this.$emit('changed');
                    return this.loadFiles()
                        .then(() => this.loadSelectedFilePreview());
                })
                .catch(() => {
                    this.alert = {
                        show: true,
                        type: 'danger',
                        message: String(this.$t('energyhistory.RecoverFailed')),
                    };
                })
                .finally(() => {
                    this.recoveringFilePath = '';
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
        formatKwh(value: number): string {
            return this.$n(value / 1000);
        },
        formatRawRow(row: EnergyHistoryRow): string {
            return JSON.stringify(row, null, 2);
        },
        formatDeepScan(scan: EnergyHistoryFileScanResponse): string {
            return JSON.stringify(scan, null, 2);
        },
        selectedFileFirstColumnValue(row: EnergyHistoryRow): string {
            if (this.selectedFilePreviewResolution === '5m') {
                return row.slot !== undefined ? this.formatSlotTime(row.slot) : '';
            }
            if (this.selectedFilePreviewResolution === 'day') {
                return row.day_of_year !== undefined ? this.formatDayOfYear(this.selectedFileQuery?.year || new Date().getFullYear(), row.day_of_year) : '';
            }
            return row.month !== undefined ? `${this.selectedFileQuery?.year || ''}-${String(row.month).padStart(2, '0')}` : '';
        },
        formatSlotTime(slot: number): string {
            const minutes = slot * 5;
            const hours = Math.floor(minutes / 60);
            const minute = minutes % 60;
            return `${String(hours).padStart(2, '0')}:${String(minute).padStart(2, '0')}`;
        },
        formatDayOfYear(year: number, dayOfYear: number): string {
            const date = new Date(Date.UTC(year, 0, dayOfYear));
            return date.toISOString().slice(0, 10);
        },
        isLeapYear(year: number): boolean {
            return (year % 4 === 0 && year % 100 !== 0) || year % 400 === 0;
        },
        calculateAveragePowerW(row: EnergyHistoryRow, rows: EnergyHistoryRow[], index: number): number {
            if (index === 0) {
                return row.avg_power_w || 0;
            }

            const prevRow = rows[index - 1];
            if (!prevRow) {
                return row.avg_power_w || 0;
            }

            const currentYield = row.yield_wh || 0;
            const prevYield = prevRow.yield_wh || 0;
            const yieldDelta = currentYield - prevYield;
            if (yieldDelta <= 0) {
                return 0;
            }

            let timeDeltaMinutes = 5;
            if (this.selectedFilePreviewResolution === '5m' && row.slot !== undefined && prevRow.slot !== undefined) {
                let slotDelta = row.slot - prevRow.slot;
                if (slotDelta < 0) {
                    slotDelta = (288 - prevRow.slot) + row.slot;
                }
                timeDeltaMinutes = slotDelta * 5;
            } else if (this.selectedFilePreviewResolution === 'day' && row.day_of_year !== undefined && prevRow.day_of_year !== undefined) {
                timeDeltaMinutes = (row.day_of_year - prevRow.day_of_year) * 24 * 60;
            } else if (this.selectedFilePreviewResolution === 'month' && row.month !== undefined && prevRow.month !== undefined) {
                timeDeltaMinutes = (row.month - prevRow.month) * 30 * 24 * 60;
            }

            if (timeDeltaMinutes <= 0) {
                return 0;
            }

            return Math.round((yieldDelta * 60) / timeDeltaMinutes);
        },
    },
});
</script>

<style scoped>
.energy-history-file-row {
    cursor: pointer;
}

.energy-history-file-details {
    padding: 1rem;
    border: 1px solid var(--bs-border-color);
    border-radius: var(--bs-border-radius);
    background: var(--bs-tertiary-bg);
}

.energy-history-file-scan {
    padding: 0.75rem;
    border: 1px solid var(--bs-border-color);
    border-radius: var(--bs-border-radius);
    background: var(--bs-body-bg);
}

.energy-history-raw-data {
    max-height: 28rem;
    padding: 0.75rem;
    overflow: auto;
    border: 1px solid var(--bs-border-color);
    border-radius: var(--bs-border-radius);
    background: var(--bs-body-bg);
    font-size: 0.82rem;
}

.energy-history-preview-row {
    position: relative;
}

.energy-history-row-raw {
    display: none;
    position: absolute;
    z-index: 20;
    top: calc(100% - 0.25rem);
    left: 0;
    min-width: 18rem;
    max-width: min(36rem, 80vw);
    max-height: 22rem;
    padding: 0.75rem;
    overflow: auto;
    border: 1px solid var(--bs-border-color);
    border-radius: var(--bs-border-radius);
    background: var(--bs-body-bg);
    box-shadow: 0 0.5rem 1rem rgba(0, 0, 0, 0.15);
    color: var(--bs-body-color);
    font-size: 0.8rem;
    text-align: left;
    white-space: pre-wrap;
}

.energy-history-preview-row:hover .energy-history-row-raw {
    display: block;
}
</style>
