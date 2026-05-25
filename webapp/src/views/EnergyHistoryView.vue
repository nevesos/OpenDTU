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

            <form class="row g-3 align-items-end" @submit.prevent="loadHistory()">
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
                <div class="col-12 col-lg-auto">
                    <label class="form-label d-block">{{ $t('energyhistory.Navigate') }}</label>
                    <div class="energy-history-period-nav">
                        <button
                            type="button"
                            class="btn btn-outline-secondary energy-history-period-arrow"
                            :title="$t('energyhistory.PreviousPeriod')"
                            @click="previousPeriod"
                        >
                            <BIconChevronLeft />
                        </button>
                        <div class="energy-history-period-label">{{ periodNavigationLabel }}</div>
                        <button
                            type="button"
                            class="btn btn-outline-secondary energy-history-period-arrow"
                            :title="$t('energyhistory.NextPeriod')"
                            @click="nextPeriod"
                        >
                            <BIconChevronRight />
                        </button>
                        <button type="button" class="btn btn-outline-primary" @click="resetToCurrentPeriod">
                            {{ currentPeriodButtonLabel }}
                        </button>
                    </div>
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
                    v-if="!dailyEnergyLoading && dailyEnergyHistories.some((history) => history.data.length > 0)"
                    type="bar"
                    :data="dailyEnergyChartData"
                    :options="dailyEnergyChartOptions"
                    :height="260"
                />
                <div v-else-if="dailyEnergyLoading" class="text-center text-muted py-4">{{ $t('base.Loading') }}</div>
                <div v-else class="text-center text-muted py-4">{{ $t('energyhistory.NoData') }}</div>
            </div>
            <div class="mt-4">
                <div class="row g-3 align-items-end">
                    <div class="col-12 col-md-auto">
                        <button
                            type="button"
                            class="btn btn-outline-primary"
                            :disabled="monthlyComparisonLoading"
                            @click="loadMonthlyComparison"
                        >
                            <BIconSearch class="me-1" />
                            {{ monthlyComparisonLoading ? $t('base.Loading') : $t('energyhistory.LoadMonthlyComparison') }}
                        </button>
                    </div>
                </div>
                <div class="energy-history-chart mt-3">
                    <ChartComponent
                        v-if="monthlyComparisonRequested && !monthlyComparisonLoading && monthlyComparisonHasData"
                        type="bar"
                        :data="monthlyComparisonChartData"
                        :options="monthlyComparisonChartOptions"
                        :height="260"
                    />
                    <div v-else-if="monthlyComparisonLoading" class="text-center text-muted py-4">{{ $t('base.Loading') }}</div>
                    <div v-else-if="monthlyComparisonRequested" class="text-center text-muted py-4">{{ $t('energyhistory.NoData') }}</div>
                </div>
                <div class="energy-history-chart mt-4">
                    <ChartComponent
                        v-if="monthlyComparisonRequested && !monthlyComparisonLoading && yearlyComparisonHasData"
                        type="bar"
                        :data="yearlyComparisonChartData"
                        :options="yearlyComparisonChartOptions"
                        :height="220"
                    />
                </div>
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

        <EnergyHistoryImportExport @changed="reloadAfterFileChange" />
    </BasePage>
</template>

<script lang="ts">
import BasePage from '@/components/BasePage.vue';
import CardElement from '@/components/CardElement.vue';
import EnergyHistoryImportExport from '@/components/EnergyHistoryImportExport.vue';
import InverterTotalInfo from '@/components/InverterTotalInfo.vue';
import type { Total } from '@/types/LiveDataStatus';
import { authHeader, handleResponse } from '@/utils/authentication';
import { BIconChevronLeft, BIconChevronRight, BIconSearch } from 'bootstrap-icons-vue';
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
    type ActiveElement,
    type ChartEvent,
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
    bytes_scanned?: number;
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

interface EnergyHistoryFile {
    path: string;
    size?: number;
}

