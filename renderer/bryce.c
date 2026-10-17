// Bryce-style landscape raytracer. Terrain heightfield + SDF objects + water/checker plane.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct { float x, y, z; } v3;
static inline v3 V(float x, float y, float z) { v3 r = {x, y, z}; return r; }
static inline v3 add(v3 a, v3 b) { return V(a.x+b.x, a.y+b.y, a.z+b.z); }
static inline v3 sub(v3 a, v3 b) { return V(a.x-b.x, a.y-b.y, a.z-b.z); }
static inline v3 mul(v3 a, float s) { return V(a.x*s, a.y*s, a.z*s); }
static inline v3 mulv(v3 a, v3 b) { return V(a.x*b.x, a.y*b.y, a.z*b.z); }
static inline float dot(v3 a, v3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static inline float len(v3 a) { return sqrtf(dot(a,a)); }
static inline v3 norm(v3 a) { float l=len(a); return l>0?mul(a,1.f/l):a; }
static inline v3 cross(v3 a, v3 b){ return V(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x); }
static inline v3 mixv(v3 a, v3 b, float t){ return add(a, mul(sub(b,a),t)); }
static inline float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}
static inline float sstep(float a,float b,float x){float t=clampf((x-a)/(b-a),0,1);return t*t*(3-2*t);}
static inline float maxf(float a,float b){return a>b?a:b;}
static inline float minf(float a,float b){return a<b?a:b;}
static inline v3 reflectv(v3 i, v3 n){ return sub(i, mul(n, 2*dot(i,n))); }
static v3 hexc(unsigned h){ return V(powf(((h>>16)&255)/255.f,2.2f), powf(((h>>8)&255)/255.f,2.2f), powf((h&255)/255.f,2.2f)); }

static uint32_t SEED = 1;
static inline float hash2(int x, int y){
  uint32_t h = (uint32_t)x*374761393u + (uint32_t)y*668265263u + SEED*2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16;
  return (h & 0xffffff) * (1.0f/16777215.0f);
}
// value noise in [-1,1] with analytic derivatives
static inline float noised(float x, float y, float *dx, float *dy){
  float fx0 = floorf(x), fy0 = floorf(y);
  int ix = (int)fx0, iy = (int)fy0;
  float fx = x - fx0, fy = y - fy0;
  float ux = fx*fx*fx*(fx*(fx*6-15)+10), uy = fy*fy*fy*(fy*(fy*6-15)+10);
  float dux = 30*fx*fx*(fx*(fx-2)+1), duy = 30*fy*fy*(fy*(fy-2)+1);
  float a = hash2(ix,iy), b = hash2(ix+1,iy), c = hash2(ix,iy+1), d = hash2(ix+1,iy+1);
  float k1 = b-a, k2 = c-a, k4 = a-b-c+d;
  *dx = 2*dux*(k1+k4*uy); *dy = 2*duy*(k2+k4*ux);
  return -1 + 2*(a + k1*ux + k2*uy + k4*ux*uy);
}
static inline float noise(float x, float y){ float a,b; return noised(x,y,&a,&b); }
static float fbm(float x, float y, int oct){
  float a=0, b=0.5f;
  for(int i=0;i<oct;i++){ a+=b*noise(x,y); b*=0.5f; float nx=1.6f*x-1.2f*y; y=1.2f*x+1.6f*y; x=nx; }
  return a;
}

enum { O_SPHERE, O_TORUS, O_BOX };
enum { M_CHROME, M_GLOSS, M_MATTE };
typedef struct { int type; v3 p; float a, b, c; int mat; v3 col; float rx, rz; float cx,sx,cz,sz; } Obj;

typedef struct {
  unsigned seed;
  float tscale, thgt, toff, terrace, stepK, maxH;
  float vx, vz, vrad, vdepth;
  int useFloor; float floorH;
  float water; int waterMat; v3 waterDeep, waterShallow; float waterMirror;
  float checkSize; v3 chkA, chkB;
  v3 rock0, rock1, rock2, flatc, snow; float snowH, bandFreq, flatTh;
  v3 skyZen, skyHor, sunCol, fogCol, sun; float fogD, sunGlow;
  float cloudCover, cloudH; v3 cloudCol, cloudShade;
  int nmoons; v3 mdir[3]; float mrad[3]; v3 mcol[3];
  v3 camP, camT; float fov, exposure;
  Obj obj[10]; int nobj;
} Scene;
static Scene S;

