"""Run: python3 tools/bay_doors/cut_doors.py <path to Enterprise-A.obj>  (the model unpacked from
blender/uss-enterprise-ncc-1701-a/source). The 1701-A's fantail door is an inner cone standing in a recess at the
stern: radius 828 cm at its base ring (z -1974, under the deck fan) to 280 cm at its top ring (z -1197), about a
vertical axis at pawn (-6330, 0). This cuts that cone (the hull's own triangles, UVs kept) into eight gores by
azimuth, each with an inner skin, and writes gores_B.obj in the FBX route's Blender frame (x, -y, z) m with the
winding flipped for the mirror (and gores_H.obj in the model's raw frame for checks). Pawn space: cm, x forward."""
import math, json, os, sys
OBJ = sys.argv[1] if len(sys.argv) > 1 else "obj/Enterprise-A.obj"
OUT = os.path.dirname(os.path.abspath(__file__)) if "tools" in os.path.abspath(__file__) else "cap"
V=[]; VT=[]; faces=[]; mtl=None; fmtl=[]
for line in open(OBJ):
    if line.startswith("v "):
        x,y,z=map(float,line.split()[1:4]); V.append((-y*100,x*100,z*100))
    elif line.startswith("vt "):
        p=line.split(); VT.append((float(p[1]),float(p[2])))
    elif line.startswith("usemtl"): mtl=line.split()[1]
    elif line.startswith("f "): faces.append([(int(t.split("/")[0])-1,int(t.split("/")[1])-1) for t in line.split()[1:]]); fmtl.append(mtl)
CX, CZ = -6330.0, -1948.0
def r_cone(z): return 828.0 - (z + 1974.0) * 0.705
DOOR_TOP = -1243.0
# The door faces: Constitution_3 triangles at the stern whose centroid sits on the inner cone.
door=[]
for fi,idx in enumerate(faces):
    if fmtl[fi] != "Constitution_3": continue
    c=[sum(V[i][k] for i,_ in idx)/3 for k in range(3)]
    if not (c[0] < -6100 and abs(c[1]) < 1000 and -1990 < c[2] < -1190): continue
    r=math.hypot(c[0]-CX, c[1]); rc=r_cone(c[2])
    if 0.9 < r / rc < 1.1: door.append(fi)
print("door faces", len(door))
tris=[[(V[i], VT[t]) for i,t in faces[fi]] for fi in door]
def clip(poly, f):
    out=[]; n=len(poly)
    for k in range(n):
        a,b = poly[k], poly[(k+1)%n]; fa, fb = f(a[0]), f(b[0])
        if fa <= 0: out.append(a)
        if (fa <= 0) != (fb <= 0):
            t = fa/(fa-fb)
            p = tuple(a[0][i] + t*(b[0][i]-a[0][i]) for i in range(3)); uv = tuple(a[1][i] + t*(b[1][i]-a[1][i]) for i in range(2))
            out.append((p,uv))
    return out
TAIL_X = -6230.0   # the cone's tail behind the axis plane ends at the bay's junction wall
centre=[]
for tri in tris:
    p = clip(tri, lambda q: q[2] - DOOR_TOP)
    if len(p) >= 3: p = clip(p, lambda q: q[0] - TAIL_X)
    if len(p) >= 3: centre.append(p)
NG=8; AMAX=90.0
edges=[-AMAX + 2*AMAX*k/NG for k in range(NG+1)]
def az_plane(a_deg, keep_greater):
    r=math.radians(a_deg)
    def f(q):
        u = -(q[0]-CX); v = q[1]
        s = -math.sin(r)*u + math.cos(r)*v
        return -s if keep_greater else s
    return f
gores=[]
for k in range(NG):
    polys=[]
    for p in centre:
        # The outermost gores take everything beyond their inner plane, including the cone's tail behind the axis
        # plane (az past 90 deg), so the door meets the recess with no slit at its sides.
        q = clip(p, az_plane(edges[k], True)) if k > 0 else p
        if len(q) >= 3 and k < NG - 1: q = clip(q, az_plane(edges[k+1], False))
        if len(q) >= 3: polys.append(q)
    gores.append(polys)
    xs=[pt[0][0] for p in polys for pt in p]; ys=[pt[0][1] for p in polys for pt in p]; zs=[pt[0][2] for p in polys for pt in p]
    print(f"gore {k+1}: {len(polys)} polys, az {edges[k]:.1f}..{edges[k+1]:.1f}, x {min(xs):.0f}..{max(xs):.0f} y {min(ys):.0f}..{max(ys):.0f} z {min(zs):.0f}..{max(zs):.0f}")
INNER_CM = 0.0   # the inner skin is the same surface wound the other way: an offset skin left a slit open at the silhouette
def inward(pos):
    r = math.hypot(pos[0]-CX, pos[1]); f = max(r - INNER_CM, 0.0) / max(r, 1e-6)
    return (CX + (pos[0]-CX) * f, pos[1] * f, pos[2])
def write(path, conv, flip):
    with open(path,"w") as f:
        f.write("mtllib Enterprise-A.mtl\n"); vi=1; ti=1
        for k,polys in enumerate(gores):
            f.write(f"o SM_BayDoor{k+1}\n")
            for skin, m, rev in (("outer", "Constitution_3", False), ("inner", "DoorInner", True)):
                f.write(f"usemtl {m}\n")
                for p in polys:
                    idx=[]
                    for pos,uv in p:
                        c=conv(pos if skin == "outer" else inward(pos)); f.write("v %.4f %.4f %.4f\n"%c); f.write("vt %.5f %.5f\n"%uv); idx.append((vi,ti)); vi+=1; ti+=1
                    order = idx[::-1] if (flip != rev) else idx
                    for j in range(1,len(order)-1):
                        f.write("f %d/%d %d/%d %d/%d\n"%(order[0][0],order[0][1],order[j][0],order[j][1],order[j+1][0],order[j+1][1]))
write(os.path.join(OUT, "gores_H.obj"), lambda p: (p[1]/100.0, -p[0]/100.0, p[2]/100.0), False)
write(os.path.join(OUT, "gores_B.obj"), lambda p: (p[0]/100.0, -p[1]/100.0, p[2]/100.0), True)
json.dump({"pivot_pawn": [CX, 0.0, CZ], "edges_deg": edges, "door_faces": door}, open(os.path.join(OUT, "gores_meta.json"),"w"))
open(os.path.join(OUT, "grp_door.txt"), "w").write(" ".join(map(str, door)))
print("written")