interface EnergyHistoryFileListResponse {
    files?: EnergyHistoryFile[];
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

const HistoryAutoRefreshIntervalMs = 10000;
const MaxHistoryFileTargets = 32;

export default defineComponent({
    components: {
        BasePage,
        CardElement,
        EnergyHistoryImportExport,
        InverterTotalInfo,
        BIconChevronLeft,
        BIconChevronRight,
        BIconSearch,
        ChartComponent,
    },
    data() {
        return {
            dataLoading: true,
            historyLoading: false,
            historyLoadId: 0,
            dailyEnergyLoading: false,
            dailyEnergyLoadId: 0,
            monthlyComparisonRequested: false,
            monthlyComparisonLoading: false,
            monthlyComparisonLoadId: 0,
            monthlyComparisonHistories: [] as EnergyHistorySeries[],
            monthlyComparisonCache: {} as Record<string, EnergyHistoryRow[]>,
            status: {} as EnergyHistoryStatus,
            lastHistoryStatusSignature: '',
            historyAutoRefreshTimer: undefined as number | undefined,
            liveTotal: null as Total | null,
            inverters: [] as InverterConfig[],
            historyFiles: [] as EnergyHistoryFile[],
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
        chartHasData(): boolean {
            return this.histories.some((history) => history.data.length > 0);
        },
        monthlyComparisonHasData(): boolean {
            return this.monthlyComparisonHistories.some((history) => history.data.length > 0);
        },
        yearlyComparisonHasData(): boolean {
            return this.monthlyComparisonHistories.some((history) => this.yearlyComparisonValue(history.data) > 0);
        },
        periodNavigationLabel(): string {
            if (this.query.view === 'day') {
                return this.parseDateInput(this.query.date).toLocaleDateString(this.$i18n.locale);
            }
            if (this.query.view === 'month') {
                return this.formatMonthName(this.query.month);
            }
            return String(this.query.year);
        },
        currentPeriodButtonLabel(): string {
            if (this.query.view === 'day') {
                return this.$t('energyhistory.Today');
            }
            if (this.query.view === 'month') {
                return this.$t('energyhistory.CurrentMonth');
            }
            return this.$t('energyhistory.CurrentYear');
        },
        showDrillDownBreadcrumb(): boolean {
            return this.drillDownMode && this.query.view !== 'year';
        },
        statusTiles(): { label: string; value: string; detail?: string }[] {
            return [
                {
                    label: this.$t('energyhistory.HistoryFiles'),
                    value: this.$n(statusValue(this.status.files_scanned)),
                },
                {
                    label: this.$t('energyhistory.HistoryFileSize'),
                    value: this.formatBytes(statusValue(this.status.bytes_scanned)),
                },
                {
                    label: this.$t('energyhistory.LittleFsUsed'),
                    value: this.formatBytes(statusValue(this.status.littlefs_used)),
                    detail: this.formatBytes(statusValue(this.status.littlefs_total)),
                },
            ];
        },
        chartData(): ChartData<'bar' | 'line', Array<number | null>, string> {
            const totalHistory = this.histories.find((history) => history.id === 'total');
            const inverterHistories = this.histories.filter((history) => history.id !== 'total' && history.data.length > 0);
            const fiveMinuteSlots = this.resolution === '5m' ? this.fiveMinuteChartSlots(this.histories) : [];
            const reference = totalHistory && totalHistory.data.length > 0
                ? totalHistory
                : this.histories.find((history) => history.data.length > 0);
            const labels = this.resolution === '5m'
                ? fiveMinuteSlots.map((slot) => this.formatSlotTime(slot))
                : reference?.data.map((row) => this.firstColumnValue(row).toString()) || [];
            const powerKey = this.resolution === '5m' ? 'avg_power_w' : 'max_power_w';
            const totalPowerDataset = totalHistory && totalHistory.data.length > 0
                ? [{
                    type: 'line' as const,
                    label: `${this.$t('energyhistory.Total')} ${this.resolution === '5m'
                        ? this.$t('energyhistory.AvgPowerW')
                        : this.$t('energyhistory.MaxPowerW')}`,
                    data: this.resolution === '5m'
                        ? this.fiveMinutePowerData(totalHistory, fiveMinuteSlots)
                        : totalHistory.data.map((row) => row[powerKey] || 0),
                    borderColor: '#212529',
                    backgroundColor: 'transparent',
                    borderWidth: 3,
                    fill: false,
                    spanGaps: false,
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
                data: this.resolution === '5m'
                    ? this.fiveMinutePowerData(history, fiveMinuteSlots)
                    : history.data.map((row) => row[powerKey] || 0),
                borderColor: history.color,
                backgroundColor: this.withAlpha(history.color, 0.18),
                borderWidth: 2,
                fill: true,
                spanGaps: false,
                tension: 0.25,
                pointRadius: this.resolution === '5m' ? 0 : 2,
                yAxisID: 'power',
            }));
            const energyDataset = totalHistory && totalHistory.data.length > 0
                ? [{
                    type: 'line' as const,
                    label: this.$t('energyhistory.CumulativeEnergyKwh'),
                    data: this.resolution === '5m'
                        ? this.fiveMinuteEnergyData(totalHistory, fiveMinuteSlots)
                        : this.cumulativeEnergyData(totalHistory.data),
                    borderColor: totalHistory.color,
                    backgroundColor: 'transparent',
                    borderWidth: 3,
                    fill: false,
                    spanGaps: false,
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
                onClick: (_event: ChartEvent, elements: ActiveElement[]) => {
                    const element = elements[0];
                    if (element && this.resolution === 'day') {
                        this.openHistoryChartDay(element.index);
                    }
                },
                onHover: (event: ChartEvent, elements: ActiveElement[]) => {
                    if (event.native?.target instanceof HTMLElement) {
                        event.native.target.style.cursor = this.resolution === 'day' && elements.length > 0 ? 'pointer' : '';
                    }
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
                onClick: (_event: ChartEvent, elements: ActiveElement[]) => {
                    const element = elements[0];
                    if (element) {
                        this.openDailyEnergyDay(element.datasetIndex, element.index);
                    }
                },
                onHover: (event: ChartEvent, elements: ActiveElement[]) => {
                    if (event.native?.target instanceof HTMLElement) {
                        event.native.target.style.cursor = elements.length > 0 ? 'pointer' : '';
                    }
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
        monthlyComparisonChartData(): ChartData<'bar', Array<number | null>, string> {
            return {
                labels: this.monthLabels(),
                datasets: this.monthlyComparisonHistories.map((history) => ({
                    type: 'bar' as const,
                    label: history.label,
                    data: this.monthlyComparisonValues(history.data),
                    borderColor: history.color,
                    backgroundColor: this.withAlpha(history.color, 0.7),
                    borderWidth: 1,
                })),
            };
        },
        monthlyComparisonChartOptions(): ChartOptions<'bar'> {
            return {
                responsive: true,
                maintainAspectRatio: false,
                interaction: {
                    intersect: false,
                    mode: 'index',
                },
                onClick: (_event: ChartEvent, elements: ActiveElement[]) => {
                    const element = elements[0];
                    if (element) {
                        this.openMonthlyComparisonMonth(element.datasetIndex, element.index);
                    }
                },
                onHover: (event: ChartEvent, elements: ActiveElement[]) => {
                    if (event.native?.target instanceof HTMLElement) {
                        event.native.target.style.cursor = elements.length > 0 ? 'pointer' : '';
                    }
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
                        },
                    },
                },
                scales: {
                    x: {
                        ticks: {
                            maxRotation: 0,
                            autoSkip: false,
                        },
                        grid: {
                            display: false,
                        },
                    },
                    y: {
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: this.$t('energyhistory.MonthlyEnergyKwh'),
                        },
                    },
                },
            };
        },
        yearlyComparisonChartData(): ChartData<'bar', number[], string> {
            return {
                labels: this.monthlyComparisonHistories.map((history) => history.label),
                datasets: [{
                    type: 'bar' as const,
                    label: this.$t('energyhistory.YearlyEnergyKwh'),
                    data: this.monthlyComparisonHistories.map((history) => this.yearlyComparisonValue(history.data)),
                    borderColor: '#198754',
                    backgroundColor: this.withAlpha('#198754', 0.7),
                    borderWidth: 1,
                }],
            };
        },
        yearlyComparisonChartOptions(): ChartOptions<'bar'> {
            return {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        display: true,
                        position: 'top',
                    },
                    tooltip: {
                        enabled: true,
                        callbacks: {
                            label: (item) => {
                                const value = typeof item.parsed.y === 'number' ? item.parsed.y : 0;
                                return `${item.dataset.label || ''}: ${this.$n(value)} kWh`;
                            },
                        },
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
                    y: {
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: this.$t('energyhistory.YearlyEnergyKwh'),
                        },
                    },
                },
            };
        },
    },
    created() {
        this.reloadAll();
        this.startHistoryAutoRefresh();
    },
    beforeUnmount() {
        this.stopHistoryAutoRefresh();
    },
    methods: {
        reloadAll() {
            this.dataLoading = true;
            this.loadStatus().catch(() => {
                this.status = {};
            });
            this.loadLiveTotal();
            Promise.all([this.loadInverters(), this.loadHistoryFileList()])
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
                    this.lastHistoryStatusSignature = this.historyStatusSignature(data);
                });
        },
        startHistoryAutoRefresh() {
            this.stopHistoryAutoRefresh();
            this.historyAutoRefreshTimer = window.setInterval(() => {
                this.refreshHistoryIfChanged();
            }, HistoryAutoRefreshIntervalMs);
        },
        stopHistoryAutoRefresh() {
            if (this.historyAutoRefreshTimer !== undefined) {
                window.clearInterval(this.historyAutoRefreshTimer);
                this.historyAutoRefreshTimer = undefined;
            }
        },
        refreshHistoryIfChanged() {
            if (this.historyLoading || this.dailyEnergyLoading || this.monthlyComparisonLoading) {
                return;
            }

            return fetch('/api/energy/history/status', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router, true))
                .then((data: EnergyHistoryStatus) => {
                    const signature = this.historyStatusSignature(data);
                    const changed = this.lastHistoryStatusSignature !== '' && signature !== this.lastHistoryStatusSignature;
                    this.status = data;
                    this.lastHistoryStatusSignature = signature;
                    if (changed) {
                        this.loadHistoryFileList()
                            .then(() => this.loadHistory());
                    }
                })
                .catch(() => undefined);
        },
        historyStatusSignature(status: EnergyHistoryStatus): string {
            return [
                status.files_scanned || 0,
                status.bytes_scanned || 0,
                status.littlefs_used || 0,
            ].join(':');
        },
        reloadAfterFileChange() {
            this.loadStatus();
            this.loadHistoryFileList()
                .then(() => this.loadHistory());
            this.clearMonthlyComparison();
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
        loadHistoryFileList(): Promise<void> {
            return fetch('/api/energy/history/file/list', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router, true))
                .then((data: EnergyHistoryFileListResponse) => {
                    this.historyFiles = data.files || [];
                })
                .catch(() => {
                    this.historyFiles = [];
                });
        },
        loadHistory(loadDailyEnergy = true) {
            const loadId = ++this.historyLoadId;
            this.historyLoading = true;
            const targets = this.historyTargets();
            this.histories = targets.map((target) => ({
                ...target,
                data: [],
            }));
            if (loadDailyEnergy) {
                this.dailyEnergyHistories = [];
                this.dailyEnergyLoadId++;
                this.dailyEnergyLoading = true;
            }

            const requests = targets.map((target, index) => this.loadTargetHistory(target)
                .then((histories) => {
                    if (loadId !== this.historyLoadId) {
                        return histories;
                    }

                    const nextHistories = this.histories.slice();
                    nextHistories[index] = histories;
                    this.histories = nextHistories;
                    return histories;
                }));

            Promise.all(requests)
                .then(() => {
                    if (loadId !== this.historyLoadId) {
                        return;
                    }

                    if (loadDailyEnergy) {
                        this.loadDailyEnergyHistory();
                    }
                });

            return (requests[0] || Promise.resolve())
                .then(() => undefined)
                .finally(() => {
                    if (loadId === this.historyLoadId) {
                        this.historyLoading = false;
                    }
                });
        },
        loadDailyEnergyHistory(): Promise<void> {
            const loadId = ++this.dailyEnergyLoadId;
            const range = this.currentMonthDayRange();
            const targets = this.historyTargets().filter((target) => target.id !== 'total');
            this.dailyEnergyLoading = true;

            return Promise.all(targets.map((target) => this.loadDailyEnergyTargetHistory(target, range)))
                .then((histories) => {
                    if (loadId === this.dailyEnergyLoadId) {
                        this.dailyEnergyHistories = histories;
                    }
                })
                .catch(() => {
                    if (loadId === this.dailyEnergyLoadId) {
                        this.dailyEnergyHistories = [];
                    }
                })
                .finally(() => {
                    if (loadId === this.dailyEnergyLoadId) {
                        this.dailyEnergyLoading = false;
                    }
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
        openHistoryChartDay(dataIndex: number) {
            if (this.resolution !== 'day') {
                return;
            }

            const reference = this.histories.find((history) => history.id === 'total' && history.data.length > 0)
                    || this.histories.find((history) => history.data.length > 0);
            const row = reference?.data[dataIndex];
            if (!row || row.day_of_year === undefined) {
                return;
            }

            const range = this.monthDayRange(this.query.month);
            this.openDay(range.year, row.day_of_year);
        },
        openDailyEnergyDay(datasetIndex: number, dataIndex: number) {
            const histories = this.dailyEnergyHistories.filter((history) => history.data.length > 0);
            const clickedRow = histories[datasetIndex]?.data[dataIndex]
                    || histories.find((history) => history.data[dataIndex])?.data[dataIndex];
            if (!clickedRow || clickedRow.day_of_year === undefined) {
                return;
            }

            const range = this.currentMonthDayRange();
            this.openDay(range.year, clickedRow.day_of_year);
        },
        openDay(year: number, dayOfYear: number) {
            this.drillDownMode = true;
            this.query.view = 'day';
            this.query.date = this.formatDayOfYear(year, dayOfYear);
            this.loadHistory(false);
        },
        openMonthlyComparisonMonth(datasetIndex: number, dataIndex: number) {
            const history = this.monthlyComparisonHistories[datasetIndex];
            const year = Number(history?.label);
            const month = dataIndex + 1;
            if (!history || !Number.isFinite(year) || month < 1 || month > 12) {
                return;
            }

            const hasMonthData = history.data.some((row) => row.month === month);
            if (!hasMonthData) {
                return;
            }

            this.drillDownMode = true;
            this.query.view = 'month';
            this.query.month = `${String(year).padStart(4, '0')}-${String(month).padStart(2, '0')}`;
            this.loadHistory();
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
        loadMonthlyComparison(): Promise<void> {
            const loadId = ++this.monthlyComparisonLoadId;
            const colors = ['#198754', '#0d6efd', '#dc3545', '#fd7e14', '#6f42c1'];
            this.monthlyComparisonRequested = true;
            this.monthlyComparisonLoading = true;
            this.monthlyComparisonHistories = [];

            return this.loadAvailableMonthlyComparisonYears()
                .then((years) => {
                    if (loadId !== this.monthlyComparisonLoadId) {
                        return [];
                    }

                    this.monthlyComparisonHistories = years.map((year, index) => ({
                        id: `total:${year}`,
                        label: String(year),
                        color: colors[index % colors.length] || '#0d6efd',
                        data: [],
                    }));

                    return Promise.all(years.map((year, index) => this.loadMonthlyComparisonYear(year)
                        .then((rows) => ({
                            id: `total:${year}`,
                            label: String(year),
                            color: colors[index % colors.length] || '#0d6efd',
                            data: rows,
                        }))));
                })
                .then((histories) => {
                    if (loadId === this.monthlyComparisonLoadId) {
                        this.monthlyComparisonHistories = histories;
                    }
                })
                .catch(() => {
                    if (loadId === this.monthlyComparisonLoadId) {
                        this.monthlyComparisonHistories = [];
                    }
                })
                .finally(() => {
                    if (loadId === this.monthlyComparisonLoadId) {
                        this.monthlyComparisonLoading = false;
                    }
                });
        },
        loadAvailableMonthlyComparisonYears(): Promise<number[]> {
            return fetch('/api/energy/history/file/list', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router, true))
                .then((data: EnergyHistoryFileListResponse) => {
                    const years = new Set<number>();
                    (data.files || []).forEach((file) => {
                        const match = file.path.match(/^\/energy\/month\/total_(\d{4})\.ehm$/);
                        if (!match) {
                            return;
                        }

                        const year = Number(match[1]);
                        if (Number.isFinite(year)) {
                            years.add(year);
                        }
                    });

                    return Array.from(years).sort((a, b) => b - a);
                })
                .catch(() => []);
        },
        loadMonthlyComparisonYear(year: number): Promise<EnergyHistoryRow[]> {
            const cacheKey = `total:${year}`;
            if (this.monthlyComparisonCache[cacheKey]) {
                return Promise.resolve(this.monthlyComparisonCache[cacheKey]);
            }

            const params = new URLSearchParams();
            params.set('resolution', 'month');
            params.set('target', 'total');
            params.set('year', String(year));
            params.set('from', '1');
            params.set('to', '12');

            return fetch('/api/energy/history?' + params.toString(), { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router, true))
                .then((data: EnergyHistoryResponse) => {
                    const rows = data.data || [];
                    this.monthlyComparisonCache[cacheKey] = rows;
                    return rows;
                })
                .catch(() => []);
        },
        clearMonthlyComparison() {
            this.monthlyComparisonLoadId++;
            this.monthlyComparisonRequested = false;
            this.monthlyComparisonLoading = false;
            this.monthlyComparisonHistories = [];
            this.monthlyComparisonCache = {};
        },
        hideMonthlyComparison() {
            this.monthlyComparisonLoadId++;
            this.monthlyComparisonRequested = false;
            this.monthlyComparisonLoading = false;
            this.monthlyComparisonHistories = [];
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

            this.historyFileTargetsForCurrentPeriod().forEach((targetId) => {
                if (targets.some((target) => target.id === targetId)) {
                    return;
                }

                const color = colors[targets.length % colors.length] || '#0d6efd';
                targets.push({
                    id: targetId,
                    label: this.historyFileTargetLabel(targetId),
                    color,
                    data: [],
                });
            });

            return targets;
        },
        historyFileTargetsForCurrentPeriod(): string[] {
            const targets = new Set<string>();
            let pattern: RegExp | null = null;

            if (this.query.view === 'day') {
                const date = this.parseDateInput(this.query.date);
                pattern = new RegExp(`^/energy/5m/(inv_\\d+)_${date.getFullYear()}_${String(date.getMonth() + 1).padStart(2, '0')}\\.eh5$`);
            } else if (this.query.view === 'month') {
                const date = this.parseMonthInput(this.query.month);
                pattern = new RegExp(`^/energy/day/(inv_\\d+)_${date.getFullYear()}\\.ehd$`);
            } else {
                pattern = new RegExp(`^/energy/month/(inv_\\d+)_${this.query.year}\\.ehm$`);
            }

            this.historyFiles.forEach((file) => {
                const match = file.path.match(pattern as RegExp);
                if (match?.[1]) {
                    targets.add(match[1]);
                }
            });

            return Array.from(targets)
                .sort((a, b) => a.localeCompare(b))
                .slice(0, MaxHistoryFileTargets);
        },
        historyFileTargetLabel(targetId: string): string {
            const serial = targetId.startsWith('inv_') ? targetId.substring(4) : targetId;
            const configured = this.inverters.find((inverter) => this.hexSerialToDecimal(inverter.serial) === serial);
            return configured?.name || serial;
        },
        setView(view: ViewMode) {
            this.drillDownMode = false;
            this.query.view = view;
            this.hideMonthlyComparison();
            this.loadHistory();
        },
        previousPeriod() {
            this.shiftPeriod(-1);
        },
        nextPeriod() {
            this.shiftPeriod(1);
        },
        shiftPeriod(direction: number) {
            this.drillDownMode = false;
            if (this.query.view === 'day') {
                const date = this.parseDateInput(this.query.date);
                date.setDate(date.getDate() + direction);
                this.query.date = localDateInputValue(date);
            } else if (this.query.view === 'month') {
                const date = this.parseMonthInput(this.query.month);
                date.setMonth(date.getMonth() + direction);
                this.query.month = localMonthInputValue(date);
            } else {
                this.query.year += direction;
            }
            this.hideMonthlyComparison();
            this.loadHistory();
        },
        resetToCurrentPeriod() {
            const now = new Date();
            this.drillDownMode = false;
            if (this.query.view === 'day') {
                this.query.date = localDateInputValue(now);
            } else if (this.query.view === 'month') {
                this.query.month = localMonthInputValue(now);
            } else {
                this.query.year = now.getFullYear();
            }
            this.hideMonthlyComparison();
            this.loadHistory();
        },
        resetToDrillDown(view: ViewMode) {
            this.drillDownMode = true;
            this.query.view = view;
            this.hideMonthlyComparison();
            this.loadHistory();
        },
        formatMonthName(monthStr: string): string {
            const date = this.parseMonthInput(monthStr);
            return date.toLocaleDateString(this.$i18n.locale, { month: 'long', year: 'numeric' });
        },
        parseDateInput(value: string): Date {
            const parts = value.split('-');
            const year = Number(parts[0]);
            const month = Number(parts[1]);
            const day = Number(parts[2]);
            if (!Number.isFinite(year) || !Number.isFinite(month) || !Number.isFinite(day)) {
                return new Date();
            }
            return new Date(year, month - 1, day);
        },
        parseMonthInput(value: string): Date {
            const parts = value.split('-');
            const year = Number(parts[0]);
            const month = Number(parts[1]);
            if (!Number.isFinite(year) || !Number.isFinite(month)) {
                return new Date(new Date().getFullYear(), new Date().getMonth(), 1);
            }
            return new Date(year, month - 1, 1);
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
        monthLabels(): string[] {
            return Array.from({ length: 12 }, (_, index) => {
                const date = new Date(2000, index, 1);
                return date.toLocaleDateString(this.$i18n.locale, { month: 'short' });
            });
        },
        monthlyComparisonValues(rows: EnergyHistoryRow[]): Array<number | null> {
            const byMonth = new Map<number, EnergyHistoryRow>();
            rows.forEach((row) => {
                if (row.month !== undefined) {
                    byMonth.set(row.month, row);
                }
            });

            return Array.from({ length: 12 }, (_, index) => {
                const row = byMonth.get(index + 1);
                return row ? this.whToKwh(row.yield_wh || 0) : null;
            });
        },
        yearlyComparisonValue(rows: EnergyHistoryRow[]): number {
            return rows.reduce((sum, row) => sum + this.whToKwh(row.yield_wh || 0), 0);
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
            const today = new Date();
            const last =
                year === today.getFullYear() && month === today.getMonth() + 1
                    ? new Date(Date.UTC(year, month - 1, today.getDate()))
                    : new Date(Date.UTC(year, month, 0));
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
        fiveMinuteChartSlots(histories: EnergyHistorySeries[]): number[] {
            const presentSlots = histories
                .flatMap((history) => history.data)
                .map((row) => row.slot)
                .filter((slot): slot is number => slot !== undefined);
            if (presentSlots.length === 0) {
                return [];
            }

            const firstSlot = Math.max(0, Math.min(...presentSlots));
            const lastSlot = Math.min(287, Math.max(...presentSlots));
            const slots: number[] = [];
            for (let slot = firstSlot; slot <= lastSlot; slot++) {
                slots.push(slot);
            }
            return slots;
        },
        fiveMinutePowerData(history: EnergyHistorySeries, slots: number[]): Array<number | null> {
            const bySlot = this.rowsBySlot(history.data);
            return slots.map((slot) => {
                const entry = bySlot.get(slot);
                return entry ? this.calculateAveragePowerW(entry.row, history.data, entry.index) : null;
            });
        },
        fiveMinuteEnergyData(history: EnergyHistorySeries, slots: number[]): Array<number | null> {
            const bySlot = this.rowsBySlot(history.data);
            return slots.map((slot) => {
                const entry = bySlot.get(slot);
                return entry ? this.whToKwh(entry.row.yield_wh || 0) : null;
            });
        },
        rowsBySlot(rows: EnergyHistoryRow[]): Map<number, { row: EnergyHistoryRow; index: number }> {
            const result = new Map<number, { row: EnergyHistoryRow; index: number }>();
            rows.forEach((row, index) => {
                if (row.slot !== undefined) {
                    result.set(row.slot, { row, index });
                }
            });
            return result;
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
            if (index === 0) {
                return row.avg_power_w || 0;
            }

            const currentYield = row.yield_wh || 0;
            let prevRow = rows[index - 1];

            if (this.resolution === '5m' && currentYield > 0) {
                for (let i = index - 1; i >= 0; i--) {
                    const candidate = rows[i];
                    if (!candidate || candidate.slot === undefined) {
                        continue;
                    }

                    const candidateYield = candidate.yield_wh || 0;
                    if (candidateYield > 0 && candidateYield < currentYield) {
                        prevRow = candidate;
                        break;
                    }
                }
            }

            if (!prevRow) {
                return row.avg_power_w || 0;
            }

            const prevYield = prevRow.yield_wh || 0;
            const yieldDelta = currentYield - prevYield;

            if (yieldDelta <= 0) {
                return 0;
            }

            let timeDeltaMinutes = 5;

            if (this.resolution === '5m' && row.slot !== undefined && prevRow.slot !== undefined) {
                let slotDelta = row.slot - prevRow.slot;
                if (slotDelta < 0) {
                    slotDelta = (288 - prevRow.slot) + row.slot;
                }
                timeDeltaMinutes = slotDelta * 5;
            } else if (this.resolution === 'day' && row.day_of_year !== undefined && prevRow.day_of_year !== undefined) {
                timeDeltaMinutes = (row.day_of_year - prevRow.day_of_year) * 24 * 60;
            } else if (this.resolution === 'month' && row.month !== undefined && prevRow.month !== undefined) {
                timeDeltaMinutes = (row.month - prevRow.month) * 30 * 24 * 60;
            }

            if (timeDeltaMinutes <= 0) {
                return 0;
            }

            const avgPowerW = (yieldDelta * 60) / timeDeltaMinutes;
            return Math.round(avgPowerW);
        },
    },
});
</script>

<style scoped>
.energy-history-chart {
    position: relative;
    min-height: 320px;
}

.energy-history-period-nav {
    display: grid;
    grid-template-columns: 2.4rem minmax(9rem, 1fr) 2.4rem auto;
    gap: 0.5rem;
    align-items: center;
    min-width: min(100%, 24rem);
}

.energy-history-period-arrow {
    width: 2.4rem;
    height: 2.4rem;
    padding: 0;
    display: inline-flex;
    align-items: center;
    justify-content: center;
}

.energy-history-period-label {
    min-height: 2.4rem;
    padding: 0.375rem 0.75rem;
    display: flex;
    align-items: center;
    justify-content: center;
    border: 1px solid var(--bs-border-color);
    border-radius: var(--bs-border-radius);
    background: var(--bs-body-bg);
    font-weight: 600;
    white-space: nowrap;
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

    .energy-history-period-nav {
        grid-template-columns: 2.4rem minmax(0, 1fr) 2.4rem;
    }

    .energy-history-period-nav .btn-outline-primary {
        grid-column: 1 / -1;
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
