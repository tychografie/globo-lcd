import cadquery as cq, trimesh, numpy as np, manifold3d as mf, json, os, pickle
OUT=os.path.expanduser('~/tp/out')
PB=111.235; CX,CZ=113.84,17.0
C_USB=0.20; C_PCB_T=0.15; C_PCB_W=0.20
Y_FRONT=78.0; Y_BACK=72.0; Y_USBPOCKET=73.0
def tomesh(shape,tol=0.01):
    v,f=shape.tessellate(tol,0.15)
    return trimesh.Trimesh(np.array([(p.x,p.y,p.z) for p in v]),np.array(f),process=True)
def tomf(tm): return mf.Manifold(mf.Mesh(np.asarray(tm.vertices,dtype=np.float32),np.asarray(tm.faces,dtype=np.uint32)))
def mf2tm(m):
    me=m.to_mesh(); return trimesh.Trimesh(me.vert_properties[:,:3],me.tri_verts)
def wp(y): return cq.Workplane(cq.Plane(origin=(0,y,0),xDir=(1,0,0),normal=(0,-1,0)))  # local x=X, local y=Z, extrude -> -Y
# openingen
usb_w=3.21+2*C_USB; usb_h=8.94+2*C_USB
slot_x0=PB-C_PCB_T; slot_x1=PB+1.0+C_PCB_T; slot_z0=11.0-C_PCB_W; slot_z1=23.0+C_PCB_W
def cut_openings(body, boss=(121.0,16.0)):
    usb=wp(Y_FRONT+0.5).center(CX,CZ).rect(usb_w,usb_h).extrude(Y_FRONT+0.5-Y_USBPOCKET).edges('|Y').fillet(1.0+C_USB)
    slot=wp(Y_FRONT+0.5).center((slot_x0+slot_x1)/2,(slot_z0+slot_z1)/2).rect(slot_x1-slot_x0,slot_z1-slot_z0).extrude(Y_FRONT+1-Y_BACK+1)
    # oor = plat lipje dat op de boss rust: alles voor y=74.6 rond/naast de boss weg (0.1 mm speling tot bossvlak 74.7)
    Y_EAR=74.6
    if boss[0]>CX: stepbox=wp(Y_FRONT+0.5).center((117.5+130)/2,CZ).rect(130-117.5,30).extrude(Y_FRONT+0.5-Y_EAR)
    else:          stepbox=wp(Y_FRONT+0.5).center((100+109.3)/2,CZ).rect(109.3-100,30).extrude(Y_FRONT+0.5-Y_EAR)
    pocket=stepbox
    screw=wp(Y_FRONT+0.5).center(*boss).circle(1.2).extrude(10)
    insert_relief=wp(74.61).center(*boss).circle(2.4).extrude(74.61-73.5)   # ruimte voor uitstekende insert / smeltrandje: Ø4.8, 1.1 mm diep
    # body=body.cut(insert_relief)  # verwijderd: oor helemaal vlak
    sq=wp(Y_FRONT+0.5).center(PB+1.0+0.6, CZ).rect(1.6, usb_h).extrude(Y_FRONT+0.5-Y_USBPOCKET)   # vierkante onderhoeken USB (lipjes)
    tail=wp(Y_USBPOCKET+0.01).center(PB+0.7, CZ).rect(1.9, usb_h).extrude(2.0)                      # ruimte soldeerpootjes achter USB
    body=body.cut(usb).cut(sq).cut(tail).cut(slot).cut(pocket).cut(screw)
    # inschuif-afschuining aan de voorkant (board schuift er van voren in)
    try: body=body.faces('<Y').edges().chamfer(0.4) if False else body
    except Exception as e: print('chamfer fail',e)
    return body
