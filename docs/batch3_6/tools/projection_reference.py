"""Batch 3.6 - independent reference implementation of the projection chain.

Deliberately does NOT import or mirror the C++ helpers: the expected values are
derived from first principles (pinhole camera, right-handed basis, camera looking
down -Z in view space) so the production code is checked against an outside
definition rather than against itself.

Conventions used here (stated, not assumed from the game):
  world  -> rel = world - camera
  view   : x_v = right . rel,  y_v = up . rel,  z_v = -forward . rel
           so anything in front of the camera has z_v < 0
  clip   : x_c = x_v / (depth * tan(hfov/2)), same for y, depth = -z_v
  screen : sx = W/2 + (W/2) * x_c ; sy = H/2 - (H/2) * y_c

Run: python projection_reference.py
"""
import json
import math
import sys

CHECKS = []


def check(name, cond, detail=""):
    CHECKS.append((name, bool(cond), detail))
    print("  %s  %s%s" % ("PASS" if cond else "FAIL", name,
                          ("   " + detail) if detail else ""))
    return bool(cond)


# ---------------------------------------------------------------- primitives
def vec_sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def normalize(v):
    n = math.sqrt(dot(v, v))
    return tuple(x / n for x in v)


def basis_from_yaw_pitch(yaw_deg, pitch_deg):
    """Minecraft convention: yaw 0 faces +Z, yaw increases clockwise seen from
    above, pitch positive looks down."""
    y = math.radians(yaw_deg)
    p = math.radians(pitch_deg)
    forward = (-math.sin(y) * math.cos(p), -math.sin(p), math.cos(y) * math.cos(p))
    right = (-math.cos(y), 0.0, -math.sin(y))
    up = normalize(cross(right, forward))
    return forward, right, up


# ------------------------------------------------- the production formula
def oworld_to_screen(rows, origin, world, fov, display):
    """Faithful model of glmatrixf::OWorldToScreen: rows[0..2] are 4-vectors,
    and the point is already made camera-relative by the caller."""
    rel = vec_sub(world, origin)
    hom = (rel[0], rel[1], rel[2], 1.0)
    x = dot(rows[0], hom)
    y = dot(rows[1], hom)
    z = dot(rows[2], hom)
    if z > 0:
        return None, {"reason": "z>0 (behind camera)"}
    if z == 0:
        return None, {"reason": "z==0 (divide by zero)"}
    mx = display[0] / 2.0
    my = display[1] / 2.0
    sx = mx + (mx * x / -z * fov[0])
    sy = my - (my * y / -z * fov[1])
    return (sx, sy), {"x": x, "y": y, "z": z}


def rows_from_basis(right, up, forward, translation=(0.0, 0.0, 0.0)):
    """rows[i] = (axis, w). w must be 0 when the caller subtracts the origin."""
    return [(right[0], right[1], right[2], translation[0]),
            (up[0], up[1], up[2], translation[1]),
            (-forward[0], -forward[1], -forward[2], translation[2])]


def classify_affine(rows, origin, eps=1e-4):
    """Mirror of the probe's verdict, kept separate so the probe's claim can be
    checked instead of restated. A non-zero w is a translation term; it is double
    subtraction only if row.w == -dot(row.xyz, origin) for all three rows."""
    w = [rows[i][3] for i in range(3)]
    if all(abs(x) <= eps for x in w):
        return "NO_TRANSLATION_TERM", w, [0.0, 0.0, 0.0]
    resid = [w[i] + dot(rows[i][:3], origin) for i in range(3)]
    if all(abs(r) <= eps for r in resid):
        return "DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED", w, resid
    return "AFFINE_TRANSLATION_TERM_PRESENT", w, resid


# ------------------------------------------- probe-side selection, mirrored
def view_of(rows, origin, p):
    rel = vec_sub(p, origin)
    hom = (rel[0], rel[1], rel[2], 1.0)
    return (dot(rows[0], hom), dot(rows[1], hom), dot(rows[2], hom))


def sensitivity(view):
    """S = sqrt(viewX^2 + viewY^2) / (-viewZ): the tangent of the angle between
    the point and the view axis. Scale free, so it says how much a parameter
    difference can move this point, not how far away it is."""
    if view[2] >= 0:
        return float("inf")
    return math.hypot(view[0], view[1]) / (-view[2])


