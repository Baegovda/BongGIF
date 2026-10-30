"""WebP Clip 로직 이식.

원작자 허락을 받았다는 저장소 주인의 진술에 따라 포팅했다.
라이선스 문구는 확인되지 않았다. 외부로 데이터를 보내는 주소는 포함하지 않는다.
"""
from .core import (EFFECTS, new_project, new_mask, interp, ranges, output_frames,
                   frame_duration, envelope, find_events, snap_fps, fit_candidates)
