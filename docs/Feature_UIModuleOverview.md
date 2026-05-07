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

## Geplanter Implementierungsansatz

- Keine Aenderung an Firmware-Livedaten.
- Keine Aenderung an `Configuration.h` / `Configuration.cpp`.
- Neue Webapp-Ansicht statt Umbau von `HomeView.vue`, zum Beispiel:
  - `webapp/src/views/ModuleOverviewView.vue`
- Neue Route, zum Beispiel:
  - `/module-overview`
- Neuer Menueeintrag neben der bestehenden Live-Ansicht.
- Die neue Ansicht nutzt dieselbe Datenquelle wie `HomeView.vue`:
  - `GET /api/livedata/status`
  - WebSocket `/livedata`

## Persistenz

Fuer minimale Firmware-Aenderungen soll die bestehende Datei-API genutzt werden:

- Layout laden: `GET /api/file/get?file=module_overview.json`
- Layout speichern: `POST /api/file/upload?file=module_overview.json`
- Optional spaeter:
  - `GET /api/file/get?file=module_overview.svg`
  - `POST /api/file/upload?file=module_overview.svg`

Wichtig: Die bestehende Upload-API in `src/WebApi_file.cpp` schreibt nach LittleFS und ruft nach Upload `RestartHelper.triggerRestart()` auf.

Konsequenz:

- Beim Verschieben der Module nicht automatisch speichern.
- Positionen waehrend des Editierens nur im Vue-State halten.
- Optional temporaer in `localStorage` sichern.
- Alle Positionen gesammelt speichern, nur bei explizitem Klick auf `Speichern`.
- Der Neustart nach Speichern ist akzeptiert.

## Vorgeschlagene Layout-JSON

```json
{
  "version": 1,
  "background": "module_overview.svg",
  "modules": [
    {
      "key": "123456789012:DC:0",
      "x": 120,
      "y": 80
    }
  ]
}
```

Zum Start kann `background` weggelassen oder ignoriert werden. Eine separate SVG-Datei kann spaeter ergaenzt werden.

## UI-Plan

- Neue Ansicht mit Normalmodus und Editiermodus.
- Im normalen Modus nur Anzeige.
- Im Editiermodus Module per Pointer-Events frei verschieben.
- Keine neue Dependency noetig.
- Modul-Key:
  - `serial + ':DC:' + channelIndex`
- Kachel zeigt zum Beispiel:
  - Wechselrichtername
  - Kanalnummer
  - aktuelle Leistung
  - optional Spannung, Strom, Tagesertrag
- Statusfarbe abgeleitet aus:
  - `poll_enabled`
  - `reachable`
  - `producing`
- Klick auf `Speichern`:
  - JSON aus aktuellem Layout bauen
  - per `/api/file/upload?file=module_overview.json` hochladen
  - danach bestehendes Neustart-/WaitRestart-Verhalten verwenden oder auf Reconnect warten

## Minimal betroffene Webapp-Dateien

- `webapp/src/views/ModuleOverviewView.vue` neu
- `webapp/src/router/index.ts`
- `webapp/src/components/NavBar.vue`
- `webapp/src/locales/de.json`
- `webapp/src/locales/en.json`
- `webapp/src/locales/fr.json`

Moeglichst nicht anfassen:

- `src/WebApi_ws_live.cpp`
- `src/WebApi_inverter.cpp`
- `src/Configuration.cpp`
- `include/Configuration.h`
- `webapp/src/views/HomeView.vue`

Eine spaetere Extraktion gemeinsamer LiveData-Logik ist moeglich, aber fuer den ersten minimalen Schritt nicht erforderlich.

## Bisherige lokale Pruefungen

Branch:

```text
Feature_UIModuleOverview
```

Der Arbeitsbaum war vor dieser Notiz sauber. Es gab zu diesem Zeitpunkt keine lokalen Feature-Aenderungen zum Committen.

## Aktueller Implementierungsstand

Stand nach erstem UI-Entwurf:

- Neue View vorhanden: `webapp/src/views/ModuleOverviewView.vue`
- Neue Route vorhanden: `/module-overview`
- Menueeintrag neben `Live-Ansicht` vorhanden.
- Locale-Texte in `de.json`, `en.json`, `fr.json` ergaenzt.
- Keine Firmware-Aenderungen.
- Keine Persistenz implementiert.
- Keine Datei-API-Aufrufe fuer `module_overview.json`.

Die Moduluebersicht nutzt:

- Initial `GET /api/livedata/status`
- Danach WebSocket `/livedata`
- Modul-Key: `${serial}:DC:${channel}`

Die Module werden aus `inverters[].DC` erzeugt. Wichtiges Datenformat-Finding:

- `GET /api/livedata/status` lieferte beim Test zunaechst nur Wechselrichter-Metadaten und `total`, aber keine `DC`-Daten.
- Die `DC`-Daten kamen ueber WebSocket.
- `DC` kommt in den Live-Daten als Objekt mit numerischen String-Keys, z. B. `"0"`, `"1"`, `"2"`, `"3"`, nicht zwingend als echtes Array.
- Die View nutzt deshalb `Object.entries(inverter.DC || {})`.

## Aktuelle UI-Funktionen

- Normalmodus: Module anzeigen.
- Editiermodus: Module per Pointer-Events frei verschieben.
- Module rasten beim Verschieben auf ein 16px-Platzierungsraster ein.
- Automatische Anordnung nutzt ebenfalls dieses Raster.
- Positionen werden nur im Vue-State gehalten.
- Keine Speicherung beim Draggen.
- Kein expliziter Speicherbutton vorhanden.
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

## Heatmap

Die Moduluebersicht hat eine optionale Heatmap-Auswahl:

- Keine Heatmap
- Heatmap: Leistung
- Heatmap: Leistung / Maximum
- Heatmap: Tagesertrag

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

## Aktuelle Verifikation

Wiederholt erfolgreich ausgefuehrt:

```powershell
cd C:\DEV\PlatformIO\OpenDTU-GitClone\OpenDTU\webapp
corepack yarn type-check
```

`corepack yarn build` wurde einmal erfolgreich ausgefuehrt. Dabei wurden auch `webapp_dist/index.html.gz` und `webapp_dist/js/app.js.gz` geaendert. Fuer einen reinen Source-Commit diese Build-Artefakte bewusst behandeln.

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

1. UI im Browser weiter gegen echten ESP32 pruefen, insbesondere:
   - scheinbare Duplikate anhand sichtbarer Modul-Keys bewerten
   - Heatmap-Modus `Leistung / Maximum` mit Fallbackwerten pruefen
   - Bedienbarkeit auf kleinen Bildschirmen pruefen
2. Entscheiden, ob der Debug-Key dauerhaft sichtbar bleiben soll oder spaeter nur im Editier-/Debugmodus.
3. Optional temporaere Layout-Sicherung in `localStorage` implementieren, ohne ESP-Restart.
4. Persistenz erst danach implementieren:
   - Layout laden: `GET /api/file/get?file=module_overview.json`
   - Layout speichern nur per explizitem Button: `POST /api/file/upload?file=module_overview.json`
   - Restart nach Upload bewusst behandeln.
5. Danach `corepack yarn type-check` und `corepack yarn build` ausfuehren.
