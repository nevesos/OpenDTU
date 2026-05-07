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

1. `webapp/vite.user.ts` fuer echten ESP32 oder Mock-Server setzen.
2. Webapp mit `corepack yarn dev` starten.
3. Neue `ModuleOverviewView.vue` erstellen.
4. Route und Menueeintrag ergaenzen.
5. Initiale Live-Daten laden und Module aus `inverters[].DC[]` ableiten.
6. Editiermodus mit Pointer-Dragging implementieren.
7. Layout-JSON nur bei explizitem Speichern per Datei-API hochladen.
8. `corepack yarn build` und bei Bedarf `corepack yarn type-check` ausfuehren.
