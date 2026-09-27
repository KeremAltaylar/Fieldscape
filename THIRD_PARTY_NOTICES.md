# Third-party notices

Fieldscape is proprietary (see LICENSE). It uses these components under their own licences; each
licence's full text is at the source given.

## Code

| Component | Used by | Licence | Source |
| --- | --- | --- | --- |
| MapLibre GL JS 5 | website (map) | BSD-3-Clause | github.com/maplibre/maplibre-gl-js |
| MapLibre Native (iOS distribution) | iOS app (map) | BSD-2-Clause | github.com/maplibre/maplibre-gl-native-distribution |
| MapLibre Native Android SDK 11 | Android app (map) | BSD-2-Clause | github.com/maplibre/maplibre-native |
| supabase-js 2 | website (archive, sign-in) | MIT | github.com/supabase/supabase-js |

The stretch engine (core/devices/stretch.cpp) is Fieldscape's own, written in a clean room from the
public-domain paulstretch_python (github.com/paulnasca/paulstretch_python, "released under Public
Domain"); its provenance is recorded in core/devices/stretch-provenance.md. No PaulXStretch (GPL)
code ships: the website's former port was removed on 2026-09-27.

## Fonts

| Font | Licence |
| --- | --- |
| Cormorant Garamond | SIL Open Font License 1.1 |
| Newsreader | SIL Open Font License 1.1 |
| Courier Prime | SIL Open Font License 1.1 |

Served from Google Fonts on the website; bundled in the iOS and Android apps.

## Map data and imagery

| Layer | Attribution | Terms |
| --- | --- | --- |
| Map | © OpenStreetMap contributors | ODbL 1.0 (openstreetmap.org/copyright) |
| Topo | © OpenTopoMap (CC-BY-SA), © OpenStreetMap contributors | CC BY-SA 3.0 |
| Satellite | Imagery © Esri and its data providers | Esri terms of use |

The attributions are shown on the map, on every platform.