def lead_in(body):
    out=body
    for box in [((CX-usb_w/2-0.1,Y_FRONT-0.05,CZ-usb_h/2-0.1),(CX+usb_w/2+0.1,Y_FRONT+0.05,CZ+usb_h/2+0.1))]:
        try:
            t=out.faces('>Y').edges(cq.selectors.BoxSelector(*box)).chamfer(0.35)
            if t.val().isValid(): out=t
            else: print('lead-in invalid, skipped')
        except Exception as e: print('lead-in fail',str(e)[:60])
    return out
def _old_lead_in(body):
    # afschuining rond USB- en print-opening op voorvlak
    try:
        return body.faces('>Y').edges(cq.selectors.BoxSelector((PB-1,Y_FRONT-0.1,9),(117,Y_FRONT+0.1,25))).chamfer(0.4)
    except Exception as e:
        print('lead-in fail',e); return body
def plate(sk, thick=Y_FRONT-Y_BACK, r=1.2):
    b=wp(Y_FRONT).placeSketch(sk.clean()).extrude(thick)
    for rr in [r, r*0.75, r*0.5, 0.3]:
        try:
            return b.edges('|Y').fillet(rr)
        except Exception as e:
            pass
    print('no fillet'); return b
V={}
# 1 Compact: afgeronde rechthoek + oor
sk=(cq.Sketch().push([(113.4,17.0)]).rect(8.2,16.0)
    .push([(119.6,16.0)]).rect(5.2,7.0,mode='a')
    .push([(122.0,16.0)]).circle(4.2,mode='a')
    )
V['A_compact']=dict(body=plate(sk),boss=(122.0,16.0))
# 2 Druppel/kraag: organische vorm (hull van USB-blok en oor)
pts=[]
import math
for a in np.linspace(0,2*math.pi,72,endpoint=False):
    pts.append((122.0+4.2*math.cos(a),16.0+4.2*math.sin(a)))
for (x,z) in [(109.4,9.2),(117.4,9.2),(117.4,24.8),(109.4,24.8)]:
    for a in np.linspace(0,2*math.pi,24,endpoint=False): pts.append((x+(1.2*math.cos(a))*(1 if x>113 else 1)*0+ (0.0), z))
from scipy.spatial import ConvexHull
P=np.array(pts+[(110.6,9.2),(116.2,9.2),(116.2,24.8),(110.6,24.8),(109.4,10.4),(109.4,23.6),(117.4,10.4),(117.4,23.6)])
h=ConvexHull(P); poly=[tuple(P[i]) for i in h.vertices]
sk2=cq.Sketch().polygon(poly+[poly[0]])
V['B_druppel']=dict(body=plate(sk2),boss=(122.0,16.0))
# 3 Slank: minimale randen (1.2 mm rond openingen)
sk3=(cq.Sketch().push([(113.4,17.0)]).rect(7.4,14.8)
    .push([(119.4,16.0)]).rect(5.0,6.0,mode='a')
    .push([(122.0,16.0)]).circle(3.75,mode='a')
    )
V['C_slank']=dict(body=plate(sk3),boss=(122.0,16.0))
# 4 Oor links (andere boss-plek)
sk4=(cq.Sketch().push([(113.4,17.0)]).rect(8.2,16.0)
    .push([(107.2,16.0)]).rect(5.2,7.0,mode='a')
    .push([(105.4,16.0)]).circle(4.2,mode='a')
    )
V['D_oor_links']=dict(body=plate(sk4),boss=(105.4,16.0))
for k,v in V.items():
    body=cut_openings(v['body'],v['boss'])
    body=lead_in(body)
    v['body']=body