// ---------------- terrain ----------------
static inline float terrainRaw(float x, float z, int oct){
  float a=0,b=1,dx=0,dz=0;
  for(int i=0;i<oct;i++){
    float nx,nz; float n=noised(x,z,&nx,&nz);
    dx+=nx; dz+=nz; a+=b*n/(1+dx*dx+dz*dz); b*=0.5f;
    float tx=1.6f*x-1.2f*z; z=1.2f*x+1.6f*z; x=tx;
  }
  return a;
}
static inline float H(float x, float z, int oct){
  float h = S.thgt*terrainRaw(x/S.tscale + 13.1f, z/S.tscale - 7.7f, oct) + S.toff;
  if (S.vdepth != 0){ float dx=x-S.vx, dz=z-S.vz; h -= S.vdepth*expf(-(dx*dx+dz*dz)/(S.vrad*S.vrad)); }
  if (S.terrace > 0){
    float f = h/S.terrace, fi = floorf(f), fr = f-fi;
    fr = sstep(0.62f, 1.0f, fr);
    h = (fi + fr)*S.terrace;
  }
  if (S.useFloor){
    float fl = S.floorH + 1.2f*noise(x*0.03f, z*0.05f) + 0.4f*noise(x*0.12f,z*0.2f);
    if (h < fl) h = fl;
  }
  return h;
}
static float marchTerrain(v3 ro, v3 rd, float tmax){
  float t = 0.5f, tp = 0.5f;
  for(int i=0;i<700;i++){
    v3 p = add(ro, mul(rd,t));
    if (rd.y >= 0 && p.y > S.maxH) return -1;
    float h = p.y - H(p.x, p.z, 8);
    if (h < 0.002f*t){
      float a=tp, b=t;
      for(int k=0;k<8;k++){ float m=0.5f*(a+b); v3 q=add(ro,mul(rd,m)); if(q.y-H(q.x,q.z,8)<0) b=m; else a=m; }
      return 0.5f*(a+b);
    }
    if (t > tmax) return -1;
    tp = t;
    t += maxf(S.stepK*h, 0.002f*t + 0.02f);
  }
  return -1;
}
static v3 terrainNormal(v3 p, float t){
  float e = 0.03f + 0.0015f*t;
  int oct = t < 300 ? 13 : (t < 900 ? 11 : 9);
  float hx = H(p.x-e,p.z,oct) - H(p.x+e,p.z,oct);
  float hz = H(p.x,p.z-e,oct) - H(p.x,p.z+e,oct);
  return norm(V(hx, 2*e, hz));
}

// ---------------- objects ----------------
static inline v3 toLocal(const Obj *o, v3 p){
  p = sub(p, o->p);
  // rotate about z then x (inverse)
  float x = o->cz*p.x + o->sz*p.y, y = -o->sz*p.x + o->cz*p.y; p.x=x; p.y=y;
  y = o->cx*p.y + o->sx*p.z; float z = -o->sx*p.y + o->cx*p.z; p.y=y; p.z=z;
  return p;
}
static inline float sdObj(const Obj *o, v3 p){
  v3 q = toLocal(o,p);
  switch(o->type){
    case O_SPHERE: return len(q) - o->a;
    case O_TORUS: { float r = sqrtf(q.x*q.x+q.z*q.z) - o->a; return sqrtf(r*r+q.y*q.y) - o->b; }
    case O_BOX: { float rr = 0.6f; v3 d = V(fabsf(q.x)-o->a+rr, fabsf(q.y)-o->b+rr, fabsf(q.z)-o->c+rr);
      v3 m = V(maxf(d.x,0),maxf(d.y,0),maxf(d.z,0));
      return len(m) + minf(maxf(d.x,maxf(d.y,d.z)),0) - rr; }
  }
  return 1e9f;
}
static float mapObj(v3 p, int *id){
  float d = 1e9f; int best=-1;
  for(int i=0;i<S.nobj;i++){ float di = sdObj(&S.obj[i], p); if(di<d){d=di;best=i;} }
  if(id) *id=best; return d;
}
static float marchObj(v3 ro, v3 rd, float tmax, int *id){
  float t = 0.01f;
  for(int i=0;i<256;i++){
    v3 p = add(ro,mul(rd,t));
    float d = mapObj(p, id);
    if (d < 0.0005f*t + 0.001f) return t;
    t += d;
    if (t > tmax) return -1;
  }
  return -1;
}
static v3 objNormal(v3 p, int id){
  float e=0.002f; const Obj *o=&S.obj[id];
  return norm(V(sdObj(o,add(p,V(e,0,0)))-sdObj(o,sub(p,V(e,0,0))),
                sdObj(o,add(p,V(0,e,0)))-sdObj(o,sub(p,V(0,e,0))),
                sdObj(o,add(p,V(0,0,e)))-sdObj(o,sub(p,V(0,0,e)))));
}

