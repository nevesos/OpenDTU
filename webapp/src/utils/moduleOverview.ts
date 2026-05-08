import type { BackgroundPath, HeatmapMode } from '@/types/ModuleOverview';

export const MODULE_OVERVIEW_LAYOUT_FILE = 'module_overview.json';

export function normalizeBackgroundPaths(paths: BackgroundPath[]): BackgroundPath[] {
    return paths
        .filter((path) => Number.isFinite(path.id) && Array.isArray(path.points) && path.points.length > 0)
        .map((path) => ({
            id: Math.trunc(path.id),
            color: typeof path.color === 'string' ? path.color : '#5b8def',
            width: Number.isFinite(path.width) ? Math.max(1, Math.trunc(path.width)) : 4,
            closed: path.closed === true,
            points: path.points
                .filter((point) => Number.isFinite(point.x) && Number.isFinite(point.y))
                .map((point) => ({
                    x: Math.max(0, Math.round(point.x)),
                    y: Math.max(0, Math.round(point.y)),
                })),
        }))
        .filter((path) => path.points.length > 0);
}

export function isValidZoomFactor(zoomFactor?: number): zoomFactor is number {
    return zoomFactor === 0.5 || zoomFactor === 0.75 || zoomFactor === 1 || zoomFactor === 1.25 || zoomFactor === 1.5;
}

export function isValidHeatmapMode(heatmapMode?: HeatmapMode): heatmapMode is HeatmapMode {
    return (
        heatmapMode === 'none' ||
        heatmapMode === 'power' ||
        heatmapMode === 'powerMax' ||
        heatmapMode === 'powerDiff' ||
        heatmapMode === 'yieldDay' ||
        heatmapMode === 'yieldDayDiff'
    );
}
