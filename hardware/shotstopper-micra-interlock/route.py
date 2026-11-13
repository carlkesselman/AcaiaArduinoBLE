"""Small two-layer grid router (A* with 45-degree moves, vias, isolation domains).

  python route.py BUILD_DIR          (needs numpy, scipy, pillow)

Reads BUILD_DIR/pads.json from build_board.py, writes BUILD_DIR/routes.json.
Each net only routes inside its own domain (logic / machine) from design.domain(),
so the two sides stay separated except across K1, U2 and PS1.
"""
import heapq, json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D

OUT = sys.argv[1]
RES = 0.05
NX, NY = int(D.BOARD_W / RES) + 2, int(D.BOARD_H / RES) + 2
LAYERS = ("F", "B")
MARGIN = 0.07                      # raster error allowance
VIA_COST = 60                      # in cells (3 mm)
TURN_COST = 2.0                    # per 45-degree turn, in cells

pads = json.load(open(os.path.join(OUT, "pads.json")))
net_names = sorted(D.NETS)
nid = {n: i for i, n in enumerate(net_names)}

xs = np.arange(NX) * RES
ys = np.arange(NY) * RES
X, Y = np.meshgrid(xs, ys)

# ---- board interior: distance to outline ----------------------------------------
W, H, R = D.BOARD_W, D.BOARD_H, D.CORNER_R
d_edge = np.minimum.reduce([X, W - X, Y, H - Y])
for cx, cy, sx, sy in ((R, R, -1, -1), (W - R, R, 1, -1), (W - R, H - R, 1, 1)):
    corner = ((X - cx) * sx > 0) & ((Y - cy) * sy > 0)
    d_edge = np.where(corner, R - np.hypot(X - cx, Y - cy), d_edge)

# ---- copper ownership maps (-1 free, -2 keep-out) ---------------------------------
copper = {L: np.full((NY, NX), -1, np.int32) for L in LAYERS}
smd_own = np.zeros((NY, NX), bool)   # no vias inside SMD pads


def raster_poly(pts):
    img = Image.new("1", (NX, NY), 0)
    ImageDraw.Draw(img).polygon([(x / RES, y / RES) for x, y in pts], fill=1, outline=1)
    return np.array(img, bool)


pad_cells = []   # (net, {layer: mask}, center)
for k, p in enumerate(pads):
    if not p["net"]:
        # unused pin: an obstacle for every net (unique owner id)
        for L, pts in p["layers"]:
            copper[L][raster_poly(pts)] = 10000 + k
        continue
    masks = {}
    for L, pts in p["layers"]:
        m = raster_poly(pts)
        copper[L][m] = nid[p["net"]]
        masks[L] = m
        if len(p["layers"]) == 1:
            smd_own |= m
    pad_cells.append((p["net"], masks, (p["x"], p["y"]), p["ref"] + "." + p["pin"]))

for hx, hy in D.MOUNTING_HOLES:
    ko = np.hypot(X - hx, Y - hy) < D.MOUNT_KEEPOUT_R
    for L in LAYERS:
        copper[L][ko] = -2

# ---- domains ------------------------------------------------------------------------
dom = {}
for L in LAYERS:
    f = np.vectorize(lambda x, y: {"L": 1, "M": 2, None: 0}[D.domain(x, y, L)])
    dom[L] = f(X, Y)


def net_domain(n):
    return 1 if n in D.LOGIC_NETS else 2


tracks, vias = [], []
DIRS = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)]


def stamp_segment(L, n, x1, y1, x2, y2, w):
    img = Image.new("1", (NX, NY), 0)
    dr = ImageDraw.Draw(img)
    r = w / 2 / RES + 1
    dr.line([(x1 / RES, y1 / RES), (x2 / RES, y2 / RES)], fill=1, width=int(math.ceil(w / RES)) + 1)  # conservative
    for (x, y) in ((x1, y1), (x2, y2)):
        dr.ellipse([x / RES - r, y / RES - r, x / RES + r, y / RES + r], fill=1)
    m = np.array(img, bool)
    copper[L][m & (copper[L] == -1)] = nid[n]


def stamp_via(n, x, y):
    m = np.hypot(X - x, Y - y) <= D.VIA_D / 2 + RES
    for L in LAYERS:
        copper[L][m & (copper[L] == -1)] = nid[n]


