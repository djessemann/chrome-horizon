# Usage: python3 tools/bake_head.py mh_body.ply 5.0 0.03 1.0 out mh_eyes_fwd.npy 0.125
# Then blur (gaussian sigma 0.9), clamp to +-0.3, save as float16 z,y,x order, gzip to models/head.sdf.gz.
# Bake a head mesh (+ optional eyeball spheres) into a signed distance grid.
import sys, numpy as np, trimesh, gzip, json
from scipy.spatial import cKDTree
from scipy import ndimage
from PIL import Image
mesh, YCUT, VOX, XH, out = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4]), sys.argv[5]
m=trimesh.load(mesh,force='mesh')
parts=[m]
if len(sys.argv)>6:
    eyes=np.load(sys.argv[6]); r=float(sys.argv[7])
    for e in eyes:
        s=trimesh.creation.icosphere(4,r); s.apply_translation(e); parts.append(s)
m=trimesh.util.concatenate(parts)
v=m.vertices; hv=v[v[:,1]>YCUT]
pad=4*VOX
bmin=hv.min(0)-pad; bmax=hv.max(0)+pad; bmin[1]=YCUT; bmin[0]=-XH; bmax[0]=XH
n=np.ceil((bmax-bmin)/VOX).astype(int)+1; bmax=bmin+(n-1)*VOX
print('grid',n,n.prod(),'bmin',bmin.round(5).tolist(),'bmax',bmax.round(5).tolist(),'ytop',hv[:,1].max())
pts,fi=trimesh.sample.sample_surface(m,3000000,seed=1); nrm=m.face_normals[fi]
tree=cKDTree(pts)
xs=[np.linspace(bmin[i],bmax[i],n[i]) for i in range(3)]
G=np.stack(np.meshgrid(*xs,indexing='ij'),-1).reshape(-1,3)
ud,idx=tree.query(G,workers=-1); ud=ud.reshape(n)
# inside/outside by parity of crossings on a vertical ray from above each grid column
T=m.triangles; T=T[T[:,:,1].max(1)>YCUT-VOX]
cross=[[[] for _ in range(n[2])] for _ in range(n[0])]
for t in T:
    a2,b2,c2=t[:,[0,2]]
    lo=np.floor((t[:,[0,2]].min(0)-bmin[[0,2]])/VOX).astype(int); hi=np.ceil((t[:,[0,2]].max(0)-bmin[[0,2]])/VOX).astype(int)
    lo=np.maximum(lo,0); hi=np.minimum(hi,[n[0]-1,n[2]-1])
    det=(b2[0]-a2[0])*(c2[1]-a2[1])-(c2[0]-a2[0])*(b2[1]-a2[1])
    if abs(det)<1e-12: continue
    for i in range(lo[0],hi[0]+1):
        for k in range(lo[1],hi[1]+1):
            px,pz=bmin[0]+i*VOX+1e-7,bmin[2]+k*VOX+1.3e-7
            u=((b2[0]-px)*(c2[1]-pz)-(c2[0]-px)*(b2[1]-pz))/det
            v_=((c2[0]-px)*(a2[1]-pz)-(a2[0]-px)*(c2[1]-pz))/det
            w=1-u-v_
            if u>=0 and v_>=0 and w>=0: cross[i][k].append(u*t[0,1]+v_*t[1,1]+w*t[2,1])
inside=np.zeros(n,bool)
for i in range(n[0]):
    for k in range(n[2]):
        c=np.sort(cross[i][k])
        if len(c)==0: continue
        cnt=len(c)-np.searchsorted(c,xs[1],side='right')  # crossings above each y
        inside[i,:,k]=(cnt%2)==1
sd=np.where(inside,-ud,ud)
# seal positive pockets that don't reach the grid edge (mouth interior)
pl,_=ndimage.label(sd>0); edge=np.zeros(n,bool); edge[0]=edge[-1]=True; edge[:,-1]=True; edge[:,:,0]=edge[:,:,-1]=True
ok=np.unique(pl[edge]); ok=ok[ok>0]; sealed=(sd>0)&~np.isin(pl,ok); sd[sealed]=-sd[sealed]
print('inside frac',(sd<0).mean(),'sealed',sealed.sum())
np.save(out+'.npy',sd.astype(np.float32))
json.dump({'n':n.tolist(),'bmin':bmin.tolist(),'bmax':bmax.tolist(),'ytop':float(hv[:,1].max())},open(out+'.json','w'))
def sl(a): a=np.clip(a/(VOX*8),-1,1); return ((a*0.5+0.5)*255).astype(np.uint8)
Image.fromarray(sl(sd[np.argmin(abs(xs[0])),:,:][::-1,:])).resize((n[2]*3,n[1]*3)).save(out+'_side.png')
Image.fromarray(sl(sd[:,:,int(n[2]*0.75)][:,::-1].T)).resize((n[0]*3,n[1]*3)).save(out+'_front.png')
