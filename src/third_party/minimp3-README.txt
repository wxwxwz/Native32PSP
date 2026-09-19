minimp3: https://github.com/lieff/minimp3
Source: upstream minimp3.h downloaded 2026-09-19. CC0; see minimp3-LICENSE.
Built with MINIMP3_ONLY_MP3 and MINIMP3_NO_SIMD for PSP.
Local change: explicit iscf array bound in the short-band gain loop; valid band counts are unchanged.
Vendored header SHA256: 3ebf6234ce6c13454ae29a9124febe624177ca8fe06e26764c4d749b578c8197
PSP hardware compatibility reference: https://github.com/hrydgard/ppsspp/blob/master/Core/HLE/sceMp3.cpp (sceMp3Init notes the non-MPEG-1 rejection).
