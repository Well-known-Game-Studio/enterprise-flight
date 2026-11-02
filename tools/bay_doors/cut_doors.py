"""Run: python3 tools/bay_doors/cut_doors.py <path to Enterprise-A.obj>  (the model unpacked from
blender/uss-enterprise-ncc-1701-a/source). Cut the clamshell door (the conical centre of the 1701-A's stern cap) into gores: the hull's own triangles with
their UVs, clipped along the painted seam lines and by azimuth about the door's vertical axis. Writes two OBJs:
gores_H.obj in the model's raw space (for a Blender check against the hull) and gores_B.obj in the FBX route's
Blender convention (x, -y, z) m with winding flipped for the mirror. Pawn space: cm, x forward, y starboard, z up."""
import math, json, os, sys
OBJ = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "Enterprise-A.obj")
OUT = os.path.dirname(os.path.abspath(__file__))
V=[]; VT=[]; faces=[]; mtl=None; fmtl=[]
for line in open(OBJ):
    if line.startswith("v "):
        x,y,z=map(float,line.split()[1:4]); V.append((-y*100,x*100,z*100))
    elif line.startswith("vt "):
        p=line.split(); VT.append((float(p[1]),float(p[2])))
    elif line.startswith("usemtl"): mtl=line.split()[1]
    elif line.startswith("f "): faces.append([(int(t.split("/")[0])-1,int(t.split("/")[1])-1) for t in line.split()[1:]]); fmtl.append(mtl)
CX, CZ = -6330.0, -1948.0
# The door faces: the stern cap's recessed surface (inside a sphere fitted to the cap), above the deck fan and
# below the top cap, on the surface of revolution (not the model's inner filler fans).
def nrm(idx):
    a,b,c=[V[i] for i,_ in idx[:3]]
    u=[b[k]-a[k] for k in range(3)]; w=[c[k]-a[k] for k in range(3)]
    n=[u[1]*w[2]-u[2]*w[1],u[2]*w[0]-u[0]*w[2],u[0]*w[1]-u[1]*w[0]]; L=math.sqrt(sum(t*t for t in n)) or 1
    return [t/L for t in n]
door=[]
for fi,idx in enumerate(faces):
    if fmtl[fi] != "Constitution_3": continue
    c=[sum(V[i][k] for i,_ in idx)/3 for k in range(3)]
    if not (c[0] < -6100 and abs(c[1]) < 1100 and -1945 < c[2] < -1235): continue
    if math.sqrt((c[0]+6200)**2 + c[1]**2 + (c[2]+2025)**2) >= 1041 - 60: continue   # the rim and overhang
    n=nrm(idx); r=math.hypot(c[0]-CX, c[1]); rad=((c[0]-CX)/r, c[1]/r) if r > 1 else (0, 0)
    dot=n[0]*rad[0]+n[1]*rad[1]
    if abs(dot) > 0.3 or (c[2] > -1320 and abs(n[2]) > 0.5 and r > 250 and min(math.hypot(V[i][0]-CX, V[i][1]) for i,_ in idx) > 200):
        door.append(fi)
print("door faces", len(door))
tris=[[(V[i], VT[t]) for i,t in faces[fi]] for fi in door]
def clip(poly, f):
    """Keep the part of poly (list of (pos, uv)) where f(pos) <= 0 (Sutherland-Hodgman)."""
    out=[]
    n=len(poly)
    for k in range(n):
        a,b = poly[k], poly[(k+1)%n]; fa, fb = f(a[0]), f(b[0])
        if fa <= 0: out.append(a)
        if (fa <= 0) != (fb <= 0):
            t = fa/(fa-fb)
            p = tuple(a[0][i] + t*(b[0][i]-a[0][i]) for i in range(3)); uv = tuple(a[1][i] + t*(b[1][i]-a[1][i]) for i in range(2))
            out.append((p,uv))
    return out
def edge_y(z): return 346.0 + (-1270.0 - z) * 0.754
DOOR_TOP = -1243.0   # the door's top edge: above it the cone turns into the roof under the top cap, which stays hull
centre=[]
for tri in tris:
    p = clip(tri, lambda q: q[1] - edge_y(q[2]))
    if len(p) >= 3: p = clip(p, lambda q: -q[1] - edge_y(q[2]))
    if len(p) >= 3: p = clip(p, lambda q: q[2] - DOOR_TOP)
    if len(p) >= 3: centre.append(p)
print("centre polys", len(centre))
NG=8; AMAX=62.0
edges=[-AMAX + 2*AMAX*k/NG for k in range(NG+1)]
def az_plane(a_deg, keep_greater):
    r=math.radians(a_deg)
    # azimuth about (CX,0): az = atan2(y, -(x-CX)); the plane through the axis at azimuth a has normal n = (sin a, cos a) in (-(x-CX), y)... derive: point at azimuth a: (-(x-CX), y) = (cos a, sin a); the left normal = (-sin a, cos a)
    def f(q):
        u = -(q[0]-CX); v = q[1]
        s = -math.sin(r)*u + math.cos(r)*v     # >0 on the side of greater azimuth
        return -s if keep_greater else s
    return f
gores=[]
for k in range(NG):
    polys=[]
    for p in centre:
        q = clip(p, az_plane(edges[k], True))
        if len(q) >= 3: q = clip(q, az_plane(edges[k+1], False))
        if len(q) >= 3: polys.append(q)
    gores.append(polys)
    xs=[pt[0][0] for p in polys for pt in p]; ys=[pt[0][1] for p in polys for pt in p]; zs=[pt[0][2] for p in polys for pt in p]
    print(f"gore {k+1}: {len(polys)} polys, x {min(xs):.0f}..{max(xs):.0f} y {min(ys):.0f}..{max(ys):.0f} z {min(zs):.0f}..{max(zs):.0f}")
INNER_CM = 6.0   # the inner skin sits this far inside the hull surface (toward the door's axis)
def inward(pos):
    r = math.hypot(pos[0]-CX, pos[1]); f = max(r - INNER_CM, 0.0) / max(r, 1e-6)
    return (CX + (pos[0]-CX) * f, pos[1] * f, pos[2])
def write(path, conv, flip):
    with open(path,"w") as f:
        f.write("mtllib Enterprise-A.mtl\n"); vi=1; ti=1
        for k,polys in enumerate(gores):
            f.write(f"o SM_BayDoor{k+1}\n")
            for skin, mtl, rev in (("outer", "Constitution_3", False), ("inner", "DoorInner", True)):
                f.write(f"usemtl {mtl}\n")
                for p in polys:
                    idx=[]
                    for pos,uv in p:
                        c=conv(pos if skin == "outer" else inward(pos)); f.write("v %.4f %.4f %.4f\n"%c); f.write("vt %.5f %.5f\n"%uv); idx.append((vi,ti)); vi+=1; ti+=1
                    order = idx[::-1] if (flip != rev) else idx
                    for j in range(1,len(order)-1):
                        f.write("f %d/%d %d/%d %d/%d\n"%(order[0][0],order[0][1],order[j][0],order[j][1],order[j+1][0],order[j+1][1]))
write(os.path.join(OUT, "gores_H.obj"), lambda p: (p[1]/100.0, -p[0]/100.0, p[2]/100.0), False)   # raw model space
write(os.path.join(OUT, "gores_B.obj"), lambda p: (p[0]/100.0, -p[1]/100.0, p[2]/100.0), True)    # FBX route convention (mirror -> flip)
json.dump({"pivot_pawn": [CX, 0.0, CZ], "edges_deg": edges}, open(os.path.join(OUT, "gores_meta.json"),"w"))
print("written")