static float shadow(v3 p, v3 l){
  float res = 1, t = 0.15f;
  float topY = S.maxH;
  for(int i=0;i<S.nobj;i++) topY = maxf(topY, S.obj[i].p.y + S.obj[i].a + S.obj[i].b + 5);
  for(int i=0;i<110;i++){
    v3 q = add(p, mul(l,t));
    if (q.y > topY) break;
    float h = q.y - H(q.x,q.z,6);
    if (S.nobj) h = minf(h, mapObj(q, NULL));
    res = minf(res, 10*h/t);
    if (res < 0.002f) return 0;
    t += clampf(h, 0.1f + 0.01f*t, 30);
    if (t > 1500) break;
  }
  return clampf(res,0,1);
}

// ---------------- sky ----------------
static v3 sky(v3 rd){
  float y = rd.y;
  v3 col = mixv(S.skyHor, S.skyZen, powf(clampf(y,0,1), 0.5f));
  if (y < 0) col = mixv(S.skyHor, S.fogCol, clampf(-y*8,0,1));
  float s = maxf(dot(rd,S.sun),0);
  col = add(col, mul(S.sunCol, S.sunGlow*(0.18f*powf(s,5) + 0.35f*powf(s,48) + 0.6f*powf(s,600))));
  // moons
  for(int m=0;m<S.nmoons;m++){
    v3 md = S.mdir[m]; float c = dot(rd, md); float cr = cosf(S.mrad[m]);
    if (c > cr - 0.0002f){
      v3 u = norm(cross(md, V(0,1,0))), v = cross(u, md);
      float sr = sinf(S.mrad[m]);
      float x = dot(rd,u)/sr, yy = dot(rd,v)/sr; float r2 = x*x+yy*yy;
      if (r2 < 1.02f){
        float z = sqrtf(maxf(1-r2,0));
        v3 n = add(add(mul(u,x), mul(v,yy)), mul(md,-z));
        float lit = maxf(dot(n, S.sun),0)*0.9f + 0.06f;
        float tex = 0.75f + 0.35f*fbm(x*3+m*7, yy*3, 5);
        v3 mc = mul(S.mcol[m], lit*tex*1.4f);
        float a = sstep(1.02f, 0.96f, r2);
        col = mixv(col, add(mc, mul(col,0.25f)), a*0.92f);
      }
    }
  }
  if (S.sunGlow > 0) col = add(col, mul(S.sunCol, 12*sstep(0.99985f, 0.99993f, s)));
  // clouds
  if (y > 0.004f && S.cloudCover < 1){
    float t = S.cloudH / y;
    float cx = rd.x*t, cz = rd.z*t;
    float c = fbm(cx*0.0011f+3.3f, cz*0.0011f, 7);
    float cov = sstep(S.cloudCover, S.cloudCover+0.3f, c);
    if (cov > 0){
      float c2 = fbm((cx+S.sun.x*120)*0.0011f+3.3f, (cz+S.sun.z*120)*0.0011f, 7);
      float lit = clampf(0.55f + (c-c2)*4.0f, 0, 1);
      v3 cc = mixv(S.cloudShade, S.cloudCol, lit);
      cc = add(cc, mul(S.sunCol, 0.35f*powf(s,6)));
      float fade = sstep(0.0f, 0.12f, y);
      col = mixv(col, cc, cov*fade*0.92f);
    }
  }
  return col;
}

static v3 applyFog(v3 col, float t, v3 rd){
  float s = maxf(dot(rd,S.sun),0);
  v3 fc = mixv(S.fogCol, mulv(S.fogCol, mul(S.sunCol,0.9f)), powf(s,8)*0.7f);
  float f = 1 - expf(-t*S.fogD);
  return mixv(col, fc, f);
}

// ---------------- shading ----------------
static v3 trace(v3 ro, v3 rd, int depth);

