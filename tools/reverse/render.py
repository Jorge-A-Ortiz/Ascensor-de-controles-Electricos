import pickle,struct
from PIL import Image
b=open("seg0_40080020.bin","rb").read(); BASE=0x40080020
def rd(a,n): o=a-BASE; return b[o:o+n]
res=pickle.load(open("fonts.pkl","rb"))
def glyph_id(r,cp):
    for i,(rs,rl,gs,ul,gl,ll,t) in enumerate(r['cmaps']):
        if t==2 or t==3 and False: pass
        if rs<=cp<rs+rl:
            rel=cp-rs
            if t==2: return gs+rel  # FORMAT0_TINY
            if t==0: return gs+rel  # format0 full? uses glyph_id_ofs_list
            if t==3: # SPARSE_TINY
                lst=struct.unpack("<%dH"%ll,rd(ul,2*ll))
                if rel in lst: return gs+lst.index(rel)
                return 0
    return 0
def glyph(r,cp):
    g=glyph_id(r,cp)
    if not g: return None
    w,=struct.unpack("<I",rd(r['gd']+g*8,4))
    bi=w&0xfffff; adv=(w>>20)&0xfff
    bw,bh,ox,oy=struct.unpack("<BBbb",rd(r['gd']+g*8+4,4))
    nbytes=(bw*bh*r['bpp']+7)//8
    data=rd(r['gb']+bi,nbytes)
    return adv/16,bw,bh,ox,oy,data
def draw(r,text,scale=1):
    W=int(sum((glyph(r,ord(c)) or (0.5*r['lh'],))[0] for c in text))+20
    img=Image.new("L",(W,r['lh']+10),255)
    x=5.0
    for c in text:
        g=glyph(r,ord(c))
        if not g: x+=r['lh']*.4; continue
        adv,bw,bh,ox,oy,data=g
        bpp=r['bpp']; bit=0
        for yy in range(bh):
            for xx in range(bw):
                bi=bit; v=(data[bi//8]>>(8-bpp-(bi%8)))&((1<<bpp)-1)
                bit+=bpp
                px=int(x)+ox+xx; py=r['lh']-r['bl']-oy-bh+yy+5
                if 0<=px<W and 0<=py<img.height:
                    img.putpixel((px,py),255-int(v*255/((1<<bpp)-1)))
        x+=adv
    return img
if __name__=="__main__":
    rows=[]
    for r in res:
        rows.append(draw(r,"P01 SUBIENDO Piso Ñandú áéíóú ¿Qué? 0123456789" if r['cn']>1 or True else ""))
    H=sum(i.height for i in rows); W=max(i.width for i in rows)
    out=Image.new("L",(W,H),255); y=0
    for i in rows: out.paste(i,(0,y)); y+=i.height
    out.save("fonts.png"); print(out.size)
