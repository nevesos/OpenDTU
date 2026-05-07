# Feature_UIModuleOverview Arbeitsnotizen

Stand: 2026-05-07

## Ziel

Auf Branch `Feature_UIModuleOverview` soll OpenDTU eine zweite Live-Ansicht fuer eine virtuelle Moduluebersicht erhalten. Module sollen in der Weboberflaeche frei platzierbar sein und aktuelle Livewerte wie Leistung anzeigen.

Leitlinie: so wenig Aenderungen wie moeglich am bestehenden Code.

## Relevante Architektur

- Bestehende Live-Ansicht: `webapp/src/views/HomeView.vue`
- Live-Datentypen: `webapp/src/types/LiveDataStatus.ts`
- WebSocket-Service: `webapp/src/utils/websocketService.ts`
- Router: `webapp/src/router/index.ts`
- Menue: `webapp/src/components/NavBar.vue`
- Uebersetzungen:
  - `webapp/src/locales/de.json`
  - `webapp/src/locales/en.json`
  - `webapp/src/locales/fr.json`

Die Firmware-Live-API ist bereits vorhanden:

- Initialer Abruf: `GET /api/livedata/status`
- Live-Updates: WebSocket `/livedata`

Pro Wechselrichter stehen Modul-/Stringdaten unter `liveData.inverters[].DC[]` zur Verfuegung, unter anderem:

- `Power`
- `Voltage`
- `Current`
- `YieldDay`
- `YieldTotal`
- `Irradiation`

## Implementierungsansatz

- Keine Aenderung an Firmware-Livedaten.
- Keine Aenderung an `Configuration.h` / `Configuration.cpp`.
- Neue Webapp-Ansicht statt Umbau von `HomeView.vue`:
  - `webapp/src/views/ModuleOverviewView.vue`
- Neue Route:
  - `/module-overview`
- Neuer Menueeintrag neben der bestehenden Live-Ansicht.
- Die neue Ansicht nutzt dieselbe Datenquelle wie `HomeView.vue`:
  - `GET /api/livedata/status`
  - WebSocket `/livedata`
- Firmware-Aenderung nur in `src/WebApi_file.cpp`:
  - `module_overview.json` ist fuer lesenden Zugriff explizit freigegeben.
  - Die Freigabe ist absichtlich eng gehalten, damit die generische Datei-API keine sensiblen Dateien lesend freigibt.

## Persistenz

Fuer die Persistenz wird die bestehende Datei-API genutzt:

- Layout laden: `GET /api/file/get?file=module_overview.json`
- Layout speichern: `POST /api/file/upload?file=module_overview.json`
- Optional spaeter:
  - `GET /api/file/get?file=module_overview.svg`
  - `POST /api/file/upload?file=module_overview.svg`

Wichtig: Die bestehende Upload-API in `src/WebApi_file.cpp` schreibt nach LittleFS und ruft nach Upload `RestartHelper.triggerRestart()` auf.

Aktueller API-Stand:

- `GET /api/file/get?file=module_overview.json`
  - liefert `404`, wenn die Datei noch nicht existiert.
  - nutzt fuer diese Datei `WebApi.checkCredentialsReadonly(...)`.
  - alle anderen Dateien bleiben wie bisher hinter Schreib-/Admin-Credentials.
- `POST /api/file/upload?file=module_overview.json`
  - nutzt weiterhin `WebApi.checkCredentials(...)`.
  - schreibt die Datei nach LittleFS.
  - triggert danach weiterhin einen Neustart.

Konsequenz:

- Beim Verschieben der Module nicht automatisch speichern.
- Positionen waehrend des Editierens nur im Vue-State halten.
- Optional temporaer in `localStorage` sichern.
- Alle Positionen gesammelt speichern, nur bei explizitem Klick auf `Speichern`.
- Der Neustart nach Speichern ist akzeptiert.

Implementiert am 2026-05-07:

- Layout wird beim Oeffnen aus `module_overview.json` geladen.
- Layout wird nur per explizitem Button `Speichern` geschrieben.
- Gespeichert werden nur die fuer die Wiederherstellung notwendigen UI-Daten:
  - `version`
  - `zoomFactor`
  - `heatmapMode`
  - `showDisabledModules`
  - Modulpositionen mit `key`, `x`, `y`
  - strukturierte SVG-Hintergrundpfade aus dem Editor
