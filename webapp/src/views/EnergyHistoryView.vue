<template>
    <BasePage
        :title="$t('energyhistory.EnergyHistory')"
        :isLoading="dataLoading"
        :show-reload="true"
        @reload="reloadAll"
    >
        <CardElement :text="$t('energyhistory.Status')" textVariant="text-bg-primary" table>
            <div class="table-responsive">
                <table class="table table-hover table-condensed">
                    <tbody>
                        <tr>
                            <th>{{ $t('energyhistory.FilesScanned') }}</th>
                            <td>{{ $n(status.files_scanned || 0) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('energyhistory.ValidBlocks') }}</th>
                            <td>{{ $n(status.valid_blocks || 0) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('energyhistory.SkippedBlocks') }}</th>
                            <td>{{ $n(status.skipped_blocks || 0) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('energyhistory.ValidRecords') }}</th>
                            <td>{{ $n(status.valid_records || 0) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('energyhistory.LittleFsUsed') }}</th>
                            <td>{{ formatBytes(status.littlefs_used || 0) }} / {{ formatBytes(status.littlefs_total || 0) }}</td>
                        </tr>
                    </tbody>
                </table>
            </div>
        </CardElement>

        <CardElement :text="$t('energyhistory.Query')" textVariant="text-bg-primary" add-space>
            <form class="row g-3 align-items-end" @submit.prevent="loadHistory">
                <div class="col-12 col-md-3">
                    <label class="form-label" for="energy-history-target">{{ $t('energyhistory.Target') }}</label>
                    <input
                        id="energy-history-target"
                        v-model.trim="query.target"
                        type="text"
                        class="form-control"
                        autocomplete="off"
                    />
                </div>
                <div class="col-12 col-md-2">
                    <label class="form-label" for="energy-history-resolution">{{ $t('energyhistory.Resolution') }}</label>
                    <select id="energy-history-resolution" v-model="query.resolution" class="form-select">
                        <option value="5m">5m</option>
                        <option value="day">{{ $t('energyhistory.Day') }}</option>
                        <option value="month">{{ $t('energyhistory.Month') }}</option>
                    </select>
                </div>
                <div class="col-12 col-md-3" v-if="query.resolution === '5m'">
                    <label class="form-label" for="energy-history-date">{{ $t('energyhistory.Date') }}</label>
                    <input id="energy-history-date" v-model="query.date" type="date" class="form-control" />
                </div>
                <div class="col-6 col-md-2" v-if="query.resolution !== '5m'">
                    <label class="form-label" for="energy-history-year">{{ $t('energyhistory.Year') }}</label>
                    <input id="energy-history-year" v-model.number="query.year" type="number" class="form-control" min="2000" />
                </div>
                <div class="col-6 col-md-2" v-if="query.resolution !== '5m'">
                    <label class="form-label" for="energy-history-from">{{ $t('energyhistory.From') }}</label>
                    <input id="energy-history-from" v-model.number="query.from" type="number" class="form-control" min="1" />
                </div>
                <div class="col-6 col-md-2" v-if="query.resolution !== '5m'">
                    <label class="form-label" for="energy-history-to">{{ $t('energyhistory.To') }}</label>
                    <input id="energy-history-to" v-model.number="query.to" type="number" class="form-control" min="1" />
                </div>
                <div class="col-12 col-md-auto">
                    <button type="submit" class="btn btn-primary" :disabled="historyLoading">
                        <BIconSearch class="me-1" />
                        {{ $t('energyhistory.Load') }}
                    </button>
                </div>
            </form>
        </CardElement>

        <CardElement :text="$t('energyhistory.Results')" textVariant="text-bg-primary" add-space table>
            <div class="table-responsive">
                <table class="table table-hover table-condensed align-middle">
                    <thead>
                        <tr>
                            <th>{{ firstColumnLabel }}</th>
                            <th>{{ $t('energyhistory.YieldWh') }}</th>
                            <th v-if="query.resolution === '5m'">{{ $t('energyhistory.Flags') }}</th>
                            <th v-if="query.resolution !== '5m'">{{ $t('energyhistory.MaxPowerW') }}</th>
                            <th v-if="query.resolution !== '5m'">{{ $t('energyhistory.AvgPowerW') }}</th>
                            <th v-if="query.resolution !== '5m'">{{ $t('energyhistory.RuntimeMin') }}</th>
                            <th v-if="query.resolution === 'day'">{{ $t('energyhistory.SampleCount') }}</th>
                            <th v-if="query.resolution === 'month'">{{ $t('energyhistory.DayCount') }}</th>
                            <th v-if="query.resolution !== '5m'">{{ $t('energyhistory.Flags') }}</th>
                        </tr>
                    </thead>
                    <tbody>
                        <tr v-for="(row, index) in history.data" :key="index">
                            <td>{{ firstColumnValue(row) }}</td>
                            <td>{{ $n(row.yield_wh || 0) }}</td>
                            <td v-if="query.resolution === '5m'">{{ row.flags }}</td>
                            <td v-if="query.resolution !== '5m'">{{ $n(row.max_power_w || 0) }}</td>
                            <td v-if="query.resolution !== '5m'">{{ $n(row.avg_power_w || 0) }}</td>
                            <td v-if="query.resolution !== '5m'">{{ $n(row.runtime_min || 0) }}</td>
                            <td v-if="query.resolution === 'day'">{{ $n(row.sample_count || 0) }}</td>
                            <td v-if="query.resolution === 'month'">{{ $n(row.day_count || 0) }}</td>
                            <td v-if="query.resolution !== '5m'">{{ row.flags }}</td>
                        </tr>
                        <tr v-if="!historyLoading && history.data.length === 0">
                            <td colspan="8" class="text-center text-muted">{{ $t('energyhistory.NoData') }}</td>
                        </tr>
                    </tbody>
                </table>
            </div>
            <div class="small text-muted px-3 pb-3" v-if="history.scan">
                {{ $t('energyhistory.ScanSummary', {
                    blocks: history.scan.valid_blocks || 0,
                    skipped: history.scan.skipped_blocks || 0,
                    records: history.scan.valid_records || 0,
                }) }}
            </div>
        </CardElement>
    </BasePage>