def corners_of(bmin, bmax):
    """All eight vertices, in the same order the probe builds them."""
    out = []
    for xi in (0, 1):
        for yi in (0, 1):
            for zi in (0, 1):
                out.append((bmax[0] if xi else bmin[0],
                            bmax[1] if yi else bmin[1],
                            bmax[2] if zi else bmin[2]))
    return out


def select_point(points, rows, math_origin, xform_origin, min_depth=0.1):
    """Highest sensitivity among the usable points; None when nothing qualifies."""
    best = None
    for p in points:
        if not all(math.isfinite(v) for v in p):
            continue
        vm = view_of(rows, math_origin, p)
        vt = view_of(rows, xform_origin, p)
        if vm[2] >= -min_depth or vt[2] >= -min_depth:
            continue
        s = sensitivity(vm)
        if best is None or s > best[0]:
            best = (s, p, vm)
    return best


def pair_sample(preliminary, control):
    """A paired sample may only be formed from facts read on the control frame.
    Reusing the preliminary block's numbers is refused outright."""
    if preliminary["frame"] != control["frame"]:
        return None
    return {"matrixCaptureFrame": preliminary["frame"],
            "controlCaptureFrame": control["frame"],
            "sameFrame": True,
            "mat16": control["mat16"]}


def search_schedule(frames, interval, budget, slow_retry):
    """Mirror of the probe's pacing: at most one search per `interval` frames, the
    budget counts searches, and after it is spent the cadence drops to slow_retry.
    Returns (frames the search ran on, how many of those were inside the budget)."""
    taken, last, count, gave_up = [], 0, 0, False
    for f in range(1, frames + 1):
        gap = slow_retry if gave_up else interval
        if not (count == 0 or (f - last) >= gap):
            continue
        last = f
        taken.append(f)
        if not gave_up:
            count += 1
            if count >= budget:
                gave_up = True
    return taken, count


