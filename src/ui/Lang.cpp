// Korean -> English dictionary. Exact matches first; strings built at run time (numbers glued to
// Korean fragments) fall back to replacing known fragments, longest first. Results are cached.
#include "ui/Lang.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace bg {

namespace {
int g_lang = 0;

const wchar_t* kDict[][2] = {
    // main window
    {L"전역 단축키를 등록하지 못했어요. 다른 프로그램이 쓰고 있어요.", L"Couldn't register the global hotkeys. Another program is using them."},
    {L"새 움짤", L"New GIF"}, {L"편집", L"Edit"}, {L"맞춤", L"Fit"}, {L"저장", L"Save"}, {L"프레임", L"Frames"}, {L"삭제", L"Delete"},
    {L"시간", L"Time"}, {L"줄이기", L"Reduce"}, {L"추가", L"Add"},
    {L"녹화하거나 파일을 열어 움짤을 만들어 보세요", L"Record or open a file to make an animation"},
    {L"GIF·WebP·APNG·MP4 파일이나 영상을 여기로 끌어다 놓아도 돼요", L"You can also drop GIF, WebP, APNG, MP4 or video files here"},
    {L"편집 및 효과", L"Edit & effects"}, {L"카드를 누르면 옵션이 펼쳐져요", L"Click a card to show its options"},
    {L"크기 조절 · 회전", L"Resize · rotate"}, {L"크기를 바꾸거나 돌리고 뒤집어요", L"Change size, rotate or flip"},
    {L"자르기", L"Crop"}, {L"필요한 부분만 남겨요", L"Keep only the part you need"},
    {L"글자 넣기", L"Add text"}, {L"움짤 위에 글자를 얹어요", L"Put text on the animation"},
    {L"이미지 워터마크", L"Image watermark"}, {L"로고나 사진을 겹쳐요", L"Overlay a logo or picture"},
    {L"화면 전환 효과", L"Transition"}, {L"두 프레임 사이를 부드럽게 이어요", L"Blend smoothly between two frames"},
    {L"필터 효과", L"Filters"}, {L"색감·흐림·모자이크·카툰 등 27가지", L"27 effects: colour, blur, mosaic, cartoon…"},
    {L"말풍선 · 스티커", L"Bubbles · stickers"}, {L"말풍선이나 이모지를 붙여요", L"Add speech bubbles or emoji"},
    {L"그리기", L"Draw"}, {L"마우스로 선을 그려 넣어요", L"Draw lines with the mouse"},
    {L"줌 애니메이션", L"Zoom animation"}, {L"한 곳으로 부드럽게 다가가요", L"Smoothly zoom into one spot"},
    {L"콜라주", L"Collage"}, {L"여러 움짤을 나란히 붙여요", L"Put several animations side by side"},
    {L"액자", L"Photo frame"}, {L"테두리·폴라로이드·필름 모양을 씌워요", L"Border, polaroid or film frame"},
    {L"처음 상태로 되돌리기", L"Revert to original"}, {L"불러온 직후로 돌아가요", L"Back to how it was when loaded"},
    {L"되돌리기", L"Undo"}, {L"적용 범위", L"Apply to"}, {L"전체", L"All"}, {L"선택", L"Selected"}, {L"닫기", L"Close"},
    {L"폭", L"Width"}, {L"높이", L"Height"}, {L"비율 유지", L"Keep aspect ratio"}, {L"폭을 바꾸면 높이도 같이 바뀌어요", L"Changing the width changes the height too"},
    {L"확대 방식", L"Upscaling"}, {L"초해상도는 AI 모델로 2~4배 키워요 (느려요)", L"Super-resolution enlarges 2-4x with an AI model (slow)"},
    {L"보통", L"Normal"}, {L"초해상도 ESPCN", L"Super-res ESPCN"}, {L"초해상도 FSRCNN", L"Super-res FSRCNN"}, {L"배율", L"Scale"},
    {L"2배", L"2x"}, {L"3배", L"3x"}, {L"4배", L"4x"}, {L"빠른 크기", L"Quick size"}, {L"크기 적용", L"Apply size"},
    {L"초해상도로 키우는 중", L"Upscaling with super-resolution"}, {L"배 초해상도로 키웠어요", L"x super-resolution applied"},
    {L"크기를 바꾸는 중", L"Resizing"}, {L"크기를 ", L"Resized to "}, {L"로 바꿨어요", L""},
    {L"회전 · 뒤집기", L"Rotate · flip"}, {L"왼쪽", L"Left"}, {L"오른쪽", L"Right"}, {L"좌우", L"Horizontal"}, {L"상하", L"Vertical"},
    {L"돌리거나 뒤집었어요", L"Rotated or flipped"}, {L"미리보기에서 드래그해 남길 영역을 고르세요.", L"Drag on the preview to choose the area to keep."},
    {L"비율", L"Ratio"}, {L"자유", L"Free"}, {L"영역이 너무 작아요. 8픽셀 이상으로 골라 주세요.", L"The area is too small. Choose at least 8 pixels."},
    {L"로 잘랐어요", L" crop applied"}, {L"글자", L"Text"}, {L"크기", L"Size"}, {L"색", L"Colour"}, {L"테두리", L"Outline"},
    {L"글자 둘레에 검은 선을 둘러 잘 보이게 해요", L"A black outline keeps text readable"},
    {L"미리보기를 누르면 그 자리에 글자가 들어가요.", L"Click the preview to place the text there."},
    {L"글자를 넣는 중", L"Adding text"}, {L"글자를 넣었어요", L"Text added"}, {L"이미지 파일", L"Image file"},
    {L"PNG면 투명 부분도 살아나요", L"PNG transparency is kept"}, {L"바꾸기", L"Change"}, {L"고르기", L"Choose"}, {L"불투명도", L"Opacity"},
    {L"미리보기를 누르면 그 자리에 이미지가 놓여요.", L"Click the preview to place the image there."},
    {L"워터마크 넣기", L"Add watermark"}, {L"워터마크를 넣는 중", L"Adding watermark"}, {L"워터마크를 넣었어요", L"Watermark added"},
    {L"지금 프레임과 다음 프레임 사이에 서서히 바뀌는 프레임을 넣어요.", L"Inserts frames that fade from this frame to the next."},
    {L"넣을 프레임 수", L"Frames to insert"}, {L"전체 시간 (ms)", L"Total time (ms)"}, {L"전환 넣기", L"Add transition"},
    {L"마지막 프레임이 아닌 곳을 골라 주세요.", L"Choose a frame other than the last one."}, {L"화면 전환 효과를 넣었어요", L"Transition added"},
    {L"효과", L"Effect"}, {L"고른 색", L"Picked colour"}, {L"드래그한 영역만", L"Dragged area only"},
    {L"미리보기에서 드래그한 부분에만 효과를 줘요", L"Apply only to the area dragged on the preview"}, {L"효과 적용", L"Apply effect"},
    {L" 적용 중", L" in progress"}, {L" 효과를 줬어요", L" applied"}, {L"먼저 녹화하거나 파일을 열어 주세요.", L"Record or open a file first."},
    {L"되돌렸어요", L"Undone"}, {L"다시 실행했어요", L"Redone"}, {L"고른 프레임 시간 정하기", L"Set selected frame time"},
    {L"전체 프레임 시간 정하기", L"Set all frame times"},
    {L"GIF는 50fps(20ms)보다 빠르게 재생하지 못해요. 그래도 바꿀까요?", L"GIF can't play faster than 50 fps (20 ms). Change anyway?"},
    {L"10% 빠르게 바꿨어요", L"10% faster"}, {L"10% 느리게 바꿨어요", L"10% slower"}, {L"번 프레임만 남겼어요", L" frames kept"},
    {L"프레임이 10개 이상일 때 쓸 수 있어요.", L"Needs at least 10 frames."}, {L"비슷한 프레임을 지우는 중", L"Removing similar frames"},
    {L"비슷한 프레임을 지웠어요", L"Similar frames removed"}, {L"프레임을 줄였어요", L"Frames reduced"},
    {L"영상을 요약하는 중", L"Summarizing video"}, {L"영상을 요약했어요", L"Video summarized"},
    {L"모든 프레임을 지울 수는 없어요.", L"You can't delete every frame."}, {L"개 프레임을 지웠어요", L" frames deleted"},
    {L"처음 불러온 상태로 되돌릴까요?", L"Revert to the original as loaded?"}, {L"처음 상태로 되돌렸어요", L"Reverted to original"},
    {L"열기", L"Open"}, {L"뒤에 붙일 파일", L"File to append"}, {L"영상을 열 수 없어요.", L"Couldn't open the video."},
    {L"영상을 읽는 중", L"Reading video"}, {L"파일을 읽는 중", L"Reading file"}, {L"개 프레임을 뒤에 붙였어요", L" frames appended"},
    {L"이미지 가져오기 (여러 장 고르기)", L"Import images (choose several)"}, {L"이미지를 가져오는 중", L"Importing images"},
    {L"이미지 ", L"Images "}, {L"장", L""}, {L"프레임을 불러왔어요", L" frames loaded"},
    {L"저장하지 않은 작업이 있어요. 버리고 계속할까요?", L"You have unsaved work. Discard it and continue?"},
    {L"저장했어요: ", L"Saved: "}, {L"녹화", L"Record"},
    {L"지금 움짤과 다른 파일을 나란히 붙여요. 짧은 움짤은 반복돼요.", L"Puts this animation next to other files. Shorter ones loop."},
    {L"붙일 파일", L"Files to add"}, {L"GIF·WebP·APNG·AVIF·이미지", L"GIF · WebP · APNG · AVIF · images"},
    {L"배치", L"Layout"}, {L"가로", L"Row"}, {L"세로", L"Column"}, {L"격자", L"Grid"}, {L"간격 (px)", L"Gap (px)"}, {L"바탕색", L"Background"},
    {L"콜라주 만들기", L"Make collage"}, {L"콜라주를 만드는 중", L"Making collage"}, {L"콜라주를 만들었어요", L"Collage made"},
    {L"넣을 것", L"Insert"}, {L"말풍선", L"Bubble"}, {L"스티커", L"Sticker"},
    {L"미리보기에서 말풍선 자리를 드래그해요.", L"Drag on the preview to place the bubble."}, {L"모양", L"Shape"}, {L"둥근", L"Round"},
    {L"타원", L"Oval"}, {L"생각", L"Thought"}, {L"외침", L"Shout"}, {L"글자 크기", L"Font size"}, {L"꼬리 방향", L"Tail"},
    {L"말풍선 색", L"Bubble colour"}, {L"스티커를 고르고 미리보기를 누르면 그 자리에 붙어요.", L"Pick a sticker and click the preview to place it."},
    {L"넣기", L"Insert"}, {L"넣는 중", L"Inserting"}, {L"넣었어요", L"Inserted"},
    {L"미리보기 위에 마우스로 그려요.", L"Draw on the preview with the mouse."}, {L"펜 색", L"Pen colour"}, {L"굵기", L"Width"},
    {L"형광펜", L"Highlighter"}, {L"반투명하게 칠해요", L"Semi-transparent strokes"}, {L"그린 선", L"Strokes"}, {L"개", L""},
    {L"하나 빼기", L"Remove last"}, {L"모두 지우기", L"Clear all"}, {L"그릴 프레임", L"Draw on"}, {L"지금", L"Current"},
    {L"그리기 적용", L"Apply drawing"}, {L"그리는 중", L"Drawing"}, {L"그림을 넣었어요", L"Drawing added"},
    {L"미리보기에서 확대할 곳을 드래그해요. 지금 프레임부터 움직여요.", L"Drag the area to zoom into. Starts at the current frame."},
    {L"확대하는 프레임 수", L"Zoom-in frames"}, {L"머무는 프레임 수", L"Hold frames"}, {L"다시 축소하는 프레임 수", L"Zoom-out frames"},
    {L"0이면 확대된 채로 끝나요", L"0 ends zoomed in"}, {L"줌 넣기", L"Add zoom"}, {L"줌 애니메이션을 만드는 중", L"Making zoom animation"},
    {L"줌 애니메이션을 넣었어요", L"Zoom animation added"}, {L"둥근 모서리", L"Rounded"}, {L"폴라로이드", L"Polaroid"}, {L"필름", L"Film"},
    {L"그림자 액자", L"Shadow"}, {L"부드러운 가장자리", L"Soft edge"}, {L"두께", L"Thickness"}, {L"액자 적용", L"Apply frame"},
    {L"액자를 씌우는 중", L"Adding frame"}, {L"액자를 씌웠어요", L"Frame added"},
    // side bar / pages
    {L"움짤 녹화·편집기", L"GIF recorder & editor"}, {L"녹화 시작", L"Record"}, {L"홈", L"Home"}, {L"파일 관리", L"Files"},
    {L"일괄 변환", L"Batch convert"}, {L"프레임 도구", L"Frame tools"}, {L"시간 설정", L"Frame time"}, {L"10% 빠르게", L"10% faster"},
    {L"10% 느리게", L"10% slower"}, {L"거꾸로 재생", L"Reverse"}, {L"거꾸로 재생하게 바꿨어요", L"Reversed"}, {L"요요 (왕복)", L"Yo-yo"},
    {L"되돌아오는 프레임을 붙였어요", L"Yo-yo frames added"}, {L"구간 남기기", L"Trim"}, {L"프레임 줄이기", L"Reduce frames"},
    {L"비디오 요약", L"Video summary"}, {L"설정", L"Settings"}, {L"녹화 시작·정지", L"Start/stop recording"}, {L"일시정지", L"Pause"},
    {L"무엇을 할까요?", L"What would you like to do?"}, {L"새로 녹화", L"New recording"},
    {L"화면의 원하는 부분을 움짤로 녹화해요", L"Record any part of the screen"}, {L"편집 (파일 열기)", L"Edit (open file)"},
    {L"GIF·WebP·APNG·AVIF·영상을 열어 고쳐요", L"Open and edit GIF, WebP, APNG, AVIF or video"}, {L"이미지 가져오기", L"Import images"},
    {L"여러 장의 사진을 이어 움짤로 만들어요", L"Turn several pictures into an animation"},
    {L"저장한 움짤을 보고 이름을 바꾸거나 지워요", L"Browse, rename or delete saved files"},
    {L"여러 파일을 한 번에 다른 형식으로 바꿔요", L"Convert many files to another format at once"}, {L"최근 파일", L"Recent files"},
    {L"아직 연 파일이 없어요", L"No files opened yet"}, {L"이름 바꾸기", L"Rename"}, {L"새 이름", L"New name"},
    {L"이름을 바꾸지 못했어요. 같은 이름이 있거나 쓰는 중이에요.", L"Couldn't rename. The name exists or the file is in use."},
    {L"휴지통으로 보낼까요?", L"Move to the Recycle Bin?"}, {L"완전히 지울까요? 되돌릴 수 없어요.", L"Delete permanently? This can't be undone."},
    {L"폴더에서 보기", L"Show in folder"}, {L"폴더 바꾸기", L"Change folder"}, {L"보여 줄 폴더", L"Folder to show"}, {L"새로 고침", L"Refresh"},
    {L"이름", L"Name"}, {L"날짜", L"Date"}, {L"이 폴더에 움짤이 없어요", L"No animations in this folder"}, {L"  ·  미리보기 3초", L"  ·  3 s preview"},
    {L"미리보기를 만들지 못했어요", L"Couldn't make a preview"},
    {L"여러 파일을 한 번에 다른 형식으로 바꿔요. 파일을 끌어다 놓아도 돼요.", L"Convert many files at once. You can drop files here too."},
    {L"파일 추가", L"Add files"}, {L"폴더 추가", L"Add folder"}, {L"변환할 파일이 있는 폴더", L"Folder with files to convert"},
    {L"목록 비우기", L"Clear list"}, {L"대기", L"Waiting"}, {L"변환 중", L"Converting"}, {L"완료", L"Done"}, {L"실패", L"Failed"},
    {L"파일 추가나 폴더 추가를 누르거나 파일을 여기로 끌어다 놓아요", L"Click Add files / Add folder or drop files here"},
    {L"변환 설정", L"Conversion"}, {L"형식", L"Format"}, {L"PNG 시퀀스", L"PNG sequence"}, {L"JPG 시퀀스", L"JPG sequence"},
    {L"화질", L"Quality"}, {L"이미지 크기", L"Image size"}, {L"FPS 상한", L"FPS limit"}, {L"0이면 제한 없어요", L"0 = no limit"},
    {L"원본과 같은 폴더에 저장", L"Save next to the original"}, {L"저장 폴더", L"Output folder"},
    {L"GIF·WebP 세부 설정은 설정 페이지의 GIF·WebP 항목을 따라요.", L"GIF/WebP details follow the GIF and WebP sections in Settings."},
    {L"멈추기", L"Stop"}, {L"변환 시작", L"Start"}, {L"일반", L"General"}, {L"단축키", L"Hotkeys"}, {L"고급", L"Advanced"}, {L"언어", L"Language"},
    {L"저장 위치와 기본 동작이에요", L"Save location and basic behaviour"},
    {L"저장 창과 파일 관리에서 이 폴더를 써요", L"Used by the save dialog and the file manager"},
    {L"시작할 때 녹화 창 열기", L"Open the recorder on start"}, {L"프로그램을 켜면 바로 녹화할 수 있어요", L"Ready to record right after launch"},
    {L"지울 때 휴지통으로 보내기", L"Delete to Recycle Bin"}, {L"파일 관리에서 지운 파일을 되살릴 수 있어요", L"Files deleted in the file manager can be restored"},
    {L"녹화가 끝나면 바로 편집", L"Edit right after recording"}, {L"끄면 녹화 창이 그대로 남아요", L"When off, the recorder stays open"},
    {L"녹화할 때의 동작이에요", L"How recording behaves"}, {L"기본 FPS", L"Default FPS"},
    {L"높을수록 부드럽고 파일이 커져요. 15가 무난해요", L"Higher is smoother but bigger. 15 is a good default"},
    {L"최대 녹화 시간 (초)", L"Max recording time (s)"}, {L"이 길이가 되면 저절로 끝나요", L"Recording stops at this length"},
    {L"메모리 한도 (MB)", L"Memory limit (MB)"}, {L"녹화 프레임이 이만큼 쌓이면 멈춰요", L"Recording stops when frames use this much"},
    {L"같은 화면은 합치기", L"Merge identical frames"}, {L"화면이 그대로면 프레임을 늘리지 않고 시간만 늘려요", L"Unchanged screens extend the frame time"},
    {L"녹화하는 동안 이 창 숨기기", L"Hide BongGIF from captures"}, {L"전체 화면을 녹화해도 BongGIF 창이 찍히지 않아요", L"BongGIF windows don't appear in full-screen recordings"},
    {L"창 크기 조절할 때 돋보기", L"Magnifier while resizing"}, {L"테두리를 끌면 커서 주변을 크게 보여 줘요", L"Shows the area around the cursor while dragging edges"},
    {L"Ctrl+Alt로 창 끌고 다니기", L"Move with Ctrl+Alt"}, {L"Ctrl+Alt를 누른 채 마우스를 움직이면 녹화 창이 따라와요", L"Hold Ctrl+Alt and move the mouse to drag the recorder"},
    {L"Ctrl+PrtSc로 녹화 시작·정지", L"Ctrl+PrtSc starts/stops recording"}, {L"바로 저장", L"Direct save"},
    {L"녹화하면서 바로 MP4로 저장해요. 메모리를 거의 쓰지 않아요", L"Writes MP4 while recording. Uses almost no memory"},
    {L"바로 저장 화질", L"Direct save quality"}, {L"바로 저장할 때 시간 제한 없음", L"No time limit for direct save"},
    {L"저장한 뒤 폴더 열기", L"Open folder after saving"}, {L"불러오기와 편집 기본값이에요", L"Import and editing defaults"},
    {L"이미지 가져오기 프레임 시간 (ms)", L"Imported image frame time (ms)"},
    {L"사진을 이어 붙일 때 한 장이 보이는 시간이에요", L"How long each imported picture shows"},
    {L"영상 가져오기 최대 FPS", L"Video import max FPS"}, {L"영상 가져오기 긴 변 최대 (px)", L"Video import max long side (px)"},
    {L"0이면 원본 크기예요", L"0 = original size"}, {L"버튼을 누른 뒤 원하는 키 조합을 눌러요. Esc로 취소해요", L"Click a button, then press the key combination. Esc cancels"},
    {L"키를 누르세요…", L"Press keys…"}, {L"GIF 저장 기본값이에요 (저장 창에서도 바꿀 수 있어요)", L"GIF defaults (also changeable in the save dialog)"},
    {L"색 줄이는 방식", L"Quantizer"}, {L"LIQ (기본)", L"LIQ (default)"}, {L"분산 분할", L"Variance split"}, {L"디더링", L"Dithering"},
    {L"가운데에 색 더 쓰기", L"Centre-weighted colours"}, {L"비슷한 픽셀 지우기", L"Remove similar pixels"},
    {L"256색 이하면 그대로 저장", L"Keep exact colours when ≤256"}, {L"파일에 프로그램 이름 남기기", L"Write program comment"},
    {L"반복 횟수 정하기", L"Limit loops"}, {L"끄면 끝없이 반복해요", L"Off = loop forever"}, {L"반복 횟수", L"Loop count"},
    {L"무손실", L"Lossless"}, {L"손실·무손실 자동 판단", L"Auto lossy/lossless"}, {L"아이폰 호환", L"iPhone compatible"},
    {L"끄면 바뀐 부분만 저장해 훨씬 작아져요", L"Off stores only changed areas: much smaller"}, {L"화질 우선", L"Quality first"},
    {L"코덱", L"Codec"}, {L"VP9가 더 작고, VP8은 오래된 기기에서도 잘 돌아가요", L"VP9 is smaller; VP8 plays on older devices"},
    {L"색 압축", L"Colour compression"},
    {L"MP4·WebM·AVIF·손실 WebP에서 비슷한 색을 묶어 파일을 5~20% 줄여요", L"Groups similar colours in MP4/WebM/AVIF/lossy WebP: 5-20% smaller"},
    {L"APNG는 화질 손실 없이 저장해요. 바뀐 부분만 저장해 크기를 줄여요.", L"APNG is lossless and stores only changed areas to stay small."},
    {L"AVIF 화질은 AV1 화질로 바뀌어요 (예: 80 → 74). 74 이상이면 4:4:4, 50~73은 4:2:2, 그 아래는 4:2:0으로 저장해요.",
     L"AVIF quality maps to AV1 quality (e.g. 80 → 74). 74+ saves 4:4:4, 50-73 4:2:2, lower 4:2:0."},
    {L"DXGI 캡처", L"DXGI capture"}, {L"HDR 화면도 제 색으로 녹화해요. 문제가 생기면 꺼 주세요", L"Records HDR screens with correct colours. Turn off if it misbehaves"},
    {L"JPG 시퀀스 화질", L"JPG sequence quality"}, {L"화면 언어", L"Display language"}, {L"한국어", L"한국어"},
    // dialogs
    {L"정하는 방식", L"Set by"}, {L"FPS로", L"FPS"}, {L"초로", L"Seconds"}, {L"초당 프레임", L"Frames per second"},
    {L"모든 대상 프레임에 같은 시간이 들어가요", L"Every target frame gets the same time"}, {L"프레임 시간 (초)", L"Frame time (s)"},
    {L"0.02초보다 짧게는 정할 수 없어요", L"Can't be shorter than 0.02 s"}, {L"시간을 숫자로 입력해 주세요.", L"Enter the time as a number."},
    {L"고른 구간만 남기고 앞뒤 프레임은 지워요.", L"Keeps the chosen range and deletes the rest."}, {L"시작", L"Start"},
    {L"번 프레임", L""}, {L"끝", L"End"}, {L"짝수 번째", L"Even frames"}, {L"홀수 번째", L"Odd frames"}, {L"N번째마다", L"Every Nth"},
    {L"비슷한 프레임", L"Similar frames"}, {L"2, 4, 6…번째 프레임을 지워요", L"Deletes frames 2, 4, 6…"},
    {L"1, 3, 5…번째 프레임을 지워요", L"Deletes frames 1, 3, 5…"}, {L"앞 프레임과 거의 같은 프레임을 지워요", L"Deletes frames almost equal to the previous one"},
    {L"몇 번째마다 지울까요", L"Delete every"}, {L"예: 3이면 3, 6, 9…번째를 지워요", L"E.g. 3 deletes frames 3, 6, 9…"},
    {L"차이 허용", L"Tolerance"}, {L"다른 픽셀이 이 비율 이하면 비슷하다고 봐요", L"Similar when this share of pixels or fewer differ"},
    {L"재생 속도 유지", L"Keep playback speed"}, {L"지운 프레임의 시간을 남은 프레임에 나눠 줘서 전체 길이가 그대로예요", L"Deleted time goes to the remaining frames so length stays the same"},
    {L"요약하기", L"Summarize"}, {L"화면이 거의 안 바뀌는 구간을 줄여서 짧게 만들어요.", L"Shortens parts where the screen barely changes."},
    {L"비슷하다고 볼 기준", L"Similarity threshold"}, {L"다른 픽셀이 이 비율 이하인 프레임을 지워요", L"Deletes frames where this share of pixels or fewer differ"},
    {L"지운 시간 일부 남기기", L"Keep part of the removed time"}, {L"지운 구간 시간의 일부를 남은 프레임에 더해요", L"Adds part of the removed time to remaining frames"},
    {L"남길 비율 (%)", L"Share to keep (%)"}, {L"프레임 시간 상한", L"Frame time limit"}, {L"한 프레임이 너무 오래 머물지 않게 해요", L"Keeps a frame from staying too long"},
    {L"상한 (ms)", L"Limit (ms)"}, {L"영상 가져오기", L"Import video"}, {L"가져오기", L"Import"}, {L"시작 (초)", L"Start (s)"}, {L"끝 (초)", L"End (s)"},
    {L"긴 영상은 메모리를 많이 써요", L"Long videos use a lot of memory"}, {L"최대 FPS", L"Max FPS"}, {L"긴 변 최대 (px)", L"Max long side (px)"},
    {L"0이면 원본 크기 그대로예요", L"0 = original size"}, {L"끝 시간이 시작보다 커야 해요.", L"End must be after start."},
    {L"문제가 생겼어요", L"Something went wrong"}, {L"예", L"Yes"}, {L"확인", L"OK"}, {L"아니요", L"No"}, {L"취소", L"Cancel"},
    {L"취소하는 중이에요…", L"Cancelling…"}, {L"잠시만 기다려 주세요.", L"Please wait."},
    // recorder
    {L"전체 화면", L"Full screen"}, {L"영역 선택", L"Select area"}, {L"창 찾기", L"Find window"}, {L"커서", L"Cursor"}, {L"강조", L"Highlight"},
    {L"클릭", L"Clicks"}, {L"바로 저장 켬: 녹화가 끝나면 MP4로 바로 저장돼요", L"Direct save on: saved to MP4 when recording ends"},
    {L"바로 저장 끔", L"Direct save off"}, {L"정지", L"Stop"}, {L"원래 크기로 녹화", L"Record at full size"}, {L"절반 크기로 녹화", L"Record at half size"},
    {L"1/4 크기로 녹화", L"Record at quarter size"}, {L"위치 1로 옮기기  Ctrl+1", L"Move to position 1  Ctrl+1"},
    {L"위치 2로 옮기기  Ctrl+2", L"Move to position 2  Ctrl+2"}, {L"위치 3으로 옮기기  Ctrl+3", L"Move to position 3  Ctrl+3"},
    {L"지금 위치를 1에 저장  Ctrl+Shift+1", L"Save position 1  Ctrl+Shift+1"}, {L"지금 위치를 2에 저장  Ctrl+Shift+2", L"Save position 2  Ctrl+Shift+2"},
    {L"지금 위치를 3에 저장  Ctrl+Shift+3", L"Save position 3  Ctrl+Shift+3"}, {L"DXGI 캡처 (HDR 화면 지원)", L"DXGI capture (HDR screens)"},
    {L"녹화하는 동안 이 창 숨기기 (캡처에서 빼기)", L"Hide BongGIF from captures"},
    {L"저장된 위치가 없어요. Ctrl+Shift+숫자로 먼저 저장해요", L"No saved position. Save one with Ctrl+Shift+number"},
    {L"위치 ", L"Position "}, {L"에 저장했어요", L" saved"}, {L"최대 녹화 시간이 지나서 녹화를 멈췄어요.", L"Recording stopped: maximum time reached."},
    {L"메모리 한도에 닿아서 녹화를 멈췄어요.", L"Recording stopped: memory limit reached."}, {L"MP4로 바로 저장하지 못했어요.", L"Couldn't save the MP4 directly."},
    {L"이미지를 저장하고 복사했어요", L"Image saved and copied"}, {L"클립보드에 복사했어요 (파일 저장은 실패)", L"Copied to clipboard (saving the file failed)"},
    {L"글자를 읽는 중", L"Reading text"}, {L"읽을 수 있는 글자를 찾지 못했어요.", L"No readable text found."},
    {L"읽은 글자를 클립보드에 복사했어요.\n\n", L"Recognized text copied to the clipboard.\n\n"},
    {L"녹화할 창을 누르세요 · Esc로 취소해요", L"Click the window to record · Esc cancels"},
    {L"드래그해서 녹화할 영역을 고르세요 · Esc로 취소해요", L"Drag to choose the area to record · Esc cancels"},
    // save dialog
    {L"파일 저장", L"Save file"}, {L"저장하는 중이에요…", L"Saving…"}, {L"예상 크기 계산 중…", L"Estimating size…"},
    {L"고른 설정으로 실제로 인코딩해서 재요", L"Measured by really encoding with these settings"}, {L"예상 크기  ", L"Estimated size  "},
    {L"설정을 바꾸면 다시 계산해요", L"Recalculated when settings change"}, {L"예상 크기를 계산하지 못했어요", L"Couldn't estimate the size"},
    {L"저장 위치", L"Save to"}, {L"저장할 폴더를 고르세요", L"Choose a folder"}, {L"파일 이름", L"File name"}, {L"GIF는 최대 ", L"GIF uses up to "},
    {L"색까지 써요", L" colours"}, {L"무손실이면 압축 노력 정도로 쓰여요", L"Lossless uses it as compression effort"},
    {L"높을수록 선명하고 파일이 커져요", L"Higher is sharper but bigger"}, {L"무손실이라 화질과 관계없어요", L"Lossless: quality doesn't apply"},
    {L"AV1 화질 ", L"AV1 quality "}, {L"최소 양자화 ", L"Min quantizer "}, {L"JPG 화질이에요", L"JPG quality"},
    {L"높을수록 비트레이트가 올라가요", L"Higher means a higher bitrate"}, {L" · 파일 이름 뒤에 _0001처럼 번호가 붙어요", L" · files get numbers like _0001"},
    {L"GIF 자세히", L"GIF details"}, {L"GIF로 저장할 때만 써요", L"Only used for GIF"},
    {L"LIQ가 가장 곱고, NeuQuant·Octree는 빠르거나 다른 느낌이에요", L"LIQ looks best; NeuQuant/Octree are faster or look different"},
    {L"색이 적어도 부드럽게 보이게 점을 섞어요", L"Mixes dots so few colours still look smooth"},
    {L"화면 가운데의 색을 더 정확하게 담아요", L"Keeps colours in the centre more accurate"},
    {L"앞 프레임과 거의 같은 점은 비워 파일을 줄여요", L"Clears pixels equal to the previous frame to save space"},
    {L"화질 100일 때 색 수가 적으면 색을 줄이지 않아요", L"At quality 100, few-colour images aren't quantized"},
    {L"색 보호", L"Protected colours"}, {L"고른 색은 그대로 남겨요. 누르면 빠져요", L"Chosen colours are kept exactly. Click to remove"},
    {L"LIQ 방식에서만 쓸 수 있어요", L"LIQ only"}, {L"GIF·WebP 주석에 BongGIF와 화질을 적어요", L"Writes BongGIF and the quality into GIF/WebP comments"},
    {L"WebP 자세히", L"WebP details"}, {L"WebP로 저장할 때만 써요", L"Only used for WebP"},
    {L"화질 손실 없이 저장해요. 파일이 커질 수 있어요", L"No quality loss. Files can get big"},
    {L"영상 두 곳을 시험 인코딩해 더 작은 쪽을 골라요", L"Test-encodes two spots and picks the smaller mode"},
    {L"켜면 모든 프레임을 통째로 저장해요. 끄면 바뀐 부분만 저장해 훨씬 작아지지만 일부 아이폰에서 깨질 수 있어요",
     L"On stores whole frames. Off stores only changes: much smaller but may break on some iPhones"},
    {L"바뀐 부분만 저장할 때 블록을 투명하게 비워 화질을 지켜요", L"Keeps quality by clearing unchanged blocks when storing changes"},
    {L"WebM 자세히", L"WebM details"}, {L"WebM으로 저장할 때만 써요", L"Only used for WebM"}, {L"공통", L"Common"},
    {L"비슷한 색을 묶어 파일을 5~20% 줄여요. 화질 80 이하에서만 동작해요", L"Groups similar colours: 5-20% smaller. Works at quality 80 or lower"},
    {L"MP4·WebM·AVIF·손실 WebP에서만 써요", L"MP4, WebM, AVIF and lossy WebP only"}, {L"진행 표시 넣기", L"Progress bar"},
    {L"화면 아래에 재생 위치 막대를 그려요", L"Draws a playback bar at the bottom"}, {L"저장 후 폴더 열기", L"Open folder after saving"},
    {L"파일 용량 더 줄이기", L"Make the file smaller"}, {L"작게 줄이면 파일도 작아져요", L"Smaller images make smaller files"},
    {L"이미지 크기 상한 (px)", L"Max image size (px)"}, {L"긴 변이 이보다 크면 줄여요. 0이면 제한 없어요", L"Shrinks when the long side is bigger. 0 = no limit"},
    {L"0이면 제한하지 않아요", L"0 = no limit"}, {L"프레임 시간 상한 (ms)", L"Max frame time (ms)"},
    {L"한 장면이 너무 오래 머물지 않게 해요. 0이면 제한 없어요", L"Keeps a scene from staying too long. 0 = no limit"},
    {L"비슷한 프레임 건너뛰기", L"Skip similar frames"}, {L"거의 같은 프레임은 하나로 합쳐요 (재생 시간은 그대로)", L"Merges near-identical frames (same duration)"},
    {L"\n같은 이름의 파일이 있어요. 덮어쓸까요?", L"\nA file with this name exists. Overwrite?"},
    // effects
    {L"밝기/대비", L"Brightness/contrast"}, {L"밝기", L"Brightness"}, {L"대비", L"Contrast"}, {L"감마", L"Gamma"}, {L"흑백", L"Grayscale"},
    {L"세피아", L"Sepia"}, {L"채도", L"Saturation"}, {L"색 온도", L"Temperature"}, {L"단색(Monotone)", L"Monotone"}, {L"색조", L"Hue"},
    {L"화이트밸런스", L"White balance"}, {L"기준 흰색 사용", L"Use reference white"},
    {L"기준 흰색을 켜면 미리보기에서 흰색이어야 할 곳을 눌러요", L"With reference white on, click what should be white"},
    {L"컬러 커브", L"Colour curve"}, {L"어두운 부분", L"Shadows"}, {L"밝은 부분", L"Highlights"}, {L"비네팅", L"Vignette"}, {L"강도", L"Strength"},
    {L"선명하게", L"Sharpen"}, {L"흐리게", L"Blur"}, {L"블룸", L"Bloom"}, {L"빛남", L"Glow"}, {L"부드럽게", L"Soften"}, {L"미니어처", L"Miniature"},
    {L"흐림", L"Blur"}, {L"선명 영역%", L"Sharp band %"}, {L"아웃포커스", L"Out of focus"}, {L"선명 반경%", L"Sharp radius %"},
    {L"미리보기에서 선명하게 남길 곳을 눌러요", L"Click the spot to keep sharp"}, {L"영역 흐림", L"Spot blur"}, {L"반경%", L"Radius %"},
    {L"미리보기에서 흐리게 할 곳을 눌러요", L"Click the spot to blur"}, {L"사각 흐림", L"Box blur"},
    {L"미리보기에서 흐리게 할 영역을 드래그해요", L"Drag the area to blur"}, {L"모자이크", L"Mosaic"},
    {L"영역을 드래그하면 그 부분만 가려요", L"Drag an area to pixelate only that part"}, {L"스캔라인", L"Scanlines"}, {L"프린트", L"Print"},
    {L"망점 크기", L"Dot size"}, {L"크로마키", L"Chroma key"}, {L"허용치", L"Tolerance"}, {L"붙어 있는 곳만", L"Contiguous only"},
    {L"흰색으로 채우기", L"Fill with white"}, {L"미리보기에서 지울 색을 눌러요", L"Click the colour to remove"}, {L"노이즈 제거", L"Denoise"},
    {L"레벨", L"Level"}, {L"카툰", L"Cartoon"}, {L"지우개 (인페인트)", L"Eraser (inpaint)"},
    {L"미리보기에서 지울 부분을 드래그하면 주변 색으로 메워요", L"Drag over what to erase; it's filled from the surroundings"},
    {L"포스터화", L"Posterize"}, {L"단계", L"Levels"}, {L"반전", L"Invert"},
    // run-time fragments
    {L"프레임  ·  ", L" frames  ·  "}, {L" 프레임", L" frames"}, {L"초  ·  ", L" s  ·  "}, {L"초", L" s"}, {L"프레임을", L" frames"}, {L"번 프레임", L""},
    {L" · ", L" · "},
};

std::unordered_map<std::wstring, std::wstring>& dict() {
    static std::unordered_map<std::wstring, std::wstring> d = [] {
        std::unordered_map<std::wstring, std::wstring> m;
        for (auto& e : kDict) m.emplace(e[0], e[1]);
        return m;
    }();
    return d;
}

// fragments for strings assembled at run time, longest first
const std::vector<std::pair<std::wstring, std::wstring>>& fragments() {
    static std::vector<std::pair<std::wstring, std::wstring>> f = [] {
        std::vector<std::pair<std::wstring, std::wstring>> v;
        for (auto& e : kDict)
            if (wcslen(e[0]) >= 1) v.push_back({e[0], e[1]});
        std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first.size() > b.first.size(); });
        return v;
    }();
    return f;
}