- Nicht gespeichert werden Livewerte oder Editiermodus.
- Der Upload nutzt `POST /api/file/upload?file=module_overview.json`.
- Nach erfolgreichem Upload wird das vorhandene Restart-Wait-Verhalten verwendet.

## Aktuelle Layout-JSON

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

Eine separate SVG-Datei wird aktuell nicht geladen oder gespeichert. Die SVG-Zeichnung wird strukturiert als `backgroundPaths` in `module_overview.json` persistiert. `backgroundSvgMarkup()` kann daraus bereits SVG-Markup erzeugen, wird im UI aber nicht als Export verwendet.

## UI-Funktionsstand

- Neue Ansicht mit Normalmodus und Editiermodus.
- Im normalen Modus Anzeige von Livewerten und Heatmap.
- Im Editiermodus Module per Pointer-Events frei verschieben.
- Keine neue Dependency noetig.
- Modul-Key:
  - `serial + ':DC:' + channelIndex`
- Kachel zeigt:
  - Wechselrichtername
  - Kanalnummer
  - aktuelle Leistung
  - Spannung
  - Strom
  - Tagesertrag
  - Debug-ID / Modul-Key
- Statusfarbe abgeleitet aus:
  - `poll_enabled`
  - `reachable`
  - `producing`
- Status wird als Rahmenfarbe und Badge dargestellt:
  - disabled: secondary
  - offline: danger
  - idle: warning
  - producing: success
- Klick auf `Speichern`:
  - JSON aus aktuellem Layout bauen
  - per `/api/file/upload?file=module_overview.json` hochladen
  - danach bestehendes Neustart-/WaitRestart-Verhalten verwenden

## Minimal betroffene Webapp-Dateien

- `webapp/src/views/ModuleOverviewView.vue` neu
- `webapp/src/router/index.ts`
- `webapp/src/components/NavBar.vue`
- `webapp/src/locales/de.json`
- `webapp/src/locales/en.json`
- `webapp/src/locales/fr.json`

Nicht angefasst:

- `src/WebApi_ws_live.cpp`
- `src/WebApi_inverter.cpp`
- `src/Configuration.cpp`
- `include/Configuration.h`
- `webapp/src/views/HomeView.vue`

Minimal angefasst in der Firmware:

- `src/WebApi_file.cpp`
  - Readonly-Allowlist fuer `/module_overview.json`

Eine spaetere Extraktion gemeinsamer LiveData-Logik ist moeglich, aber fuer den ersten minimalen Schritt nicht erforderlich.

## Bisherige lokale Pruefungen

Branch:

```text
Feature_UIModuleOverview
```

Der Arbeitsbaum war vor dieser Notiz sauber. Es gab zu diesem Zeitpunkt keine lokalen Feature-Aenderungen zum Committen.

## Aktueller Implementierungsstand

Stand 2026-05-07:

- Neue View vorhanden: `webapp/src/views/ModuleOverviewView.vue`
- Neue Route vorhanden: `/module-overview`
- Menueeintrag neben `Live-Ansicht` vorhanden.
- Locale-Texte in `de.json`, `en.json`, `fr.json` ergaenzt.
- Firmware-Livedaten unveraendert.
- `src/WebApi_file.cpp` enthaelt eine eng begrenzte Readonly-Freigabe fuer `/module_overview.json`.
- Persistenz fuer `module_overview.json` vorhanden.

Die Moduluebersicht nutzt:

- Initial `GET /api/livedata/status`
- Danach WebSocket `/livedata`
- Modul-Key: `${serial}:DC:${channel}`

Die Module werden aus `inverters[].DC` erzeugt. Datenformat-Finding:

