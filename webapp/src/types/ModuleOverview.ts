import type { ValueObject } from './LiveDataStatus';

export interface ModulePosition {
    x: number;
    y: number;
}

export interface DrawingPoint {
    x: number;
    y: number;
}

export interface BackgroundPath {
    id: number;
    color: string;
    width: number;
    points: DrawingPoint[];
    closed: boolean;
}

export interface BackgroundControlPoint extends DrawingPoint {
    pathId: number;
    pointIndex: number;
}

export interface BackgroundPreviewLine {
    x1: number;
    y1: number;
    x2: number;
    y2: number;
    color: string;
    width: number;
}

export type HeatmapMode = 'none' | 'power' | 'powerMax' | 'powerDiff' | 'yieldDay' | 'yieldDayDiff';

export interface ModuleOverviewLayout {
    version: number;
    zoomFactor?: number;
    heatmapMode?: HeatmapMode;
    showDisabledModules?: boolean;
    modules?: Array<{
        key: string;
        x: number;
        y: number;
    }>;
    backgroundPaths?: BackgroundPath[];
}

export interface ModuleItem {
    key: string;
    inverterName: string;
    serial: string;
    channel: number;
    pollEnabled: boolean;
    reachable: boolean;
    producing: boolean;
    powerMaximum: number;
    Power?: ValueObject;
    Voltage?: ValueObject;
    Current?: ValueObject;
    YieldDay?: ValueObject;
}
