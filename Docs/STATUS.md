# Status, known issues and roadmap

## Hardware test results (New 3DS)

| Date | Build | Result |
|---|---|---|
| 2026-10-13 | first fork build | App opens, search works, audio plays. SponsorBlock toggle was hard to find (moved to Settings > Playback) |
| 2026-10-13 | music UI build | Search + play, album/artist/playlist pages, next/prev/shuffle/repeat, queue tab : all work. Home feed was empty (fixed : now YouTube Music shelves). Songs skipped after exactly 1 minute (fixed : visionOS client + resume on cut-off) |
| — | client / home-feed fix build | **Not yet tested** |

Pressing HOME stops audio. That's standard for 3DS applications, not a bug.

## Test checklist for new builds

1. Play a song for more than 2 minutes : it must not skip at ~1 minute
2. Home tab shows YouTube Music shelves; tapping a song plays it, tapping an album/playlist opens it
3. Search (all 4 filters), album / playlist / artist pages, Play / Shuffle buttons
4. Next / previous, shuffle, repeat off / all / one, end of queue → radio continues
5. Queue tab : tap to jump, hold to remove
6. SponsorBlock skip notice on a music video with a non-music intro
7. Signed in : songs still play the full length

## Known issues / limitations

- Azahar emulator : the app showed only white screens on both displays. Likely cause : Azahar isn't set up
  with the owner's 3DS system files yet (its log reported missing shared font files while the app was loading
  fonts). Retry after dumping system files into Azahar.
- Untested on Old 3DS.
- The 30 new interface strings are English in every language file.
- Icon and banner are still FourthTube's.
- When signed in, songs that need sign-in use authenticated ANDROID_VR, whose streams get cut off. The player
  resumes after a cut-off up to 3 times per song, so those songs may stutter or stop.

## Roadmap

1. YouTube Music library when signed in (liked songs, saved albums, playlists). Note : the app's OAuth tokens
   are TV device-flow tokens; check whether WEB_REMIX accepts them before building on that.
2. Lyrics (YouTube Music `next` → lyrics tab → `browse`)
3. Music-specific channel/artist handling in the remaining FourthTube screens (subscriptions, history)
4. Own icon and banner
5. Translations for the new strings
6. Re-enable the updater once GitHub releases exist (`UPDATE_RELEASES_API_URL` in `source/definitions.hpp`)
