Unmodified `stb_image.h` 2.30 from the official nothings/stb repository,
commit `2c980bb59875b0d32144a71867fbdebb2f77cd20` (2026-08-02).

Upstream: https://github.com/nothings/stb
Pinned source: https://github.com/nothings/stb/blob/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h
License: the included dual license; NovaCore uses the MIT alternative.

The repository remains maintained; the image header's latest change at this
pin is 2024-05-31. Only PNG/JPEG memory decoding is compiled in one private
translation unit. No upstream source changes, private image codec, filesystem
decoder callbacks or build-time network fetch are added. The wrapper validates
MIME/signature, encoded size, dimensions and pixel budget before decoding.

SHA-256 of the unchanged upstream files:

- `stb_image.h`: `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`
- `LICENSE`: `bebfe904b14301657e4e5d655c811d51fd31b97c455b9cc2d8600d6bac6cff63`