bool hasHangul(const std::wstring& s) {
    for (wchar_t c : s)
        if (c >= 0xAC00 && c <= 0xD7A3) return true;
    return false;
}
} // namespace

void setLanguage(int lang) { g_lang = lang; }
int language() { return g_lang; }

std::wstring tr(const std::wstring& ko) {
    if (g_lang == 0 || !hasHangul(ko)) return ko;
    auto& d = dict();
    auto it = d.find(ko);
    if (it != d.end()) return it->second;
    static std::mutex mu;
    static std::unordered_map<std::wstring, std::wstring> cache;
    std::lock_guard<std::mutex> lk(mu);
    auto c = cache.find(ko);
    if (c != cache.end()) return c->second;
    // line by line, then fragment replacement
    std::wstring out;
    size_t a = 0;
    while (true) {
        size_t b = ko.find(L'\n', a);
        std::wstring line = ko.substr(a, b == std::wstring::npos ? std::wstring::npos : b - a);
        auto li = d.find(line);
        if (li != d.end()) line = li->second;
        else if (hasHangul(line)) {
            for (auto& [k, v] : fragments()) {
                if (!hasHangul(line)) break;
                size_t pos;
                while ((pos = line.find(k)) != std::wstring::npos) line.replace(pos, k.size(), v);
            }
        }
        out += line;
        if (b == std::wstring::npos) break;
        out += L'\n';
        a = b + 1;
    }
    if (cache.size() > 4000) cache.clear();
    cache.emplace(ko, out);
    return out;
}

} // namespace bg
