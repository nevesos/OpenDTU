# Feature_UIModuleOverview Arbeitsnotizen

Stand: 2026-05-08

## Ziel

OpenDTU erhaelt eine zweite Live-Ansicht fuer eine virtuelle Moduluebersicht. Die Ansicht zeigt Module als frei platzierbare Karten mit aktuellen Livewerten, Statusfarben, Heatmap und optionaler SVG-Hintergrundzeichnung.

Aktueller Arbeitsbranch:

```text
feature/module-overview
```

Der letzte Stand aus `feature/module-overview-pr` wurde fuer die eigentlichen Feature-Dateien beibehalten. Die Notes-Datei aus `nevesos/module-overview-notes` bleibt zusaetzlich erhalten.

Leitlinie bleibt: moeglichst wenig Firmware-Aenderungen, bestehende Live-Daten und bestehende Datei-API weiterverwenden.

## Architektur

Die Moduluebersicht nutzt dieselben Live-Daten wie die bestehende Live-Ansicht:

- Initialer Abruf: `GET /api/livedata/status`
- Live-Updates: WebSocket `/livedata`
- Modul-/Stringdaten: `liveData.inverters[].DC`

Relevante Webapp-Dateien:

- `webapp/src/views/ModuleOverviewView.vue`
- `webapp/src/components/ModuleCard.vue`
- `webapp/src/types/ModuleOverview.ts`
- `webapp/src/utils/moduleOverview.ts`
- `webapp/src/router/index.ts`
- `webapp/src/components/NavBar.vue`
- `webapp/src/locales/de.json`
- `webapp/src/locales/en.json`
- `webapp/src/locales/fr.json`

Firmware-Aenderung:

- `src/WebApi_file.cpp`
  - erlaubt lesenden Zugriff auf `/module_overview.json` ueber `WebApi.checkCredentialsReadonly(...)`
  - alle anderen Dateien bleiben wie bisher hinter `WebApi.checkCredentials(...)`

Nicht geaendert:

- `src/WebApi_ws_live.cpp`
- `src/WebApi_inverter.cpp`
- `src/Configuration.cpp`
- `include/Configuration.h`
- `webapp/src/views/HomeView.vue`

## UI-Funktionsstand

Die Ansicht ist unter `/module-overview` erreichbar und im Menue verlinkt.

Aktuelle Funktionen:

- Normalmodus fuer Live-Anzeige
- Editiermodus fuer Layoutbearbeitung
- freie Modulplatzierung per Pointer-Events
- 16px-Platzierungsraster beim Draggen und automatischen Anordnen
- automatische kollisionsarme Startpositionen fuer neue Module
- expliziter Speicherbutton, kein automatisches Speichern beim Draggen
- Anzeige deaktivierter Module per Schalter `Deaktivierte anzeigen`
- Zoom-Auswahl: 50 %, 75 %, 100 %, 125 %, 150 %
- SVG-Hintergrundeditor im Editiermodus
- Heatmap-Modi fuer Leistung, Leistung/Maximum, Leistungsdifferenz, Tagesertrag und Tagesertragsdifferenz
- Statusuebersicht mit Modulanzahl, produzierenden Modulen und Offline-Modulen
- Wiederverwendung von `InverterTotalInfo` fuer Gesamtwerte

Die Modulkarte ist nach `webapp/src/components/ModuleCard.vue` ausgelagert. Sie zeigt:

- Wechselrichtername
- Kanalnummer
- aktuelle Leistung
- Spannung
- Strom
- Tagesertrag
- Debug-ID / Modul-Key

Der Mouseover-Tooltip enthaelt Debugdetails:

- key
- serial
- inverter
- channel
- x/y
- poll_enabled
- reachable
- producing
- powerMaximum
- Power / Voltage / Current / YieldDay als Rohwerte

Statusdarstellung:

- disabled: secondary
- offline: danger
- idle: warning
- producing: success

## Datenmodell

Der Modul-Key wird so gebildet:

```text
${serial}:DC:${channel}
```

Die Module werden aus `inverters[].DC` erzeugt. Wichtiges Finding:

- `GET /api/livedata/status` kann ohne `DC`-Daten kommen.
- `DC`-Daten koennen erst ueber WebSocket eintreffen.
- `DC` liegt in Live-Daten als Objekt mit numerischen String-Keys vor, z. B. `"0"`, `"1"`, `"2"`, `"3"`, nicht zwingend als echtes Array.
- Die View nutzt deshalb `Object.entries(inverter.DC || {})`.
- WebSocket-Updates werden in `handleMessage(...)` in `liveData.total`, `liveData.hints` und den passenden Wechselrichter in `liveData.inverters` gemerged.