static v3 lightDiffuse(v3 alb, v3 n, float sh, float ao){
  float dif = clampf(dot(n,S.sun),0,1)*sh;
  float amb = 0.5f + 0.5f*n.y;
  v3 back = norm(V(-S.sun.x, 0, -S.sun.z));
  float bac = clampf(dot(n, back),0,1);
  v3 lin = mul(S.sunCol, dif*1.25f);
  lin = add(lin, mul(S.skyZen, amb*0.9f*ao));
  lin = add(lin, mul(S.skyHor, 0.25f*amb*ao));
  lin = add(lin, mul(mulv(S.sunCol, alb), bac*0.15f*ao));
  return mulv(alb, lin);
}

static v3 shadeTerrain(v3 p, v3 rd, float t){
  v3 n = terrainNormal(p, t);
  float nn = fbm(p.x*0.012f, p.z*0.012f, 4);
  float s = p.y*S.bandFreq + nn*5;
  float b1 = 0.5f+0.5f*sinf(s);
  float b2 = 0.5f+0.5f*sinf(s*2.31f+1.3f);
  v3 alb = mixv(S.rock0, S.rock1, sstep(0.25f,0.75f,b1));
  alb = mixv(alb, S.rock2, sstep(0.55f,0.95f,b2)*0.75f);
  alb = mul(alb, 0.8f + 0.45f*fbm(p.x*0.25f, p.z*0.25f + p.y*0.2f, 4));
  float fl = sstep(S.flatTh, S.flatTh+0.08f, n.y + 0.08f*nn);
  alb = mixv(alb, S.flatc, fl);
  float sn = sstep(S.snowH-8, S.snowH+8, p.y + 25*nn) * sstep(0.45f,0.7f,n.y);
  alb = mixv(alb, S.snow, sn);
  if (S.water > -1e8 && S.waterMat==0){
    float wet = sstep(S.water+2.0f, S.water+0.2f, p.y);
    alb = mul(alb, 1 - 0.45f*wet);
  }
  // cheap AO from height relative to neighbourhood
  float ao = clampf(0.55f + (p.y - H(p.x, p.z, 3))*0.03f + 0.45f*n.y, 0.35f, 1.0f);
  float sh = shadow(add(p, mul(n, 0.05f+0.002f*t)), S.sun);
  v3 col = lightDiffuse(alb, n, sh, ao);
  // snow sparkle / spec
  v3 h = norm(sub(S.sun, rd));
  col = add(col, mul(S.sunCol, sn*0.25f*powf(maxf(dot(n,h),0),32)*sh));
  return col;
}

static v3 shadePlane(v3 p, v3 rd, float t, int depth){
  float sh = shadow(add(p, V(0,0.02f,0)), S.sun);
  if (S.waterMat == 1){
    // mirror checker floor
    float cs = S.checkSize;
    int ci = ((int)floorf(p.x/cs) + (int)floorf(p.z/cs)) & 1;
    v3 c = ci ? S.chkA : S.chkB;
    float fa = sstep(150, 900, t);
    c = mixv(c, mul(add(S.chkA,S.chkB),0.5f), fa);
    v3 n = V(0,1,0);
    v3 dif = lightDiffuse(c, n, sh, 1);
    v3 r = reflectv(rd, n);
    float fres = 0.18f + 0.82f*powf(1-maxf(-rd.y,0),5);
    v3 refl = depth>0 ? trace(add(p,V(0,0.01f,0)), r, depth-1) : sky(r);
    return mixv(dif, refl, fres*S.waterMirror);
  }
  float amp = 0.05f/(1+t*0.02f);
  float gx,gz,gx2,gz2;
  noised(p.x*0.35f, p.z*0.9f, &gx, &gz);
  noised(p.x*1.3f+5, p.z*2.1f, &gx2, &gz2);
  v3 n = norm(V(-(gx*0.35f+gx2*0.5f)*amp, 1, -(gz*0.9f+gz2*1.0f)*amp));
  v3 r = reflectv(rd, n); if (r.y < 0.001f) r.y = 0.001f; r = norm(r);
  float fres = 0.04f + 0.96f*powf(1-maxf(dot(mul(rd,-1),n),0),5);
  fres = S.waterMirror + (1-S.waterMirror)*fres;
  v3 refl = depth>0 ? trace(add(p,V(0,0.02f,0)), r, depth-1) : sky(r);
  float dep = maxf(S.water - H(p.x,p.z,5), 0);
  v3 body = mixv(S.waterShallow, S.waterDeep, 1-expf(-dep*0.12f));
  body = mul(body, 0.35f + 0.65f*sh*clampf(S.sun.y*2,0,1));
  v3 col = mixv(body, refl, fres);
  col = add(col, mul(S.sunCol, 6*sh*powf(maxf(dot(r,S.sun),0), 700)));
  return col;
}

