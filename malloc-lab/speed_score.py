#!/usr/bin/env python3
"""
speed-*.rep 트레이스로 점수 매기기

사용법 (malloc-lab 폴더에서, make 한 다음):
    python3 speed_score.py           # 처음 한 번은 libc도 재서 .libc_kops.json에 저장
    python3 speed_score.py --libc    # libc 기준을 다시 재고 싶을 때

점수 계산은 mdriver와 같은 식인데, 속도 기준만 600 Kops 대신
"같은 트레이스에서 libc가 낸 Kops"를 쓴다.
    트레이스 점수 = 60 * util + 40 * min(1, 내 Kops / libc Kops)

왜 느렸나:
  mdriver는 시간 재기 전에 정확성 검사를 하는데, 매 요청마다 살아 있는 블록
  전체와 겹치는지 리스트를 훑는다(add_range). 블록이 수만 개면 이게 대부분의
  시간을 먹는다. 그래서
    - mdriver는 트레이스당 한 번만 돌리고(시간 측정은 mdriver 안에서 이미
      여러 번 재서 가장 빠른 값들을 쓴다)
    - libc 값은 안 바뀌니까 파일에 저장해두고 재사용한다.
"""
import json
import os
import re
import subprocess
import sys

TRACES = ["speed-bigrand", "speed-small", "speed-lifetime", "speed-realloc"]
CACHE = ".libc_kops.json"

# 예: "Total          89%   65080  0.073170   889"
# Kops가 크면 secs와 붙어서 나오므로(0.000094152542) secs를 소수점 6자리로 끊어 읽는다
ROW = re.compile(r"Total\s+(\d+)%\s+(\d+)\s+(\d+\.\d{6})\s*(\d+)")


def parse(section):
    m = ROW.search(section)
    util, ops, secs, _ = m.groups()
    return int(util) / 100, int(ops) / float(secs) / 1000   # util, Kops


def run_mm(path):
    out = subprocess.run(["./mdriver", "-V", "-f", path],
                         capture_output=True, text=True).stdout
    err = [l for l in out.splitlines() if "ERROR" in l]
    if err:
        return None, err[0].strip()
    return parse(out.split("Results for mm malloc")[-1]), None


def run_libc(path):
    out = subprocess.run(["./mdriver", "-l", "-V", "-f", path],
                         capture_output=True, text=True).stdout
    sec = out.split("Results for libc malloc")[1].split("Results for mm")[0]
    return parse(sec)[1]


def load_libc(remeasure):
    cache = {}
    if not remeasure and os.path.exists(CACHE):
        with open(CACHE) as f:
            cache = json.load(f)
    changed = False
    for t in TRACES:
        if t not in cache:
            print(f"(libc 측정 중: {t})", flush=True)
            cache[t] = run_libc(f"traces/{t}.rep")
            changed = True
    if changed:
        with open(CACHE, "w") as f:
            json.dump(cache, f, indent=2)
    return cache


def main():
    libc = load_libc("--libc" in sys.argv)

    print(f"{'trace':<16}{'util':>6}{'Kops':>10}{'libc':>10}{'util점':>8}{'속도점':>8}{'합계':>7}")
    totals = []
    for t in TRACES:
        r, err = run_mm(f"traces/{t}.rep")
        if err:
            print(f"{t:<16} {err}")
            continue
        util, k = r
        u_pts = 60 * util
        t_pts = 40 * min(1.0, k / libc[t])
        totals.append(u_pts + t_pts)
        print(f"{t:<16}{util*100:>5.0f}%{k:>10.0f}{libc[t]:>10.0f}"
              f"{u_pts:>8.1f}{t_pts:>8.1f}{u_pts+t_pts:>7.1f}", flush=True)
    if totals:
        print(f"\n평균 점수: {sum(totals)/len(totals):.1f} / 100")


if __name__ == "__main__":
    main()