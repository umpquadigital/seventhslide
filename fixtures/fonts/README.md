# Test fonts

Faces the font tests load by path, so they run the same on every machine
whatever it has installed. Made by `tools/make_fixture_fonts.py` from
Liberation Sans 2.1.5, cut down to printable ASCII and renamed: a family a
machine also has would lend Qt its other faces, and the Open Font License
does not let a modified version keep the Reserved Font Name "Liberation".

| File | Family | Style | Weight | For |
|---|---|---|---|---|
| `SSFixtureSans-Regular.ttf` | SSFixture Sans | Regular | 400 | a family with a real Bold |
| `SSFixtureSans-Bold.ttf` | SSFixture Sans | Bold | 700 | |
| `SSFixtureSansSemibold-Regular.ttf` | SSFixture Sans Semibold (typographic: SSFixture Sans / Semibold) | Regular | 600 | a family named for its weight, as "Open Sans Semibold" ships |
| `SSFixturePlain-Regular.ttf` | SSFixture Plain | Regular | 400 | a family with no Bold: bold is synthesised |
| `SSFixtureWide-Regular.ttf` | SSFixture Wide | Regular | 400 | a family that looks like no other: the rest are Liberation Sans, the app's stand-in for a missing font, so a test that must see which was painted uses this, stretched to 1.4 times the width and without hinting or kerning |
| `SSFixtureWide-Bold.ttf` | SSFixture Wide | Bold | 700 | |
| `SSFixtureCollection.ttc` | SSFixture Collection | Regular, Light, Bold | 400, 300, 700 | a collection whose weights are named as styles, as a Mac's are |

Licence: SIL Open Font License 1.1, see `OFL.txt`.
