import pickle,struct
b=open("seg0_40080020.bin","rb").read(); BASE=0x40080020
def rd(a,n): o=a-BASE; return b[o:o+n]
res=pickle.load(open("fonts.pkl","rb"))
names={19:"ui_xs",21:"ui_s",25:"ui_m",30:"ui_l",38:"ui_xl",59:"ui_xxl",66:"ui_digits"}
def arr(name,data,typ="uint8_t"):
    s=f"static const {typ} {name}[] = {{\n"
    for i in range(0,len(data),16): s+="    "+", ".join("0x%02x"%x for x in data[i:i+16])+",\n"
    return s+"};\n\n"
out='''/* GENERADO por tools/reverse/genfont.py a partir del firmware original
 * (backup_esp32p4_16MB.bin). Datos de fuente idénticos byte a byte a los del
 * original: formato lv_font_conv, 4 bpp, kerning por clases. NO editar a mano. */
#include "lvgl.h"
#include "fonts_orig.h"

'''
hdr="#pragma once\n#include \"lvgl.h\"\n\n"
for r in res[:7]:
    n=names[r['lh']]
    gb,gd,cm,cn=r['gb'],r['gd'],r['cm'],r['cn']
    ids=0
    for rs,rl,gs,*_ in r['cmaps']: ids=max(ids,gs+rl)
    # sanity
    last=struct.unpack("<I",rd(gd+(ids-1)*8,4))[0]&0xfffff
    bw,bh=struct.unpack("<BB",rd(gd+(ids-1)*8+4,2))
    need=last+(bw*bh*4+7)//8
    blen=need
    out+=arr(f"{n}_bitmap",rd(gb,blen))
    out+=arr(f"{n}_glyph_dsc",rd(gd,ids*8))
    out+=f"/* glyph_dsc: {ids} entradas de 8 bytes (bitmap_index:20 adv_w:12 box_w box_h ofs_x ofs_y) */\n"
    out+=f"static const lv_font_fmt_txt_cmap_t {n}_cmaps[] = {{\n"
    for rs,rl,gs,ul,gl,ll,t in r['cmaps']:
        out+=f"    {{ .range_start = {rs}, .range_length = {rl}, .glyph_id_start = {gs}, .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY }},\n"
    out+="};\n\n"
    gbd,gdd,cmd,kd,ks,bits=struct.unpack("<IIII",rd(r['dsc'],16))[0:4]+struct.unpack("<HH",rd(r['dsc']+16,4))
    kc=(bits>>13)&1
    kern="NULL"
    if kd:
        pv,lm,rm,lc,rc=struct.unpack("<IIIBB",rd(kd,14))
        out+=arr(f"{n}_kern_pair_values",rd(pv,lc*rc),"int8_t".replace("int8_t","uint8_t"))
        out+=arr(f"{n}_kern_left",rd(lm,ids))
        out+=arr(f"{n}_kern_right",rd(rm,ids))
        out+=f"""static const lv_font_fmt_txt_kern_classes_t {n}_kern = {{
    .class_pair_values = (const int8_t *){n}_kern_pair_values,
    .left_class_mapping = {n}_kern_left,
    .right_class_mapping = {n}_kern_right,
    .left_class_cnt = {lc},
    .right_class_cnt = {rc},
}};

"""
        kern=f"&{n}_kern"
    out+=f"""static lv_font_fmt_txt_glyph_cache_t {n}_cache;
static const lv_font_fmt_txt_dsc_t {n}_dsc = {{
    .glyph_bitmap = {n}_bitmap,
    .glyph_dsc = (const lv_font_fmt_txt_glyph_dsc_t *){n}_glyph_dsc,
    .cmaps = {n}_cmaps,
    .kern_dsc = {kern},
    .kern_scale = {ks},
    .cmap_num = {r['cn']},
    .bpp = {r['bpp']},
    .kern_classes = {kc},
    .bitmap_format = 0,
    .cache = &{n}_cache,
}};

"""
    # lv_font_t fields
    f=struct.unpack("<IIhhBbb",rd(r["addr"],15))
    out+=f"""const lv_font_t font_{n} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {r['lh']},
    .base_line = {r['bl']},
    .subpx = {f[4]},
    .underline_position = {f[5]},
    .underline_thickness = {f[6]},
    .dsc = &{n}_dsc,
    .fallback = NULL,
    .user_data = NULL,
}};

"""
    hdr+=f"extern const lv_font_t font_{n};\n"
open("fonts_orig.c","w",encoding="utf8").write(out); open("fonts_orig.h","w").write(hdr)
print(len(out))
