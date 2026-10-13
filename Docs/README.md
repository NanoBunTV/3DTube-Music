# 3DTube Music documentation

Developer documentation and project memory. Start here when picking the project back up, whether you're a
person or an AI assistant in a new chat.

| File | What's in it |
|---|---|
| [PROJECT.md](PROJECT.md) | What the project is, its history, the decisions made so far and why, and the owner's working preferences |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Source layout, scenes, threads and locking, how playback and the queue work |
| [YOUTUBE_API.md](YOUTUBE_API.md) | The YouTube / YouTube Music endpoints used, which clients work, and known breakages |
| [BUILDING.md](BUILDING.md) | Toolchain, build steps, the Windows path-with-spaces workaround, testing, regenerating the icon/banner |
| [STATUS.md](STATUS.md) | What's been tested on hardware, known issues, and the roadmap |
| [tools/](tools/) | Helper scripts : `probe_player_clients.py` (which player clients serve full streams), `check_strings.py` (missing / unused interface strings per language), `probe_library.py` (which signed-in library requests work; you sign in on google.com/device) |

Keep these files up to date when something important changes. In particular, record YouTube-side breakages
and their fixes in YOUTUBE_API.md, since they recur.
