# Compás · Dominican merengue from the Cibao to New York

5 November 2026 · Private repertoire and performance research

## Cibao and the border

Dominican merengue names a family of music and dance practices. The Dominican nomination to UNESCO distinguishes the Cibao accordion ensemble from the southern and eastern pri-prí, the atabales variant and Samaná's merengue redondo. It documents small ensembles built around button accordion, tambora and güira as well as orchestras with winds, piano and bass. Treat the Cibao model as a local practice with its own sound, and leave the other regions open to their own sources. [Dominican nomination, music and regional variants](https://ich.unesco.org/doc/src/32118.pdf) · [UNESCO inscription](https://ich.unesco.org/en/RL/merengue-music-and-dance-of-the-dominican-republic-01162)

Sydney Hutchinson compares her notation from Rafaelito Román with Coopersmith's 1944 recordings and Edna Garrido's 1947–48 field materials. She hears related tambora rhythms under different dance names and identifies a possible history of exchange across the Dominican–Haitian border. Her evidence complicates the familiar claim that the dance originated with U.S. Marines during the occupation. That story can be retained as reported oral history, with Hutchinson's alternative interpretation attributed. Her description also distinguishes the written percussion cell from flexible melodic triplets. [Hutchinson, Resonancias 20(39), especially pp. 145–148](https://resonancias.uc.cl/wp-content/uploads/sites/13/2014/09/Hutchinson.pdf#page=7)

The separate **Pambiche · Cibao study** uses the onset and stroke distinctions of the traditional two-bar tambora cell documented by Román and Hutchinson on p. 145, Figures 1a and 1b. The retained research PDF is pinned at SHA-256 `42305300fff80f095ba993bf2194888a2464b61299f763550ffb9588867be935`. The table reads across two bars of 2/4 on the machine's zero-based sixteenth grid.

| Stroke | Bar 1 | Bar 2 | Compás synthesis |
| --- | --- | --- | --- |
| Left-hand open | 0 | 13 | Low resonant voice |
| Left-hand slap | 3, 5 | 8, 11 | Hand percussion |
| Right stick on head | 1, 6 | 14 | Skin voice |
| Right stick on rim | 4 | 12 | Stick voice |

