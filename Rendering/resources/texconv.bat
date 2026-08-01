.\texconv.exe -f BC7_UNORM -m 0 -y -ft dds *.png
.\texconv.exe -f BC7_UNORM -m 0 -y -ft dds *.jpg
.\texconv.exe -f BC7_UNORM -m 0 -y -ft dds *.jpeg
.\texconv.exe -f BC6H_UF16 -m 1 -y -ft dds *.hdr

REM .\texconv.exe -f BC5_UNORM -w 512 -h 512 -m 0 -y -ft dds normal_map.png
REM .\texconv.exe -f BC7_UNORM -w 512 -h 512 -m 0 -y -ft dds *.png

@echo off
REM ============================================================================
REM texconv Option Reference (Used by This Script)
REM ============================================================================
REM
REM This batch file uses texconv.exe to convert source textures into
REM GPU-compressed DDS files.
REM
REM The options documented below describe ONLY the options actually used
REM by this script, as well as notable options that are intentionally NOT used.
REM
REM ----------------------------------------------------------------------------
REM -f <format>
REM ----------------------------------------------------------------------------
REM Specifies the output GPU compression format.
REM
REM Formats used in this script (in order of importance):
REM
REM   BC5_UNORM
REM     - Used for normal maps (tangent-space).
REM     - Stores X and Y components only.
REM     - Treated strictly as linear vector data.
REM
REM   BC6H_UF16
REM     - Used for HDR color data.
REM     - Unsigned floating-point format (0 to +infinity).
REM     - Suitable for environment maps, skyboxes, and IBL textures.
REM
REM   BC7_UNORM
REM     - Used for high-quality linear color or scalar data.
REM     - Suitable for cases where BC6H is unnecessary or too expensive.
REM
REM Notes:
REM - BC1_UNORM is NOT used.
REM - All formats are treated as LINEAR data.
REM - No *_SRGB formats are used.
REM
REM ----------------------------------------------------------------------------
REM -w <width>
REM -h <height>
REM ----------------------------------------------------------------------------
REM Forces the output texture resolution.
REM
REM Effects:
REM - Resizes the input texture to the specified width and height.
REM - Aspect ratio is NOT preserved automatically.
REM
REM Usage policy in this script:
REM - Output resolution is explicitly controlled via -w / -h.
REM - No implicit resizing (e.g., -pow2) is performed.
REM
REM WARNING:
REM - Incorrect values will distort the texture.
REM - Ensure source content is authored with this resampling in mind.
REM
REM ----------------------------------------------------------------------------
REM -m <count>
REM ----------------------------------------------------------------------------
REM Specifies the number of mipmap levels to generate.
REM
REM Examples:
REM   -m 1    : No mipmaps
REM   -m 0    : Generate full mip chain
REM
REM Notes:
REM - Mipmaps are generated in LINEAR space.
REM - No gamma correction is applied during mip generation.
REM
REM ----------------------------------------------------------------------------
REM -y
REM ----------------------------------------------------------------------------
REM Overwrites existing output files without prompting.
REM
REM ----------------------------------------------------------------------------
REM Summary
REM ----------------------------------------------------------------------------
REM Key rules enforced by this script:
REM
REM - BC5_UNORM  : Normal maps (linear vector data).
REM - BC6H_UF16  : HDR color textures (linear floating-point).
REM - BC7_UNORM  : High-quality linear color or scalar textures.
REM
REM - Output resolution is always explicit (-w / -h).
REM - No automatic color space conversion.
REM - No implicit resizing or normal map processing.
REM
REM This script performs compression ONLY.
REM ============================================================================





