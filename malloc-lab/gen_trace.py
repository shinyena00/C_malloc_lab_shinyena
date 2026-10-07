#!/usr/bin/env python3
"""
malloc lab 속도 측정용 트레이스 생성기

사용법:
    python3 gen_trace.py            # traces/ 안에 4개 생성
    python3 gen_trace.py --seed 7   # 다른 시드로 생성
    ./mdriver -V -f traces/speed-bigrand.rep

생성되는 트레이스
  speed-bigrand   : random-bal을 크게 키운 버전. 크기 다양 + 살아 있는 블록 많음
  speed-small     : 16~128바이트 위주. free 리스트가 길게 유지됨
  speed-lifetime  : 작은 블록은 오래 살고, 큰 블록은 금방 해제됨
  speed-realloc   : 위 패턴 + 일부 블록이 realloc으로 조금씩 커짐

트레이스 형식 (mdriver.c read_trace 기준)
  1행 suggested heap size (안 쓰임)
  2행 id 개수        (마지막 id + 1 과 정확히 같아야 함: assert)
  3행 요청 줄 수
  4행 weight         (안 쓰임)
  이후 a <id> <size> / f <id> / r <id> <size>
모든 블록은 끝에서 해제한다(-bal 트레이스와 동일).
MAX_HEAP 20MB 안에 들어오도록 살아 있는 바이트 총량을 LIVE_CAP으로 제한한다.
"""
import argparse
import math
import os
import random

LIVE_CAP = 8 * 1024 * 1024   # 살아 있는 payload 합 상한 (8MB, 단편화 여유 포함)


class Trace:
    def __init__(self):
        self.ops = []
        self.live = {}        # id -> size
        self.live_ids = []    # 랜덤 선택용
        self.pos = {}         # id -> live_ids 내 위치
        self.next_id = 0
        self.live_bytes = 0

    def alloc(self, size):
        i = self.next_id
        self.next_id += 1
        self.ops.append(f"a {i} {size}")
        self.live[i] = size
        self.pos[i] = len(self.live_ids)
        self.live_ids.append(i)
        self.live_bytes += size
        return i

    def free(self, i):
        self.ops.append(f"f {i}")
        self.live_bytes -= self.live.pop(i)
        # O(1) 삭제: 마지막 원소와 자리 바꾸기
        p = self.pos.pop(i)
        last = self.live_ids.pop()
        if last != i:
            self.live_ids[p] = last
            self.pos[last] = p

    def realloc(self, i, size):
        self.ops.append(f"r {i} {size}")
        self.live_bytes += size - self.live[i]
        self.live[i] = size

    def random_live(self, rng):
        return self.live_ids[rng.randrange(len(self.live_ids))]

    def free_all(self, rng):
        ids = list(self.live_ids)
        rng.shuffle(ids)
        for i in ids:
            self.free(i)

    def write(self, path):
        with open(path, "w") as f:
            f.write(f"{LIVE_CAP * 2}\n{self.next_id}\n{len(self.ops)}\n1\n")
            f.write("\n".join(self.ops))
            f.write("\n")
        return len(self.ops)


def log_uniform(rng, lo, hi):
    """작은 크기가 더 자주 나오도록 로그 스케일에서 균등 추출"""
    return int(math.exp(rng.uniform(math.log(lo), math.log(hi))))


def gen_bigrand(rng, n_ops=60000, target_live=10000):
    """random-bal 확장판: 8~8192 로그 균등, 살아 있는 블록 ~target_live 근처 유지"""
    t = Trace()
    while len(t.ops) < n_ops:
        n = len(t.live_ids)
        p_alloc = 0.9 if n < target_live * 0.5 else (0.5 if n < target_live else 0.2)
        size = log_uniform(rng, 8, 8192)
        if (rng.random() < p_alloc or n == 0) and t.live_bytes + size < LIVE_CAP:
            t.alloc(size)
        else:
            t.free(t.random_live(rng))
    t.free_all(rng)
    return t


def gen_small(rng, n_ops=80000, target_live=20000):
    """작은 객체 위주(16~128, 실제 프로그램에서 흔한 크기). 해제가 섞여 free 리스트가 길다"""
    sizes = [16, 24, 32, 40, 48, 56, 64, 72, 96, 128]
    weights = [20, 15, 15, 10, 10, 8, 8, 5, 5, 4]
    t = Trace()
    while len(t.ops) < n_ops:
        n = len(t.live_ids)
        p_alloc = 0.85 if n < target_live else 0.35
        if rng.random() < p_alloc or n == 0:
            base = rng.choices(sizes, weights)[0]
            t.alloc(base - rng.randrange(0, 8))   # 정렬 경계에 딱 맞지 않게 약간 흔듦
        else:
            t.free(t.random_live(rng))
    t.free_all(rng)
    return t


def gen_lifetime(rng, n_ops=60000):
    """작은 블록은 오래 살고(가끔만 해제), 큰 블록은 잠깐 쓰고 바로 해제"""
    t = Trace()
    short = []   # 수명이 짧은 큰 블록들
    while len(t.ops) < n_ops:
        r = rng.random()
        if r < 0.45:
            t.alloc(log_uniform(rng, 8, 256))                # 오래 사는 작은 블록
        elif r < 0.70:
            if t.live_bytes < LIVE_CAP - 65536:
                short.append(t.alloc(log_uniform(rng, 1024, 32768)))
        elif r < 0.92 and short:
            i = short.pop(rng.randrange(len(short)))         # 큰 블록은 곧 해제
            t.free(i)
        elif t.live_ids:
            i = t.random_live(rng)                           # 가끔 아무거나 해제
            if i in short:
                short.remove(i)
            t.free(i)
    t.free_all(rng)
    return t


def gen_realloc(rng, n_ops=60000, target_live=6000):
    """다양한 크기 + 일부 블록이 realloc으로 점점 커지거나 줄어듦"""
    t = Trace()
    while len(t.ops) < n_ops:
        n = len(t.live_ids)
        r = rng.random()
        if n == 0 or (r < 0.45 and n < target_live):
            t.alloc(log_uniform(rng, 8, 2048))
        elif r < 0.75:
            # mdriver.c의 realloc 검사(`newp[j] != (index & 0xFF)`)는 newp가 signed char라서
            # id 하위 바이트가 128 이상이면 데이터가 멀쩡해도 "did not preserve" 에러가 난다.
            # 그래서 realloc은 하위 바이트가 0~127인 id에만 건다.
            i = t.random_live(rng)
            for _ in range(8):
                if (i & 0x80) == 0:
                    break
                i = t.random_live(rng)
            if i & 0x80:
                continue
            old = t.live[i]
            if rng.random() < 0.8:
                new = min(old + rng.randrange(1, max(2, old // 2 + 16)), 65536)
            else:
                new = max(1, old // 2)
            if t.live_bytes + (new - old) < LIVE_CAP:
                t.realloc(i, new)
        else:
            t.free(t.random_live(rng))
    t.free_all(rng)
    return t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=213)
    ap.add_argument("--out", default="traces")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    gens = [
        ("speed-bigrand", gen_bigrand),
        ("speed-small", gen_small),
        ("speed-lifetime", gen_lifetime),
        ("speed-realloc", gen_realloc),
    ]
    for name, g in gens:
        rng = random.Random(f"{args.seed}-{name}")
        t = g(rng)
        path = os.path.join(args.out, name + ".rep")
        n = t.write(path)
        print(f"{path}: {n} ops, {t.next_id} ids")


if __name__ == "__main__":
    main()