The 126 BPM starting tempo, even sixteenth grid, güira accents, root/fifth bass and one-shot fill are Compás choices. Lift adds güira subdivisions. Break removes the bass and retains the four tambora strokes and güira. A live accordion or saxophone player has room for the elastic melody. The four voices are synthetic approximations, with no recorded tambora or artist audio. The figure and its engraving remain with the journal. [Compás score](../src/score.json) · [Original rhythm and stroke key](https://resonancias.uc.cl/wp-content/uploads/sites/13/2014/09/Hutchinson.pdf#page=7)

Listen to the Folkways collection co-produced by Hutchinson for contrasting typical ensembles. In the co-producer's notes, the recording examples distinguish older small-group perico ripiao from later accordion-led arrangements. Listen for the relation between bass support and the audible stick/head contrast, then for the places where a player leans across the grid. [Folkways field recordings and listening notes](https://folkways.si.edu/magazine-fall-2009-midnight-serenade-dominican-republic/caribbean-world/music/article/smithsonian)

## Derecho and maco for a solo player

The Folkways notes describe derecho as a four-beat cycle, with an older
two-part practice in típico ensembles. They describe maco as a shorter
two-beat pattern used in both típico and orchestra settings, including
riff-based mambo sections. Raquejo Domínguez prints separate tambora
and güira examples and connects the second derecho pattern to faster
jaleos. Macea Mateo describes a segunda example that changes the fourth
beat. The examples belong to particular teaching accounts and the
Compás version follows Raquejo's stroke key and printed rhythm.
[Folkways booklet, p. 2](https://folkways-media.si.edu/docs/folkways/artwork/SFW40547.pdf) ·
[Raquejo Domínguez, pp. 26–30](https://repository.udistrital.edu.co/bitstreams/986ed78a-7f84-4d2a-ae2e-2cb639779fd5/download#page=26) ·
[Macea Mateo, pp. 29–31](https://repositorio.unphu.edu.do/bitstream/handle/123456789/4177/El%20fraseo%20del%20saxof%C3%B3n%20en%20el%20merengue%20dominicano-%20Nayade%20Georgina%20Macea%20Mateo.pdf?sequence=1#page=31)

Raquejo's primera and maco examples mark their second bar for repeat.
Primera's repeat opens with a left-hand slap where the entry bar starts
with a right-hand head stroke. Maco's repeat opens on the rim where its
entry begins on the head. Osorio Barrera and Bonelo García write the
same distinctions with a different symbol key and cut-time grouping.
Reading each key separately gives the same hands, strokes and relative
onsets. The ledger records the repeating bars, with sixteen positions
per 4/4 bar and two bars per Compás phrase. The segunda güira uses the
first example bar from Raquejo's figure 11. The separately labelled
Juan Luis Guerra example remains a listening and score reference.
[Osorio Barrera and Bonelo García, pp. 42–44](https://repository.udistrital.edu.co/server/api/core/bitstreams/90cab3cb-00e4-4c74-b5ff-9ea47573dc7d/content#page=52) ·
[Source and stroke ledger](../repertoire/merengue-derecho-maco.json)

In **Compas-Merengue.html**, slots 4–6 are **Derecho Primera**,
**Derecho Segunda** and **Merengue a lo Maco**. Turn PRESETS during
playback to cue the next slot. The two derecho settings share a 158 BPM
starting tempo so the jaleo change can land in time. Maco starts at 168
BPM and uses the segunda güira as a documented pairing. All three leave
the lead open for a singer or live accordion or saxophone. The synthetic
voices distinguish stick head, hand head, rim and hand slap. The güira
keeps its onsets and printed accents in a single synthetic scrape voice.
Stroke direction and note length remain interpretation choices for a live
güirero. The pitched root/fifth bass, tempos and Lift pickup are authored
Compás parts. Break leaves rim and güira. The source diagrams and
recordings remain external references.
[Prepared set](../repertoire/merengue-audition-set.json)

The 73.5-second strict FM-1 emulator rehearsal verified all seven
slots, a live cue from Maco into the 2/4 *Juana Quilina* score and a
silent coda. Chromium verified the seven slots and the catalog search
exposed the three new title-level references. The 184-second full-load
run on the same image verified all fifteen 727 lanes, nineteen rhythm
cues and save and virtual reboot. Its 63,300 full audio callbacks
peaked at 891.042 µs. Peak IRQ time was 891.333 µs plus a 10 µs
allowance against a 2,901.333 µs deadline. Both strict runs had zero
missed deadlines, late DMA halves or rail samples. Static RAM was
22,516 bytes. Observed supervisor stack was 304 bytes in the rehearsal
and 268 bytes under full load. Linked bounds were 356 bytes for audio
and 1,688 bytes for foreground C.

| Full-load emulator display | Compute | Transfer | Complete |
| --- | ---: | ---: | ---: |
| R1 beat | 1.731 ms | 2.995 ms | 4.207 ms |
| R2 first visible change | 2.412 ms | 17.961 ms | 32.182 ms |
| R3 full redraw | 12.664 ms | 77.401 ms | 106.368 ms |

R2 starts at debounced input acceptance. The columns are independent
emulator maxima. The storage image SHA-256 is
`a083f63bdb5bddfe4e4ef276b4474b00dac4ba3df8e335beb4652103f8616778`.

## From early fragments to published scores

In his 1939 recollection of festivities in Baní, Federico Henríquez y
Carvajal associates *Juana Aquilina* with Alfonseca and his dance orchestra.
Coopersmith's 1949 p. 21 calls the melody a fragment and attributes it to
Alfonseca. Note 72 points to Nolasco p. 60 and identifies Henríquez y
Carvajal as her singer. The bibliography identifies Nolasco's 1939 book.
The first-hand recollection and the later notation are separate witnesses.
[Henríquez y Carvajal, Clío, printed p. 115](https://catalogo.academiadominicanahistoria.org.do/opac-tmpl/files/ppcodice/Clio-1939-007-035-114-118.pdf#page=2) ·
[Coopersmith, printed p. 21](https://archive.org/download/musicmusiciansof00coop/musicmusiciansof00coop.pdf#page=33) ·
[Nolasco holding](https://bd.bnphu.gob.do/items/0ec0a36f-1ce3-4fe6-be75-9401d2564210)

The Set Builder MIDI follows the nine written bars in Cheposo's 2011
engraving of *Juana Quilina*, in 2/4 with two sharps. Both notations have
the same broad melodic contour and the held note across bars 7–8.
Coopersmith writes a dotted eighth and sixteenth at the start of bars 2
and 4, where the modern engraving writes three attacks. The final note
of bars 5 and 6 is C-sharp in the older reading and D in the modern
engraving. These are distinct readings of a fragment. A melody fragment
does not establish a paseo, jaleo or percussion pattern. The export is
melody only with editorial tempo and velocity, with the source credit and
CC BY-SA 3.0 license embedded in the MIDI. The source PNG was checked
against SHA-256 `9d02744fd9fc376f956314470eaaa07db1601ca31b34a6d665679df734cde228`.

The Compás merengue rehearsal ends with the same nine-bar melody on the
score synth at 108 BPM. It preserves the 2/4 bar lengths and the held note
across bars 7–8. The score slot is unaccompanied. PRESETS cues it from
the separate Pambiche study, so the change is audible without implying
a surviving arrangement. The MIDI and the encoded score data carry the
engraving credit and CC BY-SA 3.0 terms.

A separate lead in Centro León’s FL03394 typescript names Nolasco’s
*Vibraciones en el tiempo* and identifies Henríquez y Carvajal as the
person who sang the version to her. The Dominican National Library
catalogs the 1948 Montalvo edition. Its notation remains to be checked
against both the 1939 citation and Coopersmith’s printed example.
[Centro León typescript](https://mediateca.centroleon.org.do/docs/FL03394.pdf) ·
[1948 National Library record](https://catalogo.bnphu.gob.do/cgi-bin/koha/opac-detail.pl?biblionumber=30949)

[Modern engraving and license](https://commons.wikimedia.org/wiki/File:Juana_Quilina_Partitura..png) ·
[Transcription ledger](../repertoire/juana-quilina-fragment.json)

Coopersmith cites an early *Ecos del Cibao* imprint from 1918, published
in Santiago de los Caballeros by La Cuna de América, Roques Román hermanos.
In an interview reproduced by Tejada, Julio Alberto Hernández recalls
collecting a short melody with García and García using it in a 1919
rondó. Hernández recalls using their collected theme in *Querer, querer
y querer* in 1928 and registering that piece with Emilio A. Morel in
1930. The interview and the imprint citation describe different source
objects. The original imprint and registration remain to be examined.
[Coopersmith, printed p. 57](https://archive.org/download/musicmusiciansof00coop/musicmusiciansof00coop.pdf#page=69) ·
[Hernández interview reproduced by Tejada, printed p. 14](https://repositorio.unphu.edu.do/bitstream/handle/123456789/4176/El%20nacionalismo%20sinf%C3%B3nico%20dominicano-%20Dr.%20Manuel%20Antonio%20Tejada%20Tabar.pdf#page=17)

For a complete chart, Hernández's *Santiago* is a lead to pursue in his
early *Álbum Musical*. Coopersmith cites vol. I, Tipografía del Carmen,
Santo Domingo, 1927 in note 152, and later
writing identifies a piano score with paseo, merengue and jaleo. Check
the actual title and music pages before treating that form as verified.
The Archivo General de la Nación holds *Santiago* on side B of
Orquesta de Nilo Menéndez's Columbia 3081-X, collection 0262, signatura
3507340. The Díaz-Ayala discography adds matrix W 96308 and dates it
circa 1926–28 in New York. The AGN entry is undated. Keep the recording,
printed composition and subsequent arrangements on separate rights
records. [AGN disc](https://colecciones.agn.gob.do/opac/ficha.php?codopac=OUAUD&idpag=0232501428&informatico=00272834SR&suposi=11) ·
[Díaz-Ayala, p. 65](https://latinpop.fiu.edu/seccion04mpt2.pdf#page=65) ·
[Coopersmith, p. 31](https://archive.org/download/musicmusiciansof00coop/musicmusiciansof00coop.pdf#page=43) ·
[Piano edition lead](https://www.diariolibre.com/opinion/lecturas/metamorfosis-del-merengue-EBDL187762)

### Juan Francisco García’s piano merengues

The Real Academia de Bellas Artes de San Fernando catalogs the 1948
Santo Domingo university print of García’s *Suite de Impresiones*,
M-3054, in the Pérez Casas legacy. The record describes a 16-page piano
score and identifies *Merengue* as movement VI. Helen Ninoska Thrall
cites that 1948 print and analyzes the finale on pp. 24–25. Figure 24 is
identified as mm. 10–21. Her reading describes paseo, cuerpo and jaleo
and relates some of the piano rhythms to the tambora. The archival
record verifies the print and title. The full movement pages remain to
be checked against the performance.
[Academy print record](https://www.academiacolecciones.com/musica/inventario.php?id=M-3054) ·
[Thrall’s 2018 thesis and edition citation](https://doi.org/10.17077/etd.zffi-977w) ·
[University recording, track 17](https://doi.org/10.25820/bath-kh57)

Thrall analyzes *Yo me Enceleré* separately on pp. 10–11. Figure 6 pairs
the piano excerpt at mm. 17–24 with two comparative tambora patterns
from Socrates García’s research. She describes a slow paseo, a syncopated
cuerpo and a jaleo coda. The recording project lists *Yo me Enceleré*
in the September 2018 session and the suite in April–May. Both pieces
are concert piano treatments. Their treatment of a section boundary can
inform a conductor rehearsal. A drum-machine lane would need its own
percussion source. Set Builder lists each piano piece as a reference
while the full score and reuse basis are being established.
[Thrall, printed pp. 10–11 and 30–32](https://doi.org/10.17077/etd.zffi-977w) ·
[University recording, track 5](https://doi.org/10.25820/7tja-nr40)

### Juangomero and Juan Gomero

Acento publishes an image credited to Tommy García and the Museo de la
Música Dominicana. The image reads *Juan Gomero* and shows a first
ensemble-score page with many staves, recurring figures and several
lower staves resting. Its 500 × 324 preview is enough to identify the
score page. The arranger, edition date, full form and relation to any
recording remain open. Preview SHA-256
`4bb06061b67e47de2367f706ee38d6f90a3882c0c983602aa5d3e44628008f87`.
[Acento score credit and competing oral accounts](https://acento.com.do/cultura/el-dia-que-jose-marti-bailo-merengue-en-montecristi-8856793.html) ·
[Museum score preview](https://ssl.c.photoshelter.com/img-get/I00002DlhWcTRApE/s/500/I00002DlhWcTRApE.jpg)

Pérez de Cuello and Solano cite p. 49 of Arzeno's 1927 book. They describe
his Juangomero as a separate merengue type whose written accents and
syncopations differ from the pambiche they describe, although their
example caption groups it with apambichado. The later spread distinguishes
Arzeno's transcription from another rhythmic reading. Hutchinson reports
that none of three Coopersmith field recordings labelled juangomero had
a pambiche-like tambora rhythm. Use the three bodies of evidence to
compare variants. A shared title or dance name leaves the actual tambora
part open. The existing Compás Pambiche follows Rafaelito Román's cell.
[Pérez de Cuello and Solano, pp. 285, 288–289](https://es.scribd.com/document/25085652/EL-MERENGUE-DOMINICANO-1) ·
[Hutchinson, p. 148, note 10](https://resonancias.uc.cl/n-39/entangled-rhythms-on-a-conflicted-island-digging-up-the-buried-histories-of-dominican-folk-music-en/?lang=en)

The BNPHU digital catalog marks its 1927 copy as request-only. The
AGN independently catalogs the same imprint as Tomo I, classification
780.97293 A797d. BNPHU separately catalogs the AGN’s 2025 facsimile of
Tomo I as xvi plus 148 pages, ISBN 9789945251432. The holdings give two
routes to the original and an identified reprint. Align the pagination
to the original before using the facsimile to locate p. 49.
[BNPHU 1927 copy request](https://bd.bnphu.gob.do/items/f5bd53c4-5830-431f-bfcb-e29cefd53791) ·
[AGN 1927 holding](https://colecciones.agn.gob.do/opac/ficha.php?codopac=OP003&idpag=1802024183&informatico=00085523PI) ·
[BNPHU facsimile record](https://catalogo.bnphu.gob.do/cgi-bin/koha/opac-detail.pl?biblionumber=60784)

The AGN identifies two orchestra witnesses. Colonial LA 1031 side B
credits J. F. García and features Arcadio Franco with Orquesta
Generalísimo Trujillo. Karen KLP-19 A5 is Rafael Solano's 1975 recording
with Vinicio Franco and an arrangement credited to Luis Pérez. The
catalogue credits apply to their respective records. Neither record has
been matched to the score page. Set Builder exposes the two records
and the score routes as references. To prepare a playable edition,
obtain Arzeno's pp. 49–51 and the museum score's credit and remaining
music pages, then compare the melodic and bass phrasing and document
reuse for the particular setting.
[Colonial LA 1031, AGN 0360](https://colecciones.agn.gob.do/opac/ficha.php?codopac=OP003&idpag=0029461263&informatico=00274076SR) ·
[Karen KLP-19, AGN 0148](https://colecciones.agn.gob.do/opac/ficha.php?codopac=OP003&idpag=0554454789&informatico=00270111SR) ·
[Arzeno, BNPHU/723](https://bd.bnphu.gob.do/items/f5bd53c4-5830-431f-bfcb-e29cefd53791)

### Archive routes for complete charts

Raquejo Domínguez's 2018 thesis supplies three score routes. Annex 4
opens and closes with written alto saxophone and accordion, with piano
figures and chord slashes in *El Diente de Oro*. The Frontera Collection
holds El Ciego de Nagua's cassette on Producciones Jose Luis JL-063,
side A, track 1. Annex 5's opening *Cabo e’ Vela* page labels the
Ventura version and writes the saxophone pickup with the piano and bass
at rest. The Dominican AGN holds a different 78, Alberti LA-1026 side B,
credited to Toño Abreu and his quartet. Annex 6 opens *La Faldita* with
Cocoband credited above the score and shows seven instrument staves.
The three chart routes are now searchable in Set Builder. Their music
and recordings remain references pending identification and permissions.
[Score annexes, pp. 81, 96 and 123](https://repository.udistrital.edu.co/bitstreams/986ed78a-7f84-4d2a-ae2e-2cb639779fd5/download#page=81) ·
[Frontera JL-063](https://frontera.library.ucla.edu/recordings/el-diente-de-oro-0) ·
[Dominican AGN 0674](https://colecciones.agn.gob.do/opac/ficha.php?codopac=OP003&idpag=0029461263&informatico=00277395SR)

For older material, the Dominican National Library holds José Dolores
Cerón's 1947 *Canciones dominicanas antiguas*, a voice-and-piano
anthology. Its catalog describes the arrangements. It takes a separate
source check to identify earlier witnesses for individual tunes. The
Library of Congress also lists two 1942 autograph merengue scores,
Ramón Díaz's *Isabel de Torres* and Rafael Ignacio's *Sábado de gloria*.
Those are catalog leads. Music pages and rights remain to be checked.
[Cerón holding, BNPHU/107](https://bd.bnphu.gob.do/items/8295665e-9fa3-4372-9776-14c9ddb88f2d) ·
[Library of Congress, Dominican composer manuscripts](https://guides.loc.gov/latin-american-composers-primary-sources/dominican-republic)

The Dominican National Library lists Julio Arzeno's *Del folk-lore musical
dominicano* (1927), handle BNPHU/723, through its copy-request route. The AGN
lists its 2025 facsimile as volume 556, ISBN 978-9945-25-143-2. Inspect the
merengue pages for the extent and identity of any notation before assigning
them to a title. The National Library also describes seven full-instrument
arrangements in Carlos Batista Matos's *Historia y evolución del merengue*
(1999), handle BNPHU/2038. Its restricted PDF is a route to the contents and
part pages. Which titles and editions it contains still needs verification.
[Arzeno original holding](https://bd.bnphu.gob.do/items/f5bd53c4-5830-431f-bfcb-e29cefd53791) ·
[AGN facsimile](https://colecciones.agn.gob.do/opac/ficha.php?codopac=OUDIG&idpag=2109229893&informatico=00281605PI) ·
[Batista Matos holding](https://bibliotecadigital.bnphu.gob.do/items/840edf8a-8661-4b1a-8120-75fa1dc9ecd7)

## Orquesta, combo and the jaleo

Centro León holds a one-page typescript, FL03401, attributed to Hernández
and identified as p. 6 of his 1927 *Álbum Musical*. He describes the second
of two different merengues in that collection without naming the piece.
The suggested form starts with a moderate eight-bar paseo repeated. An
eight- or sixteen-bar allegretto jaleo leads into the melodic section. The
jaleo returns before a trio that varies rhythm and key, then again before
a quicker coda. He describes anticipated bass in the accompaniment and
gives the recurring jaleo to the saxophones in the orchestration. The
extract verifies Hernández's description. The title and notes of its
example still require the music pages. The AGN also catalogs an undated
LP compilation with the name *Álbum musical*, a separate source object
from the 1927 piano print.
[Hernández prose extract](https://mediateca.centroleon.org.do/docs/FL03401.pdf) ·
[AGN LP holding](https://colecciones.agn.gob.do/opac/ficha.php?codopac=OPPUB&idpag=1211539214&informatico=00269553SR)

### An Altamira performance with a paseo

In the Fradique Lizardo archive, Edis Sánchez describes **Angelita de mi
vida** from Altamira, Puerto Plata. The paseo uses eight bars of 4/4, with
accompanying eighth notes and an accent on beat one. The voice enters
without an intervening tremolo. Sung primera leads into an instrumental
segunda, then the singer returns. Accordion, güira and tambora accompany
the performance, whose shortened last bar has three beats. Sánchez
compares the sung melody with Ñico Lora's *El Telefonema*. That comparison
leaves authorship open. Centro León credits Francisco Javier Vargas
Heredia for the volume's transcriptions. Item-specific notation and
recording playback remain unverified in this pass, so the catalog records
the curator's analysis and keeps the audio and any notation reference-only.
[Altamira archive and linked field recording](https://centroleon.org.do/archivo-sonoro-fradique-lizardo/#:~:text=ANGELITA%20DE%20MI%20VIDA) ·
[2022 archive credits](https://centroleon.org.do/archivo-sonoro/)

For a solo performer's chart, the two sources imply different cues.
Hernández's written model calls for returning to the same jaleo from more
than one section and entering a faster closing passage. The Altamira
performance calls for a cue from the instrumental bridge back to the
singer and a stop at a shortened final bar. Verify those boundaries on the
specific arrangement before using them in a named preset.

### Orchestral source parts

The Dominican nomination gives Luis Alberti's **Compadre Pedro Juan** as an example of the older three-part merengue form. That makes it a useful test case for how Compás might cue a paseo, a principal section, a jaleo that can run under a live solo, and a closing signal. The CUNY Dominican music project describes how the jaleo's repetitions and director-cued ending work, and follows the ensemble changes from larger orchestras to Johnny Ventura's smaller combo. Each cue still needs a match to the specific arrangement before using the song title on a slot. [Dominican nomination](https://ich.unesco.org/doc/src/32118.pdf) · [CUNY Dominican music project](https://dominicanmusicusa.com/genres/merengue/35)

FIU's Libertad Lamarque collection, Díaz-Ayala Box 6 C2, identifies a **Compadre Pedro Juan** file as FISI000854_00001. FIU credits Alberti as composer and Horace Diaz as arranger, lists both 1936 and 1955, and describes a printed Peer set and a separate handwritten set. It lists drums, piano, double bass, winds, strings and harp. The catalog is the authority for that part inventory. The MARC export assigns 1955 to the New York Peer imprint and lists 1936 separately. The only exposed file in that export is a handwritten harp cue sheet in cut time. It marks A and B, repeats and a fade instruction. It gives navigation cues for the harp player and leaves the rhythm-section parts to be located. The Sher C lead sheet, the FIU sets and the recorded versions remain separate sources. [FIU catalog record](https://dpanther.fiu.edu/record/65287?ln=es) · [FIU MARC record](https://dpanther.fiu.edu/record/65287/export/xm?ln=es) · [Harp cue sheet](https://dpanther.fiu.edu/record/65287/files/Arpa.jpg) · [Sher C edition](https://www.halleonard.com/product/240138/the-latin-real-book) · [U.S. Copyright Office term guidance](https://www.copyright.gov/circs/circ15a.pdf)

## New York and merenhouse

Proyecto Uno grew out of Lower East Side dance and restaurant gigs.
CUNY follows Nelson Zapata from playing güira in school to collaborating
with Pavel de Jesús, whose work at Quad Studios connected him to Frankie
Knuckles and David Morales. CUNY places the resulting fusion alongside
Sandy y Papo and Fulanito. Its genre survey describes maco tambora played
with synthesizers and drum machines. Zapata remembers trying the idea
on de Jesús’s Macintosh. He brought a Ramón Orlando 12-inch with a
percussion break, and de Jesús sequenced the passage and made him a
cassette. The track, software and drum machine model remain unidentified
in that account.
[CUNY’s Proyecto Uno archive](https://dominicanmusicusa.com/educational_resources/proyecto-uno/221)
· [CUNY merengue study](https://dominicanmusicusa.com/genres/merengue/35)
· [Zapata’s account](https://www.vice.com/es/article/la-historia-de-proyecto-uno-contada-por-uno-de-sus-fundadores-nelson-zapata/)

For a listening comparison, Set Builder indexes **Todo el Mundo
(Everybody)** and **El Tiburón** separately. CUNY’s group history dates
the debut album to 1990 in its text, while the archive cover caption and
Apple listing say 1991. Its account connects the song to Black Box.
A later J&N edition supplies separate production and composition credits.
CUNY places **El Tiburón** on the 1993 *In Da House*. These are recording
references. The underlying compositions, the adaptations and individual
masters each have separate rights.
[CUNY album and track references](https://dominicanmusicusa.com/educational_resources/proyecto-uno/221)
· [Digital debut listing](https://music.apple.com/us/album/todo-el-mundo/20846002)
· [J&N edition credits](https://www.qobuz.com/es-es/album/todo-el-mundo-proyecto-uno/nf094pi57stea)

Ilegales provides a second set of 1990s listening references. The
label-supplied credits on the Ariola album listing identify Vladimir
Dotel as writer and producer of *La Morena*. On *Fiesta Caliente
(Original)* the same listing credits Dotel, Pavel de Jesús and Cholo
Brenes as producers. Sony’s 2010 *Mis Favoritas* lists a different
*Fiesta Caliente (Party Mix)*, credits the remix to de Jesús and gives
that track a 1998 phonogram date. Keep the cuts separate when comparing
production. The credits connect de Jesús’s work across the two groups
and leave the sequencer, sound sources and drum-machine models open.
[Ariola album tracks 1–2](https://www.qobuz.com/es-es/album/ilegales-los-ilegales/0743213241623) ·
[Sony compilation track 4](https://www.qobuz.com/us-en/album/mis-favoritas-ilegales/0884977590708)

Alejandro Grullón Rivera’s UNPHU production study describes a common
Dominican studio workflow from practitioner interviews. The rhythm
section recorded first, with tambora, conga, güira, bass and piano.
A spoken guide marked breaks. Horns, backing vocals and lead followed.
The study names Alesis ADAT and TASCAM DA-88 as multitrack recording
formats used during the shift to digital. Those machines tell us how
the session was captured. The spoken guide also provides a documented
comparison for Compás’s cue countdown and fill into a part.
[Production study, printed p. 39](https://repositorio.unphu.edu.do/bitstream/handle/123456789/5885/Equipos%2C%20t%C3%A9cnicas%20y%20procesos%20de%20produccion%20del%20merengue%20de%20los%2090%E2%80%99s.pdf?isAllowed=y&sequence=1)

The study also discusses sequencing, sound modules and samples. Its
retrospective timeline places the Roland MC-50 among equipment arriving
in 1987. Roland’s own MC-50 manual carries a 1990 copyright. The date
and model for the earlier Dominican sessions remain open.
[Production study, printed pp. 42–43](https://repositorio.unphu.edu.do/handle/123456789/5885) ·
[Roland MC-50 manual](https://cdn.roland.com/assets/media/pdf/MC-50_MC-50MKII_UG.pdf)

### Play the three layers

Choose **Merenhouse** in the rehearsal hub. Play **Maco Percussion**, then
turn PRESETS to bring in **Maco and Club Kick**, then the full
**Merenhouse** with bass. All three run at 128 BPM with straight Feel.
Each change lands on the phrase. The tambora stick head, rim and hand
slap and the güira use the same repeated maco study onsets from Raquejo’s
figures 7 and 11. The quarter-note kick, offbeat root-and-fifth bass,
128 BPM tempo, Lift pickups and Break arrangement are Compás choices.
Break leaves kick, rim and güira. The kick and bass each have their own
Mix lane. The voices are synthesized. This study plays no master audio
or notes from either referenced song.
[Traditional cell ledger](../repertoire/merengue-derecho-maco.json)
· [Three-slot rehearsal](../repertoire/merenhouse-rehearsal-set.json)
· [Browser and strict board checks](evidence/merenhouse-offline-20261107.json)

The Musicnotes listing MN0041992 gives a separate Peer-administered 1955
lead sheet for voice or C instrument, in G major at half note 100. It
describes a one-page melody, lyrics and chord chart. Tejada Tabar’s 2020
thesis, printed p. 14, captions a melody fragment reproduced from
Incháustegui’s *Por amor al arte*. The repository’s extracted image
description reads 1936 on the manuscript while the surrounding prose
places an airing in 1930. The direct figure image was unavailable, so
both dates remain attached to their respective statements. The thesis’s
CC BY-NC-ND notice applies to the thesis as described by UNPHU. It
supplies no documented permission to encode Alberti’s music.

The official January 2026 renewal dataset, parsed record `3_960914`,
links `RE0000157738` dated 28 January 1983 to `EU0000391837`. The
separate title index identifies the linked record as *Compadre Pedro
Juan*. Musicnotes credits Peer as the lead-sheet administrator. The
specific arrangement and permission for Compás still require separate
evidence. [Musicnotes edition](https://www.musicnotes.com/sheetmusic/luis-alberti/compadre-pedro-juan/MN0041992) ·
[UNPHU thesis record](https://repositorio.unphu.edu.do/handle/123456789/4176) ·
[Copyright Office renewal dataset](https://www.copyright.gov/economic-research/usco-datasets/) ·
[Title index](https://www.copyrightencyclopedia.com/bailando-con-lola-w-and-m-luis-f-alberty-luis-f-alberti-que/)

The Dominican Archivo General de la Nación provides a separate
rhythm-section route for **Merengue del soldado**. Museo de la Música
file `8-189`, signatura `516132`, identifies a 14-page undated
Famous Music Corporation set in F, with voice, piano, bass and drums
alongside strings and winds. The catalog credits Julio A. Hernández
with music and arrangement and Emilio A. Morel with words. Inspect the
cover and those four parts to identify the printed edition and compare
it with the later anthology transcription. [AGN parts record](https://colecciones.agn.gob.do/opac/ficha.php?codopac=OP003&idpag=1345084862&informatico=00221020EX&suposi=16)

In the catalog, preserve recording, composition, arrangement, score scan and proposed device use as separate entries. The next archival step for **Compadre Pedro Juan** is to inspect both FIU sets and locate the tempo, ensemble breaks and rhythm-section pages. Match one set to a named performance where the evidence allows. Retain the exact pages and verify their bar order for the private build.

## Editable conga accompaniment · 6 November follow-through

Two [source-backed conga parts](MERENGUE-CONGA-BANK.md) now extend the 727 bank.
They retain the published two-bar basic/variation distinction and identify the
727 articulation compromises. This supports ensemble playing without assigning
a conga-only source to derecho, maco or jaleo. The linked note records the new
notation sources, inspected pages and remaining stroke-key/access gaps.
