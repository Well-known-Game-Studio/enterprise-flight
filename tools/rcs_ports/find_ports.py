"""Find the painted RCS thruster ports and phaser emitters on the Enterprise-A hull.

The model's diffuse maps mark every reaction-control port and phaser emitter as a tan patch with a mark per
nozzle or emitter: thrusters are yellow (round on the saucer faces, square on the rim, the deflector housing and
the nacelle caps), phaser emitters are larger grey discs. This finds the patches (and the marks inside them as
holes in the patch), classifies each hole by its colour, maps its texel centre through the OBJ's UVs to the
triangle under it, and prints pawn-space positions (cm) and plating normals, which is where the port table in
StarshipPawn.cpp (EnterpriseAPorts) comes from. Pawn space is (-obj.y, obj.x, obj.z) x 100.

Inputs (not in the repo, under blender/uss-enterprise-ncc-1701-a): the OBJ from source/Major Stress Enterprise-A.7z
extracted to <scratch>/obj/, and textures/Fed_TMP_Constitution_{1..4}_DIFF.png resized to 2048 and dumped raw:
    magick Fed_TMP_Constitution_1_DIFF.png -resize 2048x2048! -depth 8 rgb:<scratch>/tex/diff1_2k.rgb
Every candidate face per mark is listed: overlapping UV islands (the mirrored nacelles, some hidden geometry) give
more than one, and the symmetric set is the right one. Pure Python, a few minutes.

Usage: python3 tools/rcs_ports/find_ports.py <scratch dir>
"""
import sys, collections, math
S=sys.argv[1] if len(sys.argv) > 1 else "."
W=H=2048
V=[];VT=[];F=[];mat=None
for line in open(S+"/obj/Enterprise-A.obj"):
    if line.startswith("v "): V.append(tuple(map(float,line.split()[1:4])))
    elif line.startswith("vt "): VT.append(tuple(map(float,line.split()[1:3])))
    elif line.startswith("usemtl"): mat=line.split()[1]
    elif line.startswith("f "):
        idx=[tuple(int(x) if x else 0 for x in (p.split("/")+["",""])[:3]) for p in line.split()[1:]]
        F.append((mat,idx))
def tan(r,g,b): return r>190 and g>160 and b<175 and 45<r-b<130 and g-b>25   # the tan patch, not the brighter yellow circles
def locate(mtl, u, v, grid, tol=1e-3):
    """Every non-degenerate face containing the UV (overlapping islands: mirrored nacelles, hidden geometry)."""
    res=[]
    cx=min(int(u*128),127); cy=min(int(v*128),127)
    for fi in grid.get((cx,cy),[]):
        m,idx=F[fi]
        (a,b,c)=[VT[t-1] for _,t,_ in idx]
        d=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(d)<1e-12: continue
        l1=((b[1]-c[1])*(u-c[0])+(c[0]-b[0])*(v-c[1]))/d
        l2=((c[1]-a[1])*(u-c[0])+(a[0]-c[0])*(v-c[1]))/d
        l3=1-l1-l2
        if l1>=-tol and l2>=-tol and l3>=-tol:
            P=[V[vi-1] for vi,_,_ in idx]
            pos=[l1*P[0][k]+l2*P[1][k]+l3*P[2][k] for k in range(3)]
            e1=[P[1][k]-P[0][k] for k in range(3)]; e2=[P[2][k]-P[0][k] for k in range(3)]
            n=[e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0]]
            L=math.sqrt(sum(x*x for x in n))
            if L*0.5 < 0.02: continue          # degenerate / collapsed face
            res.append((pos,[x/L for x in n]))
    if not res and tol<0.05: return locate(mtl,u,v,grid,tol*8)
    return res