static v3 shadeObj(v3 p, v3 rd, int id, int depth){
  const Obj *o = &S.obj[id];
  v3 n = objNormal(p, id);
  float sh = shadow(add(p, mul(n,0.03f)), S.sun);
  v3 r = reflectv(rd, n);
  v3 h = norm(sub(S.sun, rd));
  float spec = powf(maxf(dot(n,h),0), 250)*sh;
  if (o->mat == M_CHROME){
    v3 refl = depth>0 ? trace(add(p,mul(n,0.03f)), r, depth-1) : sky(r);
    return add(mulv(refl, o->col), mul(S.sunCol, spec*5));
  }
  v3 dif = lightDiffuse(o->col, n, sh, 1);
  if (o->mat == M_GLOSS){
    float fres = 0.06f + 0.94f*powf(1-maxf(dot(mul(rd,-1),n),0),5);
    v3 refl = depth>0 ? trace(add(p,mul(n,0.03f)), r, depth-1) : sky(r);
    v3 col = mixv(dif, refl, fres*0.85f + 0.08f);
    return add(col, mul(S.sunCol, spec*3));
  }
  return dif;
}

static v3 trace(v3 ro, v3 rd, int depth){
  float tmax = 3000;
  float tw = -1;
  if (S.water > -1e8f && rd.y < 0 && ro.y > S.water) tw = (S.water - ro.y)/rd.y;
  float lim = tw > 0 ? tw : tmax;
  float tt = marchTerrain(ro, rd, lim);
  if (tt > 0) lim = tt;
  int oid = -1; float to = S.nobj ? marchObj(ro, rd, lim, &oid) : -1;
  v3 col; float t;
  if (to > 0){ t = to; col = shadeObj(add(ro,mul(rd,t)), rd, oid, depth); }
  else if (tt > 0){ t = tt; col = shadeTerrain(add(ro,mul(rd,t)), rd, t); }
  else if (tw > 0){ t = tw; col = shadePlane(add(ro,mul(rd,t)), rd, t, depth); }
  else return sky(rd);
  return applyFog(col, t, rd);
}

// ---------------- scenes ----------------
static void addObj(int type, v3 p, float a, float b, float c, int mat, unsigned col, float rx, float rz){
  Obj *o = &S.obj[S.nobj++];
  o->type=type; o->p=p; o->a=a; o->b=b; o->c=c; o->mat=mat; o->col=hexc(col); o->rx=rx; o->rz=rz;
  o->cx=cosf(rx); o->sx=sinf(rx); o->cz=cosf(rz); o->sz=sinf(rz);
}
static void baseScene(void){
  memset(&S,0,sizeof S);
  S.stepK = 0.4f; S.water = -1e9f; S.waterMirror = 0.1f; S.fov = 55; S.exposure = 1.0f;
  S.snowH = 1e9f; S.sunGlow = 1; S.cloudCover = 1.0f; S.cloudH = 1200;
}

