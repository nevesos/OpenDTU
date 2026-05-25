# Energy History Testdaten 2099

Diese Dateien sind Upload-Fixtures fuer die Energy-History-Oberflaeche.
Beim Upload muss der Zielpfad dem relativen Pfad unter diesem Ordner entsprechen,
zum Beispiel `/energy/5m/total_2099_01.eh5`.

Ziele:

- `total`
- `inv_999999990101`
- `inv_999999990102`

Enthaltene Besonderheiten:

- ganzes Kalenderjahr 2099 mit 5-Minuten-, Tages- und Monatsdateien
- saisonale und taegliche Ertragsvariation
- 2099-01-14: kompletter Datenausfall
- 2099-03-04: fehlende Mittags-Samples
- 2099-04-17: WR 2 zeitweise nicht erreichbar
- 2099-06-21: geschaetzte Samples und ein korrigierter Duplicate-Slot
- 2099-07-09: Day-Reset-Flag und korrigierter Tagesdatensatz
- 2099-09 total 5m: absichtlich unvollstaendiger finaler Block fuer Recovery-Tests
- 2099-10-15: einzelne fehlende Samples