- `GET /api/livedata/status` lieferte beim Test zunaechst nur Wechselrichter-Metadaten und `total`, aber keine `DC`-Daten.
- Die `DC`-Daten kamen ueber WebSocket.
- `DC` kommt in den Live-Daten als Objekt mit numerischen String-Keys, z. B. `"0"`, `"1"`, `"2"`, `"3"`, nicht zwingend als echtes Array.
- Die View nutzt deshalb `Object.entries(inverter.DC || {})`.
- WebSocket-Updates werden in `handleMessage(...)` in `liveData.total`, `liveData.hints` und den passenden Wechselrichter in `liveData.inverters` gemerged.
- Neue Modulpositionen werden mit `ensureModulePositions()` kollisionsarm vergeben.

## Aktuelle UI-Funktionen

- Normalmodus: Module anzeigen.
- Editiermodus: Module per Pointer-Events frei verschieben.
- Module rasten beim Verschieben auf ein 16px-Platzierungsraster ein.
- Automatische Anordnung nutzt ebenfalls dieses Raster.
- Positionen werden waehrend des Editierens nur im Vue-State gehalten.
- Keine Speicherung beim Draggen.
- Expliziter Speicherbutton vorhanden.
- Deaktivierte Module (`poll_enabled=false`) werden standardmaessig ausgeblendet.
- Per Schalter `Deaktivierte anzeigen` koennen sie wieder eingeblendet werden.
- Modulkarte zeigt sichtbar:
  - Wechselrichtername
  - Kanalnummer
  - Leistung
  - Spannung
  - Strom
  - Tagesertrag
  - Debug-ID / Modul-Key
- Mouseover-Tooltip zeigt Debugdetails:
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
- Module sind optisch hochformatig wie PV-Module dargestellt.
- Zellraster innerhalb der Module wurde wieder entfernt.
- Status wird als Rahmenfarbe dargestellt:
  - disabled: secondary
  - offline: danger
  - idle: warning
  - producing: success

## SVG-Hintergrund / Zeichenflaeche

Stand: 2026-05-07 nach mehreren UI-Iterationen.

Die Moduluebersicht hat jetzt im Editiermodus eine SVG-Hintergrundebene unter den Modulkarten.

Aktueller Funktionsumfang:

- Button `Zeichnen` aktiviert/deaktiviert den Zeichenmodus.
- Zeichenmodus funktioniert punktbasiert, nicht freihand:
  - Klick auf freie Flaeche setzt einen Punkt.
  - Punkte werden per gerader Linie verbunden.
  - Beim Bewegen der Maus wird eine Vorschau-Linie vom letzten Punkt zur Mausposition angezeigt.
- Solange Zeichnen aktiv ist, werden gesetzte Punkte als kleine Griffpunkte angezeigt.
- Punkte koennen per Drag verschoben werden.
- Klick auf eine bestehende Verbindungslinie fuegt dort einen neuen Punkt in diese Form ein.
- Klick auf einen vorhandenen Punkt der aktiven offenen Form schliesst die Form.
- Wird der letzte Punkt per Drag auf einen vorhandenen Punkt gelegt, wird die Form ebenfalls geschlossen.
- Nach dem Schliessen ist keine aktive Form mehr gesetzt; der naechste Klick auf die Flaeche beginnt eine neue Form.
- `Escape` bricht die aktuelle aktive Zeichnung ab:
  - gesetzte Punkte/Formen bleiben erhalten,
  - aktive Form wird verlassen,
  - Vorschau-Linie verschwindet.
- Rechtsklick auf eine Form oder auf einen ihrer Punkte loescht die ganze Form erst nach `window.confirm(...)`.
- `Undo` entfernt beim aktiven offenen Pfad den letzten Punkt bzw. entfernt den Pfad bei nur einem Punkt.
- `Hintergrund loeschen` entfernt alle SVG-Formen.

Technisches Modell:

- Typen in `ModuleOverviewView.vue`:
  - `DrawingPoint`
  - `BackgroundPath`
  - `BackgroundControlPoint`
  - `BackgroundPreviewLine`
- `BackgroundPath` enthaelt:

```ts
{
  id: number;
  color: string;
  width: number;
  points: DrawingPoint[];
  closed: boolean;
}
```

- Die SVG-Pfade werden im Vue-State als strukturierte Punktlisten gehalten.
- `pathData(path)` baut daraus den SVG-`d`-String.
- `backgroundSvgMarkup()` erzeugt bereits ein persistierbares SVG-Markup.
- Vorschau-Linie und Griffpunkte werden nur in der UI gerendert und nicht in `backgroundSvgMarkup()` exportiert.
- Die Hintergrundpfade werden aktuell strukturiert in `module_overview.json` persistiert.
- Kein separates Laden/Speichern von `module_overview.svg`.
- Kein Import/Export im UI.

