import struct,sys,re
rod=open("seg0_40080020.bin","rb").read(); RB=0x40080020
code=open("seg3_40000020.bin","rb").read(); CB=0x40000020
def find_str(s):
    s=s.encode(); out=[];i=0
    while True:
        i=rod.find(s,i)
        if i<0: break
        out.append(RB+i); i+=1
    return out
def xrefs(addr):
    # lui rd,hi ; addi/lw/sw rd,lo  patterns
    hi=(addr+0x800)>>12; lo=addr-(hi<<12)
    res=[]
    for o in range(0,len(code)-4,2):
        w=struct.unpack_from("<I",code,o)[0]
        if w&0x7f==0x37 and (w>>12)&0xfffff==hi&0xfffff:
            rd=(w>>7)&31
            for k in range(2,40,2):
                if o+k+4>len(code): break
                w2=struct.unpack_from("<I",code,o+k)[0]
                if w2&0x7f==0x13 and (w2>>15)&31==rd and ((w2>>20)&0xfff if not (w2>>31) else ((w2>>20)&0xfff)-4096)==lo:
                    res.append(CB+o); break
    return res
if __name__=="__main__":
    for s in sys.argv[1:]:
        for a in find_str(s):
            print(s,hex(a),[hex(x) for x in xrefs(a)])
