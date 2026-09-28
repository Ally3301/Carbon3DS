# SHPM palette-offset fix

Hardware screenshots exposed a decoder bug rather than a sampler bug.
For indexed Zeebo SHPM formats 0x73 and 0x76, the `aux` header field is not the palette byte offset. The palette begins immediately after the 16-byte SHPM texture header. The old decoder used `block[aux:...]`, which for MUSTANG67_HEADLIGHT (aux=8) literally read half of the texture header as palette entries; this produced the red/grey static seen on headlights, carbon hoods and spoilers.

The extractor now uses offset 16 for those palettes and computes indexed payload offsets from there. All 43 vehicle texture sets and global materials/wheels were regenerated from the original Zeebo sources.

MUSTANG67 UPGRADE02 BODY/BASE is also filtered at asset-build time, leaving body kits 00 and 01 only.
