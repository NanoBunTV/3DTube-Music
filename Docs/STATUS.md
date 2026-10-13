# Status, known issues and roadmap

## Hardware test results (New 3DS)

| Date | Build | Result |
|---|---|---|
| 2026-10-13 | first fork build | App opens, search works, audio plays. SponsorBlock toggle was hard to find (moved to Settings > Playback) |
| 2026-10-13 | music UI build | Search + play, album/artist/playlist pages, next/prev/shuffle/repeat, queue tab : all work. Home feed was empty (fixed : now YouTube Music shelves). Songs skipped after exactly 1 minute (fixed : visionOS client + resume on cut-off) |
| 2026-10-13 | client / home-feed fix build | Songs play past 1 minute (visionOS fix works). Home feed shows YouTube Music shelves. Sign-in not yet tested |
| — | lyrics / icon / translations / clean-up build | **Not yet tested** |
| 2026-10-13 | (owner report) | Sign-in works, but there was no way to see personal playlists → Library tab added |
| 2026-10-13 | library build | Library tab : playlists and Liked Music work. The 1-hour token refresh test was skipped (owner's call; the code path is only exercised after an hour) |

Pressing HOME stops audio. That's standard for 3DS applications, not a bug.

## Test checklist for new builds

1. Play a song for more than 2 minutes : it must not skip at ~1 minute
2. Home tab shows YouTube Music shelves; tapping a song plays it, tapping an album/playlist opens it
3. Search (all 4 filters), album / playlist / artist pages, Play / Shuffle buttons
4. Next / previous, shuffle, repeat off / all / one, end of queue → radio continues
5. Queue tab : tap to jump, hold to remove
6. SponsorBlock skip notice on a music video with a non-music intro
7. Signed in : songs still play the full length
8. Lyrics tab : a song (from the Songs search filter) shows lyrics and "Source: ..."; a music video shows
   "Lyrics not available"; changing tracks while the tab is open updates it
9. HOME menu : new icon, banner and banner sound; app name "3DTube Music"
10. Settings > Display/UI > UI Language : each language shows no `[SR Not Found]`, tab labels fit
11. History (overlay menu) : played songs appear; tapping one plays it
12. Tracks start at least as fast as before (one request per track now instead of four)
13. Library tab (Home, press R or tap) : signed out shows the sign-in hint; signed in shows Liked Music and
    your playlists; opening them lists songs, scrolling loads more (Liked Music pages are 20 songs), Play /
    Shuffle work; private playlists open
14. (Optional, skipped so far) after over an hour in the app, the Library / playlists still load (token refresh)
15. Sign out / in from Settings, go back to Home : the Library tab updates

## Known issues / limitations

- Azahar emulator : doesn't work for testing this app (white screens, even after the owner set it up with
  their 3DS system files; the app's main loop and audio did run). **Testing is done on hardware only.**
- Untested on Old 3DS.
- When signed in, songs that need sign-in use authenticated ANDROID_VR, whose streams get cut off. The player
  resumes after a cut-off up to 3 times per song, so those songs may stutter or stop.
- Lyrics are plain text (not synced). Music videos usually have no lyrics on YouTube Music.
- Translations were written without native-speaker review.
- Some network/status messages (e.g. "Network error", loading status on the top screen) are English only.
- The Library lists YouTube playlists (YouTube Music shows the same ones), so playlists of videos appear too.
  Saved albums and subscribed artists aren't shown : YouTube Music's library API rejects the app's TV sign-in
  (see YOUTUBE_API.md). The owner decided not to add them through the TV (TVHTML5) API.
- Liked Music has no cover image in the Library list.
- `/@handle` channel links aren't supported in the search box (only `/channel/UC...` links open the artist page).

## Roadmap

1. Resume where you left off (save the queue and position on exit)
2. Synced lyrics (needs a different client than WEB_REMIX)
3. Re-enable the updater once GitHub releases exist (`UPDATE_RELEASES_API_URL` in `source/definitions.hpp`)
