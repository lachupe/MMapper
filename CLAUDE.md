# Working in this MMapper fork

This is MMapper (Qt/C++ mapper for the MUD MUME) with a frontend protocol added for the 3D client
mume3d (`../mume3d`). `AGENTS.md` has the build and formatting rules; `BUILD.md` the build. The
work branch is `db/frontend-protocol-impl`; upstream is untouched in `src/map`, `src/mapstorage`
and `src/display`.

- MMapper stays the integer-grid and room-graph source of truth. Local spaces, float layout and
  everything 3D live in mume3d, never here; add derived hints only (the room `fingerprint` in the
  XML export and in `MMapper.Map.Position`, the map identity in `MMapper.Session.State`).
- MUME parsing belongs here and reaches the client as an `MMapper.*` package; the client consumes
  it with stub handlers where the use comes later.
- The protocol spec is outside git at `/home/db/mume/mmapper_frontend_protocol_spec.md`: back it
  up before editing, and document every new field in it.
- Every change here is also recorded in `../mume3d/CHANGELOG.md`, with its reason.
- Build in `build/` (Ninja, Debug); run the unit tests with `QT_QPA_PLATFORM=offscreen`; never
  launch the GUI from a session. `TestFont` fails under AddressSanitizer on this machine for a
  fontconfig leak unrelated to the fork.
- The branches `live-map-reshaping` (an integer-grid reshaper, unmerged) and the worktree
  `../MMapper-scaling` (a float-layout experiment, abandoned) are history; merging the reshaper
  into this branch would move rooms under mume3d's layout, so do not without coordinating.
- Commits carry `lachupe@gmail.com`. Other sessions may leave uncommitted work in this checkout:
  add files by name, never `git add -A`.
