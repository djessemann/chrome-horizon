# Bakes a head mesh into a signed distance grid for the Head shape.
# Usage: python3 tools/bake_head.py LeePerrySmith.glb -1.6 0.06 out
# Then blur (gaussian sigma 0.9), clamp to +-0.6, save as float16 z,y,x order, gzip to models/head.sdf.gz.
# Needs numpy, scipy, trimesh, networkx, pillow.
import sys, numpy as np, trimesh
from scipy.spatial import cKDTree
from scipy import ndimage
from PIL import Image
m=trimesh.load(sys.argv[1],force='mesh')
parts=m.split(only_watertight=False,repair=False)
# skin plus eyeballs; drop the mouth bag and stray bits so the head is solid
m=trimesh.util.concatenate([p for p in parts if len(p.faces)>10000 or (p.bounds[0][1]>1.2 and p.bounds[1][2]>1.5)])
print('faces',len(m.faces))
YCUT=float(sys.argv[2]); VOX=float(sys.argv[3])
v=m.vertices
keep=v[:,1]>YCUT-0.3
hv=v[keep]
pad=0.25
bmin=hv.min(0)-pad; bmax=hv.max(0)+pad; bmin[1]=YCUT; bmin[0]=-2.25; bmax[0]=2.25
n=np.ceil((bmax-bmin)/VOX).astype(int)+1
bmax=bmin+(n-1)*VOX
print('grid',n,n.prod(),'bmin',bmin,'bmax',bmax)
pts,fi=trimesh.sample.sample_surface(m,3000000,seed=1)
nrm=m.face_normals[fi]
tree=cKDTree(pts)
xs=[np.linspace(bmin[i],bmax[i],n[i]) for i in range(3)]
G=np.stack(np.meshgrid(*xs,indexing='ij'),-1).reshape(-1,3)
ud,idx=tree.query(G,workers=-1)
ud=ud.reshape(n)
# flood fill outside from all faces except bottom (y=0 layer)
wall=ud<VOX*0.9
seed=np.zeros(n,bool)
seed[0,:,:]=seed[-1,:,:]=True; seed[:,-1,:]=True; seed[:,:,0]=seed[:,:,-1]=True
seed&=~wall
lab,_=ndimage.label(~wall)
outl=np.unique(lab[seed]); outl=outl[outl>0]
outside=np.isin(lab,outl)
# near surface: sign from nearest sample normal
dv=(G-pts[idx]).reshape(tuple(n)+(3,))
sgn_n=np.sign(np.einsum('...k,...k',dv,nrm[idx].reshape(tuple(n)+(3,))))
sgn=np.where(outside,1.0,-1.0)
near=ud<VOX*2.5
sgn[near]=np.where(sgn_n[near]==0,1,sgn_n[near])
sd=ud*sgn
# positive pockets sealed inside the head (mouth bag, eye backs) count as solid
pl,_=ndimage.label(sd>0)
ok=np.unique(pl[outside]); ok=ok[ok>0]
sealed=(sd>0)&~np.isin(pl,ok)
sd[sealed]=-sd[sealed]
print('sealed voxels',sealed.sum())
print('inside frac',(sd<0).mean())
np.save(sys.argv[4],sd.astype(np.float32)); np.save(sys.argv[4]+'.meta.npy',np.array([*bmin,*bmax]))
# slices for checking
def sl(a): 
    a=np.clip(a/0.5,-1,1); return ((a*0.5+0.5)*255).astype(np.uint8)
mid=sd[np.argmin(abs(xs[0])),:,:]   # x middle: y,z plane
Image.fromarray(sl(mid[::-1,:])).resize((n[2]*3,n[1]*3)).save(sys.argv[4]+'_sideslice.png')
eyey=np.argmin(abs(xs[1]-2.2)) if xs[1].max()>2.2 else n[1]//2
Image.fromarray(sl(sd[:,n[1]//2,:].T)).resize((n[0]*3,n[2]*3)).save(sys.argv[4]+'_topslice.png')
