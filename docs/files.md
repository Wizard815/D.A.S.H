# Files

DASH keeps files on its microSD card and can share them over its own Wi-Fi.
It's built in: a **Files** tab, a Home card and a row in Links.

## On DASH

- **Shared / Not shared** shows top right on Files, and the status bar says
  "sharing". While shared, everyone on DASH-Share can see every file in `/share`.
- **QR code**: one tap from Files or from the Home card's QR button. It turns
  sharing on if it was off. Three codes, picked at the top:
  - **Wi-Fi**: join DASH-Share.
  - **Guest page**: the page where people view, save and upload.
  - **Admin**: the same page as the owner, where you can delete files, turn
    uploads on or off and stop sharing. The code carries a key that's new
    each time you share, so only scan it on your own phone. Stopping sharing
    ends the old key.
- **New upload**: when a guest uploads, the Home card says "New upload: name"
  and Files shows it; tap it to view it.
- **View**: photos and videos one at a time. Swipe, or tap the left or right
  side, to move. Tap the middle of a video to play or pause.
  - Photos: baseline JPEG, scaled to fit. Progressive JPEGs can't be decoded
    here (DASH says so); PNG isn't supported on the device.
  - Video: **MJPEG** only, no sound, about 10 to 12 frames a second. This chip
    can't decode MP4. Convert on a computer:
    `ffmpeg -i clip.mp4 -vf "scale=320:-2,fps=12" -q:v 7 -an clip.mjpeg`
  - MP4 and other videos are still shared and play fine on a phone (below).

## On a phone

The phone does the decoding, so MP4, MOV and full-size photos play normally.
Tap a photo or video to open the viewer, then swipe left or right to go
to the next one without going back to the list. **Save** downloads a file.

Each file gets a small thumbnail that the phone makes itself, one file at a
time so DASH isn't slowed down. For a photo it reads only the first 64 KB and
uses the preview the camera saved inside the JPEG. For a video it grabs a frame
near the start. The phone keeps thumbnails, so they don't load again. A photo
with no saved preview that is over 1.5 MB shows its type (JPG) instead.
Guests see and play everything too; only the owner page can delete.
DASH sends about 0.5 MB/s shared by everyone: one person can stream a 720p
clip; for 1080p or several viewers, Save first. Files are only served over
DASH-Share, except to the owner key, which also works from the home network.
DASH answers Range requests, so a phone can seek in a video and start it
before it has loaded. The DASH phone app (planned) uses the same `/list` and
`/f?n=<name>` routes.

## Use it

1. Put in a FAT32 microSD card. Files live in `/share` on the card.
2. Open **Files** and tap **Share**. DASH starts a `DASH-Share` Wi-Fi with a
   new password each time and shows a QR code to join.
3. Tap **Show page link** for the second code, or open `http://192.168.4.1`.
4. On the page people view, download and upload. **Uploads: on/off** on DASH
   controls uploads.

Rules: guests can't delete, and an upload never replaces a file
(`photo.jpg` becomes `photo (2).jpg`). Names are cleaned to letters, digits,
spaces and `.-_()`. Delete files by putting the card in a computer for now.
Sharing stops after 30 minutes with no one connected.

## Sync to the Truck Share

DASH's share is the simple version. In the truck, DASH copies its files to
the Truck Share on Atlas (copyparty on the Pi), which has the fuller viewer,
search and folders. People in the truck use that one.

- It runs by itself when DASH is on the truck Wi-Fi and isn't sharing.
- One file at a time, a few KB per loop, so the screen stays smooth. Files
  shows "To Atlas: trail.mp4 40% (3 left)", then "All 12 files are on Atlas".
- Each file goes once. A list on the card (`/.dash-atlas-synced`) remembers
  what's sent; a file with a new size is sent again. Delete that list to send
  everything again.
- DASH never deletes anything on Atlas, and deleting a file on DASH leaves the
  Atlas copy.

Set the password of copyparty's `dash` account in `secrets.h`:

```cpp
#define DASH_ATLAS_SHARE_PW "..."
```

On the Pi, `dash` needs write-only access to a `/dash` folder (see
A.T.L.A.S `brain/copyparty.conf`). DASH sends `PUT /dash/<name>` to port 3923
on the truck router with the password in a `PW` header. Change the host, port
or folder with `DASH_ATLAS_SHARE_HOST`, `DASH_ATLAS_SHARE_PORT` and
`DASH_ATLAS_SHARE_DIR`.

## Limits

- About 0.5 MB/s and up to 4 people at once: fine for photos, PDFs and music,
  slow for video.
- A download or a video stream holds the screen while it sends (links move to
  their own task in roadmap M2).
- The viewer lists up to 120 photos and videos.
- The share Wi-Fi uses the same channel as the network DASH is on.

## What goes wrong

| You see | Fix |
| --- | --- |
| "No SD card" | card missing or not FAT32. Reformat as FAT32 (cards over 32 GB need a FAT32 tool), put it back, open Files again |
| "Didn't upload: the SD card is full" | DASH keeps 1 MB free; make room on a computer |
| Page says "DASH isn't answering" | the phone left DASH-Share (phones drop Wi-Fi with no internet); rejoin |
| Old QR code doesn't join | each share gets a new password; scan the new code |
| "Atlas: wrong password for the dash account" | `DASH_ATLAS_SHARE_PW` doesn't match copyparty, or `dash` can't write to `/dash` |
| "Atlas: Truck Share isn't answering" | copyparty isn't running, or the Pi is off; DASH tries again in 5 minutes |
