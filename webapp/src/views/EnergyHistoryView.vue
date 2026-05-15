<template>
    <BasePage
        :title="$t('energyhistory.EnergyHistory')"
        :isLoading="dataLoading"
        :isWideScreen="true"
        :show-reload="true"
        @reload="reloadAll"
    >
        <div class="d-flex flex-wrap gap-2 align-items-center mb-3">
            <InverterTotalInfo v-if="liveTotal" :totalData="liveTotal" class="energy-history-row-totals" />
        </div>

        <CardElement :text="$t('energyhistory.Query')" textVariant="text-bg-primary" add-space>
            <!-- Breadcrumb Navigation -->
            <div class="mb-3" v-if="showDrillDownBreadcrumb">
                <nav aria-label="breadcrumb">
                    <ol class="breadcrumb mb-0">
                        <li class="breadcrumb-item">
                            <button
                                type="button"
                                class="btn btn-link p-0 text-decoration-none"
                                @click="resetToDrillDown('year')"
                            >
                                {{ new Date().getFullYear() }}
                            </button>
                        </li>
                        <li class="breadcrumb-item" v-if="query.view === 'month' || query.view === 'day'">
                            <button
                                type="button"
                                class="btn btn-link p-0 text-decoration-none"
                                @click="resetToDrillDown('month')"
                            >
                                {{ formatMonthName(query.month) }}
                            </button>
                        </li>
                        <li class="breadcrumb-item active" v-if="query.view === 'day'">
                            {{ new Date(query.date).toLocaleDateString() }}
                        </li>
                    </ol>
                </nav>
            </div>

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
            <div class="energy-history-chart mt-4">
                <ChartComponent
                    v-if="dailyEnergyHistories.some((history) => history.data.length > 0)"
                    type="bar"
                    :data="dailyEnergyChartData"
                    :options="dailyEnergyChartOptions"
                    :height="260"
                />
                <div v-else class="text-center text-muted py-4">{{ $t('energyhistory.NoData') }}</div>
            </div>
        </CardElement>

        <div class="row row-cols-1 row-cols-sm-2 row-cols-xl-5 g-3 energy-history-status-tiles mt-5">
            <div class="col" v-for="tile in statusTiles" :key="tile.label">
                <div class="energy-history-status-tile">
                    <div class="text-muted small">{{ tile.label }}</div>
                    <div class="fs-4 fw-semibold">{{ tile.value }}</div>
                    <div v-if="tile.detail" class="text-muted small">{{ tile.detail }}</div>
                </div>
            </div>
        </div>

        <CardElement :text="$t('energyhistory.Results')" textVariant="text-bg-primary" add-space table>
            <!-- Metadata Info -->
            <div class="row g-2 mb-3 px-3" v-if="activeHistory.metadata">
                <div class="col-12 col-md-6">
                    <div class="card border-0 bg-light">
                        <div class="card-body py-2 px-3">
                            <div class="row g-2 small">
                                <div class="col-auto">
                                    <strong>{{ $t('energyhistory.Target') }}:</strong>
                                    <span class="ms-2">{{ activeHistory.metadata.target }}</span>
                                </div>
                                <div class="col-auto">
                                    <strong>{{ $t('energyhistory.Resolution') }}:</strong>
                                    <span class="ms-2">{{ activeHistory.metadata.resolution }}</span>
                                </div>
                                <div class="col-auto" v-if="activeHistory.metadata.date">
                                    <strong>{{ $t('energyhistory.Date') }}:</strong>
                                    <span class="ms-2">{{ activeHistory.metadata.date }}</span>
                                </div>
                                <div class="col-auto" v-if="activeHistory.metadata.year">
                                    <strong>{{ $t('energyhistory.Year') }}:</strong>
                                    <span class="ms-2">{{ activeHistory.metadata.year }}</span>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
                <div class="col-12 col-md-6">
                    <div class="card border-0 bg-light">
                        <div class="card-body py-2 px-3">
                            <div class="row g-2 small">
                                <div class="col-auto" v-if="activeHistory.metadata.from !== undefined && activeHistory.metadata.to !== undefined">
                                    <strong>Range:</strong>
                                    <span class="ms-2">{{ activeHistory.metadata.from }} - {{ activeHistory.metadata.to }}</span>
                                </div>
                                <div class="col-auto" v-if="activeHistory.metadata.interval_sec">
                                    <strong>{{ $t('energyhistory.Interval') }}:</strong>
                                    <span class="ms-2">{{ activeHistory.metadata.interval_sec }}s</span>
                                </div>
                                <div class="col-auto" v-if="activeHistory.metadata.count !== undefined">
                                    <strong>{{ $t('energyhistory.Count') }}:</strong>
                                    <span class="ms-2">{{ activeHistory.metadata.count }}</span>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>

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
                        <tr
                            v-for="(row, index) in activeHistory.data"
                            :key="index"
                            :style="canDrillDown(row) ? 'cursor: pointer;' : ''"
                            @click="canDrillDown(row) && drillDown(row)"
                            :class="canDrillDown(row) ? 'table-active' : ''"
                        >
                            <td>
                                <span v-if="canDrillDown(row)" class="me-2">➜</span>
                                {{ firstColumnValue(row) }}
                            </td>
                            <td>{{ formatKwh(row.yield_wh || 0) }}</td>
                            <td v-if="resolution === '5m'">{{ $n(calculateAveragePowerW(row, activeHistory.data, index)) }}</td>
                            <td v-if="resolution !== '5m'">{{ $n(row.max_power_w || 0) }}</td>
                            <td v-if="resolution !== '5m'">{{ $n(calculateAveragePowerW(row, activeHistory.data, index)) }}</td>
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
import InverterTotalInfo from '@/components/InverterTotalInfo.vue';
import type { Total } from '@/types/LiveDataStatus';
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
    resolution?: string;
    date?: string;
    year?: number;
    from?: number;
    to?: number;
    interval_sec?: number;
    count?: number;
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
    metadata?: {
        target?: string;
        resolution?: string;
        date?: string;
        year?: number;
        from?: number;
        to?: number;
        interval_sec?: number;
        count?: number;
    };
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