Gemeinsame Typen liegen in `webapp/src/types/ModuleOverview.ts`:

- `ModulePosition`
- `DrawingPoint`
- `BackgroundPath`
- `BackgroundControlPoint`
- `BackgroundPreviewLine`
- `HeatmapMode`
- `ModuleOverviewLayout`
- `ModuleItem`

Gemeinsame Helfer liegen in `webapp/src/utils/moduleOverview.ts`:

- `MODULE_OVERVIEW_LAYOUT_FILE`
- `normalizeBackgroundPaths(...)`
- `isValidZoomFactor(...)`
- `isValidHeatmapMode(...)`

## Persistenz

Die Persistenz nutzt die bestehende Datei-API:

- Layout laden: `GET /api/file/get?file=module_overview.json`
- Layout speichern: `POST /api/file/upload?file=module_overview.json`

Aktueller API-Stand:

- `GET /api/file/get?file=module_overview.json`
  - liefert `404`, wenn die Datei noch nicht existiert
  - nutzt fuer diese Datei `WebApi.checkCredentialsReadonly(...)`
  - alle anderen Dateien bleiben hinter Schreib-/Admin-Credentials
- `POST /api/file/upload?file=module_overview.json`
  - nutzt weiterhin `WebApi.checkCredentials(...)`
  - schreibt nach LittleFS
  - triggert danach weiterhin `RestartHelper.triggerRestart()`

Konsequenz:

- Positionen werden waehrend des Editierens nur im Vue-State gehalten.
- Es gibt keine Speicherung beim Draggen.
- Gespeichert wird nur bei explizitem Klick auf `Speichern`.
- Der Neustart nach Speichern ist akzeptiert.

Gespeichert werden:

- `version`
- `zoomFactor`
- `heatmapMode`
- `showDisabledModules`
- Modulpositionen mit `key`, `x`, `y`
- strukturierte SVG-Hintergrundpfade als `backgroundPaths`

Nicht gespeichert werden:

- Livewerte
- Editiermodus
- Vorschau-Linie
- aktive Griffpunkte

Aktuelle Layout-JSON:

```json
{
  "version": 1,
  "zoomFactor": 1,
  "heatmapMode": "none",
  "showDisabledModules": false,
  "modules": [
    {
      "key": "123456789012:DC:0",
      "x": 120,
      "y": 80
    }
  ],
  "backgroundPaths": [
    {
      "id": 1,
      "color": "#5b8def",
      "width": 4,
      "closed": false,
      "points": [
        { "x": 32, "y": 32 },
        { "x": 180, "y": 96 }
      ]
    }
  ]
}
```

Eine separate SVG-Datei wird aktuell nicht geladen oder gespeichert. Der SVG-Hintergrund wird strukturiert als `backgroundPaths` in `module_overview.json` persistiert.

## SVG-Hintergrund

Die Moduluebersicht hat im Editiermodus eine SVG-Hintergrundebene unter den Modulkarten.

Aktueller Funktionsumfang:

- Button `Zeichnen` aktiviert/deaktiviert den Zeichenmodus.
- Punktbasiertes Zeichnen statt Freihand.
- Klick auf freie Flaeche setzt einen Punkt.
- Punkte werden per gerader Linie verbunden.
- Beim Bewegen der Maus erscheint eine Vorschau-Linie vom letzten Punkt zur Mausposition.
- Gesetzte Punkte werden im Zeichenmodus als Griffpunkte angezeigt.
- Punkte koennen per Drag verschoben werden.
- Klick auf eine bestehende Verbindungslinie fuegt dort einen neuen Punkt ein.
- Klick auf einen vorhandenen Punkt der aktiven offenen Form schliesst die Form.
- Wird der letzte Punkt per Drag auf einen vorhandenen Punkt gelegt, wird die Form ebenfalls geschlossen.
- Nach dem Schliessen ist keine aktive Form mehr gesetzt; der naechste Klick beginnt eine neue Form.
- `Escape` verlaesst die aktive Zeichnung, vorhandene Formen bleiben erhalten.
- Rechtsklick auf eine Form oder einen ihrer Punkte loescht die ganze Form nach `window.confirm(...)`.
- `Undo` entfernt beim aktiven offenen Pfad den letzten Punkt oder den Pfad bei nur einem Punkt.
- `Hintergrund loeschen` entfernt alle SVG-Formen.