</template>

<script lang="ts">
import BasePage from '@/components/BasePage.vue';
import CardElement from '@/components/CardElement.vue';
import { authHeader, handleResponse } from '@/utils/authentication';
import { BIconSearch } from 'bootstrap-icons-vue';
import { defineComponent } from 'vue';

type Resolution = '5m' | 'day' | 'month';

interface EnergyHistoryStatus {
    files_scanned?: number;
    valid_blocks?: number;
    skipped_blocks?: number;
    valid_records?: number;
    littlefs_total?: number;
    littlefs_used?: number;
}

interface EnergyHistoryRow {
    day?: number;
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
    data: EnergyHistoryRow[];
    scan?: {
        valid_blocks?: number;
        skipped_blocks?: number;
        valid_records?: number;
    };
}

export default defineComponent({
    components: {
        BasePage,
        CardElement,
        BIconSearch,
    },
    data() {
        return {
            dataLoading: true,
            historyLoading: false,
            status: {} as EnergyHistoryStatus,
            history: { data: [] } as EnergyHistoryResponse,
            query: {
                target: 'total',
                resolution: '5m' as Resolution,
                date: '2099-06-01',
                year: 2099,
                from: 1,
                to: 7,
            },
        };
    },
    computed: {
        firstColumnLabel(): string {
            if (this.query.resolution === '5m') {
                return this.$t('energyhistory.Slot');
            }
            if (this.query.resolution === 'day') {
                return this.$t('energyhistory.DayOfYear');
            }
            return this.$t('energyhistory.Month');
        },
    },
    created() {
        this.reloadAll();
    },
    methods: {
        reloadAll() {
            this.dataLoading = true;
            Promise.all([this.loadStatus(), this.loadHistory()]).finally(() => {
                this.dataLoading = false;
            });
        },
        loadStatus() {
            return fetch('/api/energy/history/status', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data) => {
                    this.status = data;
                });
        },
        loadHistory() {
            this.historyLoading = true;
            const params = new URLSearchParams();
            params.set('resolution', this.query.resolution);
            params.set('target', this.query.target || 'total');
            if (this.query.resolution === '5m') {
                params.set('date', this.query.date);
            } else {
                params.set('year', String(this.query.year));
                params.set('from', String(this.query.from));
                params.set('to', String(this.query.to));
            }

            return fetch('/api/energy/history?' + params.toString(), { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router, true))
                .then((data) => {
                    this.history = data;
                })
                .catch(() => {
                    this.history = { data: [] };
                })
                .finally(() => {
                    this.historyLoading = false;
                });
        },
        firstColumnValue(row: EnergyHistoryRow): string | number {
            if (this.query.resolution === '5m') {
                return row.slot ?? '';
            }
            if (this.query.resolution === 'day') {
                return row.day_of_year ?? '';
            }
            return row.month ?? '';
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
    watch: {
        'query.resolution'(resolution: Resolution) {
            if (resolution === 'month') {
                this.query.from = 1;
                this.query.to = 12;
            } else if (resolution === 'day') {
                this.query.from = 1;
                this.query.to = 7;
            }
        },
    },
});
</script>