# ---- casing aanpassingen ----
enc_tm=trimesh.load('/mnt/user-data/uploads/GloboTest_TP4057/Globo_enclosure_USBV.stl'); enc=tomf(enc_tm)
pcb_pocket=wp(Y_FRONT-0.01).center((slot_x0+slot_x1)/2,(slot_z0+slot_z1)/2).rect(slot_x1-slot_x0,slot_z1-slot_z0).extrude(-1.01)  # 78 -> 79
pocket_mf=tomf(tomesh(pcb_pocket.val()))
usbhole=tomf(tomesh(wp(80.6).center(CX,CZ).rect(4.0,9.4).extrude(3.2).edges('|Y').fillet(1.2).val()))
enc=enc-usbhole
casing_R=enc-pocket_mf
def boss_at(x,z):
    b=cq.Workplane(cq.Plane(origin=(x,78.3,z),xDir=(1,0,0),normal=(0,-1,0))).circle(3.25).extrude(78.3-74.7)
    hole=cq.Workplane(cq.Plane(origin=(x,79.2,z),xDir=(1,0,0),normal=(0,-1,0))).circle(1.6).extrude(79.2-74.6)
    return tomf(tomesh(b.val())), tomf(tomesh(hole.val()))
bR,hR=boss_at(122.0,16.0); bL,hL=boss_at(105.4,16.0)
old_boss=tomf(tomesh(cq.Workplane(cq.Plane(origin=(121.0,78.0,16.0),xDir=(1,0,0),normal=(0,-1,0))).circle(3.3).extrude(78.0-74.6).val()))
old_hole_fill=tomf(tomesh(cq.Workplane(cq.Plane(origin=(121.0,79.3,16.0),xDir=(1,0,0),normal=(0,-1,0))).circle(1.7).extrude(79.3-77.95).val()))
base=(enc-pocket_mf-old_boss)+old_hole_fill
casing_R=base+bR-hR
casing_L=base+bL-hL
mod=pickle.load(open(OUT+'/mod.pkl','rb'))['mod']
mod_parts=[]
for p in mod.split(only_watertight=False):
    try:
        x=tomf(p)
        if x.status()==mf.Error.NoError and x.volume()>0: mod_parts.append(x)
    except: pass
mod_mf=mf.Manifold.batch_boolean(mod_parts,mf.OpType.Add)
def conform(sm,cas,d=0.3):
    out=sm
    for dx,dz in [(d,0),(-d,0),(0,d),(0,-d),(d*0.7,d*0.7),(-d*0.7,d*0.7),(d*0.7,-d*0.7),(-d*0.7,-d*0.7)]:
        out=out-cas.translate([dx,0,dz])
    return out - cas
res={}
for cname,cas in [('casing_rechts',casing_R),('casing_links',casing_L)]:
    res[cname+'_vs_board']=round((mod_mf^cas).volume(),3)
for k,v in V.items():
    cas=casing_L if 'links' in k else casing_R
    sm=conform(tomf(tomesh(v['body'].val())),cas)
    v['mf']=sm
    r={'vol_mm3':round(sm.volume(),1),'vs_casing':round((sm^cas).volume(),3),'vs_board':round((sm^mod_mf).volume(),3),
       'gap_board':round(sm.min_gap(mod_mf,2.0),3),'gap_casing':round(sm.min_gap(cas,2.0),3)}
    t_=mf2tm(sm); r['bbox']=[round(float(x),1) for x in t_.extents] if t_.vertices.shape[0] else None
    res[k]=r
    if t_.vertices.shape[0]:
        r['lumps']=len(t_.split(only_watertight=False)); t_.export(OUT+f'/sokkel_{k}.stl')
mf2tm(casing_R).export(OUT+'/casing_voor_A_B_C.stl'); mf2tm(casing_L).export(OUT+'/casing_voor_D_oor_links.stl')
print(json.dumps(res,indent=1))


for k,v in V.items():
    if v['mf'].volume()>0:
        x=v['mf']^mod_mf
        for part in mf2tm(x).split(only_watertight=False):
            if abs(part.volume)>0.001: print(k,' board overlap',np.round(part.bounds,2).tolist(), round(abs(part.volume),3))
    print(k,'cq valid',v['body'].val().isValid(), tomf(tomesh(v['body'].val())).status())
