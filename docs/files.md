# Files

DASH keeps files on its microSD card and can share them over its own Wi-Fi.
It's built in: a **Files** tab, a Home card and a row in Links.

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

## Limits

- About 0.5 MB/s and up to 4 people at once: fine for photos, PDFs and music,
  slow for video.
- A download holds the screen until it finishes (links move to their own task
  in roadmap M2).
- The share Wi-Fi uses the same channel as the network DASH is on.

## What goes wrong

| You see | Fix |
| --- | --- |
| "No SD card" | card missing or not FAT32. Reformat as FAT32 (cards over 32 GB need a FAT32 tool), put it back, open Files again |
| "Didn't upload: the SD card is full" | DASH keeps 1 MB free; make room on a computer |
| Page says "DASH isn't answering" | the phone left DASH-Share (phones drop Wi-Fi with no internet); rejoin |
| Old QR code doesn't join | each share gets a new password; scan the new code |
