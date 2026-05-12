<template>
    <BasePage
        :title="$t('energyhistory.EnergyHistory')"
        :isLoading="dataLoading"
        :isWideScreen="true"
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
                <div class="col-12 col-md-auto">
                    <label class="form-label d-block">{{ $t('energyhistory.Period') }}</label>
                    <div class="btn-group" role="group">
                        <button
                            type="button"
                            class="btn"
                            :class="query.view === 'day' ? 'btn-primary' : 'btn-outline-primary'"
                            @click="setView('day')"
                        >
                            {{ $t('energyhistory.Day') }}
                        </button>
                        <button
                            type="button"
                            class="btn"
                            :class="query.view === 'month' ? 'btn-primary' : 'btn-outline-primary'"
                            @click="setView('month')"
                        >
                            {{ $t('energyhistory.MonthView') }}
                        </button>
                        <button
                            type="button"
                            class="btn"
                            :class="query.view === 'year' ? 'btn-primary' : 'btn-outline-primary'"
                            @click="setView('year')"
                        >
                            {{ $t('energyhistory.YearView') }}
                        </button>
                    </div>
                </div>
                <div class="col-12 col-md-3" v-if="query.view === 'day'">
                    <label class="form-label" for="energy-history-date">{{ $t('energyhistory.Date') }}</label>
                    <input id="energy-history-date" v-model="query.date" type="date" class="form-control" />
                </div>
                <div class="col-12 col-md-3" v-if="query.view === 'month'">
                    <label class="form-label" for="energy-history-month">{{ $t('energyhistory.Month') }}</label>
                    <input id="energy-history-month" v-model="query.month" type="month" class="form-control" />
                </div>
                <div class="col-6 col-md-2" v-if="query.view === 'year'">
                    <label class="form-label" for="energy-history-year">{{ $t('energyhistory.Year') }}</label>
                    <input id="energy-history-year" v-model.number="query.year" type="number" class="form-control" min="2000" />
                </div>
                <div class="col-12 col-md-auto">
                    <button type="submit" class="btn btn-primary" :disabled="historyLoading">
                        <BIconSearch class="me-1" />
                        {{ $t('energyhistory.Load') }}
                    </button>
                </div>
            </form>
        </CardElement>

        <CardElement :text="$t('energyhistory.Chart')" textVariant="text-bg-primary" add-space>
            <div class="energy-history-chart">
                <ChartComponent
                    v-if="chartHasData"
                    type="bar"
                    :data="chartData"
                    :options="chartOptions"
                    :height="320"
                />
                <div v-else class="text-center text-muted py-4">{{ $t('energyhistory.NoData') }}</div>
            </div>
        </CardElement>

        <CardElement :text="$t('energyhistory.Results')" textVariant="text-bg-primary" add-space table>
            <div class="table-responsive">
                <table class="table table-hover table-condensed align-middle">
                    <thead>
                        <tr>
                            <th>{{ firstColumnLabel }}</th>
                            <th>{{ $t('energyhistory.YieldWh') }}</th>
                            <th v-if="resolution === '5m'">{{ $t('energyhistory.AvgPowerW') }}</th>
                            <th v-if="resolution !== '5m'">{{ $t('energyhistory.MaxPowerW') }}</th>
                            <th v-if="resolution !== '5m'">{{ $t('energyhistory.AvgPowerW') }}</th>
                            <th v-if="resolution !== '5m'">{{ $t('energyhistory.RuntimeMin') }}</th>
                            <th v-if="resolution === 'day'">{{ $t('energyhistory.SampleCount') }}</th>
                            <th v-if="resolution === 'month'">{{ $t('energyhistory.DayCount') }}</th>
                            <th v-if="resolution !== '5m'">{{ $t('energyhistory.Flags') }}</th>
                        </tr>
                    </thead>
                    <tbody>
                        <tr v-for="(row, index) in activeHistory.data" :key="index">
                            <td>{{ firstColumnValue(row) }}</td>
                            <td>{{ $n(row.yield_wh || 0) }}</td>
                            <td v-if="resolution === '5m'">{{ $n(row.avg_power_w || 0) }}</td>
                            <td v-if="resolution !== '5m'">{{ $n(row.max_power_w || 0) }}</td>
                            <td v-if="resolution !== '5m'">{{ $n(row.avg_power_w || 0) }}</td>
                            <td v-if="resolution !== '5m'">{{ $n(row.runtime_min || 0) }}</td>
                            <td v-if="resolution === 'day'">{{ $n(row.sample_count || 0) }}</td>
                            <td v-if="resolution === 'month'">{{ $n(row.day_count || 0) }}</td>
                            <td v-if="resolution !== '5m'">{{ row.flags }}</td>
                        </tr>
                        <tr v-if="!historyLoading && activeHistory.data.length === 0">
                            <td colspan="8" class="text-center text-muted">{{ $t('energyhistory.NoData') }}</td>
                        </tr>
                    </tbody>
                </table>
            </div>
            <div class="small text-muted px-3 pb-3" v-if="activeHistory.scan">
                {{ $t('energyhistory.ScanSummary', {
                    blocks: activeHistory.scan.valid_blocks || 0,
                    skipped: activeHistory.scan.skipped_blocks || 0,
                    records: activeHistory.scan.valid_records || 0,
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
import {
    BarElement,
    BarController,
    CategoryScale,
    Chart as ChartJS,
    Filler,
    Legend,
    LinearScale,
    LineController,
    LineElement,
    PointElement,
    Tooltip,
    type ChartData,
    type ChartOptions,
} from 'chart.js';
import { defineComponent } from 'vue';
import { Chart as ChartComponent } from 'vue-chartjs';

ChartJS.register(CategoryScale, LinearScale, BarController, LineController, BarElement, LineElement, PointElement, Filler, Tooltip, Legend);

type Resolution = '5m' | 'day' | 'month';
type ViewMode = 'day' | 'month' | 'year';

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
    target?: string;
    data: EnergyHistoryRow[];
    scan?: {
        valid_blocks?: number;
        skipped_blocks?: number;
        valid_records?: number;
    };
}

interface EnergyHistorySeries {
    id: string;
    label: string;
    color: string;
    data: EnergyHistoryRow[];
    scan?: EnergyHistoryResponse['scan'];
}

interface InverterConfig {
    name?: string;
    serial: string;
    order?: number;
    poll_enable?: boolean;
}

interface InverterListResponse {
    inverter?: InverterConfig[];
}

function localDateInputValue(date = new Date()): string {
    const year = date.getFullYear();
    const month = String(date.getMonth() + 1).padStart(2, '0');
    const day = String(date.getDate()).padStart(2, '0');
    return `${year}-${month}-${day}`;
}

function localMonthInputValue(date = new Date()): string {
    const year = date.getFullYear();
    const month = String(date.getMonth() + 1).padStart(2, '0');
    return `${year}-${month}`;
}

export default defineComponent({
    components: {
        BasePage,
        CardElement,
        BIconSearch,
        ChartComponent,
    },
    data() {
        return {
            dataLoading: true,
            historyLoading: false,
            status: {} as EnergyHistoryStatus,
            inverters: [] as InverterConfig[],
            histories: [] as EnergyHistorySeries[],
            query: {
                view: 'day' as ViewMode,
                date: localDateInputValue(),
                month: localMonthInputValue(),
                year: new Date().getFullYear(),
            },
        };
    },
    computed: {
        resolution(): Resolution {
            if (this.query.view === 'day') {
                return '5m';
            }
            if (this.query.view === 'month') {
                return 'day';
            }
            return 'month';
        },
        activeHistory(): EnergyHistorySeries {
            return this.histories.find((history) => history.id === 'total') || {
                id: 'total',
                label: 'total',
                color: '#198754',
                data: [],
            };
        },
        chartHasData(): boolean {
            return this.histories.some((history) => history.data.length > 0);
        },
        firstColumnLabel(): string {
            if (this.resolution === '5m') {
                return this.$t('energyhistory.Time');
            }
            if (this.resolution === 'day') {
                return this.$t('energyhistory.Date');
            }
            return this.$t('energyhistory.Period');
        },
        chartData(): ChartData<'bar' | 'line', number[], string> {
            const reference = this.histories.find((history) => history.data.length > 0);
            const labels = reference?.data.map((row) => this.firstColumnValue(row).toString()) || [];

            return {
                labels,
                datasets: this.histories
                    .filter((history) => history.data.length > 0)
                    .map((history, index) => ({
                        type: this.resolution === '5m' || index === 0 ? 'line' : 'bar',
                        label: history.label,
                        data: history.data.map((row) => row.yield_wh || 0),
                        borderColor: history.color,
                        backgroundColor: this.withAlpha(history.color, this.resolution === '5m' ? 0.14 : 0.45),
                        borderWidth: history.id === 'total' ? 3 : 2,
                        fill: this.resolution === '5m' && history.id === 'total',
                        tension: 0.25,
                        pointRadius: this.resolution === '5m' ? 0 : 2,
                        yAxisID: 'yield',
                    })),
            };
        },
        chartOptions(): ChartOptions<'bar' | 'line'> {
            return {
                responsive: true,
                maintainAspectRatio: false,
                interaction: {
                    intersect: false,
                    mode: 'index',
                },
                plugins: {
                    legend: {
                        display: true,
                        position: 'top',
                    },
                    tooltip: {
                        enabled: true,
                    },
                },
                scales: {
                    x: {
                        ticks: {
                            maxRotation: 0,
                            autoSkip: true,
                            maxTicksLimit: 12,
                        },
                        grid: {
                            display: false,
                        },
                    },
                    yield: {
                        type: 'linear',
                        position: 'left',
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: this.$t('energyhistory.YieldWh'),
                        },
                    },
                },
            };
        },
    },
    created() {
        this.reloadAll();
    },
    methods: {
        reloadAll() {
            this.dataLoading = true;
            Promise.all([this.loadStatus(), this.loadInverters()])
                .then(() => this.loadHistory())
                .finally(() => {
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
        loadInverters() {
            return fetch('/api/inverter/list', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router, true))
                .then((data: InverterListResponse) => {
                    this.inverters = (data.inverter || [])
                        .filter((inverter) => inverter.serial)
                        .sort((a, b) => (a.order || 0) - (b.order || 0));
                })
                .catch(() => {
                    this.inverters = [];
                });
        },
        loadHistory() {
            this.historyLoading = true;
            const targets = this.historyTargets();
            return Promise.all(targets.map((target) => this.loadTargetHistory(target)))
                .then((histories) => {
                    this.histories = histories;
                })
                .finally(() => {
                    this.historyLoading = false;
                });
        },
        loadTargetHistory(target: EnergyHistorySeries): Promise<EnergyHistorySeries> {
            const params = new URLSearchParams();
            params.set('resolution', this.resolution);
            params.set('target', target.id);
            if (this.resolution === '5m') {
                params.set('date', this.query.date);
            } else if (this.resolution === 'day') {
                const range = this.monthDayRange(this.query.month);
                params.set('year', String(range.year));
                params.set('from', String(range.from));
                params.set('to', String(range.to));
            } else {
                params.set('year', String(this.query.year));
                params.set('from', '1');
                params.set('to', '12');
            }

            return fetch('/api/energy/history?' + params.toString(), { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router, true))
                .then((data: EnergyHistoryResponse) => ({
                    ...target,
                    data: data.data || [],
                    scan: data.scan,
                }))
                .catch(() => ({
                    ...target,
                    data: [],
                }));
        },
        historyTargets(): EnergyHistorySeries[] {
            const colors = ['#198754', '#0d6efd', '#dc3545', '#fd7e14', '#6f42c1', '#20c997', '#0dcaf0', '#d63384', '#6c757d', '#ffc107', '#6610f2'];
            const targets: EnergyHistorySeries[] = [{
                id: 'total',
                label: String(this.$t('energyhistory.Total')),
                color: colors[0] || '#198754',
                data: [],
            }];

            this.inverters.forEach((inverter, index) => {
                const color = colors[(index + 1) % colors.length] || '#0d6efd';
                targets.push({
                    id: `inv_${this.hexSerialToDecimal(inverter.serial)}`,
                    label: inverter.name || inverter.serial,
                    color,
                    data: [],
                });
            });

            return targets;
        },
        setView(view: ViewMode) {
            this.query.view = view;
            this.loadHistory();
        },
        firstColumnValue(row: EnergyHistoryRow): string | number {
            if (this.resolution === '5m') {
                return row.slot !== undefined ? this.formatSlotTime(row.slot) : '';
            }
            if (this.resolution === 'day') {
                const year = this.monthDayRange(this.query.month).year;
                return row.day_of_year !== undefined ? this.formatDayOfYear(year, row.day_of_year) : '';
            }
            return row.month !== undefined ? this.formatMonth(this.query.year, row.month) : '';
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
        formatMonth(year: number, month: number): string {
            return `${year}-${String(month).padStart(2, '0')}`;
        },
        monthDayRange(value: string): { year: number; from: number; to: number } {
            const parts = value.split('-');
            const parsedYear = Number(parts[0]);
            const parsedMonth = Number(parts[1]);
            const year = Number.isFinite(parsedYear) ? parsedYear : new Date().getFullYear();
            const month = Number.isFinite(parsedMonth) ? parsedMonth : new Date().getMonth() + 1;
            const first = new Date(Date.UTC(year, month - 1, 1));
            const last = new Date(Date.UTC(year, month, 0));
            return {
                year,
                from: this.dayOfYear(first),
                to: this.dayOfYear(last),
            };
        },
        dayOfYear(date: Date): number {
            const start = Date.UTC(date.getUTCFullYear(), 0, 0);
            return Math.floor((date.getTime() - start) / 86400000);
        },
        hexSerialToDecimal(serial: string): string {
            return BigInt(`0x${serial}`).toString(10);
        },
        withAlpha(hexColor: string, alpha: number): string {
            const value = hexColor.replace('#', '');
            const red = parseInt(value.substring(0, 2), 16);
            const green = parseInt(value.substring(2, 4), 16);
            const blue = parseInt(value.substring(4, 6), 16);
            return `rgba(${red}, ${green}, ${blue}, ${alpha})`;
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
.energy-history-chart {
    position: relative;
    min-height: 320px;
}
</style>