Hinweis zur Architektur:

- Die SVG-Zeichenlogik liegt aktuell noch komplett in `ModuleOverviewView.vue`.
- Kurzfristig ist das akzeptiert, weil die Funktion nur von dieser View genutzt wird.
- Vor Persistenz oder weiteren Werkzeugen sollte die Logik extrahiert werden, zum Beispiel nach:

```text
webapp/src/components/ModuleOverview/SvgBackgroundEditor.vue
webapp/src/types/ModuleOverview.ts
```

Dann sollte `ModuleOverviewView.vue` nur noch Toolbar, Live-Daten, Layout und Speichern/Laden koordinieren.

## Zoom und Zeichenbereich-Groesse

Die Ansicht hat einen Zoom-Select:

- 50 %
- 75 %
- 100 %
- 125 %
- 150 %

Wichtig:

- Gespeicherte Koordinaten bleiben logische Pixel.
- Nur die Darstellung wird skaliert.
- Die Arbeitsflaeche wird ueber `.module-overview-workspace` mit `transform: scale(zoomFactor)` skaliert.
- Ein aeusserer `.module-overview-zoom-spacer` stellt die Scrollgroesse bereit.
- Pointer-Koordinaten werden in `canvasPointerPosition(event)` durch `zoomFactor` zurueckgerechnet.

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

Aktuelle Hoehenlogik:

- `canvasAvailableHeight` wird per `updateCanvasAvailableHeight()` aus der echten Position im Viewport berechnet:

```text
window.innerHeight - canvas.getBoundingClientRect().top - CANVAS_BOTTOM_GAP
```

- Die Canvas bekommt diese Hoehe per Inline-Style aus `canvasStyle`.
- `canvasStyle` schaltet `overflowY` aktuell mit Toleranz:

```text
scaledCanvasHeight > canvasAvailableHeight + CANVAS_VERTICAL_OVERFLOW_TOLERANCE
  -> overflow-y: auto
  sonst overflow-y: hidden
```

Wichtige Historie zur Scrollbar-Suche:

- Eine reine CSS-Schaetzung wie `height: clamp(..., calc(100vh - 15rem), ...)` wurde verworfen, weil Toolbar, Navbar, Header und Zeilenumbrueche dynamisch sind.
- Ein ResizeObserver-/Viewport-Ansatz wurde kurz getestet, dann wieder entfernt, weil er die Groessenlogik zu komplex machte.
- `scrollbar-gutter: stable` wurde entfernt, weil es eine dauerhafte Scrollbar-Spur erzeugen kann.
- `box-sizing: border-box` wurde fuer `.module-card` gesetzt, damit CSS-Hoehe und `MODULE_HEIGHT` zusammenpassen.
- Die vertikale Scrollbar im Zeichenbereich war zuletzt noch Thema. Verdacht:
  - tatsaechlicher DOM-Overflow durch Modulkarte, Shadow, horizontale Scrollbar oder Rundung,
  - die aktuelle Toleranz blendet kleine vertikale Restueberlaeufe aus.
- Falls die Scrollbar weiter sichtbar bleibt, sollte im Browser direkt gemessen werden:

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

Danach zuerst pruefen, ob `scrollHeight - clientHeight` nur wenige Pixel oder echter Inhalt ist.

## Heatmap

Die Moduluebersicht hat eine optionale Heatmap-Auswahl:

- Keine Heatmap
- Heatmap: Leistung
- Heatmap: Leistung / Maximum
- Heatmap: Leistungsdifferenz
- Heatmap: Tagesertrag
- Heatmap: Tagesertragsdifferenz

Berechnung:

- `Heatmap: Leistung` skaliert relativ zum staerksten aktuell sichtbaren Modul.
- `Heatmap: Tagesertrag` skaliert relativ zum hoechsten Tagesertrag der aktuell sichtbaren Module.
- `Heatmap: Leistung / Maximum` skaliert pro Modul:

