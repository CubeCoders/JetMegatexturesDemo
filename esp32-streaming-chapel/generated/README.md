# Asset provenance

The chapel geometry, UVs, material layout and baked textures derive from
James D. Lambert's [n64brew2023](https://github.com/lambertjamesd/n64brew2023),
revision [`8841ddf3e7d591af17287391f4b3b8728064c7bf`](https://github.com/lambertjamesd/n64brew2023/tree/8841ddf3e7d591af17287391f4b3b8728064c7bf).
The upstream [MIT notice is preserved verbatim](../../LICENSE.n64brew2023).
No separate licence files were present under the source asset directories at
that revision. This attribution also applies to screenshots and GIFs of the scene.

Sources used:

- `assets/world/test.blend`: the `@megatexture` meshes, material names, UVs and painter groups.
- `assets/materials/megatextures/*.png`: the 18 baked surface textures selected by those meshes.

CubeCoders' conversion changes the coordinate basis from Blender `(x,y,z)` to
Jet `(x,z,y)`, reverses triangle winding, uses 256 units/metre and flips texture V.
Textures are reduced to a maximum dimension of 512 pixels, quantized to
256 RGB565 palette entries per material, mipmapped, and packed into 32×32
index tiles. Smallest mips are row-major. The scene contains 108 surfaces and
325 triangles. The lighting detail is baked into the source textures.

Palette conversion uses lightness-weighted Oklab fitting with extra importance
for bright/chromatic colours, edge-aware cleanup of training data only, and
deduplicated RGB565 entries. Each whole mip is Floyd–Steinberg diffused before
tiling, using a single shared palette per source texture. Mips are generated
from the undithered RGB base. The source artwork is otherwise unchanged.
See the [palette explanation](../../README.md#perceptually-modelled-palettes-and-diffusion)
and [Oklab attribution](../../THIRD_PARTY_NOTICES.md#oklab-colour-conversion).

`scene.json` records the source Blender SHA-256 and converted geometry.
`assets.json` records each original texture path/hash, dimensions, palette error,
mip offsets and the SHA-256 of `../main/assets.bin`. `../main/ChapelAssets.hpp`
contains the generated geometry and lookup metadata. These files are included so
building firmware requires neither Blender nor the upstream repository.

Regeneration instructions are in the [main README](../../README.md#regenerating-the-assets).
Upstream audio, music, N64 binaries and tool dependencies are not distributed here.