Die SVG-Zeichenlogik liegt aktuell noch in `ModuleOverviewView.vue`. Die Typen sind bereits nach `webapp/src/types/ModuleOverview.ts` ausgelagert.

Naechster sinnvoller Extraktionsschritt:

```text
webapp/src/components/ModuleOverview/SvgBackgroundEditor.vue
```

Danach sollte `ModuleOverviewView.vue` vor allem Toolbar, Live-Daten, Layout und Speichern/Laden koordinieren.

## Zoom und Zeichenbereich

Gespeicherte Koordinaten bleiben logische Pixel. Nur die Darstellung wird skaliert:

- `.module-overview-workspace` nutzt `transform: scale(zoomFactor)`
- `.module-overview-zoom-spacer` stellt die Scrollgroesse bereit
- Pointer-Koordinaten werden in `canvasPointerPosition(event)` durch `zoomFactor` zurueckgerechnet

Aktuelle Konstanten:

```ts
const MODULE_WIDTH = 150;
const MODULE_HEIGHT = 250;
const MODULE_GAP = 18;
const PLACEMENT_GRID_SIZE = 16;
const CANVAS_MIN_WIDTH = 960;
const CANVAS_MIN_HEIGHT = 320;
const CANVAS_MIN_VIEWPORT_HEIGHT = 320;
const CANVAS_BOTTOM_GAP = 16;
const CANVAS_VERTICAL_OVERFLOW_TOLERANCE = 24;
```

Die Canvas-Hoehe wird aus der echten Position im Viewport berechnet:

```text
window.innerHeight - canvas.getBoundingClientRect().top - CANVAS_BOTTOM_GAP
```

`canvasStyle` schaltet `overflowY` mit Toleranz:

```text
scaledCanvasHeight > canvasAvailableHeight + CANVAS_VERTICAL_OVERFLOW_TOLERANCE
  -> overflow-y: auto
  sonst overflow-y: hidden
```

Historie zur Scrollbar-Suche:

- Reine CSS-Schaetzung wurde verworfen, weil Toolbar, Navbar, Header und Zeilenumbrueche dynamisch sind.
- ResizeObserver-/Viewport-Ansatz wurde wieder entfernt, weil er die Logik zu komplex machte.
- `scrollbar-gutter: stable` wurde entfernt, weil es eine dauerhafte Scrollbar-Spur erzeugen kann.
- `box-sizing: border-box` sitzt jetzt in `ModuleCard.vue`, damit CSS-Hoehe und `MODULE_HEIGHT` zusammenpassen.

Falls die vertikale Scrollbar erneut auffaellt, im Browser messen:

```js
const c = document.querySelector('.module-overview-canvas');
const s = document.querySelector('.module-overview-zoom-spacer');
const w = document.querySelector('.module-overview-workspace');
({
  canvasClientHeight: c.clientHeight,
  canvasScrollHeight: c.scrollHeight,
  canvasOffsetHeight: c.offsetHeight,
  spacerClientHeight: s.clientHeight,
  spacerOffsetHeight: s.offsetHeight,
  workspaceRect: w.getBoundingClientRect(),
  canvasRect: c.getBoundingClientRect(),
});
```

## Heatmap

Verfuegbare Modi:

- Keine Heatmap
- Heatmap: Leistung
- Heatmap: Leistung / Maximum
- Heatmap: Leistungsdifferenz
- Heatmap: Tagesertrag
- Heatmap: Tagesertragsdifferenz

Berechnung:

- `Leistung` skaliert relativ zum staerksten aktuell sichtbaren aktivierten Modul.
- `Tagesertrag` skaliert relativ zum hoechsten Tagesertrag der aktuell sichtbaren aktivierten Module.
- `Leistung / Maximum` skaliert pro Modul:

```text
ratio = module.Power.v / module.powerMaximum
```

Fallback fuer `powerMaximum`:

```text
Power.max vorhanden -> Power.max
sonst -> inverter.limit_absolute / Anzahl DC-Kanaele
```

Differenzmodi:

```text
ratio = (value - minimum) / (maximum - minimum)
```

Wenn `minimum === maximum`, keine Werte vorhanden sind oder kein sinnvoller Maximalwert existiert, wird keine Heatmap-Farbe gesetzt.

Die Farbe wird in `heatmapStyle(module)` per HSL berechnet:

```text
ratio = value / maximum
hue = 210 - ratio * 150
backgroundLightness = 96 - ratio * 24
borderLightness = 58 - ratio * 18
```

Niedrige Werte sind blaeulich/hell, hohe Werte gelblich/satter.

## Locale-Keys

Relevante neue Keys:

- `menu.ModuleOverview`
- `moduleoverview.Title`
- `moduleoverview.Modules`
- `moduleoverview.Producing`
- `moduleoverview.Offline`
- `moduleoverview.EditMode`
- `moduleoverview.Arrange`
- `moduleoverview.SaveLayout`
- `moduleoverview.LoadLayoutFailed`
- `moduleoverview.DrawBackground`
- `moduleoverview.Undo`
- `moduleoverview.ClearBackground`
- `moduleoverview.ConfirmDeleteShape`
- `moduleoverview.DrawColor`
- `moduleoverview.DrawWidth`
- `moduleoverview.ShowDisabled`
- `moduleoverview.HeatmapNone`
- `moduleoverview.HeatmapPower`
- `moduleoverview.HeatmapPowerMax`
- `moduleoverview.HeatmapPowerDiff`
- `moduleoverview.HeatmapYieldDay`
- `moduleoverview.HeatmapYieldDayDiff`
- `moduleoverview.Zoom`
- `moduleoverview.NoModules`
- `moduleoverview.Channel`

## Bekannte Themen

- `GET /api/livedata/status` kann initial keine `DC`-Daten enthalten. Module erscheinen dann erst nach WebSocket-Daten.
- Falls scheinbare Duplikate sichtbar werden, zuerst die eingeblendeten Modul-Keys vergleichen:
  - Gleicher Key: Rendering-/State-Problem.
  - Unterschiedlicher Key: echte unterschiedliche Kanaele, nur optisch/positionell aehnlich.
- `ModuleOverviewView.vue` ist trotz ausgelagerter Karte, Typen und Utils weiterhin gross. Weitere Feature-Arbeit sollte eher extrahieren als die View weiter aufblasen.
- Build-Artefakt `webapp_dist/js/app.js.gz` bewusst behandeln: fuer PRs klaeren, ob es im Commit enthalten sein soll.

## Verifikation

Zuletzt relevante Checks:

```powershell
cd C:\DEV\PlatformIO\OpenDTU-GitClone\OpenDTU\webapp
corepack yarn type-check
corepack yarn build-only
```

Bekannter Hinweis:

- `corepack yarn build-only` kann in der Sandbox mit `spawn EPERM` beim Laden von `vite.config.ts` scheitern.
- Bei Bedarf ausserhalb der Sandbox bzw. mit Escalation ausfuehren.

## Lokales UI-Debugging

Projekt verwendet laut `webapp/package.json`:

- Package Manager: `yarn@1.22.22`
- Yarn ueber Corepack aufrufen:

```powershell
cd C:\DEV\PlatformIO\OpenDTU-GitClone\OpenDTU\webapp
corepack yarn install
corepack yarn dev
```

Debugging mit echtem ESP32:

```ts
// webapp/vite.user.ts
export const proxy_target = '192.168.30.60'
```

Danach:

```powershell
corepack yarn dev
```

Vite proxyt dann:

- `/api/...` nach `http://192.168.30.60`
- `/livedata` nach `ws://192.168.30.60/livedata`

Ohne ESP32 braucht die UI Mock-Daten fuer mindestens:

- `GET /api/livedata/status`

Optional fuer Persistenztests:

- `GET /api/file/get?file=module_overview.json`
- `POST /api/file/upload?file=module_overview.json`

## Naechste sinnvolle Schritte

1. UI gegen echten ESP32 pruefen:
   - Initialdaten ohne `DC`
   - WebSocket-Merge
   - Modul-Dragging bei Zoom != 100 %
   - Zeichnen, Punkt verschieben, Form schliessen, Form loeschen
   - Heatmap-Modi mit echten Modulwerten
   - Bedienbarkeit auf kleinen Bildschirmen
2. `SvgBackgroundEditor.vue` extrahieren.
3. Entscheiden, ob der Debug-Key dauerhaft sichtbar bleiben soll oder spaeter nur im Editier-/Debugmodus.
4. Optional temporaere Layout-Sicherung in `localStorage` implementieren, ohne ESP-Restart.
5. Optional SVG-Hintergrund als separate Datei laden/speichern:
   - `GET /api/file/get?file=module_overview.svg`
   - `POST /api/file/upload?file=module_overview.svg`
