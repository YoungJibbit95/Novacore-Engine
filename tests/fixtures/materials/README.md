These two images were generated for NovaCore's material decoder tests. They do
not contain third-party artwork or game content.

- `checker.png`: 2x2 RGBA, top row red (alpha 255), green (128); bottom row
  blue (64), white (0). The test verifies exact channels and straight alpha.
- `solid.jpg`: 2x2 RGB (80, 120, 160), JPEG quality 95 with no chroma
  subsampling. The test allows a three-value compression tolerance per channel.

Both fixtures were encoded with Pillow. No runtime Pillow dependency is added.