```text
ratio = module.Power.v / module.powerMaximum
```

Finding zu Maximalwerten:

- In den getesteten WebSocket-Daten war `Power.max` nicht enthalten.
- Beispiel:

```json
{"v":59.9,"u":"W","d":1}
```

Fallback fuer `powerMaximum`:

```text
Power.max vorhanden -> Power.max
sonst -> inverter.limit_absolute / Anzahl DC-Kanaele
```

Beispiel:

```text
HM1500 mit 4 DC-Kanaelen: 1500 W / 4 = 375 W pro Modul
```

Wenn weder `Power.max` noch ein sinnvoller Fallback vorhanden ist, wird im Modus `Leistung / Maximum` fuer dieses Modul keine Heatmap-Farbe gesetzt.

Differenzmodi:

- `Heatmap: Leistungsdifferenz` nutzt die aktuelle Leistung aller sichtbaren, aktivierten Module.
- `Heatmap: Tagesertragsdifferenz` nutzt den Tagesertrag aller sichtbaren, aktivierten Module.
- Die Skalierung laeuft jeweils ueber den Wertebereich der sichtbaren Module:

```text
ratio = (value - minimum) / (maximum - minimum)
```

- Wenn `minimum === maximum` oder keine Werte vorhanden sind, wird keine Heatmap-Farbe gesetzt.

Die Farbe wird in `heatmapStyle(module)` per HSL berechnet:

```text
ratio = value / maximum
hue = 210 - ratio * 150
backgroundLightness = 96 - ratio * 24
borderLightness = 58 - ratio * 18
```

Niedrige Werte sind blaeulich/hell, hohe Werte gelblich/satter.

## Bekannte UI-/Daten-Themen

- Anfangs wurden keine Module angezeigt, weil `GET /api/livedata/status` keine `DC`-Daten enthielt. Nach WebSocket-Daten erscheinen sie.
- Beim Verschieben wirkten Module teilweise doppelt. Ursache war wahrscheinlich Ueberlappung durch stueckweise eintreffende WebSocket-Daten und gleiche automatische Startpositionen. Die Positionsvergabe wurde kollisionsresistenter gemacht.
- Falls nochmal scheinbare Duplikate sichtbar werden, zuerst die eingeblendeten Modul-Keys vergleichen:
  - Gleicher Key: Rendering-/State-Bug.
  - Unterschiedlicher Key: echte unterschiedliche Kanaele, nur optisch/positionell aehnlich.

## Aktuelle technische Hinweise

- `ModuleOverviewView.vue` nutzt lokales `<style scoped>`.
- Bootstrap und globale App-Styles werden weiterhin ueber `webapp/src/scss/styles.scss` und `App.vue` genutzt.
- Die Moduluebersicht verwendet globale Bootstrap-Klassen fuer Buttons, Badges, Form Controls usw.
- Modul-spezifische CSS-Klassen bleiben lokal in `ModuleOverviewView.vue`.
- `ModuleOverviewView.vue` ist inzwischen gross und enthaelt Live-Daten, Modul-Dragging, SVG-Editor, Zoom und Hoehenlogik.
- Naechste groessere Aenderung sollte eher extrahieren als weiter in dieser View wachsen.
- Relevante Locale-Keys fuer die neuen UI-Elemente:
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

## Aktuelle Verifikation

Wiederholt erfolgreich ausgefuehrt:

```powershell
cd C:\DEV\PlatformIO\OpenDTU-GitClone\OpenDTU\webapp
corepack yarn type-check
```

`corepack yarn build-only` wurde nach den UI-Aenderungen erfolgreich ausgefuehrt, musste aber wegen `spawn EPERM` ausserhalb der Sandbox laufen. Dabei wurde `webapp_dist/js/app.js.gz` geaendert. Fuer einen reinen Source-Commit diese Build-Artefakte bewusst behandeln.

Bekannter Build-Hinweis:

- `corepack yarn build-only` kann in der Sandbox mit `spawn EPERM` beim Laden von `vite.config.ts` scheitern.
- Bei Bedarf mit Escalation ausfuehren.

Der lokale Vite-Dev-Server lief zuletzt auf:

```text
http://127.0.0.1:5174/module-overview
```

