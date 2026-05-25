# Energy History Testdaten 2098/2099

Diese Dateien sind Upload-Fixtures fuer die Energy-History-Oberflaeche.
Beim Upload muss der Zielpfad dem relativen Pfad unter diesem Ordner entsprechen,
zum Beispiel `/energy/5m/total_2099_01.eh5`.

Ziele:

- `total`
- `inv_999999990101`
- `inv_999999990102`

Enthaltene Besonderheiten:

- ganze Kalenderjahre 2098 und 2099 mit 5-Minuten-, Tages- und Monatsdateien
- saisonale und taegliche Ertragsvariation
- YYYY-01-14: kompletter Datenausfall
- YYYY-03-04: fehlende Mittags-Samples
- YYYY-04-17: WR 2 zeitweise nicht erreichbar
- YYYY-06-21: geschaetzte Samples und ein korrigierter Duplicate-Slot
- YYYY-07-09: Day-Reset-Flag und korrigierter Tagesdatensatz
- YYYY-09 total 5m: absichtlich unvollstaendiger finaler Block fuer Recovery-Tests
- YYYY-10-15: einzelne fehlende Samples
