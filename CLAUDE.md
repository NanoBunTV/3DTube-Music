# 3DTube Music

Homebrew YouTube Music client for the Nintendo 3DS, based on FourthTube (GPLv3). Audio only.

Read `Docs/README.md` first. It indexes the project's documentation and memory: history and decisions
(`Docs/PROJECT.md`), architecture (`Docs/ARCHITECTURE.md`), YouTube API notes and known breakages
(`Docs/YOUTUBE_API.md`), building and testing (`Docs/BUILDING.md`), and status/roadmap (`Docs/STATUS.md`).

Rules:
- Never run `git commit` or `git push`, and don't stage changes. The owner commits and pushes.
- Build on Windows with `.\build.ps1` (the project path contains spaces, which GNU make can't handle).
- Update the Docs when something important changes, especially YouTube-side breakages and their fixes.