Port `5173` war belegt, Vite wich auf `5174` aus.

## UI-Debugging

Die Webapp kann lokal mit Vite debuggt werden.

Projekt verwendet laut `webapp/package.json`:

- Node vorhanden: `v22.19.0`
- npm vorhanden: `10.9.3`
- Corepack vorhanden: `0.34.0`
- Package Manager: `yarn@1.22.22`

`yarn` war nicht global als PowerShell-Befehl verfuegbar. Loesung:

```powershell
cd C:\DEV\PlatformIO\OpenDTU-GitClone\OpenDTU\webapp
corepack yarn install
```

Die Installation wurde erfolgreich ausgefuehrt. Hinweis aus Yarn:

```text
eslint-plugin-vue@10.8.0 has unmet peer dependency vue-eslint-parser@^10.0.0
```

Das globale Aktivieren von Yarn mit `corepack enable yarn` scheiterte wegen fehlender Schreibrechte in:

```text
C:\Program Files\nodejs\yarn
```

Daher fuer dieses Projekt weiterhin `corepack yarn ...` verwenden.

## Debugging mit echtem ESP32

In `webapp/vite.user.ts` eine Proxy-Zieladresse anlegen:

```ts
export const proxy_target = '192.168.30.60'
```

PowerShell-Befehl zum Erstellen der Datei:

```powershell
Set-Content -Path .\vite.user.ts -Value "export const proxy_target = '192.168.30.60'" -Encoding Ascii
```

Danach starten:

```powershell
corepack yarn dev
```

Browser:

```text
http://localhost:5173
```

Vite proxyt dann:

- `/api/...` nach `http://192.168.30.60`
- `/livedata` nach `ws://192.168.30.60/livedata`

## Debugging ohne ESP32

Ohne ESP32 braucht die UI Mock-Daten fuer mindestens:

- `GET /api/livedata/status`

Fuer die Moduluebersicht optional zusaetzlich:

- `GET /api/file/get?file=module_overview.json`
- `POST /api/file/upload?file=module_overview.json`

Pragmatischer Ansatz:

- Kleinen lokalen Mock-Server auf `127.0.0.1:8080` starten.
- `webapp/vite.user.ts` entsprechend setzen:

```ts
export const proxy_target = '127.0.0.1:8080'
```

Fuer reines UI-Layout und Dragging reicht initial der GET-Mock. WebSocket `/livedata` kann fuer den ersten Layout-Stand ignoriert werden, solange die Ansicht mit initialen Daten umgehen kann.

## Naechste sinnvolle Schritte

1. Aktuelle Zeichenbereich-Groesse im Browser messen:
   - `clientHeight`, `scrollHeight`, `offsetHeight` von `.module-overview-canvas`
   - Hoehe von `.module-overview-zoom-spacer`
   - `getBoundingClientRect()` von Canvas, Spacer, Workspace und Modulkarten
   - erst danach weitere Scrollbar-Fixes machen.
2. UI im Browser gegen echten ESP32 pruefen:
   - Zeichnen, Punkt verschieben, Form schliessen, Form loeschen mit Confirm
   - Zoom 50/75/100/125/150 %
   - Modul-Dragging bei Zoom != 100 %
   - scheinbare Duplikate anhand sichtbarer Modul-Keys bewerten
   - Heatmap-Modus `Leistung / Maximum` mit Fallbackwerten pruefen
   - Heatmap-Differenzmodi mit echten Modulwerten pruefen
   - Bedienbarkeit auf kleinen Bildschirmen pruefen
3. Vor weiterer Erweiterung SVG-Editor extrahieren:
   - `SvgBackgroundEditor.vue`
   - gemeinsame Typen in `webapp/src/types/ModuleOverview.ts`
4. Entscheiden, ob der Debug-Key dauerhaft sichtbar bleiben soll oder spaeter nur im Editier-/Debugmodus.
5. Optional temporaere Layout-Sicherung in `localStorage` implementieren, ohne ESP-Restart.
6. Optional SVG-Hintergrund als separate Datei laden/speichern:
   - `GET /api/file/get?file=module_overview.svg`
   - `POST /api/file/upload?file=module_overview.svg`
