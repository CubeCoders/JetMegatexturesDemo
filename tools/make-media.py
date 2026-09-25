"""Encode native captures; requires FFmpeg on PATH (no Python packages)."""
import argparse, hashlib, json, subprocess
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('capture',type=Path,help='Directory written by chapel_capture')
parser.add_argument('--ffmpeg',default='ffmpeg')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
out=root/'docs/media';out.mkdir(parents=True,exist_ok=True)
def run(*words):
    subprocess.run([args.ffmpeg,'-hide_banner','-loglevel','error','-y',*map(str,words)],check=True)
run('-i',args.capture/'chapel-8.ppm', '-frames:v','1',out/'chapel.png')
run('-f','rawvideo','-pixel_format','rgb24','-video_size','480x320','-framerate','30',
    '-i',args.capture/'chapel.rgb','-filter_complex',
    'fps=12,split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=3',
    '-loop','0',out/'chapel.gif')
manifest={
    'source':'chapel_capture (native Jet, ESP32 visual configuration)',
    'size':[480,320],'rgb565':True,'half_width':True,'alternating_fields':True,
    'overlay':False,'filter':'dynamic cached bilinear / nearest on miss',
    'capture_clock':'nominal 60 fields/s, paired to 30 frames/s; not S3 timing',
    'fill_allowance':'deterministic 64 rows / at most one tile per field; no host wall-time budget',
    'warmup':'all preceding tour fields rendered, including gaps between selected clips',
    'screenshot_seconds':479/60,
    'gif_tour_segments_seconds':[[4,8],[20,24],[46,50]],'gif_fps':12,
    'upstream':'https://github.com/lambertjamesd/n64brew2023',
    'upstream_revision':'8841ddf3e7d591af17287391f4b3b8728064c7bf',
    'licence':'../../LICENSE.n64brew2023',
    'files':{p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
             for p in [out/'chapel.png',out/'chapel.gif']}}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print(json.dumps(manifest['files'],indent=2))
