import re,subprocess
props={}
for l in open("C:/Users/yenni/Documents/.Github Repositorios/Ascensor-de-controles-Electricos/managed_components/lvgl__lvgl/src/misc/lv_style.h"):
    m=re.match(r"\s*LV_STYLE_([A-Z_0-9]+)\s*=\s*(\d+),",l)
    if m: props[int(m.group(2))]=m.group(1).lower()
L=[]
for l in open("code.S",encoding="utf8",errors="ignore"):
    m=re.match(r"([0-9a-f]{8}):\s+([0-9a-f]+)\s+(\S+)\s*(.*)",l)
    if m: L.append((int(m.group(1),16),m.group(3),m.group(4).split("#")[0].strip()))
idx={a:i for i,(a,_,_) in enumerate(L)}
targets=set()
for l in open("ui_calls.txt"): 
    for t in re.findall(r"call (0x[0-9a-f]+)",l): targets.add(int(t,16))
out=[]
for t in sorted(targets):
    i=idx.get(t)
    if i is None: continue
    body=L[i:i+10]
    ids=[]
    tail=None
    for a,mn,ops in body:
        m=re.match(r"x(\d+),x0,(-?\d+)$",ops) if mn in("addi","c.li") else None
        if mn=="c.li":
            m=re.match(r"x(\d+),(-?\d+)",ops)
        if m: ids.append(int(m.group(2)))
        if mn in("c.j","jal") and (mn=="c.j" or ops.startswith("x0")): tail=ops.split(",")[-1]; break
        if mn in("ret","c.jr"): break
    out.append((t,ids,tail))
    print(hex(t),ids,tail, [props.get(x) for x in ids if x in props])
