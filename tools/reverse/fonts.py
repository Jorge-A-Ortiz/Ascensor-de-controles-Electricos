import struct,collections,sys
b=open("seg0_40080020.bin","rb").read()
BASE=0x40080020
def rd(a,n): o=a-BASE; return b[o:o+n]
def u(fmt,a): return struct.unpack(fmt,rd(a,struct.calcsize(fmt)))
fonts=[]
for o in range(0,len(b)-32,4):
    w0,w1,lh,bl,sp,up,ut,pad,dsc,fb,ud=struct.unpack_from("<IIhhBbbBIII",b,o)
    if 0x40000000<=w0<0x40080000 and 0x40000000<=w1<0x40080000 and w0!=w1 and 8<=lh<=140 and 0<=bl<=40 and 0x40080000<=dsc<0x40080000+len(b) and ud==0 and sp==0 and (fb==0 or 0x40080000<=fb<0x40080000+len(b)):
        fonts.append((BASE+o,lh,bl,dsc,fb))
print(len(fonts))
def parse(f):
    a,lh,bl,dsc,fb=f
    gb,gd,cm,kd,ks,bits=struct.unpack("<IIIIHI",rd(dsc,22))
    # fields: kern_scale u16 then bitfields u16: cmap_num:9? lv8: cmap_num:9, bpp:4, kern_classes:1, bitmap_format:2 =16 bits
    ks,bits=struct.unpack("<HH",rd(dsc+16,4))
    cn=bits&0x1ff; bpp=(bits>>9)&0xf; fmt=(bits>>14)&3
    return dict(addr=a,lh=lh,bl=bl,gb=gb,gd=gd,cm=cm,cn=cn,bpp=bpp,fmt=fmt,fb=fb,dsc=dsc)
res=[parse(f) for f in fonts]
for r in res:
    cmaps=[]
    for i in range(r['cn']):
        rs,rl,gs,ul,gl,ll,t=struct.unpack("<IHHIIHB",rd(r['cm']+20*i,19))
        cmaps.append((rs,rl,gs,ul,gl,ll,t))
    r['cmaps']=cmaps
    print(hex(r['addr']),'lh',r['lh'],'bl',r['bl'],'bpp',r['bpp'],'fmt',r['fmt'],'fb',hex(r['fb']),[ (hex(c[0]),c[1],c[6]) for c in cmaps][:6])
import pickle; pickle.dump(res,open("fonts.pkl","wb"))