# ---------------------------------------------------------------- the tests
def main():
    W, H = 1920.0, 1080.0
    display = (W, H)
    fovy_deg = 70.0
    yf = 1.0 / math.tan(math.radians(fovy_deg) / 2.0)
    aspect = W / H
    fov = (yf / aspect, yf)                     # (1/tan(hfov/2), 1/tan(vfov/2))

    cam = (100.0, 70.0, -250.0)
    fwd, right, up = basis_from_yaw_pitch(0.0, 0.0)   # looking +Z
    rows = rows_from_basis(right, up, fwd)

    print("=== T1 identity / camera at origin looking +Z ===")
    p, d = oworld_to_screen(rows, (0, 0, 0), (0, 0, 10), fov, display)
    check("T1 point 10 ahead projects to screen centre",
          p is not None and abs(p[0] - W / 2) < 1e-3 and abs(p[1] - H / 2) < 1e-3,
          str(p))

    print("\n=== T2 translation only (camera moves, direction unchanged) ===")
    p2, _ = oworld_to_screen(rows, cam, (cam[0], cam[1], cam[2] + 10), fov, display)
    check("T2 same relative point gives the same pixel", p2 == p, str(p2))

    print("\n=== T3 left / right (facing +Z the player's right is world -X) ===")
    fast = (cam[0], cam[1], cam[2] + 10)
    pr, _ = oworld_to_screen(rows, cam, (fast[0] - 2.0, fast[1], fast[2]), fov, display)
    check("T3 world -X is the player's RIGHT -> screen x > centre",
          pr is not None and pr[0] > W / 2, str(pr))
    pl, _ = oworld_to_screen(rows, cam, (fast[0] + 2.0, fast[1], fast[2]), fov, display)
    check("T3 world +X is the player's LEFT -> screen x < centre",
          pl is not None and pl[0] < W / 2, str(pl))

    print("\n=== T4 up / down ===")
    pu, _ = oworld_to_screen(rows, cam, (fast[0], fast[1] + 2.0, fast[2]), fov, display)
    check("T4 +Y is ABOVE centre (screen y < centre)",
          pu is not None and pu[1] < H / 2, str(pu))
    pd, _ = oworld_to_screen(rows, cam, (fast[0], fast[1] - 2.0, fast[2]), fov, display)
    check("T4 -Y is BELOW centre", pd is not None and pd[1] > H / 2, str(pd))

    print("\n=== T5 behind the camera ===")
    pb, info = oworld_to_screen(rows, cam, (cam[0], cam[1], cam[2] - 10), fov, display)
    check("T5 point behind is rejected (z > 0)", pb is None, str(info))

    print("\n=== T6 aspect ratio: fov aspect must match display aspect ===")
    fp = (cam[0] + 10.0, cam[1], cam[2] + 10.0)

    def fov_for(w, h):
        yf_ = 1.0 / math.tan(math.radians(fovy_deg) / 2.0)
        return (yf_ / (w / h), yf_)

    # (a) fov rebuilt for each display: the VERTICAL fov is the anchor, so a
    #     point on the vertical axis keeps the same normalised y. (The horizontal
    #     normalised position legitimately depends on the aspect.)
    w1, h1 = 1280.0, 1080.0
    w2, h2 = 2560.0, 1080.0
    vp = (cam[0], cam[1] + 2.0, cam[2] + 10.0)
    p1, _ = oworld_to_screen(rows, cam, vp, fov_for(w1, h1), (w1, h1))
    p2, _ = oworld_to_screen(rows, cam, vp, fov_for(w2, h2), (w2, h2))
    n1 = (p1[1] - h1 / 2) / (h1 / 2)
    n2 = (p2[1] - h2 / 2) / (h2 / 2)
    check("T6a with a per-display fov the normalised y is resolution-invariant",
          abs(n1 - n2) < 1e-6, "n1=%.6f n2=%.6f" % (n1, n2))

    # (b) fov built for 16:9 but used on a 21:9 display: the position shifts
    p3, _ = oworld_to_screen(rows, cam, fp, fov_for(1920.0, 1080.0), (2560.0, 1080.0))
    p2h, _ = oworld_to_screen(rows, cam, fp, fov_for(w2, h2), (w2, h2))
    n3 = abs((p3[0] - 2560.0 / 2) / (2560.0 / 2))
    n2h = abs((p2h[0] - 2560.0 / 2) / (2560.0 / 2))
    check("T6b a stale aspect in fov shifts the point (this is detectable at runtime)",
          abs(n3 - n2h) > 0.05, "n2(correct)=%.4f n3(stale)=%.4f" % (n2h, n3))

    print("\n=== T7 translation leaked into the matrix (the defect under review) ===")
    # identical camera-relative geometry, two different world positions. With
    # w == 0 the pixel must be identical; with w == camera it must not be.
    cam_b = (900.0, 70.0, -250.0)
    point_a = (cam[0], cam[1], cam[2] + 10)
    point_b = (cam_b[0], cam_b[1], cam_b[2] + 10)
    ok_a, _ = oworld_to_screen(rows, cam, point_a, fov, display)
    ok_b, _ = oworld_to_screen(rows, cam_b, point_b, fov, display)
    check("T7 with w == 0 the same relative geometry gives the same pixel",
          ok_a == ok_b, "%s vs %s" % (ok_a, ok_b))

    leaked_a, _ = oworld_to_screen(rows_from_basis(right, up, fwd, translation=cam),
                                   cam, point_a, fov, display)
    leaked_b, _ = oworld_to_screen(rows_from_basis(right, up, fwd, translation=cam_b),
                                   cam_b, point_b, fov, display)
    check("T7 with w == camera the same relative geometry lands on DIFFERENT pixels",
          leaked_a is not None and leaked_b is not None
          and abs(leaked_a[0] - leaked_b[0]) > 100.0,
          "%.1f vs %.1f px" % (leaked_a[0], leaked_b[0]))
    check("T7 the leak is proportional to the world position, not to the offset",
          abs(leaked_a[0] - ok_a[0]) > 100.0,
          "shift=%.1f px at camera x=%.0f" % (leaked_a[0] - ok_a[0], cam[0]))

    print("\n=== T8 fov given in degrees instead of a multiplier (second defect) ===")
    fov_deg = (fovy_deg, fovy_deg)          # aspect lost
    pdeg, _ = oworld_to_screen(rows, cam, fp, fov_deg, display)
    pref, _ = oworld_to_screen(rows, cam, fp, fov_for(1920.0, 1080.0), display)
    check("T8 treating degrees as a multiplier collapses the aspect correction",
          pdeg is not None and abs(pdeg[0] - pref[0]) > 1.0,
          "deg-x=%.1f  correct-x=%.1f" % (pdeg[0], pref[0]))

    print("\n=== T9 yaw 90 (looking -X) keeps the basis right-handed ===")
    f90, r90, u90 = basis_from_yaw_pitch(90.0, 0.0)
    rows90 = rows_from_basis(r90, u90, f90)
    ahead = (cam[0] - 10.0, cam[1], cam[2])       # -X is forward at yaw 90
    p90, _ = oworld_to_screen(rows90, cam, ahead, fov, display)
    check("T9 point 10 blocks down -X is at the centre",
          p90 is not None and abs(p90[0] - W / 2) < 1e-3, str(p90))
    right90, _ = oworld_to_screen(rows90, cam,
                                  (ahead[0], ahead[1], ahead[2] - 2.0),
                                  fov, display)
    check("T9 -Z is the player's RIGHT when facing -X (screen x > centre)",
          right90 is not None and right90[0] > W / 2, str(right90))

    print("\n=== T10 z near zero ===")
    pz, dz = oworld_to_screen(rows, cam, (cam[0], cam[1], cam[2]), fov, display)
    check("T10 a point at the camera is rejected, not divided by zero",
          pz is None, str(dz))

    print("\n=== T11 production parameters vs transform snapshot (the control) ===")
    # getBoxPoints() passes the MathUtils statics (origin / fov / displaySize) into
    # the projection while taking the matrix from RenderUtils::transform. The probe
    # used the transform's own copies of origin and fov, so it could describe a
    # projection the renderer never performs. Both sets are projected here from the
    # same matrix, the same point and the same frame.
    pp = (cam[0] + 0.5, cam[1] + 0.5, cam[2] + 10.0)      # off-axis, so offsets show
    sa, _ = oworld_to_screen(rows, cam, pp, fov, display)
    sb, _ = oworld_to_screen(rows, cam, pp, fov, display)
    check("T11a equal parameter sets give an exactly identical pixel",
          sa is not None and sa == sb, "%s vs %s" % (sa, sb))

    cam_stale = (cam[0], cam[1] + 0.25, cam[2])            # snapshot one tick old
    sc, _ = oworld_to_screen(rows, cam_stale, pp, fov, display)
    check("T11b a 0.25-block origin difference moves the pixel by > 1 px",
          sc is not None and abs(sc[1] - sa[1]) > 1.0,
          "delta_y=%.3f px" % (sc[1] - sa[1]))

    fov_stale = (fov[0] * 1.05, fov[1])                    # aspect drifted 5%
    sd, _ = oworld_to_screen(rows, cam, pp, fov_stale, display)
    check("T11c a 5% fov drift moves the pixel by > 1 px",
          sd is not None and abs(sd[0] - sa[0]) > 1.0,
          "delta_x=%.3f px" % (sd[0] - sa[0]))

    d_equal  = abs(sa[0] - sb[0]) + abs(sa[1] - sb[1])
    d_origin = abs(sc[0] - sa[0]) + abs(sc[1] - sa[1])
    d_fov    = abs(sd[0] - sa[0]) + abs(sd[1] - sa[1])
    check("T11d equal parameters produce zero delta, and the selected perturbations are detectable",
          d_equal == 0.0 and d_origin > 1.0 and d_fov > 1.0,
          "equal=%.6f origin=%.3f fov=%.3f" % (d_equal, d_origin, d_fov))

    print("\n=== T12 affine translation: term present vs double subtraction proven ===")
    axes = [rows[i][:3] for i in range(3)]
    baked = tuple(-dot(axes[i], cam) for i in range(3))
    rows_baked = rows_from_basis(right, up, fwd, translation=baked)
    verdict_b, _, resid_b = classify_affine(rows_baked, cam)
    check("T12a w == -dot(axis, origin) classifies as DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED",
          verdict_b == "DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED"
          and max(abs(r) for r in resid_b) < 1e-4,
          "%s resid=%s" % (verdict_b, resid_b))

    p_baked, _ = oworld_to_screen(rows_baked, cam, pp, fov, display)
    check("T12b the projection really shifts, so the duplication is not cosmetic",
          p_baked is not None and abs(p_baked[0] - sa[0]) > 1.0,
          "shift=%.1f px" % (p_baked[0] - sa[0]))

    rows_unknown = rows_from_basis(right, up, fwd, translation=(5.0, -3.0, 2.0))
    verdict_u, _, resid_u = classify_affine(rows_unknown, cam)
    check("T12c an unexplained w is AFFINE_TRANSLATION_TERM_PRESENT and nothing more",
          verdict_u == "AFFINE_TRANSLATION_TERM_PRESENT"
          and max(abs(r) for r in resid_u) > 1e-4,
          "%s resid=%s" % (verdict_u, resid_u))

    verdict_n, _, _ = classify_affine(rows, cam)
    check("T12d w == 0 is NO_TRANSLATION_TERM, not a silent pass",
          verdict_n == "NO_TRANSLATION_TERM", verdict_n)

    print("\n=== T13 screenDelta is a sensitivity measure, not an equality proof ===")
    # A point on the view axis keeps its pixel whatever the fov does, so a zero
    # screenDelta says nothing about whether the two parameter sets agree. This is
    # the counterexample that stops the old wording ("delta 0 proves the sets are
    # equal") from being used again.
    on_axis = (cam[0], cam[1], cam[2] + 10.0)
    pa, _ = oworld_to_screen(rows, cam, on_axis, fov, display)
    fov_moved = (fov[0] * 1.30, fov[1])
    pb, _ = oworld_to_screen(rows, cam, on_axis, fov_moved, display)
    check("T13a an on-axis point keeps its pixel even with a 30% fov difference",
          pa is not None and pb is not None
          and abs(pa[0] - pb[0]) < 1e-9 and abs(pa[1] - pb[1]) < 1e-9,
          "delta=(%.6f,%.6f)" % (pb[0] - pa[0], pb[1] - pa[1]))

    check("T13b yet the two parameter sets are NOT equal -- fovsMatch would be false",
          fov != fov_moved, "fov=(%.4f,%.4f) vs (%.4f,%.4f)" % (fov + fov_moved))

    # Equality has to come from comparing the parameters themselves, and the
    # off-axis point is only there to say how much a difference would have shown.
    off_axis = (cam[0] + 0.5, cam[1] + 0.5, cam[2] + 10.0)
    qa, _ = oworld_to_screen(rows, cam, off_axis, fov, display)
    qb, _ = oworld_to_screen(rows, cam, off_axis, fov_moved, display)
    check("T13c off-axis the same fov difference does move the pixel -- sensitivity, quantified",
          qa is not None and qb is not None and abs(qa[0] - qb[0]) > 1.0,
          "delta_x=%.3f px" % (qb[0] - qa[0]))

    print("\n=== T14 affine residual must be taken from the production origin ===")
    # getBoxPoints() subtracts MathUtils::origin, so a matrix that has already
    # baked that origin is double subtraction. Using the transform's copy instead
    # misclassifies the very same matrix when the two origins differ by a frame.
    axes = [rows[i][:3] for i in range(3)]
    math_origin = cam
    xform_origin = (cam[0], cam[1] + 0.5, cam[2])       # lagging copy
    baked2 = tuple(-dot(axes[i], math_origin) for i in range(3))
    rows_b = rows_from_basis(right, up, fwd, translation=baked2)
    v_math, _, r_math = classify_affine(rows_b, math_origin)
    v_xform, _, r_xform = classify_affine(rows_b, xform_origin)
    check("T14a production origin -> residual zero -> DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED",
          v_math == "DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED"
          and max(abs(r) for r in r_math) < 1e-4,
          "%s resid=%s" % (v_math, r_math))
    check("T14b the stale transform origin misclassifies the same matrix",
          v_xform == "AFFINE_TRANSLATION_TERM_PRESENT"
          and max(abs(r) for r in r_xform) > 1e-4,
          "%s resid=%s" % (v_xform, r_xform))
    check("T14c the two verdicts disagree here, so the choice of origin is load-bearing",
          v_math != v_xform, "%s vs %s" % (v_math, v_xform))

    print("\n=== T15 test-point validity: a point at the camera measures nothing ===")
    def usable(pt, o_math, o_x, min_depth=0.1):
        if not all(math.isfinite(v) for v in pt):
            return False
        _, dm = oworld_to_screen(rows, o_math, pt, fov, display)
        _, dx = oworld_to_screen(rows, o_x, pt, fov, display)
        zm, zx = dm.get("z"), dx.get("z")
        if zm is None or zx is None:
            return False
        return zm < -min_depth and zx < -min_depth

    at_camera = cam
    behind = (cam[0], cam[1], cam[2] - 10.0)
    not_finite = (float("nan"), cam[1], cam[2] + 10.0)
    foot_corner = (cam[0] + 0.3, cam[1] - 1.62, cam[2] + 0.3)

    check("T15a the local player's render position is at the camera plane -> rejected",
          not usable(at_camera, cam, cam), "z=0 / z=0")

    check("T15b a point behind the camera is rejected",
          not usable(behind, cam, cam), "z>0")

    check("T15c a non-finite coordinate is rejected",
          not usable(not_finite, cam, cam), "nan")

    check("T15d an AABB corner below and beside the camera is accepted",
          usable(foot_corner, cam, cam), "z<-0.1")

    picked = None
    for cand in (at_camera, behind, not_finite, foot_corner):
        if usable(cand, cam, cam):
            picked = cand
            break
    check("T15e the picker skips every unusable candidate and returns the corner",
          picked == foot_corner, str(picked))

    print("\n=== T16 a paired sample may not mix frames ===")
    prelim = {"frame": 41, "mat16": ("m@41",)}
    later = {"frame": 900, "mat16": ("m@900",)}
    same = {"frame": 41, "mat16": ("m@41",)}

    check("T16a pairing stale matrix facts with a later control frame is refused",
          pair_sample(prelim, later) is None, "frames 41 vs 900")

    merged = pair_sample(prelim, same)
    check("T16b facts read on the control frame pair, and both stamps are equal",
          merged is not None
          and merged["matrixCaptureFrame"] == merged["controlCaptureFrame"] == 41,
          str(merged))

    # The probe does not rely on the stamps matching: the paired block re-reads
    # every fact, so it carries the control frame's matrix even when the frames
    # differ, and there is nothing left to merge by hand.
    self_contained = {"matrixCaptureFrame": later["frame"],
                      "controlCaptureFrame": later["frame"],
                      "mat16": later["mat16"]}
    check("T16c the paired block is self-contained: it carries the control frame's matrix",
          self_contained["mat16"] == later["mat16"]
          and self_contained["mat16"] != prelim["mat16"],
          "paired mat16=%s, preliminary=%s" % (self_contained["mat16"], prelim["mat16"]))

    print("\n=== T17 all eight AABB corners ===")
    bmin, bmax = (1.0, 2.0, 3.0), (2.0, 4.0, 5.0)
    cs = corners_of(bmin, bmax)
    check("T17a eight distinct vertices, covering min and max on every axis",
          len(cs) == 8 and len(set(cs)) == 8
          and {c[0] for c in cs} == {bmin[0], bmax[0]}
          and {c[1] for c in cs} == {bmin[1], bmax[1]}
          and {c[2] for c in cs} == {bmin[2], bmax[2]},
          "%d corners" % len(cs))

    old_four = [cs[7], cs[2], cs[4], cs[1]]      # what the earlier probe checked
    only_elsewhere = {cs[3], cs[5]}              # usable corners outside that subset
    found_full = [c for c in cs if c in only_elsewhere]
    found_old = [c for c in old_four if c in only_elsewhere]
    check("T17b a box whose only usable corners lie outside the old four is still found",
          len(found_full) == 2 and found_old == [],
          "eight-corner scan=%d, four-corner scan=%d" % (len(found_full), len(found_old)))

    check("T17c the old four were a strict subset: four vertices were never examined",
          set(old_four) < set(cs) and len(set(cs) - set(old_four)) == 4,
          "unexamined corners: %d" % len(set(cs) - set(old_four)))

    print("\n=== T18 the selector prefers the more off-axis candidate ===")
    near_axis = (cam[0] + 0.05, cam[1], cam[2] + 10.0)
    off_axis_pt = (cam[0] + 5.0, cam[1] + 2.0, cam[2] + 10.0)
    picked2 = select_point([near_axis, off_axis_pt], rows, cam, cam)
    check("T18a the off-axis point wins even though the on-axis one is listed first",
          picked2 is not None and picked2[1] == off_axis_pt,
          "picked=%s" % (str(None if picked2 is None else picked2[1]),))

    s_near = sensitivity(view_of(rows, cam, near_axis))
    check("T18b and its sensitivity score is strictly larger",
          picked2 is not None and picked2[0] > s_near > 0,
          "off=%.4f near=%.4f" % (picked2[0] if picked2 else -1, s_near))

    print("\n=== T19 LOW_SENSITIVITY: source as well as score ===")
    THRESH = 0.15

    def label(score, source):
        # The local fallback is flagged from its source on purpose: it sits ~0.3
        # blocks ahead, which inflates S while telling you nothing about a scene
        # at distance. A near point is not a better probe, it is a clipped one.
        return "LOW_SENSITIVITY" if (score < THRESH or source.startswith("local_")) else "OK"

    s_local = sensitivity(view_of(rows, cam, (cam[0] + 0.3, cam[1] - 1.62, cam[2] + 0.3)))
    s_bad = sensitivity(view_of(rows, cam, (cam[0] + 1e-4, cam[1], cam[2] + 10.0)))
    check("T19a the local fallback is flagged from its source even though its raw score is high",
          s_local > THRESH
          and label(s_local, "local_actor_aabb_corner_low_sensitivity") == "LOW_SENSITIVITY",
          "S=%.4f" % s_local)
    check("T19b a nearly on-axis non-local point is flagged from its score",
          s_bad < THRESH and label(s_bad, "nonlocal_actor_aabb_corner") == "LOW_SENSITIVITY",
          "S=%.6f" % s_bad)
    check("T19c an off-axis non-local point is OK, so screenDelta is usable there",
          label(picked2[0], "nonlocal_actor_aabb_corner") == "OK",
          "S=%.4f" % picked2[0])

    print("\n=== T20 search pacing and budget counting ===")
    sched, _ = search_schedule(1000, 20, 5, 600)
    gaps = [b - a for a, b in zip(sched, sched[1:])]
    check("T20a first search on the first frame, then no more often than the interval",
          sched[0] == 1 and all(g >= 20 for g in gaps),
          "frames=%s gaps=%s" % (sched[:6], gaps[:6]))

    full, n_full = search_schedule(20000, 20, 900, 600)
    tight, n_tight = search_schedule(20000, 20, 4, 600)
    check("T20b the budget counts searches, not frames",
          n_full == 900 and n_tight == 4,
          "budget900 -> %d budgeted searches, budget4 -> %d" % (n_full, n_tight))

    sched2, _ = search_schedule(5000, 20, 4, 600)
    gaps2 = [b - a for a, b in zip(sched2, sched2[1:])]
    check("T20c after the budget the cadence drops to the slow retry interval",
          len(sched2) >= 2 and gaps2[-1] >= 600,
          "gap after the budget = %d frames" % (gaps2[-1] if gaps2 else -1))

    failed = [c for c in CHECKS if not c[1]]
    out = {
        "checks": [{"name": n, "pass": ok, "detail": d} for n, ok, d in CHECKS],
        "total": len(CHECKS), "failed": len(failed),
        "result": "ALL_PASS" if not failed else "FAILED",
        "conventions": {
            "viewSpace": "camera looks down -Z; z_v < 0 in front",
            "rows": "rows[0]=right, rows[1]=up, rows[2]=-forward, w must be 0",
            "fov": "(1/tan(hfov/2), 1/tan(vfov/2))",
            "screen": "sx = W/2 + W/2 * x_v/(depth*fovx); sy = H/2 - H/2 * y_v/(depth*fovy)"
        }
    }
    json.dump(out, open(r"G:/hacker/docs/batch3_6/projection-test-results.json", "w",
                        encoding="utf-8"), indent=1, ensure_ascii=False)
    print("\n%d checks, %d failed -> %s" % (len(CHECKS), len(failed), out["result"]))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