function statusValue(value?: number): number {
    return value || 0;
}

const DemoHistoryTargets = [
    { id: 'inv_999999990101', label: 'Demo WR 1' },
    { id: 'inv_999999990102', label: 'Demo WR 2' },
];

export default defineComponent({
    components: {
        BasePage,
        CardElement,
        InverterTotalInfo,
        BIconSearch,
        ChartComponent,
    },
    data() {
        return {
            dataLoading: true,
            historyLoading: false,
            status: {} as EnergyHistoryStatus,
            liveTotal: null as Total | null,
            inverters: [] as InverterConfig[],
            histories: [] as EnergyHistorySeries[],
            dailyEnergyHistories: [] as EnergyHistorySeries[],
            drillDownMode: false,
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
        showDrillDownBreadcrumb(): boolean {
            return this.drillDownMode && this.query.view !== 'year';
        },
        statusTiles(): { label: string; value: string; detail?: string }[] {
            return [
                {
                    label: this.$t('energyhistory.FilesScanned'),
                    value: this.$n(statusValue(this.status.files_scanned)),
                },
                {
                    label: this.$t('energyhistory.ValidBlocks'),
                    value: this.$n(statusValue(this.status.valid_blocks)),
                },
                {
                    label: this.$t('energyhistory.SkippedBlocks'),
                    value: this.$n(statusValue(this.status.skipped_blocks)),
                },
                {
                    label: this.$t('energyhistory.ValidRecords'),
                    value: this.$n(statusValue(this.status.valid_records)),
                },
                {
                    label: this.$t('energyhistory.LittleFsUsed'),
                    value: this.formatBytes(statusValue(this.status.littlefs_used)),
                    detail: this.formatBytes(statusValue(this.status.littlefs_total)),
                },
            ];
        },
        chartData(): ChartData<'bar' | 'line', number[], string> {
            const totalHistory = this.histories.find((history) => history.id === 'total');
            const inverterHistories = this.histories.filter((history) => history.id !== 'total' && history.data.length > 0);
            const reference = totalHistory && totalHistory.data.length > 0
                ? totalHistory
                : this.histories.find((history) => history.data.length > 0);
            const labels = reference?.data.map((row) => this.firstColumnValue(row).toString()) || [];
            const powerKey = this.resolution === '5m' ? 'avg_power_w' : 'max_power_w';
            const totalPowerDataset = totalHistory && totalHistory.data.length > 0
                ? [{
                    type: 'line' as const,
                    label: `${this.$t('energyhistory.Total')} ${this.resolution === '5m'
                        ? this.$t('energyhistory.AvgPowerW')
                        : this.$t('energyhistory.MaxPowerW')}`,
                    data: totalHistory.data.map((row, index) => this.resolution === '5m' ? this.calculateAveragePowerW(row, totalHistory.data, index) : row[powerKey] || 0),
                    borderColor: '#212529',
                    backgroundColor: 'transparent',
                    borderWidth: 3,
                    fill: false,
                    tension: 0.18,
                    pointRadius: this.resolution === '5m' ? 0 : 2,
                    yAxisID: 'power',
                }]
                : [];
            const powerDatasets = inverterHistories.map((history) => ({
                type: 'line' as const,
                label: `${history.label} ${this.resolution === '5m'
                    ? this.$t('energyhistory.AvgPowerW')
                    : this.$t('energyhistory.MaxPowerW')}`,
                data: history.data.map((row, index) => this.resolution === '5m' ? this.calculateAveragePowerW(row, history.data, index) : row[powerKey] || 0),
                borderColor: history.color,
                backgroundColor: this.withAlpha(history.color, 0.18),
                borderWidth: 2,
                fill: true,
                tension: 0.25,
                pointRadius: this.resolution === '5m' ? 0 : 2,
                yAxisID: 'power',
            }));
            const energyDataset = totalHistory && totalHistory.data.length > 0
                ? [{
                    type: 'line' as const,
                    label: this.$t('energyhistory.CumulativeEnergyKwh'),
                    data: this.cumulativeEnergyData(totalHistory.data),
                    borderColor: totalHistory.color,
                    backgroundColor: 'transparent',
                    borderWidth: 3,
                    fill: false,
                    tension: 0.18,
                    pointRadius: this.resolution === '5m' ? 0 : 2,
                    yAxisID: 'energy',
                }]
                : [];

            return {
                labels,
                datasets: [...totalPowerDataset, ...powerDatasets, ...energyDataset],
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
                    power: {
                        type: 'linear',
                        position: 'left',
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: this.resolution === '5m'
                                ? this.$t('energyhistory.AvgPowerW')
                                : this.$t('energyhistory.MaxPowerW'),
                        },
                    },
                    energy: {
                        type: 'linear',
                        position: 'right',
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: this.$t('energyhistory.CumulativeEnergyKwh'),
                        },
                        grid: {
                            drawOnChartArea: false,
                        },
                    },
                },
            };
        },
        dailyEnergyChartData(): ChartData<'bar', number[], string> {
            const reference = this.dailyEnergyHistories.find((history) => history.data.length > 0);
            return {
                labels: reference?.data.map((row) => this.dailyEnergyLabel(row)) || [],
                datasets: this.dailyEnergyHistories
                    .filter((history) => history.data.length > 0)
                    .map((history) => ({
                        type: 'bar' as const,
                        label: history.label,
                        data: history.data.map((row) => this.whToKwh(row.yield_wh || 0)),
                        borderColor: history.color,
                        backgroundColor: this.withAlpha(history.color, 0.72),
                        borderWidth: 1,
                        stack: 'daily-energy',
                    })),
            };
        },
        dailyEnergyChartOptions(): ChartOptions<'bar'> {
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
                        mode: 'index',
                        intersect: false,
                        callbacks: {
                            label: (item) => {
                                const value = typeof item.parsed.y === 'number' ? item.parsed.y : 0;
                                return `${item.dataset.label || ''}: ${this.$n(value)} kWh`;
                            },
                            footer: (items) => {
                                const dataIndex = items[0]?.dataIndex;
                                if (dataIndex === undefined) {
                                    return '';
                                }

                                const total = this.dailyEnergyHistories.reduce((sum, history) => {
                                    return sum + this.whToKwh(history.data[dataIndex]?.yield_wh || 0);
                                }, 0);
                                return `${this.$t('energyhistory.Total')}: ${this.$n(total)} kWh`;
                            },
                        },
                    },
                },
                scales: {
                    x: {
                        stacked: true,
                        ticks: {
                            maxRotation: 0,
                            autoSkip: true,
                            maxTicksLimit: 16,
                        },
                        grid: {
                            display: false,
                        },
                    },
                    y: {
                        stacked: true,
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: this.$t('energyhistory.DailyEnergyKwh'),
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
            Promise.all([this.loadStatus(), this.loadLiveTotal(), this.loadInverters()])
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
        loadLiveTotal() {
            return fetch('/api/livedata/status', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router, true))
                .then((data) => {
                    this.liveTotal = data.total || null;
                })
                .catch(() => {
                    this.liveTotal = null;
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
            return Promise.all([
                Promise.all(targets.map((target) => this.loadTargetHistory(target))),
                this.loadDailyEnergyHistory(),
            ])
                .then(([histories]) => {
                    this.histories = histories;
                })
                .finally(() => {
                    this.historyLoading = false;
                });
        },
        loadDailyEnergyHistory(): Promise<void> {
            const range = this.currentMonthDayRange();
            const targets = this.historyTargets().filter((target) => target.id !== 'total');

            return Promise.all(targets.map((target) => this.loadDailyEnergyTargetHistory(target, range)))
                .then((histories) => {
                    this.dailyEnergyHistories = histories;
                })
                .catch(() => {
                    this.dailyEnergyHistories = [];
                });
        },
        loadDailyEnergyTargetHistory(target: EnergyHistorySeries, range: { year: number; from: number; to: number }): Promise<EnergyHistorySeries> {
            const params = new URLSearchParams();
            params.set('resolution', 'day');
            params.set('target', target.id);
            params.set('year', String(range.year));
            params.set('from', String(range.from));
            params.set('to', String(range.to));

            return fetch('/api/energy/history?' + params.toString(), { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router, true))
                .then((data: EnergyHistoryResponse) => ({
                    ...target,
                    data: data.data || [],
                    scan: data.scan,
                    metadata: {
                        target: data.target,
                        resolution: data.resolution,
                        date: data.date,
                        year: data.year,
                        from: data.from,
                        to: data.to,
                        interval_sec: data.interval_sec,
                        count: data.count,
                    },
                }))
                .catch(() => ({
                    ...target,
                    data: [],
                }));
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
                    metadata: {
                        target: data.target,
                        resolution: data.resolution,
                        date: data.date,
                        year: data.year,
                        from: data.from,
                        to: data.to,
                        interval_sec: data.interval_sec,
                        count: data.count,
                    },
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

            if (this.isDemoHistoryPeriod()) {
                const existingTargets = new Set(targets.map((target) => target.id));
                DemoHistoryTargets.forEach((demoTarget) => {
                    if (existingTargets.has(demoTarget.id)) {
                        return;
                    }

                    const color = colors[targets.length % colors.length] || '#0d6efd';
                    targets.push({
                        id: demoTarget.id,
                        label: demoTarget.label,
                        color,
                        data: [],
                    });
                });
            }

            return targets;
        },
        isDemoHistoryPeriod(): boolean {
            if (this.query.view === 'day') {
                return this.query.date.startsWith('2099-06-');
            }
            if (this.query.view === 'month') {
                return this.query.month === '2099-06';
            }
            return this.query.year === 2099;
        },
        setView(view: ViewMode) {
            this.drillDownMode = false;
            this.query.view = view;
            this.loadHistory();
        },
        canDrillDown(row: EnergyHistoryRow): boolean {
            if (this.resolution === '5m') {
                return false;
            }
            if (this.resolution === 'day' && row.day_of_year !== undefined) {
                return true;
            }
            if (this.resolution === 'month' && row.month !== undefined) {
                return true;
            }
            return false;
        },
        drillDown(row: EnergyHistoryRow) {
            if (this.resolution === 'month' && row.month !== undefined) {
                this.drillDownMode = true;
                const monthStr = String(row.month).padStart(2, '0');
                this.query.month = `${this.query.year}-${monthStr}`;
                this.query.view = 'month';
                this.loadHistory();
            } else if (this.resolution === 'day' && row.day_of_year !== undefined) {
                this.drillDownMode = true;
                const year = this.monthDayRange(this.query.month).year;
                const date = this.formatDayOfYear(year, row.day_of_year);
                this.query.date = date;
                this.query.view = 'day';
                this.loadHistory();
            }
        },
        resetToDrillDown(view: ViewMode) {
            this.drillDownMode = true;
            this.query.view = view;
            this.loadHistory();
        },
        formatMonthName(monthStr: string): string {
            const [year, month] = monthStr.split('-');
            const date = new Date(parseInt(year), parseInt(month) - 1);
            return date.toLocaleDateString(this.$i18n.locale, { month: 'long', year: 'numeric' });
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
        dailyEnergyLabel(row: EnergyHistoryRow): string {
            const range = this.currentMonthDayRange();
            if (row.day_of_year === undefined) {
                return '';
            }

            return this.formatDayOfYear(range.year, row.day_of_year).substring(8, 10);
        },
        currentMonthDayRange(): { year: number; from: number; to: number } {
            if (this.query.view === 'day') {
                return this.monthDayRange(this.query.date.substring(0, 7));
            }
            if (this.query.view === 'month') {
                return this.monthDayRange(this.query.month);
            }
            return this.monthDayRange(localMonthInputValue(new Date(this.query.year, new Date().getMonth(), 1)));
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
        cumulativeEnergyData(rows: EnergyHistoryRow[]): number[] {
            if (this.resolution === '5m') {
                return rows.map((row) => this.whToKwh(row.yield_wh || 0));
            }

            let cumulative = 0;
            return rows.map((row) => {
                cumulative += row.yield_wh || 0;
                return this.whToKwh(cumulative);
            });
        },
        whToKwh(value: number): number {
            return value / 1000;
        },
        formatKwh(value: number): string {
            return this.$n(this.whToKwh(value));
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
        calculateAveragePowerW(row: EnergyHistoryRow, rows: EnergyHistoryRow[], index: number): number {
            // Korrigierte Leistungsberechnung basierend auf tatsächlichem Zeitraum zwischen Datenpunkten
            if (index === 0) {
                return row.avg_power_w || 0;
            }

            const prevRow = rows[index - 1];
            const currentYield = row.yield_wh || 0;
            const prevYield = prevRow.yield_wh || 0;
            const yieldDelta = currentYield - prevYield;

            if (yieldDelta <= 0) {
                return 0;
            }

            // Berechne Zeit-Differenz je nach Resolution
            let timeDeltaMinutes = 5; // Default fuer 5-Minuten-Slots

            if (this.resolution === '5m' && row.slot !== undefined && prevRow.slot !== undefined) {
                // Slots sind 5-Minuten-Intervalle
                // Bei Uebergaengen ueber Tagesgrenze (von slot 287 zu slot 0 des naechsten Tages)
                let slotDelta = row.slot - prevRow.slot;
                if (slotDelta < 0) {
                    slotDelta = (288 - prevRow.slot) + row.slot; // Uebergangszaehlung
                }
                timeDeltaMinutes = slotDelta * 5;
            } else if (this.resolution === 'day' && row.day_of_year !== undefined && prevRow.day_of_year !== undefined) {
                // Tages-Unterschied (in Minuten fuer Konsistenz)
                timeDeltaMinutes = (row.day_of_year - prevRow.day_of_year) * 24 * 60;
            } else if (this.resolution === 'month' && row.month !== undefined && prevRow.month !== undefined) {
                // Monatlicher Unterschied (vereinfacht als durchschnittlich 30 Tage)
                timeDeltaMinutes = (row.month - prevRow.month) * 30 * 24 * 60;
            }

            // Berechne durchschnittliche Leistung
            // yieldDelta ist in Wh, timeDeltaMinutes ist in Minuten
            // avg_power_w = yieldDelta (Wh) / (timeDeltaMinutes / 60) (Stunden)
            // avg_power_w = yieldDelta * 60 / timeDeltaMinutes
            if (timeDeltaMinutes <= 0) {
                return 0;
            }

            const avgPowerW = (yieldDelta * 60) / timeDeltaMinutes;
            return Math.round(avgPowerW);
        },
    },
    watch: {
        'query.date'() {
            this.loadDailyEnergyHistory();
        },
        'query.month'() {
            this.loadDailyEnergyHistory();
        },
        'query.year'() {
            this.loadDailyEnergyHistory();
        },
    },
});
</script>

<style scoped>
.energy-history-chart {
    position: relative;
    min-height: 320px;
}

.energy-history-row-totals,
.energy-history-status-tiles {
    min-width: 0;
}

.energy-history-row-totals {
    flex: 1 1 42rem;
    max-width: 52rem;
    min-width: 32rem;
}

.energy-history-row-totals :deep(.row) {
    --bs-gutter-x: 0.5rem;
    --bs-gutter-y: 0.5rem;
}

.energy-history-row-totals :deep(.card-body) {
    padding: 0.45rem 0.75rem;
}

.energy-history-row-totals :deep(.card-header) {
    padding: 0.3rem 0.75rem;
    line-height: 1.15;
}

.energy-history-row-totals :deep(h2) {
    margin-bottom: 0;
    font-size: 1.25rem;
}

.energy-history-status-tile {
    height: 100%;
    min-height: 92px;
    padding: 1rem;
    border: 1px solid var(--bs-border-color);
    border-radius: var(--bs-border-radius);
    background: var(--bs-body-bg);
}

@media (min-width: 768px) and (max-width: 1199.98px) {
    .energy-history-row-totals {
        flex-basis: 28rem;
        min-width: 24rem;
    }

    .energy-history-row-totals :deep(.card-body) {
        padding: 0.28rem 0.45rem;
    }

    .energy-history-row-totals :deep(.card-header) {
        padding: 0.22rem 0.45rem;
        font-size: 0.82rem;
        line-height: 1.1;
    }

    .energy-history-row-totals :deep(h2) {
        font-size: 0.98rem;
    }
}

@media (max-width: 767.98px) {
    .energy-history-row-totals {
        flex-basis: 100%;
        min-width: 100%;
        max-width: none;
    }
}

/* Drill-down Navigation Styles */
.table tbody tr[style*="cursor"] {
    transition: background-color 0.15s ease-in-out;
}

.table tbody tr[style*="cursor"]:hover {
    background-color: rgba(13, 110, 253, 0.1);
}

.breadcrumb {
    font-size: 0.9rem;
}

.breadcrumb .btn-link {
    font-size: inherit;
    line-height: 1.5;
}
</style>