out=[]
for slot in (1,2,3,4):
    mtl=f"Constitution_{slot}"
    grid=collections.defaultdict(list)
    for fi,(m,idx) in enumerate(F):
        if m!=mtl: continue
        us=[VT[t-1][0] for _,t,_ in idx]; vs=[VT[t-1][1] for _,t,_ in idx]
        for cx in range(max(0,int(min(us)*128)),min(127,int(max(us)*128))+1):
            for cy in range(max(0,int(min(vs)*128)),min(127,int(max(vs)*128))+1):
                grid[(cx,cy)].append(fi)
    data=open(f"{S}/tex/diff{slot}_2k.rgb","rb").read()
    mask=bytearray(W*H)
    for i in range(W*H):
        r=data[3*i];g=data[3*i+1];b=data[3*i+2]
        if tan(r,g,b): mask[i]=1
    seen=bytearray(W*H); comps=[]
    for i in range(W*H):
        if mask[i] and not seen[i]:
            st=[i]; seen[i]=1; px=[]
            while st:
                j=st.pop(); px.append(j); y,x=divmod(j,W)
                for nj in (j-1,j+1,j-W,j+W):
                    if 0<=nj<W*H and mask[nj] and not seen[nj] and abs((nj%W)-x)<=1: seen[nj]=1; st.append(nj)
            if len(px)>=60: comps.append(px)
    for ci,px in enumerate(comps):
        xs=[p%W for p in px]; ys=[p//W for p in px]
        x0,x1,y0,y1=min(xs)-1,max(xs)+1,min(ys)-1,max(ys)+1
        # holes: non-tan pixels in the bbox not reachable from its border
        bw=x1-x0+1; bh=y1-y0+1
        inside=bytearray(bw*bh)
        for p in px: inside[(p//W-y0)*bw+(p%W-x0)]=1
        reach=bytearray(bw*bh); st=[]
        for x in range(bw):
            for y in (0,bh-1):
                k=y*bw+x
                if not inside[k] and not reach[k]: reach[k]=1; st.append(k)
        for y in range(bh):
            for x in (0,bw-1):
                k=y*bw+x
                if not inside[k] and not reach[k]: reach[k]=1; st.append(k)
        while st:
            k=st.pop(); y,x=divmod(k,bw)
            for nx,ny in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
                if 0<=nx<bw and 0<=ny<bh:
                    nk=ny*bw+nx
                    if not inside[nk] and not reach[nk]: reach[nk]=1; st.append(nk)
        hseen=bytearray(bw*bh); holes=[]
        for k in range(bw*bh):
            if not inside[k] and not reach[k] and not hseen[k]:
                st=[k]; hseen[k]=1; hp=[]
                while st:
                    j=st.pop(); hp.append(j); y,x=divmod(j,bw)
                    for nx,ny in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
                        if 0<=nx<bw and 0<=ny<bh:
                            nj=ny*bw+nx
                            if not inside[nj] and not reach[nj] and not hseen[nj]: hseen[nj]=1; st.append(nj)
                if len(hp)>=4: holes.append(hp)
        cu=(sum(xs)/len(xs)+0.5)/W; cv=1-(sum(ys)/len(ys)+0.5)/H
        patch=locate(mtl,cu,cv,grid)
        rec={"slot":slot,"patch":ci,"px":len(px),"bbox":(x0,y0,x1,y1),"centre":patch,"holes":[]}
        for hp in holes:
            hx=sum(j%bw for j in hp)/len(hp)+x0; hy=sum(j//bw for j in hp)/len(hp)+y0
            u=(hx+0.5)/W; v=1-(hy+0.5)/H
            # the mark's colour says what it is: yellow (255,245,104) thruster, grey (204,204,204) phaser emitter
            gi=[(y0+j//bw)*W+(x0+j%bw) for j in hp]
            mr=sum(data[3*i] for i in gi)/len(gi); mg=sum(data[3*i+1] for i in gi)/len(gi); mb=sum(data[3*i+2] for i in gi)/len(gi)
            kind="thruster" if mr-mb>60 else ("phaser" if max(mr,mg,mb)-min(mr,mg,mb)<25 and mr>120 else "?")
            rec["holes"].append((len(hp),locate(mtl,u,v,grid),kind,(round(mr),round(mg),round(mb))))
        out.append(rec)
def pawn(p): return (-p[1]*100, p[0]*100, p[2]*100)
def fmt(c): return "none" if not c else " | ".join("pawn %7.0f %7.0f %7.0f  n %5.2f %5.2f %5.2f"%(*pawn(a[0]),*pawn(a[1])) for a in c)
for r in out:
    print(f"slot {r['slot']} patch {r['patch']:2d} px {r['px']:5d} bbox {r['bbox']}  centre", fmt(r["centre"]))
    for n,h,kind,rgb in r["holes"]:
        print(f"      hole px {n:4d} {kind:8s} rgb {rgb} ", fmt(h))
import json
json.dump([{**r,"centre":r["centre"],"holes":r["holes"]} for r in out], open(S+"/rcsmarks.json","w"))