def route(n, src_masks, dst_masks, dst_center):
    """A* from any cell of src (dict layer->mask) to any cell of dst."""
    w = D.TRACK_W.get(n, D.DEFAULT_TRACK_W)
    me = nid[n]
    free = {}
    for L in LAYERS:
        other = (copper[L] != -1) & (copper[L] != me)
        dist = ndimage.distance_transform_edt(~other) * RES
        ok = (dist >= w / 2 + D.CLEARANCE + MARGIN) & (d_edge >= D.EDGE_CLEARANCE + w / 2)
        ok &= (dom[L] == net_domain(n)) | (copper[L] == me)
        ok |= src_masks.get(L, False) | dst_masks.get(L, False)
        free[L] = (ok, dist)
    via_ok = np.ones((NY, NX), bool)
    for L in LAYERS:
        via_ok &= free[L][1] >= D.VIA_D / 2 + D.CLEARANCE + MARGIN
        via_ok &= dom[L] == net_domain(n)
    via_ok &= d_edge >= D.EDGE_CLEARANCE + D.VIA_D / 2
    via_ok &= ndimage.distance_transform_edt(~smd_own) * RES > D.VIA_D / 2 + 0.1
    fl = [free["F"][0], free["B"][0]]
    goal = [dst_masks.get("F", np.zeros((NY, NX), bool)), dst_masks.get("B", np.zeros((NY, NX), bool))]
    gx, gy = dst_center[0] / RES, dst_center[1] / RES

    def h(i, j):
        dx, dy = abs(i - gx), abs(j - gy)
        return max(dx, dy) + 0.414 * min(dx, dy)

    INF = 1e18
    best = {}
    heap = []
    for li, L in enumerate(LAYERS):
        m = src_masks.get(L)
        if m is None:
            continue
        for j, i in zip(*np.nonzero(m)):
            key = (li, j, i, 8)
            best[key] = 0
            heapq.heappush(heap, (h(i, j), 0.0, li, j, i, 8, None))
    parent = {}
    found = None
    expansions = 0
    while heap:
        f, gcost, li, j, i, d, par = heapq.heappop(heap)
        key = (li, j, i, d)
        if key in parent:
            continue
        parent[key] = par
        expansions += 1
        if goal[li][j, i]:
            found = key
            break
        for nd, (di, dj) in enumerate(DIRS):
            ii, jj = i + di, j + dj
            if not (0 <= ii < NX and 0 <= jj < NY) or not fl[li][jj, ii]:
                continue
            step = 1.414 if di and dj else 1.0
            if d != 8:
                t = min((nd - d) % 8, (d - nd) % 8)
                if t > 2:
                    continue
                step += TURN_COST * t
            nk = (li, jj, ii, nd)
            ng = gcost + step
            if ng < best.get(nk, INF):
                best[nk] = ng
                heapq.heappush(heap, (ng + h(ii, jj), ng, li, jj, ii, nd, key))
        if via_ok[j, i]:
            ol = 1 - li
            if fl[ol][j, i]:
                nk = (ol, j, i, 8)
                ng = gcost + VIA_COST
                if ng < best.get(nk, INF):
                    best[nk] = ng
                    heapq.heappush(heap, (ng + h(i, j), ng, ol, j, i, 8, key))
    if not found:
        return None, expansions
    path = []
    k = found
    while k is not None:
        path.append(k)
        k = parent[k]
    path.reverse()
    return path, expansions


def emit(n, path):
    w = D.TRACK_W.get(n, D.DEFAULT_TRACK_W)
    pts = [(li, i * RES, j * RES) for li, j, i, d in path]
    # split into runs per layer, simplify collinear points
    run = [pts[0]]
    out = []

    def flush(run):
        if len(run) < 2:
            return
        simp = [run[0]]
        for k in range(1, len(run) - 1):
            a, b, c = simp[-1], run[k], run[k + 1]
            if abs((b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1])) > 1e-9:
                simp.append(b)
        simp.append(run[-1])
        for a, b in zip(simp, simp[1:]):
            L = LAYERS[a[0]]
            tracks.append((n, L, round(a[1], 3), round(a[2], 3), round(b[1], 3), round(b[2], 3), w))
            stamp_segment(L, n, a[1], a[2], b[1], b[2], w)

    for p in pts[1:]:
        if p[0] != run[-1][0]:
            flush(run)
            vias.append((n, round(p[1], 3), round(p[2], 3)))
            stamp_via(n, p[1], p[2])
            run = [p]
        else:
            run.append(p)
    flush(run)


def net_masks(n):
    """Current copper of net n, per layer."""
    return {L: copper[L] == nid[n] for L in LAYERS}


# ---- routing order --------------------------------------------------------------------
ORDER = (["RIB8_P", "RIB12_P", "RIB8_B", "RIB12_B", "PADDLE", "BRAIN",
          "GND", "VIN", "+3V3", "SENSE", "STOP", "K1_A", "COMMON"]
         + [f"RIB{k}" for k in (16, 15, 14, 13, 11, 10, 9, 7, 5, 4, 3, 2, 1)]
         + ["U2_K", "U2_A", "ISO5V"])
ORDER = [n for n in ORDER if n in D.NETS] + [n for n in D.NETS if n not in ORDER]
if len(sys.argv) > 2:
    ORDER = sys.argv[2].split(",") + [n for n in ORDER if n not in sys.argv[2].split(",")]

failed = []
for n in ORDER:
    pins = [pc for pc in pad_cells if pc[0] == n]
    if len(pins) < 2:
        continue
    connected = [pins[0]]
    rest = pins[1:]
    while rest:
        # nearest unconnected pad to the connected tree (by centre distance)
        best_k = min(range(len(rest)), key=lambda k: min(math.dist(rest[k][2], c[2]) for c in connected))
        tgt = rest.pop(best_k)
        # tree = all copper of this net except pads not yet connected
        src = net_masks(n)
        for other in rest + [tgt]:
            for L, m in other[1].items():
                src[L] = src[L] & ~m
        if any((ndimage.binary_dilation(src[L]) & m).any() for L, m in tgt[1].items()):
            connected.append(tgt)
            continue
        path, ex = route(n, src, tgt[1], tgt[2])
        if path is None:
            print(f"FAIL {n} -> {tgt[3]} ({ex} expansions)", flush=True)
            failed.append((n, tgt[3]))
        else:
            emit(n, path)
            print(f"ok   {n} -> {tgt[3]} ({ex} exp, {len(path)} cells)", flush=True)
        connected.append(tgt)

json.dump({"tracks": tracks, "vias": vias, "failed": failed}, open(os.path.join(OUT, "routes.json"), "w"))
print("tracks", len(tracks), "vias", len(vias), "failed", len(failed))