static void scene(int id){
  baseScene();
  switch(id){
  case 1: // Chrome Lake: classic Bryce
    S.seed=7; S.tscale=240; S.thgt=230; S.toff=25; S.maxH=380;
    S.vx=0; S.vz=110; S.vrad=170; S.vdepth=190;
    S.water=0; S.waterDeep=hexc(0x0a2342); S.waterShallow=hexc(0x2d7f86); S.waterMirror=0.12f;
    S.rock0=hexc(0x86705a); S.rock1=hexc(0xb89f7d); S.rock2=hexc(0x5a4636); S.flatc=hexc(0x66763a);
    S.snow=hexc(0xf4f6fa); S.snowH=120; S.bandFreq=0.28f; S.flatTh=0.82f;
    S.skyZen=hexc(0x2659b8); S.skyHor=hexc(0xbcd6ee); S.fogCol=hexc(0xb4cde6); S.fogD=0.0006f;
    S.sun=norm(V(-0.65f,0.48f,0.35f)); S.sunCol=mul(hexc(0xfff0d6),2.0f); S.sunGlow=0.6f;
    S.cloudCover=0.08f; S.cloudH=1400; S.cloudCol=hexc(0xffffff); S.cloudShade=hexc(0x8a9ab8);
    S.camP=V(0,16,0); S.camT=V(0,24,300); S.fov=58;
    addObj(O_SPHERE, V(-22,15,115), 11,0,0, M_CHROME, 0xf0f0f0, 0,0);
    addObj(O_SPHERE, V(32,7,150), 5,0,0, M_GLOSS, 0xd8261c, 0,0);
    addObj(O_TORUS, V(14,28,190), 9,1.8f,0, M_CHROME, 0xffd27a, 1.3f,0.4f);
    break;
  case 2: // Red Mesa: alien desert
    S.seed=21; S.tscale=210; S.thgt=190; S.toff=10; S.maxH=260; S.terrace=16; S.stepK=0.25f;
    S.vx=0; S.vz=170; S.vrad=170; S.vdepth=110;
    S.useFloor=1; S.floorH=0;
    S.rock0=hexc(0xa63a22); S.rock1=hexc(0xd9814a); S.rock2=hexc(0xf2cf96); S.flatc=hexc(0xd8a26a);
    S.bandFreq=0.75f; S.flatTh=0.86f;
    S.skyZen=hexc(0x3b2468); S.skyHor=hexc(0xf4b07a); S.fogCol=hexc(0xe6a07a); S.fogD=0.0008f;
    S.sun=norm(V(-0.75f,0.22f,0.55f)); S.sunCol=mul(hexc(0xffd09a),2.3f); S.sunGlow=1.0f;
    S.cloudCover=0.32f; S.cloudH=1600; S.cloudCol=hexc(0xffd7c0); S.cloudShade=hexc(0x8a5a7a);
    S.nmoons=2;
    S.mdir[0]=norm(V(0.38f,0.30f,1)); S.mrad[0]=0.10f; S.mcol[0]=hexc(0xe2d8f0);
    S.mdir[1]=norm(V(0.62f,0.47f,1)); S.mrad[1]=0.035f; S.mcol[1]=hexc(0xbfe8dc);
    S.camP=V(0,9,0); S.camT=V(0,26,300); S.fov=60;
    addObj(O_TORUS, V(8,44,175), 17,3.2f,0, M_CHROME, 0xf2f2f2, 1.15f,0.35f);
    addObj(O_SPHERE, V(-28,20,130), 8,0,0, M_GLOSS, 0x1f5fd6, 0,0);
    addObj(O_SPHERE, V(22,10,95), 4,0,0, M_CHROME, 0xf0f0f0, 0,0);
    break;
  case 3: // Sunset Lake
    S.seed=99; S.tscale=280; S.thgt=210; S.toff=10; S.maxH=360;
    S.vx=0; S.vz=140; S.vrad=230; S.vdepth=210;
    S.water=0; S.waterDeep=hexc(0x070a1c); S.waterShallow=hexc(0x1e2a3e); S.waterMirror=0.25f;
    S.rock0=hexc(0x3a2a3a); S.rock1=hexc(0x5c3a4a); S.rock2=hexc(0x251e2e); S.flatc=hexc(0x2c3628);
    S.snow=hexc(0xf6e4ec); S.snowH=130; S.bandFreq=0.3f; S.flatTh=0.84f;
    S.skyZen=hexc(0x1b1650); S.skyHor=hexc(0xff8a40); S.fogCol=hexc(0xc8705a); S.fogD=0.0008f;
    S.sun=norm(V(0.1f,0.085f,1)); S.sunCol=mul(hexc(0xffa060),2.4f); S.sunGlow=1.4f;
    S.cloudCover=0.14f; S.cloudH=1300; S.cloudCol=hexc(0xff9c86); S.cloudShade=hexc(0x4a2850);
    S.camP=V(0,7,0); S.camT=V(0,14,300); S.fov=60;
    addObj(O_SPHERE, V(-20,8,75), 6.5f,0,0, M_CHROME, 0xf0f0f0, 0,0);
    addObj(O_BOX, V(24,11,100), 5,5,5, M_CHROME, 0xe8e8f0, 0.62f,0.55f);
    addObj(O_TORUS, V(2,6,140), 6,1.4f,0, M_GLOSS, 0x2ad0c0, 1.5708f,0.0f);
    break;
  case 4: // Checker World
    S.seed=3; S.tscale=240; S.thgt=230; S.toff=0; S.maxH=340;
    S.vx=0; S.vz=150; S.vrad=650; S.vdepth=300;
    S.water=0; S.waterMat=1; S.checkSize=14; S.chkA=hexc(0xf2f2f2); S.chkB=hexc(0x15151c); S.waterMirror=0.55f;
    S.rock0=hexc(0x6a78a0); S.rock1=hexc(0xa3b2cc); S.rock2=hexc(0x46506e); S.flatc=hexc(0xeef2fa);
    S.snow=hexc(0xffffff); S.snowH=70; S.bandFreq=0.25f; S.flatTh=0.8f;
    S.skyZen=hexc(0x0c2a80); S.skyHor=hexc(0x96dcf2); S.fogCol=hexc(0xa2d2ee); S.fogD=0.0007f;
    S.sun=norm(V(0.55f,0.55f,-0.35f)); S.sunCol=mul(hexc(0xfff4e2),1.9f); S.sunGlow=0.5f;
    S.cloudCover=0.18f; S.cloudH=1500; S.cloudCol=hexc(0xffffff); S.cloudShade=hexc(0x7088b8);
    S.camP=V(0,22,-30); S.camT=V(0,12,150); S.fov=55;
    addObj(O_SPHERE, V(0,15,95), 15,0,0, M_CHROME, 0xf4f4f4, 0,0);
    addObj(O_SPHERE, V(-38,9,72), 9,0,0, M_GLOSS, 0xe0201c, 0,0);
    addObj(O_SPHERE, V(34,6.5f,60), 6.5f,0,0, M_GLOSS, 0xf2c014, 0,0);
    addObj(O_TORUS, V(-14,34,160), 13,2.6f,0, M_CHROME, 0xffd890, 1.5708f,0.25f);
    addObj(O_BOX, V(46,10,140), 7,10,7, M_GLOSS, 0x1aa860, 0,0.0f);
    break;
  }
  SEED = S.seed;
}

