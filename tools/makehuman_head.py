# Run in a folder with MakeHuman base.obj and the macro and face targets listed in feminine_targets.txt
# (from github.com/makehumancommunity/makehuman, makehuman/data). Writes mh_body.ply and mh_eyes.npy.
# Build a CC0 MakeHuman female head mesh (base hm08 + macro targets) and save body+eye spheres
import numpy as np, trimesh, sys
V=[];F=[];grp=None;faces={}
for line in open('base.obj'):
    if line.startswith('v '): V.append([float(x) for x in line.split()[1:4]])
    elif line.startswith('g '): grp=line.split()[1]
    elif line.startswith('f '):
        idx=[int(t.split('/')[0])-1 for t in line.split()[1:]]
        faces.setdefault(grp,[]).append(idx)
V=np.array(V)
def target(name,w):
    for line in open(name+'.target'):
        if line.startswith('#') or not line.strip(): continue
        i,x,y,z=line.split(); V[int(i)]+=w*np.array([float(x),float(y),float(z)])
for r in ['african','asian','caucasian']: target(r+'-female-young',1/3)
# feminine face details (CC0 MakeHuman modifiers)
for line in open('fem.txt'):
    t,w=line.split(); target(t.replace('/','_'),float(w))
tri=[]
for f in faces['body']:
    if len(f)==4: tri+= [[f[0],f[1],f[2]],[f[0],f[2],f[3]]]
    else: tri.append(f[:3])
m=trimesh.Trimesh(V,np.array(tri),process=True)
eyes=[V[np.unique(np.array(faces[g]))].mean(0) for g in ['joint-l-eye','joint-r-eye']]
print('verts',len(m.vertices),'bounds',m.bounds.round(2).tolist(),'eyes',np.round(eyes,3).tolist())
np.save('mh_eyes.npy',np.array(eyes)); m.export('mh_body.ply')
