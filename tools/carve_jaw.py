# Usage: python3 tools/carve_jaw.py 2.0,-0.44 0.85,-0.70 0.62,-1.6 0.25  (points are z,y in scan units)
# Run after bake_head.py; then blur (sigma 0.9), clamp to +-0.6, float16 z,y,x, gzip to models/head.sdf.gz.
# Carve away the sagging under-chin of the scanned head: remove the region below a clean jaw line
# and in front of a clean neck line, with a smooth blend.
import numpy as np, sys
from PIL import Image
sd=np.load('mesh/sd.npy'); bmin,bmax=np.load('mesh/sd.meta.npy').reshape(2,3)
n=np.array(sd.shape); xs=[np.linspace(bmin[i],bmax[i],n[i]) for i in range(3)]
X,Y,Z=np.meshgrid(*xs,indexing='ij')
C=np.array([float(a) for a in sys.argv[1].split(',')])   # chin underside point (z,y)
J=np.array([float(a) for a in sys.argv[2].split(',')])   # jaw/neck corner (z,y)
B=np.array([float(a) for a in sys.argv[3].split(',')])   # neck front at the bottom (z,y)
k=float(sys.argv[4])
def line(P,Q,up):
    d=Q-P; nrm=np.array([-d[1],d[0]]); nrm/=np.linalg.norm(nrm)
    if np.sign(nrm[1] if up else nrm[0])<0: nrm=-nrm
    return (Z-P[0])*nrm[0]+(Y-P[1])*nrm[1]
above=line(C,J,True)        # >0 above the jaw line
front=line(J,B,False)       # >0 in front of the neck line
dR=np.maximum(above,-front) # <0 inside the region to remove
def smax(a,b,k):
    h=np.maximum(k-np.abs(a-b),0)/k; return np.maximum(a,b)+h*h*k*0.25
out=smax(sd,-dR,k)
np.save('mesh/sd_jaw.npy',out)
ix=np.argmin(abs(xs[0]))
for nm,a in [('before',sd),('after',out)]:
    Image.fromarray(((a[ix]<0)*255).astype(np.uint8)[::-1]).resize((n[2]*4,n[1]*4),0).save('mesh/prof_'+nm+'.png')
ix2=np.argmin(abs(xs[0]-0.9))
Image.fromarray(((out[ix2]<0)*255).astype(np.uint8)[::-1]).resize((n[2]*4,n[1]*4),0).save('mesh/prof_after_side.png')
