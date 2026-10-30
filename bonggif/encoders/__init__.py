"""형식별 내보내기. 공개 포맷 규격과 오픈소스 코덱만 사용한다.

libimagequant 는 쓰지 않는다(업스트림이 GPLv3/상용). 팔레트는 Pillow median-cut.
영상 코덱은 imageio-ffmpeg 가 제공하는 ffmpeg 를 서브프로세스로 호출한다.
라이선스 정리는 docs/FORMATS.md.
"""
from .gifenc import write_gif, stamp_gif_metadata
from .webpenc import write_webp
from .apngenc import write_apng
from .mp4enc import write_mp4
from .webmenc import write_webm
from .avifenc import write_avif, avif_available

__all__ = [
    "write_gif", "stamp_gif_metadata", "write_webp", "write_apng",
    "write_mp4", "write_webm", "write_avif", "avif_available",
]