int main(int argc, char **argv){
  int id = argc>1 ? atoi(argv[1]) : 1;
  int W = argc>2 ? atoi(argv[2]) : 640, Hh = argc>3 ? atoi(argv[3]) : 360;
  int SS = argc>4 ? atoi(argv[4]) : 1;
  const char *out = argc>5 ? argv[5] : "out.ppm";
  scene(id);
  v3 fw = norm(sub(S.camT, S.camP));
  v3 rt = norm(cross(fw, V(0,1,0)));
  v3 up = cross(rt, fw);
  float f = 1.0f/tanf(S.fov*0.5f*3.14159265f/180.f) * ((float)W/Hh) * 0.5f * 2.0f / ((float)W/Hh);
  // f relative to vertical half-height = 1
  f = 1.0f/tanf(S.fov*0.5f*3.14159265f/180.f);
  float *img = malloc(sizeof(float)*3*W*Hh);
  float aspect = (float)W/Hh;
  #pragma omp parallel for schedule(dynamic,2)
  for(int j=0;j<Hh;j++){
    for(int i=0;i<W;i++){
      v3 acc = V(0,0,0);
      for(int sy=0;sy<SS;sy++) for(int sx=0;sx<SS;sx++){
        float px = (i + (sx+0.5f)/SS)/W*2-1, py = 1-(j + (sy+0.5f)/SS)/Hh*2;
        // horizontal fov
        v3 rd = norm(add(add(mul(fw, f), mul(rt, px)), mul(up, py/aspect)));
        acc = add(acc, trace(S.camP, rd, 2));
      }
      acc = mul(acc, 1.0f/(SS*SS));
      img[3*(j*W+i)+0]=acc.x; img[3*(j*W+i)+1]=acc.y; img[3*(j*W+i)+2]=acc.z;
    }
  }
  FILE *fp = fopen(out,"wb");
  fprintf(fp,"P6\n%d %d\n255\n",W,Hh);
  for(int j=0;j<Hh;j++) for(int i=0;i<W;i++){
    float u = (float)i/W-0.5f, v=(float)j/Hh-0.5f;
    float vig = 1 - 0.35f*(u*u+v*v)*1.6f;
    for(int c=0;c<3;c++){
      float x = img[3*(j*W+i)+c]*S.exposure*vig;
      x = x/(1+0.12f*x);           // gentle shoulder
      x = powf(clampf(x,0,1), 1/2.2f);
      fputc((int)(x*255+0.5f), fp);
    }
  }
  fclose(fp);
  return 0;
}
