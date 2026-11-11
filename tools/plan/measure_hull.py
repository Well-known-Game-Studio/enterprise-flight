"""Measure the 1701-A hull (the Major Stress OBJ the game flies, metres, y along the length; unpack
blender/uss-enterprise-ncc-1701-a/source/*.7z) in the game's frame (cm, x forward, y starboard, z up, origin the
bounds centre) for the deck plan (ShipPlan.cpp's deck table): the saucer's skin top and bottom by radius, the
neck's and the secondary hull's extents by height and by station.

    python3 tools/plan/measure_hull.py <Enterprise-A.obj>
"""
import sys, math
vs=[]
for line in open(sys.argv[1]):
    if line.startswith('v '):
        p=line.split(); vs.append((float(p[1]),float(p[2]),float(p[3])))
lo=[min(v[i] for v in vs) for i in range(3)]; hi=[max(v[i] for v in vs) for i in range(3)]
c=[(lo[i]+hi[i])/2 for i in range(3)]
# bow: the saucer (widest slices) end
bins={}
for v in vs:
    k=min(19,max(0,int((v[1]-lo[1])/(hi[1]-lo[1])*20))); bins[k]=max(bins.get(k,0),abs(v[0]))
bow=1.0 if max(bins,key=bins.get)>=10 else -1.0
P=[((v[1]-c[1])*100*bow, v[0]*100*(1 if bow>0 else -1), (v[2]-c[2])*100) for v in vs]
print("bow sign",bow,"L",(hi[1]-lo[1])*100)
# saucer: centre (8000,0): skin top and bottom by radius
print("\nSAUCER r: zmin zmax (verts x>1500, excluding the bridge module |y|<800 & z>2900 is kept)")
for r in range(0,7300,250):
    zs=[p[2] for p in P if p[0]>1200 and abs(math.hypot(p[0]-8000,p[1])-r)<=125]
    if zs: print(f"{r:5d} {min(zs):7.0f} {max(zs):7.0f}  n={len(zs)}")
print("\nSAUCER by z: outer radius (max r of verts x>1200, |y| any, within z+-30), inner radius of hull at that z")
for z in range(-200,3700,100):
    rs=[math.hypot(p[0]-8000,p[1]) for p in P if p[0]>1200 and abs(p[2]-z)<=30]
    if rs: print(f"{z:5d} rmax {max(rs):7.0f} rmin {min(rs):7.0f} n={len(rs)}")
print("\nNECK by z: x range and half-width (verts 900<x<4700, |y|<2500)")
for z in range(-1200,700,100):
    q=[p for p in P if 900<p[0]<4700 and abs(p[1])<2500 and abs(p[2]-z)<=30]
    if q: print(f"{z:5d} x {min(p[0] for p in q):7.0f}..{max(p[0] for p in q):7.0f} halfw {max(abs(p[1]) for p in q):6.0f} n={len(q)}")
print("\nSECONDARY by z: x range, half-beam (verts -7700<x<4700, |y|<2000)")
for z in range(-3700,-600,100):
    q=[p for p in P if -7700<p[0]<4700 and abs(p[1])<2000 and abs(p[2]-z)<=30]
    if q: print(f"{z:5d} x {min(p[0] for p in q):7.0f}..{max(p[0] for p in q):7.0f} halfw {max(abs(p[1]) for p in q):6.0f} n={len(q)}")
print("\nSECONDARY by x: z range and half-beam (|y|<2000)")
for x in range(-7600,4800,400):
    q=[p for p in P if abs(p[0]-x)<=50 and abs(p[1])<2000 and p[2]<-700]
    if q: print(f"{x:6d} z {min(p[2] for p in q):7.0f}..{max(p[2] for p in q):7.0f} halfw {max(abs(p[1]) for p in q):6.0f}")
