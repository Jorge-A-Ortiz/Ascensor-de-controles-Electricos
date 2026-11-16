import re,sys,struct
rod=open("seg0_40080020.bin","rb").read(); RB=0x40080020
def cstr(a):
    o=a-RB
    if 0<=o<len(rod):
        e=rod.find(b"\0",o)
        try: return rod[o:e].decode("utf8")
        except: return None
lines=[]
for l in open("code.S",encoding="utf8",errors="ignore"):
    m=re.match(r"([0-9a-f]{8}):\s+([0-9a-f]+)\s+(\S+)\s*(.*)",l)
    if m: lines.append((int(m.group(1),16),m.group(3),m.group(4).split("#")[0].strip()))
idx={a:i for i,(a,_,_) in enumerate(lines)}
def reg(s): return int(s[1:])
names={}
if len(sys.argv)>3:
    for kv in sys.argv[3:]:
        k,v=kv.split("="); names[int(k,16)]=v
def fmt(v):
    if v is None: return "?"
    if isinstance(v,str): return v
    s=cstr(v) if 0x40080000<=v<0x400f0000 else None
    if s is not None and len(s)>0 and s.isprintable(): return repr(s)
    if v in names: return names[v]
    if v>=0x40000000: return hex(v)
    if v>=0x100: return hex(v)
    return str(v-(1<<32) if v>=0x80000000 else v)
lo=int(sys.argv[1],16); hi=int(sys.argv[2],16)
R=[0]*32; R=[0]+[None]*31
rc=0
for a,mn,ops in lines:
    if a<lo: continue
    if a>hi: break
    p=[x.strip() for x in ops.replace("(",",").replace(")","").split(",")] if ops else []
    try:
        if mn in("lui","c.lui"):
            R[reg(p[0])]=(int(p[1],16 if p[1].startswith("0x") else 10)<<12)&0xffffffff
        elif mn in("addi","c.addi","c.li","c.addi4spn","c.addi16sp") and p:
            if mn=="c.li": R[reg(p[0])]=int(p[1],0)&0xffffffff
            elif mn=="addi":
                s=R[reg(p[1])]
                R[reg(p[0])]=None if s is None or isinstance(s,str) else (s+int(p[2],0))&0xffffffff
            elif mn=="c.addi":
                s=R[reg(p[0])]; R[reg(p[0])]=None if s is None or isinstance(s,str) else (s+int(p[1],0))&0xffffffff
            else: R[reg(p[0])]=None
        elif mn=="c.mv": R[reg(p[0])]=R[reg(p[1])]
        elif mn in("jal","c.jal") and p[0] in("x1",) or (mn=="c.jal"):
            tgt=int(p[-1],16)
            args=[fmt(R[10+i]) for i in range(8)]
            print(f"{a:08x} call {names.get(tgt,hex(tgt))}({', '.join(args)})")
            for r in list(range(5,8))+list(range(10,18))+list(range(28,32)): R[r]=None
            rc+=1; R[10]=f"R{rc}"
        elif mn in("jal","j","c.j"):
            print(f"{a:08x} {mn} {p[-1]}")
        elif mn.startswith("sw") or mn.startswith("c.sw") or mn.startswith("sb") or mn.startswith("sh"):
            if mn!="c.swsp": print(f"{a:08x} STORE {mn} {ops}  val={fmt(R[reg(p[0])])}")
        elif mn.startswith("b") or mn.startswith("c.b"):
            print(f"{a:08x} {mn} {ops}")
        elif mn in("ret","c.jr","jalr","c.jalr"):
            print(f"{a:08x} {mn} {ops}")
            if mn=="c.jalr":
                print("   (indirect call)"); 
        elif mn in("lw","c.lw","c.lwsp","lbu","lhu","lb","lh") and p:
            R[reg(p[0])]=None
        elif p and re.match(r"x\d+$",p[0]) and mn not in("c.swsp",):
            R[reg(p[0])]=None
    except Exception as e:
        pass
